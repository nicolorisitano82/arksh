#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "arksh/executor.h"
#include "arksh/line_editor.h"
#include "arksh/lexer.h"
#include "arksh/perf.h"
#include "arksh/parser.h"
#include "arksh/platform.h"
#include "arksh/prompt.h"
#include "arksh/shell.h"

/* E1-S6-T1: per-signal pending flags set by the async signal handler. */
static volatile sig_atomic_t s_pending_traps[ARKSH_TRAP_COUNT];

/* Mapping from ArkshTrapKind to POSIX signal number (0 for pseudo-signals). */
static const struct { const char *name; ArkshTrapKind kind; int signum; }
s_trap_map[] = {
  { "EXIT",  ARKSH_TRAP_EXIT,  0        },
  { "ERR",   ARKSH_TRAP_ERR,   0        },
#ifndef _WIN32
  { "HUP",   ARKSH_TRAP_HUP,   SIGHUP   },
  { "INT",   ARKSH_TRAP_INT,   SIGINT   },
  { "QUIT",  ARKSH_TRAP_QUIT,  SIGQUIT  },
  { "ILL",   ARKSH_TRAP_ILL,   SIGILL   },
  { "ABRT",  ARKSH_TRAP_ABRT,  SIGABRT  },
  { "FPE",   ARKSH_TRAP_FPE,   SIGFPE   },
  { "SEGV",  ARKSH_TRAP_SEGV,  SIGSEGV  },
  { "PIPE",  ARKSH_TRAP_PIPE,  SIGPIPE  },
  { "ALRM",  ARKSH_TRAP_ALRM,  SIGALRM  },
  { "TERM",  ARKSH_TRAP_TERM,  SIGTERM  },
  { "USR1",  ARKSH_TRAP_USR1,  SIGUSR1  },
  { "USR2",  ARKSH_TRAP_USR2,  SIGUSR2  },
  { "CHLD",  ARKSH_TRAP_CHLD,  SIGCHLD  },
  { "TSTP",  ARKSH_TRAP_TSTP,  SIGTSTP  },
  { "TTIN",  ARKSH_TRAP_TTIN,  SIGTTIN  },
  { "TTOU",  ARKSH_TRAP_TTOU,  SIGTTOU  },
#endif
  { NULL,    ARKSH_TRAP_COUNT, 0        }
};

static ArkshValue *allocate_shell_value(char *out, size_t out_size) {
  ArkshValue *value = (ArkshValue *) calloc(1, sizeof(*value));

  if (value == NULL && out != NULL && out_size > 0) {
    snprintf(out, out_size, "out of memory");
  }
  return value;
}

static ArkshValue *allocate_runtime_value(char *error, size_t error_size, const char *label);
static int build_class_property_list(const ArkshShell *shell, const char *class_name, ArkshValue *out_value, char *out, size_t out_size);
static int build_class_method_list(const ArkshShell *shell, const char *class_name, ArkshValue *out_value, char *out, size_t out_size);

static void copy_string(char *dest, size_t dest_size, const char *src) {
  if (dest_size == 0) {
    return;
  }

  snprintf(dest, dest_size, "%s", src == NULL ? "" : src);
}

static int append_text(char *dest, size_t dest_size, const char *src) {
  size_t current_len;
  size_t src_len;

  if (dest == NULL || dest_size == 0 || src == NULL) {
    return 1;
  }

  current_len = strlen(dest);
  src_len = strlen(src);
  if (current_len + src_len >= dest_size) {
    return 1;
  }

  memcpy(dest + current_len, src, src_len + 1);
  return 0;
}

static int append_slice(char *dest, size_t dest_size, const char *src, size_t start, size_t end) {
  size_t current_len;
  size_t slice_len;

  if (dest == NULL || dest_size == 0 || src == NULL || end < start) {
    return 1;
  }

  current_len = strlen(dest);
  slice_len = end - start;
  if (current_len + slice_len >= dest_size) {
    return 1;
  }

  memcpy(dest + current_len, src + start, slice_len);
  dest[current_len + slice_len] = '\0';
  return 0;
}

static int append_output_line(char *dest, size_t dest_size, const char *line) {
  if (dest == NULL || dest_size == 0 || line == NULL || line[0] == '\0') {
    return 0;
  }

  if (dest[0] != '\0' && append_text(dest, dest_size, "\n") != 0) {
    return 1;
  }

  return append_text(dest, dest_size, line);
}

static void write_buffer(const char *text) {
  size_t len;

  if (text == NULL || text[0] == '\0') {
    return;
  }

  len = strlen(text);
  fputs(text, stdout);
  if (len == 0 || text[len - 1] != '\n') {
    fputc('\n', stdout);
  }
}

static int is_valid_identifier(const char *name) {
  size_t i;

  if (name == NULL || name[0] == '\0') {
    return 0;
  }

  if (!(isalpha((unsigned char) name[0]) || name[0] == '_')) {
    return 0;
  }

  for (i = 1; name[i] != '\0'; ++i) {
    if (!(isalnum((unsigned char) name[i]) || name[i] == '_')) {
      return 0;
    }
  }

  return 1;
}

static int is_valid_alias_name(const char *name) {
  size_t i;

  if (name == NULL || name[0] == '\0') {
    return 0;
  }

  for (i = 0; name[i] != '\0'; ++i) {
    if (isspace((unsigned char) name[i])) {
      return 0;
    }
    if (strchr("|<>()=,", name[i]) != NULL) {
      return 0;
    }
  }

  return 1;
}

static int is_blank_or_comment_line(const char *line) {
  size_t i = 0;

  if (line == NULL) {
    return 1;
  }

  while (line[i] != '\0' && isspace((unsigned char) line[i])) {
    i++;
  }

  return line[i] == '\0' || line[i] == '#';
}

static void trim_copy(const char *src, char *dest, size_t dest_size) {
  size_t start = 0;
  size_t end;
  size_t len;

  if (dest == NULL || dest_size == 0) {
    return;
  }

  if (src == NULL) {
    dest[0] = '\0';
    return;
  }

  len = strlen(src);
  while (start < len && isspace((unsigned char) src[start])) {
    start++;
  }

  end = len;
  while (end > start && isspace((unsigned char) src[end - 1])) {
    end--;
  }

  if (end <= start) {
    dest[0] = '\0';
    return;
  }

  if (end - start >= dest_size) {
    end = start + dest_size - 1;
  }

  memcpy(dest, src + start, end - start);
  dest[end - start] = '\0';
}

static int contains_top_level_list_operator(const char *text) {
  size_t i;
  char quote = '\0';
  int paren_depth = 0;
  int brace_depth = 0;
  int bracket_depth = 0;

  if (text == NULL) {
    return 0;
  }

  for (i = 0; text[i] != '\0'; ++i) {
    char c = text[i];

    if (quote != '\0') {
      if (c == quote) {
        quote = '\0';
      } else if (quote == '"' && c == '\\' && text[i + 1] != '\0') {
        i++;
      }
      continue;
    }

    if (c == '"' || c == '\'') {
      quote = c;
      continue;
    }
    if (c == '(') {
      paren_depth++;
      continue;
    }
    if (c == ')' && paren_depth > 0) {
      paren_depth--;
      continue;
    }
    if (c == '{') {
      brace_depth++;
      continue;
    }
    if (c == '}' && brace_depth > 0) {
      brace_depth--;
      continue;
    }
    if (c == '[') {
      bracket_depth++;
      continue;
    }
    if (c == ']' && bracket_depth > 0) {
      bracket_depth--;
      continue;
    }

    if (paren_depth == 0 && brace_depth == 0 && bracket_depth == 0) {
      if (c == ';' || c == '&') {
        return 1;
      }
      if (c == '|' && text[i + 1] == '|') {
        return 1;
      }
    }
  }

  return 0;
}

static void trim_trailing_newlines(char *text) {
  size_t len;

  if (text == NULL) {
    return;
  }

  len = strlen(text);
  while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
    text[len - 1] = '\0';
    len--;
  }
}

static int parse_error_is_incomplete_compound(const char *error) {
  return error != NULL &&
         (strncmp(error, "unterminated if command", 22) == 0 ||
          strncmp(error, "unterminated while command", 25) == 0 ||
          strncmp(error, "unterminated until command", 25) == 0 ||
          strncmp(error, "unterminated heredoc", 20) == 0 ||
          strncmp(error, "unterminated for command", 23) == 0 ||
          strncmp(error, "unterminated case command", 24) == 0 ||
          strncmp(error, "unterminated switch command", 27) == 0 ||
          strncmp(error, "unterminated function command", 29) == 0 ||
          strncmp(error, "unterminated class command", 26) == 0 ||
          strncmp(error, "unterminated group command", 26) == 0 ||
          strncmp(error, "unterminated subshell command", 29) == 0);
}

static int parse_error_is_unterminated_heredoc(const char *error) {
  return error != NULL && strncmp(error, "unterminated heredoc", 20) == 0;
}

static int command_requires_more_input(const char *text, char *error, size_t error_size) {
  ArkshAst ast;
  char parse_error[ARKSH_MAX_OUTPUT];
  char trimmed[ARKSH_MAX_LINE];
  int has_newline;

  if (text == NULL || error == NULL || error_size == 0) {
    return -1;
  }

  trim_copy(text, trimmed, sizeof(trimmed));
  has_newline = strchr(trimmed, '\n') != NULL || strchr(trimmed, '\r') != NULL;
  if (!has_newline && !contains_top_level_list_operator(trimmed)) {
    if (strcmp(trimmed, "let") == 0 ||
        (strncmp(trimmed, "let", 3) == 0 && isspace((unsigned char) trimmed[3])) ||
        strcmp(trimmed, "extend") == 0 ||
        (strncmp(trimmed, "extend", 6) == 0 && isspace((unsigned char) trimmed[6])) ||
        strcmp(trimmed, "break") == 0 ||
        (strncmp(trimmed, "break", 5) == 0 && isspace((unsigned char) trimmed[5])) ||
        strcmp(trimmed, "continue") == 0 ||
        (strncmp(trimmed, "continue", 8) == 0 && isspace((unsigned char) trimmed[8])) ||
        strcmp(trimmed, "return") == 0 ||
        (strncmp(trimmed, "return", 6) == 0 && isspace((unsigned char) trimmed[6]))) {
      error[0] = '\0';
      return 0;
    }
  }

  parse_error[0] = '\0';
  if (arksh_parse_line(text, &ast, parse_error, sizeof(parse_error)) == 0) {
    error[0] = '\0';
    return 0;
  }

  copy_string(error, error_size, parse_error);
  return parse_error_is_incomplete_compound(parse_error) ? 1 : -1;
}

/* Callback wrapper for the line editor: checks if text needs more input. */
static int repl_needs_more(const char *text) {
  char parse_error[ARKSH_MAX_OUTPUT];
  return command_requires_more_input(text, parse_error, sizeof(parse_error));
}

static int append_command_fragment(char *command, size_t command_size, const char *fragment) {
  char cleaned[ARKSH_MAX_LINE];

  if (command == NULL || command_size == 0 || fragment == NULL) {
    return 1;
  }

  copy_string(cleaned, sizeof(cleaned), fragment);
  trim_trailing_newlines(cleaned);
  if (command[0] != '\0' && append_text(command, command_size, "\n") != 0) {
    return 1;
  }

  return append_text(command, command_size, cleaned);
}

static void normalize_history_entry(char *text) {
  char normalized[ARKSH_MAX_LINE];
  size_t read_index = 0;
  size_t write_index = 0;

  if (text == NULL) {
    return;
  }

  normalized[0] = '\0';
  while (text[read_index] != '\0' && write_index + 1 < sizeof(normalized)) {
    char c = text[read_index];

    if (c == '\n' || c == '\r') {
      while (text[read_index] == '\n' || text[read_index] == '\r') {
        read_index++;
      }
      if (write_index > 0 && normalized[write_index - 1] != ' ') {
        normalized[write_index++] = ' ';
      }
      if (write_index + 2 >= sizeof(normalized)) {
        break;
      }
      normalized[write_index++] = ';';
      normalized[write_index++] = ' ';
      continue;
    }

    normalized[write_index++] = c;
    read_index++;
  }

  normalized[write_index] = '\0';
  trim_copy(normalized, text, ARKSH_MAX_LINE);
}

static int set_process_env(const char *name, const char *value) {
  if (name == NULL) {
    return 1;
  }

#ifdef _WIN32
  return _putenv_s(name, value == NULL ? "" : value);
#else
  return setenv(name, value == NULL ? "" : value, 1);
#endif
}

static int unset_process_env(const char *name) {
  if (name == NULL) {
    return 1;
  }

#ifdef _WIN32
  return _putenv_s(name, "");
#else
  return unsetenv(name);
#endif
}

static ArkshShellVar *find_var_entry(ArkshShell *shell, const char *name) {
  size_t i;

  if (shell == NULL || name == NULL) {
    return NULL;
  }

  for (i = 0; i < shell->var_count; ++i) {
    if (strcmp(shell->vars[i].name, name) == 0) {
      return &shell->vars[i];
    }
  }

  return NULL;
}

static const ArkshShellVar *find_var_entry_const(const ArkshShell *shell, const char *name) {
  size_t i;

  if (shell == NULL || name == NULL) {
    return NULL;
  }

  for (i = 0; i < shell->var_count; ++i) {
    if (strcmp(shell->vars[i].name, name) == 0) {
      return &shell->vars[i];
    }
  }

  return NULL;
}

static ArkshValueBinding *find_binding_entry(ArkshShell *shell, const char *name) {
  size_t i;

  if (shell == NULL || name == NULL) {
    return NULL;
  }

  for (i = 0; i < shell->binding_count; ++i) {
    if (strcmp(shell->bindings[i].name, name) == 0) {
      return &shell->bindings[i];
    }
  }

  return NULL;
}

static const ArkshValueBinding *find_binding_entry_const(const ArkshShell *shell, const char *name) {
  size_t i;

  if (shell == NULL || name == NULL) {
    return NULL;
  }

  for (i = 0; i < shell->binding_count; ++i) {
    if (strcmp(shell->bindings[i].name, name) == 0) {
      return &shell->bindings[i];
    }
  }

  return NULL;
}

static ArkshShellFunction *find_function_entry(ArkshShell *shell, const char *name) {
  size_t i;

  if (shell == NULL || name == NULL) {
    return NULL;
  }

  for (i = 0; i < shell->function_count; ++i) {
    if (strcmp(shell->functions[i].name, name) == 0) {
      return &shell->functions[i];
    }
  }

  return NULL;
}

static const ArkshShellFunction *find_function_entry_const(const ArkshShell *shell, const char *name) {
  size_t i;

  if (shell == NULL || name == NULL) {
    return NULL;
  }

  for (i = 0; i < shell->function_count; ++i) {
    if (strcmp(shell->functions[i].name, name) == 0) {
      return &shell->functions[i];
    }
  }

  return NULL;
}

static ArkshClassDef *find_class_entry(ArkshShell *shell, const char *name) {
  size_t i;

  if (shell == NULL || name == NULL) {
    return NULL;
  }

  for (i = 0; i < shell->class_count; ++i) {
    if (strcmp(shell->classes[i].name, name) == 0) {
      return &shell->classes[i];
    }
  }

  return NULL;
}

static const ArkshClassDef *find_class_entry_const(const ArkshShell *shell, const char *name) {
  size_t i;

  if (shell == NULL || name == NULL) {
    return NULL;
  }

  for (i = 0; i < shell->class_count; ++i) {
    if (strcmp(shell->classes[i].name, name) == 0) {
      return &shell->classes[i];
    }
  }

  return NULL;
}

static ArkshClassInstance *find_instance_entry(ArkshShell *shell, int id) {
  size_t i;

  if (shell == NULL || id <= 0) {
    return NULL;
  }

  for (i = 0; i < shell->instance_count; ++i) {
    if (shell->instances[i].id == id) {
      return &shell->instances[i];
    }
  }

  return NULL;
}

static const ArkshClassInstance *find_instance_entry_const(const ArkshShell *shell, int id) {
  size_t i;

  if (shell == NULL || id <= 0) {
    return NULL;
  }

  for (i = 0; i < shell->instance_count; ++i) {
    if (shell->instances[i].id == id) {
      return &shell->instances[i];
    }
  }

  return NULL;
}

static ArkshAlias *find_alias_entry(ArkshShell *shell, const char *name) {
  size_t i;

  if (shell == NULL || name == NULL) {
    return NULL;
  }

  for (i = 0; i < shell->alias_count; ++i) {
    if (strcmp(shell->aliases[i].name, name) == 0) {
      return &shell->aliases[i];
    }
  }

  return NULL;
}

static const ArkshAlias *find_alias_entry_const(const ArkshShell *shell, const char *name) {
  size_t i;

  if (shell == NULL || name == NULL) {
    return NULL;
  }

  for (i = 0; i < shell->alias_count; ++i) {
    if (strcmp(shell->aliases[i].name, name) == 0) {
      return &shell->aliases[i];
    }
  }

  return NULL;
}

static int plugin_index_is_active(const ArkshShell *shell, int plugin_index) {
  if (shell == NULL || plugin_index < 0) {
    return 1;
  }

  if ((size_t) plugin_index >= shell->plugin_count) {
    return 0;
  }

  return shell->plugins[plugin_index].active != 0;
}

static const ArkshCommandDef *find_registered_command(const ArkshShell *shell, const char *name) {
  size_t i;

  if (shell == NULL || name == NULL) {
    return NULL;
  }

  for (i = 0; i < shell->command_count; ++i) {
    if (strcmp(shell->commands[i].name, name) == 0 &&
        (!shell->commands[i].is_plugin_command || plugin_index_is_active(shell, shell->commands[i].owner_plugin_index))) {
      return &shell->commands[i];
    }
  }

  return NULL;
}

const ArkshValueResolverDef *arksh_shell_find_value_resolver(const ArkshShell *shell, const char *name) {
  size_t i;

  if (shell == NULL || name == NULL) {
    return NULL;
  }

  for (i = 0; i < shell->value_resolver_count; ++i) {
    if (strcmp(shell->value_resolvers[i].name, name) == 0 &&
        (!shell->value_resolvers[i].is_plugin_resolver ||
         plugin_index_is_active(shell, shell->value_resolvers[i].owner_plugin_index))) {
      return &shell->value_resolvers[i];
    }
  }

  return NULL;
}

const ArkshPipelineStageDef *arksh_shell_find_pipeline_stage(const ArkshShell *shell, const char *name) {
  size_t i;

  if (shell == NULL || name == NULL) {
    return NULL;
  }

  for (i = 0; i < shell->pipeline_stage_count; ++i) {
    if (strcmp(shell->pipeline_stages[i].name, name) == 0 &&
        (!shell->pipeline_stages[i].is_plugin_stage ||
         plugin_index_is_active(shell, shell->pipeline_stages[i].owner_plugin_index))) {
      return &shell->pipeline_stages[i];
    }
  }

  return NULL;
}

static ArkshLoadedPlugin *find_loaded_plugin(ArkshShell *shell, const char *query) {
  char resolved[ARKSH_MAX_PATH];
  size_t i;
  int have_resolved = 0;

  if (shell == NULL || query == NULL || query[0] == '\0') {
    return NULL;
  }

  if (arksh_shell_resolve_plugin_path(shell, query, resolved, sizeof(resolved)) == 0) {
    have_resolved = 1;
  }

  for (i = 0; i < shell->plugin_count; ++i) {
    if (strcmp(shell->plugins[i].name, query) == 0 ||
        strcmp(shell->plugins[i].path, query) == 0 ||
        (have_resolved && strcmp(shell->plugins[i].path, resolved) == 0)) {
      return &shell->plugins[i];
    }
  }

  return NULL;
}

const char *arksh_shell_get_var(const ArkshShell *shell, const char *name) {
  const ArkshShellVar *entry;

  if (name == NULL || name[0] == '\0') {
    return NULL;
  }

  entry = find_var_entry_const(shell, name);
  if (entry != NULL) {
    return entry->value;
  }

  return getenv(name);
}

const ArkshValue *arksh_shell_get_binding(const ArkshShell *shell, const char *name) {
  const ArkshValueBinding *entry;

  if (name == NULL || name[0] == '\0') {
    return NULL;
  }

  entry = find_binding_entry_const(shell, name);
  return entry == NULL ? NULL : &entry->value;
}

const ArkshShellFunction *arksh_shell_find_function(const ArkshShell *shell, const char *name) {
  if (name == NULL || name[0] == '\0') {
    return NULL;
  }

  return find_function_entry_const(shell, name);
}

int arksh_shell_set_var(ArkshShell *shell, const char *name, const char *value, int exported) {
  ArkshShellVar *entry;
  int effective_exported;

  if (shell == NULL || !is_valid_identifier(name)) {
    return 1;
  }

  entry = find_var_entry(shell, name);
  if (entry == NULL) {
    if (shell->var_count >= ARKSH_MAX_SHELL_VARS) {
      return 1;
    }
    entry = &shell->vars[shell->var_count++];
    memset(entry, 0, sizeof(*entry));
    copy_string(entry->name, sizeof(entry->name), name);
  }

  effective_exported = exported || entry->exported;
  copy_string(entry->value, sizeof(entry->value), value == NULL ? "" : value);
  entry->exported = effective_exported;

  if (effective_exported && set_process_env(name, entry->value) != 0) {
    return 1;
  }

  return 0;
}

int arksh_shell_unset_var(ArkshShell *shell, const char *name) {
  size_t i;

  if (shell == NULL || !is_valid_identifier(name)) {
    return 1;
  }

  for (i = 0; i < shell->var_count; ++i) {
    if (strcmp(shell->vars[i].name, name) == 0) {
      size_t remaining = shell->var_count - i - 1;

      if (remaining > 0) {
        memmove(&shell->vars[i], &shell->vars[i + 1], remaining * sizeof(shell->vars[i]));
      }
      shell->var_count--;
      break;
    }
  }

  if (unset_process_env(name) != 0) {
    return 1;
  }

  return 0;
}

int arksh_shell_set_binding(ArkshShell *shell, const char *name, const ArkshValue *value) {
  ArkshValueBinding *entry;

  if (shell == NULL || !is_valid_identifier(name) || value == NULL) {
    return 1;
  }

  entry = find_binding_entry(shell, name);
  if (entry == NULL) {
    if (shell->binding_count >= ARKSH_MAX_VALUE_BINDINGS) {
      return 1;
    }
    entry = &shell->bindings[shell->binding_count++];
    memset(entry, 0, sizeof(*entry));
    copy_string(entry->name, sizeof(entry->name), name);
  }

  arksh_value_free(&entry->value);
  return arksh_value_copy(&entry->value, value);
}

int arksh_shell_unset_binding(ArkshShell *shell, const char *name) {
  size_t i;

  if (shell == NULL || !is_valid_identifier(name)) {
    return 1;
  }

  for (i = 0; i < shell->binding_count; ++i) {
    if (strcmp(shell->bindings[i].name, name) == 0) {
      size_t remaining = shell->binding_count - i - 1;

      arksh_value_free(&shell->bindings[i].value);
      if (remaining > 0) {
        memmove(&shell->bindings[i], &shell->bindings[i + 1], remaining * sizeof(shell->bindings[i]));
      }
      shell->binding_count--;
      return 0;
    }
  }

  return 1;
}

int arksh_shell_set_function(ArkshShell *shell, const ArkshFunctionCommandNode *function_node) {
  ArkshShellFunction *entry;
  int i;

  if (shell == NULL || function_node == NULL || !is_valid_identifier(function_node->name)) {
    return 1;
  }

  entry = find_function_entry(shell, function_node->name);
  if (entry == NULL) {
    if (shell->function_count >= ARKSH_MAX_FUNCTIONS) {
      return 1;
    }
    entry = &shell->functions[shell->function_count++];
    memset(entry, 0, sizeof(*entry));
  }

  copy_string(entry->name, sizeof(entry->name), function_node->name);
  entry->param_count = function_node->param_count;
  for (i = 0; i < function_node->param_count && i < ARKSH_MAX_FUNCTION_PARAMS; ++i) {
    copy_string(entry->params[i], sizeof(entry->params[i]), function_node->params[i]);
  }
  copy_string(entry->body, sizeof(entry->body), function_node->body);
  copy_string(entry->source, sizeof(entry->source), function_node->source);
  return 0;
}

int arksh_shell_register_value_resolver(ArkshShell *shell, const char *name, const char *description, ArkshValueResolverFn fn, int is_plugin_resolver) {
  size_t i;

  if (shell == NULL || name == NULL || name[0] == '\0' || fn == NULL || !is_valid_identifier(name)) {
    return 1;
  }

  for (i = 0; i < shell->value_resolver_count; ++i) {
    if (strcmp(shell->value_resolvers[i].name, name) == 0) {
      if (description != NULL && description[0] != '\0') {
        copy_string(shell->value_resolvers[i].description, sizeof(shell->value_resolvers[i].description), description);
      }
      shell->value_resolvers[i].fn = fn;
      shell->value_resolvers[i].is_plugin_resolver = is_plugin_resolver;
      shell->value_resolvers[i].owner_plugin_index = is_plugin_resolver ? shell->loading_plugin_index : -1;
      return 0;
    }
  }

  if (shell->value_resolver_count >= ARKSH_MAX_VALUE_RESOLVERS) {
    return 1;
  }

  copy_string(shell->value_resolvers[shell->value_resolver_count].name, sizeof(shell->value_resolvers[shell->value_resolver_count].name), name);
  copy_string(shell->value_resolvers[shell->value_resolver_count].description, sizeof(shell->value_resolvers[shell->value_resolver_count].description), description != NULL ? description : "");
  shell->value_resolvers[shell->value_resolver_count].fn = fn;
  shell->value_resolvers[shell->value_resolver_count].is_plugin_resolver = is_plugin_resolver;
  shell->value_resolvers[shell->value_resolver_count].owner_plugin_index = is_plugin_resolver ? shell->loading_plugin_index : -1;
  shell->value_resolver_count++;
  return 0;
}

int arksh_shell_register_pipeline_stage(ArkshShell *shell, const char *name, const char *description, ArkshPipelineStageFn fn, int is_plugin_stage) {
  size_t i;

  /* fn may be NULL for metadata-only (built-in) entries. */
  if (shell == NULL || name == NULL || name[0] == '\0' || !is_valid_identifier(name)) {
    return 1;
  }

  for (i = 0; i < shell->pipeline_stage_count; ++i) {
    if (strcmp(shell->pipeline_stages[i].name, name) == 0) {
      if (description != NULL && description[0] != '\0') {
        copy_string(shell->pipeline_stages[i].description, sizeof(shell->pipeline_stages[i].description), description);
      }
      if (fn != NULL) {
        shell->pipeline_stages[i].fn = fn;
      }
      shell->pipeline_stages[i].is_plugin_stage = is_plugin_stage;
      shell->pipeline_stages[i].owner_plugin_index = is_plugin_stage ? shell->loading_plugin_index : -1;
      return 0;
    }
  }

  if (shell->pipeline_stage_count >= ARKSH_MAX_PIPELINE_STAGE_HANDLERS) {
    return 1;
  }

  copy_string(shell->pipeline_stages[shell->pipeline_stage_count].name, sizeof(shell->pipeline_stages[shell->pipeline_stage_count].name), name);
  copy_string(shell->pipeline_stages[shell->pipeline_stage_count].description, sizeof(shell->pipeline_stages[shell->pipeline_stage_count].description), description != NULL ? description : "");
  shell->pipeline_stages[shell->pipeline_stage_count].fn = fn;
  shell->pipeline_stages[shell->pipeline_stage_count].is_plugin_stage = is_plugin_stage;
  shell->pipeline_stages[shell->pipeline_stage_count].owner_plugin_index = is_plugin_stage ? shell->loading_plugin_index : -1;
  shell->pipeline_stage_count++;
  return 0;
}

static int starts_with_prefix(const char *text, const char *prefix) {
  size_t prefix_len;

  if (text == NULL || prefix == NULL) {
    return 0;
  }

  prefix_len = strlen(prefix);
  return strncmp(text, prefix, prefix_len) == 0;
}

static void append_completion_match(
  char matches[][ARKSH_MAX_PATH],
  size_t max_matches,
  size_t *count,
  const char *value
) {
  size_t i;

  if (matches == NULL || count == NULL || value == NULL || value[0] == '\0') {
    return;
  }

  for (i = 0; i < *count; ++i) {
    if (strcmp(matches[i], value) == 0) {
      return;
    }
  }

  if (*count >= max_matches) {
    return;
  }

  copy_string(matches[*count], ARKSH_MAX_PATH, value);
  (*count)++;
}

static void append_member_completion(
  const char *name,
  int is_method,
  const char *prefix,
  char matches[][ARKSH_MAX_PATH],
  size_t max_matches,
  size_t *count
) {
  char label[ARKSH_MAX_PATH];

  if (name == NULL || prefix == NULL) {
    return;
  }

  snprintf(label, sizeof(label), "%s%s", name, is_method ? "()" : "");
  if (!starts_with_prefix(label, prefix)) {
    return;
  }

  append_completion_match(matches, max_matches, count, label);
}

static int extension_target_matches_value(const ArkshObjectExtension *extension, const ArkshValue *receiver) {
  if (extension == NULL || receiver == NULL) {
    return 0;
  }

  switch (extension->target_kind) {
    case ARKSH_EXTENSION_TARGET_ANY:
      return 1;
    case ARKSH_EXTENSION_TARGET_VALUE_KIND:
      return receiver->kind == extension->value_kind;
    case ARKSH_EXTENSION_TARGET_OBJECT_KIND:
      return receiver->kind == ARKSH_VALUE_OBJECT && receiver->object.kind == extension->object_kind;
    case ARKSH_EXTENSION_TARGET_TYPED_MAP: {
      /* E6-S2-T1: match typed-map values whose __type__ tag equals the target name. */
      const ArkshValueItem *type_entry;
      if (receiver->kind != ARKSH_VALUE_MAP) {
        return 0;
      }
      type_entry = arksh_value_map_get_item(receiver, "__type__");
      if (type_entry == NULL || type_entry->kind != ARKSH_VALUE_STRING) {
        return 0;
      }
      return strcmp(type_entry->text, extension->target_name) == 0;
    }
    default:
      return 0;
  }
}

static void collect_builtin_member_completions(
  const ArkshValue *receiver,
  const char *prefix,
  char matches[][ARKSH_MAX_PATH],
  size_t max_matches,
  size_t *count
) {
  static const char *object_properties[] = {
    "type", "value_type", "value", "path", "name", "exists", "size", "hidden", "readable", "writable"
  };
  static const char *object_methods[] = {
    "children", "read_text", "parent", "describe"
  };
  static const char *string_properties[] = {
    "type", "value_type", "value", "text", "length"
  };
  static const char *number_properties[] = {
    "type", "value_type", "value", "number"
  };
  static const char *bool_properties[] = {
    "type", "value_type", "value", "bool"
  };
  static const char *block_properties[] = {
    "type", "value_type", "value", "arity", "source", "body", "params"
  };
  static const char *block_methods[] = {
    "call"
  };
  static const char *list_properties[] = {
    "type", "value_type", "value", "count", "length"
  };
  static const char *map_properties[] = {
    "type", "value_type", "value", "count", "length"
  };
  static const char *map_methods[] = {
    "keys", "values", "entries", "get", "has", "get_path", "has_path", "set_path", "pick", "merge"
  };
  static const char *dict_properties[] = {
    "type", "value_type", "value", "count", "length", "keys", "values"
  };
  static const char *dict_methods[] = {
    "get", "has", "set", "delete", "to_json", "from_json", "get_path", "has_path", "set_path", "pick", "merge"
  };
  static const char *class_properties[] = {
    "type", "value_type", "value", "name", "source", "bases", "base_count", "properties", "property_count", "methods", "method_count"
  };
  static const char *class_methods[] = {
    "new"
  };
  static const char *instance_properties[] = {
    "type", "value_type", "value", "id", "class", "class_name", "fields", "properties", "property_count", "methods"
  };
  static const char *instance_methods[] = {
    "set", "get", "isa"
  };
  static const char *empty_properties[] = {
    "type", "value_type", "value"
  };
  const char **properties = NULL;
  const char **methods = NULL;
  size_t property_count = 0;
  size_t method_count = 0;
  size_t i;

  if (receiver == NULL || prefix == NULL || matches == NULL || count == NULL) {
    return;
  }

  switch (receiver->kind) {
    case ARKSH_VALUE_OBJECT:
      properties = object_properties;
      property_count = sizeof(object_properties) / sizeof(object_properties[0]);
      methods = object_methods;
      method_count = sizeof(object_methods) / sizeof(object_methods[0]);
      break;
    case ARKSH_VALUE_STRING:
      properties = string_properties;
      property_count = sizeof(string_properties) / sizeof(string_properties[0]);
      break;
    case ARKSH_VALUE_NUMBER:
      properties = number_properties;
      property_count = sizeof(number_properties) / sizeof(number_properties[0]);
      break;
    case ARKSH_VALUE_BOOLEAN:
      properties = bool_properties;
      property_count = sizeof(bool_properties) / sizeof(bool_properties[0]);
      break;
    case ARKSH_VALUE_BLOCK:
      properties = block_properties;
      property_count = sizeof(block_properties) / sizeof(block_properties[0]);
      methods = block_methods;
      method_count = sizeof(block_methods) / sizeof(block_methods[0]);
      break;
    case ARKSH_VALUE_LIST:
      properties = list_properties;
      property_count = sizeof(list_properties) / sizeof(list_properties[0]);
      break;
    case ARKSH_VALUE_MAP:
      properties = map_properties;
      property_count = sizeof(map_properties) / sizeof(map_properties[0]);
      methods = map_methods;
      method_count = sizeof(map_methods) / sizeof(map_methods[0]);
      break;
    case ARKSH_VALUE_DICT:
      properties = dict_properties;
      property_count = sizeof(dict_properties) / sizeof(dict_properties[0]);
      methods = dict_methods;
      method_count = sizeof(dict_methods) / sizeof(dict_methods[0]);
      break;
    case ARKSH_VALUE_CLASS:
      properties = class_properties;
      property_count = sizeof(class_properties) / sizeof(class_properties[0]);
      methods = class_methods;
      method_count = sizeof(class_methods) / sizeof(class_methods[0]);
      break;
    case ARKSH_VALUE_INSTANCE:
      properties = instance_properties;
      property_count = sizeof(instance_properties) / sizeof(instance_properties[0]);
      methods = instance_methods;
      method_count = sizeof(instance_methods) / sizeof(instance_methods[0]);
      break;
    case ARKSH_VALUE_EMPTY:
    default:
      properties = empty_properties;
      property_count = sizeof(empty_properties) / sizeof(empty_properties[0]);
      break;
  }

  for (i = 0; i < property_count; ++i) {
    append_member_completion(properties[i], 0, prefix, matches, max_matches, count);
  }
  if (receiver->kind == ARKSH_VALUE_MAP || receiver->kind == ARKSH_VALUE_DICT) {
    for (i = 0; i < receiver->map.count; ++i) {
      append_member_completion(receiver->map.entries[i].key, 0, prefix, matches, max_matches, count);
    }
  }
  for (i = 0; i < method_count; ++i) {
    append_member_completion(methods[i], 1, prefix, matches, max_matches, count);
  }
}

static void collect_class_runtime_member_completions(
  const ArkshShell *shell,
  const ArkshValue *receiver,
  const char *prefix,
  char matches[][ARKSH_MAX_PATH],
  size_t max_matches,
  size_t *count
) {
  ArkshValue *names;
  char error[ARKSH_MAX_OUTPUT];
  size_t i;

  if (shell == NULL || receiver == NULL || prefix == NULL || matches == NULL || count == NULL) {
    return;
  }
  if (!(receiver->kind == ARKSH_VALUE_CLASS || receiver->kind == ARKSH_VALUE_INSTANCE)) {
    return;
  }

  names = allocate_shell_value(NULL, 0);
  if (names == NULL) {
    return;
  }

  error[0] = '\0';
  if (receiver->kind == ARKSH_VALUE_CLASS) {
    if (build_class_property_list(shell, receiver->text, names, error, sizeof(error)) == 0) {
      for (i = 0; i < names->list.count; ++i) {
        append_member_completion(names->list.items[i].text, 0, prefix, matches, max_matches, count);
      }
      arksh_value_free(names);
    }
  } else {
    const ArkshClassInstance *instance = find_instance_entry_const(shell, (int) receiver->number);

    if (instance != NULL) {
      for (i = 0; i < instance->fields.map.count; ++i) {
        append_member_completion(instance->fields.map.entries[i].key, 0, prefix, matches, max_matches, count);
      }
    }
  }

  error[0] = '\0';
  if (build_class_method_list(shell, receiver->text, names, error, sizeof(error)) == 0) {
    for (i = 0; i < names->list.count; ++i) {
      append_member_completion(names->list.items[i].text, 1, prefix, matches, max_matches, count);
    }
    arksh_value_free(names);
  }
  free(names);
}

static void collect_extension_member_completions(
  const ArkshShell *shell,
  const ArkshValue *receiver,
  const char *prefix,
  char matches[][ARKSH_MAX_PATH],
  size_t max_matches,
  size_t *count
) {
  size_t i;

  if (shell == NULL || receiver == NULL || prefix == NULL || matches == NULL || count == NULL) {
    return;
  }

  for (i = 0; i < shell->extension_count; ++i) {
    const ArkshObjectExtension *extension = &shell->extensions[i];

    if (extension->is_plugin_extension && !plugin_index_is_active(shell, extension->owner_plugin_index)) {
      continue;
    }
    if (!extension_target_matches_value(extension, receiver)) {
      continue;
    }

    append_member_completion(
      extension->name,
      extension->member_kind == ARKSH_MEMBER_METHOD,
      prefix,
      matches,
      max_matches,
      count
    );
  }
}

int arksh_shell_collect_member_completions(
  ArkshShell *shell,
  const char *receiver_text,
  const char *prefix,
  char matches[][ARKSH_MAX_PATH],
  size_t max_matches,
  size_t *out_count
) {
  ArkshValue *value;
  ArkshObject object;
  char error[ARKSH_MAX_OUTPUT];

  if (shell == NULL || receiver_text == NULL || prefix == NULL || matches == NULL || out_count == NULL) {
    return 1;
  }

  *out_count = 0;
  memset(matches, 0, max_matches * sizeof(matches[0]));
  error[0] = '\0';
  value = allocate_runtime_value(error, sizeof(error), "completion receiver");
  if (value == NULL) {
    return 1;
  }

  if (arksh_evaluate_line_value(shell, receiver_text, value, error, sizeof(error)) != 0) {
    if (arksh_object_resolve(shell->cwd, receiver_text, &object) != 0) {
      free(value);
      return 1;
    }
    arksh_value_set_object(value, &object);
  }

  collect_builtin_member_completions(value, prefix, matches, max_matches, out_count);
  collect_class_runtime_member_completions(shell, value, prefix, matches, max_matches, out_count);
  collect_extension_member_completions(shell, value, prefix, matches, max_matches, out_count);
  arksh_value_free(value);
  free(value);
  return *out_count == 0 ? 1 : 0;
}

static int parse_extension_target(
  const char *target,
  ArkshExtensionTargetKind *out_kind,
  ArkshValueKind *out_value_kind,
  ArkshObjectKind *out_object_kind
) {
  if (target == NULL || out_kind == NULL || out_value_kind == NULL || out_object_kind == NULL) {
    return 1;
  }

  *out_kind = ARKSH_EXTENSION_TARGET_ANY;
  *out_value_kind = ARKSH_VALUE_EMPTY;
  *out_object_kind = ARKSH_OBJECT_UNKNOWN;

  if (strcmp(target, "any") == 0) {
    return 0;
  }

  if (strcmp(target, "string") == 0) {
    *out_kind = ARKSH_EXTENSION_TARGET_VALUE_KIND;
    *out_value_kind = ARKSH_VALUE_STRING;
    return 0;
  }
  if (strcmp(target, "number") == 0) {
    *out_kind = ARKSH_EXTENSION_TARGET_VALUE_KIND;
    *out_value_kind = ARKSH_VALUE_NUMBER;
    return 0;
  }
  if (strcmp(target, "bool") == 0 || strcmp(target, "boolean") == 0) {
    *out_kind = ARKSH_EXTENSION_TARGET_VALUE_KIND;
    *out_value_kind = ARKSH_VALUE_BOOLEAN;
    return 0;
  }
  if (strcmp(target, "object") == 0) {
    *out_kind = ARKSH_EXTENSION_TARGET_VALUE_KIND;
    *out_value_kind = ARKSH_VALUE_OBJECT;
    return 0;
  }
  if (strcmp(target, "block") == 0) {
    *out_kind = ARKSH_EXTENSION_TARGET_VALUE_KIND;
    *out_value_kind = ARKSH_VALUE_BLOCK;
    return 0;
  }
  if (strcmp(target, "list") == 0 || strcmp(target, "array") == 0) {
    *out_kind = ARKSH_EXTENSION_TARGET_VALUE_KIND;
    *out_value_kind = ARKSH_VALUE_LIST;
    return 0;
  }
  if (strcmp(target, "map") == 0 || strcmp(target, "object_map") == 0) {
    *out_kind = ARKSH_EXTENSION_TARGET_VALUE_KIND;
    *out_value_kind = ARKSH_VALUE_MAP;
    return 0;
  }
  /* E6-S6: Dict is a distinct value kind from map */
  if (strcmp(target, "dict") == 0) {
    *out_kind = ARKSH_EXTENSION_TARGET_VALUE_KIND;
    *out_value_kind = ARKSH_VALUE_DICT;
    return 0;
  }
  /* E6-S8: Matrix */
  if (strcmp(target, "matrix") == 0) {
    *out_kind = ARKSH_EXTENSION_TARGET_VALUE_KIND;
    *out_value_kind = ARKSH_VALUE_MATRIX;
    return 0;
  }
  if (strcmp(target, "class") == 0) {
    *out_kind = ARKSH_EXTENSION_TARGET_VALUE_KIND;
    *out_value_kind = ARKSH_VALUE_CLASS;
    return 0;
  }
  if (strcmp(target, "instance") == 0) {
    *out_kind = ARKSH_EXTENSION_TARGET_VALUE_KIND;
    *out_value_kind = ARKSH_VALUE_INSTANCE;
    return 0;
  }
  if (strcmp(target, "empty") == 0) {
    *out_kind = ARKSH_EXTENSION_TARGET_VALUE_KIND;
    *out_value_kind = ARKSH_VALUE_EMPTY;
    return 0;
  }

  if (strcmp(target, "path") == 0) {
    *out_kind = ARKSH_EXTENSION_TARGET_OBJECT_KIND;
    *out_object_kind = ARKSH_OBJECT_PATH;
    return 0;
  }
  if (strcmp(target, "file") == 0) {
    *out_kind = ARKSH_EXTENSION_TARGET_OBJECT_KIND;
    *out_object_kind = ARKSH_OBJECT_FILE;
    return 0;
  }
  if (strcmp(target, "directory") == 0 || strcmp(target, "dir") == 0) {
    *out_kind = ARKSH_EXTENSION_TARGET_OBJECT_KIND;
    *out_object_kind = ARKSH_OBJECT_DIRECTORY;
    return 0;
  }
  if (strcmp(target, "device") == 0) {
    *out_kind = ARKSH_EXTENSION_TARGET_OBJECT_KIND;
    *out_object_kind = ARKSH_OBJECT_DEVICE;
    return 0;
  }
  if (strcmp(target, "mount") == 0 || strcmp(target, "mount_point") == 0 || strcmp(target, "mount-point") == 0) {
    *out_kind = ARKSH_EXTENSION_TARGET_OBJECT_KIND;
    *out_object_kind = ARKSH_OBJECT_MOUNT_POINT;
    return 0;
  }

  /* E6-S2-T1: any unrecognised target is treated as a custom typed-map type
   * name.  The target string is already stored in extension->target_name by
   * register_extension_common, so nothing extra is needed here. */
  *out_kind = ARKSH_EXTENSION_TARGET_TYPED_MAP;
  return 0;
}

static ArkshObjectExtension *find_extension_entry(ArkshShell *shell, const char *target, ArkshMemberKind member_kind, const char *name) {
  size_t i;

  if (shell == NULL || target == NULL || name == NULL) {
    return NULL;
  }

  for (i = 0; i < shell->extension_count; ++i) {
    if (shell->extensions[i].member_kind == member_kind &&
        strcmp(shell->extensions[i].target_name, target) == 0 &&
        strcmp(shell->extensions[i].name, name) == 0) {
      return &shell->extensions[i];
    }
  }

  return NULL;
}

static int register_extension_common(
  ArkshShell *shell,
  const char *target,
  const char *name,
  ArkshMemberKind member_kind,
  ArkshExtensionImplKind impl_kind,
  const ArkshBlock *block,
  ArkshExtensionPropertyFn property_fn,
  ArkshExtensionMethodFn method_fn,
  int is_plugin_extension
) {
  ArkshObjectExtension *entry;
  ArkshExtensionTargetKind target_kind;
  ArkshValueKind value_kind;
  ArkshObjectKind object_kind;

  if (shell == NULL || target == NULL || name == NULL || name[0] == '\0') {
    return 1;
  }

  if (!is_valid_identifier(name)) {
    return 1;
  }

  if (parse_extension_target(target, &target_kind, &value_kind, &object_kind) != 0) {
    return 1;
  }

  entry = find_extension_entry(shell, target, member_kind, name);
  if (entry == NULL) {
    if (shell->extension_count >= ARKSH_MAX_EXTENSIONS) {
      return 1;
    }
    entry = &shell->extensions[shell->extension_count++];
    memset(entry, 0, sizeof(*entry));
  }

  copy_string(entry->target_name, sizeof(entry->target_name), target);
  copy_string(entry->name, sizeof(entry->name), name);
  entry->member_kind = member_kind;
  entry->target_kind = target_kind;
  entry->value_kind = value_kind;
  entry->object_kind = object_kind;
  entry->impl_kind = impl_kind;
  if (block != NULL) {
    entry->block = *block;
  } else {
    memset(&entry->block, 0, sizeof(entry->block));
  }
  entry->property_fn = property_fn;
  entry->method_fn = method_fn;
  entry->is_plugin_extension = is_plugin_extension;
  entry->owner_plugin_index = is_plugin_extension ? shell->loading_plugin_index : -1;
  return 0;
}

int arksh_shell_register_block_property_extension(ArkshShell *shell, const char *target, const char *name, const ArkshBlock *block) {
  if (block == NULL) {
    return 1;
  }
  return register_extension_common(shell, target, name, ARKSH_MEMBER_PROPERTY, ARKSH_EXTENSION_IMPL_BLOCK, block, NULL, NULL, 0);
}

int arksh_shell_register_block_method_extension(ArkshShell *shell, const char *target, const char *name, const ArkshBlock *block) {
  if (block == NULL) {
    return 1;
  }
  return register_extension_common(shell, target, name, ARKSH_MEMBER_METHOD, ARKSH_EXTENSION_IMPL_BLOCK, block, NULL, NULL, 0);
}

int arksh_shell_register_native_property_extension(
  ArkshShell *shell,
  const char *target,
  const char *name,
  ArkshExtensionPropertyFn fn,
  int is_plugin_extension
) {
  if (fn == NULL) {
    return 1;
  }
  return register_extension_common(shell, target, name, ARKSH_MEMBER_PROPERTY, ARKSH_EXTENSION_IMPL_NATIVE, NULL, fn, NULL, is_plugin_extension);
}

int arksh_shell_register_native_method_extension(
  ArkshShell *shell,
  const char *target,
  const char *name,
  ArkshExtensionMethodFn fn,
  int is_plugin_extension
) {
  if (fn == NULL) {
    return 1;
  }
  return register_extension_common(shell, target, name, ARKSH_MEMBER_METHOD, ARKSH_EXTENSION_IMPL_NATIVE, NULL, NULL, fn, is_plugin_extension);
}

int arksh_shell_register_type_descriptor(ArkshShell *shell, const char *type_name, const char *description) {
  size_t i;

  if (shell == NULL || type_name == NULL || type_name[0] == '\0') {
    return 1;
  }
  /* Ignore duplicate registrations */
  for (i = 0; i < shell->type_descriptor_count; ++i) {
    if (strcmp(shell->type_descriptors[i].type_name, type_name) == 0) {
      return 0;
    }
  }
  if (shell->type_descriptor_count >= ARKSH_MAX_TYPE_DESCRIPTORS) {
    return 1;
  }
  copy_string(shell->type_descriptors[shell->type_descriptor_count].type_name,
              ARKSH_MAX_NAME, type_name);
  copy_string(shell->type_descriptors[shell->type_descriptor_count].description,
              ARKSH_MAX_DESCRIPTION, description != NULL ? description : "");
  shell->type_descriptor_count++;
  return 0;
}

static int receiver_is_json_file_target(const ArkshValue *receiver) {
  if (receiver == NULL || receiver->kind != ARKSH_VALUE_OBJECT) {
    return 0;
  }

  return receiver->object.kind == ARKSH_OBJECT_FILE || receiver->object.kind == ARKSH_OBJECT_PATH;
}

static int method_read_json(
  ArkshShell *shell,
  const ArkshValue *receiver,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *out,
  size_t out_size
) {
  char json_text[ARKSH_MAX_OUTPUT];

  (void) shell;
  (void) args;

  if (receiver == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (!receiver_is_json_file_target(receiver)) {
    snprintf(out, out_size, "read_json() is only valid on file-like objects");
    return 1;
  }
  if (argc != 0) {
    snprintf(out, out_size, "read_json() does not accept arguments");
    return 1;
  }
  if (arksh_platform_read_text_file(receiver->object.path, sizeof(json_text) - 1, json_text, sizeof(json_text)) != 0) {
    return 1;
  }
  if (arksh_value_parse_json(json_text, out_value, out, out_size) != 0) {
    return 1;
  }

  return 0;
}

static int method_write_json(
  ArkshShell *shell,
  const ArkshValue *receiver,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *out,
  size_t out_size
) {
  char json_text[ARKSH_MAX_OUTPUT];

  (void) shell;
  (void) out_value;

  if (receiver == NULL || args == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (!receiver_is_json_file_target(receiver)) {
    snprintf(out, out_size, "write_json() is only valid on file-like objects");
    return 1;
  }
  if (argc != 1) {
    snprintf(out, out_size, "write_json() expects exactly one value argument");
    return 1;
  }
  if (arksh_value_to_json(&args[0], json_text, sizeof(json_text)) != 0) {
    snprintf(out, out_size, "unable to serialize value as JSON");
    return 1;
  }
  if (arksh_platform_write_text_file(receiver->object.path, json_text, 0, out, out_size) != 0) {
    return 1;
  }

  return 0;
}

static int append_char_text(char *dest, size_t dest_size, char c) {
  char text[2];

  text[0] = c;
  text[1] = '\0';
  return append_text(dest, dest_size, text);
}

static int parse_print_number(const ArkshValue *value, double *out_number, char *error, size_t error_size) {
  char rendered[ARKSH_MAX_OUTPUT];
  char *endptr = NULL;

  if (value == NULL || out_number == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  switch (value->kind) {
    case ARKSH_VALUE_NUMBER:
      *out_number = value->number;
      return 0;
    case ARKSH_VALUE_BOOLEAN:
      *out_number = value->boolean ? 1.0 : 0.0;
      return 0;
    case ARKSH_VALUE_STRING:
      *out_number = strtod(value->text, &endptr);
      if (endptr == value->text || *endptr != '\0') {
        snprintf(error, error_size, "print() expected a numeric value");
        return 1;
      }
      return 0;
    default:
      if (arksh_value_render(value, rendered, sizeof(rendered)) != 0) {
        snprintf(error, error_size, "unable to render print() argument");
        return 1;
      }
      *out_number = strtod(rendered, &endptr);
      if (endptr == rendered || *endptr != '\0') {
        snprintf(error, error_size, "print() expected a numeric value");
        return 1;
      }
      return 0;
  }
}

static int render_print_argument(
  char specifier,
  const ArkshValue *value,
  char *out,
  size_t out_size,
  char *error,
  size_t error_size
) {
  double number;

  if (value == NULL || out == NULL || out_size == 0 || error == NULL || error_size == 0) {
    return 1;
  }

  out[0] = '\0';
  switch (specifier) {
    case 's':
    case 'v':
      if (arksh_value_render(value, out, out_size) != 0) {
        snprintf(error, error_size, "unable to render print() argument");
        return 1;
      }
      return 0;
    case 'd':
    case 'i':
      if (parse_print_number(value, &number, error, error_size) != 0) {
        return 1;
      }
      snprintf(out, out_size, "%lld", (long long) number);
      return 0;
    case 'u':
      if (parse_print_number(value, &number, error, error_size) != 0) {
        return 1;
      }
      if (number < 0) {
        snprintf(error, error_size, "print() expected a non-negative numeric value for %%u");
        return 1;
      }
      snprintf(out, out_size, "%llu", (unsigned long long) number);
      return 0;
    case 'f':
      if (parse_print_number(value, &number, error, error_size) != 0) {
        return 1;
      }
      snprintf(out, out_size, "%f", number);
      return 0;
    case 'g':
      if (parse_print_number(value, &number, error, error_size) != 0) {
        return 1;
      }
      snprintf(out, out_size, "%.15g", number);
      return 0;
    case 'b':
      if (value->kind == ARKSH_VALUE_BOOLEAN) {
        copy_string(out, out_size, value->boolean ? "true" : "false");
        return 0;
      }
      if (parse_print_number(value, &number, error, error_size) != 0) {
        return 1;
      }
      copy_string(out, out_size, number != 0.0 ? "true" : "false");
      return 0;
    default:
      snprintf(error, error_size, "unsupported print() format specifier: %%%c", specifier);
      return 1;
  }
}

static int format_print_output(
  const char *format,
  int argc,
  const ArkshValue *args,
  char *out,
  size_t out_size,
  char *error,
  size_t error_size
) {
  size_t i;
  int arg_index = 0;

  if (format == NULL || out == NULL || out_size == 0 || error == NULL || error_size == 0) {
    return 1;
  }

  out[0] = '\0';
  for (i = 0; format[i] != '\0'; ++i) {
    char c = format[i];

    if (c != '%') {
      if (append_char_text(out, out_size, c) != 0) {
        snprintf(error, error_size, "print() output is too large");
        return 1;
      }
      continue;
    }

    if (format[i + 1] == '\0') {
      snprintf(error, error_size, "dangling %% in print() format");
      return 1;
    }
    if (format[i + 1] == '%') {
      if (append_char_text(out, out_size, '%') != 0) {
        snprintf(error, error_size, "print() output is too large");
        return 1;
      }
      ++i;
      continue;
    }
    if (arg_index >= argc) {
      snprintf(error, error_size, "print() is missing arguments for format string");
      return 1;
    }

    {
      char rendered[ARKSH_MAX_OUTPUT];

      if (render_print_argument(format[i + 1], &args[arg_index], rendered, sizeof(rendered), error, error_size) != 0) {
        return 1;
      }
      if (append_text(out, out_size, rendered) != 0) {
        snprintf(error, error_size, "print() output is too large");
        return 1;
      }
    }

    ++arg_index;
    ++i;
  }

  if (arg_index < argc) {
    snprintf(error, error_size, "print() received too many arguments for the format string");
    return 1;
  }

  return 0;
}

static int method_print(
  ArkshShell *shell,
  const ArkshValue *receiver,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *out,
  size_t out_size
) {
  char rendered[ARKSH_MAX_OUTPUT];

  (void) shell;

  if (receiver == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (receiver->kind == ARKSH_VALUE_STRING) {
    if (format_print_output(receiver->text, argc, args, rendered, sizeof(rendered), out, out_size) != 0) {
      return 1;
    }
    arksh_value_set_string(out_value, rendered);
    return 0;
  }

  if (argc != 0) {
    snprintf(out, out_size, "print() accepts format arguments only when the receiver is a string");
    return 1;
  }

  if (arksh_value_render(receiver, rendered, sizeof(rendered)) != 0) {
    snprintf(out, out_size, "unable to render receiver for print()");
    return 1;
  }

  arksh_value_set_string(out_value, rendered);
  return 0;
}

const char *arksh_shell_get_alias(const ArkshShell *shell, const char *name) {
  const ArkshAlias *entry = find_alias_entry_const(shell, name);

  return entry == NULL ? NULL : entry->value;
}

int arksh_shell_set_alias(ArkshShell *shell, const char *name, const char *value) {
  ArkshAlias *entry;

  if (shell == NULL || !is_valid_alias_name(name)) {
    return 1;
  }

  entry = find_alias_entry(shell, name);
  if (entry == NULL) {
    if (shell->alias_count >= ARKSH_MAX_ALIASES) {
      return 1;
    }
    entry = &shell->aliases[shell->alias_count++];
    memset(entry, 0, sizeof(*entry));
    copy_string(entry->name, sizeof(entry->name), name);
  }

  copy_string(entry->value, sizeof(entry->value), value == NULL ? "" : value);
  return 0;
}

int arksh_shell_unset_alias(ArkshShell *shell, const char *name) {
  size_t i;

  if (shell == NULL || !is_valid_alias_name(name)) {
    return 1;
  }

  for (i = 0; i < shell->alias_count; ++i) {
    if (strcmp(shell->aliases[i].name, name) == 0) {
      size_t remaining = shell->alias_count - i - 1;

      if (remaining > 0) {
        memmove(&shell->aliases[i], &shell->aliases[i + 1], remaining * sizeof(shell->aliases[i]));
      }
      shell->alias_count--;
      return 0;
    }
  }

  return 1;
}

static int join_arguments(int argc, char **argv, int start_index, char *out, size_t out_size) {
  int i;

  if (out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  for (i = start_index; i < argc; ++i) {
    if (i > start_index && append_text(out, out_size, " ") != 0) {
      return 1;
    }
    if (append_text(out, out_size, argv[i]) != 0) {
      return 1;
    }
  }

  return 0;
}

static int map_add_value_entry(ArkshValue *map, const char *key, const ArkshValue *entry_value) {
  return arksh_value_map_set(map, key, entry_value);
}

static int map_add_string_entry(ArkshValue *map, const char *key, const char *text) {
  ArkshValue *entry;
  int status;

  entry = (ArkshValue *) calloc(1, sizeof(*entry));
  if (entry == NULL) {
    return 1;
  }

  arksh_value_set_string(entry, text);
  status = arksh_value_map_set(map, key, entry);
  arksh_value_free(entry);
  free(entry);
  return status;
}

static int map_add_number_entry(ArkshValue *map, const char *key, double number) {
  ArkshValue *entry;
  int status;

  entry = (ArkshValue *) calloc(1, sizeof(*entry));
  if (entry == NULL) {
    return 1;
  }

  arksh_value_set_number(entry, number);
  status = arksh_value_map_set(map, key, entry);
  arksh_value_free(entry);
  free(entry);
  return status;
}

static int map_add_bool_entry(ArkshValue *map, const char *key, int boolean) {
  ArkshValue *entry;
  int status;

  entry = (ArkshValue *) calloc(1, sizeof(*entry));
  if (entry == NULL) {
    return 1;
  }

  arksh_value_set_boolean(entry, boolean);
  status = arksh_value_map_set(map, key, entry);
  arksh_value_free(entry);
  free(entry);
  return status;
}

static const char *job_state_name(ArkshJobState state) {
  switch (state) {
    case ARKSH_JOB_STOPPED:
      return "stopped";
    case ARKSH_JOB_DONE:
      return "done";
    case ARKSH_JOB_RUNNING:
    default:
      return "running";
  }
}

static int value_is_map_like(const ArkshValue *value) {
  return value != NULL && (value->kind == ARKSH_VALUE_MAP || value->kind == ARKSH_VALUE_DICT);
}

static int render_argument_key(const ArkshValue *value, char *out, size_t out_size, char *error, size_t error_size) {
  if (value == NULL || out == NULL || out_size == 0 || error == NULL || error_size == 0) {
    return 1;
  }

  if (arksh_value_render(value, out, out_size) != 0) {
    snprintf(error, error_size, "unable to render map key");
    return 1;
  }
  return 0;
}

static ArkshValue *allocate_runtime_value(char *error, size_t error_size, const char *label) {
  ArkshValue *value = (ArkshValue *) calloc(1, sizeof(*value));

  if (value == NULL && error != NULL && error_size > 0) {
    snprintf(error, error_size, "unable to allocate %s", label == NULL ? "runtime value" : label);
  }
  return value;
}

static void free_class_definition_contents(ArkshClassDef *class_def) {
  size_t i;

  if (class_def == NULL) {
    return;
  }

  for (i = 0; i < class_def->property_count; ++i) {
    arksh_value_free(&class_def->properties[i].default_value);
  }
  memset(class_def, 0, sizeof(*class_def));
}

static const ArkshClassDef *resolve_class_for_lookup(const ArkshShell *shell, const ArkshClassDef *pending, const char *name) {
  if (pending != NULL && name != NULL && strcmp(pending->name, name) == 0) {
    return pending;
  }
  return find_class_entry_const(shell, name);
}

static int class_chain_contains(
  const ArkshShell *shell,
  const ArkshClassDef *pending,
  const char *start_name,
  const char *target_name,
  int depth
) {
  const ArkshClassDef *start_class;
  int i;

  if (shell == NULL || start_name == NULL || target_name == NULL || depth > ARKSH_MAX_CLASSES) {
    return 0;
  }

  start_class = resolve_class_for_lookup(shell, pending, start_name);
  if (start_class == NULL) {
    return 0;
  }

  for (i = 0; i < start_class->base_count; ++i) {
    if (strcmp(start_class->bases[i], target_name) == 0) {
      return 1;
    }
    if (class_chain_contains(shell, pending, start_class->bases[i], target_name, depth + 1)) {
      return 1;
    }
  }

  return 0;
}

static const ArkshClassProperty *find_property_in_class(const ArkshClassDef *class_def, const char *name) {
  size_t i;

  if (class_def == NULL || name == NULL) {
    return NULL;
  }

  for (i = 0; i < class_def->property_count; ++i) {
    if (strcmp(class_def->properties[i].name, name) == 0) {
      return &class_def->properties[i];
    }
  }

  return NULL;
}

static const ArkshClassMethod *find_method_in_class(const ArkshClassDef *class_def, const char *name) {
  size_t i;

  if (class_def == NULL || name == NULL) {
    return NULL;
  }

  for (i = 0; i < class_def->method_count; ++i) {
    if (strcmp(class_def->methods[i].name, name) == 0) {
      return &class_def->methods[i];
    }
  }

  return NULL;
}

static const ArkshClassProperty *lookup_property_recursive(
  const ArkshShell *shell,
  const char *class_name,
  const char *property_name,
  int depth
) {
  const ArkshClassDef *class_def;
  const ArkshClassProperty *property;
  int i;

  if (shell == NULL || class_name == NULL || property_name == NULL || depth > ARKSH_MAX_CLASSES) {
    return NULL;
  }

  class_def = find_class_entry_const(shell, class_name);
  if (class_def == NULL) {
    return NULL;
  }

  property = find_property_in_class(class_def, property_name);
  if (property != NULL) {
    return property;
  }

  for (i = 0; i < class_def->base_count; ++i) {
    property = lookup_property_recursive(shell, class_def->bases[i], property_name, depth + 1);
    if (property != NULL) {
      return property;
    }
  }

  return NULL;
}

static const ArkshClassMethod *lookup_method_recursive(
  const ArkshShell *shell,
  const char *class_name,
  const char *method_name,
  int depth
) {
  const ArkshClassDef *class_def;
  const ArkshClassMethod *method;
  int i;

  if (shell == NULL || class_name == NULL || method_name == NULL || depth > ARKSH_MAX_CLASSES) {
    return NULL;
  }

  class_def = find_class_entry_const(shell, class_name);
  if (class_def == NULL) {
    return NULL;
  }

  method = find_method_in_class(class_def, method_name);
  if (method != NULL) {
    return method;
  }

  for (i = 0; i < class_def->base_count; ++i) {
    method = lookup_method_recursive(shell, class_def->bases[i], method_name, depth + 1);
    if (method != NULL) {
      return method;
    }
  }

  return NULL;
}

static int find_next_class_body_separator(const char *text, size_t start, size_t *out_index) {
  size_t i;
  char quote = '\0';
  int paren_depth = 0;
  int bracket_depth = 0;

  if (text == NULL || out_index == NULL) {
    return 1;
  }

  for (i = start; text[i] != '\0'; ++i) {
    char c = text[i];

    if (quote != '\0') {
      if (c == quote) {
        quote = '\0';
      } else if (quote == '"' && text[i + 1] != '\0' && c == '\\') {
        i++;
      }
      continue;
    }

    if (c == '"' || c == '\'') {
      quote = c;
      continue;
    }
    if (c == '(') {
      paren_depth++;
      continue;
    }
    if (c == ')' && paren_depth > 0) {
      paren_depth--;
      continue;
    }
    if (c == '[') {
      bracket_depth++;
      continue;
    }
    if (c == ']' && bracket_depth > 0) {
      bracket_depth--;
      continue;
    }
    if ((c == ';' || c == '\n' || c == '\r') && paren_depth == 0 && bracket_depth == 0) {
      *out_index = i;
      return 0;
    }
  }

  return 1;
}

static int find_top_level_assignment_in_text(const char *text, size_t *out_index) {
  size_t i;
  char quote = '\0';
  int paren_depth = 0;
  int bracket_depth = 0;

  if (text == NULL || out_index == NULL) {
    return 1;
  }

  for (i = 0; text[i] != '\0'; ++i) {
    char c = text[i];

    if (quote != '\0') {
      if (c == quote) {
        quote = '\0';
      } else if (quote == '"' && text[i + 1] != '\0' && c == '\\') {
        i++;
      }
      continue;
    }

    if (c == '"' || c == '\'') {
      quote = c;
      continue;
    }
    if (c == '(') {
      paren_depth++;
      continue;
    }
    if (c == ')' && paren_depth > 0) {
      paren_depth--;
      continue;
    }
    if (c == '[') {
      bracket_depth++;
      continue;
    }
    if (c == ']' && bracket_depth > 0) {
      bracket_depth--;
      continue;
    }
    if (c == '=' && paren_depth == 0 && bracket_depth == 0) {
      if ((i > 0 && text[i - 1] == '=') || text[i + 1] == '=') {
        continue;
      }
      *out_index = i;
      return 0;
    }
  }

  return 1;
}

static int parse_class_member_definition(
  const char *text,
  ArkshMemberKind *out_member_kind,
  char *name,
  size_t name_size,
  char *expression,
  size_t expression_size
) {
  char trimmed[ARKSH_MAX_LINE];
  const char *cursor;
  size_t token_len = 0;
  size_t operator_index = 0;

  if (text == NULL || out_member_kind == NULL || name == NULL || expression == NULL || name_size == 0 || expression_size == 0) {
    return 1;
  }

  trim_copy(text, trimmed, sizeof(trimmed));
  if (trimmed[0] == '\0' || trimmed[0] == '#') {
    return 1;
  }

  cursor = trimmed;
  if (strncmp(cursor, "property", 8) == 0 && isspace((unsigned char) cursor[8])) {
    *out_member_kind = ARKSH_MEMBER_PROPERTY;
    cursor += 8;
  } else if (strncmp(cursor, "method", 6) == 0 && isspace((unsigned char) cursor[6])) {
    *out_member_kind = ARKSH_MEMBER_METHOD;
    cursor += 6;
  } else {
    return 2;
  }

  while (*cursor != '\0' && isspace((unsigned char) *cursor)) {
    cursor++;
  }

  while (cursor[token_len] != '\0' && (isalnum((unsigned char) cursor[token_len]) || cursor[token_len] == '_')) {
    token_len++;
  }
  if (token_len == 0 || token_len >= name_size) {
    return 2;
  }

  memcpy(name, cursor, token_len);
  name[token_len] = '\0';
  if (!is_valid_identifier(name)) {
    return 2;
  }
  cursor += token_len;

  while (*cursor != '\0' && isspace((unsigned char) *cursor)) {
    cursor++;
  }

  if (find_top_level_assignment_in_text(cursor, &operator_index) != 0) {
    return 2;
  }

  {
    char raw_expression[ARKSH_MAX_LINE];

    copy_string(raw_expression, sizeof(raw_expression), cursor + operator_index + 1);
    trim_copy(raw_expression, expression, expression_size);
  }
  return expression[0] == '\0' ? 2 : 0;
}

static int class_definition_add_property(ArkshClassDef *class_def, const char *name, const ArkshValue *value) {
  size_t i;

  if (class_def == NULL || name == NULL || value == NULL) {
    return 1;
  }

  for (i = 0; i < class_def->property_count; ++i) {
    if (strcmp(class_def->properties[i].name, name) == 0) {
      arksh_value_free(&class_def->properties[i].default_value);
      return arksh_value_copy(&class_def->properties[i].default_value, value);
    }
  }

  if (class_def->property_count >= ARKSH_MAX_CLASS_PROPERTIES) {
    return 1;
  }

  copy_string(class_def->properties[class_def->property_count].name, sizeof(class_def->properties[class_def->property_count].name), name);
  if (arksh_value_copy(&class_def->properties[class_def->property_count].default_value, value) != 0) {
    return 1;
  }
  class_def->property_count++;
  return 0;
}

static int class_definition_add_method(ArkshClassDef *class_def, const char *name, const ArkshBlock *block) {
  size_t i;

  if (class_def == NULL || name == NULL || block == NULL) {
    return 1;
  }

  for (i = 0; i < class_def->method_count; ++i) {
    if (strcmp(class_def->methods[i].name, name) == 0) {
      class_def->methods[i].block = *block;
      return 0;
    }
  }

  if (class_def->method_count >= ARKSH_MAX_CLASS_METHODS) {
    return 1;
  }

  copy_string(class_def->methods[class_def->method_count].name, sizeof(class_def->methods[class_def->method_count].name), name);
  class_def->methods[class_def->method_count].block = *block;
  class_def->method_count++;
  return 0;
}

const ArkshClassDef *arksh_shell_find_class(const ArkshShell *shell, const char *name) {
  if (name == NULL || name[0] == '\0') {
    return NULL;
  }

  return find_class_entry_const(shell, name);
}

const ArkshClassInstance *arksh_shell_find_instance(const ArkshShell *shell, int id) {
  return find_instance_entry_const(shell, id);
}

static int build_class_name_list_recursive(
  const ArkshShell *shell,
  const char *class_name,
  int want_methods,
  ArkshValue *out_list,
  int depth
) {
  const ArkshClassDef *class_def;
  size_t i;

  if (shell == NULL || class_name == NULL || out_list == NULL || depth > ARKSH_MAX_CLASSES) {
    return 1;
  }

  class_def = find_class_entry_const(shell, class_name);
  if (class_def == NULL) {
    return 1;
  }

  for (i = 0; i < class_def->base_count; ++i) {
    if (build_class_name_list_recursive(shell, class_def->bases[i], want_methods, out_list, depth + 1) != 0) {
      return 1;
    }
  }

  if (want_methods) {
    for (i = 0; i < class_def->method_count; ++i) {
      if (map_add_string_entry(out_list, class_def->methods[i].name, "method") != 0) {
        return 1;
      }
    }
  } else {
    for (i = 0; i < class_def->property_count; ++i) {
      if (map_add_string_entry(out_list, class_def->properties[i].name, "property") != 0) {
        return 1;
      }
    }
  }

  return 0;
}

int arksh_shell_set_class(ArkshShell *shell, const ArkshClassCommandNode *class_node, char *out, size_t out_size) {
  ArkshClassDef *candidate;
  ArkshClassDef *entry;
  size_t offset = 0;

  if (shell == NULL || class_node == NULL || out == NULL || out_size == 0 || !is_valid_identifier(class_node->name)) {
    return 1;
  }

  candidate = (ArkshClassDef *) calloc(1, sizeof(*candidate));
  if (candidate == NULL) {
    snprintf(out, out_size, "unable to allocate class definition");
    return 1;
  }

  copy_string(candidate->name, sizeof(candidate->name), class_node->name);
  copy_string(candidate->source, sizeof(candidate->source), class_node->source);
  candidate->base_count = class_node->base_count;
  for (int i = 0; i < class_node->base_count && i < ARKSH_MAX_CLASS_BASES; ++i) {
    copy_string(candidate->bases[i], sizeof(candidate->bases[i]), class_node->bases[i]);
  }

  for (int i = 0; i < candidate->base_count; ++i) {
    if (strcmp(candidate->bases[i], candidate->name) == 0) {
      snprintf(out, out_size, "class %s cannot extend itself", candidate->name);
      free(candidate);
      return 1;
    }
    if (find_class_entry_const(shell, candidate->bases[i]) == NULL) {
      snprintf(out, out_size, "unknown base class: %s", candidate->bases[i]);
      free(candidate);
      return 1;
    }
    if (class_chain_contains(shell, candidate, candidate->bases[i], candidate->name, 0)) {
      snprintf(out, out_size, "class inheritance cycle detected through %s", candidate->bases[i]);
      free(candidate);
      return 1;
    }
  }

  while (class_node->body[offset] != '\0') {
    size_t end = 0;
    char segment[ARKSH_MAX_LINE];
    ArkshMemberKind member_kind;
    char name[ARKSH_MAX_NAME];
    char expression[ARKSH_MAX_LINE];

    if (find_next_class_body_separator(class_node->body, offset, &end) != 0) {
      end = strlen(class_node->body);
    }

    copy_string(segment, sizeof(segment), "");
    append_slice(segment, sizeof(segment), class_node->body, offset, end);
    trim_copy(segment, segment, sizeof(segment));
    if (segment[0] != '\0' && segment[0] != '#') {
      ArkshValue *value = allocate_runtime_value(out, out_size, "class member value");

      if (value == NULL) {
        free(candidate);
        return 1;
      }

      if (parse_class_member_definition(segment, &member_kind, name, sizeof(name), expression, sizeof(expression)) != 0) {
        free(value);
        free_class_definition_contents(candidate);
        free(candidate);
        snprintf(out, out_size, "invalid class member definition: %s", segment);
        return 1;
      }

      if (arksh_evaluate_line_value(shell, expression, value, out, out_size) != 0) {
        free(value);
        free_class_definition_contents(candidate);
        free(candidate);
        return 1;
      }

      if (member_kind == ARKSH_MEMBER_PROPERTY) {
        if (class_definition_add_property(candidate, name, value) != 0) {
          arksh_value_free(value);
          free(value);
          free_class_definition_contents(candidate);
          free(candidate);
          snprintf(out, out_size, "unable to register class property: %s", name);
          return 1;
        }
      } else {
        if (value->kind != ARKSH_VALUE_BLOCK) {
          arksh_value_free(value);
          free(value);
          free_class_definition_contents(candidate);
          free(candidate);
          snprintf(out, out_size, "class methods must be block values: %s", name);
          return 1;
        }
        if (class_definition_add_method(candidate, name, &value->block) != 0) {
          arksh_value_free(value);
          free(value);
          free_class_definition_contents(candidate);
          free(candidate);
          snprintf(out, out_size, "unable to register class method: %s", name);
          return 1;
        }
      }

      arksh_value_free(value);
      free(value);
    }

    if (class_node->body[end] == '\0') {
      break;
    }
    offset = end + 1;
  }

  entry = find_class_entry(shell, candidate->name);
  if (entry == NULL) {
    if (shell->class_count >= ARKSH_MAX_CLASSES) {
      free_class_definition_contents(candidate);
      free(candidate);
      snprintf(out, out_size, "class limit reached");
      return 1;
    }
    entry = &shell->classes[shell->class_count++];
    memset(entry, 0, sizeof(*entry));
  } else {
    free_class_definition_contents(entry);
  }

  *entry = *candidate;
  free(candidate);
  out[0] = '\0';
  return 0;
}

static int populate_instance_defaults_recursive(
  ArkshShell *shell,
  const char *class_name,
  ArkshValue *fields,
  char *out,
  size_t out_size,
  int depth
) {
  const ArkshClassDef *class_def;
  size_t i;

  if (shell == NULL || class_name == NULL || fields == NULL || out == NULL || out_size == 0 || depth > ARKSH_MAX_CLASSES) {
    return 1;
  }

  class_def = find_class_entry_const(shell, class_name);
  if (class_def == NULL) {
    snprintf(out, out_size, "unknown class: %s", class_name);
    return 1;
  }

  for (i = (size_t) class_def->base_count; i > 0; --i) {
    if (populate_instance_defaults_recursive(shell, class_def->bases[i - 1], fields, out, out_size, depth + 1) != 0) {
      return 1;
    }
  }

  for (i = 0; i < class_def->property_count; ++i) {
    if (arksh_value_map_set(fields, class_def->properties[i].name, &class_def->properties[i].default_value) != 0) {
      snprintf(out, out_size, "instance field map is too large");
      return 1;
    }
  }

  return 0;
}

static int rollback_last_instance(ArkshShell *shell) {
  if (shell == NULL || shell->instance_count == 0) {
    return 1;
  }

  arksh_value_free(&shell->instances[shell->instance_count - 1].fields);
  memset(&shell->instances[shell->instance_count - 1], 0, sizeof(shell->instances[shell->instance_count - 1]));
  shell->instance_count--;
  return 0;
}

static int class_is_a_recursive(const ArkshShell *shell, const char *class_name, const char *target_name, int depth) {
  const ArkshClassDef *class_def;
  int i;

  if (shell == NULL || class_name == NULL || target_name == NULL || depth > ARKSH_MAX_CLASSES) {
    return 0;
  }

  if (strcmp(class_name, target_name) == 0) {
    return 1;
  }

  class_def = find_class_entry_const(shell, class_name);
  if (class_def == NULL) {
    return 0;
  }

  for (i = 0; i < class_def->base_count; ++i) {
    if (class_is_a_recursive(shell, class_def->bases[i], target_name, depth + 1)) {
      return 1;
    }
  }

  return 0;
}

int arksh_shell_instantiate_class(
  ArkshShell *shell,
  const char *name,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *out,
  size_t out_size
) {
  const ArkshClassDef *class_def;
  ArkshClassInstance *instance;
  ArkshValue *instance_value;

  if (shell == NULL || name == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  class_def = find_class_entry_const(shell, name);
  if (class_def == NULL) {
    snprintf(out, out_size, "unknown class: %s", name);
    return 1;
  }
  if (shell->instance_count >= ARKSH_MAX_INSTANCES) {
    snprintf(out, out_size, "instance limit reached");
    return 1;
  }

  instance = &shell->instances[shell->instance_count++];
  memset(instance, 0, sizeof(*instance));
  instance->id = shell->next_instance_id++;
  copy_string(instance->class_name, sizeof(instance->class_name), name);
  arksh_value_set_map(&instance->fields);

  instance_value = allocate_runtime_value(out, out_size, "instance value");
  if (instance_value == NULL) {
    rollback_last_instance(shell);
    return 1;
  }

  if (populate_instance_defaults_recursive(shell, name, &instance->fields, out, out_size, 0) != 0) {
    free(instance_value);
    rollback_last_instance(shell);
    return 1;
  }

  arksh_value_set_instance(instance_value, name, instance->id);
  if (lookup_method_recursive(shell, name, "init", 0) != NULL) {
    ArkshValue *ignored_result = allocate_runtime_value(out, out_size, "constructor result");

    if (ignored_result == NULL) {
      free(instance_value);
      rollback_last_instance(shell);
      return 1;
    }
    if (arksh_shell_call_class_method(shell, instance_value, "init", argc, args, ignored_result, out, out_size) != 0) {
      arksh_value_free(ignored_result);
      free(ignored_result);
      free(instance_value);
      rollback_last_instance(shell);
      return 1;
    }
    arksh_value_free(ignored_result);
    free(ignored_result);
  } else if (argc != 0) {
    free(instance_value);
    rollback_last_instance(shell);
    snprintf(out, out_size, "class %s does not define init(), so constructor arguments are not accepted", name);
    return 1;
  }

  arksh_value_set_instance(out_value, name, instance->id);
  free(instance_value);
  out[0] = '\0';
  return 0;
}

static int build_class_property_list(const ArkshShell *shell, const char *class_name, ArkshValue *out_value, char *out, size_t out_size) {
  ArkshValue *seen;
  size_t i;

  if (shell == NULL || class_name == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  arksh_value_init(out_value);
  out_value->kind = ARKSH_VALUE_LIST;
  seen = allocate_runtime_value(out, out_size, "class property set");
  if (seen == NULL) {
    return 1;
  }

  arksh_value_set_map(seen);
  if (build_class_name_list_recursive(shell, class_name, 0, seen, 0) != 0) {
    arksh_value_free(seen);
    free(seen);
    snprintf(out, out_size, "unable to build class property list");
    return 1;
  }

  for (i = 0; i < seen->map.count; ++i) {
    ArkshValue *item = allocate_runtime_value(out, out_size, "class property item");

    if (item == NULL) {
      arksh_value_free(seen);
      free(seen);
      return 1;
    }
    arksh_value_set_string(item, seen->map.entries[i].key);
    if (arksh_value_list_append_value(out_value, item) != 0) {
      arksh_value_free(item);
      free(item);
      arksh_value_free(seen);
      free(seen);
      snprintf(out, out_size, "class method list is too large");
      return 1;
    }
    arksh_value_free(item);
    free(item);
  }

  arksh_value_free(seen);
  free(seen);
  return 0;
}

static int build_class_method_list(const ArkshShell *shell, const char *class_name, ArkshValue *out_value, char *out, size_t out_size) {
  ArkshValue *seen;
  size_t i;

  if (shell == NULL || class_name == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  arksh_value_init(out_value);
  out_value->kind = ARKSH_VALUE_LIST;
  seen = allocate_runtime_value(out, out_size, "class method set");
  if (seen == NULL) {
    return 1;
  }

  arksh_value_set_map(seen);
  if (build_class_name_list_recursive(shell, class_name, 1, seen, 0) != 0 ||
      map_add_string_entry(seen, "new", "method") != 0) {
    arksh_value_free(seen);
    free(seen);
    snprintf(out, out_size, "unable to build class method list");
    return 1;
  }

  for (i = 0; i < seen->map.count; ++i) {
    ArkshValue *item = allocate_runtime_value(out, out_size, "class method item");

    if (item == NULL) {
      arksh_value_free(seen);
      free(seen);
      return 1;
    }
    arksh_value_set_string(item, seen->map.entries[i].key);
    if (arksh_value_list_append_value(out_value, item) != 0) {
      arksh_value_free(item);
      free(item);
      arksh_value_free(seen);
      free(seen);
      snprintf(out, out_size, "class property list is too large");
      return 1;
    }
    arksh_value_free(item);
    free(item);
  }

  arksh_value_free(seen);
  free(seen);
  return 0;
}

int arksh_shell_get_class_property_value(
  ArkshShell *shell,
  const ArkshValue *receiver,
  const char *property,
  ArkshValue *out_value,
  char *out,
  size_t out_size
) {
  if (shell == NULL || receiver == NULL || property == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (receiver->kind == ARKSH_VALUE_CLASS) {
    const ArkshClassDef *class_def = find_class_entry_const(shell, receiver->text);
    size_t i;

    if (class_def == NULL) {
      snprintf(out, out_size, "unknown class: %s", receiver->text);
      return 1;
    }

    if (strcmp(property, "type") == 0 || strcmp(property, "value_type") == 0) {
      arksh_value_set_string(out_value, strcmp(property, "type") == 0 ? "class" : "class");
      return 0;
    }
    if (strcmp(property, "name") == 0) {
      arksh_value_set_string(out_value, class_def->name);
      return 0;
    }
    if (strcmp(property, "source") == 0) {
      arksh_value_set_string(out_value, class_def->source);
      return 0;
    }
    if (strcmp(property, "base_count") == 0) {
      arksh_value_set_number(out_value, (double) class_def->base_count);
      return 0;
    }
    if (strcmp(property, "bases") == 0) {
      arksh_value_init(out_value);
      out_value->kind = ARKSH_VALUE_LIST;
      for (i = 0; i < (size_t) class_def->base_count; ++i) {
        ArkshValue *base_value = allocate_runtime_value(out, out_size, "base class value");

        if (base_value == NULL) {
          return 1;
        }
        arksh_value_set_string(base_value, class_def->bases[i]);
        if (arksh_value_list_append_value(out_value, base_value) != 0) {
          arksh_value_free(base_value);
          free(base_value);
          snprintf(out, out_size, "base class list is too large");
          return 1;
        }
        arksh_value_free(base_value);
        free(base_value);
      }
      return 0;
    }
    if (strcmp(property, "property_count") == 0) {
      ArkshValue *names = allocate_runtime_value(out, out_size, "class property names");

      if (names == NULL) {
        return 1;
      }
      if (build_class_property_list(shell, class_def->name, names, out, out_size) != 0) {
        free(names);
        return 1;
      }
      arksh_value_set_number(out_value, (double) names->list.count);
      arksh_value_free(names);
      free(names);
      return 0;
    }
    if (strcmp(property, "method_count") == 0) {
      ArkshValue *names = allocate_runtime_value(out, out_size, "class method names");

      if (names == NULL) {
        return 1;
      }
      if (build_class_method_list(shell, class_def->name, names, out, out_size) != 0) {
        free(names);
        return 1;
      }
      arksh_value_set_number(out_value, (double) names->list.count);
      arksh_value_free(names);
      free(names);
      return 0;
    }
    if (strcmp(property, "properties") == 0) {
      return build_class_property_list(shell, class_def->name, out_value, out, out_size);
    }
    if (strcmp(property, "methods") == 0) {
      return build_class_method_list(shell, class_def->name, out_value, out, out_size);
    }

    snprintf(out, out_size, "unknown property: %s", property);
    return 1;
  }

  if (receiver->kind == ARKSH_VALUE_INSTANCE) {
    const ArkshClassInstance *instance = find_instance_entry_const(shell, (int) receiver->number);
    const ArkshValueItem *field_item;

    if (instance == NULL) {
      snprintf(out, out_size, "unknown instance: %s#%d", receiver->text, (int) receiver->number);
      return 1;
    }

    if (strcmp(property, "type") == 0) {
      arksh_value_set_string(out_value, instance->class_name);
      return 0;
    }
    if (strcmp(property, "value_type") == 0) {
      arksh_value_set_string(out_value, "instance");
      return 0;
    }
    if (strcmp(property, "id") == 0) {
      arksh_value_set_number(out_value, receiver->number);
      return 0;
    }
    if (strcmp(property, "class") == 0 || strcmp(property, "class_name") == 0) {
      arksh_value_set_string(out_value, instance->class_name);
      return 0;
    }
    if (strcmp(property, "fields") == 0) {
      return arksh_value_copy(out_value, &instance->fields);
    }
    if (strcmp(property, "properties") == 0) {
      return build_class_property_list(shell, instance->class_name, out_value, out, out_size);
    }
    if (strcmp(property, "methods") == 0) {
      return build_class_method_list(shell, instance->class_name, out_value, out, out_size);
    }
    if (strcmp(property, "property_count") == 0) {
      arksh_value_set_number(out_value, (double) instance->fields.map.count);
      return 0;
    }

    field_item = arksh_value_map_get_item(&instance->fields, property);
    if (field_item != NULL) {
      return arksh_value_set_from_item(out_value, field_item);
    }

    {
      const ArkshClassProperty *property_def = lookup_property_recursive(shell, instance->class_name, property, 0);

      if (property_def != NULL) {
        return arksh_value_copy(out_value, &property_def->default_value);
      }
    }

    snprintf(out, out_size, "unknown property: %s", property);
    return 1;
  }

  snprintf(out, out_size, "receiver is not a class or instance");
  return 1;
}

int arksh_shell_call_class_method(
  ArkshShell *shell,
  const ArkshValue *receiver,
  const char *method,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *out,
  size_t out_size
) {
  if (shell == NULL || receiver == NULL || method == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (receiver->kind == ARKSH_VALUE_CLASS) {
    if (strcmp(method, "new") == 0) {
      return arksh_shell_instantiate_class(shell, receiver->text, argc, args, out_value, out, out_size);
    }

    snprintf(out, out_size, "unknown method: %s", method);
    return 1;
  }

  if (receiver->kind != ARKSH_VALUE_INSTANCE) {
    snprintf(out, out_size, "receiver is not a class instance");
    return 1;
  }

  {
    ArkshClassInstance *instance = find_instance_entry(shell, (int) receiver->number);

    if (instance == NULL) {
      snprintf(out, out_size, "unknown instance: %s#%d", receiver->text, (int) receiver->number);
      return 1;
    }

    if (strcmp(method, "set") == 0) {
      char key[ARKSH_MAX_NAME];

      if (argc != 2) {
        snprintf(out, out_size, "set() expects a field name and a value");
        return 1;
      }
      if (render_argument_key(&args[0], key, sizeof(key), out, out_size) != 0) {
        return 1;
      }
      if (!is_valid_identifier(key)) {
        snprintf(out, out_size, "invalid field name: %s", key);
        return 1;
      }
      if (arksh_value_map_set(&instance->fields, key, &args[1]) != 0) {
        snprintf(out, out_size, "unable to set instance field: %s", key);
        return 1;
      }
      arksh_value_set_instance(out_value, instance->class_name, instance->id);
      return 0;
    }

    if (strcmp(method, "get") == 0) {
      char key[ARKSH_MAX_NAME];
      ArkshValue *property_value;

      if (argc != 1) {
        snprintf(out, out_size, "get() expects exactly one field name");
        return 1;
      }
      if (render_argument_key(&args[0], key, sizeof(key), out, out_size) != 0) {
        return 1;
      }
      property_value = allocate_runtime_value(out, out_size, "instance property value");
      if (property_value == NULL) {
        return 1;
      }
      if (arksh_shell_get_class_property_value(shell, receiver, key, property_value, out, out_size) != 0) {
        free(property_value);
        return 1;
      }
      *out_value = *property_value;
      free(property_value);
      return 0;
    }

    if (strcmp(method, "isa") == 0) {
      char class_name[ARKSH_MAX_NAME];

      if (argc != 1) {
        snprintf(out, out_size, "isa() expects exactly one class name");
        return 1;
      }
      if (render_argument_key(&args[0], class_name, sizeof(class_name), out, out_size) != 0) {
        return 1;
      }
      arksh_value_set_boolean(out_value, class_is_a_recursive(shell, instance->class_name, class_name, 0));
      return 0;
    }

    {
      const ArkshClassMethod *method_def = lookup_method_recursive(shell, instance->class_name, method, 0);
      ArkshValue *block_args;
      int i;
      int status;

      if (method_def == NULL) {
        snprintf(out, out_size, "unknown method: %s", method);
        return 1;
      }

      if (argc + 1 > ARKSH_MAX_ARGS) {
        snprintf(out, out_size, "too many method arguments");
        return 1;
      }

      block_args = (ArkshValue *) calloc((size_t) argc + 1, sizeof(*block_args));
      if (block_args == NULL) {
        snprintf(out, out_size, "unable to allocate class method arguments");
        return 1;
      }

      arksh_value_set_instance(&block_args[0], instance->class_name, instance->id);
      for (i = 0; i < argc; ++i) {
        if (arksh_value_copy(&block_args[i + 1], &args[i]) != 0) {
          int rollback;

          for (rollback = 0; rollback <= i; ++rollback) {
            arksh_value_free(&block_args[rollback]);
          }
          free(block_args);
          snprintf(out, out_size, "unable to prepare class method arguments");
          return 1;
        }
      }

      status = arksh_execute_block(shell, &method_def->block, block_args, argc + 1, out_value, out, out_size);
      for (i = 0; i < argc + 1; ++i) {
        arksh_value_free(&block_args[i]);
      }
      free(block_args);
      return status;
    }
  }
}

static int resolver_map(
  ArkshShell *shell,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  int i;

  (void) shell;

  if (out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (argc % 2 != 0) {
    snprintf(error, error_size, "map() expects alternating key/value arguments");
    return 1;
  }

  arksh_value_set_map(out_value);
  for (i = 0; i < argc; i += 2) {
    char key[ARKSH_MAX_NAME];

    if (render_argument_key(&args[i], key, sizeof(key), error, error_size) != 0) {
      return 1;
    }
    if (arksh_value_map_set(out_value, key, &args[i + 1]) != 0) {
      snprintf(error, error_size, "map() is too large");
      return 1;
    }
  }

  return 0;
}

static int resolver_env(
  ArkshShell *shell,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  if (shell == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (argc == 1) {
    char key[ARKSH_MAX_NAME];
    const char *value;

    if (render_argument_key(&args[0], key, sizeof(key), error, error_size) != 0) {
      return 1;
    }
    value = arksh_shell_get_var(shell, key);
    if (value == NULL) {
      arksh_value_init(out_value);
      return 0;
    }
    arksh_value_set_string(out_value, value);
    return 0;
  }
  if (argc != 0) {
    snprintf(error, error_size, "env() accepts zero arguments or a single key");
    return 1;
  }

  arksh_value_set_map(out_value);
  {
    ArkshPlatformEnvEntry entries[ARKSH_MAX_SHELL_VARS + 64];
    size_t count = 0;
    size_t i;

    if (arksh_platform_list_environment(entries, sizeof(entries) / sizeof(entries[0]), &count) != 0) {
      snprintf(error, error_size, "unable to enumerate environment");
      return 1;
    }
    for (i = 0; i < count; ++i) {
      if (map_add_string_entry(out_value, entries[i].name, entries[i].value) != 0) {
        snprintf(error, error_size, "environment namespace is too large");
        return 1;
      }
    }
    for (i = 0; i < shell->var_count; ++i) {
      if (map_add_string_entry(out_value, shell->vars[i].name, shell->vars[i].value) != 0) {
        snprintf(error, error_size, "environment namespace is too large");
        return 1;
      }
    }
  }

  return 0;
}

static int resolver_proc(
  ArkshShell *shell,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  ArkshPlatformProcessInfo info;
  char hostname[ARKSH_MAX_NAME];

  (void) args;

  if (shell == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (argc != 0) {
    snprintf(error, error_size, "proc() does not accept arguments");
    return 1;
  }
  if (arksh_platform_get_process_info(&info) != 0) {
    snprintf(error, error_size, "unable to inspect current process");
    return 1;
  }

  arksh_value_set_map(out_value);
  hostname[0] = '\0';
  arksh_platform_gethostname(hostname, sizeof(hostname));
  if (map_add_number_entry(out_value, "pid", (double) info.pid) != 0 ||
      map_add_number_entry(out_value, "ppid", (double) info.ppid) != 0 ||
      map_add_string_entry(out_value, "cwd", shell->cwd) != 0 ||
      map_add_string_entry(out_value, "host", hostname) != 0 ||
      map_add_string_entry(out_value, "os", arksh_platform_os_name()) != 0) {
    snprintf(error, error_size, "process namespace is too large");
    return 1;
  }

  return 0;
}

static int resolver_shell_namespace(
  ArkshShell *shell,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  size_t i;

  (void) args;

  if (shell == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (argc != 0) {
    snprintf(error, error_size, "shell() does not accept arguments");
    return 1;
  }

  arksh_value_set_map(out_value);
  if (map_add_string_entry(out_value, "cwd", shell->cwd) != 0 ||
      map_add_string_entry(out_value, "config_dir", shell->config_dir) != 0 ||
      map_add_string_entry(out_value, "cache_dir", shell->cache_dir) != 0 ||
      map_add_string_entry(out_value, "state_dir", shell->state_dir) != 0 ||
      map_add_string_entry(out_value, "data_dir", shell->data_dir) != 0 ||
      map_add_string_entry(out_value, "plugin_dir", shell->plugin_dir) != 0 ||
      map_add_string_entry(out_value, "history_path", shell->history_path) != 0 ||
      map_add_number_entry(out_value, "last_status", (double) shell->last_status) != 0 ||
      map_add_bool_entry(out_value, "running", shell->running) != 0 ||
      map_add_number_entry(out_value, "history_count", (double) shell->history_count) != 0 ||
      map_add_number_entry(out_value, "var_count", (double) shell->var_count) != 0 ||
      map_add_number_entry(out_value, "binding_count", (double) shell->binding_count) != 0 ||
      map_add_number_entry(out_value, "function_count", (double) shell->function_count) != 0 ||
      map_add_number_entry(out_value, "class_count", (double) shell->class_count) != 0 ||
      map_add_number_entry(out_value, "instance_count", (double) shell->instance_count) != 0 ||
      map_add_number_entry(out_value, "alias_count", (double) shell->alias_count) != 0 ||
      map_add_number_entry(out_value, "plugin_count", (double) shell->plugin_count) != 0 ||
      map_add_number_entry(out_value, "job_count", (double) shell->job_count) != 0 ||
      map_add_string_entry(out_value, "os", arksh_platform_os_name()) != 0) {
    snprintf(error, error_size, "shell namespace is too large");
    return 1;
  }

  {
    ArkshValue *vars = allocate_runtime_value(error, error_size, "shell vars namespace");
    ArkshValue *bindings = allocate_runtime_value(error, error_size, "shell bindings namespace");
    ArkshValue *aliases = allocate_runtime_value(error, error_size, "shell aliases namespace");
    ArkshValue *classes = allocate_runtime_value(error, error_size, "shell classes namespace");
    ArkshValue *instances = allocate_runtime_value(error, error_size, "shell instances namespace");
    ArkshValue *plugins = allocate_runtime_value(error, error_size, "shell plugins namespace");
    ArkshValue *jobs = allocate_runtime_value(error, error_size, "shell jobs namespace");

    if (vars == NULL || bindings == NULL || aliases == NULL || classes == NULL || instances == NULL || plugins == NULL || jobs == NULL) {
      free(vars);
      free(bindings);
      free(aliases);
      free(classes);
      free(instances);
      free(plugins);
      free(jobs);
      return 1;
    }

    arksh_value_set_map(vars);
    arksh_value_set_map(bindings);
    arksh_value_set_map(aliases);
    arksh_value_init(classes);
    classes->kind = ARKSH_VALUE_LIST;
    arksh_value_init(instances);
    instances->kind = ARKSH_VALUE_LIST;
    arksh_value_init(plugins);
    plugins->kind = ARKSH_VALUE_LIST;
    arksh_value_init(jobs);
    jobs->kind = ARKSH_VALUE_LIST;

    for (i = 0; i < shell->var_count; ++i) {
      if (map_add_string_entry(vars, shell->vars[i].name, shell->vars[i].value) != 0) {
        snprintf(error, error_size, "shell namespace is too large");
        arksh_value_free(vars);
        arksh_value_free(bindings);
        arksh_value_free(aliases);
        arksh_value_free(classes);
        arksh_value_free(instances);
        arksh_value_free(plugins);
        arksh_value_free(jobs);
        free(vars);
        free(bindings);
        free(aliases);
        free(classes);
        free(instances);
        free(plugins);
        free(jobs);
        return 1;
      }
    }
    for (i = 0; i < shell->binding_count; ++i) {
      if (map_add_value_entry(bindings, shell->bindings[i].name, &shell->bindings[i].value) != 0) {
        snprintf(error, error_size, "shell namespace is too large");
        arksh_value_free(vars);
        arksh_value_free(bindings);
        arksh_value_free(aliases);
        arksh_value_free(classes);
        arksh_value_free(instances);
        arksh_value_free(plugins);
        arksh_value_free(jobs);
        free(vars);
        free(bindings);
        free(aliases);
        free(classes);
        free(instances);
        free(plugins);
        free(jobs);
        return 1;
      }
    }
    for (i = 0; i < shell->alias_count; ++i) {
      if (map_add_string_entry(aliases, shell->aliases[i].name, shell->aliases[i].value) != 0) {
        snprintf(error, error_size, "shell namespace is too large");
        arksh_value_free(vars);
        arksh_value_free(bindings);
        arksh_value_free(aliases);
        arksh_value_free(classes);
        arksh_value_free(instances);
        arksh_value_free(plugins);
        arksh_value_free(jobs);
        free(vars);
        free(bindings);
        free(aliases);
        free(classes);
        free(instances);
        free(plugins);
        free(jobs);
        return 1;
      }
    }
    for (i = 0; i < shell->class_count; ++i) {
      ArkshValue *class_entry = allocate_runtime_value(error, error_size, "shell class entry");

      if (class_entry == NULL) {
        arksh_value_free(vars);
        arksh_value_free(bindings);
        arksh_value_free(aliases);
        arksh_value_free(classes);
        arksh_value_free(instances);
        arksh_value_free(plugins);
        arksh_value_free(jobs);
        free(vars);
        free(bindings);
        free(aliases);
        free(classes);
        free(instances);
        free(plugins);
        free(jobs);
        return 1;
      }

      arksh_value_set_map(class_entry);
      if (map_add_string_entry(class_entry, "name", shell->classes[i].name) != 0 ||
          map_add_number_entry(class_entry, "base_count", (double) shell->classes[i].base_count) != 0 ||
          map_add_number_entry(class_entry, "property_count", (double) shell->classes[i].property_count) != 0 ||
          map_add_number_entry(class_entry, "method_count", (double) shell->classes[i].method_count) != 0 ||
          arksh_value_list_append_value(classes, class_entry) != 0) {
        arksh_value_free(class_entry);
        free(class_entry);
        snprintf(error, error_size, "shell namespace is too large");
        arksh_value_free(vars);
        arksh_value_free(bindings);
        arksh_value_free(aliases);
        arksh_value_free(classes);
        arksh_value_free(instances);
        arksh_value_free(plugins);
        arksh_value_free(jobs);
        free(vars);
        free(bindings);
        free(aliases);
        free(classes);
        free(instances);
        free(plugins);
        free(jobs);
        return 1;
      }
      arksh_value_free(class_entry);
      free(class_entry);
    }
    for (i = 0; i < shell->instance_count; ++i) {
      ArkshValue *instance_entry = allocate_runtime_value(error, error_size, "shell instance entry");

      if (instance_entry == NULL) {
        arksh_value_free(vars);
        arksh_value_free(bindings);
        arksh_value_free(aliases);
        arksh_value_free(classes);
        arksh_value_free(instances);
        arksh_value_free(plugins);
        arksh_value_free(jobs);
        free(vars);
        free(bindings);
        free(aliases);
        free(classes);
        free(instances);
        free(plugins);
        free(jobs);
        return 1;
      }

      arksh_value_set_map(instance_entry);
      if (map_add_number_entry(instance_entry, "id", (double) shell->instances[i].id) != 0 ||
          map_add_string_entry(instance_entry, "class", shell->instances[i].class_name) != 0 ||
          arksh_value_list_append_value(instances, instance_entry) != 0) {
        arksh_value_free(instance_entry);
        free(instance_entry);
        snprintf(error, error_size, "shell namespace is too large");
        arksh_value_free(vars);
        arksh_value_free(bindings);
        arksh_value_free(aliases);
        arksh_value_free(classes);
        arksh_value_free(instances);
        arksh_value_free(plugins);
        arksh_value_free(jobs);
        free(vars);
        free(bindings);
        free(aliases);
        free(classes);
        free(instances);
        free(plugins);
        free(jobs);
        return 1;
      }
      arksh_value_free(instance_entry);
      free(instance_entry);
    }
    for (i = 0; i < shell->plugin_count; ++i) {
      ArkshValue *plugin_entry = allocate_runtime_value(error, error_size, "shell plugin entry");

      if (plugin_entry == NULL) {
        arksh_value_free(vars);
        arksh_value_free(bindings);
        arksh_value_free(aliases);
        arksh_value_free(classes);
        arksh_value_free(instances);
        arksh_value_free(plugins);
        arksh_value_free(jobs);
        free(vars);
        free(bindings);
        free(aliases);
        free(classes);
        free(instances);
        free(plugins);
        free(jobs);
        return 1;
      }

      arksh_value_set_map(plugin_entry);
      if (map_add_string_entry(plugin_entry, "name", shell->plugins[i].name) != 0 ||
          map_add_string_entry(plugin_entry, "version", shell->plugins[i].version) != 0 ||
          map_add_string_entry(plugin_entry, "description", shell->plugins[i].description) != 0 ||
          map_add_string_entry(plugin_entry, "path", shell->plugins[i].path) != 0 ||
          map_add_bool_entry(plugin_entry, "active", shell->plugins[i].active) != 0 ||
          arksh_value_list_append_value(plugins, plugin_entry) != 0) {
        arksh_value_free(plugin_entry);
        free(plugin_entry);
        snprintf(error, error_size, "shell namespace is too large");
        arksh_value_free(vars);
        arksh_value_free(bindings);
        arksh_value_free(aliases);
        arksh_value_free(classes);
        arksh_value_free(instances);
        arksh_value_free(plugins);
        arksh_value_free(jobs);
        free(vars);
        free(bindings);
        free(aliases);
        free(classes);
        free(instances);
        free(plugins);
        free(jobs);
        return 1;
      }
      arksh_value_free(plugin_entry);
      free(plugin_entry);
    }
    for (i = 0; i < shell->job_count; ++i) {
      ArkshValue *job_entry = allocate_runtime_value(error, error_size, "shell job entry");

      if (job_entry == NULL) {
        arksh_value_free(vars);
        arksh_value_free(bindings);
        arksh_value_free(aliases);
        arksh_value_free(plugins);
        arksh_value_free(jobs);
        free(vars);
        free(bindings);
        free(aliases);
        free(plugins);
        free(jobs);
        return 1;
      }

      arksh_value_set_map(job_entry);
      if (map_add_number_entry(job_entry, "id", (double) shell->jobs[i].id) != 0 ||
          map_add_string_entry(job_entry, "state", job_state_name(shell->jobs[i].state)) != 0 ||
          map_add_number_entry(job_entry, "exit_code", (double) shell->jobs[i].exit_code) != 0 ||
          map_add_string_entry(job_entry, "command", shell->jobs[i].command) != 0 ||
          map_add_number_entry(job_entry, "pid", (double) shell->jobs[i].process.pid) != 0 ||
          map_add_number_entry(job_entry, "pgid", (double) shell->jobs[i].process.pgid) != 0 ||
          arksh_value_list_append_value(jobs, job_entry) != 0) {
        arksh_value_free(job_entry);
        free(job_entry);
        snprintf(error, error_size, "shell namespace is too large");
        arksh_value_free(vars);
        arksh_value_free(bindings);
        arksh_value_free(aliases);
        arksh_value_free(classes);
        arksh_value_free(instances);
        arksh_value_free(plugins);
        arksh_value_free(jobs);
        free(vars);
        free(bindings);
        free(aliases);
        free(classes);
        free(instances);
        free(plugins);
        free(jobs);
        return 1;
      }
      arksh_value_free(job_entry);
      free(job_entry);
    }

    if (map_add_value_entry(out_value, "vars", vars) != 0 ||
        map_add_value_entry(out_value, "bindings", bindings) != 0 ||
        map_add_value_entry(out_value, "aliases", aliases) != 0 ||
        map_add_value_entry(out_value, "classes", classes) != 0 ||
        map_add_value_entry(out_value, "instances", instances) != 0 ||
        map_add_value_entry(out_value, "plugins", plugins) != 0 ||
        map_add_value_entry(out_value, "jobs", jobs) != 0) {
      snprintf(error, error_size, "shell namespace is too large");
      arksh_value_free(vars);
      arksh_value_free(bindings);
      arksh_value_free(aliases);
      arksh_value_free(classes);
      arksh_value_free(instances);
      arksh_value_free(plugins);
      arksh_value_free(jobs);
      free(vars);
      free(bindings);
      free(aliases);
      free(classes);
      free(instances);
      free(plugins);
      free(jobs);
      return 1;
    }

    arksh_value_free(vars);
    arksh_value_free(bindings);
    arksh_value_free(aliases);
    arksh_value_free(classes);
    arksh_value_free(instances);
    arksh_value_free(plugins);
    arksh_value_free(jobs);
    free(vars);
    free(bindings);
    free(aliases);
    free(classes);
    free(instances);
    free(plugins);
    free(jobs);
  }

  return 0;
}

/* E6-S1-T1: fs() — filesystem namespace */
static int resolver_fs(
  ArkshShell *shell,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  const char *home;
  const char *tmp_dir;

  (void) args;

  if (shell == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (argc != 0) {
    snprintf(error, error_size, "fs() does not accept arguments");
    return 1;
  }

  home = getenv("HOME");
#ifdef _WIN32
  if (home == NULL) {
    home = getenv("USERPROFILE");
  }
  tmp_dir = getenv("TEMP");
  if (tmp_dir == NULL) {
    tmp_dir = getenv("TMP");
  }
  if (tmp_dir == NULL) {
    tmp_dir = "C:\\Temp";
  }
#else
  tmp_dir = "/tmp";
#endif

  if (home == NULL) {
    home = "";
  }

  arksh_value_set_map(out_value);
  if (map_add_string_entry(out_value, "cwd", shell->cwd) != 0 ||
      map_add_string_entry(out_value, "home", home) != 0 ||
      map_add_string_entry(out_value, "temp", tmp_dir) != 0 ||
      map_add_string_entry(out_value, "separator", arksh_platform_path_separator()) != 0) {
    snprintf(error, error_size, "fs namespace is too large");
    return 1;
  }

  return 0;
}

/* E6-S1-T2: user() — current user namespace */
static int resolver_user(
  ArkshShell *shell,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  const char *username;
  const char *home;
  const char *login_shell;

  (void) shell;
  (void) args;

  if (out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (argc != 0) {
    snprintf(error, error_size, "user() does not accept arguments");
    return 1;
  }

  username = getenv("USER");
#ifdef _WIN32
  if (username == NULL) {
    username = getenv("USERNAME");
  }
#else
  if (username == NULL) {
    username = getenv("LOGNAME");
  }
#endif
  if (username == NULL) {
    username = "";
  }

  home = getenv("HOME");
#ifdef _WIN32
  if (home == NULL) {
    home = getenv("USERPROFILE");
  }
#endif
  if (home == NULL) {
    home = "";
  }

  login_shell = getenv("SHELL");
  if (login_shell == NULL) {
    login_shell = "";
  }

  arksh_value_set_map(out_value);
  if (map_add_string_entry(out_value, "name", username) != 0 ||
      map_add_string_entry(out_value, "home", home) != 0 ||
      map_add_string_entry(out_value, "shell", login_shell) != 0) {
    snprintf(error, error_size, "user namespace is too large");
    return 1;
  }

#ifndef _WIN32
  if (map_add_number_entry(out_value, "uid", (double) getuid()) != 0 ||
      map_add_number_entry(out_value, "gid", (double) getgid()) != 0) {
    snprintf(error, error_size, "user namespace is too large");
    return 1;
  }
#endif

  return 0;
}

/* E6-S1-T3: sys() — system/hardware namespace */
static int resolver_sys(
  ArkshShell *shell,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  char hostname[ARKSH_MAX_NAME];
  int cpu_count = 1;
  const char *arch;

  (void) shell;
  (void) args;

  if (out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (argc != 0) {
    snprintf(error, error_size, "sys() does not accept arguments");
    return 1;
  }

  hostname[0] = '\0';
  arksh_platform_gethostname(hostname, sizeof(hostname));

#ifdef _WIN32
  {
    const char *nop = getenv("NUMBER_OF_PROCESSORS");
    if (nop != NULL) {
      cpu_count = atoi(nop);
      if (cpu_count < 1) {
        cpu_count = 1;
      }
    }
  }
#else
  {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n > 0) {
      cpu_count = (int) n;
    }
  }
#endif

#if defined(__x86_64__) || defined(_M_X64)
  arch = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
  arch = "arm64";
#elif defined(__i386__) || defined(_M_IX86)
  arch = "x86";
#elif defined(__arm__) || defined(_M_ARM)
  arch = "arm";
#else
  arch = "unknown";
#endif

  arksh_value_set_map(out_value);
  if (map_add_string_entry(out_value, "os", arksh_platform_os_name()) != 0 ||
      map_add_string_entry(out_value, "host", hostname) != 0 ||
      map_add_string_entry(out_value, "arch", arch) != 0 ||
      map_add_number_entry(out_value, "cpu_count", (double) cpu_count) != 0) {
    snprintf(error, error_size, "sys namespace is too large");
    return 1;
  }

  return 0;
}

/* E6-S1-T3: time() — current local time namespace */
static int resolver_time(
  ArkshShell *shell,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  time_t now;
  struct tm tm_buf;
  struct tm *tm_info;
  char iso[32];

  (void) shell;
  (void) args;

  if (out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (argc != 0) {
    snprintf(error, error_size, "time() does not accept arguments");
    return 1;
  }

  now = time(NULL);
#ifdef _WIN32
  localtime_s(&tm_buf, &now);
  tm_info = &tm_buf;
#else
  tm_info = localtime_r(&now, &tm_buf);
#endif

  snprintf(iso, sizeof(iso), "%04d-%02d-%02dT%02d:%02d:%02d",
           tm_info->tm_year + 1900,
           tm_info->tm_mon + 1,
           tm_info->tm_mday,
           tm_info->tm_hour,
           tm_info->tm_min,
           tm_info->tm_sec);

  arksh_value_set_map(out_value);
  if (map_add_number_entry(out_value, "epoch",  (double) now) != 0 ||
      map_add_number_entry(out_value, "year",   (double)(tm_info->tm_year + 1900)) != 0 ||
      map_add_number_entry(out_value, "month",  (double)(tm_info->tm_mon + 1)) != 0 ||
      map_add_number_entry(out_value, "day",    (double) tm_info->tm_mday) != 0 ||
      map_add_number_entry(out_value, "hour",   (double) tm_info->tm_hour) != 0 ||
      map_add_number_entry(out_value, "minute", (double) tm_info->tm_min) != 0 ||
      map_add_number_entry(out_value, "second", (double) tm_info->tm_sec) != 0 ||
      map_add_string_entry(out_value, "iso",    iso) != 0) {
    snprintf(error, error_size, "time namespace is too large");
    return 1;
  }

  return 0;
}

static int map_method_keys(
  ArkshShell *shell,
  const ArkshValue *receiver,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *out,
  size_t out_size
) {
  size_t i;

  (void) shell;
  (void) args;
  (void) out;
  (void) out_size;

  if (receiver == NULL || out_value == NULL || receiver->kind != ARKSH_VALUE_MAP || argc != 0) {
    if (out != NULL && out_size > 0) {
      snprintf(out, out_size, "keys() is only valid on map values");
    }
    return 1;
  }

  arksh_value_init(out_value);
  out_value->kind = ARKSH_VALUE_LIST;
  for (i = 0; i < receiver->map.count; ++i) {
    ArkshValue *key_value = allocate_runtime_value(out, out_size, "map key value");

    if (key_value == NULL) {
      return 1;
    }

    arksh_value_set_string(key_value, receiver->map.entries[i].key);
    if (arksh_value_list_append_value(out_value, key_value) != 0) {
      arksh_value_free(key_value);
      free(key_value);
      if (out != NULL && out_size > 0) {
        snprintf(out, out_size, "keys() result is too large");
      }
      return 1;
    }
    arksh_value_free(key_value);
    free(key_value);
  }

  return 0;
}

static int map_method_values(
  ArkshShell *shell,
  const ArkshValue *receiver,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *out,
  size_t out_size
) {
  size_t i;

  (void) shell;
  (void) args;

  if (receiver == NULL || out_value == NULL || receiver->kind != ARKSH_VALUE_MAP || argc != 0) {
    snprintf(out, out_size, "values() is only valid on map values");
    return 1;
  }

  arksh_value_init(out_value);
  out_value->kind = ARKSH_VALUE_LIST;
  for (i = 0; i < receiver->map.count; ++i) {
    ArkshValue *entry_value = allocate_runtime_value(out, out_size, "map entry value");

    if (entry_value == NULL) {
      return 1;
    }

    if (arksh_value_set_from_item(entry_value, &receiver->map.entries[i].value) != 0 ||
        arksh_value_list_append_value(out_value, entry_value) != 0) {
      arksh_value_free(entry_value);
      free(entry_value);
      snprintf(out, out_size, "values() result is too large");
      return 1;
    }
    arksh_value_free(entry_value);
    free(entry_value);
  }

  return 0;
}

static int map_method_entries(
  ArkshShell *shell,
  const ArkshValue *receiver,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *out,
  size_t out_size
) {
  size_t i;

  (void) shell;
  (void) args;

  if (receiver == NULL || out_value == NULL || receiver->kind != ARKSH_VALUE_MAP || argc != 0) {
    snprintf(out, out_size, "entries() is only valid on map values");
    return 1;
  }

  arksh_value_init(out_value);
  out_value->kind = ARKSH_VALUE_LIST;
  for (i = 0; i < receiver->map.count; ++i) {
    ArkshValue *entry_map = allocate_runtime_value(out, out_size, "map entry map");
    ArkshValue *nested_value = allocate_runtime_value(out, out_size, "map nested value");

    if (entry_map == NULL || nested_value == NULL) {
      free(entry_map);
      free(nested_value);
      return 1;
    }

    arksh_value_set_map(entry_map);
    arksh_value_set_string(nested_value, receiver->map.entries[i].key);
    if (map_add_value_entry(entry_map, "key", nested_value) != 0 ||
        arksh_value_set_from_item(nested_value, &receiver->map.entries[i].value) != 0 ||
        map_add_value_entry(entry_map, "value", nested_value) != 0 ||
        arksh_value_list_append_value(out_value, entry_map) != 0) {
      arksh_value_free(nested_value);
      arksh_value_free(entry_map);
      free(nested_value);
      free(entry_map);
      snprintf(out, out_size, "entries() result is too large");
      return 1;
    }
    arksh_value_free(nested_value);
    arksh_value_free(entry_map);
    free(nested_value);
    free(entry_map);
  }

  return 0;
}

static int map_method_get(
  ArkshShell *shell,
  const ArkshValue *receiver,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *out,
  size_t out_size
) {
  const ArkshValueItem *entry;
  char key[ARKSH_MAX_NAME];

  (void) shell;

  if (receiver == NULL || out_value == NULL || receiver->kind != ARKSH_VALUE_MAP) {
    snprintf(out, out_size, "get() is only valid on map values");
    return 1;
  }
  if (argc != 1) {
    snprintf(out, out_size, "get() expects exactly one key");
    return 1;
  }
  if (render_argument_key(&args[0], key, sizeof(key), out, out_size) != 0) {
    return 1;
  }

  entry = arksh_value_map_get_item(receiver, key);
  if (entry == NULL) {
    arksh_value_init(out_value);
    return 0;
  }
  return arksh_value_set_from_item(out_value, entry);
}

static int map_method_has(
  ArkshShell *shell,
  const ArkshValue *receiver,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *out,
  size_t out_size
) {
  char key[ARKSH_MAX_NAME];

  (void) shell;

  if (receiver == NULL || out_value == NULL || receiver->kind != ARKSH_VALUE_MAP) {
    snprintf(out, out_size, "has() is only valid on map values");
    return 1;
  }
  if (argc != 1) {
    snprintf(out, out_size, "has() expects exactly one key");
    return 1;
  }
  if (render_argument_key(&args[0], key, sizeof(key), out, out_size) != 0) {
    return 1;
  }

  arksh_value_set_boolean(out_value, arksh_value_map_get_item(receiver, key) != NULL);
  return 0;
}

static int map_like_method_get_path(
  ArkshShell *shell,
  const ArkshValue *receiver,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *out,
  size_t out_size
) {
  char path[ARKSH_MAX_OUTPUT];
  int found = 0;

  (void) shell;

  if (receiver == NULL || out_value == NULL || !value_is_map_like(receiver)) {
    snprintf(out, out_size, "get_path() is only valid on map or dict values");
    return 1;
  }
  if (argc != 1) {
    snprintf(out, out_size, "get_path() expects exactly one path argument");
    return 1;
  }
  if (render_argument_key(&args[0], path, sizeof(path), out, out_size) != 0) {
    return 1;
  }
  if (arksh_value_get_path(receiver, path, out_value, &found, out, out_size) != 0) {
    return 1;
  }
  if (!found) {
    arksh_value_init(out_value);
  }
  return 0;
}

static int map_like_method_has_path(
  ArkshShell *shell,
  const ArkshValue *receiver,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *out,
  size_t out_size
) {
  char path[ARKSH_MAX_OUTPUT];
  ArkshValue resolved;
  int found = 0;

  (void) shell;

  if (receiver == NULL || out_value == NULL || !value_is_map_like(receiver)) {
    snprintf(out, out_size, "has_path() is only valid on map or dict values");
    return 1;
  }
  if (argc != 1) {
    snprintf(out, out_size, "has_path() expects exactly one path argument");
    return 1;
  }
  if (render_argument_key(&args[0], path, sizeof(path), out, out_size) != 0) {
    return 1;
  }
  arksh_value_init(&resolved);
  if (arksh_value_get_path(receiver, path, &resolved, &found, out, out_size) != 0) {
    arksh_value_free(&resolved);
    return 1;
  }
  arksh_value_free(&resolved);
  arksh_value_set_boolean(out_value, found);
  return 0;
}

static int map_like_method_set_path(
  ArkshShell *shell,
  const ArkshValue *receiver,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *out,
  size_t out_size
) {
  char path[ARKSH_MAX_OUTPUT];

  (void) shell;

  if (receiver == NULL || out_value == NULL || !value_is_map_like(receiver)) {
    snprintf(out, out_size, "set_path() is only valid on map or dict values");
    return 1;
  }
  if (argc != 2) {
    snprintf(out, out_size, "set_path() expects exactly two arguments: path and value");
    return 1;
  }
  if (render_argument_key(&args[0], path, sizeof(path), out, out_size) != 0) {
    return 1;
  }
  return arksh_value_set_path(receiver, path, &args[1], out_value, out, out_size);
}

static int map_like_method_pick(
  ArkshShell *shell,
  const ArkshValue *receiver,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *out,
  size_t out_size
) {
  char keys[ARKSH_MAX_ARGS][ARKSH_MAX_NAME];
  int i;

  (void) shell;

  if (receiver == NULL || out_value == NULL || !value_is_map_like(receiver)) {
    snprintf(out, out_size, "pick() is only valid on map or dict values");
    return 1;
  }
  if (argc <= 0) {
    snprintf(out, out_size, "pick() expects at least one key");
    return 1;
  }
  for (i = 0; i < argc; ++i) {
    if (render_argument_key(&args[i], keys[i], sizeof(keys[i]), out, out_size) != 0) {
      return 1;
    }
  }
  return arksh_value_pick(receiver, argc, keys, out_value, out, out_size);
}

static int map_like_method_merge(
  ArkshShell *shell,
  const ArkshValue *receiver,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *out,
  size_t out_size
) {
  (void) shell;

  if (receiver == NULL || out_value == NULL || !value_is_map_like(receiver)) {
    snprintf(out, out_size, "merge() is only valid on map or dict values");
    return 1;
  }
  if (argc != 1 || !value_is_map_like(&args[0])) {
    snprintf(out, out_size, "merge() expects exactly one map or dict argument");
    return 1;
  }
  return arksh_value_merge(receiver, &args[0], out_value, out, out_size);
}

/* path() resolver: creates a filesystem path value from a string argument */
static int resolver_path(ArkshShell *shell, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  char path_text[ARKSH_MAX_PATH];
  ArkshObject object;

  if (argc != 1 || args == NULL) {
    snprintf(out, out_size, "path() expects exactly one argument");
    return 1;
  }

  if (args[0].kind == ARKSH_VALUE_STRING) {
    copy_string(path_text, sizeof(path_text), args[0].text);
  } else if (args[0].kind == ARKSH_VALUE_NUMBER || args[0].kind == ARKSH_VALUE_INTEGER ||
             args[0].kind == ARKSH_VALUE_FLOAT   || args[0].kind == ARKSH_VALUE_DOUBLE) {
    if (arksh_value_render(&args[0], path_text, sizeof(path_text)) != 0) {
      snprintf(out, out_size, "path() unable to render argument");
      return 1;
    }
  } else if (args[0].kind == ARKSH_VALUE_OBJECT) {
    copy_string(path_text, sizeof(path_text), args[0].object.path);
  } else {
    snprintf(out, out_size, "path() expects a string argument");
    return 1;
  }

  if (path_text[0] == '\0') {
    snprintf(out, out_size, "path() argument must not be empty");
    return 1;
  }

  if (arksh_object_resolve(shell->cwd, path_text, &object) != 0) {
    snprintf(out, out_size, "path() unable to resolve: %s", path_text);
    return 1;
  }

  arksh_value_set_object(out_value, &object);
  return 0;
}

/* E6-S5-T2: explicit numeric type resolvers */
static int parse_numeric_arg(int argc, const ArkshValue *args, double *out, char *out_err, size_t out_err_size) {
  char text[ARKSH_MAX_TOKEN];

  if (argc != 1) {
    snprintf(out_err, out_err_size, "expects exactly one argument");
    return 1;
  }
  /* accept string, number, integer, float, double, or imaginary argument */
  if (args[0].kind == ARKSH_VALUE_STRING) {
    double v;
    char *end;
    copy_string(text, sizeof(text), args[0].text);
    v = strtod(text, &end);
    if (end == text || (*end != '\0' && *end != 'i')) {
      snprintf(out_err, out_err_size, "invalid numeric argument: %s", text);
      return 1;
    }
    *out = v;
    return 0;
  }
  if (args[0].kind == ARKSH_VALUE_NUMBER  ||
      args[0].kind == ARKSH_VALUE_INTEGER ||
      args[0].kind == ARKSH_VALUE_FLOAT   ||
      args[0].kind == ARKSH_VALUE_DOUBLE  ||
      args[0].kind == ARKSH_VALUE_IMAGINARY) {
    *out = args[0].number;
    return 0;
  }
  /* try to render and parse */
  if (arksh_value_render(&args[0], text, sizeof(text)) != 0) {
    snprintf(out_err, out_err_size, "cannot convert argument to number");
    return 1;
  }
  {
    double v;
    char *end;
    v = strtod(text, &end);
    if (end == text) {
      snprintf(out_err, out_err_size, "invalid numeric argument: %s", text);
      return 1;
    }
    *out = v;
    return 0;
  }
}

static int resolver_integer(ArkshShell *shell, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  double v;
  (void) shell;
  if (parse_numeric_arg(argc, args, &v, out, out_size) != 0) { return 1; }
  arksh_value_set_integer(out_value, v);
  return 0;
}

static int resolver_float(ArkshShell *shell, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  double v;
  (void) shell;
  if (parse_numeric_arg(argc, args, &v, out, out_size) != 0) { return 1; }
  arksh_value_set_float(out_value, v);
  return 0;
}

static int resolver_double(ArkshShell *shell, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  double v;
  (void) shell;
  if (parse_numeric_arg(argc, args, &v, out, out_size) != 0) { return 1; }
  arksh_value_set_double(out_value, v);
  return 0;
}

static int resolver_imaginary(ArkshShell *shell, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  double v;
  (void) shell;
  if (parse_numeric_arg(argc, args, &v, out, out_size) != 0) { return 1; }
  arksh_value_set_imaginary(out_value, v);
  return 0;
}

/* E6-S6: Dict resolver and methods */

static int resolver_dict(ArkshShell *shell, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  int i;

  (void) shell;

  if (out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }
  if (argc % 2 != 0) {
    snprintf(out, out_size, "Dict() expects alternating key/value arguments (got %d)", argc);
    return 1;
  }

  arksh_value_set_dict(out_value);
  for (i = 0; i < argc; i += 2) {
    char key[ARKSH_MAX_NAME];

    if (args[i].kind != ARKSH_VALUE_STRING) {
      if (arksh_value_render(&args[i], key, sizeof(key)) != 0) {
        snprintf(out, out_size, "Dict() key at position %d must be a string", i);
        return 1;
      }
    } else {
      copy_string(key, sizeof(key), args[i].text);
    }
    if (arksh_value_map_set(out_value, key, &args[i + 1]) != 0) {
      snprintf(out, out_size, "Dict() is too large");
      return 1;
    }
  }

  return 0;
}

static int dict_method_set(ArkshShell *shell, const ArkshValue *receiver, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  char key[ARKSH_MAX_NAME];

  (void) shell;

  if (receiver == NULL || receiver->kind != ARKSH_VALUE_DICT) {
    snprintf(out, out_size, "set() is only valid on dict values");
    return 1;
  }
  if (argc != 2) {
    snprintf(out, out_size, "set() expects exactly two arguments: key and value");
    return 1;
  }
  if (args[0].kind != ARKSH_VALUE_STRING) {
    snprintf(out, out_size, "set() key must be a string");
    return 1;
  }
  copy_string(key, sizeof(key), args[0].text);

  if (arksh_value_copy(out_value, receiver) != 0) {
    snprintf(out, out_size, "set() failed to copy dict");
    return 1;
  }
  if (arksh_value_map_set(out_value, key, &args[1]) != 0) {
    snprintf(out, out_size, "set() dict is full");
    return 1;
  }
  return 0;
}

static int dict_method_delete(ArkshShell *shell, const ArkshValue *receiver, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  char key[ARKSH_MAX_NAME];
  size_t i;

  (void) shell;

  if (receiver == NULL || receiver->kind != ARKSH_VALUE_DICT) {
    snprintf(out, out_size, "delete() is only valid on dict values");
    return 1;
  }
  if (argc != 1 || args[0].kind != ARKSH_VALUE_STRING) {
    snprintf(out, out_size, "delete() expects exactly one string key");
    return 1;
  }
  copy_string(key, sizeof(key), args[0].text);

  arksh_value_set_dict(out_value);
  for (i = 0; i < receiver->map.count; ++i) {
    ArkshValue *entry_val = allocate_shell_value(out, out_size);

    if (entry_val == NULL) {
      return 1;
    }

    if (strcmp(receiver->map.entries[i].key, key) == 0) {
      free(entry_val);
      continue;
    }
    arksh_value_init(entry_val);
    if (arksh_value_set_from_item(entry_val, &receiver->map.entries[i].value) != 0) {
      free(entry_val);
      snprintf(out, out_size, "delete() failed to copy entry");
      return 1;
    }
    if (arksh_value_map_set(out_value, receiver->map.entries[i].key, entry_val) != 0) {
      arksh_value_free(entry_val);
      free(entry_val);
      snprintf(out, out_size, "delete() dict is full");
      return 1;
    }
    arksh_value_free(entry_val);
    free(entry_val);
  }
  return 0;
}

static int dict_method_get(ArkshShell *shell, const ArkshValue *receiver, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  const ArkshValueItem *entry;
  char key[ARKSH_MAX_NAME];

  (void) shell;

  if (receiver == NULL || receiver->kind != ARKSH_VALUE_DICT) {
    snprintf(out, out_size, "get() is only valid on dict values");
    return 1;
  }
  if (argc != 1 || args[0].kind != ARKSH_VALUE_STRING) {
    snprintf(out, out_size, "get() expects exactly one string key");
    return 1;
  }
  copy_string(key, sizeof(key), args[0].text);

  entry = arksh_value_map_get_item(receiver, key);
  if (entry == NULL) {
    arksh_value_init(out_value);
    return 0;
  }
  return arksh_value_set_from_item(out_value, entry);
}

static int dict_method_has(ArkshShell *shell, const ArkshValue *receiver, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  char key[ARKSH_MAX_NAME];

  (void) shell;

  if (receiver == NULL || receiver->kind != ARKSH_VALUE_DICT) {
    snprintf(out, out_size, "has() is only valid on dict values");
    return 1;
  }
  if (argc != 1 || args[0].kind != ARKSH_VALUE_STRING) {
    snprintf(out, out_size, "has() expects exactly one string key");
    return 1;
  }
  copy_string(key, sizeof(key), args[0].text);
  arksh_value_set_boolean(out_value, arksh_value_map_get_item(receiver, key) != NULL);
  return 0;
}

static int dict_prop_keys(ArkshShell *shell, const ArkshValue *receiver, ArkshValue *out_value, char *out, size_t out_size) {
  size_t i;

  (void) shell;

  if (receiver == NULL || receiver->kind != ARKSH_VALUE_DICT) {
    snprintf(out, out_size, "keys is only valid on dict values");
    return 1;
  }
  arksh_value_init(out_value);
  out_value->kind = ARKSH_VALUE_LIST;
  for (i = 0; i < receiver->map.count; ++i) {
    ArkshValue *key_val = allocate_shell_value(out, out_size);

    if (key_val == NULL) {
      return 1;
    }
    arksh_value_set_string(key_val, receiver->map.entries[i].key);
    if (arksh_value_list_append_value(out_value, key_val) != 0) {
      arksh_value_free(key_val);
      free(key_val);
      snprintf(out, out_size, "keys: result is too large");
      return 1;
    }
    arksh_value_free(key_val);
    free(key_val);
  }
  return 0;
}

static int dict_prop_values(ArkshShell *shell, const ArkshValue *receiver, ArkshValue *out_value, char *out, size_t out_size) {
  size_t i;

  (void) shell;

  if (receiver == NULL || receiver->kind != ARKSH_VALUE_DICT) {
    snprintf(out, out_size, "values is only valid on dict values");
    return 1;
  }
  arksh_value_init(out_value);
  out_value->kind = ARKSH_VALUE_LIST;
  for (i = 0; i < receiver->map.count; ++i) {
    ArkshValue *item_val = allocate_shell_value(out, out_size);

    if (item_val == NULL) {
      return 1;
    }
    arksh_value_init(item_val);
    if (arksh_value_set_from_item(item_val, &receiver->map.entries[i].value) != 0) {
      free(item_val);
      snprintf(out, out_size, "values: failed to expand item");
      return 1;
    }
    if (arksh_value_list_append_value(out_value, item_val) != 0) {
      arksh_value_free(item_val);
      free(item_val);
      snprintf(out, out_size, "values: result is too large");
      return 1;
    }
    arksh_value_free(item_val);
    free(item_val);
  }
  return 0;
}

static int dict_method_to_json(ArkshShell *shell, const ArkshValue *receiver, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  char json[ARKSH_MAX_OUTPUT];

  (void) shell;
  (void) args;

  if (receiver == NULL || receiver->kind != ARKSH_VALUE_DICT) {
    snprintf(out, out_size, "to_json is only valid on dict values");
    return 1;
  }
  if (argc != 0) {
    snprintf(out, out_size, "to_json expects no arguments");
    return 1;
  }
  if (arksh_value_to_json(receiver, json, sizeof(json)) != 0) {
    snprintf(out, out_size, "to_json: serialization failed");
    return 1;
  }
  arksh_value_set_string(out_value, json);
  return 0;
}

static int dict_method_from_json(ArkshShell *shell, const ArkshValue *receiver, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  ArkshValue *parsed;
  char error[ARKSH_MAX_OUTPUT];

  (void) shell;
  (void) receiver;

  if (argc != 1 || args[0].kind != ARKSH_VALUE_STRING) {
    snprintf(out, out_size, "from_json() expects exactly one string argument");
    return 1;
  }
  parsed = allocate_shell_value(out, out_size);
  if (parsed == NULL) {
    return 1;
  }
  arksh_value_init(parsed);
  if (arksh_value_parse_json(args[0].text, parsed, error, sizeof(error)) != 0) {
    free(parsed);
    snprintf(out, out_size, "from_json: %s", error);
    return 1;
  }
  if (parsed->kind != ARKSH_VALUE_MAP) {
    arksh_value_free(parsed);
    free(parsed);
    snprintf(out, out_size, "from_json: expected JSON object");
    return 1;
  }
  parsed->kind = ARKSH_VALUE_DICT;
  *out_value = *parsed;
  free(parsed);
  return 0;
}

/* E6-S8: forward declaration — defined later near register_builtin_extensions */
static int resolver_matrix(ArkshShell *shell, int argc, const ArkshValue *args, ArkshValue *out_value, char *error, size_t error_size);

static int register_builtin_value_resolvers(ArkshShell *shell) {
  if (shell == NULL) {
    return 1;
  }

  if (arksh_shell_register_value_resolver(shell, "map",   "typed map of key-value pairs", resolver_map, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_value_resolver(shell, "env",   "environment variables namespace (HOME, PATH, …)", resolver_env, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_value_resolver(shell, "proc",  "current process info (pid, args, …)", resolver_proc, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_value_resolver(shell, "shell", "arksh runtime introspection (vars, functions, plugins, …)", resolver_shell_namespace, 0) != 0) {
    return 1;
  }
  /* E6-S1 namespaces */
  if (arksh_shell_register_value_resolver(shell, "fs",   "filesystem namespace (cwd, home, temp, separator)", resolver_fs, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_value_resolver(shell, "user", "current user info (name, home, uid, shell)", resolver_user, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_value_resolver(shell, "sys",  "system info (os, host, arch, cpu_count)", resolver_sys, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_value_resolver(shell, "time", "current time (epoch, year, month, day, hour, minute, second, iso)", resolver_time, 0) != 0) {
    return 1;
  }
  /* path() resolver */
  if (arksh_shell_register_value_resolver(shell, "path", "create a path value from a string", resolver_path, 0) != 0) {
    return 1;
  }
  /* E6-S5: explicit numeric sub-kinds */
  if (arksh_shell_register_value_resolver(shell, "Integer",   "64-bit integer value", resolver_integer, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_value_resolver(shell, "Float",     "32-bit floating-point value", resolver_float, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_value_resolver(shell, "Double",    "64-bit floating-point value", resolver_double, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_value_resolver(shell, "Imaginary", "purely imaginary number b·i", resolver_imaginary, 0) != 0) {
    return 1;
  }
  /* E6-S6: immutable key-value dictionary */
  if (arksh_shell_register_value_resolver(shell, "Dict", "immutable key-value dictionary", resolver_dict, 0) != 0) {
    return 1;
  }
  /* E6-S8: Matrix */
  if (arksh_shell_register_value_resolver(shell, "Matrix", "create a Matrix with named columns", resolver_matrix, 0) != 0) {
    return 1;
  }

  return 0;
}

/* E6-S4-T1: register built-in pipeline stages as metadata entries so that
   help, completion and type introspection can discover them.  fn is NULL
   because dispatch is handled by the hardcoded strcmp chain in executor.c. */
static int register_builtin_pipeline_stages(ArkshShell *shell) {
  static const struct { const char *name; const char *desc; } stages[] = {
    /* filtering / selection */
    { "where",     "filter list by predicate (property name, value, or block)" },
    { "filter",    "alias for where: filter list by predicate" },
    { "grep",      "keep items or lines that match a pattern string" },
    /* ordering */
    { "sort",      "sort list by property and optional direction (asc|desc)" },
    /* slicing */
    { "take",      "take the first N items from a list" },
    { "first",     "return the first item of a list" },
    /* counting / aggregation */
    { "count",     "count items in a list or lines in text" },
    { "sum",       "sum numeric items (optional property selector)" },
    { "min",       "minimum numeric item (optional property selector)" },
    { "max",       "maximum numeric item (optional property selector)" },
    { "reduce",    "fold list into a single value with an accumulator block" },
    /* projection / transformation */
    { "each",      "project each item to a property name or block result" },
    { "pluck",     "extract a nested path or property from each list item" },
    { "map",       "transform each item with a block, return new list" },
    { "flat_map",  "transform each item with a block and flatten one level" },
    { "group_by",  "group items by property or block into a map" },
    /* text operations */
    { "lines",     "split text value into a list of lines" },
    { "trim",      "remove leading and trailing whitespace from text" },
    { "split",     "split text at a separator string" },
    { "join",      "join list items into text with a separator" },
    /* JSON */
    { "to_json",        "serialize value to a JSON string" },
    { "from_json",      "parse a JSON string into a value" },
    /* encoding (E6-S7) */
    { "base64_encode",  "encode a string to Base64 (RFC 4648)" },
    { "base64_decode",  "decode a Base64-encoded string (RFC 4648)" },
    /* matrix (E6-S8) */
    { "transpose",  "transpose a Matrix (swap rows and columns)" },
    { "fill_na",    "replace empty cells in a matrix column with a value" },
    /* misc */
    { "render",    "render any value to its text representation" },
    { NULL, NULL }
  };
  size_t i;

  if (shell == NULL) {
    return 1;
  }
  for (i = 0; stages[i].name != NULL; ++i) {
    if (arksh_shell_register_pipeline_stage(shell, stages[i].name, stages[i].desc, NULL, 0) != 0) {
      return 1;
    }
  }
  return 0;
}

static int split_assignment(const char *text, char *name, size_t name_size, char *value, size_t value_size) {
  const char *equals;
  size_t name_len;

  if (text == NULL || name == NULL || value == NULL) {
    return 1;
  }

  equals = strchr(text, '=');
  if (equals == NULL || equals == text) {
    return 1;
  }

  name_len = (size_t) (equals - text);
  if (name_len >= name_size) {
    return 1;
  }

  memcpy(name, text, name_len);
  name[name_len] = '\0';
  copy_string(value, value_size, equals + 1);
  return 0;
}

static int parse_let_assignment(const char *line, char *name, size_t name_size, char *expression, size_t expression_size) {
  char trimmed[ARKSH_MAX_LINE];
  const char *cursor;
  size_t name_len = 0;

  if (line == NULL || name == NULL || expression == NULL || name_size == 0 || expression_size == 0) {
    return 1;
  }

  trim_copy(line, trimmed, sizeof(trimmed));
  if (strcmp(trimmed, "let") == 0) {
    name[0] = '\0';
    expression[0] = '\0';
    return 0;
  }

  if (strncmp(trimmed, "let", 3) != 0 || !isspace((unsigned char) trimmed[3])) {
    return 1;
  }

  cursor = trimmed + 3;
  while (*cursor != '\0' && isspace((unsigned char) *cursor)) {
    cursor++;
  }

  while (cursor[name_len] != '\0' && (isalnum((unsigned char) cursor[name_len]) || cursor[name_len] == '_')) {
    name_len++;
  }

  if (name_len == 0 || name_len >= name_size) {
    return 1;
  }

  memcpy(name, cursor, name_len);
  name[name_len] = '\0';
  cursor += name_len;

  while (*cursor != '\0' && isspace((unsigned char) *cursor)) {
    cursor++;
  }

  if (*cursor != '=') {
    return 1;
  }
  cursor++;
  trim_copy(cursor, expression, expression_size);
  return 0;
}

static int parse_extend_definition(
  const char *line,
  char *target,
  size_t target_size,
  ArkshMemberKind *out_member_kind,
  char *name,
  size_t name_size,
  char *expression,
  size_t expression_size
) {
  char trimmed[ARKSH_MAX_LINE];
  const char *cursor;
  size_t token_len;

  if (line == NULL || target == NULL || out_member_kind == NULL || name == NULL || expression == NULL) {
    return 1;
  }

  trim_copy(line, trimmed, sizeof(trimmed));
  if (strcmp(trimmed, "extend") == 0) {
    target[0] = '\0';
    name[0] = '\0';
    expression[0] = '\0';
    return 0;
  }

  if (strncmp(trimmed, "extend", 6) != 0 || !isspace((unsigned char) trimmed[6])) {
    return 1;
  }

  cursor = trimmed + 6;
  while (*cursor != '\0' && isspace((unsigned char) *cursor)) {
    cursor++;
  }

  token_len = 0;
  while (cursor[token_len] != '\0' && !isspace((unsigned char) cursor[token_len])) {
    token_len++;
  }
  if (token_len == 0 || token_len >= target_size) {
    return 1;
  }
  memcpy(target, cursor, token_len);
  target[token_len] = '\0';
  cursor += token_len;

  while (*cursor != '\0' && isspace((unsigned char) *cursor)) {
    cursor++;
  }
  if (strncmp(cursor, "property", 8) == 0 && isspace((unsigned char) cursor[8])) {
    *out_member_kind = ARKSH_MEMBER_PROPERTY;
    cursor += 8;
  } else if (strncmp(cursor, "method", 6) == 0 && isspace((unsigned char) cursor[6])) {
    *out_member_kind = ARKSH_MEMBER_METHOD;
    cursor += 6;
  } else {
    return 1;
  }

  while (*cursor != '\0' && isspace((unsigned char) *cursor)) {
    cursor++;
  }

  token_len = 0;
  while (cursor[token_len] != '\0' && (isalnum((unsigned char) cursor[token_len]) || cursor[token_len] == '_')) {
    token_len++;
  }
  if (token_len == 0 || token_len >= name_size) {
    return 1;
  }
  memcpy(name, cursor, token_len);
  name[token_len] = '\0';
  cursor += token_len;

  while (*cursor != '\0' && isspace((unsigned char) *cursor)) {
    cursor++;
  }
  if (*cursor != '=') {
    return 1;
  }
  cursor++;
  trim_copy(cursor, expression, expression_size);
  return 0;
}

static int format_var_list(const ArkshShell *shell, int exported_only, char *out, size_t out_size) {
  size_t i;
  int found = 0;

  if (out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';

  for (i = 0; i < shell->var_count; ++i) {
    if (exported_only && !shell->vars[i].exported) {
      continue;
    }

    if (out[0] != '\0' && append_text(out, out_size, "\n") != 0) {
      return 1;
    }
    if (append_text(out, out_size, shell->vars[i].name) != 0 ||
        append_text(out, out_size, "=") != 0 ||
        append_text(out, out_size, shell->vars[i].value) != 0) {
      return 1;
    }
    if (!exported_only && shell->vars[i].exported && append_text(out, out_size, " [exported]") != 0) {
      return 1;
    }
    found = 1;
  }

  if (!found) {
    copy_string(out, out_size, exported_only ? "no exported variables" : "no shell variables defined");
  }

  return 0;
}

static int format_binding_list(const ArkshShell *shell, char *out, size_t out_size) {
  size_t i;

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  if (shell->binding_count == 0) {
    copy_string(out, out_size, "no value bindings defined");
    return 0;
  }

  for (i = 0; i < shell->binding_count; ++i) {
    char rendered[ARKSH_MAX_OUTPUT];
    char line[ARKSH_MAX_OUTPUT];

    if (arksh_value_render(&shell->bindings[i].value, rendered, sizeof(rendered)) != 0) {
      copy_string(rendered, sizeof(rendered), "<unrenderable>");
    }
    snprintf(
      line,
      sizeof(line),
      "%s=%s [%s]",
      shell->bindings[i].name,
      rendered,
      arksh_value_kind_name(shell->bindings[i].value.kind)
    );
    if (append_output_line(out, out_size, line) != 0) {
      return 1;
    }
  }

  return 0;
}

static int format_extension_list(const ArkshShell *shell, char *out, size_t out_size) {
  size_t i;

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  if (shell->extension_count == 0) {
    copy_string(out, out_size, "no object extensions defined");
    return 0;
  }

  for (i = 0; i < shell->extension_count; ++i) {
    char line[ARKSH_MAX_OUTPUT];
    const char *member_kind = shell->extensions[i].member_kind == ARKSH_MEMBER_METHOD ? "method" : "property";
    const char *source_kind = shell->extensions[i].is_plugin_extension ? "plugin" : "lang";
    const char *impl_kind = shell->extensions[i].impl_kind == ARKSH_EXTENSION_IMPL_NATIVE ? "native" : "block";

    snprintf(
      line,
      sizeof(line),
      "%s %s %s [%s,%s]",
      shell->extensions[i].target_name,
      member_kind,
      shell->extensions[i].name,
      source_kind,
      impl_kind
    );
    if (append_output_line(out, out_size, line) != 0) {
      return 1;
    }
  }

  return 0;
}

static int format_function_list(const ArkshShell *shell, char *out, size_t out_size) {
  size_t i;

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  if (shell->function_count == 0) {
    copy_string(out, out_size, "no shell functions defined");
    return 0;
  }

  for (i = 0; i < shell->function_count; ++i) {
    char signature[ARKSH_MAX_OUTPUT];
    int param_index;

    snprintf(signature, sizeof(signature), "function %s(", shell->functions[i].name);
    for (param_index = 0; param_index < shell->functions[i].param_count; ++param_index) {
      if (param_index > 0) {
        snprintf(signature + strlen(signature), sizeof(signature) - strlen(signature), ", ");
      }
      snprintf(
        signature + strlen(signature),
        sizeof(signature) - strlen(signature),
        "%s",
        shell->functions[i].params[param_index]
      );
    }
    snprintf(signature + strlen(signature), sizeof(signature) - strlen(signature), ")");

    if (append_output_line(out, out_size, signature) != 0) {
      return 1;
    }
  }

  return 0;
}

static int format_class_list(const ArkshShell *shell, char *out, size_t out_size) {
  size_t i;

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  if (shell->class_count == 0) {
    copy_string(out, out_size, "no classes defined");
    return 0;
  }

  for (i = 0; i < shell->class_count; ++i) {
    char line[ARKSH_MAX_OUTPUT];
    size_t base_index;

    snprintf(line, sizeof(line), "class %s", shell->classes[i].name);
    if (shell->classes[i].base_count > 0) {
      snprintf(line + strlen(line), sizeof(line) - strlen(line), " extends ");
      for (base_index = 0; base_index < (size_t) shell->classes[i].base_count; ++base_index) {
        if (base_index > 0) {
          snprintf(line + strlen(line), sizeof(line) - strlen(line), ", ");
        }
        snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s", shell->classes[i].bases[base_index]);
      }
    }
    snprintf(
      line + strlen(line),
      sizeof(line) - strlen(line),
      " [%zu properties, %zu methods]",
      shell->classes[i].property_count,
      shell->classes[i].method_count
    );

    if (append_output_line(out, out_size, line) != 0) {
      return 1;
    }
  }

  return 0;
}

static int handle_let_line(ArkshShell *shell, const char *line, char *out, size_t out_size) {
  char name[ARKSH_MAX_VAR_NAME];
  char expression[ARKSH_MAX_LINE];
  ArkshValue *value;

  if (shell == NULL || line == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (parse_let_assignment(line, name, sizeof(name), expression, sizeof(expression)) != 0) {
    snprintf(out, out_size, "usage: let <name> = <value-expression>");
    return 1;
  }

  if (name[0] == '\0' && expression[0] == '\0') {
    return format_binding_list(shell, out, out_size);
  }

  if (!is_valid_identifier(name)) {
    snprintf(out, out_size, "invalid binding name: %s", name);
    return 1;
  }

  if (expression[0] == '\0') {
    snprintf(out, out_size, "let requires a right-hand value expression");
    return 1;
  }

  value = (ArkshValue *) calloc(1, sizeof(*value));
  if (value == NULL) {
    snprintf(out, out_size, "unable to allocate let() value");
    return 1;
  }

  if (arksh_evaluate_line_value(shell, expression, value, out, out_size) != 0) {
    free(value);
    if (out[0] == '\0') {
      snprintf(out, out_size, "unable to evaluate let expression");
    }
    return 1;
  }

  if (arksh_shell_set_binding(shell, name, value) != 0) {
    free(value);
    snprintf(out, out_size, "unable to store binding: %s", name);
    return 1;
  }

  free(value);
  out[0] = '\0';
  return 0;
}

static int handle_extend_line(ArkshShell *shell, const char *line, char *out, size_t out_size) {
  char target[ARKSH_MAX_NAME];
  char name[ARKSH_MAX_NAME];
  char expression[ARKSH_MAX_LINE];
  ArkshMemberKind member_kind;
  ArkshValue *value;

  if (shell == NULL || line == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (parse_extend_definition(line, target, sizeof(target), &member_kind, name, sizeof(name), expression, sizeof(expression)) != 0) {
    snprintf(out, out_size, "usage: extend <target> <property|method> <name> = <block>");
    return 1;
  }

  if (target[0] == '\0' && name[0] == '\0' && expression[0] == '\0') {
    return format_extension_list(shell, out, out_size);
  }

  if (expression[0] == '\0') {
    snprintf(out, out_size, "extend requires a block expression");
    return 1;
  }

  value = (ArkshValue *) calloc(1, sizeof(*value));
  if (value == NULL) {
    snprintf(out, out_size, "unable to allocate extend() value");
    return 1;
  }

  if (arksh_evaluate_line_value(shell, expression, value, out, out_size) != 0) {
    free(value);
    if (out[0] == '\0') {
      snprintf(out, out_size, "unable to evaluate extend expression");
    }
    return 1;
  }

  if (value->kind != ARKSH_VALUE_BLOCK) {
    free(value);
    snprintf(out, out_size, "extend expects a block value");
    return 1;
  }

  if (member_kind == ARKSH_MEMBER_PROPERTY) {
    if (value->block.param_count != 1) {
      free(value);
      snprintf(out, out_size, "property extensions expect exactly one block parameter for the receiver");
      return 1;
    }
    if (arksh_shell_register_block_property_extension(shell, target, name, &value->block) != 0) {
      free(value);
      snprintf(out, out_size, "unable to register property extension: %s", name);
      return 1;
    }
  } else {
    if (value->block.param_count < 1) {
      free(value);
      snprintf(out, out_size, "method extensions expect the receiver as first block parameter");
      return 1;
    }
    if (arksh_shell_register_block_method_extension(shell, target, name, &value->block) != 0) {
      free(value);
      snprintf(out, out_size, "unable to register method extension: %s", name);
      return 1;
    }
  }

  free(value);
  out[0] = '\0';
  return 0;
}

static int parse_control_line_argument(const char *line, const char *keyword, char *argument, size_t argument_size) {
  char trimmed[ARKSH_MAX_LINE];
  const char *cursor;
  size_t keyword_length;

  if (line == NULL || keyword == NULL || argument == NULL || argument_size == 0) {
    return 1;
  }

  trim_copy(line, trimmed, sizeof(trimmed));
  keyword_length = strlen(keyword);
  if (strncmp(trimmed, keyword, keyword_length) != 0) {
    return 1;
  }
  if (trimmed[keyword_length] != '\0' && !isspace((unsigned char) trimmed[keyword_length])) {
    return 1;
  }

  cursor = trimmed + keyword_length;
  while (*cursor != '\0' && isspace((unsigned char) *cursor)) {
    cursor++;
  }

  copy_string(argument, argument_size, cursor);
  return 0;
}

static int parse_positive_control_count(const char *text, int *out_count) {
  char *endptr = NULL;
  long value;

  if (text == NULL || out_count == NULL) {
    return 1;
  }
  if (text[0] == '\0') {
    *out_count = 1;
    return 0;
  }

  value = strtol(text, &endptr, 10);
  if (endptr == text || *endptr != '\0' || value <= 0 || value > 1024) {
    return 1;
  }

  *out_count = (int) value;
  return 0;
}

static int handle_loop_control_line(
  ArkshShell *shell,
  const char *line,
  const char *keyword,
  ArkshControlSignalKind kind,
  char *out,
  size_t out_size
) {
  char argument[ARKSH_MAX_LINE];
  int count;

  if (shell == NULL || line == NULL || keyword == NULL || out == NULL || out_size == 0) {
    return 1;
  }
  if (parse_control_line_argument(line, keyword, argument, sizeof(argument)) != 0) {
    snprintf(out, out_size, "usage: %s [count]", keyword);
    return 1;
  }
  if (shell->loop_depth <= 0) {
    snprintf(out, out_size, "%s is only valid inside loops", keyword);
    return 1;
  }
  if (parse_positive_control_count(argument, &count) != 0) {
    snprintf(out, out_size, "usage: %s [positive-count]", keyword);
    return 1;
  }
  if (arksh_shell_raise_control_signal(shell, kind, count) != 0) {
    snprintf(out, out_size, "unable to raise %s control signal", keyword);
    return 1;
  }

  out[0] = '\0';
  return 0;
}

static int handle_return_line(ArkshShell *shell, const char *line, char *out, size_t out_size) {
  char argument[ARKSH_MAX_LINE];

  if (shell == NULL || line == NULL || out == NULL || out_size == 0) {
    return 1;
  }
  if (parse_control_line_argument(line, "return", argument, sizeof(argument)) != 0) {
    snprintf(out, out_size, "usage: return [value-expression]");
    return 1;
  }
  if (shell->function_depth <= 0) {
    snprintf(out, out_size, "return is only valid inside shell functions");
    return 1;
  }

  if (argument[0] != '\0') {
    ArkshValue *value = allocate_runtime_value(out, out_size, "return value");

    if (value == NULL) {
      return 1;
    }
    if (arksh_evaluate_line_value(shell, argument, value, out, out_size) != 0) {
      free(value);
      if (out[0] == '\0') {
        snprintf(out, out_size, "unable to evaluate return expression");
      }
      return 1;
    }
    if (arksh_value_render(value, out, out_size) != 0) {
      free(value);
      snprintf(out, out_size, "unable to render return value");
      return 1;
    }
    free(value);
  } else {
    out[0] = '\0';
  }

  if (arksh_shell_raise_control_signal(shell, ARKSH_CONTROL_SIGNAL_RETURN, 1) != 0) {
    snprintf(out, out_size, "unable to raise return control signal");
    return 1;
  }
  return 0;
}

static int format_alias_list(const ArkshShell *shell, char *out, size_t out_size) {
  size_t i;

  if (out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  if (shell->alias_count == 0) {
    copy_string(out, out_size, "no aliases defined");
    return 0;
  }

  for (i = 0; i < shell->alias_count; ++i) {
    if (out[0] != '\0' && append_text(out, out_size, "\n") != 0) {
      return 1;
    }
    if (append_text(out, out_size, shell->aliases[i].name) != 0 ||
        append_text(out, out_size, "=") != 0 ||
        append_text(out, out_size, shell->aliases[i].value) != 0) {
      return 1;
    }
  }

  return 0;
}

static int update_directory_vars(ArkshShell *shell, const char *previous_cwd) {
  if (previous_cwd != NULL && previous_cwd[0] != '\0') {
    if (arksh_shell_set_var(shell, "OLDPWD", previous_cwd, 0) != 0) {
      return 1;
    }
  }

  return arksh_shell_set_var(shell, "PWD", shell->cwd, 1);
}

static void initialize_default_variables(ArkshShell *shell) {
  const char *home;
  const char *path;

  if (shell == NULL) {
    return;
  }

  arksh_shell_set_var(shell, "PWD", shell->cwd, 1);

  home = getenv("HOME");
#ifdef _WIN32
  if (home == NULL || home[0] == '\0') {
    home = getenv("USERPROFILE");
  }
#endif
  if (home != NULL && home[0] != '\0') {
    arksh_shell_set_var(shell, "HOME", home, 1);
  }

  path = getenv("PATH");
  if (path != NULL && path[0] != '\0') {
    arksh_shell_set_var(shell, "PATH", path, 1);
  }

  /* POSIX default field separator: space, tab, newline. */
  arksh_shell_set_var(shell, "IFS", " \t\n", 0);
}

static int join_runtime_path(const char *base, const char *name, char *out, size_t out_size) {
  const char *separator = arksh_platform_path_separator();
  size_t base_len;

  if (base == NULL || name == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (base[0] == '\0') {
    copy_string(out, out_size, name);
    return 0;
  }

  base_len = strlen(base);
  if (base_len > 0 &&
      (base[base_len - 1] == '/' ||
       base[base_len - 1] == '\\' ||
       base[base_len - 1] == separator[0])) {
    if (snprintf(out, out_size, "%s%s", base, name) >= (int) out_size) {
      return 1;
    }
    return 0;
  }

  if (snprintf(out, out_size, "%s%s%s", base, separator, name) >= (int) out_size) {
    return 1;
  }

  return 0;
}

static int resolve_dir_override(ArkshShell *shell, const char *env_name, char *out, size_t out_size) {
  const char *value;

  if (shell == NULL || env_name == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  value = getenv(env_name);
  if (value == NULL || value[0] == '\0') {
    out[0] = '\0';
    return 1;
  }

  if (arksh_platform_resolve_path(shell->cwd, value, out, out_size) != 0) {
    out[0] = '\0';
    return 1;
  }

  return 0;
}

static void build_legacy_arksh_dir(ArkshShell *shell, char *out, size_t out_size) {
  const char *home = arksh_shell_get_var(shell, "HOME");

  if (out == NULL || out_size == 0) {
    return;
  }

  out[0] = '\0';
  if (home == NULL || home[0] == '\0') {
    return;
  }

  join_runtime_path(home, ".arksh", out, out_size);
}

static void resolve_runtime_directories(ArkshShell *shell) {
  const char *home;
  char base[ARKSH_MAX_PATH];

  if (shell == NULL) {
    return;
  }

  shell->config_dir[0] = '\0';
  shell->cache_dir[0] = '\0';
  shell->state_dir[0] = '\0';
  shell->data_dir[0] = '\0';
  shell->plugin_dir[0] = '\0';

  home = arksh_shell_get_var(shell, "HOME");

  if (resolve_dir_override(shell, "ARKSH_CONFIG_HOME", shell->config_dir, sizeof(shell->config_dir)) != 0) {
#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    if (appdata != NULL && appdata[0] != '\0') {
      join_runtime_path(appdata, "arksh", shell->config_dir, sizeof(shell->config_dir));
    }
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg != NULL && xdg[0] != '\0') {
      join_runtime_path(xdg, "arksh", shell->config_dir, sizeof(shell->config_dir));
    } else if (home != NULL && home[0] != '\0') {
      join_runtime_path(home, ".config/arksh", shell->config_dir, sizeof(shell->config_dir));
    }
#endif
  }

  if (resolve_dir_override(shell, "ARKSH_CACHE_HOME", shell->cache_dir, sizeof(shell->cache_dir)) != 0) {
#ifdef _WIN32
    const char *local_appdata = getenv("LOCALAPPDATA");
    if (local_appdata != NULL && local_appdata[0] != '\0') {
      join_runtime_path(local_appdata, "arksh/cache", shell->cache_dir, sizeof(shell->cache_dir));
    }
#else
    const char *xdg = getenv("XDG_CACHE_HOME");
    if (xdg != NULL && xdg[0] != '\0') {
      join_runtime_path(xdg, "arksh", shell->cache_dir, sizeof(shell->cache_dir));
    } else if (home != NULL && home[0] != '\0') {
      join_runtime_path(home, ".cache/arksh", shell->cache_dir, sizeof(shell->cache_dir));
    }
#endif
  }

  if (resolve_dir_override(shell, "ARKSH_STATE_HOME", shell->state_dir, sizeof(shell->state_dir)) != 0) {
#ifdef _WIN32
    const char *local_appdata = getenv("LOCALAPPDATA");
    if (local_appdata != NULL && local_appdata[0] != '\0') {
      join_runtime_path(local_appdata, "arksh/state", shell->state_dir, sizeof(shell->state_dir));
    }
#else
    const char *xdg = getenv("XDG_STATE_HOME");
    if (xdg != NULL && xdg[0] != '\0') {
      join_runtime_path(xdg, "arksh", shell->state_dir, sizeof(shell->state_dir));
    } else if (home != NULL && home[0] != '\0') {
      join_runtime_path(home, ".local/state/arksh", shell->state_dir, sizeof(shell->state_dir));
    }
#endif
  }

  if (resolve_dir_override(shell, "ARKSH_DATA_HOME", shell->data_dir, sizeof(shell->data_dir)) != 0) {
#ifdef _WIN32
    const char *local_appdata = getenv("LOCALAPPDATA");
    if (local_appdata != NULL && local_appdata[0] != '\0') {
      join_runtime_path(local_appdata, "arksh/data", shell->data_dir, sizeof(shell->data_dir));
    }
#else
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg != NULL && xdg[0] != '\0') {
      join_runtime_path(xdg, "arksh", shell->data_dir, sizeof(shell->data_dir));
    } else if (home != NULL && home[0] != '\0') {
      join_runtime_path(home, ".local/share/arksh", shell->data_dir, sizeof(shell->data_dir));
    }
#endif
  }

  if (resolve_dir_override(shell, "ARKSH_PLUGIN_HOME", shell->plugin_dir, sizeof(shell->plugin_dir)) != 0) {
#ifdef _WIN32
    const char *local_appdata = getenv("LOCALAPPDATA");
    if (local_appdata != NULL && local_appdata[0] != '\0') {
      join_runtime_path(local_appdata, "arksh/plugins", shell->plugin_dir, sizeof(shell->plugin_dir));
    }
#else
    if (shell->data_dir[0] != '\0') {
      join_runtime_path(shell->data_dir, "plugins", shell->plugin_dir, sizeof(shell->plugin_dir));
    }
#endif
  }

  if (shell->config_dir[0] == '\0' || shell->cache_dir[0] == '\0' || shell->state_dir[0] == '\0') {
    build_legacy_arksh_dir(shell, base, sizeof(base));
    if (base[0] != '\0') {
      if (shell->config_dir[0] == '\0') {
        copy_string(shell->config_dir, sizeof(shell->config_dir), base);
      }
      if (shell->cache_dir[0] == '\0') {
        copy_string(shell->cache_dir, sizeof(shell->cache_dir), base);
      }
      if (shell->state_dir[0] == '\0') {
        copy_string(shell->state_dir, sizeof(shell->state_dir), base);
      }
      if (shell->data_dir[0] == '\0') {
        copy_string(shell->data_dir, sizeof(shell->data_dir), base);
      }
      if (shell->plugin_dir[0] == '\0') {
        join_runtime_path(base, "plugins", shell->plugin_dir, sizeof(shell->plugin_dir));
      }
    }
  }

  if (shell->config_dir[0] != '\0') {
    arksh_shell_set_var(shell, "ARKSH_CONFIG_DIR", shell->config_dir, 0);
  }
  if (shell->cache_dir[0] != '\0') {
    arksh_shell_set_var(shell, "ARKSH_CACHE_DIR", shell->cache_dir, 0);
  }
  if (shell->state_dir[0] != '\0') {
    arksh_shell_set_var(shell, "ARKSH_STATE_DIR", shell->state_dir, 0);
  }
  if (shell->data_dir[0] != '\0') {
    arksh_shell_set_var(shell, "ARKSH_DATA_DIR", shell->data_dir, 0);
  }
  if (shell->plugin_dir[0] != '\0') {
    arksh_shell_set_var(shell, "ARKSH_PLUGIN_DIR", shell->plugin_dir, 0);
  }
}

static void resolve_history_path(ArkshShell *shell) {
  const char *env_path;
  char legacy_dir[ARKSH_MAX_PATH];

  if (shell == NULL) {
    return;
  }

  shell->history_path[0] = '\0';

  env_path = getenv("ARKSH_HISTORY");
  if (env_path != NULL && env_path[0] != '\0') {
    arksh_platform_resolve_path(shell->cwd, env_path, shell->history_path, sizeof(shell->history_path));
    return;
  }

  if (shell->state_dir[0] != '\0' &&
      join_runtime_path(shell->state_dir, "history", shell->history_path, sizeof(shell->history_path)) == 0) {
    return;
  }

  build_legacy_arksh_dir(shell, legacy_dir, sizeof(legacy_dir));
  if (legacy_dir[0] != '\0') {
    join_runtime_path(legacy_dir, "history", shell->history_path, sizeof(shell->history_path));
  }
}

static int arksh_shell_history_add(ArkshShell *shell, const char *line) {
  char entry[ARKSH_MAX_LINE];

  if (shell == NULL || line == NULL) {
    return 1;
  }

  copy_string(entry, sizeof(entry), line);
  trim_trailing_newlines(entry);
  normalize_history_entry(entry);
  if (is_blank_or_comment_line(entry)) {
    return 0;
  }

  if (shell->history_count > 0 && strcmp(shell->history[shell->history_count - 1], entry) == 0) {
    return 0;
  }

  if (shell->history_count >= ARKSH_MAX_HISTORY) {
    memmove(shell->history, shell->history + 1, (ARKSH_MAX_HISTORY - 1) * sizeof(shell->history[0]));
    shell->history_count = ARKSH_MAX_HISTORY - 1;
  }

  copy_string(shell->history[shell->history_count], sizeof(shell->history[shell->history_count]), entry);
  shell->history_count++;
  shell->history_dirty = 1;
  return 0;
}

static void load_history(ArkshShell *shell) {
  FILE *fp;
  char line[ARKSH_MAX_LINE];

  if (shell == NULL || shell->history_path[0] == '\0') {
    return;
  }

  fp = fopen(shell->history_path, "rb");
  if (fp == NULL) {
    return;
  }

  while (fgets(line, sizeof(line), fp) != NULL) {
    trim_trailing_newlines(line);
    arksh_shell_history_add(shell, line);
  }

  fclose(fp);
  shell->history_dirty = 0;
}

static void save_history(const ArkshShell *shell) {
  FILE *fp;
  char directory[ARKSH_MAX_PATH];
  size_t i;

  if (shell == NULL || shell->history_path[0] == '\0' || !shell->history_dirty) {
    return;
  }

  arksh_platform_dirname(shell->history_path, directory, sizeof(directory));
  if (directory[0] != '\0' && strcmp(directory, ".") != 0) {
    arksh_platform_ensure_directory(directory);
  }

  fp = fopen(shell->history_path, "wb");
  if (fp == NULL) {
    return;
  }

  for (i = 0; i < shell->history_count; ++i) {
    fprintf(fp, "%s\n", shell->history[i]);
  }

  fclose(fp);
}

static void remove_job_at(ArkshShell *shell, size_t index) {
  if (shell == NULL || index >= shell->job_count) {
    return;
  }

  arksh_platform_close_background_process(&shell->jobs[index].process);
  if (index + 1 < shell->job_count) {
    memmove(&shell->jobs[index], &shell->jobs[index + 1], (shell->job_count - index - 1) * sizeof(shell->jobs[index]));
  }
  shell->job_count--;
}

static void prune_completed_jobs(ArkshShell *shell) {
  size_t i = 0;

  if (shell == NULL) {
    return;
  }

  while (i < shell->job_count) {
    if (shell->jobs[i].state == ARKSH_JOB_DONE) {
      remove_job_at(shell, i);
      continue;
    }
    i++;
  }
}

static ArkshJob *find_job_by_id(ArkshShell *shell, int id) {
  size_t i;

  if (shell == NULL) {
    return NULL;
  }

  for (i = 0; i < shell->job_count; ++i) {
    if (shell->jobs[i].id == id) {
      return &shell->jobs[i];
    }
  }

  return NULL;
}

static const char *signal_name(int sig) {
  switch (sig) {
    case 1:  return "HUP";
    case 2:  return "INT";
    case 3:  return "QUIT";
    case 6:  return "ABRT";
    case 9:  return "KILL";
    case 11: return "SEGV";
    case 13: return "PIPE";
    case 15: return "TERM";
    case 20: return "TSTP";
    default: return "SIG";
  }
}

static ArkshJob *find_default_job(ArkshShell *shell) {
  size_t i;
  ArkshJob *running_job = NULL;

  if (shell == NULL || shell->job_count == 0) {
    return NULL;
  }

  for (i = shell->job_count; i > 0; --i) {
    if (shell->jobs[i - 1].state == ARKSH_JOB_STOPPED) {
      return &shell->jobs[i - 1];
    }
    if (running_job == NULL && shell->jobs[i - 1].state == ARKSH_JOB_RUNNING) {
      running_job = &shell->jobs[i - 1];
    }
  }

  return running_job;
}

static int parse_job_id(const char *text, int *out_id) {
  char *endptr = NULL;
  long value;

  if (text == NULL || out_id == NULL) {
    return 1;
  }

  if (text[0] == '%') {
    text++;
  }

  if (text[0] == '\0') {
    return 1;
  }

  value = strtol(text, &endptr, 10);
  if (endptr == text || *endptr != '\0' || value <= 0) {
    return 1;
  }

  *out_id = (int) value;
  return 0;
}

static int wait_for_job_at(ArkshShell *shell, size_t index, int *out_status, char *out, size_t out_size) {
  ArkshJob *job;
  ArkshPlatformProcessState state = ARKSH_PLATFORM_PROCESS_UNCHANGED;
  int exit_code = 0;

  if (shell == NULL || out_status == NULL || out == NULL || out_size == 0 || index >= shell->job_count) {
    return 1;
  }

  job = &shell->jobs[index];
  out[0] = '\0';

  if (job->state == ARKSH_JOB_DONE) {
    *out_status = job->exit_code;
    remove_job_at(shell, index);
    return 0;
  }
  if (job->state == ARKSH_JOB_STOPPED) {
    snprintf(out, out_size, "[%d] stopped pid=%lld %s", job->id, job->process.pid, job->command);
    *out_status = 1;
    return 0;
  }

  if (arksh_platform_wait_background_process(&job->process, 0, &state, &exit_code) != 0) {
    snprintf(out, out_size, "unable to wait for background job [%d]", job->id);
    return 1;
  }

  if (state == ARKSH_PLATFORM_PROCESS_STOPPED) {
    job->state = ARKSH_JOB_STOPPED;
    job->exit_code = exit_code;
    snprintf(out, out_size, "[%d] stopped pid=%lld %s", job->id, job->process.pid, job->command);
    *out_status = 1;
    return 0;
  }

  job->state = ARKSH_JOB_DONE;
  job->exit_code = exit_code;
  job->termination_signal = (exit_code > 128) ? (exit_code - 128) : 0;
  *out_status = exit_code;
  if (job->termination_signal > 0) {
    snprintf(out, out_size, "[%d] done signal=%s pid=%lld %s", job->id, signal_name(job->termination_signal), job->process.pid, job->command);
  } else if (exit_code != 0) {
    snprintf(out, out_size, "[%d] done exit=%d pid=%lld %s", job->id, exit_code, job->process.pid, job->command);
  } else {
    snprintf(out, out_size, "[%d] done pid=%lld %s", job->id, job->process.pid, job->command);
  }
  remove_job_at(shell, index);
  return 0;
}

void arksh_shell_refresh_jobs(ArkshShell *shell) {
  size_t i;

  if (shell == NULL) {
    return;
  }

  for (i = 0; i < shell->job_count; ++i) {
    ArkshPlatformProcessState state = ARKSH_PLATFORM_PROCESS_UNCHANGED;
    int exit_code = 0;

    if (shell->jobs[i].state == ARKSH_JOB_DONE) {
      continue;
    }

    if (arksh_platform_poll_background_process(&shell->jobs[i].process, &state, &exit_code) != 0) {
      continue;
    }

    if (state == ARKSH_PLATFORM_PROCESS_RUNNING) {
      shell->jobs[i].state = ARKSH_JOB_RUNNING;
    } else if (state == ARKSH_PLATFORM_PROCESS_STOPPED) {
      shell->jobs[i].state = ARKSH_JOB_STOPPED;
      shell->jobs[i].exit_code = exit_code;
    } else if (state == ARKSH_PLATFORM_PROCESS_EXITED) {
      shell->jobs[i].state = ARKSH_JOB_DONE;
      shell->jobs[i].exit_code = exit_code;
      shell->jobs[i].termination_signal = (exit_code > 128) ? (exit_code - 128) : 0;
    }
  }
}

void arksh_shell_set_program_path(ArkshShell *shell, const char *path) {
  if (shell == NULL || path == NULL || path[0] == '\0') {
    return;
  }

  if (strchr(path, '/') != NULL || strchr(path, '\\') != NULL) {
    if (arksh_platform_resolve_path(shell->cwd, path, shell->program_path, sizeof(shell->program_path)) == 0) {
      copy_string(shell->executable_path, sizeof(shell->executable_path), shell->program_path);
      return;
    }
  }

  copy_string(shell->program_path, sizeof(shell->program_path), path);
  copy_string(shell->executable_path, sizeof(shell->executable_path), shell->program_path);
}

int arksh_shell_start_background_job(ArkshShell *shell, const char *command_text, char *out, size_t out_size) {
  ArkshPlatformAsyncProcess process;
  char *argv[4];
  char error[ARKSH_MAX_OUTPUT];

  if (shell == NULL || command_text == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  arksh_shell_refresh_jobs(shell);
  if (shell->job_count >= ARKSH_MAX_JOBS) {
    prune_completed_jobs(shell);
  }

  if (shell->job_count >= ARKSH_MAX_JOBS) {
    snprintf(out, out_size, "too many background jobs");
    return 1;
  }

  if (shell->executable_path[0] == '\0') {
    snprintf(out, out_size, "background jobs unavailable: shell executable path not set");
    return 1;
  }

  argv[0] = shell->executable_path;
  argv[1] = "-c";
  argv[2] = (char *) command_text;
  argv[3] = NULL;

  error[0] = '\0';
  if (arksh_platform_spawn_background_process(shell->cwd, argv, &process, error, sizeof(error)) != 0) {
    snprintf(out, out_size, "%s", error[0] == '\0' ? "unable to start background job" : error);
    return 1;
  }

  memset(&shell->jobs[shell->job_count], 0, sizeof(shell->jobs[shell->job_count]));
  shell->jobs[shell->job_count].id = shell->next_job_id++;
  shell->jobs[shell->job_count].state = ARKSH_JOB_RUNNING;
  shell->jobs[shell->job_count].exit_code = 0;
  shell->jobs[shell->job_count].process = process;
  shell->last_bg_pid = (long long) process.pid;
  copy_string(shell->jobs[shell->job_count].command, sizeof(shell->jobs[shell->job_count].command), command_text);
  snprintf(
    out,
    out_size,
    "[%d] running pid=%lld %s",
    shell->jobs[shell->job_count].id,
    shell->jobs[shell->job_count].process.pid,
    shell->jobs[shell->job_count].command
  );
  shell->job_count++;
  return 0;
}

void arksh_shell_clear_control_signal(ArkshShell *shell) {
  if (shell == NULL) {
    return;
  }

  shell->control_signal = ARKSH_CONTROL_SIGNAL_NONE;
  shell->control_levels = 0;
}

int arksh_shell_raise_control_signal(ArkshShell *shell, ArkshControlSignalKind kind, int levels) {
  if (shell == NULL || kind == ARKSH_CONTROL_SIGNAL_NONE || levels <= 0) {
    return 1;
  }

  shell->control_signal = kind;
  shell->control_levels = levels;
  return 0;
}

int arksh_shell_run_exit_trap(ArkshShell *shell, char *out, size_t out_size) {
  int status;

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  if (!shell->traps[ARKSH_TRAP_EXIT].active ||
      shell->traps[ARKSH_TRAP_EXIT].command[0] == '\0' ||
      shell->running_exit_trap) {
    return 0;
  }

  shell->running_exit_trap = 1;
  status = arksh_shell_execute_line(shell, shell->traps[ARKSH_TRAP_EXIT].command, out, out_size);
  shell->running_exit_trap = 0;
  return status;
}

/* E1-S6-T1: Generic async signal handler — just sets the pending flag. */
#ifndef _WIN32
static void arksh_generic_signal_handler(int signum) {
  int k;
  for (k = 0; s_trap_map[k].name != NULL; k++) {
    if (s_trap_map[k].signum == signum) {
      s_pending_traps[s_trap_map[k].kind] = 1;
      return;
    }
  }
}
#endif

/* Translate a signal name (with or without "SIG" prefix) to its trap kind. */
static ArkshTrapKind trap_name_to_kind(const char *name) {
  const char *n = name;
  int k;
  if (n == NULL) {
    return ARKSH_TRAP_COUNT;
  }
  /* Strip optional "SIG" prefix. */
  if (strncmp(n, "SIG", 3) == 0) {
    n += 3;
  }
  for (k = 0; s_trap_map[k].name != NULL; k++) {
    if (strcmp(s_trap_map[k].name, n) == 0) {
      return s_trap_map[k].kind;
    }
  }
  return ARKSH_TRAP_COUNT;
}

/* Install or remove the signal handler for the given trap kind. */
static void install_trap_signal(ArkshTrapKind kind, int install) {
#ifndef _WIN32
  int k;
  for (k = 0; s_trap_map[k].name != NULL; k++) {
    if (s_trap_map[k].kind == kind) {
      int signum = s_trap_map[k].signum;
      if (signum != 0) {
        signal(signum, install ? arksh_generic_signal_handler : SIG_DFL);
      }
      return;
    }
  }
#else
  (void) kind;
  (void) install;
#endif
}

/* Fire all pending async trap commands.  Called after each top-level command. */
static void fire_pending_traps(ArkshShell *shell, char *out, size_t out_size) {
  int k;
  (void) out;
  (void) out_size;
  for (k = 0; k < ARKSH_TRAP_COUNT; k++) {
    if (s_pending_traps[k]) {
      s_pending_traps[k] = 0;
      if (shell->traps[k].active && shell->traps[k].command[0] != '\0') {
        char trap_out[ARKSH_MAX_OUTPUT];
        trap_out[0] = '\0';
        arksh_shell_execute_line(shell, shell->traps[k].command, trap_out, sizeof(trap_out));
        if (trap_out[0] != '\0') {
          arksh_shell_write_output(trap_out);
        }
      }
    }
  }
}

static void configure_shell_signals(void) {
#ifndef _WIN32
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);
  signal(SIGTSTP, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
#endif
}

static int is_command_position(const ArkshTokenStream *stream, size_t index) {
  return index == 0 || stream->tokens[index - 1].kind == ARKSH_TOKEN_SHELL_PIPE;
}

static int token_is_plain_word(const ArkshToken *token) {
  return token != NULL &&
         token->kind == ARKSH_TOKEN_WORD &&
         token->text[0] != '\0' &&
         strcmp(token->raw, token->text) == 0;
}

static int expand_aliases_once(
  ArkshShell *shell,
  const char *line,
  char *expanded,
  size_t expanded_size,
  int *out_replaced,
  char *error,
  size_t error_size
) {
  ArkshTokenStream stream;
  size_t last_position = 0;
  size_t i;

  if (shell == NULL || line == NULL || expanded == NULL || expanded_size == 0 || out_replaced == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  *out_replaced = 0;
  expanded[0] = '\0';

  if (arksh_lex_line(line, &stream, error, error_size) != 0) {
    return 1;
  }

  for (i = 0; i < stream.count; ++i) {
    const ArkshToken *token = &stream.tokens[i];
    const char *alias_value;
    size_t token_end;

    if (token->kind == ARKSH_TOKEN_EOF) {
      break;
    }

    if (!is_command_position(&stream, i) ||
        !token_is_plain_word(token) ||
        (i + 1 < stream.count && stream.tokens[i + 1].kind == ARKSH_TOKEN_ARROW)) {
      continue;
    }

    alias_value = arksh_shell_get_alias(shell, token->text);
    if (alias_value == NULL) {
      continue;
    }

    token_end = token->position + strlen(token->raw);
    if (append_slice(expanded, expanded_size, line, last_position, token->position) != 0 ||
        append_text(expanded, expanded_size, alias_value) != 0) {
      snprintf(error, error_size, "alias expansion produced a line that is too long");
      return 1;
    }

    last_position = token_end;
    *out_replaced = 1;
  }

  if (append_slice(expanded, expanded_size, line, last_position, strlen(line)) != 0) {
    snprintf(error, error_size, "alias expansion produced a line that is too long");
    return 1;
  }

  return 0;
}

static int expand_aliases(
  ArkshShell *shell,
  const char *line,
  char *expanded,
  size_t expanded_size,
  char *error,
  size_t error_size
) {
  char current[ARKSH_MAX_LINE];
  char next[ARKSH_MAX_LINE];
  int depth;

  if (shell == NULL || line == NULL || expanded == NULL || expanded_size == 0 || error == NULL || error_size == 0) {
    return 1;
  }

  copy_string(current, sizeof(current), line);
  for (depth = 0; depth < 8; ++depth) {
    int replaced = 0;

    if (expand_aliases_once(shell, current, next, sizeof(next), &replaced, error, error_size) != 0) {
      return 1;
    }

    if (!replaced) {
      copy_string(expanded, expanded_size, current);
      return 0;
    }

    copy_string(current, sizeof(current), next);
  }

  snprintf(error, error_size, "alias expansion depth exceeded");
  return 1;
}

static int join_path(const char *directory, const char *name, char *out, size_t out_size) {
  const char *separator = arksh_platform_path_separator();

  if (directory == NULL || name == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (directory[0] == '\0' || strcmp(directory, ".") == 0) {
    copy_string(out, out_size, name);
    return 0;
  }

  if (snprintf(out, out_size, "%s%s%s", directory, separator, name) >= (int) out_size) {
    return 1;
  }

  return 0;
}

static int command_has_path_component(const char *name) {
  if (name == NULL) {
    return 0;
  }

  if (strchr(name, '/') != NULL || strchr(name, '\\') != NULL) {
    return 1;
  }

#ifdef _WIN32
  if (strchr(name, ':') != NULL) {
    return 1;
  }
#endif

  return 0;
}

static int path_exists_as_file(const char *path) {
  ArkshPlatformFileInfo info;

  if (path == NULL || path[0] == '\0') {
    return 0;
  }

  if (arksh_platform_stat(path, &info) != 0 || !info.exists || info.is_directory) {
    return 0;
  }

  return 1;
}

static int try_command_candidate(const char *candidate, char *resolved, size_t resolved_size) {
  if (candidate == NULL || resolved == NULL || resolved_size == 0) {
    return 1;
  }

  if (path_exists_as_file(candidate)) {
    copy_string(resolved, resolved_size, candidate);
    return 0;
  }

#ifdef _WIN32
  {
    const char *pathext = getenv("PATHEXT");
    const char *fallback = ".COM;.EXE;.BAT;.CMD";
    const char *cursor;
    char extension[32];
    const char *last_dot = strrchr(candidate, '.');
    const char *last_sep = strrchr(candidate, '\\');

    if (last_sep == NULL) {
      last_sep = strrchr(candidate, '/');
    }

    if (last_dot != NULL && (last_sep == NULL || last_dot > last_sep)) {
      return 1;
    }

    cursor = (pathext != NULL && pathext[0] != '\0') ? pathext : fallback;
    while (*cursor != '\0') {
      size_t ext_len = 0;
      char with_ext[ARKSH_MAX_PATH];

      while (*cursor != '\0' && *cursor != ';') {
        if (ext_len + 1 >= sizeof(extension)) {
          return 1;
        }
        extension[ext_len++] = *cursor++;
      }
      extension[ext_len] = '\0';

      if (snprintf(with_ext, sizeof(with_ext), "%s%s", candidate, extension) < (int) sizeof(with_ext) &&
          path_exists_as_file(with_ext)) {
        copy_string(resolved, resolved_size, with_ext);
        return 0;
      }

      if (*cursor == ';') {
        cursor++;
      }
    }
  }
#endif

  return 1;
}

static int resolve_command_path(const ArkshShell *shell, const char *name, char *out, size_t out_size) {
  const char *path_env;
  const char *cursor;
  char resolved[ARKSH_MAX_PATH];

  if (shell == NULL || name == NULL || name[0] == '\0' || out == NULL || out_size == 0) {
    return 1;
  }

  if (command_has_path_component(name)) {
    if (arksh_platform_resolve_path(shell->cwd, name, resolved, sizeof(resolved)) != 0) {
      return 1;
    }
    if (try_command_candidate(resolved, out, out_size) == 0) {
      return 0;
    }
    return 1;
  }

  path_env = arksh_shell_get_var(shell, "PATH");
  if (path_env == NULL || path_env[0] == '\0') {
    return 1;
  }

  cursor = path_env;
  while (*cursor != '\0') {
    char directory[ARKSH_MAX_PATH];
    char candidate[ARKSH_MAX_PATH];
    size_t dir_len = 0;
    char separator =
#ifdef _WIN32
      ';';
#else
      ':';
#endif

    while (*cursor != '\0' && *cursor != separator) {
      if (dir_len + 1 >= sizeof(directory)) {
        return 1;
      }
      directory[dir_len++] = *cursor++;
    }
    directory[dir_len] = '\0';

    if (directory[0] == '\0') {
      copy_string(directory, sizeof(directory), ".");
    }

    if (join_path(directory, name, candidate, sizeof(candidate)) != 0) {
      return 1;
    }
    if (arksh_platform_resolve_path(shell->cwd, candidate, resolved, sizeof(resolved)) == 0 &&
        try_command_candidate(resolved, out, out_size) == 0) {
      return 0;
    }

    if (*cursor == separator) {
      cursor++;
    }
  }

  return 1;
}

/* E6-S4-T2: structured help with topic filtering and per-name lookup. */
static int command_help(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  const char *topic = argc >= 2 ? argv[1] : NULL;
  size_t i;
  int found = 0;

  if (topic == NULL) {
    arksh_shell_print_help(shell, out, out_size);
    return 0;
  }

  /* help commands */
  if (strcmp(topic, "commands") == 0) {
    snprintf(out, out_size, "Commands (%zu):\n", shell->command_count);
    for (i = 0; i < shell->command_count; ++i) {
      if (shell->commands[i].is_plugin_command && !plugin_index_is_active(shell, shell->commands[i].owner_plugin_index)) continue;
      snprintf(out + strlen(out), out_size - strlen(out), "  %-14s %s%s\n",
        shell->commands[i].name,
        shell->commands[i].description,
        shell->commands[i].is_plugin_command ? " [plugin]" : "");
    }
    return 0;
  }

  /* help resolvers */
  if (strcmp(topic, "resolvers") == 0) {
    snprintf(out, out_size, "Value Resolvers (%zu):\n", shell->value_resolver_count);
    for (i = 0; i < shell->value_resolver_count; ++i) {
      if (shell->value_resolvers[i].is_plugin_resolver && !plugin_index_is_active(shell, shell->value_resolvers[i].owner_plugin_index)) continue;
      snprintf(out + strlen(out), out_size - strlen(out), "  %-14s %s%s\n",
        shell->value_resolvers[i].name,
        shell->value_resolvers[i].description,
        shell->value_resolvers[i].is_plugin_resolver ? " [plugin]" : "");
    }
    return 0;
  }

  /* help stages */
  if (strcmp(topic, "stages") == 0) {
    snprintf(out, out_size, "Pipeline Stages (%zu):\n", shell->pipeline_stage_count);
    for (i = 0; i < shell->pipeline_stage_count; ++i) {
      if (shell->pipeline_stages[i].is_plugin_stage && !plugin_index_is_active(shell, shell->pipeline_stages[i].owner_plugin_index)) continue;
      snprintf(out + strlen(out), out_size - strlen(out), "  %-14s %s%s\n",
        shell->pipeline_stages[i].name,
        shell->pipeline_stages[i].description,
        shell->pipeline_stages[i].is_plugin_stage ? " [plugin]" : "");
    }
    return 0;
  }

  /* help types */
  if (strcmp(topic, "types") == 0) {
    snprintf(out, out_size, "Types (%zu):\n", shell->type_descriptor_count);
    for (i = 0; i < shell->type_descriptor_count; ++i) {
      snprintf(out + strlen(out), out_size - strlen(out), "  %-14s %s\n",
        shell->type_descriptors[i].type_name,
        shell->type_descriptors[i].description);
    }
    return 0;
  }

  /* help <name> — search across all categories */
  for (i = 0; i < shell->command_count; ++i) {
    if (strcmp(shell->commands[i].name, topic) == 0) {
      snprintf(out + strlen(out), out_size - strlen(out), "%s  [command]\n  %s\n", shell->commands[i].name, shell->commands[i].description);
      found = 1;
    }
  }
  for (i = 0; i < shell->value_resolver_count; ++i) {
    if (strcmp(shell->value_resolvers[i].name, topic) == 0) {
      snprintf(out + strlen(out), out_size - strlen(out), "%s  [resolver]\n  %s\n", shell->value_resolvers[i].name, shell->value_resolvers[i].description);
      found = 1;
    }
  }
  for (i = 0; i < shell->pipeline_stage_count; ++i) {
    if (strcmp(shell->pipeline_stages[i].name, topic) == 0) {
      snprintf(out + strlen(out), out_size - strlen(out), "%s  [stage]\n  %s\n", shell->pipeline_stages[i].name, shell->pipeline_stages[i].description);
      found = 1;
    }
  }
  for (i = 0; i < shell->type_descriptor_count; ++i) {
    if (strcmp(shell->type_descriptors[i].type_name, topic) == 0) {
      snprintf(out + strlen(out), out_size - strlen(out), "%s  [type]\n  %s\n", shell->type_descriptors[i].type_name, shell->type_descriptors[i].description);
      found = 1;
    }
  }

  if (!found) {
    snprintf(out, out_size, "no help entry for '%s'\nTopics: commands, resolvers, stages, types", topic);
    return 1;
  }
  return 0;
}

static int command_exit(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  (void) argc;
  (void) argv;
  shell->running = 0;
  copy_string(out, out_size, "bye");
  return 0;
}

static int command_pwd(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  (void) argc;
  (void) argv;
  snprintf(out, out_size, "%s", shell->cwd);
  return 0;
}

static int command_cd(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  const char *target;
  char resolved[ARKSH_MAX_PATH];
  char previous_cwd[ARKSH_MAX_PATH];

  copy_string(previous_cwd, sizeof(previous_cwd), shell->cwd);

  if (argc >= 2 && strcmp(argv[1], "-") == 0) {
    target = arksh_shell_get_var(shell, "OLDPWD");
    if (target == NULL || target[0] == '\0') {
      snprintf(out, out_size, "OLDPWD is not set");
      return 1;
    }
  } else {
    target = argc >= 2 ? argv[1] : arksh_shell_get_var(shell, "HOME");
    if (target == NULL || target[0] == '\0') {
      target = ".";
    }
  }

  if (arksh_platform_resolve_path(shell->cwd, target, resolved, sizeof(resolved)) != 0) {
    snprintf(out, out_size, "unable to resolve path: %s", target);
    return 1;
  }

  if (arksh_platform_chdir(resolved) != 0) {
    snprintf(out, out_size, "unable to change directory to %s", resolved);
    return 1;
  }

  if (arksh_platform_getcwd(shell->cwd, sizeof(shell->cwd)) != 0) {
    snprintf(out, out_size, "directory changed but getcwd failed");
    return 1;
  }

  if (update_directory_vars(shell, previous_cwd) != 0) {
    snprintf(out, out_size, "directory changed but shell variables could not be updated");
    return 1;
  }

  snprintf(out, out_size, "%s", shell->cwd);
  return 0;
}

static int command_inspect(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  ArkshObject object;

  if (argc < 2) {
    snprintf(out, out_size, "usage: inspect <path>");
    return 1;
  }

  if (arksh_object_resolve(shell->cwd, argv[1], &object) != 0) {
    snprintf(out, out_size, "unable to inspect %s", argv[1]);
    return 1;
  }

  return arksh_object_call_method(&object, "describe", 0, NULL, out, out_size);
}

static int command_get(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  ArkshObject object;

  if (argc < 3) {
    snprintf(out, out_size, "usage: get <path> <property>");
    return 1;
  }

  if (arksh_object_resolve(shell->cwd, argv[1], &object) != 0) {
    snprintf(out, out_size, "unable to resolve %s", argv[1]);
    return 1;
  }

  return arksh_object_get_property(&object, argv[2], out, out_size);
}

static int command_call(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  ArkshObject object;

  if (argc < 3) {
    snprintf(out, out_size, "usage: call <path> <method> [args...]");
    return 1;
  }

  if (arksh_object_resolve(shell->cwd, argv[1], &object) != 0) {
    snprintf(out, out_size, "unable to resolve %s", argv[1]);
    return 1;
  }

  return arksh_object_call_method(&object, argv[2], argc - 3, argv + 3, out, out_size);
}

static int command_prompt(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  char path[ARKSH_MAX_PATH];

  if (argc < 2 || strcmp(argv[1], "show") == 0) {
    size_t i;
    char left[128] = "";
    char right[128] = "";

    for (i = 0; i < shell->prompt.left_count; ++i) {
      if (left[0] != '\0') {
        snprintf(left + strlen(left), sizeof(left) - strlen(left), ",");
      }
      snprintf(left + strlen(left), sizeof(left) - strlen(left), "%s", shell->prompt.left[i]);
    }

    for (i = 0; i < shell->prompt.right_count; ++i) {
      if (right[0] != '\0') {
        snprintf(right + strlen(right), sizeof(right) - strlen(right), ",");
      }
      snprintf(right + strlen(right), sizeof(right) - strlen(right), "%s", shell->prompt.right[i]);
    }

    snprintf(
      out,
      out_size,
      "theme=%s\nleft=%s\nright=%s\nseparator=%s\nuse_color=%d\nplugins=%zu",
      shell->prompt.theme,
      left,
      right,
      shell->prompt.separator,
      shell->prompt.use_color,
      shell->prompt.plugin_count
    );
    return 0;
  }

  if (strcmp(argv[1], "load") == 0) {
    if (argc < 3) {
      snprintf(out, out_size, "usage: prompt load <path>");
      return 1;
    }
    if (arksh_platform_resolve_path(shell->cwd, argv[2], path, sizeof(path)) != 0) {
      snprintf(out, out_size, "unable to resolve config path: %s", argv[2]);
      return 1;
    }
    return arksh_shell_load_config(shell, path, out, out_size);
  }

  snprintf(out, out_size, "unknown prompt command: %s", argv[1]);
  return 1;
}

static int plugin_autoload_conf_paths(
  ArkshShell *shell,
  char *primary,
  size_t primary_size,
  char *legacy,
  size_t legacy_size
) {
  char legacy_dir[ARKSH_MAX_PATH];

  if (shell == NULL) {
    return 1;
  }

  if (primary != NULL && primary_size > 0) {
    primary[0] = '\0';
  }
  if (legacy != NULL && legacy_size > 0) {
    legacy[0] = '\0';
  }

  if (primary != NULL && primary_size > 0 && shell->config_dir[0] != '\0') {
    if (join_runtime_path(shell->config_dir, "plugins.conf", primary, primary_size) != 0) {
      primary[0] = '\0';
    }
  }

  if (legacy != NULL && legacy_size > 0) {
    build_legacy_arksh_dir(shell, legacy_dir, sizeof(legacy_dir));
    if (legacy_dir[0] != '\0' &&
        join_runtime_path(legacy_dir, "plugins.conf", legacy, legacy_size) != 0) {
      legacy[0] = '\0';
    }
  }

  return (primary != NULL && primary[0] != '\0') || (legacy != NULL && legacy[0] != '\0') ? 0 : 1;
}

static int try_load_plugin_autoload_from(ArkshShell *shell, const char *conf_path) {
  char line[ARKSH_MAX_PATH];
  char trimmed[ARKSH_MAX_PATH];
  char plugin_output[ARKSH_MAX_OUTPUT];
  FILE *fp;

  if (conf_path == NULL || conf_path[0] == '\0') {
    return 0;
  }

  fp = fopen(conf_path, "r");
  if (fp == NULL) {
    return 0;
  }

  while (fgets(line, sizeof(line), fp) != NULL) {
    trim_copy(line, trimmed, sizeof(trimmed));
    if (trimmed[0] == '\0' || trimmed[0] == '#') {
      continue;
    }
    arksh_shell_load_plugin(shell, trimmed, plugin_output, sizeof(plugin_output));
  }

  fclose(fp);
  return 0;
}

static int try_load_plugin_autoload(ArkshShell *shell) {
  char primary[ARKSH_MAX_PATH];
  char legacy[ARKSH_MAX_PATH];

  if (plugin_autoload_conf_paths(shell, primary, sizeof(primary), legacy, sizeof(legacy)) != 0) {
    return 0;
  }

  if (primary[0] != '\0') {
    try_load_plugin_autoload_from(shell, primary);
  }
  if (legacy[0] != '\0' && strcmp(primary, legacy) != 0) {
    try_load_plugin_autoload_from(shell, legacy);
  }

  return 0;
}

static int command_plugin(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  ArkshLoadedPlugin *plugin;

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (argc < 2 || strcmp(argv[1], "list") == 0) {
    size_t i;

    if (shell->plugin_count == 0) {
      snprintf(out, out_size, "no plugins loaded");
      return 0;
    }

    out[0] = '\0';
    for (i = 0; i < shell->plugin_count; ++i) {
      snprintf(
        out + strlen(out),
        out_size - strlen(out),
        "%s%s %s [%s] %s :: %s",
        i == 0 ? "" : "\n",
        shell->plugins[i].name,
        shell->plugins[i].version,
        shell->plugins[i].active ? "enabled" : "disabled",
        shell->plugins[i].path,
        shell->plugins[i].description
      );
    }
    return 0;
  }

  if (strcmp(argv[1], "load") == 0) {
    if (argc < 3) {
      snprintf(out, out_size, "usage: plugin load <path>");
      return 1;
    }
    return arksh_shell_load_plugin(shell, argv[2], out, out_size);
  }

  if (argc >= 3 && strcmp(argv[1], "info") == 0) {
    plugin = find_loaded_plugin(shell, argv[2]);
    if (plugin == NULL) {
      snprintf(out, out_size, "plugin not found: %s", argv[2]);
      return 1;
    }

    snprintf(
      out,
      out_size,
      "name=%s\nversion=%s\nstatus=%s\npath=%s\ndescription=%s",
      plugin->name,
      plugin->version,
      plugin->active ? "enabled" : "disabled",
      plugin->path,
      plugin->description
    );
    return 0;
  }

  if (argc >= 3 && strcmp(argv[1], "enable") == 0) {
    plugin = find_loaded_plugin(shell, argv[2]);
    if (plugin == NULL) {
      snprintf(out, out_size, "plugin not found: %s", argv[2]);
      return 1;
    }
    plugin->active = 1;
    snprintf(out, out_size, "plugin enabled: %s", plugin->name);
    return 0;
  }

  if (argc >= 3 && strcmp(argv[1], "disable") == 0) {
    plugin = find_loaded_plugin(shell, argv[2]);
    if (plugin == NULL) {
      snprintf(out, out_size, "plugin not found: %s", argv[2]);
      return 1;
    }
    plugin->active = 0;
    snprintf(out, out_size, "plugin disabled: %s", plugin->name);
    return 0;
  }

  if (strcmp(argv[1], "autoload") == 0) {
    char conf_path[ARKSH_MAX_PATH];
    char legacy_conf_path[ARKSH_MAX_PATH];
    char dir_path[ARKSH_MAX_PATH];

    if (plugin_autoload_conf_paths(shell, conf_path, sizeof(conf_path), legacy_conf_path, sizeof(legacy_conf_path)) != 0) {
      snprintf(out, out_size, "plugin autoload: config directory unavailable");
      return 1;
    }

    /* plugin autoload list */
    if (argc < 3 || strcmp(argv[2], "list") == 0) {
      FILE *fp = NULL;
      char line[ARKSH_MAX_PATH];
      char trimmed[ARKSH_MAX_PATH];
      int found = 0;

      if (conf_path[0] != '\0') {
        fp = fopen(conf_path, "r");
      }
      if (fp == NULL && legacy_conf_path[0] != '\0' && strcmp(conf_path, legacy_conf_path) != 0) {
        fp = fopen(legacy_conf_path, "r");
      }

      if (fp != NULL) {
        out[0] = '\0';
        while (fgets(line, sizeof(line), fp) != NULL) {
          trim_copy(line, trimmed, sizeof(trimmed));
          if (trimmed[0] == '\0' || trimmed[0] == '#') {
            continue;
          }
          if (found) {
            strncat(out, "\n", out_size - strlen(out) - 1);
          }
          strncat(out, trimmed, out_size - strlen(out) - 1);
          found = 1;
        }
        fclose(fp);
      }

      if (!found) {
        snprintf(out, out_size, "no plugins configured for autoload");
      }
      return 0;
    }

    /* plugin autoload set <path> */
    if (strcmp(argv[2], "set") == 0) {
      char resolved[ARKSH_MAX_PATH];
      char line[ARKSH_MAX_PATH];
      char trimmed[ARKSH_MAX_PATH];
      FILE *fp;
      int already_set = 0;

      if (argc < 4) {
        snprintf(out, out_size, "usage: plugin autoload set <path>");
        return 1;
      }

      if (arksh_shell_resolve_plugin_path(shell, argv[3], resolved, sizeof(resolved)) != 0) {
        snprintf(out, out_size, "unable to resolve path: %s", argv[3]);
        return 1;
      }

      if (conf_path[0] == '\0') {
        copy_string(conf_path, sizeof(conf_path), legacy_conf_path);
      }

      /* ensure config directory exists */
      arksh_platform_dirname(conf_path, dir_path, sizeof(dir_path));
      arksh_platform_ensure_directory(dir_path);

      /* check if already present */
      fp = fopen(conf_path, "r");
      if (fp != NULL) {
        while (fgets(line, sizeof(line), fp) != NULL) {
          trim_copy(line, trimmed, sizeof(trimmed));
          if (strcmp(trimmed, resolved) == 0) {
            already_set = 1;
            break;
          }
        }
        fclose(fp);
      }

      if (already_set) {
        snprintf(out, out_size, "plugin already configured for autoload: %s", resolved);
        return 0;
      }

      fp = fopen(conf_path, "a");
      if (fp == NULL) {
        snprintf(out, out_size, "unable to write autoload config: %s", conf_path);
        return 1;
      }
      fprintf(fp, "%s\n", resolved);
      fclose(fp);

      snprintf(out, out_size, "plugin added to autoload: %s", resolved);
      return 0;
    }

    /* plugin autoload unset <path-or-name> */
    if (strcmp(argv[2], "unset") == 0) {
      char resolved[ARKSH_MAX_PATH];
      char tmp_path[ARKSH_MAX_PATH];
      char line[ARKSH_MAX_PATH];
      char trimmed[ARKSH_MAX_PATH];
      FILE *fp;
      FILE *tmp_fp;
      int have_resolved;
      int removed = 0;

      if (argc < 4) {
        snprintf(out, out_size, "usage: plugin autoload unset <path>");
        return 1;
      }

      have_resolved = (arksh_shell_resolve_plugin_path(shell, argv[3], resolved, sizeof(resolved)) == 0);

      fp = fopen(conf_path, "r");
      if (fp == NULL && legacy_conf_path[0] != '\0' && strcmp(conf_path, legacy_conf_path) != 0) {
        copy_string(conf_path, sizeof(conf_path), legacy_conf_path);
        fp = fopen(conf_path, "r");
      }
      if (fp == NULL) {
        snprintf(out, out_size, "no autoload config found");
        return 1;
      }

      snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", conf_path);
      tmp_fp = fopen(tmp_path, "w");
      if (tmp_fp == NULL) {
        fclose(fp);
        snprintf(out, out_size, "unable to write temporary config: %s", tmp_path);
        return 1;
      }

      while (fgets(line, sizeof(line), fp) != NULL) {
        trim_copy(line, trimmed, sizeof(trimmed));
        if (trimmed[0] == '\0' || trimmed[0] == '#') {
          fputs(line, tmp_fp);
          continue;
        }
        if (strcmp(trimmed, argv[3]) == 0 ||
            (have_resolved && strcmp(trimmed, resolved) == 0)) {
          removed = 1;
        } else {
          fputs(line, tmp_fp);
        }
      }
      fclose(fp);
      fclose(tmp_fp);

      if (!removed) {
        remove(tmp_path);
        snprintf(out, out_size, "plugin not found in autoload: %s", argv[3]);
        return 1;
      }

      if (rename(tmp_path, conf_path) != 0) {
        snprintf(out, out_size, "unable to update autoload config");
        return 1;
      }

      snprintf(out, out_size, "plugin removed from autoload: %s", argv[3]);
      return 0;
    }

    snprintf(out, out_size, "usage: plugin autoload [list|set <path>|unset <path>]");
    return 1;
  }

  snprintf(out, out_size, "unknown plugin command: %s", argv[1]);
  return 1;
}

static int command_run(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  if (argc < 2) {
    snprintf(out, out_size, "usage: run <external-command> [args...]");
    return 1;
  }

  return arksh_execute_external_command(shell, argc - 1, argv + 1, out, out_size);
}

static int command_let(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  (void) argv;

  if (argc == 1) {
    return format_binding_list(shell, out, out_size);
  }

  snprintf(out, out_size, "let uses whole-line syntax: let <name> = <value-expression>");
  return 1;
}

static int command_function(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  const ArkshShellFunction *function_def;
  int i;

  if (argc == 1) {
    return format_function_list(shell, out, out_size);
  }

  if (argc != 2) {
    snprintf(out, out_size, "usage: function [name]");
    return 1;
  }

  function_def = arksh_shell_find_function(shell, argv[1]);
  if (function_def == NULL) {
    snprintf(out, out_size, "function not found: %s", argv[1]);
    return 1;
  }

  snprintf(out, out_size, "function %s(", function_def->name);
  for (i = 0; i < function_def->param_count; ++i) {
    if (i > 0) {
      snprintf(out + strlen(out), out_size - strlen(out), ", ");
    }
    snprintf(out + strlen(out), out_size - strlen(out), "%s", function_def->params[i]);
  }
  snprintf(out + strlen(out), out_size - strlen(out), ")\ndo\n%s\nendfunction", function_def->body);
  return 0;
}

static int command_class(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  const ArkshClassDef *class_def;
  size_t i;

  if (argc == 1) {
    return format_class_list(shell, out, out_size);
  }

  if (argc != 2) {
    snprintf(out, out_size, "usage: classes [name]");
    return 1;
  }

  class_def = arksh_shell_find_class(shell, argv[1]);
  if (class_def == NULL) {
    snprintf(out, out_size, "class not found: %s", argv[1]);
    return 1;
  }

  snprintf(out, out_size, "class %s", class_def->name);
  if (class_def->base_count > 0) {
    snprintf(out + strlen(out), out_size - strlen(out), " extends ");
    for (i = 0; i < (size_t) class_def->base_count; ++i) {
      if (i > 0) {
        snprintf(out + strlen(out), out_size - strlen(out), ", ");
      }
      snprintf(out + strlen(out), out_size - strlen(out), "%s", class_def->bases[i]);
    }
  }
  snprintf(out + strlen(out), out_size - strlen(out), "\ndo\n");
  for (i = 0; i < class_def->property_count; ++i) {
    char rendered[ARKSH_MAX_OUTPUT];

    if (arksh_value_render(&class_def->properties[i].default_value, rendered, sizeof(rendered)) != 0) {
      copy_string(rendered, sizeof(rendered), "<unrenderable>");
    }
    snprintf(
      out + strlen(out),
      out_size - strlen(out),
      "  property %s = %s\n",
      class_def->properties[i].name,
      rendered
    );
  }
  for (i = 0; i < class_def->method_count; ++i) {
    snprintf(
      out + strlen(out),
      out_size - strlen(out),
      "  method %s = %s\n",
      class_def->methods[i].name,
      class_def->methods[i].block.source
    );
  }
  snprintf(out + strlen(out), out_size - strlen(out), "endclass");
  return 0;
}

static int command_extend(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  (void) argv;

  if (argc == 1) {
    return format_extension_list(shell, out, out_size);
  }

  snprintf(out, out_size, "extend uses whole-line syntax: extend <target> <property|method> <name> = <block>");
  return 1;
}

/* E1-S6-T2: set built-in — handles -e/-u/-x/-o plus legacy assignment. */
static int command_set(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  int i;
  char name[ARKSH_MAX_VAR_NAME];
  char value[ARKSH_MAX_VAR_VALUE];
  int saw_double_dash = 0;

  if (argc == 1) {
    return format_var_list(shell, 0, out, out_size);
  }

  out[0] = '\0';

  /* Process flags: -e -u -x +e +u +x -o pipefail -o nopipefail etc. */
  for (i = 1; i < argc; i++) {
    const char *arg = argv[i];
    int enable;

    if (arg[0] != '-' && arg[0] != '+') {
      break; /* not a flag, fall through to legacy var-assign logic */
    }
    if (strcmp(arg, "--") == 0) {
      saw_double_dash = 1;
      i++;
      break; /* end of options: remaining args are positional params */
    }

    enable = (arg[0] == '-') ? 1 : 0;

    if (strcmp(arg, "-o") == 0 || strcmp(arg, "+o") == 0) {
      /* -o option / +o option */
      i++;
      if (i >= argc) {
        snprintf(out, out_size, "set: -o requires an option name");
        return 1;
      }
      const char *optname = argv[i];
      if (strcmp(optname, "errexit") == 0) {
        shell->opt_errexit = enable;
      } else if (strcmp(optname, "nounset") == 0) {
        shell->opt_nounset = enable;
      } else if (strcmp(optname, "xtrace") == 0) {
        shell->opt_xtrace = enable;
      } else if (strcmp(optname, "pipefail") == 0) {
        shell->opt_pipefail = enable;
      } else if (strcmp(optname, "nopipefail") == 0) {
        shell->opt_pipefail = !enable;
      } else {
        snprintf(out, out_size, "set: unknown option: %s", optname);
        return 1;
      }
      continue;
    }

    /* Flags in a single arg like -eux */
    {
      const char *f = arg + 1;
      while (*f != '\0') {
        switch (*f) {
          case 'e': shell->opt_errexit = enable; break;
          case 'u': shell->opt_nounset = enable; break;
          case 'x': shell->opt_xtrace  = enable; break;
          default:
            /* Unknown flag — stop flag processing, treat as positional. */
            goto end_flags;
        }
        f++;
      }
    }
    continue;
  end_flags:
    break;
  }

  /* E1-S7-T6: set -- [args...] sets positional parameters $1 $2 ... */
  if (saw_double_dash) {
    int j;
    int new_count = argc - i;

    if (new_count > ARKSH_MAX_POSITIONAL_PARAMS) {
      snprintf(out, out_size, "set: too many positional parameters");
      return 1;
    }
    for (j = 0; j < new_count; j++) {
      copy_string(shell->positional_params[j], sizeof(shell->positional_params[j]), argv[i + j]);
    }
    for (j = new_count; j < shell->positional_count; j++) {
      shell->positional_params[j][0] = '\0';
    }
    shell->positional_count = new_count;
    return 0;
  }

  /* If all args were flags, done. */
  if (i >= argc) {
    return 0;
  }

  /* Legacy: set name value  or  set name=value */
  if (i == 1 && split_assignment(argv[1], name, sizeof(name), value, sizeof(value)) == 0) {
    if (!is_valid_identifier(name) || arksh_shell_set_var(shell, name, value, 0) != 0) {
      snprintf(out, out_size, "unable to set variable: %s", argv[1]);
      return 1;
    }
    return 0;
  }

  copy_string(name, sizeof(name), argv[i]);
  if (!is_valid_identifier(name)) {
    snprintf(out, out_size, "invalid variable name: %s", argv[i]);
    return 1;
  }

  if (i + 1 >= argc) {
    value[0] = '\0';
  } else if (join_arguments(argc, argv, i + 1, value, sizeof(value)) != 0) {
    snprintf(out, out_size, "variable value too long");
    return 1;
  }

  if (arksh_shell_set_var(shell, name, value, 0) != 0) {
    snprintf(out, out_size, "unable to set variable: %s", name);
    return 1;
  }

  return 0;
}

static int command_export(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  char name[ARKSH_MAX_VAR_NAME];
  char value[ARKSH_MAX_VAR_VALUE];
  const char *current_value;

  if (argc == 1) {
    return format_var_list(shell, 1, out, out_size);
  }

  if (argc == 2 && split_assignment(argv[1], name, sizeof(name), value, sizeof(value)) == 0) {
    if (!is_valid_identifier(name) || arksh_shell_set_var(shell, name, value, 1) != 0) {
      snprintf(out, out_size, "unable to export variable: %s", argv[1]);
      return 1;
    }
    out[0] = '\0';
    return 0;
  }

  copy_string(name, sizeof(name), argv[1]);
  if (!is_valid_identifier(name)) {
    snprintf(out, out_size, "invalid variable name: %s", argv[1]);
    return 1;
  }

  if (argc == 2) {
    current_value = arksh_shell_get_var(shell, name);
    if (current_value == NULL) {
      current_value = "";
    }
    copy_string(value, sizeof(value), current_value);
  } else if (join_arguments(argc, argv, 2, value, sizeof(value)) != 0) {
    snprintf(out, out_size, "variable value too long");
    return 1;
  }

  if (arksh_shell_set_var(shell, name, value, 1) != 0) {
    snprintf(out, out_size, "unable to export variable: %s", name);
    return 1;
  }

  out[0] = '\0';
  return 0;
}

static int command_unset(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  int i;

  if (argc < 2) {
    snprintf(out, out_size, "usage: unset <name> [name...]");
    return 1;
  }

  out[0] = '\0';
  for (i = 1; i < argc; ++i) {
    int removed = 0;

    if (!is_valid_identifier(argv[i])) {
      snprintf(out, out_size, "unable to unset variable: %s", argv[i]);
      return 1;
    }

    if (arksh_shell_unset_var(shell, argv[i]) == 0) {
      removed = 1;
    }
    if (arksh_shell_unset_binding(shell, argv[i]) == 0) {
      removed = 1;
    }
    if (!removed) {
      snprintf(out, out_size, "unable to unset variable: %s", argv[i]);
      return 1;
    }
  }

  return 0;
}

static int command_alias(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  char name[ARKSH_MAX_VAR_NAME];
  char value[ARKSH_MAX_ALIAS_VALUE];
  const char *alias_value;

  if (argc == 1) {
    return format_alias_list(shell, out, out_size);
  }

  if (argc == 2 && split_assignment(argv[1], name, sizeof(name), value, sizeof(value)) == 0) {
    if (!is_valid_alias_name(name) || arksh_shell_set_alias(shell, name, value) != 0) {
      snprintf(out, out_size, "unable to define alias: %s", argv[1]);
      return 1;
    }
    out[0] = '\0';
    return 0;
  }

  if (argc == 2) {
    alias_value = arksh_shell_get_alias(shell, argv[1]);
    if (alias_value == NULL) {
      snprintf(out, out_size, "alias not found: %s", argv[1]);
      return 1;
    }
    snprintf(out, out_size, "%s=%s", argv[1], alias_value);
    return 0;
  }

  copy_string(name, sizeof(name), argv[1]);
  if (!is_valid_alias_name(name)) {
    snprintf(out, out_size, "invalid alias name: %s", argv[1]);
    return 1;
  }

  if (join_arguments(argc, argv, 2, value, sizeof(value)) != 0) {
    snprintf(out, out_size, "alias value too long");
    return 1;
  }

  if (arksh_shell_set_alias(shell, name, value) != 0) {
    snprintf(out, out_size, "unable to define alias: %s", name);
    return 1;
  }

  out[0] = '\0';
  return 0;
}

static int command_unalias(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  int i;

  if (argc < 2) {
    snprintf(out, out_size, "usage: unalias <name> [name...]");
    return 1;
  }

  out[0] = '\0';
  for (i = 1; i < argc; ++i) {
    if (arksh_shell_unset_alias(shell, argv[i]) != 0) {
      snprintf(out, out_size, "alias not found: %s", argv[i]);
      return 1;
    }
  }

  return 0;
}

int arksh_shell_source_file(ArkshShell *shell, const char *path, int positional_count, char **positional_args, char *out, size_t out_size) {
  char resolved[ARKSH_MAX_PATH];
  FILE *fp;
  char line[ARKSH_MAX_LINE];
  char command_buffer[ARKSH_MAX_OUTPUT];
  size_t line_number = 0;
  size_t command_start_line = 0;
  int pending_heredoc = 0;
  char saved_program_path[ARKSH_MAX_PATH];
  char saved_positional_params[ARKSH_MAX_POSITIONAL_PARAMS][ARKSH_MAX_VAR_VALUE];
  int saved_positional_count;
  int i;

  if (shell == NULL || path == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  command_buffer[0] = '\0';

  if (arksh_platform_resolve_path(shell->cwd, path, resolved, sizeof(resolved)) != 0) {
    snprintf(out, out_size, "unable to resolve source path: %s", path);
    return 1;
  }

  fp = fopen(resolved, "rb");
  if (fp == NULL) {
    snprintf(out, out_size, "unable to open source file: %s", resolved);
    return 1;
  }

  /* Snapshot current positional context and set new one for the sourced file. */
  copy_string(saved_program_path, sizeof(saved_program_path), shell->program_path);
  saved_positional_count = shell->positional_count;
  for (i = 0; i < shell->positional_count && i < ARKSH_MAX_POSITIONAL_PARAMS; ++i) {
    copy_string(saved_positional_params[i], sizeof(saved_positional_params[i]), shell->positional_params[i]);
  }

  copy_string(shell->program_path, sizeof(shell->program_path), resolved);
  shell->positional_count = 0;
  if (positional_count > 0 && positional_args != NULL) {
    int n = positional_count < ARKSH_MAX_POSITIONAL_PARAMS ? positional_count : ARKSH_MAX_POSITIONAL_PARAMS;
    for (i = 0; i < n; ++i) {
      copy_string(shell->positional_params[i], sizeof(shell->positional_params[i]),
                  positional_args[i] != NULL ? positional_args[i] : "");
    }
    shell->positional_count = n;
  }

  {
    int source_status = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
      char line_output[ARKSH_MAX_OUTPUT];
      char parse_error[ARKSH_MAX_OUTPUT];
      size_t line_length;
      int parse_status;
      int exec_status;

      line_number++;
      line_length = strlen(line);
      if (line_length > 0 && line[line_length - 1] != '\n' && !feof(fp)) {
        snprintf(out, out_size, "%s:%zu: line too long", resolved, line_number);
        source_status = 1;
        break;
      }

      if (command_buffer[0] == '\0' && is_blank_or_comment_line(line)) {
        continue;
      }
      if (!pending_heredoc && is_blank_or_comment_line(line)) {
        continue;
      }

      if (command_buffer[0] == '\0') {
        command_start_line = line_number;
      }
      if (append_command_fragment(command_buffer, sizeof(command_buffer), line) != 0) {
        snprintf(out, out_size, "%s:%zu: command block too large", resolved, command_start_line);
        source_status = 1;
        break;
      }

      parse_status = command_requires_more_input(command_buffer, parse_error, sizeof(parse_error));
      if (parse_status > 0) {
        pending_heredoc = parse_error_is_unterminated_heredoc(parse_error);
        continue;
      }
      pending_heredoc = 0;
      if (parse_status < 0) {
        snprintf(out, out_size, "%s:%zu: %s", resolved, command_start_line, parse_error[0] == '\0' ? "parse error" : parse_error);
        source_status = 1;
        break;
      }

      line_output[0] = '\0';
      exec_status = arksh_shell_execute_line(shell, command_buffer, line_output, sizeof(line_output));
      if (exec_status != 0) {
        snprintf(out, out_size, "%s:%zu: %s", resolved, command_start_line, line_output[0] == '\0' ? "command failed" : line_output);
        source_status = 1;
        break;
      }

      if (append_output_line(out, out_size, line_output) != 0) {
        snprintf(out, out_size, "%s:%zu: sourced output too large", resolved, command_start_line);
        source_status = 1;
        break;
      }

      command_buffer[0] = '\0';
      command_start_line = 0;
    }

    if (source_status == 0 && command_buffer[0] != '\0') {
      char parse_error[ARKSH_MAX_OUTPUT];

      command_requires_more_input(command_buffer, parse_error, sizeof(parse_error));
      snprintf(out, out_size, "%s:%zu: %s", resolved, command_start_line, parse_error[0] == '\0' ? "incomplete command block" : parse_error);
      source_status = 1;
    }

    fclose(fp);

    /* Restore positional context. */
    copy_string(shell->program_path, sizeof(shell->program_path), saved_program_path);
    shell->positional_count = saved_positional_count;
    for (i = 0; i < saved_positional_count && i < ARKSH_MAX_POSITIONAL_PARAMS; ++i) {
      copy_string(shell->positional_params[i], sizeof(shell->positional_params[i]), saved_positional_params[i]);
    }

    return source_status;
  }
}

static int command_source(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  if (argc < 2) {
    snprintf(out, out_size, "usage: source <path> [arg ...]");
    return 1;
  }

  return arksh_shell_source_file(shell, argv[1], argc - 2, argv + 2, out, out_size);
}

static int command_type(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  int i;
  int status = 0;

  if (argc < 2) {
    snprintf(out, out_size, "usage: type <name> [name...]");
    return 1;
  }

  out[0] = '\0';
  for (i = 1; i < argc; ++i) {
    const char *alias_value = arksh_shell_get_alias(shell, argv[i]);
    const ArkshValue *binding_value = arksh_shell_get_binding(shell, argv[i]);
    const ArkshShellFunction *function_def = arksh_shell_find_function(shell, argv[i]);
    const ArkshClassDef *class_def = arksh_shell_find_class(shell, argv[i]);
    const ArkshCommandDef *command = find_registered_command(shell, argv[i]);
    char command_path[ARKSH_MAX_PATH];
    char line[ARKSH_MAX_OUTPUT];

    if (alias_value != NULL) {
      snprintf(line, sizeof(line), "%s is an alias for %s", argv[i], alias_value);
    } else if (binding_value != NULL) {
      snprintf(line, sizeof(line), "%s is a value binding of type %s", argv[i], arksh_value_kind_name(binding_value->kind));
    } else if (function_def != NULL) {
      snprintf(line, sizeof(line), "%s is a shell function", argv[i]);
    } else if (class_def != NULL) {
      snprintf(line, sizeof(line), "%s is a class", argv[i]);
    } else if (command != NULL) {
      if (command->is_plugin_command) {
        snprintf(line, sizeof(line), "%s is a plugin command", argv[i]);
      } else {
        const char *kind_label =
          (command->kind == ARKSH_BUILTIN_MUTANT) ? "mutant" :
          (command->kind == ARKSH_BUILTIN_MIXED)  ? "mixed"  : "pure";
        snprintf(line, sizeof(line), "%s is a %s shell builtin", argv[i], kind_label);
      }
    } else if (resolve_command_path(shell, argv[i], command_path, sizeof(command_path)) == 0) {
      snprintf(line, sizeof(line), "%s is %s", argv[i], command_path);
    } else {
      snprintf(line, sizeof(line), "%s not found", argv[i]);
      status = 1;
    }

    if (append_output_line(out, out_size, line) != 0) {
      snprintf(out, out_size, "type output too large");
      return 1;
    }
  }

  return status;
}

static int command_history(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  size_t i;

  (void) argc;
  (void) argv;

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  if (shell->history_count == 0) {
    copy_string(out, out_size, "history is empty");
    return 0;
  }

  for (i = 0; i < shell->history_count; ++i) {
    snprintf(
      out + strlen(out),
      out_size - strlen(out),
      "%s%4zu  %s",
      i == 0 ? "" : "\n",
      i + 1,
      shell->history[i]
    );
  }

  return 0;
}

static int command_perf(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  ArkshPerfCounters counters;
  const char *action = argc >= 2 ? argv[1] : "show";

  (void) shell;

  if (out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';

  if (strcmp(action, "show") == 0 || strcmp(action, "status") == 0) {
    arksh_perf_snapshot(&counters);
    snprintf(
      out,
      out_size,
      "enabled=%d\n"
      "malloc_calls=%llu\n"
      "calloc_calls=%llu\n"
      "realloc_calls=%llu\n"
      "free_calls=%llu\n"
      "malloc_bytes=%llu\n"
      "calloc_bytes=%llu\n"
      "realloc_bytes=%llu\n"
      "temp_buffer_calls=%llu\n"
      "temp_buffer_bytes=%llu\n"
      "value_copy_calls=%llu\n"
      "value_render_calls=%llu",
      counters.enabled,
      counters.malloc_calls,
      counters.calloc_calls,
      counters.realloc_calls,
      counters.free_calls,
      counters.malloc_bytes,
      counters.calloc_bytes,
      counters.realloc_bytes,
      counters.temp_buffer_calls,
      counters.temp_buffer_bytes,
      counters.value_copy_calls,
      counters.value_render_calls
    );
    return 0;
  }

  if (strcmp(action, "on") == 0 || strcmp(action, "enable") == 0) {
    arksh_perf_enable(1);
    return 0;
  }

  if (strcmp(action, "off") == 0 || strcmp(action, "disable") == 0) {
    arksh_perf_enable(0);
    return 0;
  }

  if (strcmp(action, "reset") == 0) {
    arksh_perf_reset();
    return 0;
  }

  snprintf(out, out_size, "usage: perf [show|status|on|off|reset]");
  return 1;
}

static int command_jobs(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  size_t i;

  (void) argc;
  (void) argv;

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  arksh_shell_refresh_jobs(shell);
  out[0] = '\0';

  if (shell->job_count == 0) {
    copy_string(out, out_size, "no background jobs");
    return 0;
  }

  {
    /* Determine which jobs get + and - markers (POSIX current/previous) */
    ArkshJob *current_job  = NULL;
    ArkshJob *previous_job = NULL;
    size_t j;

    for (j = shell->job_count; j > 0; --j) {
      ArkshJob *candidate = &shell->jobs[j - 1];
      if (candidate->state == ARKSH_JOB_DONE) continue;
      if (current_job == NULL) {
        current_job = candidate;
      } else if (previous_job == NULL) {
        previous_job = candidate;
        break;
      }
    }
    /* If no stopped job found yet, retry preferring stopped */
    if (current_job == NULL) {
      for (j = shell->job_count; j > 0; --j) {
        if (shell->jobs[j - 1].state == ARKSH_JOB_STOPPED) {
          current_job = &shell->jobs[j - 1];
          break;
        }
      }
    }

    for (i = 0; i < shell->job_count; ++i) {
      char line[ARKSH_MAX_OUTPUT];
      const char *status = "running";
      char detail[64];
      char marker;

      detail[0] = '\0';
      marker = ' ';
      if (&shell->jobs[i] == current_job)  marker = '+';
      else if (&shell->jobs[i] == previous_job) marker = '-';

      if (shell->jobs[i].state == ARKSH_JOB_STOPPED) {
        status = "stopped";
      } else if (shell->jobs[i].state == ARKSH_JOB_DONE) {
        status = "done";
        if (shell->jobs[i].termination_signal > 0) {
          snprintf(detail, sizeof(detail), " signal=%s", signal_name(shell->jobs[i].termination_signal));
        } else if (shell->jobs[i].exit_code != 0) {
          snprintf(detail, sizeof(detail), " exit=%d", shell->jobs[i].exit_code);
        }
      }

      snprintf(
        line,
        sizeof(line),
        "[%d]%c %s%s pid=%lld %s",
        shell->jobs[i].id,
        marker,
        status,
        detail,
        shell->jobs[i].process.pid,
        shell->jobs[i].command
      );

      if (append_output_line(out, out_size, line) != 0) {
        snprintf(out, out_size, "jobs output too large");
        return 1;
      }
    }
  }

  return 0;
}

static int command_fg(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  ArkshJob *job;
  int job_id = 0;
  int exit_code = 0;
  ArkshPlatformProcessState state = ARKSH_PLATFORM_PROCESS_UNCHANGED;
  size_t index;
  char error[ARKSH_MAX_OUTPUT];

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  arksh_shell_refresh_jobs(shell);
  if (argc >= 2) {
    if (parse_job_id(argv[1], &job_id) != 0) {
      snprintf(out, out_size, "invalid job id: %s", argv[1]);
      return 1;
    }
    job = find_job_by_id(shell, job_id);
  } else {
    job = find_default_job(shell);
  }

  if (job == NULL) {
    snprintf(out, out_size, "job not found");
    return 1;
  }

  if (job->state == ARKSH_JOB_DONE) {
    snprintf(out, out_size, "[%d] already completed %s", job->id, job->command);
    return job->exit_code == 0 ? 0 : 1;
  }

  index = (size_t) (job - shell->jobs);
  error[0] = '\0';
  if (arksh_platform_resume_background_process(&job->process, 1, error, sizeof(error)) != 0) {
    snprintf(out, out_size, "%s", error[0] == '\0' ? "unable to continue job" : error);
    return 1;
  }

  if (arksh_platform_wait_background_process(&job->process, 1, &state, &exit_code) != 0) {
    snprintf(out, out_size, "unable to wait for background job [%d]", job->id);
    return 1;
  }

  if (state == ARKSH_PLATFORM_PROCESS_STOPPED) {
    job->state = ARKSH_JOB_STOPPED;
    job->exit_code = exit_code;
    snprintf(out, out_size, "[%d] stopped %s", job->id, job->command);
    return 1;
  }

  job->state = ARKSH_JOB_DONE;
  job->exit_code = exit_code;
  out[0] = '\0';
  remove_job_at(shell, index);
  return exit_code == 0 ? 0 : 1;
}

static int command_bg(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  ArkshJob *job;
  int job_id = 0;

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  arksh_shell_refresh_jobs(shell);
  if (argc >= 2) {
    if (parse_job_id(argv[1], &job_id) != 0) {
      snprintf(out, out_size, "invalid job id: %s", argv[1]);
      return 1;
    }
    job = find_job_by_id(shell, job_id);
  } else {
    job = find_default_job(shell);
  }

  if (job == NULL) {
    snprintf(out, out_size, "job not found");
    return 1;
  }

  if (job->state == ARKSH_JOB_DONE) {
    snprintf(out, out_size, "[%d] already completed %s", job->id, job->command);
    return 1;
  }

  if (job->state == ARKSH_JOB_STOPPED) {
    char error[ARKSH_MAX_OUTPUT];

    error[0] = '\0';
    if (arksh_platform_resume_background_process(&job->process, 0, error, sizeof(error)) != 0) {
      snprintf(out, out_size, "%s", error[0] == '\0' ? "unable to continue job" : error);
      return 1;
    }
    job->state = ARKSH_JOB_RUNNING;
  }

  snprintf(out, out_size, "[%d] running pid=%lld %s", job->id, job->process.pid, job->command);
  return 0;
}

static int command_wait(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  int last_status = 0;
  int i;

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  arksh_shell_refresh_jobs(shell);
  out[0] = '\0';

  if (argc == 1) {
    size_t index = 0;

    if (shell->job_count == 0) {
      copy_string(out, out_size, "no background jobs");
      return 0;
    }

    while (index < shell->job_count) {
      char line[ARKSH_MAX_OUTPUT];
      size_t before = shell->job_count;

      line[0] = '\0';
      if (wait_for_job_at(shell, index, &last_status, line, sizeof(line)) != 0) {
        snprintf(out, out_size, "%s", line[0] == '\0' ? "wait failed" : line);
        return 1;
      }
      if (line[0] != '\0' && append_output_line(out, out_size, line) != 0) {
        snprintf(out, out_size, "wait output too large");
        return 1;
      }
      if (shell->job_count == before) {
        index++;
      }
    }

    return last_status;
  }

  for (i = 1; i < argc; ++i) {
    ArkshJob *job;
    int job_id = 0;
    char line[ARKSH_MAX_OUTPUT];
    size_t index;

    if (parse_job_id(argv[i], &job_id) != 0) {
      snprintf(out, out_size, "invalid job id: %s", argv[i]);
      return 1;
    }

    arksh_shell_refresh_jobs(shell);
    job = find_job_by_id(shell, job_id);
    if (job == NULL) {
      snprintf(out, out_size, "job not found");
      return 1;
    }

    index = (size_t) (job - shell->jobs);
    line[0] = '\0';
    if (wait_for_job_at(shell, index, &last_status, line, sizeof(line)) != 0) {
      snprintf(out, out_size, "%s", line[0] == '\0' ? "wait failed" : line);
      return 1;
    }
    if (line[0] != '\0' && append_output_line(out, out_size, line) != 0) {
      snprintf(out, out_size, "wait output too large");
      return 1;
    }
  }

  return last_status;
}

static int command_eval(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  char line[ARKSH_MAX_LINE];

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (argc <= 1) {
    out[0] = '\0';
    return 0;
  }

  if (join_arguments(argc, argv, 1, line, sizeof(line)) != 0) {
    snprintf(out, out_size, "eval input too large");
    return 1;
  }

  return arksh_shell_execute_line(shell, line, out, out_size);
}

static int command_exec(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  char resolved[ARKSH_MAX_PATH];
  int status;

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (argc < 2) {
    snprintf(out, out_size, "usage: exec <command> [args...]");
    return 1;
  }
  if (resolve_command_path(shell, argv[1], resolved, sizeof(resolved)) != 0) {
    snprintf(out, out_size, "command not found: %s", argv[1]);
    return 1;
  }

  status = arksh_execute_external_command(shell, argc - 1, argv + 1, out, out_size);
  shell->running = 0;
  return status;
}

/* E1-S6-T1: Full POSIX trap built-in. */
static int command_trap(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  int i, k;
  int print_mode = 0;

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }
  out[0] = '\0';

  /* trap -p [signal ...]: print trap settings. */
  if (argc >= 2 && strcmp(argv[1], "-p") == 0) {
    print_mode = 1;
  }

  /* trap (no args) or trap -p (no signals): print all active traps. */
  if (argc == 1 || (print_mode && argc == 2)) {
    char buf[ARKSH_MAX_OUTPUT];
    buf[0] = '\0';
    for (k = 0; s_trap_map[k].name != NULL; k++) {
      ArkshTrapKind kind = s_trap_map[k].kind;
      if (kind == ARKSH_TRAP_COUNT) {
        continue;
      }
      if (shell->traps[kind].active && shell->traps[kind].command[0] != '\0') {
        char line[ARKSH_MAX_LINE + 64];
        snprintf(line, sizeof(line), "trap -- '%s' %s\n",
                 shell->traps[kind].command, s_trap_map[k].name);
        if (strlen(buf) + strlen(line) < sizeof(buf) - 1) {
          strcat(buf, line);
        }
      }
    }
    if (buf[0] == '\0') {
      copy_string(out, out_size, "");
    } else {
      /* Remove trailing newline for cleaner output. */
      size_t len = strlen(buf);
      if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
      }
      copy_string(out, out_size, buf);
    }
    return 0;
  }

  /* trap -p signal ...: print specific traps. */
  if (print_mode) {
    char buf[ARKSH_MAX_OUTPUT];
    buf[0] = '\0';
    for (i = 2; i < argc; i++) {
      ArkshTrapKind kind = trap_name_to_kind(argv[i]);
      if (kind == ARKSH_TRAP_COUNT) {
        snprintf(out, out_size, "trap: invalid signal: %s", argv[i]);
        return 1;
      }
      if (shell->traps[kind].active && shell->traps[kind].command[0] != '\0') {
        char line[ARKSH_MAX_LINE + 64];
        snprintf(line, sizeof(line), "trap -- '%s' %s\n",
                 shell->traps[kind].command, argv[i]);
        if (strlen(buf) + strlen(line) < sizeof(buf) - 1) {
          strcat(buf, line);
        }
      }
    }
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
      buf[len - 1] = '\0';
    }
    copy_string(out, out_size, buf);
    return 0;
  }

  /* trap - signal ...: reset to default. */
  if (argc >= 2 && strcmp(argv[1], "-") == 0) {
    for (i = 2; i < argc; i++) {
      ArkshTrapKind kind = trap_name_to_kind(argv[i]);
      if (kind == ARKSH_TRAP_COUNT) {
        snprintf(out, out_size, "trap: invalid signal: %s", argv[i]);
        return 1;
      }
      shell->traps[kind].active = 0;
      shell->traps[kind].command[0] = '\0';
      install_trap_signal(kind, 0);
    }
    return 0;
  }

  /* trap '' signal ...: ignore signal. */
  /* trap '<cmd>' signal ...: set trap. */
  if (argc < 3) {
    snprintf(out, out_size, "usage: trap <command> <signal> [signal...] | trap - <signal>... | trap [-p]");
    return 1;
  }

  for (i = 2; i < argc; i++) {
    ArkshTrapKind kind = trap_name_to_kind(argv[i]);
    if (kind == ARKSH_TRAP_COUNT) {
      snprintf(out, out_size, "trap: invalid signal: %s", argv[i]);
      return 1;
    }
    copy_string(shell->traps[kind].command, sizeof(shell->traps[kind].command), argv[1]);
    shell->traps[kind].active = 1;
    install_trap_signal(kind, 1);
  }
  return 0;
}

static int command_true(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  (void) shell;
  (void) argc;
  (void) argv;
  if (out != NULL && out_size > 0) {
    out[0] = '\0';
  }
  return 0;
}

static int command_false(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  (void) shell;
  (void) argc;
  (void) argv;
  if (out != NULL && out_size > 0) {
    out[0] = '\0';
  }
  return 1;
}

static int command_break(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  char line[ARKSH_MAX_LINE];

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }
  if (argc > 2) {
    snprintf(out, out_size, "usage: break [count]");
    return 1;
  }

  copy_string(line, sizeof(line), "break");
  if (argc == 2) {
    if (append_text(line, sizeof(line), " ") != 0 || append_text(line, sizeof(line), argv[1]) != 0) {
      snprintf(out, out_size, "usage: break [count]");
      return 1;
    }
  }

  return handle_loop_control_line(shell, line, "break", ARKSH_CONTROL_SIGNAL_BREAK, out, out_size);
}

static int command_continue(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  char line[ARKSH_MAX_LINE];

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }
  if (argc > 2) {
    snprintf(out, out_size, "usage: continue [count]");
    return 1;
  }

  copy_string(line, sizeof(line), "continue");
  if (argc == 2) {
    if (append_text(line, sizeof(line), " ") != 0 || append_text(line, sizeof(line), argv[1]) != 0) {
      snprintf(out, out_size, "usage: continue [count]");
      return 1;
    }
  }

  return handle_loop_control_line(shell, line, "continue", ARKSH_CONTROL_SIGNAL_CONTINUE, out, out_size);
}

/* =========================================================================
   E1-S7-T6: shift [n] — shift positional parameters left by n (default 1)
   ========================================================================= */
static int command_shift(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  int n = 1;
  int i;

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';

  if (argc >= 2) {
    char *endptr = NULL;
    long val = strtol(argv[1], &endptr, 10);

    if (endptr == argv[1] || *endptr != '\0' || val < 0) {
      snprintf(out, out_size, "shift: invalid count: %s", argv[1]);
      return 1;
    }
    n = (int) val;
  }

  if (n > shell->positional_count) {
    snprintf(out, out_size, "shift: cannot shift: count exceeds number of positional parameters");
    return 1;
  }

  for (i = 0; i + n < shell->positional_count; i++) {
    copy_string(shell->positional_params[i], sizeof(shell->positional_params[i]), shell->positional_params[i + n]);
  }
  for (i = shell->positional_count - n; i < shell->positional_count; i++) {
    shell->positional_params[i][0] = '\0';
  }
  shell->positional_count -= n;
  return 0;
}

/* =========================================================================
   E1-S7-T7: local [var[=value] ...] — declare function-local variables
   ========================================================================= */
static int command_local(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  int i;
  char var_name[ARKSH_MAX_VAR_NAME];
  char var_value[ARKSH_MAX_VAR_VALUE];

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';

  if (argc == 1) {
    return 0; /* nothing to do */
  }

  for (i = 1; i < argc; i++) {
    /* Accept both `local NAME` and `local NAME=VALUE` */
    if (split_assignment(argv[i], var_name, sizeof(var_name), var_value, sizeof(var_value)) == 0) {
      /* NAME=VALUE form */
      if (!is_valid_identifier(var_name)) {
        snprintf(out, out_size, "local: invalid variable name: %s", var_name);
        return 1;
      }
      if (arksh_shell_set_var(shell, var_name, var_value, 0) != 0) {
        snprintf(out, out_size, "local: cannot set variable: %s", var_name);
        return 1;
      }
    } else {
      /* NAME-only form: declare as empty if not already set */
      copy_string(var_name, sizeof(var_name), argv[i]);
      if (!is_valid_identifier(var_name)) {
        snprintf(out, out_size, "local: invalid variable name: %s", var_name);
        return 1;
      }
      if (arksh_shell_get_var(shell, var_name) == NULL) {
        if (arksh_shell_set_var(shell, var_name, "", 0) != 0) {
          snprintf(out, out_size, "local: cannot declare variable: %s", var_name);
          return 1;
        }
      }
    }
  }
  return 0;
}

/* =========================================================================
   E1-S7-T8b: readonly [var[=value] ...] — mark variables as read-only
   ========================================================================= */
static int command_readonly(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  int i;
  char var_name[ARKSH_MAX_VAR_NAME];
  char var_value[ARKSH_MAX_VAR_VALUE];

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';

  for (i = 1; i < argc; i++) {
    if (split_assignment(argv[i], var_name, sizeof(var_name), var_value, sizeof(var_value)) == 0) {
      if (!is_valid_identifier(var_name)) {
        snprintf(out, out_size, "readonly: invalid variable name: %s", var_name);
        return 1;
      }
      if (arksh_shell_set_var(shell, var_name, var_value, 0) != 0) {
        snprintf(out, out_size, "readonly: cannot set variable: %s", var_name);
        return 1;
      }
    } else {
      copy_string(var_name, sizeof(var_name), argv[i]);
      if (!is_valid_identifier(var_name)) {
        snprintf(out, out_size, "readonly: invalid variable name: %s", var_name);
        return 1;
      }
    }
    /* Note: arksh does not yet have a read-only flag per variable;
       we set the value but do not enforce the restriction. */
  }
  return 0;
}

static size_t printf_process_escape(const char *s, char *out, size_t out_size);

/* =========================================================================
   E1-S7-T8c: echo [-n] [-e] [args...] — print arguments
   ========================================================================= */
static int command_echo(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  int newline = 1;
  int interpret_escapes = 0;
  int first_arg = 1;
  int i;
  char result[ARKSH_MAX_OUTPUT];
  size_t pos = 0;

  (void) shell;

  if (out == NULL || out_size == 0) {
    return 1;
  }

  /* Parse flags */
  while (first_arg < argc) {
    const char *a = argv[first_arg];

    if (a[0] != '-' || a[1] == '\0') {
      break;
    }
    {
      const char *f = a + 1;
      int valid = 1;

      while (*f != '\0') {
        if (*f == 'n') {
          newline = 0;
        } else if (*f == 'e') {
          interpret_escapes = 1;
        } else if (*f == 'E') {
          interpret_escapes = 0;
        } else {
          valid = 0;
          break;
        }
        f++;
      }
      if (!valid) {
        break;
      }
    }
    first_arg++;
  }

  result[0] = '\0';
  for (i = first_arg; i < argc; i++) {
    if (i > first_arg) {
      if (pos < sizeof(result) - 1) {
        result[pos++] = ' ';
      }
    }
    if (interpret_escapes) {
      pos += printf_process_escape(argv[i], result + pos, sizeof(result) - pos);
    } else {
      size_t len = strlen(argv[i]);

      if (pos + len >= sizeof(result)) {
        len = sizeof(result) - pos - 1;
      }
      memcpy(result + pos, argv[i], len);
      pos += len;
    }
  }
  if (newline && pos < sizeof(result) - 1) {
    result[pos++] = '\n';
  }
  result[pos] = '\0';

  /* Write to out buffer; the shell framework (write_buffer / print_output_if_any)
     handles printing to stdout, and the |> bridge captures via out_size. */
  copy_string(out, out_size, result);
  return 0;
}

static int command_return(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  char line[ARKSH_MAX_LINE];
  int index;

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  copy_string(line, sizeof(line), "return");
  for (index = 1; index < argc; ++index) {
    if (append_text(line, sizeof(line), " ") != 0 || append_text(line, sizeof(line), argv[index]) != 0) {
      snprintf(out, out_size, "return expression too long");
      return 1;
    }
  }

  return handle_return_line(shell, line, out, out_size);
}

static int register_builtin(ArkshShell *shell, const char *name, const char *description,
                             ArkshCommandFn fn, ArkshBuiltinKind kind);

/* E3-S5: builtin <name> [args...] — invoke a shell built-in directly,
   bypassing any user function that has the same name.  Useful inside
   override functions, e.g. `function cd(dir) do ... ; builtin cd $dir ; done`. */
static int command_builtin(ArkshShell *shell, int argc, char *argv[], char *out, size_t out_size) {
  size_t i;

  if (argc < 2) {
    snprintf(out, out_size, "usage: builtin <command> [args...]");
    return 1;
  }

  for (i = 0; i < shell->command_count; ++i) {
    if (strcmp(shell->commands[i].name, argv[1]) == 0) {
      /* Call the built-in with argv shifted: argv[1] becomes argv[0]. */
      return shell->commands[i].fn(shell, argc - 1, argv + 1, out, out_size);
    }
  }

  snprintf(out, out_size, "builtin: %s: not a shell built-in", argv[1]);
  return 1;
}

/* =========================================================================
   E1-S6-T3: read built-in
   ========================================================================= */
static int command_read(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  int raw_mode = 0;
  int nchars = -1;
  const char *prompt_str = NULL;
  int i;
  int var_start;
  char line[ARKSH_MAX_LINE];
  size_t line_len = 0;
  int c;

  (void) out;
  (void) out_size;

  if (shell == NULL) {
    return 1;
  }

  /* Parse options. */
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-r") == 0) {
      raw_mode = 1;
    } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
      i++;
      prompt_str = argv[i];
    } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
      i++;
      nchars = atoi(argv[i]);
    } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
      /* timeout: not supported on all platforms, skip */
      i++;
    } else if (argv[i][0] == '-') {
      snprintf(out, out_size, "read: unknown option: %s", argv[i]);
      return 1;
    } else {
      break;
    }
  }
  var_start = i;

  if (var_start >= argc) {
    snprintf(out, out_size, "read: missing variable name");
    return 1;
  }

  if (prompt_str != NULL) {
    fputs(prompt_str, stderr);
    fflush(stderr);
  }

  /* Read characters from stdin. */
  line[0] = '\0';
  while (1) {
    c = fgetc(stdin);
    if (c == EOF || c == '\n') {
      break;
    }
    if (nchars > 0 && (int) line_len >= nchars) {
      break;
    }
    if (!raw_mode && c == '\\') {
      int next = fgetc(stdin);
      if (next == '\n') {
        /* line continuation */
        continue;
      }
      if (next != EOF) {
        if (line_len + 1 < sizeof(line) - 1) {
          line[line_len++] = (char) next;
        }
        continue;
      }
    }
    if (line_len + 1 < sizeof(line) - 1) {
      line[line_len++] = (char) c;
    }
  }
  line[line_len] = '\0';

  /* Split line on IFS and assign to variables. */
  {
    const char *ifs_val = arksh_shell_get_var(shell, "IFS");
    const char *ifs = (ifs_val != NULL) ? ifs_val : " \t\n";
    char *p = line;
    int n_vars = argc - var_start;

    for (i = var_start; i < argc; i++) {
      char field[ARKSH_MAX_VAR_VALUE];
      int is_last = (i == argc - 1);

      if (is_last) {
        /* Last variable gets the remainder. */
        copy_string(field, sizeof(field), p);
      } else {
        /* Split on IFS. */
        size_t span = strcspn(p, ifs);
        if (span >= sizeof(field)) {
          span = sizeof(field) - 1;
        }
        memcpy(field, p, span);
        field[span] = '\0';
        p += span;
        /* Skip IFS chars. */
        p += strspn(p, ifs);
      }
      arksh_shell_set_var(shell, argv[i], field, 0);
      (void) n_vars;
    }
  }

  if (out != NULL && out_size > 0) {
    out[0] = '\0';
  }
  return (c == EOF && line_len == 0) ? 1 : 0;
}

/* =========================================================================
   E1-S6-T4: printf built-in
   ========================================================================= */

/* Process printf escape sequences in format string token. */
static size_t printf_process_escape(const char *s, char *out, size_t out_size) {
  size_t pos = 0;
  while (*s != '\0') {
    if (*s != '\\') {
      if (pos < out_size - 1) {
        out[pos++] = *s;
      }
      s++;
      continue;
    }
    s++; /* skip backslash */
    switch (*s) {
      case 'a': if (pos < out_size - 1) out[pos++] = '\a'; s++; break;
      case 'b': if (pos < out_size - 1) out[pos++] = '\b'; s++; break;
      case 'f': if (pos < out_size - 1) out[pos++] = '\f'; s++; break;
      case 'n': if (pos < out_size - 1) out[pos++] = '\n'; s++; break;
      case 'r': if (pos < out_size - 1) out[pos++] = '\r'; s++; break;
      case 't': if (pos < out_size - 1) out[pos++] = '\t'; s++; break;
      case 'v': if (pos < out_size - 1) out[pos++] = '\v'; s++; break;
      case '\\': if (pos < out_size - 1) out[pos++] = '\\'; s++; break;
      case '\'': if (pos < out_size - 1) out[pos++] = '\''; s++; break;
      case '"':  if (pos < out_size - 1) out[pos++] = '"';  s++; break;
      case '0': {
        /* Octal escape: \0NNN */
        unsigned int val = 0;
        int k = 0;
        s++;
        while (k < 3 && *s >= '0' && *s <= '7') {
          val = val * 8 + (unsigned int)(*s - '0');
          s++;
          k++;
        }
        if (pos < out_size - 1) {
          out[pos++] = (char) val;
        }
        break;
      }
      default:
        if (pos < out_size - 1) out[pos++] = '\\';
        if (pos < out_size - 1 && *s != '\0') out[pos++] = *s;
        if (*s != '\0') s++;
        break;
    }
  }
  if (out_size > 0) {
    out[pos] = '\0';
  }
  return pos;
}

static int command_printf(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  const char *fmt;
  int arg_idx;
  char result[ARKSH_MAX_OUTPUT];
  size_t res_pos = 0;

  (void) shell;

  if (argc < 2) {
    snprintf(out, out_size, "printf: missing format string");
    return 1;
  }

  fmt = argv[1];
  arg_idx = 2;
  result[0] = '\0';

  /* Walk format string. */
  while (*fmt != '\0' && res_pos < sizeof(result) - 1) {
    if (*fmt == '\\') {
      char esc_buf[8];
      char esc_in[4];
      fmt++;
      esc_in[0] = '\\';
      esc_in[1] = *fmt ? *fmt : '\0';
      esc_in[2] = '\0';
      size_t wrote = printf_process_escape(esc_in, esc_buf, sizeof(esc_buf));
      /* Handle octal specially */
      if (*fmt == '0') {
        char octal_str[8];
        size_t oi = 0;
        octal_str[oi++] = '\\';
        octal_str[oi++] = *fmt;
        fmt++;
        while (oi < 4 && *fmt >= '0' && *fmt <= '7') {
          octal_str[oi++] = *fmt++;
        }
        octal_str[oi] = '\0';
        wrote = printf_process_escape(octal_str, esc_buf, sizeof(esc_buf));
        for (size_t wi = 0; wi < wrote && res_pos < sizeof(result) - 1; wi++) {
          result[res_pos++] = esc_buf[wi];
        }
      } else {
        if (*fmt != '\0') {
          fmt++;
        }
        for (size_t wi = 0; wi < wrote && res_pos < sizeof(result) - 1; wi++) {
          result[res_pos++] = esc_buf[wi];
        }
      }
      continue;
    }

    if (*fmt != '%') {
      result[res_pos++] = *fmt++;
      continue;
    }

    /* Format specifier. */
    fmt++; /* skip '%' */
    if (*fmt == '%') {
      result[res_pos++] = '%';
      fmt++;
      continue;
    }

    /* Collect flags/width/precision into a mini-format. */
    char spec[64];
    size_t si = 0;
    spec[si++] = '%';
    /* Flags */
    while (*fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '0' || *fmt == '#') {
      if (si < sizeof(spec) - 3) spec[si++] = *fmt;
      fmt++;
    }
    /* Width */
    while (*fmt >= '0' && *fmt <= '9') {
      if (si < sizeof(spec) - 3) spec[si++] = *fmt;
      fmt++;
    }
    /* Precision */
    if (*fmt == '.') {
      if (si < sizeof(spec) - 3) spec[si++] = *fmt;
      fmt++;
      while (*fmt >= '0' && *fmt <= '9') {
        if (si < sizeof(spec) - 3) spec[si++] = *fmt;
        fmt++;
      }
    }

    char conv = *fmt;
    if (conv != '\0') {
      fmt++;
    }
    spec[si++] = conv;
    spec[si] = '\0';

    const char *arg_str = (arg_idx < argc) ? argv[arg_idx++] : "";

    char piece[ARKSH_MAX_OUTPUT / 4];
    switch (conv) {
      case 's':
        snprintf(piece, sizeof(piece), spec, arg_str);
        break;
      case 'b': {
        /* %b: like %s but process escape sequences in the argument. */
        char esc[ARKSH_MAX_OUTPUT / 4];
        printf_process_escape(arg_str, esc, sizeof(esc));
        /* Replace spec's 'b' with 's'. */
        spec[si - 1] = 's';
        snprintf(piece, sizeof(piece), spec, esc);
        break;
      }
      case 'd': case 'i':
        snprintf(piece, sizeof(piece), spec, (int) strtol(arg_str, NULL, 0));
        break;
      case 'u':
        snprintf(piece, sizeof(piece), spec, (unsigned int) strtoul(arg_str, NULL, 0));
        break;
      case 'o':
        snprintf(piece, sizeof(piece), spec, (unsigned int) strtoul(arg_str, NULL, 0));
        break;
      case 'x': case 'X':
        snprintf(piece, sizeof(piece), spec, (unsigned int) strtoul(arg_str, NULL, 0));
        break;
      case 'f': case 'e': case 'E': case 'g': case 'G':
        snprintf(piece, sizeof(piece), spec, strtod(arg_str, NULL));
        break;
      case 'c':
        piece[0] = arg_str[0];
        piece[1] = '\0';
        break;
      default:
        piece[0] = conv;
        piece[1] = '\0';
        break;
    }

    size_t piece_len = strlen(piece);
    if (res_pos + piece_len < sizeof(result) - 1) {
      memcpy(result + res_pos, piece, piece_len);
      res_pos += piece_len;
    }
  }
  result[res_pos] = '\0';

  /* Write to stdout directly (printf outputs to stdout, not via `out`). */
  fputs(result, stdout);
  fflush(stdout);

  if (out != NULL && out_size > 0) {
    out[0] = '\0';
  }
  return 0;
}

/* =========================================================================
   E1-S6-T6: getopts built-in
   ========================================================================= */
static int command_getopts(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  const char *optstring;
  const char *varname;
  const char *optind_str;
  int optind_val;
  int args_start;
  int nargs;
  const char *arg;
  char ch;
  char optarg_buf[ARKSH_MAX_VAR_VALUE];
  char optind_new[32];

  if (argc < 3) {
    snprintf(out, out_size, "getopts: usage: getopts optstring name [args]");
    return 1;
  }

  optstring = argv[1];
  varname   = argv[2];
  args_start = 3; /* use argv[3..] or positional params if not provided */

  /* Get current OPTIND (default 1). */
  optind_str = arksh_shell_get_var(shell, "OPTIND");
  optind_val = (optind_str != NULL && optind_str[0] != '\0') ? atoi(optind_str) : 1;
  if (optind_val < 1) {
    optind_val = 1;
  }

  /* Build the argument list to scan. */
  if (argc > args_start) {
    nargs = argc - args_start;
  } else {
    /* Use positional parameters. */
    nargs = shell->positional_count;
    args_start = -1; /* signal: use positional */
  }

  /* Find argument at OPTIND. */
  {
    int arg_index = optind_val; /* 1-based */
    if (arg_index > nargs) {
      /* No more options. */
      arksh_shell_set_var(shell, varname, "?", 0);
      if (out != NULL && out_size > 0) out[0] = '\0';
      return 1;
    }

    arg = (args_start >= 0) ? argv[args_start + arg_index - 1]
                             : shell->positional_params[arg_index - 1];
  }

  /* Check if we're past options. */
  if (arg[0] != '-' || arg[1] == '\0' || strcmp(arg, "--") == 0) {
    arksh_shell_set_var(shell, varname, "?", 0);
    if (out != NULL && out_size > 0) out[0] = '\0';
    return 1;
  }

  ch = arg[1];

  /* Look for ch in optstring. */
  {
    const char *p = optstring;
    int silent = (p[0] == ':');
    if (silent) p++;

    const char *found = strchr(p, (int) ch);
    if (found == NULL) {
      /* Unknown option. */
      char ch_str[2] = { ch, '\0' };
      arksh_shell_set_var(shell, varname, silent ? "?" : "?", 0);
      if (!silent) {
        snprintf(out, out_size, "getopts: illegal option -- %c", ch);
      } else {
        arksh_shell_set_var(shell, "OPTARG", ch_str, 0);
        if (out != NULL && out_size > 0) out[0] = '\0';
      }
      /* Advance OPTIND. */
      snprintf(optind_new, sizeof(optind_new), "%d", optind_val + 1);
      arksh_shell_set_var(shell, "OPTIND", optind_new, 0);
      return 0;
    }

    /* Does option take an argument? */
    if (*(found + 1) == ':') {
      /* Need an argument. */
      if (arg[2] != '\0') {
        /* Argument is concatenated. */
        copy_string(optarg_buf, sizeof(optarg_buf), arg + 2);
        snprintf(optind_new, sizeof(optind_new), "%d", optind_val + 1);
      } else {
        /* Argument is next item. */
        int next_idx = optind_val + 1;
        const char *next_arg;
        if (next_idx > nargs) {
          if (silent) {
            arksh_shell_set_var(shell, varname, ":", 0);
          } else {
            arksh_shell_set_var(shell, varname, "?", 0);
            snprintf(out, out_size, "getopts: option requires an argument -- %c", ch);
          }
          snprintf(optind_new, sizeof(optind_new), "%d", next_idx);
          arksh_shell_set_var(shell, "OPTIND", optind_new, 0);
          return 0;
        }
        next_arg = (args_start >= 0) ? argv[args_start + next_idx - 1]
                                     : shell->positional_params[next_idx - 1];
        copy_string(optarg_buf, sizeof(optarg_buf), next_arg);
        snprintf(optind_new, sizeof(optind_new), "%d", next_idx + 1);
      }
      arksh_shell_set_var(shell, "OPTARG", optarg_buf, 0);
    } else {
      /* No argument. */
      arksh_shell_set_var(shell, "OPTARG", "", 0);
      snprintf(optind_new, sizeof(optind_new), "%d", optind_val + 1);
    }
  }

  {
    char ch_str[2] = { ch, '\0' };
    arksh_shell_set_var(shell, varname, ch_str, 0);
  }
  arksh_shell_set_var(shell, "OPTIND", optind_new, 0);

  if (out != NULL && out_size > 0) {
    out[0] = '\0';
  }
  return 0;
}

/* =========================================================================
   E1-S6-T7: test / [ built-in
   ========================================================================= */

/* Forward declaration. */
static int test_eval(int *argc_ptr, char ***argv_ptr, char *err, size_t err_size);

static int test_primary(int argc, char **argv, char *err, size_t err_size) {
  (void) err;
  (void) err_size;

  if (argc == 0) {
    return 1; /* false */
  }

  if (argc == 1) {
    /* Non-empty string → true. */
    return (argv[0][0] != '\0') ? 0 : 1;
  }

  if (argc >= 2 && strcmp(argv[0], "!") == 0) {
    int rest_argc = argc - 1;
    char **rest_argv = argv + 1;
    return test_eval(&rest_argc, &rest_argv, err, err_size) == 0 ? 1 : 0;
  }

  if (argc == 2) {
    const char *op  = argv[0];
    const char *arg = argv[1];

    /* Unary file tests. */
#ifndef _WIN32
    if (strcmp(op, "-e") == 0) {
      struct stat st;
      return stat(arg, &st) == 0 ? 0 : 1;
    }
    if (strcmp(op, "-f") == 0) {
      struct stat st;
      return (stat(arg, &st) == 0 && S_ISREG(st.st_mode)) ? 0 : 1;
    }
    if (strcmp(op, "-d") == 0) {
      struct stat st;
      return (stat(arg, &st) == 0 && S_ISDIR(st.st_mode)) ? 0 : 1;
    }
    if (strcmp(op, "-L") == 0 || strcmp(op, "-h") == 0) {
      struct stat st;
      return (lstat(arg, &st) == 0 && S_ISLNK(st.st_mode)) ? 0 : 1;
    }
    if (strcmp(op, "-r") == 0) {
      return access(arg, R_OK) == 0 ? 0 : 1;
    }
    if (strcmp(op, "-w") == 0) {
      return access(arg, W_OK) == 0 ? 0 : 1;
    }
    if (strcmp(op, "-x") == 0) {
      return access(arg, X_OK) == 0 ? 0 : 1;
    }
    if (strcmp(op, "-s") == 0) {
      struct stat st;
      return (stat(arg, &st) == 0 && st.st_size > 0) ? 0 : 1;
    }
    if (strcmp(op, "-b") == 0) {
      struct stat st;
      return (stat(arg, &st) == 0 && S_ISBLK(st.st_mode)) ? 0 : 1;
    }
    if (strcmp(op, "-c") == 0) {
      struct stat st;
      return (stat(arg, &st) == 0 && S_ISCHR(st.st_mode)) ? 0 : 1;
    }
    if (strcmp(op, "-p") == 0) {
      struct stat st;
      return (stat(arg, &st) == 0 && S_ISFIFO(st.st_mode)) ? 0 : 1;
    }
    if (strcmp(op, "-S") == 0) {
      struct stat st;
      return (stat(arg, &st) == 0 && S_ISSOCK(st.st_mode)) ? 0 : 1;
    }
    if (strcmp(op, "-t") == 0) {
      return isatty(atoi(arg)) ? 0 : 1;
    }
#else
    if (strcmp(op, "-e") == 0 || strcmp(op, "-f") == 0 || strcmp(op, "-d") == 0) {
      FILE *f = fopen(arg, "r");
      if (f != NULL) { fclose(f); return 0; }
      return 1;
    }
    if (strcmp(op, "-r") == 0 || strcmp(op, "-w") == 0 || strcmp(op, "-x") == 0) {
      FILE *f = fopen(arg, "r");
      if (f != NULL) { fclose(f); return 0; }
      return 1;
    }
    if (strcmp(op, "-t") == 0) {
      return 1;
    }
#endif
    /* Unary string tests. */
    if (strcmp(op, "-n") == 0) {
      return (arg[0] != '\0') ? 0 : 1;
    }
    if (strcmp(op, "-z") == 0) {
      return (arg[0] == '\0') ? 0 : 1;
    }
  }

  if (argc == 3) {
    const char *lhs = argv[0];
    const char *op  = argv[1];
    const char *rhs = argv[2];

    /* String comparisons. */
    if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) {
      return strcmp(lhs, rhs) == 0 ? 0 : 1;
    }
    if (strcmp(op, "!=") == 0) {
      return strcmp(lhs, rhs) != 0 ? 0 : 1;
    }
    if (strcmp(op, "<") == 0) {
      return strcmp(lhs, rhs) < 0 ? 0 : 1;
    }
    if (strcmp(op, ">") == 0) {
      return strcmp(lhs, rhs) > 0 ? 0 : 1;
    }

    /* Numeric comparisons. */
    if (strcmp(op, "-eq") == 0) {
      return strtol(lhs, NULL, 10) == strtol(rhs, NULL, 10) ? 0 : 1;
    }
    if (strcmp(op, "-ne") == 0) {
      return strtol(lhs, NULL, 10) != strtol(rhs, NULL, 10) ? 0 : 1;
    }
    if (strcmp(op, "-lt") == 0) {
      return strtol(lhs, NULL, 10) <  strtol(rhs, NULL, 10) ? 0 : 1;
    }
    if (strcmp(op, "-le") == 0) {
      return strtol(lhs, NULL, 10) <= strtol(rhs, NULL, 10) ? 0 : 1;
    }
    if (strcmp(op, "-gt") == 0) {
      return strtol(lhs, NULL, 10) >  strtol(rhs, NULL, 10) ? 0 : 1;
    }
    if (strcmp(op, "-ge") == 0) {
      return strtol(lhs, NULL, 10) >= strtol(rhs, NULL, 10) ? 0 : 1;
    }

#ifndef _WIN32
    /* File comparisons. */
    if (strcmp(op, "-nt") == 0) {
      struct stat s1, s2;
      if (stat(lhs, &s1) != 0 || stat(rhs, &s2) != 0) return 1;
      return s1.st_mtime > s2.st_mtime ? 0 : 1;
    }
    if (strcmp(op, "-ot") == 0) {
      struct stat s1, s2;
      if (stat(lhs, &s1) != 0 || stat(rhs, &s2) != 0) return 1;
      return s1.st_mtime < s2.st_mtime ? 0 : 1;
    }
    if (strcmp(op, "-ef") == 0) {
      struct stat s1, s2;
      if (stat(lhs, &s1) != 0 || stat(rhs, &s2) != 0) return 1;
      return (s1.st_dev == s2.st_dev && s1.st_ino == s2.st_ino) ? 0 : 1;
    }
#endif
  }

  return 1; /* default: false */
}

/* Simple recursive descent evaluator for test expressions. */
static int test_eval_or(int *argc_ptr, char ***argv_ptr, char *err, size_t err_size);
static int test_eval_and(int *argc_ptr, char ***argv_ptr, char *err, size_t err_size);
static int test_eval_not(int *argc_ptr, char ***argv_ptr, char *err, size_t err_size);

static int test_eval(int *argc_ptr, char ***argv_ptr, char *err, size_t err_size) {
  return test_eval_or(argc_ptr, argv_ptr, err, err_size);
}

static int test_eval_or(int *argc_ptr, char ***argv_ptr, char *err, size_t err_size) {
  int result = test_eval_and(argc_ptr, argv_ptr, err, err_size);
  while (*argc_ptr >= 1 && strcmp((*argv_ptr)[0], "-o") == 0) {
    (*argc_ptr)--;
    (*argv_ptr)++;
    int right = test_eval_and(argc_ptr, argv_ptr, err, err_size);
    result = (result == 0 || right == 0) ? 0 : 1;
  }
  return result;
}

static int test_eval_and(int *argc_ptr, char ***argv_ptr, char *err, size_t err_size) {
  int result = test_eval_not(argc_ptr, argv_ptr, err, err_size);
  while (*argc_ptr >= 1 && strcmp((*argv_ptr)[0], "-a") == 0) {
    (*argc_ptr)--;
    (*argv_ptr)++;
    int right = test_eval_not(argc_ptr, argv_ptr, err, err_size);
    result = (result == 0 && right == 0) ? 0 : 1;
  }
  return result;
}

static int test_eval_not(int *argc_ptr, char ***argv_ptr, char *err, size_t err_size) {
  if (*argc_ptr >= 1 && strcmp((*argv_ptr)[0], "!") == 0) {
    (*argc_ptr)--;
    (*argv_ptr)++;
    int val = test_eval_not(argc_ptr, argv_ptr, err, err_size);
    return val == 0 ? 1 : 0;
  }
  /* Collect args for primary until we hit -a, -o, or run out. */
  char *prim_argv[16];
  int prim_argc = 0;
  while (*argc_ptr > 0 &&
         strcmp((*argv_ptr)[0], "-a") != 0 &&
         strcmp((*argv_ptr)[0], "-o") != 0 &&
         prim_argc < 15) {
    /* Look-ahead: stop before a binary operator at position 2 (e.g. a -eq b). */
    if (prim_argc == 1 && *argc_ptr >= 3) {
      const char *maybe_op = (*argv_ptr)[1];
      if (strcmp(maybe_op, "-eq") == 0 || strcmp(maybe_op, "-ne") == 0 ||
          strcmp(maybe_op, "-lt") == 0 || strcmp(maybe_op, "-le") == 0 ||
          strcmp(maybe_op, "-gt") == 0 || strcmp(maybe_op, "-ge") == 0 ||
          strcmp(maybe_op, "=")   == 0 || strcmp(maybe_op, "!=")  == 0 ||
          strcmp(maybe_op, "<")   == 0 || strcmp(maybe_op, ">")   == 0 ||
          strcmp(maybe_op, "-nt") == 0 || strcmp(maybe_op, "-ot") == 0 ||
          strcmp(maybe_op, "-ef") == 0) {
        /* Consume 2 more args (op + rhs). */
        prim_argv[prim_argc++] = (*argv_ptr)[0];
        prim_argv[prim_argc++] = (*argv_ptr)[1];
        prim_argv[prim_argc++] = (*argv_ptr)[2];
        *argc_ptr -= 3;
        *argv_ptr += 3;
        break;
      }
    }
    prim_argv[prim_argc++] = (*argv_ptr)[0];
    (*argc_ptr)--;
    (*argv_ptr)++;
  }
  return test_primary(prim_argc, prim_argv, err, err_size);
}

static int command_test(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  int eval_argc;
  char **eval_argv;
  int result;
  char err[256];

  (void) shell;
  if (out != NULL && out_size > 0) {
    out[0] = '\0';
  }

  /* argv[0] = "test", args start at 1. */
  eval_argc = argc - 1;
  eval_argv = argv + 1;
  err[0] = '\0';

  if (eval_argc == 0) {
    return 1; /* false */
  }

  result = test_eval(&eval_argc, &eval_argv, err, sizeof(err));
  if (err[0] != '\0' && out != NULL && out_size > 0) {
    copy_string(out, out_size, err);
  }
  return result;
}

static int command_lbracket(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  /* [ expr ] — must end with ]. */
  if (argc < 2 || strcmp(argv[argc - 1], "]") != 0) {
    if (out != NULL && out_size > 0) {
      snprintf(out, out_size, "[: missing closing ]");
    }
    return 2;
  }
  /* Call command_test with argc-1 (strip ]). */
  return command_test(shell, argc - 1, argv, out, out_size);
}

static int register_builtin_commands(ArkshShell *shell) {
  /* --- PURE: read-only, never modifies shell state ----------------------- */
  if (register_builtin(shell, "help",      "show commands and expression syntax",       command_help,    ARKSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "pwd",       "print current directory",                   command_pwd,     ARKSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "type",      "show how a command name resolves",          command_type,    ARKSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "history",   "show interactive command history",          command_history, ARKSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "jobs",      "list background jobs with state and pid",   command_jobs,    ARKSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "true",      "return success",                            command_true,    ARKSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "false",     "return failure",                            command_false,   ARKSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "inspect",   "print object metadata for a path",         command_inspect, ARKSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "get",       "read an object property",                  command_get,     ARKSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "call",      "invoke an object method",                  command_call,    ARKSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "run",       "execute an external command natively",     command_run,     ARKSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "function",  "list or inspect shell functions",          command_function, ARKSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "functions", "list or inspect shell functions",          command_function, ARKSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "class",     "list defined classes",                     command_class,   ARKSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "classes",   "list or inspect defined classes",          command_class,   ARKSH_BUILTIN_PURE) != 0) {
    return 1;
  }

  /* --- MUTANT: must run in the current shell process --------------------- */
  if (register_builtin(shell, "builtin",  "invoke a built-in directly, bypassing function overrides", command_builtin, ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "exit",     "terminate the shell",                            command_exit,     ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "quit",     "terminate the shell",                            command_exit,     ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "cd",       "change current directory",                       command_cd,       ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "set",      "list or assign shell variables",                 command_set,      ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "export",   "mark a shell variable for child processes",      command_export,   ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "unset",    "remove shell variables",                         command_unset,    ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "unalias",  "remove aliases",                                 command_unalias,  ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "source",   "execute commands from a file",                   command_source,   ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, ".",        "execute commands from a file",                   command_source,   ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "eval",     "evaluate a command string in the current shell", command_eval,     ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "exec",     "run an external command and terminate the current shell", command_exec, ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "trap",     "list or define shell exit traps",                command_trap,     ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "break",    "exit the current loop or an outer loop",         command_break,    ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "continue", "skip to the next loop iteration",                command_continue, ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "return",   "exit the current shell function",                command_return,   ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "bg",       "resume a stopped background job",                command_bg,       ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "fg",       "resume or wait for a background job in the foreground", command_fg, ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "wait",     "wait for background jobs to complete",           command_wait,     ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "read",     "read a line from stdin into variables",          command_read,     ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "getopts",  "parse option flags from positional arguments",   command_getopts,  ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "shift",    "shift positional parameters left by n",          command_shift,    ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "local",    "declare function-local variables",               command_local,    ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "readonly", "mark variables as read-only",                    command_readonly, ARKSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "echo",     "print arguments to standard output",             command_echo,     ARKSH_BUILTIN_MUTANT) != 0) {
    return 1;
  }

  /* --- PURE: new POSIX commands (no shell state modification) ------------ */
  if (register_builtin(shell, "printf",   "format and print arguments",                    command_printf,   ARKSH_BUILTIN_PURE)   != 0 ||
      register_builtin(shell, "test",     "evaluate a conditional expression",             command_test,     ARKSH_BUILTIN_PURE)   != 0 ||
      register_builtin(shell, "[",        "evaluate a conditional expression",             command_lbracket, ARKSH_BUILTIN_PURE)   != 0) {
    return 1;
  }

  /* --- MIXED: read-only when listing, mutating when defining/loading ----- */
  if (register_builtin(shell, "alias",   "list or define aliases",                    command_alias,   ARKSH_BUILTIN_MIXED) != 0 ||
      register_builtin(shell, "extend",  "list or define object/value extensions",    command_extend,  ARKSH_BUILTIN_MIXED) != 0 ||
      register_builtin(shell, "let",     "list or create typed value bindings",       command_let,     ARKSH_BUILTIN_MIXED) != 0 ||
      register_builtin(shell, "perf",    "show or control lightweight core counters", command_perf,    ARKSH_BUILTIN_MIXED) != 0 ||
      register_builtin(shell, "plugin",  "load, inspect or toggle plugins",           command_plugin,  ARKSH_BUILTIN_MIXED) != 0 ||
      register_builtin(shell, "prompt",  "show or load prompt configuration",        command_prompt,  ARKSH_BUILTIN_MIXED) != 0) {
    return 1;
  }

  return 0;
}

/* ===== E6-S8: Matrix type — helpers, properties, methods, resolver ===== */

/* Helper: convert ArkshValue to matrix cell. */
static void matrix_cell_set(ArkshMatrixCell *cell, const ArkshValue *v) {
  if (v == NULL || v->kind == ARKSH_VALUE_EMPTY) {
    memset(cell, 0, sizeof(*cell));
    cell->kind = ARKSH_VALUE_EMPTY;
    return;
  }
  cell->kind = v->kind;
  cell->number = v->number;
  cell->boolean = v->boolean;
  copy_string(cell->text, sizeof(cell->text), v->text);
}

/* Helper: convert matrix cell to ArkshValue. */
static void matrix_cell_get(const ArkshMatrixCell *cell, ArkshValue *out) {
  arksh_value_init(out);
  if (cell == NULL) return;
  switch (cell->kind) {
    case ARKSH_VALUE_NUMBER:
    case ARKSH_VALUE_INTEGER:
    case ARKSH_VALUE_FLOAT:
    case ARKSH_VALUE_DOUBLE:
    case ARKSH_VALUE_IMAGINARY:
      out->kind = cell->kind;
      out->number = cell->number;
      break;
    case ARKSH_VALUE_BOOLEAN:
      arksh_value_set_boolean(out, cell->boolean);
      break;
    case ARKSH_VALUE_STRING:
      arksh_value_set_string(out, cell->text);
      break;
    default:
      arksh_value_set_string(out, cell->text);
      break;
  }
}

/* Helper: format cell to text. */
static void matrix_cell_format(const ArkshMatrixCell *cell, char *out, size_t out_size) {
  if (cell == NULL || cell->kind == ARKSH_VALUE_EMPTY) {
    copy_string(out, out_size, "");
    return;
  }
  if (cell->kind == ARKSH_VALUE_NUMBER ||
      cell->kind == ARKSH_VALUE_INTEGER ||
      cell->kind == ARKSH_VALUE_FLOAT ||
      cell->kind == ARKSH_VALUE_DOUBLE) {
    snprintf(out, out_size, "%.6g", cell->number);
  } else if (cell->kind == ARKSH_VALUE_BOOLEAN) {
    copy_string(out, out_size, cell->boolean ? "true" : "false");
  } else {
    copy_string(out, out_size, cell->text);
  }
}

/* Helper: find column index by name, -1 if not found. */
static int matrix_find_col(const ArkshMatrix *m, const char *name) {
  size_t i;
  if (m == NULL || name == NULL) return -1;
  for (i = 0; i < m->col_count; ++i) {
    if (strcmp(m->col_names[i], name) == 0) return (int)i;
  }
  return -1;
}

/* Helper: compare cell against ArkshValue using operator string. */
static int matrix_cell_compare(const ArkshMatrixCell *cell, const char *op, const ArkshValue *val) {
  int cmp;
  /* Numeric comparison if cell and val are numeric */
  if ((cell->kind == ARKSH_VALUE_NUMBER || cell->kind == ARKSH_VALUE_INTEGER ||
       cell->kind == ARKSH_VALUE_FLOAT || cell->kind == ARKSH_VALUE_DOUBLE) &&
      (val->kind == ARKSH_VALUE_NUMBER || val->kind == ARKSH_VALUE_INTEGER ||
       val->kind == ARKSH_VALUE_FLOAT || val->kind == ARKSH_VALUE_DOUBLE)) {
    double a = cell->number;
    double b = val->number;
    if (strcmp(op, "==") == 0) return a == b;
    if (strcmp(op, "!=") == 0) return a != b;
    if (strcmp(op, "<")  == 0) return a <  b;
    if (strcmp(op, "<=") == 0) return a <= b;
    if (strcmp(op, ">")  == 0) return a >  b;
    if (strcmp(op, ">=") == 0) return a >= b;
    return 0;
  }
  /* String comparison otherwise */
  {
    char cell_text[ARKSH_MAX_MATRIX_CELL_TEXT];
    matrix_cell_format(cell, cell_text, sizeof(cell_text));
    cmp = strcmp(cell_text, val->text);
    if (strcmp(op, "==") == 0) return cmp == 0;
    if (strcmp(op, "!=") == 0) return cmp != 0;
    if (strcmp(op, "<")  == 0) return cmp <  0;
    if (strcmp(op, "<=") == 0) return cmp <= 0;
    if (strcmp(op, ">")  == 0) return cmp >  0;
    if (strcmp(op, ">=") == 0) return cmp >= 0;
  }
  return 0;
}

/* --- Properties --- */

static int matrix_prop_rows(ArkshShell *shell, const ArkshValue *receiver, ArkshValue *out_value, char *out, size_t out_size) {
  (void) shell;
  if (receiver == NULL || receiver->kind != ARKSH_VALUE_MATRIX) {
    snprintf(out, out_size, "rows is only valid on matrix values");
    return 1;
  }
  arksh_value_set_integer(out_value, (double)(receiver->matrix ? receiver->matrix->row_count : 0));
  return 0;
}

static int matrix_prop_cols(ArkshShell *shell, const ArkshValue *receiver, ArkshValue *out_value, char *out, size_t out_size) {
  (void) shell;
  if (receiver == NULL || receiver->kind != ARKSH_VALUE_MATRIX) {
    snprintf(out, out_size, "cols is only valid on matrix values");
    return 1;
  }
  arksh_value_set_integer(out_value, (double)(receiver->matrix ? receiver->matrix->col_count : 0));
  return 0;
}

static int matrix_prop_col_names(ArkshShell *shell, const ArkshValue *receiver, ArkshValue *out_value, char *out, size_t out_size) {
  size_t i;
  ArkshMatrix *m;
  (void) shell;
  if (receiver == NULL || receiver->kind != ARKSH_VALUE_MATRIX) {
    snprintf(out, out_size, "col_names is only valid on matrix values");
    return 1;
  }
  m = receiver->matrix;
  arksh_value_init(out_value);
  out_value->kind = ARKSH_VALUE_LIST;
  if (m == NULL) return 0;
  for (i = 0; i < m->col_count; ++i) {
    ArkshValue *col_val = allocate_shell_value(out, out_size);

    if (col_val == NULL) {
      return 1;
    }
    arksh_value_set_string(col_val, m->col_names[i]);
    if (arksh_value_list_append_value(out_value, col_val) != 0) {
      arksh_value_free(col_val);
      free(col_val);
      snprintf(out, out_size, "col_names: result too large");
      return 1;
    }
    arksh_value_free(col_val);
    free(col_val);
  }
  return 0;
}

static int matrix_prop_type(ArkshShell *shell, const ArkshValue *receiver, ArkshValue *out_value, char *out, size_t out_size) {
  (void) shell; (void) out; (void) out_size;
  (void) receiver;
  arksh_value_set_string(out_value, "matrix");
  return 0;
}

/* --- Mutation methods --- */

static int matrix_method_add_row(ArkshShell *shell, const ArkshValue *receiver, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  ArkshMatrix *src, *dst;
  size_t col;
  (void) shell;
  if (receiver == NULL || receiver->kind != ARKSH_VALUE_MATRIX || receiver->matrix == NULL) {
    snprintf(out, out_size, "add_row() is only valid on matrix values");
    return 1;
  }
  src = receiver->matrix;
  if ((size_t)argc != src->col_count) {
    snprintf(out, out_size, "add_row() expects %zu arguments, got %d", src->col_count, argc);
    return 1;
  }
  if (src->row_count >= ARKSH_MAX_MATRIX_ROWS) {
    snprintf(out, out_size, "add_row() matrix is full (max %d rows)", ARKSH_MAX_MATRIX_ROWS);
    return 1;
  }
  if (arksh_value_copy(out_value, receiver) != 0) {
    snprintf(out, out_size, "add_row() copy failed");
    return 1;
  }
  dst = out_value->matrix;
  for (col = 0; col < dst->col_count; ++col) {
    matrix_cell_set(&dst->rows[dst->row_count][col], &args[col]);
  }
  dst->row_count++;
  return 0;
}

static int matrix_method_drop_row(ArkshShell *shell, const ArkshValue *receiver, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  ArkshMatrix *src, *dst;
  size_t n, row, col;
  (void) shell;
  if (receiver == NULL || receiver->kind != ARKSH_VALUE_MATRIX || receiver->matrix == NULL) {
    snprintf(out, out_size, "drop_row() is only valid on matrix values");
    return 1;
  }
  if (argc != 1 || (args[0].kind != ARKSH_VALUE_NUMBER && args[0].kind != ARKSH_VALUE_INTEGER)) {
    snprintf(out, out_size, "drop_row() expects exactly one integer index");
    return 1;
  }
  src = receiver->matrix;
  n = (size_t)(long)args[0].number;
  if (n >= src->row_count) {
    snprintf(out, out_size, "drop_row() index %zu out of range (0..%zu)", n, src->row_count > 0 ? src->row_count - 1 : 0);
    return 1;
  }
  arksh_value_init(out_value);
  {
    const char *names[ARKSH_MAX_MATRIX_COLS];
    for (col = 0; col < src->col_count; ++col) names[col] = src->col_names[col];
    arksh_value_set_matrix(out_value, names, src->col_count);
  }
  dst = out_value->matrix;
  for (row = 0; row < src->row_count; ++row) {
    if (row == n) continue;
    for (col = 0; col < src->col_count; ++col) {
      dst->rows[dst->row_count][col] = src->rows[row][col];
    }
    dst->row_count++;
  }
  return 0;
}

static int matrix_method_rename_col(ArkshShell *shell, const ArkshValue *receiver, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  ArkshMatrix *src, *dst;
  int idx;
  (void) shell;
  if (receiver == NULL || receiver->kind != ARKSH_VALUE_MATRIX || receiver->matrix == NULL) {
    snprintf(out, out_size, "rename_col() is only valid on matrix values");
    return 1;
  }
  if (argc != 2 || args[0].kind != ARKSH_VALUE_STRING || args[1].kind != ARKSH_VALUE_STRING) {
    snprintf(out, out_size, "rename_col() expects two string arguments (old_name, new_name)");
    return 1;
  }
  src = receiver->matrix;
  idx = matrix_find_col(src, args[0].text);
  if (idx < 0) {
    snprintf(out, out_size, "rename_col() column \"%s\" not found", args[0].text);
    return 1;
  }
  if (arksh_value_copy(out_value, receiver) != 0) {
    snprintf(out, out_size, "rename_col() copy failed");
    return 1;
  }
  dst = out_value->matrix;
  copy_string(dst->col_names[idx], ARKSH_MAX_NAME, args[1].text);
  return 0;
}

/* --- Access and selection --- */

static int matrix_method_row(ArkshShell *shell, const ArkshValue *receiver, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  ArkshMatrix *m;
  size_t n, col;
  (void) shell;
  if (receiver == NULL || receiver->kind != ARKSH_VALUE_MATRIX || receiver->matrix == NULL) {
    snprintf(out, out_size, "row() is only valid on matrix values");
    return 1;
  }
  if (argc != 1 || (args[0].kind != ARKSH_VALUE_NUMBER && args[0].kind != ARKSH_VALUE_INTEGER)) {
    snprintf(out, out_size, "row() expects exactly one integer index");
    return 1;
  }
  m = receiver->matrix;
  n = (size_t)(long)args[0].number;
  if (n >= m->row_count) {
    snprintf(out, out_size, "row() index %zu out of range (0..%zu)", n, m->row_count > 0 ? m->row_count - 1 : 0);
    return 1;
  }
  arksh_value_set_map(out_value);
  for (col = 0; col < m->col_count; ++col) {
    ArkshValue *cell_val = allocate_shell_value(out, out_size);

    if (cell_val == NULL) {
      return 1;
    }
    matrix_cell_get(&m->rows[n][col], cell_val);
    if (arksh_value_map_set(out_value, m->col_names[col], cell_val) != 0) {
      arksh_value_free(cell_val);
      free(cell_val);
      snprintf(out, out_size, "row() result too large");
      return 1;
    }
    arksh_value_free(cell_val);
    free(cell_val);
  }
  return 0;
}

static int matrix_method_col(ArkshShell *shell, const ArkshValue *receiver, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  ArkshMatrix *m;
  int idx;
  size_t row;
  (void) shell;
  if (receiver == NULL || receiver->kind != ARKSH_VALUE_MATRIX || receiver->matrix == NULL) {
    snprintf(out, out_size, "col() is only valid on matrix values");
    return 1;
  }
  if (argc != 1 || args[0].kind != ARKSH_VALUE_STRING) {
    snprintf(out, out_size, "col() expects exactly one string column name");
    return 1;
  }
  m = receiver->matrix;
  idx = matrix_find_col(m, args[0].text);
  if (idx < 0) {
    snprintf(out, out_size, "col() column \"%s\" not found", args[0].text);
    return 1;
  }
  arksh_value_init(out_value);
  out_value->kind = ARKSH_VALUE_LIST;
  for (row = 0; row < m->row_count; ++row) {
    ArkshValue *cell_val = allocate_shell_value(out, out_size);

    if (cell_val == NULL) {
      return 1;
    }
    matrix_cell_get(&m->rows[row][(size_t)idx], cell_val);
    if (arksh_value_list_append_value(out_value, cell_val) != 0) {
      arksh_value_free(cell_val);
      free(cell_val);
      snprintf(out, out_size, "col() result too large");
      return 1;
    }
    arksh_value_free(cell_val);
    free(cell_val);
  }
  return 0;
}

static int matrix_method_select(ArkshShell *shell, const ArkshValue *receiver, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  ArkshMatrix *src, *dst;
  int col_idx[ARKSH_MAX_MATRIX_COLS];
  const char *new_names[ARKSH_MAX_MATRIX_COLS];
  int i, num_cols;
  size_t row, col;
  (void) shell;
  if (receiver == NULL || receiver->kind != ARKSH_VALUE_MATRIX || receiver->matrix == NULL) {
    snprintf(out, out_size, "select() is only valid on matrix values");
    return 1;
  }
  if (argc == 0) {
    snprintf(out, out_size, "select() expects at least one column name");
    return 1;
  }
  src = receiver->matrix;
  num_cols = argc < ARKSH_MAX_MATRIX_COLS ? argc : ARKSH_MAX_MATRIX_COLS;
  for (i = 0; i < num_cols; ++i) {
    if (args[i].kind != ARKSH_VALUE_STRING) {
      snprintf(out, out_size, "select() expects string column names");
      return 1;
    }
    col_idx[i] = matrix_find_col(src, args[i].text);
    if (col_idx[i] < 0) {
      snprintf(out, out_size, "select() column \"%s\" not found", args[i].text);
      return 1;
    }
    new_names[i] = src->col_names[col_idx[i]];
  }
  arksh_value_set_matrix(out_value, new_names, (size_t)num_cols);
  dst = out_value->matrix;
  for (row = 0; row < src->row_count; ++row) {
    for (col = 0; col < (size_t)num_cols; ++col) {
      dst->rows[dst->row_count][col] = src->rows[row][(size_t)col_idx[col]];
    }
    dst->row_count++;
  }
  return 0;
}

static int matrix_method_where(ArkshShell *shell, const ArkshValue *receiver, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  ArkshMatrix *src, *dst;
  int idx;
  size_t row, col;
  (void) shell;
  if (receiver == NULL || receiver->kind != ARKSH_VALUE_MATRIX || receiver->matrix == NULL) {
    snprintf(out, out_size, "where() is only valid on matrix values");
    return 1;
  }
  if (argc != 3 || args[0].kind != ARKSH_VALUE_STRING || args[1].kind != ARKSH_VALUE_STRING) {
    snprintf(out, out_size, "where() expects (col_name, operator, value)");
    return 1;
  }
  src = receiver->matrix;
  idx = matrix_find_col(src, args[0].text);
  if (idx < 0) {
    snprintf(out, out_size, "where() column \"%s\" not found", args[0].text);
    return 1;
  }
  {
    const char *names[ARKSH_MAX_MATRIX_COLS];
    for (col = 0; col < src->col_count; ++col) names[col] = src->col_names[col];
    arksh_value_set_matrix(out_value, names, src->col_count);
  }
  dst = out_value->matrix;
  for (row = 0; row < src->row_count; ++row) {
    if (matrix_cell_compare(&src->rows[row][(size_t)idx], args[1].text, &args[2])) {
      for (col = 0; col < src->col_count; ++col) {
        dst->rows[dst->row_count][col] = src->rows[row][col];
      }
      dst->row_count++;
    }
  }
  return 0;
}

/* --- Interoperability --- */

static int matrix_method_to_maps(ArkshShell *shell, const ArkshValue *receiver, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  ArkshMatrix *m;
  size_t row, col;
  (void) shell; (void) argc; (void) args;
  if (receiver == NULL || receiver->kind != ARKSH_VALUE_MATRIX || receiver->matrix == NULL) {
    snprintf(out, out_size, "to_maps is only valid on matrix values");
    return 1;
  }
  m = receiver->matrix;
  arksh_value_init(out_value);
  out_value->kind = ARKSH_VALUE_LIST;
  for (row = 0; row < m->row_count; ++row) {
    ArkshValue *row_map = allocate_shell_value(out, out_size);

    if (row_map == NULL) {
      return 1;
    }
    arksh_value_set_map(row_map);
    for (col = 0; col < m->col_count; ++col) {
      ArkshValue *cell_val = allocate_shell_value(out, out_size);

      if (cell_val == NULL) {
        arksh_value_free(row_map);
        free(row_map);
        return 1;
      }
      matrix_cell_get(&m->rows[row][col], cell_val);
      if (arksh_value_map_set(row_map, m->col_names[col], cell_val) != 0) {
        arksh_value_free(cell_val);
        free(cell_val);
        arksh_value_free(row_map);
        free(row_map);
        snprintf(out, out_size, "to_maps: map too large");
        return 1;
      }
      arksh_value_free(cell_val);
      free(cell_val);
    }
    if (arksh_value_list_append_value(out_value, row_map) != 0) {
      arksh_value_free(row_map);
      free(row_map);
      snprintf(out, out_size, "to_maps: list too large");
      return 1;
    }
    arksh_value_free(row_map);
    free(row_map);
  }
  return 0;
}

static int matrix_method_from_maps(ArkshShell *shell, const ArkshValue *receiver, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  const ArkshValue *list_val;
  ArkshMatrix *dst;
  size_t row;
  (void) shell; (void) receiver;
  if (argc != 1) {
    snprintf(out, out_size, "from_maps() expects exactly one list argument");
    return 1;
  }
  /* Accept list directly or as nested */
  list_val = &args[0];
  if (list_val->kind != ARKSH_VALUE_LIST) {
    snprintf(out, out_size, "from_maps() argument must be a list");
    return 1;
  }
  if (list_val->list.count == 0) {
    arksh_value_set_matrix(out_value, NULL, 0);
    return 0;
  }
  /* Infer columns from first item (must be a map) */
  {
    const ArkshValueItem *first = &list_val->list.items[0];
    const ArkshValue *first_map = (first->nested != NULL) ? first->nested : NULL;
    const char *col_names[ARKSH_MAX_MATRIX_COLS];
    size_t col_count;

    if (first->kind != ARKSH_VALUE_MAP && first->kind != ARKSH_VALUE_DICT) {
      snprintf(out, out_size, "from_maps() list items must be maps");
      return 1;
    }
    if (first_map == NULL) {
      snprintf(out, out_size, "from_maps() could not access first map");
      return 1;
    }
    col_count = first_map->map.count;
    if (col_count > ARKSH_MAX_MATRIX_COLS) col_count = ARKSH_MAX_MATRIX_COLS;
    {
      size_t c;
      for (c = 0; c < col_count; ++c) col_names[c] = first_map->map.entries[c].key;
      arksh_value_set_matrix(out_value, col_names, col_count);
    }
  }
  dst = out_value->matrix;
  for (row = 0; row < list_val->list.count && dst->row_count < ARKSH_MAX_MATRIX_ROWS; ++row) {
    const ArkshValueItem *item = &list_val->list.items[row];
    const ArkshValue *map_val = (item->nested != NULL) ? item->nested : NULL;
    size_t col;
    if (item->kind != ARKSH_VALUE_MAP && item->kind != ARKSH_VALUE_DICT) continue;
    if (map_val == NULL) continue;
    for (col = 0; col < dst->col_count; ++col) {
      const ArkshValueItem *entry = arksh_value_map_get_item(map_val, dst->col_names[col]);
      if (entry != NULL) {
        ArkshValue *cell_val = allocate_shell_value(out, out_size);

        if (cell_val == NULL) {
          return 1;
        }
        if (arksh_value_set_from_item(cell_val, entry) == 0) {
          matrix_cell_set(&dst->rows[dst->row_count][col], cell_val);
          arksh_value_free(cell_val);
        }
        free(cell_val);
      }
    }
    dst->row_count++;
  }
  return 0;
}

/* Simple CSV field writer: quotes field if it contains comma, quote, or newline. */
static void csv_write_field(const char *text, char *out, size_t out_size) {
  int needs_quote = 0;
  const char *p;
  size_t pos;

  for (p = text; *p; ++p) {
    if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') {
      needs_quote = 1;
      break;
    }
  }
  pos = 0;
  if (needs_quote) {
    if (pos < out_size - 1) out[pos++] = '"';
    for (p = text; *p && pos < out_size - 3; ++p) {
      if (*p == '"') { out[pos++] = '"'; out[pos++] = '"'; }
      else out[pos++] = *p;
    }
    if (pos < out_size - 1) out[pos++] = '"';
  } else {
    for (p = text; *p && pos < out_size - 1; ++p) out[pos++] = *p;
  }
  out[pos] = '\0';
}

static int matrix_method_to_csv(ArkshShell *shell, const ArkshValue *receiver, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  ArkshMatrix *m;
  size_t row, col, flen;
  char buf[ARKSH_MAX_OUTPUT];
  size_t pos = 0;
  (void) shell; (void) argc; (void) args;
  if (receiver == NULL || receiver->kind != ARKSH_VALUE_MATRIX || receiver->matrix == NULL) {
    snprintf(out, out_size, "to_csv is only valid on matrix values");
    return 1;
  }
  m = receiver->matrix;
  /* Header row */
  for (col = 0; col < m->col_count; ++col) {
    char field[ARKSH_MAX_NAME * 3];
    if (col > 0 && pos < sizeof(buf) - 1) buf[pos++] = ',';
    csv_write_field(m->col_names[col], field, sizeof(field));
    flen = strlen(field);
    if (pos + flen < sizeof(buf) - 1) { memcpy(buf + pos, field, flen); pos += flen; }
  }
  if (pos < sizeof(buf) - 1) buf[pos++] = '\n';
  /* Data rows */
  for (row = 0; row < m->row_count; ++row) {
    for (col = 0; col < m->col_count; ++col) {
      char field[ARKSH_MAX_MATRIX_CELL_TEXT * 3];
      char raw[ARKSH_MAX_MATRIX_CELL_TEXT];
      matrix_cell_format(&m->rows[row][col], raw, sizeof(raw));
      if (col > 0 && pos < sizeof(buf) - 1) buf[pos++] = ',';
      csv_write_field(raw, field, sizeof(field));
      flen = strlen(field);
      if (pos + flen < sizeof(buf) - 1) { memcpy(buf + pos, field, flen); pos += flen; }
    }
    if (pos < sizeof(buf) - 1) buf[pos++] = '\n';
  }
  buf[pos] = '\0';
  arksh_value_set_string(out_value, buf);
  return 0;
}

/* Parse one CSV field from src starting at *pos, advance *pos past the delimiter. */
static void csv_read_field(const char *src, size_t *pos, char *out, size_t out_size) {
  size_t p = *pos;
  size_t j = 0;

  if (src[p] == '"') {
    p++;
    while (src[p] != '\0') {
      if (src[p] == '"') {
        if (src[p + 1] == '"') { if (j < out_size - 1) out[j++] = '"'; p += 2; }
        else { p++; break; }
      } else {
        if (j < out_size - 1) out[j++] = src[p];
        p++;
      }
    }
  } else {
    while (src[p] != '\0' && src[p] != ',' && src[p] != '\n' && src[p] != '\r') {
      if (j < out_size - 1) out[j++] = src[p];
      p++;
    }
  }
  out[j] = '\0';
  if (src[p] == ',') p++;
  *pos = p;
}

static int matrix_method_from_csv(ArkshShell *shell, const ArkshValue *receiver, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  const char *csv;
  size_t pos, col;
  const char *col_names[ARKSH_MAX_MATRIX_COLS];
  char name_buf[ARKSH_MAX_MATRIX_COLS][ARKSH_MAX_NAME];
  size_t col_count;
  ArkshMatrix *dst;
  (void) shell; (void) receiver;
  if (argc != 1 || args[0].kind != ARKSH_VALUE_STRING) {
    snprintf(out, out_size, "from_csv() expects exactly one string argument");
    return 1;
  }
  csv = args[0].text;
  pos = 0;
  col_count = 0;
  /* Parse header row */
  while (csv[pos] != '\0' && csv[pos] != '\n' && csv[pos] != '\r') {
    if (col_count >= ARKSH_MAX_MATRIX_COLS) break;
    csv_read_field(csv, &pos, name_buf[col_count], ARKSH_MAX_NAME);
    col_names[col_count] = name_buf[col_count];
    col_count++;
    if (csv[pos] == '\n' || csv[pos] == '\r') break;
  }
  /* Skip newline */
  while (csv[pos] == '\n' || csv[pos] == '\r') pos++;
  if (col_count == 0) {
    arksh_value_set_matrix(out_value, NULL, 0);
    return 0;
  }
  arksh_value_set_matrix(out_value, col_names, col_count);
  dst = out_value->matrix;
  /* Parse data rows */
  while (csv[pos] != '\0' && dst->row_count < ARKSH_MAX_MATRIX_ROWS) {
    /* Skip blank lines */
    if (csv[pos] == '\n' || csv[pos] == '\r') {
      while (csv[pos] == '\n' || csv[pos] == '\r') pos++;
      continue;
    }
    for (col = 0; col < dst->col_count; ++col) {
      char field[ARKSH_MAX_MATRIX_CELL_TEXT];
      csv_read_field(csv, &pos, field, sizeof(field));
      copy_string(dst->rows[dst->row_count][col].text, ARKSH_MAX_MATRIX_CELL_TEXT, field);
      dst->rows[dst->row_count][col].kind = ARKSH_VALUE_STRING;
    }
    /* Skip to end of line */
    while (csv[pos] != '\0' && csv[pos] != '\n' && csv[pos] != '\r') pos++;
    while (csv[pos] == '\n' || csv[pos] == '\r') pos++;
    dst->row_count++;
  }
  return 0;
}

static int matrix_method_to_json(ArkshShell *shell, const ArkshValue *receiver, int argc, const ArkshValue *args, ArkshValue *out_value, char *out, size_t out_size) {
  ArkshMatrix *m;
  char buf[ARKSH_MAX_OUTPUT];
  size_t pos, row, col;
  (void) shell; (void) argc; (void) args;
  if (receiver == NULL || receiver->kind != ARKSH_VALUE_MATRIX || receiver->matrix == NULL) {
    snprintf(out, out_size, "to_json is only valid on matrix values");
    return 1;
  }
  m = receiver->matrix;
  pos = 0;
  if (pos < sizeof(buf) - 1) buf[pos++] = '[';
  for (row = 0; row < m->row_count; ++row) {
    if (row > 0 && pos < sizeof(buf) - 1) { buf[pos++] = ','; if (pos < sizeof(buf) - 1) buf[pos++] = ' '; }
    if (pos < sizeof(buf) - 1) buf[pos++] = '{';
    for (col = 0; col < m->col_count; ++col) {
      char cell_text[ARKSH_MAX_MATRIX_CELL_TEXT];
      if (col > 0 && pos < sizeof(buf) - 1) { buf[pos++] = ','; if (pos < sizeof(buf) - 1) buf[pos++] = ' '; }
      /* key */
      if (pos < sizeof(buf) - 1) buf[pos++] = '"';
      { const char *k = m->col_names[col]; while (*k && pos < sizeof(buf) - 1) buf[pos++] = *k++; }
      if (pos < sizeof(buf) - 1) buf[pos++] = '"';
      if (pos < sizeof(buf) - 1) buf[pos++] = ':';
      if (pos < sizeof(buf) - 1) buf[pos++] = ' ';
      /* value */
      matrix_cell_format(&m->rows[row][col], cell_text, sizeof(cell_text));
      if (m->rows[row][col].kind == ARKSH_VALUE_NUMBER ||
          m->rows[row][col].kind == ARKSH_VALUE_INTEGER ||
          m->rows[row][col].kind == ARKSH_VALUE_FLOAT ||
          m->rows[row][col].kind == ARKSH_VALUE_DOUBLE) {
        const char *n = cell_text;
        while (*n && pos < sizeof(buf) - 1) buf[pos++] = *n++;
      } else if (m->rows[row][col].kind == ARKSH_VALUE_BOOLEAN) {
        const char *bv = m->rows[row][col].boolean ? "true" : "false";
        while (*bv && pos < sizeof(buf) - 1) buf[pos++] = *bv++;
      } else {
        const char *s = cell_text;
        if (pos < sizeof(buf) - 1) buf[pos++] = '"';
        while (*s && pos < sizeof(buf) - 3) {
          if (*s == '"' || *s == '\\') { buf[pos++] = '\\'; }
          buf[pos++] = *s++;
        }
        if (pos < sizeof(buf) - 1) buf[pos++] = '"';
      }
    }
    if (pos < sizeof(buf) - 1) buf[pos++] = '}';
  }
  if (pos < sizeof(buf) - 1) buf[pos++] = ']';
  buf[pos] = '\0';
  arksh_value_set_string(out_value, buf);
  return 0;
}

/* --- Resolver --- */

static int resolver_matrix(ArkshShell *shell, int argc, const ArkshValue *args, ArkshValue *out_value, char *error, size_t error_size) {
  const char *col_names[ARKSH_MAX_MATRIX_COLS];
  int i;
  (void) shell;
  if (argc > ARKSH_MAX_MATRIX_COLS) {
    snprintf(error, error_size, "Matrix() too many columns (max %d)", ARKSH_MAX_MATRIX_COLS);
    return 1;
  }
  for (i = 0; i < argc; ++i) {
    if (args[i].kind != ARKSH_VALUE_STRING) {
      snprintf(error, error_size, "Matrix() expects string column names");
      return 1;
    }
    col_names[i] = args[i].text;
  }
  arksh_value_set_matrix(out_value, col_names, (size_t)argc);
  return 0;
}

/* ===== end E6-S8 ===== */

static int register_builtin_extensions(ArkshShell *shell) {
  if (shell == NULL) {
    return 1;
  }

  if (arksh_shell_register_native_method_extension(shell, "any", "print", method_print, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "map", "keys", map_method_keys, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "map", "values", map_method_values, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "map", "entries", map_method_entries, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "map", "get", map_method_get, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "map", "has", map_method_has, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "map", "get_path", map_like_method_get_path, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "map", "has_path", map_like_method_has_path, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "map", "set_path", map_like_method_set_path, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "map", "pick", map_like_method_pick, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "map", "merge", map_like_method_merge, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "file", "read_json", method_read_json, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "file", "write_json", method_write_json, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "path", "write_json", method_write_json, 0) != 0) {
    return 1;
  }
  /* E6-S6: Dict methods and properties */
  if (arksh_shell_register_native_method_extension(shell, "dict", "get", dict_method_get, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "dict", "has", dict_method_has, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "dict", "set", dict_method_set, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "dict", "delete", dict_method_delete, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "dict", "to_json", dict_method_to_json, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "dict", "from_json", dict_method_from_json, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "dict", "get_path", map_like_method_get_path, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "dict", "has_path", map_like_method_has_path, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "dict", "set_path", map_like_method_set_path, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "dict", "pick", map_like_method_pick, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_method_extension(shell, "dict", "merge", map_like_method_merge, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_property_extension(shell, "dict", "keys", dict_prop_keys, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_native_property_extension(shell, "dict", "values", dict_prop_values, 0) != 0) {
    return 1;
  }
  if (arksh_shell_register_type_descriptor(shell, "dict", "immutable key-value dictionary with string keys") != 0) {
    return 1;
  }

  /* E6-S8: Matrix */
  if (arksh_shell_register_native_property_extension(shell, "matrix", "rows",      matrix_prop_rows,     0) != 0) return 1;
  if (arksh_shell_register_native_property_extension(shell, "matrix", "cols",      matrix_prop_cols,     0) != 0) return 1;
  if (arksh_shell_register_native_property_extension(shell, "matrix", "col_names", matrix_prop_col_names,0) != 0) return 1;
  if (arksh_shell_register_native_property_extension(shell, "matrix", "type",      matrix_prop_type,     0) != 0) return 1;
  if (arksh_shell_register_native_method_extension(shell, "matrix", "add_row",    matrix_method_add_row,    0) != 0) return 1;
  if (arksh_shell_register_native_method_extension(shell, "matrix", "drop_row",   matrix_method_drop_row,   0) != 0) return 1;
  if (arksh_shell_register_native_method_extension(shell, "matrix", "rename_col", matrix_method_rename_col, 0) != 0) return 1;
  if (arksh_shell_register_native_method_extension(shell, "matrix", "row",        matrix_method_row,        0) != 0) return 1;
  if (arksh_shell_register_native_method_extension(shell, "matrix", "col",        matrix_method_col,        0) != 0) return 1;
  if (arksh_shell_register_native_method_extension(shell, "matrix", "select",     matrix_method_select,     0) != 0) return 1;
  if (arksh_shell_register_native_method_extension(shell, "matrix", "where",      matrix_method_where,      0) != 0) return 1;
  if (arksh_shell_register_native_method_extension(shell, "matrix", "to_maps",    matrix_method_to_maps,    0) != 0) return 1;
  if (arksh_shell_register_native_method_extension(shell, "matrix", "from_maps",  matrix_method_from_maps,  0) != 0) return 1;
  if (arksh_shell_register_native_method_extension(shell, "matrix", "to_csv",     matrix_method_to_csv,     0) != 0) return 1;
  if (arksh_shell_register_native_method_extension(shell, "matrix", "from_csv",   matrix_method_from_csv,   0) != 0) return 1;
  if (arksh_shell_register_native_method_extension(shell, "matrix", "to_json",    matrix_method_to_json,    0) != 0) return 1;
  if (arksh_shell_register_type_descriptor(shell, "matrix", "bidimensional tabular value with named columns") != 0) return 1;

  return 0;
}

static int try_load_default_config(ArkshShell *shell) {
  const char *env_path = getenv("ARKSH_CONFIG");
  char legacy_dir[ARKSH_MAX_PATH];
  char path[ARKSH_MAX_PATH];
  char output[ARKSH_MAX_OUTPUT];

  if (env_path != NULL && env_path[0] != '\0') {
    if (arksh_shell_load_config(shell, env_path, output, sizeof(output)) == 0) {
      return 0;
    }
  }

  if (arksh_shell_load_config(shell, "arksh.conf", output, sizeof(output)) == 0) {
    return 0;
  }

  if (shell->config_dir[0] != '\0' &&
      join_runtime_path(shell->config_dir, "prompt.conf", path, sizeof(path)) == 0) {
    if (arksh_shell_load_config(shell, path, output, sizeof(output)) == 0) {
      return 0;
    }
  }

  build_legacy_arksh_dir(shell, legacy_dir, sizeof(legacy_dir));
  if (legacy_dir[0] != '\0' &&
      join_runtime_path(legacy_dir, "prompt.conf", path, sizeof(path)) == 0) {
    if (arksh_shell_load_config(shell, path, output, sizeof(output)) == 0) {
      return 0;
    }
  }

  return 0;
}

static int try_load_default_rc(ArkshShell *shell) {
  const char *env_path = getenv("ARKSH_RC");
  const char *home = arksh_shell_get_var(shell, "HOME");
  char legacy_path[ARKSH_MAX_PATH];
  char path[ARKSH_MAX_PATH];
  char output[ARKSH_MAX_OUTPUT];

  if (env_path != NULL && env_path[0] != '\0') {
    if (arksh_shell_source_file(shell, env_path, 0, NULL, output, sizeof(output)) != 0) {
      fprintf(stderr, "arksh: %s\n", output);
    }
    return 0;
  }

  if (shell->config_dir[0] != '\0' &&
      join_runtime_path(shell->config_dir, "arkshrc", path, sizeof(path)) == 0) {
    if (arksh_shell_source_file(shell, path, 0, NULL, output, sizeof(output)) == 0) {
      return 0;
    }
    if (strstr(output, "unable to open source file") == NULL) {
      fprintf(stderr, "arksh: %s\n", output);
      return 0;
    }
  }

  if (home == NULL || home[0] == '\0' ||
      join_runtime_path(home, ".arkshrc", legacy_path, sizeof(legacy_path)) != 0) {
    return 0;
  }

  if (arksh_shell_source_file(shell, legacy_path, 0, NULL, output, sizeof(output)) != 0 &&
      strstr(output, "unable to open source file") == NULL) {
    fprintf(stderr, "arksh: %s\n", output);
  }

  return 0;
}

int arksh_shell_init(ArkshShell *shell) {
  const char *perf_env;

  if (shell == NULL) {
    return 1;
  }

  memset(shell, 0, sizeof(*shell));
  shell->running = 1;
  shell->last_status = 0;
  shell->next_instance_id = 1;
  shell->next_job_id = 1;
  shell->loading_plugin_index = -1;
  shell->last_bg_pid = -1;
  arksh_scratch_arena_init(&shell->scratch);
  perf_env = getenv("ARKSH_PERF");
  arksh_perf_enable(perf_env != NULL && perf_env[0] != '\0' && strcmp(perf_env, "0") != 0);
  {
    ArkshPlatformProcessInfo proc_info;
    memset(&proc_info, 0, sizeof(proc_info));
    if (arksh_platform_get_process_info(&proc_info) == 0) {
      shell->shell_pid = (long long) proc_info.pid;
    }
  }
  copy_string(shell->traps[ARKSH_TRAP_EXIT].name, sizeof(shell->traps[ARKSH_TRAP_EXIT].name), "EXIT");

  if (arksh_platform_getcwd(shell->cwd, sizeof(shell->cwd)) != 0) {
    copy_string(shell->cwd, sizeof(shell->cwd), ".");
  }

  arksh_prompt_config_init(&shell->prompt);
  initialize_default_variables(shell);
  resolve_runtime_directories(shell);
  resolve_history_path(shell);
  load_history(shell);

  if (register_builtin_commands(shell) != 0) {
    return 1;
  }
  if (register_builtin_value_resolvers(shell) != 0) {
    return 1;
  }
  if (register_builtin_pipeline_stages(shell) != 0) {
    return 1;
  }
  if (register_builtin_extensions(shell) != 0) {
    return 1;
  }

  if (try_load_default_config(shell) != 0) {
    return 1;
  }

  if (try_load_plugin_autoload(shell) != 0) {
    return 1;
  }

  return try_load_default_rc(shell);
}

void arksh_shell_destroy(ArkshShell *shell) {
  size_t i;

  if (shell == NULL) {
    return;
  }

  save_history(shell);

  for (i = 0; i < shell->binding_count; ++i) {
    arksh_value_free(&shell->bindings[i].value);
  }
  for (i = 0; i < shell->class_count; ++i) {
    free_class_definition_contents(&shell->classes[i]);
  }
  for (i = 0; i < shell->instance_count; ++i) {
    arksh_value_free(&shell->instances[i].fields);
  }

  for (i = 0; i < shell->job_count; ++i) {
    arksh_platform_close_background_process(&shell->jobs[i].process);
  }

  for (i = 0; i < shell->plugin_count; ++i) {
    ArkshPluginShutdownFn shutdown_fn;

    shutdown_fn = (ArkshPluginShutdownFn) arksh_platform_library_symbol(shell->plugins[i].handle, "arksh_plugin_shutdown");
    if (shutdown_fn != NULL) {
      shutdown_fn(shell);
    }
    arksh_platform_library_close(shell->plugins[i].handle);
    shell->plugins[i].handle = NULL;
  }

  arksh_scratch_arena_destroy(&shell->scratch);
}

int arksh_shell_register_command(ArkshShell *shell, const char *name, const char *description, ArkshCommandFn fn, int is_plugin_command) {
  size_t i;

  if (shell == NULL || name == NULL || description == NULL || fn == NULL) {
    return 1;
  }

  if (shell->command_count >= ARKSH_MAX_COMMANDS) {
    return 1;
  }

  for (i = 0; i < shell->command_count; ++i) {
    if (strcmp(shell->commands[i].name, name) == 0) {
      return 1;
    }
  }

  copy_string(shell->commands[shell->command_count].name, sizeof(shell->commands[shell->command_count].name), name);
  copy_string(shell->commands[shell->command_count].description, sizeof(shell->commands[shell->command_count].description), description);
  shell->commands[shell->command_count].fn = fn;
  shell->commands[shell->command_count].is_plugin_command = is_plugin_command;
  shell->commands[shell->command_count].owner_plugin_index = is_plugin_command ? shell->loading_plugin_index : -1;
  /* Plugin commands default to PURE – they receive a copy of the output
     buffer and cannot directly modify shell-internal state. */
  shell->commands[shell->command_count].kind = ARKSH_BUILTIN_PURE;
  shell->command_count++;
  return 0;
}

/* Internal helper: like arksh_shell_register_command but accepts an explicit
   ArkshBuiltinKind so that register_builtin_commands() can classify each
   built-in at registration time. */
static int register_builtin(ArkshShell *shell, const char *name, const char *description,
                             ArkshCommandFn fn, ArkshBuiltinKind kind) {
  if (arksh_shell_register_command(shell, name, description, fn, 0) != 0) {
    return 1;
  }
  /* Overwrite the default kind set by arksh_shell_register_command. */
  shell->commands[shell->command_count - 1].kind = kind;
  return 0;
}

void arksh_shell_write_output(const char *text) {
  write_buffer(text);
}

void arksh_shell_print_help(const ArkshShell *shell, char *out, size_t out_size) {
  size_t i;

  if (shell == NULL || out == NULL || out_size == 0) {
    return;
  }

  out[0] = '\0';

  /* ── Commands ─────────────────────────────────────────────────────────── */
  snprintf(out + strlen(out), out_size - strlen(out), "Commands (%zu):\n", shell->command_count);
  for (i = 0; i < shell->command_count; ++i) {
    if (shell->commands[i].is_plugin_command && !plugin_index_is_active(shell, shell->commands[i].owner_plugin_index)) {
      continue;
    }
    snprintf(out + strlen(out), out_size - strlen(out), "  %-14s %s%s\n",
      shell->commands[i].name,
      shell->commands[i].description,
      shell->commands[i].is_plugin_command ? " [plugin]" : "");
  }

  /* ── Value Resolvers ──────────────────────────────────────────────────── */
  snprintf(out + strlen(out), out_size - strlen(out), "\nValue Resolvers (%zu):\n", shell->value_resolver_count);
  for (i = 0; i < shell->value_resolver_count; ++i) {
    if (shell->value_resolvers[i].is_plugin_resolver && !plugin_index_is_active(shell, shell->value_resolvers[i].owner_plugin_index)) {
      continue;
    }
    snprintf(out + strlen(out), out_size - strlen(out), "  %-14s %s%s\n",
      shell->value_resolvers[i].name,
      shell->value_resolvers[i].description,
      shell->value_resolvers[i].is_plugin_resolver ? " [plugin]" : "");
  }

  /* ── Pipeline Stages ──────────────────────────────────────────────────── */
  snprintf(out + strlen(out), out_size - strlen(out), "\nPipeline Stages (%zu):\n", shell->pipeline_stage_count);
  for (i = 0; i < shell->pipeline_stage_count; ++i) {
    if (shell->pipeline_stages[i].is_plugin_stage && !plugin_index_is_active(shell, shell->pipeline_stages[i].owner_plugin_index)) {
      continue;
    }
    snprintf(out + strlen(out), out_size - strlen(out), "  %-14s %s%s\n",
      shell->pipeline_stages[i].name,
      shell->pipeline_stages[i].description,
      shell->pipeline_stages[i].is_plugin_stage ? " [plugin]" : "");
  }

  /* ── Types ────────────────────────────────────────────────────────────── */
  if (shell->type_descriptor_count > 0) {
    snprintf(out + strlen(out), out_size - strlen(out), "\nTypes (%zu):\n", shell->type_descriptor_count);
    for (i = 0; i < shell->type_descriptor_count; ++i) {
      snprintf(out + strlen(out), out_size - strlen(out), "  %-14s %s\n",
        shell->type_descriptors[i].type_name,
        shell->type_descriptors[i].description);
    }
  }

  snprintf(out + strlen(out), out_size - strlen(out), "\nUse 'help commands|resolvers|stages|types' for a section, 'help <name>' for details.\n");

  snprintf(
    out + strlen(out),
    out_size - strlen(out),
    "\nShell State:\n"
    "  set PROJECT arksh\n"
    "  export PROJECT_ROOT \"$PWD\"\n"
    "  alias ll=\"ls -lah\"\n"
    "  let files = . -> children()\n"
    "  let is_file = [:it | it -> type == \"file\"]\n"
    "  function greet(name) do\n"
    "    text(\"hello %%s\") -> print(name)\n"
    "  endfunction\n"
    "  class Named do\n"
    "    property name = text(\"unnamed\")\n"
    "    method rename = [:self :next | self -> set(\"name\", next)]\n"
    "  endclass\n"
    "  class Document extends Named do\n"
    "    method init = [:self :name | self -> set(\"name\", name)]\n"
    "  endclass\n"
    "  extend directory property child_count = [:it | it -> children() |> count()]\n"
    "  extend object method label = [:it :prefix | prefix]\n"
    "  let\n"
    "  function\n"
    "  functions greet\n"
    "  class\n"
    "  classes Document\n"
    "  extend\n"
    "  history\n"
    "  perf show\n"
    "  sleep 5 &\n"
    "  jobs\n"
    "  fg\n"
    "  bg\n"
    "  wait %%1\n"
    "  eval \"text(\\\"hello\\\") -> print()\"\n"
    "  trap \"text(\\\"bye\\\") -> print()\" EXIT\n"
    "  true && text(\"ok\") -> print()\n"
    "  bool(true) ? \"yes\" : \"no\"\n"
    "  if true ; then text(\"ok\") -> print() ; fi\n"
    "  until true ; do text(\"retry\") -> print() ; done\n"
    "  case . -> type in directory) text(\"dir\") -> print() ;; *) text(\"other\") -> print() ;; esac\n"
    "  switch . -> type ; case \"directory\" ; then text(\"dir\") -> print() ; default ; then text(\"other\") -> print() ; endswitch\n"
    "  for entry in . -> children() |> take(3) ; do entry -> name ; done\n"
    "  greet team\n"
    "  source $ARKSH_CONFIG_DIR/arkshrc\n"
    "  type ls\n"
    "  plugin list\n"
    "  plugin disable sample-plugin\n"
    "  plugin enable sample-plugin\n"
    "\nInteractive Keys:\n"
    "  arrows to move and browse history\n"
    "  tab for command/path completion and -> member hints\n"
    "  ctrl-a / ctrl-e to jump line bounds\n"
    "  ctrl-z to stop a foreground job on POSIX\n"
    "\nExpressions:\n"
    "  . -> type\n"
    "  . -> children()\n"
    "  README.md -> read_text(256)\n"
    "  data.json -> read_json()\n"
    "  text(\"hello\")\n"
    "  bool(true) ? \"yes\" : \"no\"\n"
    "  text(\"%%s\") -> print(\"hello\")\n"
    "  env() -> HOME\n"
    "  proc() -> pid\n"
    "  shell() -> plugins |> count()\n"
    "  list(1, 2, \"three\")\n"
    "  capture(\"pwd\")\n"
    "  [:it | it -> name]\n"
    "  [:n | local next = n + 1 ; next]\n"
    "  Document(text(\"readme\")) -> type\n"
    "  Document(text(\"readme\")) -> isa(\"Named\")\n"
    "  is_file -> arity\n"
    "  obj(\".\").type   [legacy]\n"
    "\nObject Pipelines:\n"
    "  . -> children() |> where(type == \"file\") |> sort(size desc)\n"
    "  . -> children() |> where(type == \"file\") |> take(5)\n"
    "  . -> children() |> first()\n"
    "  . -> children() |> each(name) |> take(5)\n"
    "  . -> children() |> where(is_file) |> each([:it | it -> name])\n"
    "  . -> child_count\n"
    "  capture(\"pwd\") |> lines() |> first()\n"
    "  text(\" a, b , c \") |> trim() |> split(\",\") |> join(\" | \")\n"
    "  list(1, 2, 3) |> reduce(number(0), [:acc :n | local next = acc + n ; next])\n"
    "  list(1, 2, 3) |> reduce(number(0), [:acc :n | acc + n])\n"
    "  list(1, 2, 3) |> to_json()\n"
    "  map(\"a\", list(1, 2, map(\"b\", true))) |> to_json()\n"
    "  tests/fixtures/json/nested.json -> read_json() -> get_path(\"a[2].b\")\n"
    "  list(map(\"name\", \"alpha\"), map(\"name\", \"beta\")) |> pluck(\"name\") |> join(\",\")\n"
    "  list(1, 20, 3) |> sort(value desc)\n"
    "  plugin load build/arksh_sample_plugin.dylib ; sample() -> name\n"
    "  plugin load build/arksh_sample_plugin.dylib ; text(\"ciao\") |> sample_wrap()\n"
    "\nShell Pipelines:\n"
    "  ls -1 | wc -l\n"
    "  ls include > out.txt\n"
    "  cat < README.md | grep arksh\n"
    "  ls missing 2>&1 | wc -l\n"
    "  ./build/arksh_test_count_lines <<EOF\n"
    "one\n"
    "two\n"
    "EOF\n"
    "  ./build/arksh_test_emit_args hello 3> out.txt 1>&3\n"
    "\nControl Flow:\n"
    "  bool(true) ? \"yes\" : \"no\"\n"
    "  if bool(false) ; then text(\"no\") -> print() ; else text(\"yes\") -> print() ; fi\n"
    "  while false ; do text(\"no\") -> print() ; done\n"
    "  until true ; do text(\"retry\") -> print() ; done\n"
    "  for n in list(1, 2, 3) ; do n -> value ; done\n"
    "  case . -> type in directory) text(\"dir\") -> print() ;; *) text(\"other\") -> print() ;; esac\n"
    "  switch . -> type ; case \"directory\" ; then text(\"dir\") -> print() ; default ; then text(\"other\") -> print() ; endswitch\n"
    "  multiline blocks also work in REPL and source files\n"
    "\nJob Control:\n"
    "  sleep 30 &\n"
    "  jobs\n"
    "  fg %%1\n"
    "  bg\n"
    "  wait %%1\n"
    "\nExpansions:\n"
    "  cd ~\n"
    "  text(\"%%s\") -> print(\"$HOME\")\n"
    "  text(\"%%s\") -> print($(pwd))\n"
  );
}

int arksh_shell_load_config(ArkshShell *shell, const char *path, char *out, size_t out_size) {
  ArkshPromptConfig config;
  char resolved[ARKSH_MAX_PATH];
  size_t i;
  int plugin_status = 0;
  char plugin_output[ARKSH_MAX_OUTPUT];

  if (shell == NULL || path == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (arksh_platform_resolve_path(shell->cwd, path, resolved, sizeof(resolved)) != 0) {
    snprintf(out, out_size, "unable to resolve config path: %s", path);
    return 1;
  }

  if (arksh_prompt_config_load(&config, resolved) != 0) {
    snprintf(out, out_size, "unable to load config: %s", resolved);
    return 1;
  }

  shell->prompt = config;

  for (i = 0; i < shell->prompt.plugin_count; ++i) {
    if (arksh_shell_load_plugin(shell, shell->prompt.plugins[i], plugin_output, sizeof(plugin_output)) != 0) {
      plugin_status = 1;
    }
  }

  if (plugin_status == 0) {
    snprintf(out, out_size, "prompt config loaded: %s", resolved);
    return 0;
  }

  snprintf(out, out_size, "prompt config loaded with plugin warnings: %s", resolved);
  return 0;
}

int arksh_shell_execute_line(ArkshShell *shell, const char *line, char *out, size_t out_size) {
  ArkshAst ast;
  char expanded_line[ARKSH_MAX_LINE];
  char parse_error[ARKSH_MAX_OUTPUT];
  int status;

  if (shell == NULL || line == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';

  if (is_blank_or_comment_line(line)) {
    shell->last_status = 0;
    return 0;
  }

  if (expand_aliases(shell, line, expanded_line, sizeof(expanded_line), parse_error, sizeof(parse_error)) != 0) {
    snprintf(out, out_size, "%s", parse_error[0] == '\0' ? "alias expansion error" : parse_error);
    shell->last_status = 1;
    return 1;
  }

  {
    char trimmed_line[ARKSH_MAX_LINE];
    int has_newline;

    trim_copy(expanded_line, trimmed_line, sizeof(trimmed_line));
    has_newline = strchr(trimmed_line, '\n') != NULL || strchr(trimmed_line, '\r') != NULL;
    if (!has_newline && !contains_top_level_list_operator(trimmed_line)) {
      if (strcmp(trimmed_line, "let") == 0 ||
          (strncmp(trimmed_line, "let", 3) == 0 && isspace((unsigned char) trimmed_line[3]))) {
        status = handle_let_line(shell, trimmed_line, out, out_size);
        shell->last_status = status;
        return status;
      }
      if (strcmp(trimmed_line, "extend") == 0 ||
          (strncmp(trimmed_line, "extend", 6) == 0 && isspace((unsigned char) trimmed_line[6]))) {
        status = handle_extend_line(shell, trimmed_line, out, out_size);
        shell->last_status = status;
        return status;
      }
      if (strcmp(trimmed_line, "break") == 0 ||
          (strncmp(trimmed_line, "break", 5) == 0 && isspace((unsigned char) trimmed_line[5]))) {
        status = handle_loop_control_line(shell, trimmed_line, "break", ARKSH_CONTROL_SIGNAL_BREAK, out, out_size);
        shell->last_status = status;
        return status;
      }
      if (strcmp(trimmed_line, "continue") == 0 ||
          (strncmp(trimmed_line, "continue", 8) == 0 && isspace((unsigned char) trimmed_line[8]))) {
        status = handle_loop_control_line(shell, trimmed_line, "continue", ARKSH_CONTROL_SIGNAL_CONTINUE, out, out_size);
        shell->last_status = status;
        return status;
      }
      if (strcmp(trimmed_line, "return") == 0 ||
          (strncmp(trimmed_line, "return", 6) == 0 && isspace((unsigned char) trimmed_line[6]))) {
        status = handle_return_line(shell, trimmed_line, out, out_size);
        shell->last_status = status;
        return status;
      }
    }
  }

  if (arksh_parse_line(expanded_line, &ast, parse_error, sizeof(parse_error)) != 0) {
    snprintf(out, out_size, "%s", parse_error[0] == '\0' ? "parse error" : parse_error);
    shell->last_status = 1;
    return 1;
  }

  status = arksh_execute_ast(shell, &ast, out, out_size);
  shell->last_status = status;

  /* E1-S6-T1: fire any pending async signal traps. */
  fire_pending_traps(shell, out, out_size);

  /* E1-S6-T2: ERR trap and errexit — fire only when not in a condition. */
  if (status != 0 && shell->in_condition == 0 && !shell->running_exit_trap) {
    if (shell->traps[ARKSH_TRAP_ERR].active && shell->traps[ARKSH_TRAP_ERR].command[0] != '\0') {
      char err_out[ARKSH_MAX_OUTPUT];
      err_out[0] = '\0';
      /* Temporarily disable ERR trap to avoid recursion. */
      shell->traps[ARKSH_TRAP_ERR].active = 0;
      arksh_shell_execute_line(shell, shell->traps[ARKSH_TRAP_ERR].command, err_out, sizeof(err_out));
      shell->traps[ARKSH_TRAP_ERR].active = 1;
      if (err_out[0] != '\0') {
        arksh_shell_write_output(err_out);
      }
    }
    if (shell->opt_errexit) {
      shell->running = 0;
    }
  }

  return status;
}

int arksh_shell_run_repl(ArkshShell *shell) {
  char line[ARKSH_MAX_LINE];
  char command[ARKSH_MAX_OUTPUT];
  char prompt[ARKSH_MAX_OUTPUT];
  char output[ARKSH_MAX_OUTPUT];
  int interactive;

  if (shell == NULL) {
    return 1;
  }

  interactive = arksh_line_editor_is_interactive();
  if (interactive) {
    configure_shell_signals();
  }

  while (shell->running) {
    command[0] = '\0';
    if (interactive) {
      ArkshLineReadStatus read_status;

      arksh_prompt_render(&shell->prompt, shell, prompt, sizeof(prompt));
      fputs(prompt, stdout);
      fflush(stdout);

      read_status = arksh_line_editor_read_line(
        shell, prompt, shell->prompt.continuation, repl_needs_more,
        command, sizeof(command)
      );
      if (read_status == ARKSH_LINE_READ_EOF) {
        return 0;
      }
      if (read_status == ARKSH_LINE_READ_ERROR) {
        return 1;
      }

      /* Detect and report parse errors that slipped through (e.g. heredoc). */
      if (command[0] != '\0') {
        char parse_error[ARKSH_MAX_OUTPUT];
        int check = command_requires_more_input(command, parse_error, sizeof(parse_error));
        if (check < 0) {
          char message[ARKSH_MAX_OUTPUT];
          snprintf(message, sizeof(message), "arksh: %s",
                   parse_error[0] == '\0' ? "parse error" : parse_error);
          write_buffer(message);
          command[0] = '\0';
        }
      }
    } else {
      int needs_more = 0;
      int pending_heredoc = 0;
      while (1) {
        char parse_error[ARKSH_MAX_OUTPUT];

        if (fgets(line, sizeof(line), stdin) == NULL) {
          if (command[0] != '\0') {
            write_buffer("arksh: incomplete command block");
          }
          return 0;
        }
        trim_trailing_newlines(line);
        if (command[0] == '\0' && is_blank_or_comment_line(line)) {
          break;
        }
        if (!pending_heredoc && is_blank_or_comment_line(line)) {
          continue;
        }
        if (append_command_fragment(command, sizeof(command), line) != 0) {
          write_buffer("arksh: command block too large");
          command[0] = '\0';
          break;
        }

        needs_more = command_requires_more_input(command, parse_error, sizeof(parse_error));
        if (needs_more > 0) {
          pending_heredoc = parse_error_is_unterminated_heredoc(parse_error);
          continue;
        }
        pending_heredoc = 0;
        if (needs_more < 0) {
          char message[ARKSH_MAX_OUTPUT];
          snprintf(message, sizeof(message), "arksh: %s", parse_error[0] == '\0' ? "parse error" : parse_error);
          write_buffer(message);
          command[0] = '\0';
        }
        break;
      }
    }

    if (command[0] == '\0') {
      continue;
    }

    arksh_shell_history_add(shell, command);
    output[0] = '\0';
    arksh_shell_execute_line(shell, command, output, sizeof(output));
    write_buffer(output);
  }

  return 0;
}
