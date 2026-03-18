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
  OOSH_KEY_CTRL_E,
  OOSH_KEY_CTRL_G,
  OOSH_KEY_CTRL_R,
  OOSH_KEY_ESC,
  OOSH_KEY_WORD_LEFT,
  OOSH_KEY_WORD_RIGHT,
  OOSH_KEY_CTRL_K,           /* kill to end of line   (^K, 0x0b) */
  OOSH_KEY_CTRL_U,           /* kill to start of line (^U, 0x15) */
  OOSH_KEY_CTRL_W,           /* kill word backward    (^W, 0x17) */
  OOSH_KEY_CTRL_Y,           /* yank from kill buffer (^Y, 0x19) */
  OOSH_KEY_CTRL_UNDERSCORE   /* undo                  (^_, 0x1f) */
};

typedef enum {
  OOSH_CMATCH_CMD     = 0, /* registered built-in */
  OOSH_CMATCH_FN      = 1, /* shell function */
  OOSH_CMATCH_ALIAS   = 2, /* alias */
  OOSH_CMATCH_PATHCMD = 3, /* external command in $PATH */
  OOSH_CMATCH_FILE    = 4, /* file */
  OOSH_CMATCH_DIR     = 5, /* directory */
  OOSH_CMATCH_VAR     = 6, /* shell variable */
  OOSH_CMATCH_BINDING = 7, /* typed binding (let) */
  OOSH_CMATCH_STAGE   = 8  /* pipeline stage */
} OoshCompletionKind;

typedef struct {
  char items[OOSH_MAX_COMPLETION_MATCHES][OOSH_MAX_PATH];
  int  kinds[OOSH_MAX_COMPLETION_MATCHES];
  int  count;
} OoshCompletionMatches;

/* Kill ring: single-slot buffer shared across calls within one session. */
static char s_kill_buf[OOSH_MAX_LINE];

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

static int append_match_kind(OoshCompletionMatches *matches, const char *value, OoshCompletionKind kind) {
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
  matches->kinds[matches->count] = (int) kind;
  matches->count++;
  return 0;
}

static int append_match(OoshCompletionMatches *matches, const char *value) {
  return append_match_kind(matches, value, OOSH_CMATCH_CMD);
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

/* ── Syntax highlighting (E5-S5-T1) ─────────────────────────────────────── */

#define HL_RESET_CODE  "\033[0m"
#define HL_BOLD_CODE   "\033[1m"
#define HL_GREEN_CODE  "\033[32m"
#define HL_CYAN_CODE   "\033[36m"
#define HL_YELLOW_CODE "\033[33m"
#define HL_GRAY_CODE   "\033[90m"

static const char *s_hl_keywords[] = {
  "if", "then", "else", "elif", "fi",
  "while", "until", "do", "done",
  "for", "in", "case", "esac",
  "function", "endfunction",
  "return", "break", "continue",
  "true", "false",
  NULL
};

static int hl_is_keyword(const char *word, size_t len) {
  const char **kw;
  for (kw = s_hl_keywords; *kw != NULL; ++kw) {
    if (strlen(*kw) == len && memcmp(*kw, word, len) == 0) {
      return 1;
    }
  }
  return 0;
}

static int hl_is_word_char(char c) {
  return !isspace((unsigned char) c) &&
         c != '|' && c != '&' && c != ';' && c != '>' && c != '<' &&
         c != '\'' && c != '"' && c != '$' && c != '#';
}

/* Build an ANSI-colorized copy of buffer[0..len) into out[0..out_size). */
static void highlight_line(const char *buf, size_t len, char *out, size_t out_size) {
  enum { S_NORMAL, S_COMMENT, S_SQ, S_DQ, S_VAR } state = S_NORMAL;
  size_t i;
  size_t out_pos = 0;
  int cur_color = 0;     /* tracks active color id to avoid redundant escapes */
  int at_word_start = 1; /* true when next non-space char opens a new word */

#define HL_PUT(s) do { \
  const char *_s = (s); size_t _l = strlen(_s); \
  if (out_pos + _l < out_size) { memcpy(out + out_pos, _s, _l); out_pos += _l; } \
} while (0)
#define HL_PUTC(c) do { \
  if (out_pos + 1 < out_size) { out[out_pos++] = (c); } \
} while (0)
#define HL_COLOR(code, id) do { \
  if (cur_color != (id)) { HL_PUT(code); cur_color = (id); } \
} while (0)
#define HL_RESET() HL_COLOR(HL_RESET_CODE, 0)

  for (i = 0; i < len; ++i) {
    char c  = buf[i];
    char nx = (i + 1 < len) ? buf[i + 1] : '\0';

    switch (state) {
    case S_COMMENT:
      HL_COLOR(HL_GRAY_CODE, 5);
      HL_PUTC(c);
      break;

    case S_SQ:
      HL_COLOR(HL_GREEN_CODE, 2);
      HL_PUTC(c);
      if (c == '\'') { HL_RESET(); state = S_NORMAL; at_word_start = 0; }
      break;

    case S_DQ:
      HL_COLOR(HL_GREEN_CODE, 2);
      HL_PUTC(c);
      if (c == '"') { HL_RESET(); state = S_NORMAL; at_word_start = 0; }
      break;

    case S_VAR:
      if (isalnum((unsigned char) c) || c == '_') {
        HL_COLOR(HL_CYAN_CODE, 3);
        HL_PUTC(c);
      } else {
        HL_RESET();
        state = S_NORMAL;
        i--; /* reprocess this char in S_NORMAL */
      }
      break;

    case S_NORMAL:
      if (c == '#') {
        state = S_COMMENT;
        HL_COLOR(HL_GRAY_CODE, 5);
        HL_PUTC(c);
      } else if (c == '\'') {
        state = S_SQ;
        HL_COLOR(HL_GREEN_CODE, 2);
        HL_PUTC(c);
        at_word_start = 0;
      } else if (c == '"') {
        state = S_DQ;
        HL_COLOR(HL_GREEN_CODE, 2);
        HL_PUTC(c);
        at_word_start = 0;
      } else if (c == '$') {
        state = S_VAR;
        HL_COLOR(HL_CYAN_CODE, 3);
        HL_PUTC(c);
        at_word_start = 0;
      } else if (c == '-' && nx == '>') {
        /* -> operator */
        HL_RESET();
        HL_COLOR(HL_YELLOW_CODE, 4);
        HL_PUTC(c); i++; HL_PUTC(buf[i]);
        HL_RESET();
        at_word_start = 1;
      } else if (c == '|' || c == '&' || c == ';' || c == '>' || c == '<') {
        HL_RESET();
        HL_COLOR(HL_YELLOW_CODE, 4);
        HL_PUTC(c);
        /* consume second char of two-char operators: |> || && >> */
        if ((c == '|' && (nx == '>' || nx == '|')) ||
            (c == '&' && nx == '&') ||
            (c == '>' && nx == '>')) {
          i++;
          HL_PUTC(buf[i]);
        }
        HL_RESET();
        at_word_start = 1;
      } else if (isspace((unsigned char) c)) {
        HL_RESET();
        HL_PUTC(c);
        /* at_word_start stays as-is: 1 if we're between commands */
      } else {
        /* Regular word character */
        if (at_word_start) {
          /* Look ahead to find end of word, then check for keyword */
          size_t wend = i;
          while (wend < len && hl_is_word_char(buf[wend])) { wend++; }
          if (hl_is_keyword(buf + i, wend - i)) {
            HL_COLOR(HL_BOLD_CODE, 1);
          } else {
            HL_RESET();
          }
          at_word_start = 0;
        }
        HL_PUTC(c);
      }
      break;
    }
  }

  HL_RESET();
  if (out_pos < out_size) { out[out_pos] = '\0'; }
  else if (out_size > 0)  { out[out_size - 1] = '\0'; }

#undef HL_PUT
#undef HL_PUTC
#undef HL_COLOR
#undef HL_RESET
}

/* ── Autosuggestion (E5-S5-T2) ──────────────────────────────────────────── */

/* Find the most-recent history entry that starts with buffer[0..len).
   Writes the suffix (the part after the prefix) into out.
   Returns 1 if a suggestion was found, 0 otherwise. */
static int find_autosuggestion(OoshShell *shell, const char *buffer, size_t len,
                               char *out, size_t out_size) {
  size_t i;

  if (shell == NULL || buffer == NULL || len == 0 || out == NULL || out_size == 0) {
    return 0;
  }

  for (i = shell->history_count; i > 0; --i) {
    const char *entry = shell->history[i - 1];
    size_t entry_len = strlen(entry);
    if (entry_len > len && strncmp(entry, buffer, len) == 0) {
      copy_string(out, out_size, entry + len);
      return 1;
    }
  }
  return 0;
}

/* ── Line renderer ───────────────────────────────────────────────────────── */

/* Redraws the current input line with optional syntax highlighting and
   autosuggestion ghost text.  Pass shell=NULL to render plain text
   (used inside search mode). */
static void redraw_line(const char *prompt, const char *buffer, size_t length,
                        size_t cursor, OoshShell *shell) {
  size_t move_left;
  size_t suggestion_len = 0;

  fputs("\r", stdout);
  fputs(prompt, stdout);

  if (shell != NULL && oosh_line_editor_is_interactive()) {
    /* T1: syntax-highlighted buffer */
    char hl_buf[OOSH_MAX_LINE * 10];
    highlight_line(buffer, length, hl_buf, sizeof(hl_buf));
    fputs(hl_buf, stdout);

    /* T2: autosuggestion ghost text — only when cursor is at end of line */
    if (cursor == length) {
      char suggestion[OOSH_MAX_LINE];
      if (find_autosuggestion(shell, buffer, length, suggestion, sizeof(suggestion))) {
        suggestion_len = strlen(suggestion);
        fputs(HL_GRAY_CODE, stdout);
        fputs(suggestion, stdout);
        fputs(HL_RESET_CODE, stdout);
      }
    }
  } else {
    fputs(buffer, stdout);
  }

  fputs("\033[K", stdout);

  /* Move cursor left past any suggestion and any chars after the cursor. */
  move_left = (length - cursor) + suggestion_len;
  if (move_left > 0) {
    fprintf(stdout, "\033[%zuD", move_left);
  }
  fflush(stdout);
}

static void print_completion_matches(const OoshCompletionMatches *matches) {
  /* Short suffix shown after each match when multiple options are listed.
     Empty string means no indicator. Order must match OoshCompletionKind. */
  static const char *kind_suffix[] = {
    "",       /* CMD     — built-in, obvious from context */
    "(fn)",   /* FN      — user-defined shell function */
    "(@)",    /* ALIAS   — alias */
    "",       /* PATHCMD — external command */
    "",       /* FILE    — file */
    "",       /* DIR     — already has '/' suffix */
    "",       /* VAR     — already has '$' prefix */
    "(let)",  /* BINDING — typed binding */
    ""        /* STAGE   — context already implies stage */
  };
  int i;

  if (matches == NULL || matches->count == 0) {
    return;
  }

  fputc('\n', stdout);
  for (i = 0; i < matches->count; ++i) {
    int kind = matches->kinds[i];
    fputs(matches->items[i], stdout);
    if (kind >= 0 && kind < (int)(sizeof(kind_suffix) / sizeof(kind_suffix[0])) &&
        kind_suffix[kind][0] != '\0') {
      fputs(kind_suffix[kind], stdout);
    }
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

  append_match_kind(matches, value, is_directory ? OOSH_CMATCH_DIR : OOSH_CMATCH_FILE);
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
      append_match_kind(matches, shell->commands[i].name, OOSH_CMATCH_CMD);
    }
  }

  for (i = 0; i < shell->alias_count; ++i) {
    if (starts_with(shell->aliases[i].name, prefix)) {
      append_match_kind(matches, shell->aliases[i].name, OOSH_CMATCH_ALIAS);
    }
  }

  for (i = 0; i < shell->function_count; ++i) {
    if (starts_with(shell->functions[i].name, prefix)) {
      append_match_kind(matches, shell->functions[i].name, OOSH_CMATCH_FN);
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

        append_match_kind(matches, names[i], OOSH_CMATCH_PATHCMD);
      }
    }

    if (*cursor == separator) {
      cursor++;
    }
  }
}

/* T3: shell variables — activated when prefix starts with '$'. */
static void collect_env_var_matches(OoshShell *shell, const char *name_prefix, OoshCompletionMatches *matches) {
  char candidate[OOSH_MAX_PATH];
  size_t i;

  if (shell == NULL || name_prefix == NULL || matches == NULL) {
    return;
  }

  for (i = 0; i < shell->var_count; ++i) {
    if (starts_with(shell->vars[i].name, name_prefix)) {
      snprintf(candidate, sizeof(candidate), "$%s", shell->vars[i].name);
      append_match_kind(matches, candidate, OOSH_CMATCH_VAR);
    }
  }
}

/* T4: typed bindings created with `let` — activated in non-command context. */
static void collect_binding_matches(OoshShell *shell, const char *prefix, OoshCompletionMatches *matches) {
  size_t i;

  if (shell == NULL || prefix == NULL || matches == NULL) {
    return;
  }

  for (i = 0; i < shell->binding_count; ++i) {
    if (starts_with(shell->bindings[i].name, prefix)) {
      append_match_kind(matches, shell->bindings[i].name, OOSH_CMATCH_BINDING);
    }
  }
}

/* T5: detect if cursor is positioned right after a '|>' operator. */
static int is_pipeline_stage_position(const char *buffer, size_t token_start) {
  size_t i = token_start;

  while (i > 0 && isspace((unsigned char) buffer[i - 1])) {
    i--;
  }

  return i >= 2 && buffer[i - 1] == '>' && buffer[i - 2] == '|';
}

/* Known built-in pipeline stages (must stay in sync with apply_pipeline_stage). */
static const char *s_builtin_stages[] = {
  "count", "each", "filter", "first", "from_json", "grep", "join", "lines",
  "reduce", "render", "sort", "split", "take", "to_json", "trim", "where",
  NULL
};

/* T5: pipeline stage completion — activated after '|>'. */
static void collect_stage_matches(OoshShell *shell, const char *prefix, OoshCompletionMatches *matches) {
  const char **s;
  size_t i;

  if (shell == NULL || prefix == NULL || matches == NULL) {
    return;
  }

  for (s = s_builtin_stages; *s != NULL; ++s) {
    if (starts_with(*s, prefix)) {
      append_match_kind(matches, *s, OOSH_CMATCH_STAGE);
    }
  }

  for (i = 0; i < shell->pipeline_stage_count; ++i) {
    if (starts_with(shell->pipeline_stages[i].name, prefix)) {
      append_match_kind(matches, shell->pipeline_stages[i].name, OOSH_CMATCH_STAGE);
    }
  }
}

static void collect_completion_matches(OoshShell *shell, const char *prefix, int command_position, int stage_position, OoshCompletionMatches *matches) {
  if (matches == NULL) {
    return;
  }

  memset(matches, 0, sizeof(*matches));

  /* T3: env var — activated by '$' prefix in any context. */
  if (prefix[0] == '$') {
    collect_env_var_matches(shell, prefix + 1, matches);
    return;
  }

  /* T5: pipeline stage — activated after '|>'. */
  if (stage_position) {
    collect_stage_matches(shell, prefix, matches);
    return;
  }

  if (command_position) {
    collect_registered_command_matches(shell, prefix, matches);
    collect_path_command_matches(shell, prefix, matches);
    if (prefix[0] == '\0') {
      return;
    }
  } else {
    /* T4: typed bindings — in non-command, non-stage positions. */
    collect_binding_matches(shell, prefix, matches);
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
  int stage_position;
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
  stage_position   = is_pipeline_stage_position(buffer, start);

  if (member_context) {
    collect_member_completion_matches(shell, buffer, receiver_start, receiver_end, prefix, &matches);
  } else {
    collect_completion_matches(shell, prefix, command_position, stage_position, &matches);
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
      redraw_line(prompt, buffer, *length, *cursor, shell);
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
        redraw_line(prompt, buffer, *length, *cursor, shell);
      }
      return;
    }
  }

  print_completion_matches(&matches);
  redraw_line(prompt, buffer, *length, *cursor, shell);
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

static size_t word_backward(const char *buf, size_t cursor) {
  size_t pos = cursor;

  while (pos > 0 && isspace((unsigned char) buf[pos - 1])) {
    pos--;
  }
  while (pos > 0 && !isspace((unsigned char) buf[pos - 1])) {
    pos--;
  }
  return pos;
}

static size_t word_forward(const char *buf, size_t length, size_t cursor) {
  size_t pos = cursor;

  while (pos < length && !isspace((unsigned char) buf[pos])) {
    pos++;
  }
  while (pos < length && isspace((unsigned char) buf[pos])) {
    pos++;
  }
  return pos;
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
  if (ch == 7) {
    return OOSH_KEY_CTRL_G;
  }
  if (ch == 11) {
    return OOSH_KEY_CTRL_K;
  }
  if (ch == 18) {
    return OOSH_KEY_CTRL_R;
  }
  if (ch == 21) {
    return OOSH_KEY_CTRL_U;
  }
  if (ch == 23) {
    return OOSH_KEY_CTRL_W;
  }
  if (ch == 25) {
    return OOSH_KEY_CTRL_Y;
  }
  if (ch == 31) {
    return OOSH_KEY_CTRL_UNDERSCORE;
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
      case 115:
        return OOSH_KEY_WORD_LEFT;
      case 116:
        return OOSH_KEY_WORD_RIGHT;
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
  if (ch == 7) {
    return OOSH_KEY_CTRL_G;
  }
  if (ch == 11) {
    return OOSH_KEY_CTRL_K;
  }
  if (ch == 18) {
    return OOSH_KEY_CTRL_R;
  }
  if (ch == 21) {
    return OOSH_KEY_CTRL_U;
  }
  if (ch == 23) {
    return OOSH_KEY_CTRL_W;
  }
  if (ch == 25) {
    return OOSH_KEY_CTRL_Y;
  }
  if (ch == 31) {
    return OOSH_KEY_CTRL_UNDERSCORE;
  }
  if (ch == 27) {
    struct termios save;
    struct termios peek;
    unsigned char b0;
    int n;

    /* Use a brief timeout to distinguish bare ESC from escape sequences. */
    tcgetattr(STDIN_FILENO, &save);
    peek = save;
    peek.c_cc[VMIN] = 0;
    peek.c_cc[VTIME] = 1; /* 100 ms */
    tcsetattr(STDIN_FILENO, TCSANOW, &peek);
    n = (int) read(STDIN_FILENO, &b0, 1);
    tcsetattr(STDIN_FILENO, TCSANOW, &save);

    if (n <= 0) {
      return OOSH_KEY_ESC;
    }

    if (b0 == 'f' || b0 == 'F') {
      return OOSH_KEY_WORD_RIGHT;
    }
    if (b0 == 'b' || b0 == 'B') {
      return OOSH_KEY_WORD_LEFT;
    }

    if (b0 == '[') {
      unsigned char b1;

      if (read(STDIN_FILENO, &b1, 1) != 1) {
        return -1;
      }

      if (b1 >= '0' && b1 <= '9') {
        unsigned char b2;

        if (read(STDIN_FILENO, &b2, 1) != 1) {
          return -1;
        }
        if (b1 == '3' && b2 == '~') {
          return OOSH_KEY_DELETE;
        }
        /* Handle ESC [ 1 ; 5 C/D for Ctrl-arrows (xterm, VTE). */
        if (b1 == '1' && b2 == ';') {
          unsigned char b3;
          unsigned char b4;

          if (read(STDIN_FILENO, &b3, 1) != 1) {
            return -1;
          }
          if (read(STDIN_FILENO, &b4, 1) != 1) {
            return -1;
          }
          if (b3 == '5') {
            if (b4 == 'C') {
              return OOSH_KEY_WORD_RIGHT;
            }
            if (b4 == 'D') {
              return OOSH_KEY_WORD_LEFT;
            }
          }
        }
      } else {
        switch (b1) {
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

static void redraw_search(const char *query, const char *match) {
  char search_prompt[256];

  snprintf(search_prompt, sizeof(search_prompt), "(reverse-i-search)'%s': ", query != NULL ? query : "");
  fputs("\r", stdout);
  fputs(search_prompt, stdout);
  if (match != NULL) {
    fputs(match, stdout);
  }
  fputs("\033[K", stdout);
  fflush(stdout);
}

static int find_history_match(OoshShell *shell, const char *query, size_t from, size_t *found_index) {
  size_t i = from;

  if (shell == NULL || query == NULL || query[0] == '\0' || shell->history_count == 0) {
    return 0;
  }

  while (i > 0) {
    i--;
    if (strstr(shell->history[i], query) != NULL) {
      *found_index = i;
      return 1;
    }
  }
  return 0;
}

/* Returns: 1 = submit (Enter pressed with match loaded), 0 = continue editing, -1 = cancelled. */
static int do_reverse_search(
  OoshShell *shell,
  char *buffer,
  size_t buffer_size,
  size_t *length,
  size_t *cursor,
  size_t *history_index
) {
  char query[OOSH_MAX_LINE];
  size_t query_len = 0;
  size_t search_from;
  size_t match_idx;
  const char *match_str;
  int key;

  query[0] = '\0';
  search_from = shell->history_count;
  match_idx = shell->history_count;
  match_str = NULL;

  redraw_search(query, buffer);

  for (;;) {
    key = read_key();

    if (key == -1 || key == OOSH_KEY_CTRL_G || key == OOSH_KEY_ESC || key == OOSH_KEY_CTRL_C) {
      return -1;
    }

    if (key == OOSH_KEY_ENTER) {
      if (match_str != NULL) {
        copy_string(buffer, buffer_size, match_str);
        *length = strlen(buffer);
        *cursor = *length;
        *history_index = match_idx;
      }
      return 1;
    }

    if (key == OOSH_KEY_CTRL_R) {
      if (query_len > 0) {
        size_t found;

        if (match_idx < shell->history_count) {
          search_from = match_idx;
        }
        if (find_history_match(shell, query, search_from, &found)) {
          match_idx = found;
          match_str = shell->history[found];
          search_from = found;
        } else {
          fputc('\a', stdout);
          fflush(stdout);
        }
      }
      redraw_search(query, match_str);
      continue;
    }

    if (key == OOSH_KEY_BACKSPACE) {
      if (query_len > 0) {
        size_t found;

        query_len--;
        query[query_len] = '\0';
        search_from = shell->history_count;
        match_idx = shell->history_count;
        match_str = NULL;
        if (query_len > 0 && find_history_match(shell, query, search_from, &found)) {
          match_idx = found;
          match_str = shell->history[found];
          search_from = found;
        }
      }
      redraw_search(query, match_str);
      continue;
    }

    if (isprint(key) && key < 128) {
      if (query_len + 1 < sizeof(query)) {
        size_t found;

        query[query_len++] = (char) key;
        query[query_len] = '\0';
        search_from = shell->history_count;
        match_idx = shell->history_count;
        match_str = NULL;
        if (find_history_match(shell, query, search_from, &found)) {
          match_idx = found;
          match_str = shell->history[found];
          search_from = found;
        }
      }
      redraw_search(query, match_str);
      continue;
    }

    /* Any other key: accept match and continue editing. */
    if (match_str != NULL) {
      copy_string(buffer, buffer_size, match_str);
      *length = strlen(buffer);
      *cursor = *length;
      *history_index = match_idx;
    }
    return 0;
  }
}

OoshLineReadStatus oosh_line_editor_read_line(
  OoshShell *shell,
  const char *prompt,
  const char *prompt_continue,
  OoshLineEditorNeedsMoreFn needs_more,
  char *out,
  size_t out_size
) {
  OoshTerminalState terminal_state;
  /* Single-line editing buffer — reused for each continuation line. */
  char line[OOSH_MAX_LINE];
  size_t length = 0;
  size_t cursor = 0;
  size_t history_index;
  char history_scratch[OOSH_MAX_LINE];
  /* Multiline accumulation buffer. */
  char multi[OOSH_MAX_OUTPUT];
  size_t multi_len = 0;
  /* Active prompt for the current editing line. */
  const char *active_prompt;
  int in_continuation = 0;
  /* Single-level undo snapshot (E5-S3-T3). */
  char undo_line[OOSH_MAX_LINE];
  size_t undo_length = 0;
  size_t undo_cursor = 0;
  int undo_valid = 0;

  if (shell == NULL || prompt == NULL || out == NULL || out_size == 0) {
    return OOSH_LINE_READ_ERROR;
  }

  out[0] = '\0';
  line[0] = '\0';
  multi[0] = '\0';
  history_scratch[0] = '\0';
  history_index = shell->history_count;
  active_prompt = prompt;

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
      /* Append current line into the multiline buffer. */
      size_t space = sizeof(multi) - multi_len - 1;

      if (multi_len > 0 && space > 0) {
        multi[multi_len++] = '\n';
        space--;
      }
      if (length > 0 && length <= space) {
        memcpy(multi + multi_len, line, length);
        multi_len += length;
      }
      multi[multi_len] = '\0';

      /* Check if more input is needed. */
      if (needs_more != NULL && multi_len > 0 && needs_more(multi) > 0) {
        /* Enter continuation mode: print newline, show secondary prompt. */
        fputc('\n', stdout);
        active_prompt = (prompt_continue != NULL && prompt_continue[0] != '\0')
                        ? prompt_continue : "... ";
        fputs(active_prompt, stdout);
        fflush(stdout);
        in_continuation = 1;
        /* Reset single-line state for the next line. */
        length = 0;
        cursor = 0;
        line[0] = '\0';
        history_index = shell->history_count;
        history_scratch[0] = '\0';
        continue;
      }

      /* Complete (or no callback): submit. */
      terminal_leave_raw(&terminal_state);
      fputc('\n', stdout);
      copy_string(out, out_size, multi);
      return OOSH_LINE_READ_OK;
    }

    if (key == OOSH_KEY_CTRL_D) {
      if (length == 0 && multi_len == 0) {
        terminal_leave_raw(&terminal_state);
        fputc('\n', stdout);
        return OOSH_LINE_READ_EOF;
      }
      /* In multiline mode: submit whatever we have. */
      if (multi_len > 0) {
        terminal_leave_raw(&terminal_state);
        fputc('\n', stdout);
        copy_string(out, out_size, multi);
        return OOSH_LINE_READ_OK;
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
      redraw_line(active_prompt, line, length, cursor, shell);
      continue;
    }

    if (key == OOSH_KEY_CTRL_E) {
      cursor = length;
      redraw_line(active_prompt, line, length, cursor, shell);
      continue;
    }

    if (key == OOSH_KEY_LEFT) {
      if (cursor > 0) {
        cursor--;
        redraw_line(active_prompt, line, length, cursor, shell);
      }
      continue;
    }

    if (key == OOSH_KEY_RIGHT) {
      if (cursor < length) {
        cursor++;
        redraw_line(active_prompt, line, length, cursor, shell);
      }
      continue;
    }

    if (key == OOSH_KEY_WORD_LEFT) {
      cursor = word_backward(line, cursor);
      redraw_line(active_prompt, line, length, cursor, shell);
      continue;
    }

    if (key == OOSH_KEY_WORD_RIGHT) {
      cursor = word_forward(line, length, cursor);
      redraw_line(active_prompt, line, length, cursor, shell);
      continue;
    }

    if (key == OOSH_KEY_UP) {
      /* History navigation only on the first line. */
      if (!in_continuation) {
        navigate_history(shell, -1, line, sizeof(line), &length, &cursor, &history_index, history_scratch, sizeof(history_scratch));
        redraw_line(active_prompt, line, length, cursor, shell);
      }
      continue;
    }

    if (key == OOSH_KEY_DOWN) {
      if (!in_continuation) {
        navigate_history(shell, 1, line, sizeof(line), &length, &cursor, &history_index, history_scratch, sizeof(history_scratch));
        redraw_line(active_prompt, line, length, cursor, shell);
      }
      continue;
    }

    if (key == OOSH_KEY_CTRL_R) {
      if (!in_continuation) {
        int search_result = do_reverse_search(shell, line, sizeof(line), &length, &cursor, &history_index);

        if (search_result == 1) {
          /* Enter was pressed in search: submit the loaded line. */
          size_t space = sizeof(multi) - multi_len - 1;

          if (multi_len > 0 && space > 0) {
            multi[multi_len++] = '\n';
            space--;
          }
          if (length > 0 && length <= space) {
            memcpy(multi + multi_len, line, length);
            multi_len += length;
          }
          multi[multi_len] = '\0';

          if (needs_more != NULL && multi_len > 0 && needs_more(multi) > 0) {
            fputc('\n', stdout);
            active_prompt = (prompt_continue != NULL && prompt_continue[0] != '\0')
                            ? prompt_continue : "... ";
            fputs(active_prompt, stdout);
            fflush(stdout);
            in_continuation = 1;
            length = 0;
            cursor = 0;
            line[0] = '\0';
            history_index = shell->history_count;
            history_scratch[0] = '\0';
          } else {
            terminal_leave_raw(&terminal_state);
            fputc('\n', stdout);
            copy_string(out, out_size, multi);
            return OOSH_LINE_READ_OK;
          }
        } else {
          /* Cancelled (< 0) or continue editing (== 0): redraw current line. */
          redraw_line(active_prompt, line, length, cursor, shell);
        }
      }
      continue;
    }

    if (key == OOSH_KEY_TAB) {
      handle_completion(shell, active_prompt, line, sizeof(line), &length, &cursor);
      continue;
    }

    if (key == OOSH_KEY_CTRL_K) {
      /* Kill from cursor to end of line; save killed text in kill buffer. */
      if (cursor < length) {
        memcpy(undo_line, line, length + 1); undo_length = length; undo_cursor = cursor; undo_valid = 1;
        copy_string(s_kill_buf, sizeof(s_kill_buf), line + cursor);
        line[cursor] = '\0';
        length = cursor;
        redraw_line(active_prompt, line, length, cursor, shell);
      }
      continue;
    }

    if (key == OOSH_KEY_CTRL_U) {
      /* Kill from start of line to cursor; save killed text in kill buffer. */
      if (cursor > 0) {
        memcpy(undo_line, line, length + 1); undo_length = length; undo_cursor = cursor; undo_valid = 1;
        if (cursor < sizeof(s_kill_buf)) {
          memcpy(s_kill_buf, line, cursor);
          s_kill_buf[cursor] = '\0';
        }
        memmove(line, line + cursor, length - cursor + 1);
        length -= cursor;
        cursor = 0;
        redraw_line(active_prompt, line, length, cursor, shell);
      }
      continue;
    }

    if (key == OOSH_KEY_CTRL_W) {
      /* Kill word backward; save killed text in kill buffer. */
      if (cursor > 0) {
        size_t new_pos = word_backward(line, cursor);
        size_t killed = cursor - new_pos;
        memcpy(undo_line, line, length + 1); undo_length = length; undo_cursor = cursor; undo_valid = 1;
        if (killed < sizeof(s_kill_buf)) {
          memcpy(s_kill_buf, line + new_pos, killed);
          s_kill_buf[killed] = '\0';
        }
        memmove(line + new_pos, line + cursor, length - cursor + 1);
        length -= killed;
        cursor = new_pos;
        redraw_line(active_prompt, line, length, cursor, shell);
      }
      continue;
    }

    if (key == OOSH_KEY_CTRL_Y) {
      /* Yank: insert kill buffer text at cursor. */
      size_t yank_len = strlen(s_kill_buf);
      if (yank_len > 0 && length + yank_len < sizeof(line)) {
        memcpy(undo_line, line, length + 1); undo_length = length; undo_cursor = cursor; undo_valid = 1;
        memmove(line + cursor + yank_len, line + cursor, length - cursor + 1);
        memcpy(line + cursor, s_kill_buf, yank_len);
        length += yank_len;
        cursor += yank_len;
        redraw_line(active_prompt, line, length, cursor, shell);
      }
      continue;
    }

    if (key == OOSH_KEY_CTRL_UNDERSCORE) {
      /* Undo: restore line to state before last modifying key. */
      if (undo_valid) {
        memcpy(line, undo_line, undo_length + 1);
        length = undo_length;
        cursor = undo_cursor;
        undo_valid = 0;
        redraw_line(active_prompt, line, length, cursor, shell);
      }
      continue;
    }

    if (key == OOSH_KEY_BACKSPACE) {
      if (cursor > 0) {
        memcpy(undo_line, line, length + 1); undo_length = length; undo_cursor = cursor; undo_valid = 1;
        memmove(line + cursor - 1, line + cursor, length - cursor + 1);
        length--;
        cursor--;
        redraw_line(active_prompt, line, length, cursor, shell);
      }
      continue;
    }

    if (key == OOSH_KEY_DELETE) {
      if (cursor < length) {
        memcpy(undo_line, line, length + 1); undo_length = length; undo_cursor = cursor; undo_valid = 1;
        memmove(line + cursor, line + cursor + 1, length - cursor);
        length--;
        redraw_line(active_prompt, line, length, cursor, shell);
      }
      continue;
    }

    if (isprint(key)) {
      if (length + 1 >= sizeof(line)) {
        fputc('\a', stdout);
        fflush(stdout);
        continue;
      }

      memcpy(undo_line, line, length + 1); undo_length = length; undo_cursor = cursor; undo_valid = 1;
      memmove(line + cursor + 1, line + cursor, length - cursor + 1);
      line[cursor] = (char) key;
      length++;
      cursor++;
      redraw_line(active_prompt, line, length, cursor, shell);
    }
  }
}
