#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <conio.h>
#include <io.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include "oosh/line_editor.h"
#include "oosh/platform.h"
#include "oosh/shell.h"

#define OOSH_MAX_COMPLETION_MATCHES 64

enum {
  OOSH_KEY_BACKSPACE = 1000,
  OOSH_KEY_DELETE,
  OOSH_KEY_LEFT,
  OOSH_KEY_RIGHT,
  OOSH_KEY_UP,
  OOSH_KEY_DOWN,
  OOSH_KEY_TAB,
  OOSH_KEY_ENTER,
  OOSH_KEY_CTRL_A,
  OOSH_KEY_CTRL_C,
  OOSH_KEY_CTRL_D,
  OOSH_KEY_CTRL_E
};

typedef struct {
  char items[OOSH_MAX_COMPLETION_MATCHES][OOSH_MAX_PATH];
  int count;
} OoshCompletionMatches;

static void copy_string(char *dest, size_t dest_size, const char *src) {
  if (dest_size == 0) {
    return;
  }

  snprintf(dest, dest_size, "%s", src == NULL ? "" : src);
}

static int starts_with(const char *text, const char *prefix) {
  size_t prefix_len;

  if (text == NULL || prefix == NULL) {
    return 0;
  }

  prefix_len = strlen(prefix);
  return strncmp(text, prefix, prefix_len) == 0;
}

static int append_match(OoshCompletionMatches *matches, const char *value) {
  int i;

  if (matches == NULL || value == NULL || value[0] == '\0') {
    return 1;
  }

  for (i = 0; i < matches->count; ++i) {
    if (strcmp(matches->items[i], value) == 0) {
      return 0;
    }
  }

  if (matches->count >= OOSH_MAX_COMPLETION_MATCHES) {
    return 1;
  }

  copy_string(matches->items[matches->count], sizeof(matches->items[matches->count]), value);
  matches->count++;
  return 0;
}

static int is_token_delimiter(char c) {
  return isspace((unsigned char) c) || c == '|' || c == '&' || c == ';' || c == '<' || c == '>' || c == '(' || c == ')' || c == ',' || c == '=';
}

static int is_member_char(char c) {
  return isalnum((unsigned char) c) || c == '_';
}

static size_t find_token_start(const char *buffer, size_t cursor) {
  size_t start = cursor;

  while (start > 0 && !is_token_delimiter(buffer[start - 1])) {
    start--;
  }

  return start;
}

static int is_command_position(const char *buffer, size_t token_start) {
  size_t i = token_start;

  while (i > 0) {
    i--;
    if (isspace((unsigned char) buffer[i])) {
      continue;
    }
    return buffer[i] == '|' || buffer[i] == '&' || buffer[i] == ';';
  }

  return 1;
}

static int find_object_member_context(
  const char *buffer,
  size_t cursor,
  size_t *out_receiver_start,
  size_t *out_receiver_end,
  size_t *out_member_start
) {
  size_t member_start;
  size_t scan;
  size_t receiver_end;
  size_t receiver_start;

  if (buffer == NULL || out_receiver_start == NULL || out_receiver_end == NULL || out_member_start == NULL) {
    return 0;
  }

  member_start = cursor;
  while (member_start > 0 && is_member_char(buffer[member_start - 1])) {
    member_start--;
  }

  scan = member_start;
  while (scan > 0 && isspace((unsigned char) buffer[scan - 1])) {
    scan--;
  }

  if (scan < 2 || buffer[scan - 1] != '>' || buffer[scan - 2] != '-') {
    return 0;
  }

  receiver_end = scan - 2;
  while (receiver_end > 0 && isspace((unsigned char) buffer[receiver_end - 1])) {
    receiver_end--;
  }
  if (receiver_end == 0) {
    return 0;
  }

  receiver_start = receiver_end;
  while (receiver_start > 0 && !is_token_delimiter(buffer[receiver_start - 1])) {
    receiver_start--;
  }
  if (receiver_end <= receiver_start) {
    return 0;
  }

  *out_receiver_start = receiver_start;
  *out_receiver_end = receiver_end;
  *out_member_start = member_start;
  return 1;
}

static void redraw_line(const char *prompt, const char *buffer, size_t length, size_t cursor) {
  size_t move_left = length > cursor ? length - cursor : 0;

  fputs("\r", stdout);
  fputs(prompt, stdout);
  fputs(buffer, stdout);
  fputs("\033[K", stdout);
  if (move_left > 0) {
    fprintf(stdout, "\033[%zuD", move_left);
  }
  fflush(stdout);
}

static void print_completion_matches(const OoshCompletionMatches *matches) {
  int i;

  if (matches == NULL || matches->count == 0) {
    return;
  }

  fputc('\n', stdout);
  for (i = 0; i < matches->count; ++i) {
    fputs(matches->items[i], stdout);
    if (i + 1 < matches->count) {
      fputs("  ", stdout);
    }
  }
  fputc('\n', stdout);
}

static size_t shared_prefix_length(const OoshCompletionMatches *matches) {
  size_t length = 0;
  int i;

  if (matches == NULL || matches->count == 0) {
    return 0;
  }

  length = strlen(matches->items[0]);
  for (i = 1; i < matches->count; ++i) {
    size_t j = 0;

    while (j < length && matches->items[0][j] != '\0' && matches->items[i][j] != '\0' &&
           matches->items[0][j] == matches->items[i][j]) {
      j++;
    }
    length = j;
  }

  return length;
}

static int replace_range(char *buffer, size_t buffer_size, size_t *length, size_t *cursor, size_t start, size_t end, const char *replacement) {
  size_t replacement_len;
  size_t tail_len;

  if (buffer == NULL || buffer_size == 0 || length == NULL || cursor == NULL || replacement == NULL || end < start) {
    return 1;
  }

  replacement_len = strlen(replacement);
  tail_len = *length - end;

  if (start + replacement_len + tail_len >= buffer_size) {
    return 1;
  }

  memmove(buffer + start + replacement_len, buffer + end, tail_len + 1);
  memcpy(buffer + start, replacement, replacement_len);
  *length = start + replacement_len + tail_len;
  *cursor = start + replacement_len;
  return 0;
}

static void expand_home_prefix(const OoshShell *shell, const char *raw, char *out, size_t out_size) {
  const char *home;

  if (raw == NULL || out == NULL || out_size == 0) {
    return;
  }

  if (raw[0] == '~' && (raw[1] == '\0' || raw[1] == '/' || raw[1] == '\\')) {
    home = oosh_shell_get_var(shell, "HOME");
    if (home != NULL && home[0] != '\0') {
      snprintf(out, out_size, "%s%s", home, raw + 1);
      return;
    }
  }

  copy_string(out, out_size, raw);
}

static void split_path_prefix(
  const OoshShell *shell,
  const char *prefix,
  char *raw_dir_prefix,
  size_t raw_dir_prefix_size,
  char *lookup_dir,
  size_t lookup_dir_size,
  char *base_prefix,
  size_t base_prefix_size
) {
  const char *slash = strrchr(prefix, '/');
  const char *backslash = strrchr(prefix, '\\');
  const char *separator = slash;
  char expanded_dir[OOSH_MAX_PATH];

  if (backslash != NULL && (separator == NULL || backslash > separator)) {
    separator = backslash;
  }

  if (separator == NULL) {
    raw_dir_prefix[0] = '\0';
    copy_string(base_prefix, base_prefix_size, prefix);
    copy_string(lookup_dir, lookup_dir_size, shell->cwd);
    return;
  }

  {
    size_t dir_len = (size_t) (separator - prefix + 1);

    if (dir_len >= raw_dir_prefix_size) {
      raw_dir_prefix[0] = '\0';
      base_prefix[0] = '\0';
      lookup_dir[0] = '\0';
      return;
    }

    memcpy(raw_dir_prefix, prefix, dir_len);
    raw_dir_prefix[dir_len] = '\0';
    copy_string(base_prefix, base_prefix_size, separator + 1);
  }

  expand_home_prefix(shell, raw_dir_prefix, expanded_dir, sizeof(expanded_dir));
  if (oosh_platform_resolve_path(shell->cwd, expanded_dir, lookup_dir, lookup_dir_size) != 0) {
    lookup_dir[0] = '\0';
  }
}

static void append_path_match(
  OoshCompletionMatches *matches,
  const char *raw_dir_prefix,
  const char *name,
  int is_directory
) {
  char value[OOSH_MAX_PATH];

  if (raw_dir_prefix[0] == '\0') {
    snprintf(value, sizeof(value), "%s%s", name, is_directory ? oosh_platform_path_separator() : "");
  } else {
    snprintf(value, sizeof(value), "%s%s%s", raw_dir_prefix, name, is_directory ? oosh_platform_path_separator() : "");
  }

  append_match(matches, value);
}

static void collect_file_matches(OoshShell *shell, const char *prefix, OoshCompletionMatches *matches) {
  char raw_dir_prefix[OOSH_MAX_PATH];
  char lookup_dir[OOSH_MAX_PATH];
  char base_prefix[OOSH_MAX_NAME];
  char names[OOSH_MAX_COMPLETION_MATCHES][OOSH_MAX_PATH];
  size_t count = 0;
  size_t i;

  if (shell == NULL || prefix == NULL || matches == NULL) {
    return;
  }

  split_path_prefix(shell, prefix, raw_dir_prefix, sizeof(raw_dir_prefix), lookup_dir, sizeof(lookup_dir), base_prefix, sizeof(base_prefix));
  if (lookup_dir[0] == '\0' || oosh_platform_list_children_names(lookup_dir, names, OOSH_MAX_COMPLETION_MATCHES, &count) != 0) {
    return;
  }

  for (i = 0; i < count; ++i) {
    OoshPlatformFileInfo info;
    char candidate_path[OOSH_MAX_PATH];

    if (!starts_with(names[i], base_prefix)) {
      continue;
    }

    if (oosh_platform_resolve_path(lookup_dir, names[i], candidate_path, sizeof(candidate_path)) != 0 ||
        oosh_platform_stat(candidate_path, &info) != 0) {
      continue;
    }

    append_path_match(matches, raw_dir_prefix, names[i], info.is_directory);
  }
}

static void collect_registered_command_matches(OoshShell *shell, const char *prefix, OoshCompletionMatches *matches) {
  size_t i;

  if (shell == NULL || prefix == NULL || matches == NULL) {
    return;
  }

  for (i = 0; i < shell->command_count; ++i) {
    if (starts_with(shell->commands[i].name, prefix)) {
      append_match(matches, shell->commands[i].name);
    }
  }

  for (i = 0; i < shell->alias_count; ++i) {
    if (starts_with(shell->aliases[i].name, prefix)) {
      append_match(matches, shell->aliases[i].name);
    }
  }

  for (i = 0; i < shell->function_count; ++i) {
    if (starts_with(shell->functions[i].name, prefix)) {
      append_match(matches, shell->functions[i].name);
    }
  }
}

static void collect_path_command_matches(OoshShell *shell, const char *prefix, OoshCompletionMatches *matches) {
  const char *path_env;
  const char *cursor;
  char separator =
#ifdef _WIN32
    ';';
#else
    ':';
#endif

  if (shell == NULL || prefix == NULL || matches == NULL) {
    return;
  }

  path_env = oosh_shell_get_var(shell, "PATH");
  if (path_env == NULL || path_env[0] == '\0') {
    return;
  }

  cursor = path_env;
  while (*cursor != '\0') {
    char segment[OOSH_MAX_PATH];
    char resolved[OOSH_MAX_PATH];
    char names[OOSH_MAX_COMPLETION_MATCHES][OOSH_MAX_PATH];
    size_t segment_len = 0;
    size_t count = 0;
    size_t i;

    while (*cursor != '\0' && *cursor != separator) {
      if (segment_len + 1 < sizeof(segment)) {
        segment[segment_len++] = *cursor;
      }
      cursor++;
    }
    segment[segment_len] = '\0';

    if (segment[0] == '\0') {
      copy_string(segment, sizeof(segment), ".");
    }

    if (oosh_platform_resolve_path(shell->cwd, segment, resolved, sizeof(resolved)) == 0 &&
        oosh_platform_list_children_names(resolved, names, OOSH_MAX_COMPLETION_MATCHES, &count) == 0) {
      for (i = 0; i < count; ++i) {
        OoshPlatformFileInfo info;
        char candidate[OOSH_MAX_PATH];

        if (!starts_with(names[i], prefix)) {
          continue;
        }

        if (oosh_platform_resolve_path(resolved, names[i], candidate, sizeof(candidate)) != 0 ||
            oosh_platform_stat(candidate, &info) != 0 ||
            info.is_directory) {
          continue;
        }

        append_match(matches, names[i]);
      }
    }

    if (*cursor == separator) {
      cursor++;
    }
  }
}

static void collect_completion_matches(OoshShell *shell, const char *prefix, int command_position, OoshCompletionMatches *matches) {
  if (matches == NULL) {
    return;
  }

  memset(matches, 0, sizeof(*matches));

  if (command_position) {
    collect_registered_command_matches(shell, prefix, matches);
    collect_path_command_matches(shell, prefix, matches);
    if (prefix[0] == '\0') {
      return;
    }
  }

  collect_file_matches(shell, prefix, matches);
}

static void collect_member_completion_matches(
  OoshShell *shell,
  const char *buffer,
  size_t receiver_start,
  size_t receiver_end,
  const char *prefix,
  OoshCompletionMatches *matches
) {
  char receiver_text[OOSH_MAX_LINE];
  size_t receiver_len;
  size_t count = 0;

  if (shell == NULL || buffer == NULL || prefix == NULL || matches == NULL || receiver_end < receiver_start) {
    return;
  }

  memset(matches, 0, sizeof(*matches));
  receiver_len = receiver_end - receiver_start;
  if (receiver_len == 0 || receiver_len >= sizeof(receiver_text)) {
    return;
  }

  memcpy(receiver_text, buffer + receiver_start, receiver_len);
  receiver_text[receiver_len] = '\0';

  if (oosh_shell_collect_member_completions(shell, receiver_text, prefix, matches->items, OOSH_MAX_COMPLETION_MATCHES, &count) != 0) {
    return;
  }

  matches->count = (int) count;
}

static void handle_completion(
  OoshShell *shell,
  const char *prompt,
  char *buffer,
  size_t buffer_size,
  size_t *length,
  size_t *cursor
) {
  size_t start;
  size_t receiver_start = 0;
  size_t receiver_end = 0;
  char prefix[OOSH_MAX_PATH];
  OoshCompletionMatches matches;
  size_t prefix_len;
  int command_position;
  int member_context;

  if (shell == NULL || prompt == NULL || buffer == NULL || length == NULL || cursor == NULL) {
    return;
  }

  member_context = find_object_member_context(buffer, *cursor, &receiver_start, &receiver_end, &start);
  if (!member_context) {
    start = find_token_start(buffer, *cursor);
  }
  prefix_len = *cursor - start;
  if (prefix_len >= sizeof(prefix)) {
    return;
  }

  memcpy(prefix, buffer + start, prefix_len);
  prefix[prefix_len] = '\0';
  command_position = is_command_position(buffer, start);

  if (member_context) {
    collect_member_completion_matches(shell, buffer, receiver_start, receiver_end, prefix, &matches);
  } else {
    collect_completion_matches(shell, prefix, command_position, &matches);
  }
  if (matches.count == 0) {
    fputc('\a', stdout);
    fflush(stdout);
    return;
  }

  if (matches.count == 1) {
    char replacement[OOSH_MAX_PATH];

    copy_string(replacement, sizeof(replacement), matches.items[0]);
    if (!member_context) {
      size_t replacement_len = strlen(replacement);

      if (replacement_len > 0) {
        char last_char = replacement[replacement_len - 1];

        if (last_char != '/' && last_char != '\\' && replacement_len + 1 < sizeof(replacement)) {
          replacement[replacement_len] = ' ';
          replacement[replacement_len + 1] = '\0';
        }
      }
    }

    if (replace_range(buffer, buffer_size, length, cursor, start, *cursor, replacement) == 0) {
      redraw_line(prompt, buffer, *length, *cursor);
    }
    return;
  }

  {
    size_t shared = shared_prefix_length(&matches);

    if (shared > prefix_len) {
      char replacement[OOSH_MAX_PATH];

      if (shared >= sizeof(replacement)) {
        return;
      }
      memcpy(replacement, matches.items[0], shared);
      replacement[shared] = '\0';
      if (replace_range(buffer, buffer_size, length, cursor, start, *cursor, replacement) == 0) {
        redraw_line(prompt, buffer, *length, *cursor);
      }
      return;
    }
  }

  print_completion_matches(&matches);
  redraw_line(prompt, buffer, *length, *cursor);
}

static void apply_history_entry(char *buffer, size_t buffer_size, size_t *length, size_t *cursor, const char *entry) {
  copy_string(buffer, buffer_size, entry);
  *length = strlen(buffer);
  *cursor = *length;
}

static void navigate_history(
  OoshShell *shell,
  int direction,
  char *buffer,
  size_t buffer_size,
  size_t *length,
  size_t *cursor,
  size_t *history_index,
  char *scratch,
  size_t scratch_size
) {
  if (shell == NULL || buffer == NULL || length == NULL || cursor == NULL || history_index == NULL || scratch == NULL || scratch_size == 0) {
    return;
  }

  if (shell->history_count == 0) {
    return;
  }

  if (direction < 0) {
    if (*history_index == 0) {
      return;
    }
    if (*history_index == shell->history_count) {
      copy_string(scratch, scratch_size, buffer);
    }
    (*history_index)--;
    apply_history_entry(buffer, buffer_size, length, cursor, shell->history[*history_index]);
    return;
  }

  if (*history_index >= shell->history_count) {
    return;
  }

  (*history_index)++;
  if (*history_index == shell->history_count) {
    apply_history_entry(buffer, buffer_size, length, cursor, scratch);
  } else {
    apply_history_entry(buffer, buffer_size, length, cursor, shell->history[*history_index]);
  }
}

#ifdef _WIN32
typedef struct {
  HANDLE input_handle;
  DWORD original_mode;
  int active;
} OoshTerminalState;

static int terminal_enter_raw(OoshTerminalState *state) {
  DWORD mode;

  if (state == NULL) {
    return 1;
  }

  memset(state, 0, sizeof(*state));
  state->input_handle = GetStdHandle(STD_INPUT_HANDLE);
  if (state->input_handle == INVALID_HANDLE_VALUE || !GetConsoleMode(state->input_handle, &mode)) {
    return 1;
  }

  state->original_mode = mode;
  mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
  if (!SetConsoleMode(state->input_handle, mode)) {
    return 1;
  }

  state->active = 1;
  return 0;
}

static void terminal_leave_raw(OoshTerminalState *state) {
  if (state != NULL && state->active) {
    SetConsoleMode(state->input_handle, state->original_mode);
    state->active = 0;
  }
}

static int read_key(void) {
  int ch = _getch();

  if (ch == '\r') {
    return OOSH_KEY_ENTER;
  }
  if (ch == '\t') {
    return OOSH_KEY_TAB;
  }
  if (ch == 8) {
    return OOSH_KEY_BACKSPACE;
  }
  if (ch == 1) {
    return OOSH_KEY_CTRL_A;
  }
  if (ch == 3) {
    return OOSH_KEY_CTRL_C;
  }
  if (ch == 4) {
    return OOSH_KEY_CTRL_D;
  }
  if (ch == 5) {
    return OOSH_KEY_CTRL_E;
  }
  if (ch == 0 || ch == 224) {
    int ext = _getch();

    switch (ext) {
      case 72:
        return OOSH_KEY_UP;
      case 75:
        return OOSH_KEY_LEFT;
      case 77:
        return OOSH_KEY_RIGHT;
      case 80:
        return OOSH_KEY_DOWN;
      case 83:
        return OOSH_KEY_DELETE;
      default:
        return -1;
    }
  }

  return ch;
}

int oosh_line_editor_is_interactive(void) {
  return _isatty(_fileno(stdin)) && _isatty(_fileno(stdout));
}
#else
typedef struct {
  struct termios original;
  int active;
} OoshTerminalState;

static int terminal_enter_raw(OoshTerminalState *state) {
  struct termios raw;

  if (state == NULL || tcgetattr(STDIN_FILENO, &state->original) != 0) {
    return 1;
  }

  raw = state->original;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= CS8;
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
    return 1;
  }

  state->active = 1;
  return 0;
}

static void terminal_leave_raw(OoshTerminalState *state) {
  if (state != NULL && state->active) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &state->original);
    state->active = 0;
  }
}

static int read_key(void) {
  unsigned char ch = 0;

  if (read(STDIN_FILENO, &ch, 1) != 1) {
    return -1;
  }

  if (ch == '\r' || ch == '\n') {
    return OOSH_KEY_ENTER;
  }
  if (ch == '\t') {
    return OOSH_KEY_TAB;
  }
  if (ch == 127 || ch == 8) {
    return OOSH_KEY_BACKSPACE;
  }
  if (ch == 1) {
    return OOSH_KEY_CTRL_A;
  }
  if (ch == 3) {
    return OOSH_KEY_CTRL_C;
  }
  if (ch == 4) {
    return OOSH_KEY_CTRL_D;
  }
  if (ch == 5) {
    return OOSH_KEY_CTRL_E;
  }
  if (ch == 27) {
    unsigned char sequence[3];

    if (read(STDIN_FILENO, &sequence[0], 1) != 1) {
      return -1;
    }
    if (read(STDIN_FILENO, &sequence[1], 1) != 1) {
      return -1;
    }

    if (sequence[0] == '[') {
      if (sequence[1] >= '0' && sequence[1] <= '9') {
        if (read(STDIN_FILENO, &sequence[2], 1) != 1) {
          return -1;
        }
        if (sequence[1] == '3' && sequence[2] == '~') {
          return OOSH_KEY_DELETE;
        }
      } else {
        switch (sequence[1]) {
          case 'A':
            return OOSH_KEY_UP;
          case 'B':
            return OOSH_KEY_DOWN;
          case 'C':
            return OOSH_KEY_RIGHT;
          case 'D':
            return OOSH_KEY_LEFT;
          default:
            break;
        }
      }
    }

    return -1;
  }

  return ch;
}

int oosh_line_editor_is_interactive(void) {
  return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
}
#endif

OoshLineReadStatus oosh_line_editor_read_line(OoshShell *shell, const char *prompt, char *out, size_t out_size) {
  OoshTerminalState terminal_state;
  size_t length = 0;
  size_t cursor = 0;
  size_t history_index;
  char history_scratch[OOSH_MAX_LINE];

  if (shell == NULL || prompt == NULL || out == NULL || out_size == 0) {
    return OOSH_LINE_READ_ERROR;
  }

  out[0] = '\0';
  history_scratch[0] = '\0';
  history_index = shell->history_count;

  if (terminal_enter_raw(&terminal_state) != 0) {
    return OOSH_LINE_READ_ERROR;
  }

  while (1) {
    int key = read_key();

    if (key == -1) {
      terminal_leave_raw(&terminal_state);
      return OOSH_LINE_READ_ERROR;
    }

    if (key == OOSH_KEY_ENTER) {
      terminal_leave_raw(&terminal_state);
      fputc('\n', stdout);
      return OOSH_LINE_READ_OK;
    }

    if (key == OOSH_KEY_CTRL_D) {
      terminal_leave_raw(&terminal_state);
      if (length == 0) {
        fputc('\n', stdout);
        return OOSH_LINE_READ_EOF;
      }
      continue;
    }

    if (key == OOSH_KEY_CTRL_C) {
      terminal_leave_raw(&terminal_state);
      out[0] = '\0';
      fputs("^C\n", stdout);
      return OOSH_LINE_READ_OK;
    }

    if (key == OOSH_KEY_CTRL_A) {
      cursor = 0;
      redraw_line(prompt, out, length, cursor);
      continue;
    }

    if (key == OOSH_KEY_CTRL_E) {
      cursor = length;
      redraw_line(prompt, out, length, cursor);
      continue;
    }

    if (key == OOSH_KEY_LEFT) {
      if (cursor > 0) {
        cursor--;
        redraw_line(prompt, out, length, cursor);
      }
      continue;
    }

    if (key == OOSH_KEY_RIGHT) {
      if (cursor < length) {
        cursor++;
        redraw_line(prompt, out, length, cursor);
      }
      continue;
    }

    if (key == OOSH_KEY_UP) {
      navigate_history(shell, -1, out, out_size, &length, &cursor, &history_index, history_scratch, sizeof(history_scratch));
      redraw_line(prompt, out, length, cursor);
      continue;
    }

    if (key == OOSH_KEY_DOWN) {
      navigate_history(shell, 1, out, out_size, &length, &cursor, &history_index, history_scratch, sizeof(history_scratch));
      redraw_line(prompt, out, length, cursor);
      continue;
    }

    if (key == OOSH_KEY_TAB) {
      handle_completion(shell, prompt, out, out_size, &length, &cursor);
      continue;
    }

    if (key == OOSH_KEY_BACKSPACE) {
      if (cursor > 0) {
        memmove(out + cursor - 1, out + cursor, length - cursor + 1);
        length--;
        cursor--;
        redraw_line(prompt, out, length, cursor);
      }
      continue;
    }

    if (key == OOSH_KEY_DELETE) {
      if (cursor < length) {
        memmove(out + cursor, out + cursor + 1, length - cursor);
        length--;
        redraw_line(prompt, out, length, cursor);
      }
      continue;
    }

    if (isprint(key)) {
      if (length + 1 >= out_size) {
        fputc('\a', stdout);
        fflush(stdout);
        continue;
      }

      memmove(out + cursor + 1, out + cursor, length - cursor + 1);
      out[cursor] = (char) key;
      length++;
      cursor++;
      redraw_line(prompt, out, length, cursor);
    }
  }
}
