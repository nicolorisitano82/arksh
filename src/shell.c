#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "oosh/executor.h"
#include "oosh/line_editor.h"
#include "oosh/lexer.h"
#include "oosh/parser.h"
#include "oosh/platform.h"
#include "oosh/prompt.h"
#include "oosh/shell.h"

static OoshValue *allocate_runtime_value(char *error, size_t error_size, const char *label);
static int build_class_property_list(const OoshShell *shell, const char *class_name, OoshValue *out_value, char *out, size_t out_size);
static int build_class_method_list(const OoshShell *shell, const char *class_name, OoshValue *out_value, char *out, size_t out_size);

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
  OoshAst ast;
  char parse_error[OOSH_MAX_OUTPUT];
  char trimmed[OOSH_MAX_LINE];
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
  if (oosh_parse_line(text, &ast, parse_error, sizeof(parse_error)) == 0) {
    error[0] = '\0';
    return 0;
  }

  copy_string(error, error_size, parse_error);
  return parse_error_is_incomplete_compound(parse_error) ? 1 : -1;
}

/* Callback wrapper for the line editor: checks if text needs more input. */
static int repl_needs_more(const char *text) {
  char parse_error[OOSH_MAX_OUTPUT];
  return command_requires_more_input(text, parse_error, sizeof(parse_error));
}

static int append_command_fragment(char *command, size_t command_size, const char *fragment) {
  char cleaned[OOSH_MAX_LINE];

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
  char normalized[OOSH_MAX_LINE];
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
  trim_copy(normalized, text, OOSH_MAX_LINE);
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

static OoshShellVar *find_var_entry(OoshShell *shell, const char *name) {
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

static const OoshShellVar *find_var_entry_const(const OoshShell *shell, const char *name) {
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

static OoshValueBinding *find_binding_entry(OoshShell *shell, const char *name) {
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

static const OoshValueBinding *find_binding_entry_const(const OoshShell *shell, const char *name) {
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

static OoshShellFunction *find_function_entry(OoshShell *shell, const char *name) {
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

static const OoshShellFunction *find_function_entry_const(const OoshShell *shell, const char *name) {
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

static OoshClassDef *find_class_entry(OoshShell *shell, const char *name) {
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

static const OoshClassDef *find_class_entry_const(const OoshShell *shell, const char *name) {
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

static OoshClassInstance *find_instance_entry(OoshShell *shell, int id) {
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

static const OoshClassInstance *find_instance_entry_const(const OoshShell *shell, int id) {
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

static OoshAlias *find_alias_entry(OoshShell *shell, const char *name) {
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

static const OoshAlias *find_alias_entry_const(const OoshShell *shell, const char *name) {
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

static int plugin_index_is_active(const OoshShell *shell, int plugin_index) {
  if (shell == NULL || plugin_index < 0) {
    return 1;
  }

  if ((size_t) plugin_index >= shell->plugin_count) {
    return 0;
  }

  return shell->plugins[plugin_index].active != 0;
}

static const OoshCommandDef *find_registered_command(const OoshShell *shell, const char *name) {
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

const OoshValueResolverDef *oosh_shell_find_value_resolver(const OoshShell *shell, const char *name) {
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

const OoshPipelineStageDef *oosh_shell_find_pipeline_stage(const OoshShell *shell, const char *name) {
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

static OoshLoadedPlugin *find_loaded_plugin(OoshShell *shell, const char *query) {
  char resolved[OOSH_MAX_PATH];
  size_t i;
  int have_resolved = 0;

  if (shell == NULL || query == NULL || query[0] == '\0') {
    return NULL;
  }

  if (oosh_platform_resolve_path(shell->cwd, query, resolved, sizeof(resolved)) == 0) {
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

const char *oosh_shell_get_var(const OoshShell *shell, const char *name) {
  const OoshShellVar *entry;

  if (name == NULL || name[0] == '\0') {
    return NULL;
  }

  entry = find_var_entry_const(shell, name);
  if (entry != NULL) {
    return entry->value;
  }

  return getenv(name);
}

const OoshValue *oosh_shell_get_binding(const OoshShell *shell, const char *name) {
  const OoshValueBinding *entry;

  if (name == NULL || name[0] == '\0') {
    return NULL;
  }

  entry = find_binding_entry_const(shell, name);
  return entry == NULL ? NULL : &entry->value;
}

const OoshShellFunction *oosh_shell_find_function(const OoshShell *shell, const char *name) {
  if (name == NULL || name[0] == '\0') {
    return NULL;
  }

  return find_function_entry_const(shell, name);
}

int oosh_shell_set_var(OoshShell *shell, const char *name, const char *value, int exported) {
  OoshShellVar *entry;
  int effective_exported;

  if (shell == NULL || !is_valid_identifier(name)) {
    return 1;
  }

  entry = find_var_entry(shell, name);
  if (entry == NULL) {
    if (shell->var_count >= OOSH_MAX_SHELL_VARS) {
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

int oosh_shell_unset_var(OoshShell *shell, const char *name) {
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

int oosh_shell_set_binding(OoshShell *shell, const char *name, const OoshValue *value) {
  OoshValueBinding *entry;

  if (shell == NULL || !is_valid_identifier(name) || value == NULL) {
    return 1;
  }

  entry = find_binding_entry(shell, name);
  if (entry == NULL) {
    if (shell->binding_count >= OOSH_MAX_VALUE_BINDINGS) {
      return 1;
    }
    entry = &shell->bindings[shell->binding_count++];
    memset(entry, 0, sizeof(*entry));
    copy_string(entry->name, sizeof(entry->name), name);
  }

  oosh_value_free(&entry->value);
  return oosh_value_copy(&entry->value, value);
}

int oosh_shell_unset_binding(OoshShell *shell, const char *name) {
  size_t i;

  if (shell == NULL || !is_valid_identifier(name)) {
    return 1;
  }

  for (i = 0; i < shell->binding_count; ++i) {
    if (strcmp(shell->bindings[i].name, name) == 0) {
      size_t remaining = shell->binding_count - i - 1;

      oosh_value_free(&shell->bindings[i].value);
      if (remaining > 0) {
        memmove(&shell->bindings[i], &shell->bindings[i + 1], remaining * sizeof(shell->bindings[i]));
      }
      shell->binding_count--;
      return 0;
    }
  }

  return 1;
}

int oosh_shell_set_function(OoshShell *shell, const OoshFunctionCommandNode *function_node) {
  OoshShellFunction *entry;
  int i;

  if (shell == NULL || function_node == NULL || !is_valid_identifier(function_node->name)) {
    return 1;
  }

  entry = find_function_entry(shell, function_node->name);
  if (entry == NULL) {
    if (shell->function_count >= OOSH_MAX_FUNCTIONS) {
      return 1;
    }
    entry = &shell->functions[shell->function_count++];
    memset(entry, 0, sizeof(*entry));
  }

  copy_string(entry->name, sizeof(entry->name), function_node->name);
  entry->param_count = function_node->param_count;
  for (i = 0; i < function_node->param_count && i < OOSH_MAX_FUNCTION_PARAMS; ++i) {
    copy_string(entry->params[i], sizeof(entry->params[i]), function_node->params[i]);
  }
  copy_string(entry->body, sizeof(entry->body), function_node->body);
  copy_string(entry->source, sizeof(entry->source), function_node->source);
  return 0;
}

int oosh_shell_register_value_resolver(OoshShell *shell, const char *name, OoshValueResolverFn fn, int is_plugin_resolver) {
  size_t i;

  if (shell == NULL || name == NULL || name[0] == '\0' || fn == NULL || !is_valid_identifier(name)) {
    return 1;
  }

  for (i = 0; i < shell->value_resolver_count; ++i) {
    if (strcmp(shell->value_resolvers[i].name, name) == 0) {
      shell->value_resolvers[i].fn = fn;
      shell->value_resolvers[i].is_plugin_resolver = is_plugin_resolver;
      shell->value_resolvers[i].owner_plugin_index = is_plugin_resolver ? shell->loading_plugin_index : -1;
      return 0;
    }
  }

  if (shell->value_resolver_count >= OOSH_MAX_VALUE_RESOLVERS) {
    return 1;
  }

  copy_string(shell->value_resolvers[shell->value_resolver_count].name, sizeof(shell->value_resolvers[shell->value_resolver_count].name), name);
  shell->value_resolvers[shell->value_resolver_count].fn = fn;
  shell->value_resolvers[shell->value_resolver_count].is_plugin_resolver = is_plugin_resolver;
  shell->value_resolvers[shell->value_resolver_count].owner_plugin_index = is_plugin_resolver ? shell->loading_plugin_index : -1;
  shell->value_resolver_count++;
  return 0;
}

int oosh_shell_register_pipeline_stage(OoshShell *shell, const char *name, OoshPipelineStageFn fn, int is_plugin_stage) {
  size_t i;

  if (shell == NULL || name == NULL || name[0] == '\0' || fn == NULL || !is_valid_identifier(name)) {
    return 1;
  }

  for (i = 0; i < shell->pipeline_stage_count; ++i) {
    if (strcmp(shell->pipeline_stages[i].name, name) == 0) {
      shell->pipeline_stages[i].fn = fn;
      shell->pipeline_stages[i].is_plugin_stage = is_plugin_stage;
      shell->pipeline_stages[i].owner_plugin_index = is_plugin_stage ? shell->loading_plugin_index : -1;
      return 0;
    }
  }

  if (shell->pipeline_stage_count >= OOSH_MAX_PIPELINE_STAGE_HANDLERS) {
    return 1;
  }

  copy_string(shell->pipeline_stages[shell->pipeline_stage_count].name, sizeof(shell->pipeline_stages[shell->pipeline_stage_count].name), name);
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
  char matches[][OOSH_MAX_PATH],
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

  copy_string(matches[*count], OOSH_MAX_PATH, value);
  (*count)++;
}

static void append_member_completion(
  const char *name,
  int is_method,
  const char *prefix,
  char matches[][OOSH_MAX_PATH],
  size_t max_matches,
  size_t *count
) {
  char label[OOSH_MAX_PATH];

  if (name == NULL || prefix == NULL) {
    return;
  }

  snprintf(label, sizeof(label), "%s%s", name, is_method ? "()" : "");
  if (!starts_with_prefix(label, prefix)) {
    return;
  }

  append_completion_match(matches, max_matches, count, label);
}

static int extension_target_matches_value(const OoshObjectExtension *extension, const OoshValue *receiver) {
  if (extension == NULL || receiver == NULL) {
    return 0;
  }

  switch (extension->target_kind) {
    case OOSH_EXTENSION_TARGET_ANY:
      return 1;
    case OOSH_EXTENSION_TARGET_VALUE_KIND:
      return receiver->kind == extension->value_kind;
    case OOSH_EXTENSION_TARGET_OBJECT_KIND:
      return receiver->kind == OOSH_VALUE_OBJECT && receiver->object.kind == extension->object_kind;
    default:
      return 0;
  }
}

static void collect_builtin_member_completions(
  const OoshValue *receiver,
  const char *prefix,
  char matches[][OOSH_MAX_PATH],
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
    "keys", "values", "entries", "get", "has"
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
    case OOSH_VALUE_OBJECT:
      properties = object_properties;
      property_count = sizeof(object_properties) / sizeof(object_properties[0]);
      methods = object_methods;
      method_count = sizeof(object_methods) / sizeof(object_methods[0]);
      break;
    case OOSH_VALUE_STRING:
      properties = string_properties;
      property_count = sizeof(string_properties) / sizeof(string_properties[0]);
      break;
    case OOSH_VALUE_NUMBER:
      properties = number_properties;
      property_count = sizeof(number_properties) / sizeof(number_properties[0]);
      break;
    case OOSH_VALUE_BOOLEAN:
      properties = bool_properties;
      property_count = sizeof(bool_properties) / sizeof(bool_properties[0]);
      break;
    case OOSH_VALUE_BLOCK:
      properties = block_properties;
      property_count = sizeof(block_properties) / sizeof(block_properties[0]);
      methods = block_methods;
      method_count = sizeof(block_methods) / sizeof(block_methods[0]);
      break;
    case OOSH_VALUE_LIST:
      properties = list_properties;
      property_count = sizeof(list_properties) / sizeof(list_properties[0]);
      break;
    case OOSH_VALUE_MAP:
      properties = map_properties;
      property_count = sizeof(map_properties) / sizeof(map_properties[0]);
      methods = map_methods;
      method_count = sizeof(map_methods) / sizeof(map_methods[0]);
      break;
    case OOSH_VALUE_CLASS:
      properties = class_properties;
      property_count = sizeof(class_properties) / sizeof(class_properties[0]);
      methods = class_methods;
      method_count = sizeof(class_methods) / sizeof(class_methods[0]);
      break;
    case OOSH_VALUE_INSTANCE:
      properties = instance_properties;
      property_count = sizeof(instance_properties) / sizeof(instance_properties[0]);
      methods = instance_methods;
      method_count = sizeof(instance_methods) / sizeof(instance_methods[0]);
      break;
    case OOSH_VALUE_EMPTY:
    default:
      properties = empty_properties;
      property_count = sizeof(empty_properties) / sizeof(empty_properties[0]);
      break;
  }

  for (i = 0; i < property_count; ++i) {
    append_member_completion(properties[i], 0, prefix, matches, max_matches, count);
  }
  if (receiver->kind == OOSH_VALUE_MAP) {
    for (i = 0; i < receiver->map.count; ++i) {
      append_member_completion(receiver->map.entries[i].key, 0, prefix, matches, max_matches, count);
    }
  }
  for (i = 0; i < method_count; ++i) {
    append_member_completion(methods[i], 1, prefix, matches, max_matches, count);
  }
}

static void collect_class_runtime_member_completions(
  const OoshShell *shell,
  const OoshValue *receiver,
  const char *prefix,
  char matches[][OOSH_MAX_PATH],
  size_t max_matches,
  size_t *count
) {
  OoshValue names;
  char error[OOSH_MAX_OUTPUT];
  size_t i;

  if (shell == NULL || receiver == NULL || prefix == NULL || matches == NULL || count == NULL) {
    return;
  }
  if (!(receiver->kind == OOSH_VALUE_CLASS || receiver->kind == OOSH_VALUE_INSTANCE)) {
    return;
  }

  error[0] = '\0';
  if (receiver->kind == OOSH_VALUE_CLASS) {
    if (build_class_property_list(shell, receiver->text, &names, error, sizeof(error)) == 0) {
      for (i = 0; i < names.list.count; ++i) {
        append_member_completion(names.list.items[i].text, 0, prefix, matches, max_matches, count);
      }
      oosh_value_free(&names);
    }
  } else {
    const OoshClassInstance *instance = find_instance_entry_const(shell, (int) receiver->number);

    if (instance != NULL) {
      for (i = 0; i < instance->fields.map.count; ++i) {
        append_member_completion(instance->fields.map.entries[i].key, 0, prefix, matches, max_matches, count);
      }
    }
  }

  error[0] = '\0';
  if (build_class_method_list(shell, receiver->text, &names, error, sizeof(error)) == 0) {
    for (i = 0; i < names.list.count; ++i) {
      append_member_completion(names.list.items[i].text, 1, prefix, matches, max_matches, count);
    }
    oosh_value_free(&names);
  }
}

static void collect_extension_member_completions(
  const OoshShell *shell,
  const OoshValue *receiver,
  const char *prefix,
  char matches[][OOSH_MAX_PATH],
  size_t max_matches,
  size_t *count
) {
  size_t i;

  if (shell == NULL || receiver == NULL || prefix == NULL || matches == NULL || count == NULL) {
    return;
  }

  for (i = 0; i < shell->extension_count; ++i) {
    const OoshObjectExtension *extension = &shell->extensions[i];

    if (extension->is_plugin_extension && !plugin_index_is_active(shell, extension->owner_plugin_index)) {
      continue;
    }
    if (!extension_target_matches_value(extension, receiver)) {
      continue;
    }

    append_member_completion(
      extension->name,
      extension->member_kind == OOSH_MEMBER_METHOD,
      prefix,
      matches,
      max_matches,
      count
    );
  }
}

int oosh_shell_collect_member_completions(
  OoshShell *shell,
  const char *receiver_text,
  const char *prefix,
  char matches[][OOSH_MAX_PATH],
  size_t max_matches,
  size_t *out_count
) {
  OoshValue *value;
  OoshObject object;
  char error[OOSH_MAX_OUTPUT];

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

  if (oosh_evaluate_line_value(shell, receiver_text, value, error, sizeof(error)) != 0) {
    if (oosh_object_resolve(shell->cwd, receiver_text, &object) != 0) {
      free(value);
      return 1;
    }
    oosh_value_set_object(value, &object);
  }

  collect_builtin_member_completions(value, prefix, matches, max_matches, out_count);
  collect_class_runtime_member_completions(shell, value, prefix, matches, max_matches, out_count);
  collect_extension_member_completions(shell, value, prefix, matches, max_matches, out_count);
  oosh_value_free(value);
  free(value);
  return *out_count == 0 ? 1 : 0;
}

static int parse_extension_target(
  const char *target,
  OoshExtensionTargetKind *out_kind,
  OoshValueKind *out_value_kind,
  OoshObjectKind *out_object_kind
) {
  if (target == NULL || out_kind == NULL || out_value_kind == NULL || out_object_kind == NULL) {
    return 1;
  }

  *out_kind = OOSH_EXTENSION_TARGET_ANY;
  *out_value_kind = OOSH_VALUE_EMPTY;
  *out_object_kind = OOSH_OBJECT_UNKNOWN;

  if (strcmp(target, "any") == 0) {
    return 0;
  }

  if (strcmp(target, "string") == 0) {
    *out_kind = OOSH_EXTENSION_TARGET_VALUE_KIND;
    *out_value_kind = OOSH_VALUE_STRING;
    return 0;
  }
  if (strcmp(target, "number") == 0) {
    *out_kind = OOSH_EXTENSION_TARGET_VALUE_KIND;
    *out_value_kind = OOSH_VALUE_NUMBER;
    return 0;
  }
  if (strcmp(target, "bool") == 0 || strcmp(target, "boolean") == 0) {
    *out_kind = OOSH_EXTENSION_TARGET_VALUE_KIND;
    *out_value_kind = OOSH_VALUE_BOOLEAN;
    return 0;
  }
  if (strcmp(target, "object") == 0) {
    *out_kind = OOSH_EXTENSION_TARGET_VALUE_KIND;
    *out_value_kind = OOSH_VALUE_OBJECT;
    return 0;
  }
  if (strcmp(target, "block") == 0) {
    *out_kind = OOSH_EXTENSION_TARGET_VALUE_KIND;
    *out_value_kind = OOSH_VALUE_BLOCK;
    return 0;
  }
  if (strcmp(target, "list") == 0 || strcmp(target, "array") == 0) {
    *out_kind = OOSH_EXTENSION_TARGET_VALUE_KIND;
    *out_value_kind = OOSH_VALUE_LIST;
    return 0;
  }
  if (strcmp(target, "map") == 0 || strcmp(target, "dict") == 0 || strcmp(target, "object_map") == 0) {
    *out_kind = OOSH_EXTENSION_TARGET_VALUE_KIND;
    *out_value_kind = OOSH_VALUE_MAP;
    return 0;
  }
  if (strcmp(target, "class") == 0) {
    *out_kind = OOSH_EXTENSION_TARGET_VALUE_KIND;
    *out_value_kind = OOSH_VALUE_CLASS;
    return 0;
  }
  if (strcmp(target, "instance") == 0) {
    *out_kind = OOSH_EXTENSION_TARGET_VALUE_KIND;
    *out_value_kind = OOSH_VALUE_INSTANCE;
    return 0;
  }
  if (strcmp(target, "empty") == 0) {
    *out_kind = OOSH_EXTENSION_TARGET_VALUE_KIND;
    *out_value_kind = OOSH_VALUE_EMPTY;
    return 0;
  }

  if (strcmp(target, "path") == 0) {
    *out_kind = OOSH_EXTENSION_TARGET_OBJECT_KIND;
    *out_object_kind = OOSH_OBJECT_PATH;
    return 0;
  }
  if (strcmp(target, "file") == 0) {
    *out_kind = OOSH_EXTENSION_TARGET_OBJECT_KIND;
    *out_object_kind = OOSH_OBJECT_FILE;
    return 0;
  }
  if (strcmp(target, "directory") == 0 || strcmp(target, "dir") == 0) {
    *out_kind = OOSH_EXTENSION_TARGET_OBJECT_KIND;
    *out_object_kind = OOSH_OBJECT_DIRECTORY;
    return 0;
  }
  if (strcmp(target, "device") == 0) {
    *out_kind = OOSH_EXTENSION_TARGET_OBJECT_KIND;
    *out_object_kind = OOSH_OBJECT_DEVICE;
    return 0;
  }
  if (strcmp(target, "mount") == 0 || strcmp(target, "mount_point") == 0 || strcmp(target, "mount-point") == 0) {
    *out_kind = OOSH_EXTENSION_TARGET_OBJECT_KIND;
    *out_object_kind = OOSH_OBJECT_MOUNT_POINT;
    return 0;
  }

  return 1;
}

static OoshObjectExtension *find_extension_entry(OoshShell *shell, const char *target, OoshMemberKind member_kind, const char *name) {
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
  OoshShell *shell,
  const char *target,
  const char *name,
  OoshMemberKind member_kind,
  OoshExtensionImplKind impl_kind,
  const OoshBlock *block,
  OoshExtensionPropertyFn property_fn,
  OoshExtensionMethodFn method_fn,
  int is_plugin_extension
) {
  OoshObjectExtension *entry;
  OoshExtensionTargetKind target_kind;
  OoshValueKind value_kind;
  OoshObjectKind object_kind;

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
    if (shell->extension_count >= OOSH_MAX_EXTENSIONS) {
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

int oosh_shell_register_block_property_extension(OoshShell *shell, const char *target, const char *name, const OoshBlock *block) {
  if (block == NULL) {
    return 1;
  }
  return register_extension_common(shell, target, name, OOSH_MEMBER_PROPERTY, OOSH_EXTENSION_IMPL_BLOCK, block, NULL, NULL, 0);
}

int oosh_shell_register_block_method_extension(OoshShell *shell, const char *target, const char *name, const OoshBlock *block) {
  if (block == NULL) {
    return 1;
  }
  return register_extension_common(shell, target, name, OOSH_MEMBER_METHOD, OOSH_EXTENSION_IMPL_BLOCK, block, NULL, NULL, 0);
}

int oosh_shell_register_native_property_extension(
  OoshShell *shell,
  const char *target,
  const char *name,
  OoshExtensionPropertyFn fn,
  int is_plugin_extension
) {
  if (fn == NULL) {
    return 1;
  }
  return register_extension_common(shell, target, name, OOSH_MEMBER_PROPERTY, OOSH_EXTENSION_IMPL_NATIVE, NULL, fn, NULL, is_plugin_extension);
}

int oosh_shell_register_native_method_extension(
  OoshShell *shell,
  const char *target,
  const char *name,
  OoshExtensionMethodFn fn,
  int is_plugin_extension
) {
  if (fn == NULL) {
    return 1;
  }
  return register_extension_common(shell, target, name, OOSH_MEMBER_METHOD, OOSH_EXTENSION_IMPL_NATIVE, NULL, NULL, fn, is_plugin_extension);
}

static int receiver_is_json_file_target(const OoshValue *receiver) {
  if (receiver == NULL || receiver->kind != OOSH_VALUE_OBJECT) {
    return 0;
  }

  return receiver->object.kind == OOSH_OBJECT_FILE || receiver->object.kind == OOSH_OBJECT_PATH;
}

static int method_read_json(
  OoshShell *shell,
  const OoshValue *receiver,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
  char *out,
  size_t out_size
) {
  char json_text[OOSH_MAX_OUTPUT];

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
  if (oosh_platform_read_text_file(receiver->object.path, sizeof(json_text) - 1, json_text, sizeof(json_text)) != 0) {
    return 1;
  }
  if (oosh_value_parse_json(json_text, out_value, out, out_size) != 0) {
    return 1;
  }

  return 0;
}

static int method_write_json(
  OoshShell *shell,
  const OoshValue *receiver,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
  char *out,
  size_t out_size
) {
  char json_text[OOSH_MAX_OUTPUT];

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
  if (oosh_value_to_json(&args[0], json_text, sizeof(json_text)) != 0) {
    snprintf(out, out_size, "unable to serialize value as JSON");
    return 1;
  }
  if (oosh_platform_write_text_file(receiver->object.path, json_text, 0, out, out_size) != 0) {
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

static int parse_print_number(const OoshValue *value, double *out_number, char *error, size_t error_size) {
  char rendered[OOSH_MAX_OUTPUT];
  char *endptr = NULL;

  if (value == NULL || out_number == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  switch (value->kind) {
    case OOSH_VALUE_NUMBER:
      *out_number = value->number;
      return 0;
    case OOSH_VALUE_BOOLEAN:
      *out_number = value->boolean ? 1.0 : 0.0;
      return 0;
    case OOSH_VALUE_STRING:
      *out_number = strtod(value->text, &endptr);
      if (endptr == value->text || *endptr != '\0') {
        snprintf(error, error_size, "print() expected a numeric value");
        return 1;
      }
      return 0;
    default:
      if (oosh_value_render(value, rendered, sizeof(rendered)) != 0) {
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
  const OoshValue *value,
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
      if (oosh_value_render(value, out, out_size) != 0) {
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
      if (value->kind == OOSH_VALUE_BOOLEAN) {
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
  const OoshValue *args,
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
      char rendered[OOSH_MAX_OUTPUT];

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
  OoshShell *shell,
  const OoshValue *receiver,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
  char *out,
  size_t out_size
) {
  char rendered[OOSH_MAX_OUTPUT];

  (void) shell;

  if (receiver == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (receiver->kind == OOSH_VALUE_STRING) {
    if (format_print_output(receiver->text, argc, args, rendered, sizeof(rendered), out, out_size) != 0) {
      return 1;
    }
    oosh_value_set_string(out_value, rendered);
    return 0;
  }

  if (argc != 0) {
    snprintf(out, out_size, "print() accepts format arguments only when the receiver is a string");
    return 1;
  }

  if (oosh_value_render(receiver, rendered, sizeof(rendered)) != 0) {
    snprintf(out, out_size, "unable to render receiver for print()");
    return 1;
  }

  oosh_value_set_string(out_value, rendered);
  return 0;
}

const char *oosh_shell_get_alias(const OoshShell *shell, const char *name) {
  const OoshAlias *entry = find_alias_entry_const(shell, name);

  return entry == NULL ? NULL : entry->value;
}

int oosh_shell_set_alias(OoshShell *shell, const char *name, const char *value) {
  OoshAlias *entry;

  if (shell == NULL || !is_valid_alias_name(name)) {
    return 1;
  }

  entry = find_alias_entry(shell, name);
  if (entry == NULL) {
    if (shell->alias_count >= OOSH_MAX_ALIASES) {
      return 1;
    }
    entry = &shell->aliases[shell->alias_count++];
    memset(entry, 0, sizeof(*entry));
    copy_string(entry->name, sizeof(entry->name), name);
  }

  copy_string(entry->value, sizeof(entry->value), value == NULL ? "" : value);
  return 0;
}

int oosh_shell_unset_alias(OoshShell *shell, const char *name) {
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

static int map_add_value_entry(OoshValue *map, const char *key, const OoshValue *entry_value) {
  return oosh_value_map_set(map, key, entry_value);
}

static int map_add_string_entry(OoshValue *map, const char *key, const char *text) {
  OoshValue *entry;
  int status;

  entry = (OoshValue *) calloc(1, sizeof(*entry));
  if (entry == NULL) {
    return 1;
  }

  oosh_value_set_string(entry, text);
  status = oosh_value_map_set(map, key, entry);
  oosh_value_free(entry);
  free(entry);
  return status;
}

static int map_add_number_entry(OoshValue *map, const char *key, double number) {
  OoshValue *entry;
  int status;

  entry = (OoshValue *) calloc(1, sizeof(*entry));
  if (entry == NULL) {
    return 1;
  }

  oosh_value_set_number(entry, number);
  status = oosh_value_map_set(map, key, entry);
  oosh_value_free(entry);
  free(entry);
  return status;
}

static int map_add_bool_entry(OoshValue *map, const char *key, int boolean) {
  OoshValue *entry;
  int status;

  entry = (OoshValue *) calloc(1, sizeof(*entry));
  if (entry == NULL) {
    return 1;
  }

  oosh_value_set_boolean(entry, boolean);
  status = oosh_value_map_set(map, key, entry);
  oosh_value_free(entry);
  free(entry);
  return status;
}

static const char *job_state_name(OoshJobState state) {
  switch (state) {
    case OOSH_JOB_STOPPED:
      return "stopped";
    case OOSH_JOB_DONE:
      return "done";
    case OOSH_JOB_RUNNING:
    default:
      return "running";
  }
}

static int render_argument_key(const OoshValue *value, char *out, size_t out_size, char *error, size_t error_size) {
  if (value == NULL || out == NULL || out_size == 0 || error == NULL || error_size == 0) {
    return 1;
  }

  if (oosh_value_render(value, out, out_size) != 0) {
    snprintf(error, error_size, "unable to render map key");
    return 1;
  }
  return 0;
}

static OoshValue *allocate_runtime_value(char *error, size_t error_size, const char *label) {
  OoshValue *value = (OoshValue *) calloc(1, sizeof(*value));

  if (value == NULL && error != NULL && error_size > 0) {
    snprintf(error, error_size, "unable to allocate %s", label == NULL ? "runtime value" : label);
  }
  return value;
}

static void free_class_definition_contents(OoshClassDef *class_def) {
  size_t i;

  if (class_def == NULL) {
    return;
  }

  for (i = 0; i < class_def->property_count; ++i) {
    oosh_value_free(&class_def->properties[i].default_value);
  }
  memset(class_def, 0, sizeof(*class_def));
}

static const OoshClassDef *resolve_class_for_lookup(const OoshShell *shell, const OoshClassDef *pending, const char *name) {
  if (pending != NULL && name != NULL && strcmp(pending->name, name) == 0) {
    return pending;
  }
  return find_class_entry_const(shell, name);
}

static int class_chain_contains(
  const OoshShell *shell,
  const OoshClassDef *pending,
  const char *start_name,
  const char *target_name,
  int depth
) {
  const OoshClassDef *start_class;
  int i;

  if (shell == NULL || start_name == NULL || target_name == NULL || depth > OOSH_MAX_CLASSES) {
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

static const OoshClassProperty *find_property_in_class(const OoshClassDef *class_def, const char *name) {
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

static const OoshClassMethod *find_method_in_class(const OoshClassDef *class_def, const char *name) {
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

static const OoshClassProperty *lookup_property_recursive(
  const OoshShell *shell,
  const char *class_name,
  const char *property_name,
  int depth
) {
  const OoshClassDef *class_def;
  const OoshClassProperty *property;
  int i;

  if (shell == NULL || class_name == NULL || property_name == NULL || depth > OOSH_MAX_CLASSES) {
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

static const OoshClassMethod *lookup_method_recursive(
  const OoshShell *shell,
  const char *class_name,
  const char *method_name,
  int depth
) {
  const OoshClassDef *class_def;
  const OoshClassMethod *method;
  int i;

  if (shell == NULL || class_name == NULL || method_name == NULL || depth > OOSH_MAX_CLASSES) {
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
  OoshMemberKind *out_member_kind,
  char *name,
  size_t name_size,
  char *expression,
  size_t expression_size
) {
  char trimmed[OOSH_MAX_LINE];
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
    *out_member_kind = OOSH_MEMBER_PROPERTY;
    cursor += 8;
  } else if (strncmp(cursor, "method", 6) == 0 && isspace((unsigned char) cursor[6])) {
    *out_member_kind = OOSH_MEMBER_METHOD;
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
    char raw_expression[OOSH_MAX_LINE];

    copy_string(raw_expression, sizeof(raw_expression), cursor + operator_index + 1);
    trim_copy(raw_expression, expression, expression_size);
  }
  return expression[0] == '\0' ? 2 : 0;
}

static int class_definition_add_property(OoshClassDef *class_def, const char *name, const OoshValue *value) {
  size_t i;

  if (class_def == NULL || name == NULL || value == NULL) {
    return 1;
  }

  for (i = 0; i < class_def->property_count; ++i) {
    if (strcmp(class_def->properties[i].name, name) == 0) {
      oosh_value_free(&class_def->properties[i].default_value);
      return oosh_value_copy(&class_def->properties[i].default_value, value);
    }
  }

  if (class_def->property_count >= OOSH_MAX_CLASS_PROPERTIES) {
    return 1;
  }

  copy_string(class_def->properties[class_def->property_count].name, sizeof(class_def->properties[class_def->property_count].name), name);
  if (oosh_value_copy(&class_def->properties[class_def->property_count].default_value, value) != 0) {
    return 1;
  }
  class_def->property_count++;
  return 0;
}

static int class_definition_add_method(OoshClassDef *class_def, const char *name, const OoshBlock *block) {
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

  if (class_def->method_count >= OOSH_MAX_CLASS_METHODS) {
    return 1;
  }

  copy_string(class_def->methods[class_def->method_count].name, sizeof(class_def->methods[class_def->method_count].name), name);
  class_def->methods[class_def->method_count].block = *block;
  class_def->method_count++;
  return 0;
}

const OoshClassDef *oosh_shell_find_class(const OoshShell *shell, const char *name) {
  if (name == NULL || name[0] == '\0') {
    return NULL;
  }

  return find_class_entry_const(shell, name);
}

const OoshClassInstance *oosh_shell_find_instance(const OoshShell *shell, int id) {
  return find_instance_entry_const(shell, id);
}

static int build_class_name_list_recursive(
  const OoshShell *shell,
  const char *class_name,
  int want_methods,
  OoshValue *out_list,
  int depth
) {
  const OoshClassDef *class_def;
  size_t i;

  if (shell == NULL || class_name == NULL || out_list == NULL || depth > OOSH_MAX_CLASSES) {
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

int oosh_shell_set_class(OoshShell *shell, const OoshClassCommandNode *class_node, char *out, size_t out_size) {
  OoshClassDef *candidate;
  OoshClassDef *entry;
  size_t offset = 0;

  if (shell == NULL || class_node == NULL || out == NULL || out_size == 0 || !is_valid_identifier(class_node->name)) {
    return 1;
  }

  candidate = (OoshClassDef *) calloc(1, sizeof(*candidate));
  if (candidate == NULL) {
    snprintf(out, out_size, "unable to allocate class definition");
    return 1;
  }

  copy_string(candidate->name, sizeof(candidate->name), class_node->name);
  copy_string(candidate->source, sizeof(candidate->source), class_node->source);
  candidate->base_count = class_node->base_count;
  for (int i = 0; i < class_node->base_count && i < OOSH_MAX_CLASS_BASES; ++i) {
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
    char segment[OOSH_MAX_LINE];
    OoshMemberKind member_kind;
    char name[OOSH_MAX_NAME];
    char expression[OOSH_MAX_LINE];

    if (find_next_class_body_separator(class_node->body, offset, &end) != 0) {
      end = strlen(class_node->body);
    }

    copy_string(segment, sizeof(segment), "");
    append_slice(segment, sizeof(segment), class_node->body, offset, end);
    trim_copy(segment, segment, sizeof(segment));
    if (segment[0] != '\0' && segment[0] != '#') {
      OoshValue *value = allocate_runtime_value(out, out_size, "class member value");

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

      if (oosh_evaluate_line_value(shell, expression, value, out, out_size) != 0) {
        free(value);
        free_class_definition_contents(candidate);
        free(candidate);
        return 1;
      }

      if (member_kind == OOSH_MEMBER_PROPERTY) {
        if (class_definition_add_property(candidate, name, value) != 0) {
          oosh_value_free(value);
          free(value);
          free_class_definition_contents(candidate);
          free(candidate);
          snprintf(out, out_size, "unable to register class property: %s", name);
          return 1;
        }
      } else {
        if (value->kind != OOSH_VALUE_BLOCK) {
          oosh_value_free(value);
          free(value);
          free_class_definition_contents(candidate);
          free(candidate);
          snprintf(out, out_size, "class methods must be block values: %s", name);
          return 1;
        }
        if (class_definition_add_method(candidate, name, &value->block) != 0) {
          oosh_value_free(value);
          free(value);
          free_class_definition_contents(candidate);
          free(candidate);
          snprintf(out, out_size, "unable to register class method: %s", name);
          return 1;
        }
      }

      oosh_value_free(value);
      free(value);
    }

    if (class_node->body[end] == '\0') {
      break;
    }
    offset = end + 1;
  }

  entry = find_class_entry(shell, candidate->name);
  if (entry == NULL) {
    if (shell->class_count >= OOSH_MAX_CLASSES) {
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
  OoshShell *shell,
  const char *class_name,
  OoshValue *fields,
  char *out,
  size_t out_size,
  int depth
) {
  const OoshClassDef *class_def;
  size_t i;

  if (shell == NULL || class_name == NULL || fields == NULL || out == NULL || out_size == 0 || depth > OOSH_MAX_CLASSES) {
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
    if (oosh_value_map_set(fields, class_def->properties[i].name, &class_def->properties[i].default_value) != 0) {
      snprintf(out, out_size, "instance field map is too large");
      return 1;
    }
  }

  return 0;
}

static int rollback_last_instance(OoshShell *shell) {
  if (shell == NULL || shell->instance_count == 0) {
    return 1;
  }

  oosh_value_free(&shell->instances[shell->instance_count - 1].fields);
  memset(&shell->instances[shell->instance_count - 1], 0, sizeof(shell->instances[shell->instance_count - 1]));
  shell->instance_count--;
  return 0;
}

static int class_is_a_recursive(const OoshShell *shell, const char *class_name, const char *target_name, int depth) {
  const OoshClassDef *class_def;
  int i;

  if (shell == NULL || class_name == NULL || target_name == NULL || depth > OOSH_MAX_CLASSES) {
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

int oosh_shell_instantiate_class(
  OoshShell *shell,
  const char *name,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
  char *out,
  size_t out_size
) {
  const OoshClassDef *class_def;
  OoshClassInstance *instance;
  OoshValue *instance_value;

  if (shell == NULL || name == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  class_def = find_class_entry_const(shell, name);
  if (class_def == NULL) {
    snprintf(out, out_size, "unknown class: %s", name);
    return 1;
  }
  if (shell->instance_count >= OOSH_MAX_INSTANCES) {
    snprintf(out, out_size, "instance limit reached");
    return 1;
  }

  instance = &shell->instances[shell->instance_count++];
  memset(instance, 0, sizeof(*instance));
  instance->id = shell->next_instance_id++;
  copy_string(instance->class_name, sizeof(instance->class_name), name);
  oosh_value_set_map(&instance->fields);

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

  oosh_value_set_instance(instance_value, name, instance->id);
  if (lookup_method_recursive(shell, name, "init", 0) != NULL) {
    OoshValue *ignored_result = allocate_runtime_value(out, out_size, "constructor result");

    if (ignored_result == NULL) {
      free(instance_value);
      rollback_last_instance(shell);
      return 1;
    }
    if (oosh_shell_call_class_method(shell, instance_value, "init", argc, args, ignored_result, out, out_size) != 0) {
      oosh_value_free(ignored_result);
      free(ignored_result);
      free(instance_value);
      rollback_last_instance(shell);
      return 1;
    }
    oosh_value_free(ignored_result);
    free(ignored_result);
  } else if (argc != 0) {
    free(instance_value);
    rollback_last_instance(shell);
    snprintf(out, out_size, "class %s does not define init(), so constructor arguments are not accepted", name);
    return 1;
  }

  oosh_value_set_instance(out_value, name, instance->id);
  free(instance_value);
  out[0] = '\0';
  return 0;
}

static int build_class_property_list(const OoshShell *shell, const char *class_name, OoshValue *out_value, char *out, size_t out_size) {
  OoshValue *seen;
  size_t i;

  if (shell == NULL || class_name == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  oosh_value_init(out_value);
  out_value->kind = OOSH_VALUE_LIST;
  seen = allocate_runtime_value(out, out_size, "class property set");
  if (seen == NULL) {
    return 1;
  }

  oosh_value_set_map(seen);
  if (build_class_name_list_recursive(shell, class_name, 0, seen, 0) != 0) {
    oosh_value_free(seen);
    free(seen);
    snprintf(out, out_size, "unable to build class property list");
    return 1;
  }

  for (i = 0; i < seen->map.count; ++i) {
    OoshValue *item = allocate_runtime_value(out, out_size, "class property item");

    if (item == NULL) {
      oosh_value_free(seen);
      free(seen);
      return 1;
    }
    oosh_value_set_string(item, seen->map.entries[i].key);
    if (oosh_value_list_append_value(out_value, item) != 0) {
      oosh_value_free(item);
      free(item);
      oosh_value_free(seen);
      free(seen);
      snprintf(out, out_size, "class method list is too large");
      return 1;
    }
    oosh_value_free(item);
    free(item);
  }

  oosh_value_free(seen);
  free(seen);
  return 0;
}

static int build_class_method_list(const OoshShell *shell, const char *class_name, OoshValue *out_value, char *out, size_t out_size) {
  OoshValue *seen;
  size_t i;

  if (shell == NULL || class_name == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  oosh_value_init(out_value);
  out_value->kind = OOSH_VALUE_LIST;
  seen = allocate_runtime_value(out, out_size, "class method set");
  if (seen == NULL) {
    return 1;
  }

  oosh_value_set_map(seen);
  if (build_class_name_list_recursive(shell, class_name, 1, seen, 0) != 0 ||
      map_add_string_entry(seen, "new", "method") != 0) {
    oosh_value_free(seen);
    free(seen);
    snprintf(out, out_size, "unable to build class method list");
    return 1;
  }

  for (i = 0; i < seen->map.count; ++i) {
    OoshValue *item = allocate_runtime_value(out, out_size, "class method item");

    if (item == NULL) {
      oosh_value_free(seen);
      free(seen);
      return 1;
    }
    oosh_value_set_string(item, seen->map.entries[i].key);
    if (oosh_value_list_append_value(out_value, item) != 0) {
      oosh_value_free(item);
      free(item);
      oosh_value_free(seen);
      free(seen);
      snprintf(out, out_size, "class property list is too large");
      return 1;
    }
    oosh_value_free(item);
    free(item);
  }

  oosh_value_free(seen);
  free(seen);
  return 0;
}

int oosh_shell_get_class_property_value(
  OoshShell *shell,
  const OoshValue *receiver,
  const char *property,
  OoshValue *out_value,
  char *out,
  size_t out_size
) {
  if (shell == NULL || receiver == NULL || property == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (receiver->kind == OOSH_VALUE_CLASS) {
    const OoshClassDef *class_def = find_class_entry_const(shell, receiver->text);
    size_t i;

    if (class_def == NULL) {
      snprintf(out, out_size, "unknown class: %s", receiver->text);
      return 1;
    }

    if (strcmp(property, "type") == 0 || strcmp(property, "value_type") == 0) {
      oosh_value_set_string(out_value, strcmp(property, "type") == 0 ? "class" : "class");
      return 0;
    }
    if (strcmp(property, "name") == 0) {
      oosh_value_set_string(out_value, class_def->name);
      return 0;
    }
    if (strcmp(property, "source") == 0) {
      oosh_value_set_string(out_value, class_def->source);
      return 0;
    }
    if (strcmp(property, "base_count") == 0) {
      oosh_value_set_number(out_value, (double) class_def->base_count);
      return 0;
    }
    if (strcmp(property, "bases") == 0) {
      oosh_value_init(out_value);
      out_value->kind = OOSH_VALUE_LIST;
      for (i = 0; i < (size_t) class_def->base_count; ++i) {
        OoshValue *base_value = allocate_runtime_value(out, out_size, "base class value");

        if (base_value == NULL) {
          return 1;
        }
        oosh_value_set_string(base_value, class_def->bases[i]);
        if (oosh_value_list_append_value(out_value, base_value) != 0) {
          oosh_value_free(base_value);
          free(base_value);
          snprintf(out, out_size, "base class list is too large");
          return 1;
        }
        oosh_value_free(base_value);
        free(base_value);
      }
      return 0;
    }
    if (strcmp(property, "property_count") == 0) {
      OoshValue *names = allocate_runtime_value(out, out_size, "class property names");

      if (names == NULL) {
        return 1;
      }
      if (build_class_property_list(shell, class_def->name, names, out, out_size) != 0) {
        free(names);
        return 1;
      }
      oosh_value_set_number(out_value, (double) names->list.count);
      oosh_value_free(names);
      free(names);
      return 0;
    }
    if (strcmp(property, "method_count") == 0) {
      OoshValue *names = allocate_runtime_value(out, out_size, "class method names");

      if (names == NULL) {
        return 1;
      }
      if (build_class_method_list(shell, class_def->name, names, out, out_size) != 0) {
        free(names);
        return 1;
      }
      oosh_value_set_number(out_value, (double) names->list.count);
      oosh_value_free(names);
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

  if (receiver->kind == OOSH_VALUE_INSTANCE) {
    const OoshClassInstance *instance = find_instance_entry_const(shell, (int) receiver->number);
    const OoshValueItem *field_item;

    if (instance == NULL) {
      snprintf(out, out_size, "unknown instance: %s#%d", receiver->text, (int) receiver->number);
      return 1;
    }

    if (strcmp(property, "type") == 0) {
      oosh_value_set_string(out_value, instance->class_name);
      return 0;
    }
    if (strcmp(property, "value_type") == 0) {
      oosh_value_set_string(out_value, "instance");
      return 0;
    }
    if (strcmp(property, "id") == 0) {
      oosh_value_set_number(out_value, receiver->number);
      return 0;
    }
    if (strcmp(property, "class") == 0 || strcmp(property, "class_name") == 0) {
      oosh_value_set_string(out_value, instance->class_name);
      return 0;
    }
    if (strcmp(property, "fields") == 0) {
      return oosh_value_copy(out_value, &instance->fields);
    }
    if (strcmp(property, "properties") == 0) {
      return build_class_property_list(shell, instance->class_name, out_value, out, out_size);
    }
    if (strcmp(property, "methods") == 0) {
      return build_class_method_list(shell, instance->class_name, out_value, out, out_size);
    }
    if (strcmp(property, "property_count") == 0) {
      oosh_value_set_number(out_value, (double) instance->fields.map.count);
      return 0;
    }

    field_item = oosh_value_map_get_item(&instance->fields, property);
    if (field_item != NULL) {
      return oosh_value_set_from_item(out_value, field_item);
    }

    {
      const OoshClassProperty *property_def = lookup_property_recursive(shell, instance->class_name, property, 0);

      if (property_def != NULL) {
        return oosh_value_copy(out_value, &property_def->default_value);
      }
    }

    snprintf(out, out_size, "unknown property: %s", property);
    return 1;
  }

  snprintf(out, out_size, "receiver is not a class or instance");
  return 1;
}

int oosh_shell_call_class_method(
  OoshShell *shell,
  const OoshValue *receiver,
  const char *method,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
  char *out,
  size_t out_size
) {
  if (shell == NULL || receiver == NULL || method == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (receiver->kind == OOSH_VALUE_CLASS) {
    if (strcmp(method, "new") == 0) {
      return oosh_shell_instantiate_class(shell, receiver->text, argc, args, out_value, out, out_size);
    }

    snprintf(out, out_size, "unknown method: %s", method);
    return 1;
  }

  if (receiver->kind != OOSH_VALUE_INSTANCE) {
    snprintf(out, out_size, "receiver is not a class instance");
    return 1;
  }

  {
    OoshClassInstance *instance = find_instance_entry(shell, (int) receiver->number);

    if (instance == NULL) {
      snprintf(out, out_size, "unknown instance: %s#%d", receiver->text, (int) receiver->number);
      return 1;
    }

    if (strcmp(method, "set") == 0) {
      char key[OOSH_MAX_NAME];

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
      if (oosh_value_map_set(&instance->fields, key, &args[1]) != 0) {
        snprintf(out, out_size, "unable to set instance field: %s", key);
        return 1;
      }
      oosh_value_set_instance(out_value, instance->class_name, instance->id);
      return 0;
    }

    if (strcmp(method, "get") == 0) {
      char key[OOSH_MAX_NAME];
      OoshValue *property_value;

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
      if (oosh_shell_get_class_property_value(shell, receiver, key, property_value, out, out_size) != 0) {
        free(property_value);
        return 1;
      }
      *out_value = *property_value;
      free(property_value);
      return 0;
    }

    if (strcmp(method, "isa") == 0) {
      char class_name[OOSH_MAX_NAME];

      if (argc != 1) {
        snprintf(out, out_size, "isa() expects exactly one class name");
        return 1;
      }
      if (render_argument_key(&args[0], class_name, sizeof(class_name), out, out_size) != 0) {
        return 1;
      }
      oosh_value_set_boolean(out_value, class_is_a_recursive(shell, instance->class_name, class_name, 0));
      return 0;
    }

    {
      const OoshClassMethod *method_def = lookup_method_recursive(shell, instance->class_name, method, 0);
      OoshValue *block_args;
      int i;
      int status;

      if (method_def == NULL) {
        snprintf(out, out_size, "unknown method: %s", method);
        return 1;
      }

      if (argc + 1 > OOSH_MAX_ARGS) {
        snprintf(out, out_size, "too many method arguments");
        return 1;
      }

      block_args = (OoshValue *) calloc((size_t) argc + 1, sizeof(*block_args));
      if (block_args == NULL) {
        snprintf(out, out_size, "unable to allocate class method arguments");
        return 1;
      }

      oosh_value_set_instance(&block_args[0], instance->class_name, instance->id);
      for (i = 0; i < argc; ++i) {
        if (oosh_value_copy(&block_args[i + 1], &args[i]) != 0) {
          int rollback;

          for (rollback = 0; rollback <= i; ++rollback) {
            oosh_value_free(&block_args[rollback]);
          }
          free(block_args);
          snprintf(out, out_size, "unable to prepare class method arguments");
          return 1;
        }
      }

      status = oosh_execute_block(shell, &method_def->block, block_args, argc + 1, out_value, out, out_size);
      for (i = 0; i < argc + 1; ++i) {
        oosh_value_free(&block_args[i]);
      }
      free(block_args);
      return status;
    }
  }
}

static int resolver_map(
  OoshShell *shell,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
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

  oosh_value_set_map(out_value);
  for (i = 0; i < argc; i += 2) {
    char key[OOSH_MAX_NAME];

    if (render_argument_key(&args[i], key, sizeof(key), error, error_size) != 0) {
      return 1;
    }
    if (oosh_value_map_set(out_value, key, &args[i + 1]) != 0) {
      snprintf(error, error_size, "map() is too large");
      return 1;
    }
  }

  return 0;
}

static int resolver_env(
  OoshShell *shell,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
  char *error,
  size_t error_size
) {
  if (shell == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (argc == 1) {
    char key[OOSH_MAX_NAME];
    const char *value;

    if (render_argument_key(&args[0], key, sizeof(key), error, error_size) != 0) {
      return 1;
    }
    value = oosh_shell_get_var(shell, key);
    if (value == NULL) {
      oosh_value_init(out_value);
      return 0;
    }
    oosh_value_set_string(out_value, value);
    return 0;
  }
  if (argc != 0) {
    snprintf(error, error_size, "env() accepts zero arguments or a single key");
    return 1;
  }

  oosh_value_set_map(out_value);
  {
    OoshPlatformEnvEntry entries[OOSH_MAX_SHELL_VARS + 64];
    size_t count = 0;
    size_t i;

    if (oosh_platform_list_environment(entries, sizeof(entries) / sizeof(entries[0]), &count) != 0) {
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
  OoshShell *shell,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
  char *error,
  size_t error_size
) {
  OoshPlatformProcessInfo info;
  char hostname[OOSH_MAX_NAME];

  (void) args;

  if (shell == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (argc != 0) {
    snprintf(error, error_size, "proc() does not accept arguments");
    return 1;
  }
  if (oosh_platform_get_process_info(&info) != 0) {
    snprintf(error, error_size, "unable to inspect current process");
    return 1;
  }

  oosh_value_set_map(out_value);
  hostname[0] = '\0';
  oosh_platform_gethostname(hostname, sizeof(hostname));
  if (map_add_number_entry(out_value, "pid", (double) info.pid) != 0 ||
      map_add_number_entry(out_value, "ppid", (double) info.ppid) != 0 ||
      map_add_string_entry(out_value, "cwd", shell->cwd) != 0 ||
      map_add_string_entry(out_value, "host", hostname) != 0 ||
      map_add_string_entry(out_value, "os", oosh_platform_os_name()) != 0) {
    snprintf(error, error_size, "process namespace is too large");
    return 1;
  }

  return 0;
}

static int resolver_shell_namespace(
  OoshShell *shell,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
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

  oosh_value_set_map(out_value);
  if (map_add_string_entry(out_value, "cwd", shell->cwd) != 0 ||
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
      map_add_string_entry(out_value, "os", oosh_platform_os_name()) != 0) {
    snprintf(error, error_size, "shell namespace is too large");
    return 1;
  }

  {
    OoshValue *vars = allocate_runtime_value(error, error_size, "shell vars namespace");
    OoshValue *bindings = allocate_runtime_value(error, error_size, "shell bindings namespace");
    OoshValue *aliases = allocate_runtime_value(error, error_size, "shell aliases namespace");
    OoshValue *classes = allocate_runtime_value(error, error_size, "shell classes namespace");
    OoshValue *instances = allocate_runtime_value(error, error_size, "shell instances namespace");
    OoshValue *plugins = allocate_runtime_value(error, error_size, "shell plugins namespace");
    OoshValue *jobs = allocate_runtime_value(error, error_size, "shell jobs namespace");

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

    oosh_value_set_map(vars);
    oosh_value_set_map(bindings);
    oosh_value_set_map(aliases);
    oosh_value_init(classes);
    classes->kind = OOSH_VALUE_LIST;
    oosh_value_init(instances);
    instances->kind = OOSH_VALUE_LIST;
    oosh_value_init(plugins);
    plugins->kind = OOSH_VALUE_LIST;
    oosh_value_init(jobs);
    jobs->kind = OOSH_VALUE_LIST;

    for (i = 0; i < shell->var_count; ++i) {
      if (map_add_string_entry(vars, shell->vars[i].name, shell->vars[i].value) != 0) {
        snprintf(error, error_size, "shell namespace is too large");
        oosh_value_free(vars);
        oosh_value_free(bindings);
        oosh_value_free(aliases);
        oosh_value_free(classes);
        oosh_value_free(instances);
        oosh_value_free(plugins);
        oosh_value_free(jobs);
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
        oosh_value_free(vars);
        oosh_value_free(bindings);
        oosh_value_free(aliases);
        oosh_value_free(classes);
        oosh_value_free(instances);
        oosh_value_free(plugins);
        oosh_value_free(jobs);
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
        oosh_value_free(vars);
        oosh_value_free(bindings);
        oosh_value_free(aliases);
        oosh_value_free(classes);
        oosh_value_free(instances);
        oosh_value_free(plugins);
        oosh_value_free(jobs);
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
      OoshValue *class_entry = allocate_runtime_value(error, error_size, "shell class entry");

      if (class_entry == NULL) {
        oosh_value_free(vars);
        oosh_value_free(bindings);
        oosh_value_free(aliases);
        oosh_value_free(classes);
        oosh_value_free(instances);
        oosh_value_free(plugins);
        oosh_value_free(jobs);
        free(vars);
        free(bindings);
        free(aliases);
        free(classes);
        free(instances);
        free(plugins);
        free(jobs);
        return 1;
      }

      oosh_value_set_map(class_entry);
      if (map_add_string_entry(class_entry, "name", shell->classes[i].name) != 0 ||
          map_add_number_entry(class_entry, "base_count", (double) shell->classes[i].base_count) != 0 ||
          map_add_number_entry(class_entry, "property_count", (double) shell->classes[i].property_count) != 0 ||
          map_add_number_entry(class_entry, "method_count", (double) shell->classes[i].method_count) != 0 ||
          oosh_value_list_append_value(classes, class_entry) != 0) {
        oosh_value_free(class_entry);
        free(class_entry);
        snprintf(error, error_size, "shell namespace is too large");
        oosh_value_free(vars);
        oosh_value_free(bindings);
        oosh_value_free(aliases);
        oosh_value_free(classes);
        oosh_value_free(instances);
        oosh_value_free(plugins);
        oosh_value_free(jobs);
        free(vars);
        free(bindings);
        free(aliases);
        free(classes);
        free(instances);
        free(plugins);
        free(jobs);
        return 1;
      }
      oosh_value_free(class_entry);
      free(class_entry);
    }
    for (i = 0; i < shell->instance_count; ++i) {
      OoshValue *instance_entry = allocate_runtime_value(error, error_size, "shell instance entry");

      if (instance_entry == NULL) {
        oosh_value_free(vars);
        oosh_value_free(bindings);
        oosh_value_free(aliases);
        oosh_value_free(classes);
        oosh_value_free(instances);
        oosh_value_free(plugins);
        oosh_value_free(jobs);
        free(vars);
        free(bindings);
        free(aliases);
        free(classes);
        free(instances);
        free(plugins);
        free(jobs);
        return 1;
      }

      oosh_value_set_map(instance_entry);
      if (map_add_number_entry(instance_entry, "id", (double) shell->instances[i].id) != 0 ||
          map_add_string_entry(instance_entry, "class", shell->instances[i].class_name) != 0 ||
          oosh_value_list_append_value(instances, instance_entry) != 0) {
        oosh_value_free(instance_entry);
        free(instance_entry);
        snprintf(error, error_size, "shell namespace is too large");
        oosh_value_free(vars);
        oosh_value_free(bindings);
        oosh_value_free(aliases);
        oosh_value_free(classes);
        oosh_value_free(instances);
        oosh_value_free(plugins);
        oosh_value_free(jobs);
        free(vars);
        free(bindings);
        free(aliases);
        free(classes);
        free(instances);
        free(plugins);
        free(jobs);
        return 1;
      }
      oosh_value_free(instance_entry);
      free(instance_entry);
    }
    for (i = 0; i < shell->plugin_count; ++i) {
      OoshValue *plugin_entry = allocate_runtime_value(error, error_size, "shell plugin entry");

      if (plugin_entry == NULL) {
        oosh_value_free(vars);
        oosh_value_free(bindings);
        oosh_value_free(aliases);
        oosh_value_free(classes);
        oosh_value_free(instances);
        oosh_value_free(plugins);
        oosh_value_free(jobs);
        free(vars);
        free(bindings);
        free(aliases);
        free(classes);
        free(instances);
        free(plugins);
        free(jobs);
        return 1;
      }

      oosh_value_set_map(plugin_entry);
      if (map_add_string_entry(plugin_entry, "name", shell->plugins[i].name) != 0 ||
          map_add_string_entry(plugin_entry, "version", shell->plugins[i].version) != 0 ||
          map_add_string_entry(plugin_entry, "description", shell->plugins[i].description) != 0 ||
          map_add_string_entry(plugin_entry, "path", shell->plugins[i].path) != 0 ||
          map_add_bool_entry(plugin_entry, "active", shell->plugins[i].active) != 0 ||
          oosh_value_list_append_value(plugins, plugin_entry) != 0) {
        oosh_value_free(plugin_entry);
        free(plugin_entry);
        snprintf(error, error_size, "shell namespace is too large");
        oosh_value_free(vars);
        oosh_value_free(bindings);
        oosh_value_free(aliases);
        oosh_value_free(classes);
        oosh_value_free(instances);
        oosh_value_free(plugins);
        oosh_value_free(jobs);
        free(vars);
        free(bindings);
        free(aliases);
        free(classes);
        free(instances);
        free(plugins);
        free(jobs);
        return 1;
      }
      oosh_value_free(plugin_entry);
      free(plugin_entry);
    }
    for (i = 0; i < shell->job_count; ++i) {
      OoshValue *job_entry = allocate_runtime_value(error, error_size, "shell job entry");

      if (job_entry == NULL) {
        oosh_value_free(vars);
        oosh_value_free(bindings);
        oosh_value_free(aliases);
        oosh_value_free(plugins);
        oosh_value_free(jobs);
        free(vars);
        free(bindings);
        free(aliases);
        free(plugins);
        free(jobs);
        return 1;
      }

      oosh_value_set_map(job_entry);
      if (map_add_number_entry(job_entry, "id", (double) shell->jobs[i].id) != 0 ||
          map_add_string_entry(job_entry, "state", job_state_name(shell->jobs[i].state)) != 0 ||
          map_add_number_entry(job_entry, "exit_code", (double) shell->jobs[i].exit_code) != 0 ||
          map_add_string_entry(job_entry, "command", shell->jobs[i].command) != 0 ||
          map_add_number_entry(job_entry, "pid", (double) shell->jobs[i].process.pid) != 0 ||
          map_add_number_entry(job_entry, "pgid", (double) shell->jobs[i].process.pgid) != 0 ||
          oosh_value_list_append_value(jobs, job_entry) != 0) {
        oosh_value_free(job_entry);
        free(job_entry);
        snprintf(error, error_size, "shell namespace is too large");
        oosh_value_free(vars);
        oosh_value_free(bindings);
        oosh_value_free(aliases);
        oosh_value_free(classes);
        oosh_value_free(instances);
        oosh_value_free(plugins);
        oosh_value_free(jobs);
        free(vars);
        free(bindings);
        free(aliases);
        free(classes);
        free(instances);
        free(plugins);
        free(jobs);
        return 1;
      }
      oosh_value_free(job_entry);
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
      oosh_value_free(vars);
      oosh_value_free(bindings);
      oosh_value_free(aliases);
      oosh_value_free(classes);
      oosh_value_free(instances);
      oosh_value_free(plugins);
      oosh_value_free(jobs);
      free(vars);
      free(bindings);
      free(aliases);
      free(classes);
      free(instances);
      free(plugins);
      free(jobs);
      return 1;
    }

    oosh_value_free(vars);
    oosh_value_free(bindings);
    oosh_value_free(aliases);
    oosh_value_free(classes);
    oosh_value_free(instances);
    oosh_value_free(plugins);
    oosh_value_free(jobs);
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
  OoshShell *shell,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
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

  oosh_value_set_map(out_value);
  if (map_add_string_entry(out_value, "cwd", shell->cwd) != 0 ||
      map_add_string_entry(out_value, "home", home) != 0 ||
      map_add_string_entry(out_value, "temp", tmp_dir) != 0 ||
      map_add_string_entry(out_value, "separator", oosh_platform_path_separator()) != 0) {
    snprintf(error, error_size, "fs namespace is too large");
    return 1;
  }

  return 0;
}

/* E6-S1-T2: user() — current user namespace */
static int resolver_user(
  OoshShell *shell,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
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

  oosh_value_set_map(out_value);
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
  OoshShell *shell,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
  char *error,
  size_t error_size
) {
  char hostname[OOSH_MAX_NAME];
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
  oosh_platform_gethostname(hostname, sizeof(hostname));

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

  oosh_value_set_map(out_value);
  if (map_add_string_entry(out_value, "os", oosh_platform_os_name()) != 0 ||
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
  OoshShell *shell,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
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

  oosh_value_set_map(out_value);
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
  OoshShell *shell,
  const OoshValue *receiver,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
  char *out,
  size_t out_size
) {
  size_t i;

  (void) shell;
  (void) args;
  (void) out;
  (void) out_size;

  if (receiver == NULL || out_value == NULL || receiver->kind != OOSH_VALUE_MAP || argc != 0) {
    if (out != NULL && out_size > 0) {
      snprintf(out, out_size, "keys() is only valid on map values");
    }
    return 1;
  }

  oosh_value_init(out_value);
  out_value->kind = OOSH_VALUE_LIST;
  for (i = 0; i < receiver->map.count; ++i) {
    OoshValue *key_value = allocate_runtime_value(out, out_size, "map key value");

    if (key_value == NULL) {
      return 1;
    }

    oosh_value_set_string(key_value, receiver->map.entries[i].key);
    if (oosh_value_list_append_value(out_value, key_value) != 0) {
      oosh_value_free(key_value);
      free(key_value);
      if (out != NULL && out_size > 0) {
        snprintf(out, out_size, "keys() result is too large");
      }
      return 1;
    }
    oosh_value_free(key_value);
    free(key_value);
  }

  return 0;
}

static int map_method_values(
  OoshShell *shell,
  const OoshValue *receiver,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
  char *out,
  size_t out_size
) {
  size_t i;

  (void) shell;
  (void) args;

  if (receiver == NULL || out_value == NULL || receiver->kind != OOSH_VALUE_MAP || argc != 0) {
    snprintf(out, out_size, "values() is only valid on map values");
    return 1;
  }

  oosh_value_init(out_value);
  out_value->kind = OOSH_VALUE_LIST;
  for (i = 0; i < receiver->map.count; ++i) {
    OoshValue *entry_value = allocate_runtime_value(out, out_size, "map entry value");

    if (entry_value == NULL) {
      return 1;
    }

    if (oosh_value_set_from_item(entry_value, &receiver->map.entries[i].value) != 0 ||
        oosh_value_list_append_value(out_value, entry_value) != 0) {
      oosh_value_free(entry_value);
      free(entry_value);
      snprintf(out, out_size, "values() result is too large");
      return 1;
    }
    oosh_value_free(entry_value);
    free(entry_value);
  }

  return 0;
}

static int map_method_entries(
  OoshShell *shell,
  const OoshValue *receiver,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
  char *out,
  size_t out_size
) {
  size_t i;

  (void) shell;
  (void) args;

  if (receiver == NULL || out_value == NULL || receiver->kind != OOSH_VALUE_MAP || argc != 0) {
    snprintf(out, out_size, "entries() is only valid on map values");
    return 1;
  }

  oosh_value_init(out_value);
  out_value->kind = OOSH_VALUE_LIST;
  for (i = 0; i < receiver->map.count; ++i) {
    OoshValue *entry_map = allocate_runtime_value(out, out_size, "map entry map");
    OoshValue *nested_value = allocate_runtime_value(out, out_size, "map nested value");

    if (entry_map == NULL || nested_value == NULL) {
      free(entry_map);
      free(nested_value);
      return 1;
    }

    oosh_value_set_map(entry_map);
    oosh_value_set_string(nested_value, receiver->map.entries[i].key);
    if (map_add_value_entry(entry_map, "key", nested_value) != 0 ||
        oosh_value_set_from_item(nested_value, &receiver->map.entries[i].value) != 0 ||
        map_add_value_entry(entry_map, "value", nested_value) != 0 ||
        oosh_value_list_append_value(out_value, entry_map) != 0) {
      oosh_value_free(nested_value);
      oosh_value_free(entry_map);
      free(nested_value);
      free(entry_map);
      snprintf(out, out_size, "entries() result is too large");
      return 1;
    }
    oosh_value_free(nested_value);
    oosh_value_free(entry_map);
    free(nested_value);
    free(entry_map);
  }

  return 0;
}

static int map_method_get(
  OoshShell *shell,
  const OoshValue *receiver,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
  char *out,
  size_t out_size
) {
  const OoshValueItem *entry;
  char key[OOSH_MAX_NAME];

  (void) shell;

  if (receiver == NULL || out_value == NULL || receiver->kind != OOSH_VALUE_MAP) {
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

  entry = oosh_value_map_get_item(receiver, key);
  if (entry == NULL) {
    oosh_value_init(out_value);
    return 0;
  }
  return oosh_value_set_from_item(out_value, entry);
}

static int map_method_has(
  OoshShell *shell,
  const OoshValue *receiver,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
  char *out,
  size_t out_size
) {
  char key[OOSH_MAX_NAME];

  (void) shell;

  if (receiver == NULL || out_value == NULL || receiver->kind != OOSH_VALUE_MAP) {
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

  oosh_value_set_boolean(out_value, oosh_value_map_get_item(receiver, key) != NULL);
  return 0;
}

static int register_builtin_value_resolvers(OoshShell *shell) {
  if (shell == NULL) {
    return 1;
  }

  if (oosh_shell_register_value_resolver(shell, "map", resolver_map, 0) != 0) {
    return 1;
  }
  if (oosh_shell_register_value_resolver(shell, "env", resolver_env, 0) != 0) {
    return 1;
  }
  if (oosh_shell_register_value_resolver(shell, "proc", resolver_proc, 0) != 0) {
    return 1;
  }
  if (oosh_shell_register_value_resolver(shell, "shell", resolver_shell_namespace, 0) != 0) {
    return 1;
  }
  /* E6-S1 namespaces */
  if (oosh_shell_register_value_resolver(shell, "fs", resolver_fs, 0) != 0) {
    return 1;
  }
  if (oosh_shell_register_value_resolver(shell, "user", resolver_user, 0) != 0) {
    return 1;
  }
  if (oosh_shell_register_value_resolver(shell, "sys", resolver_sys, 0) != 0) {
    return 1;
  }
  if (oosh_shell_register_value_resolver(shell, "time", resolver_time, 0) != 0) {
    return 1;
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
  char trimmed[OOSH_MAX_LINE];
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
  OoshMemberKind *out_member_kind,
  char *name,
  size_t name_size,
  char *expression,
  size_t expression_size
) {
  char trimmed[OOSH_MAX_LINE];
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
    *out_member_kind = OOSH_MEMBER_PROPERTY;
    cursor += 8;
  } else if (strncmp(cursor, "method", 6) == 0 && isspace((unsigned char) cursor[6])) {
    *out_member_kind = OOSH_MEMBER_METHOD;
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

static int format_var_list(const OoshShell *shell, int exported_only, char *out, size_t out_size) {
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

static int format_binding_list(const OoshShell *shell, char *out, size_t out_size) {
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
    char rendered[OOSH_MAX_OUTPUT];
    char line[OOSH_MAX_OUTPUT];

    if (oosh_value_render(&shell->bindings[i].value, rendered, sizeof(rendered)) != 0) {
      copy_string(rendered, sizeof(rendered), "<unrenderable>");
    }
    snprintf(
      line,
      sizeof(line),
      "%s=%s [%s]",
      shell->bindings[i].name,
      rendered,
      oosh_value_kind_name(shell->bindings[i].value.kind)
    );
    if (append_output_line(out, out_size, line) != 0) {
      return 1;
    }
  }

  return 0;
}

static int format_extension_list(const OoshShell *shell, char *out, size_t out_size) {
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
    char line[OOSH_MAX_OUTPUT];
    const char *member_kind = shell->extensions[i].member_kind == OOSH_MEMBER_METHOD ? "method" : "property";
    const char *source_kind = shell->extensions[i].is_plugin_extension ? "plugin" : "lang";
    const char *impl_kind = shell->extensions[i].impl_kind == OOSH_EXTENSION_IMPL_NATIVE ? "native" : "block";

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

static int format_function_list(const OoshShell *shell, char *out, size_t out_size) {
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
    char signature[OOSH_MAX_OUTPUT];
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

static int format_class_list(const OoshShell *shell, char *out, size_t out_size) {
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
    char line[OOSH_MAX_OUTPUT];
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

static int handle_let_line(OoshShell *shell, const char *line, char *out, size_t out_size) {
  char name[OOSH_MAX_VAR_NAME];
  char expression[OOSH_MAX_LINE];
  OoshValue *value;

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

  value = (OoshValue *) calloc(1, sizeof(*value));
  if (value == NULL) {
    snprintf(out, out_size, "unable to allocate let() value");
    return 1;
  }

  if (oosh_evaluate_line_value(shell, expression, value, out, out_size) != 0) {
    free(value);
    if (out[0] == '\0') {
      snprintf(out, out_size, "unable to evaluate let expression");
    }
    return 1;
  }

  if (oosh_shell_set_binding(shell, name, value) != 0) {
    free(value);
    snprintf(out, out_size, "unable to store binding: %s", name);
    return 1;
  }

  free(value);
  out[0] = '\0';
  return 0;
}

static int handle_extend_line(OoshShell *shell, const char *line, char *out, size_t out_size) {
  char target[OOSH_MAX_NAME];
  char name[OOSH_MAX_NAME];
  char expression[OOSH_MAX_LINE];
  OoshMemberKind member_kind;
  OoshValue *value;

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

  value = (OoshValue *) calloc(1, sizeof(*value));
  if (value == NULL) {
    snprintf(out, out_size, "unable to allocate extend() value");
    return 1;
  }

  if (oosh_evaluate_line_value(shell, expression, value, out, out_size) != 0) {
    free(value);
    if (out[0] == '\0') {
      snprintf(out, out_size, "unable to evaluate extend expression");
    }
    return 1;
  }

  if (value->kind != OOSH_VALUE_BLOCK) {
    free(value);
    snprintf(out, out_size, "extend expects a block value");
    return 1;
  }

  if (member_kind == OOSH_MEMBER_PROPERTY) {
    if (value->block.param_count != 1) {
      free(value);
      snprintf(out, out_size, "property extensions expect exactly one block parameter for the receiver");
      return 1;
    }
    if (oosh_shell_register_block_property_extension(shell, target, name, &value->block) != 0) {
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
    if (oosh_shell_register_block_method_extension(shell, target, name, &value->block) != 0) {
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
  char trimmed[OOSH_MAX_LINE];
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
  OoshShell *shell,
  const char *line,
  const char *keyword,
  OoshControlSignalKind kind,
  char *out,
  size_t out_size
) {
  char argument[OOSH_MAX_LINE];
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
  if (oosh_shell_raise_control_signal(shell, kind, count) != 0) {
    snprintf(out, out_size, "unable to raise %s control signal", keyword);
    return 1;
  }

  out[0] = '\0';
  return 0;
}

static int handle_return_line(OoshShell *shell, const char *line, char *out, size_t out_size) {
  char argument[OOSH_MAX_LINE];

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
    OoshValue *value = allocate_runtime_value(out, out_size, "return value");

    if (value == NULL) {
      return 1;
    }
    if (oosh_evaluate_line_value(shell, argument, value, out, out_size) != 0) {
      free(value);
      if (out[0] == '\0') {
        snprintf(out, out_size, "unable to evaluate return expression");
      }
      return 1;
    }
    if (oosh_value_render(value, out, out_size) != 0) {
      free(value);
      snprintf(out, out_size, "unable to render return value");
      return 1;
    }
    free(value);
  } else {
    out[0] = '\0';
  }

  if (oosh_shell_raise_control_signal(shell, OOSH_CONTROL_SIGNAL_RETURN, 1) != 0) {
    snprintf(out, out_size, "unable to raise return control signal");
    return 1;
  }
  return 0;
}

static int format_alias_list(const OoshShell *shell, char *out, size_t out_size) {
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

static int update_directory_vars(OoshShell *shell, const char *previous_cwd) {
  if (previous_cwd != NULL && previous_cwd[0] != '\0') {
    if (oosh_shell_set_var(shell, "OLDPWD", previous_cwd, 0) != 0) {
      return 1;
    }
  }

  return oosh_shell_set_var(shell, "PWD", shell->cwd, 1);
}

static void initialize_default_variables(OoshShell *shell) {
  const char *home;
  const char *path;

  if (shell == NULL) {
    return;
  }

  oosh_shell_set_var(shell, "PWD", shell->cwd, 1);

  home = getenv("HOME");
#ifdef _WIN32
  if (home == NULL || home[0] == '\0') {
    home = getenv("USERPROFILE");
  }
#endif
  if (home != NULL && home[0] != '\0') {
    oosh_shell_set_var(shell, "HOME", home, 1);
  }

  path = getenv("PATH");
  if (path != NULL && path[0] != '\0') {
    oosh_shell_set_var(shell, "PATH", path, 1);
  }

  /* POSIX default field separator: space, tab, newline. */
  oosh_shell_set_var(shell, "IFS", " \t\n", 0);
}

static void resolve_history_path(OoshShell *shell) {
  const char *env_path;
  const char *home;

  if (shell == NULL) {
    return;
  }

  shell->history_path[0] = '\0';

  env_path = getenv("OOSH_HISTORY");
  if (env_path != NULL && env_path[0] != '\0') {
    oosh_platform_resolve_path(shell->cwd, env_path, shell->history_path, sizeof(shell->history_path));
    return;
  }

  home = oosh_shell_get_var(shell, "HOME");
  if (home != NULL && home[0] != '\0') {
    snprintf(shell->history_path, sizeof(shell->history_path), "%s/.oosh/history", home);
  }
}

static int oosh_shell_history_add(OoshShell *shell, const char *line) {
  char entry[OOSH_MAX_LINE];

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

  if (shell->history_count >= OOSH_MAX_HISTORY) {
    memmove(shell->history, shell->history + 1, (OOSH_MAX_HISTORY - 1) * sizeof(shell->history[0]));
    shell->history_count = OOSH_MAX_HISTORY - 1;
  }

  copy_string(shell->history[shell->history_count], sizeof(shell->history[shell->history_count]), entry);
  shell->history_count++;
  shell->history_dirty = 1;
  return 0;
}

static void load_history(OoshShell *shell) {
  FILE *fp;
  char line[OOSH_MAX_LINE];

  if (shell == NULL || shell->history_path[0] == '\0') {
    return;
  }

  fp = fopen(shell->history_path, "rb");
  if (fp == NULL) {
    return;
  }

  while (fgets(line, sizeof(line), fp) != NULL) {
    trim_trailing_newlines(line);
    oosh_shell_history_add(shell, line);
  }

  fclose(fp);
  shell->history_dirty = 0;
}

static void save_history(const OoshShell *shell) {
  FILE *fp;
  char directory[OOSH_MAX_PATH];
  size_t i;

  if (shell == NULL || shell->history_path[0] == '\0' || !shell->history_dirty) {
    return;
  }

  oosh_platform_dirname(shell->history_path, directory, sizeof(directory));
  if (directory[0] != '\0' && strcmp(directory, ".") != 0) {
    oosh_platform_ensure_directory(directory);
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

static void remove_job_at(OoshShell *shell, size_t index) {
  if (shell == NULL || index >= shell->job_count) {
    return;
  }

  oosh_platform_close_background_process(&shell->jobs[index].process);
  if (index + 1 < shell->job_count) {
    memmove(&shell->jobs[index], &shell->jobs[index + 1], (shell->job_count - index - 1) * sizeof(shell->jobs[index]));
  }
  shell->job_count--;
}

static void prune_completed_jobs(OoshShell *shell) {
  size_t i = 0;

  if (shell == NULL) {
    return;
  }

  while (i < shell->job_count) {
    if (shell->jobs[i].state == OOSH_JOB_DONE) {
      remove_job_at(shell, i);
      continue;
    }
    i++;
  }
}

static OoshJob *find_job_by_id(OoshShell *shell, int id) {
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

static OoshJob *find_default_job(OoshShell *shell) {
  size_t i;
  OoshJob *running_job = NULL;

  if (shell == NULL || shell->job_count == 0) {
    return NULL;
  }

  for (i = shell->job_count; i > 0; --i) {
    if (shell->jobs[i - 1].state == OOSH_JOB_STOPPED) {
      return &shell->jobs[i - 1];
    }
    if (running_job == NULL && shell->jobs[i - 1].state == OOSH_JOB_RUNNING) {
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

static int wait_for_job_at(OoshShell *shell, size_t index, int *out_status, char *out, size_t out_size) {
  OoshJob *job;
  OoshPlatformProcessState state = OOSH_PLATFORM_PROCESS_UNCHANGED;
  int exit_code = 0;

  if (shell == NULL || out_status == NULL || out == NULL || out_size == 0 || index >= shell->job_count) {
    return 1;
  }

  job = &shell->jobs[index];
  out[0] = '\0';

  if (job->state == OOSH_JOB_DONE) {
    *out_status = job->exit_code;
    remove_job_at(shell, index);
    return 0;
  }
  if (job->state == OOSH_JOB_STOPPED) {
    snprintf(out, out_size, "[%d] stopped pid=%lld %s", job->id, job->process.pid, job->command);
    *out_status = 1;
    return 0;
  }

  if (oosh_platform_wait_background_process(&job->process, 0, &state, &exit_code) != 0) {
    snprintf(out, out_size, "unable to wait for background job [%d]", job->id);
    return 1;
  }

  if (state == OOSH_PLATFORM_PROCESS_STOPPED) {
    job->state = OOSH_JOB_STOPPED;
    job->exit_code = exit_code;
    snprintf(out, out_size, "[%d] stopped pid=%lld %s", job->id, job->process.pid, job->command);
    *out_status = 1;
    return 0;
  }

  job->state = OOSH_JOB_DONE;
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

void oosh_shell_refresh_jobs(OoshShell *shell) {
  size_t i;

  if (shell == NULL) {
    return;
  }

  for (i = 0; i < shell->job_count; ++i) {
    OoshPlatformProcessState state = OOSH_PLATFORM_PROCESS_UNCHANGED;
    int exit_code = 0;

    if (shell->jobs[i].state == OOSH_JOB_DONE) {
      continue;
    }

    if (oosh_platform_poll_background_process(&shell->jobs[i].process, &state, &exit_code) != 0) {
      continue;
    }

    if (state == OOSH_PLATFORM_PROCESS_RUNNING) {
      shell->jobs[i].state = OOSH_JOB_RUNNING;
    } else if (state == OOSH_PLATFORM_PROCESS_STOPPED) {
      shell->jobs[i].state = OOSH_JOB_STOPPED;
      shell->jobs[i].exit_code = exit_code;
    } else if (state == OOSH_PLATFORM_PROCESS_EXITED) {
      shell->jobs[i].state = OOSH_JOB_DONE;
      shell->jobs[i].exit_code = exit_code;
      shell->jobs[i].termination_signal = (exit_code > 128) ? (exit_code - 128) : 0;
    }
  }
}

void oosh_shell_set_program_path(OoshShell *shell, const char *path) {
  if (shell == NULL || path == NULL || path[0] == '\0') {
    return;
  }

  if (strchr(path, '/') != NULL || strchr(path, '\\') != NULL) {
    if (oosh_platform_resolve_path(shell->cwd, path, shell->program_path, sizeof(shell->program_path)) == 0) {
      return;
    }
  }

  copy_string(shell->program_path, sizeof(shell->program_path), path);
}

int oosh_shell_start_background_job(OoshShell *shell, const char *command_text, char *out, size_t out_size) {
  OoshPlatformAsyncProcess process;
  char *argv[4];
  char error[OOSH_MAX_OUTPUT];

  if (shell == NULL || command_text == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  oosh_shell_refresh_jobs(shell);
  if (shell->job_count >= OOSH_MAX_JOBS) {
    prune_completed_jobs(shell);
  }

  if (shell->job_count >= OOSH_MAX_JOBS) {
    snprintf(out, out_size, "too many background jobs");
    return 1;
  }

  if (shell->program_path[0] == '\0') {
    snprintf(out, out_size, "background jobs unavailable: shell executable path not set");
    return 1;
  }

  argv[0] = shell->program_path;
  argv[1] = "-c";
  argv[2] = (char *) command_text;
  argv[3] = NULL;

  error[0] = '\0';
  if (oosh_platform_spawn_background_process(shell->cwd, argv, &process, error, sizeof(error)) != 0) {
    snprintf(out, out_size, "%s", error[0] == '\0' ? "unable to start background job" : error);
    return 1;
  }

  memset(&shell->jobs[shell->job_count], 0, sizeof(shell->jobs[shell->job_count]));
  shell->jobs[shell->job_count].id = shell->next_job_id++;
  shell->jobs[shell->job_count].state = OOSH_JOB_RUNNING;
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

void oosh_shell_clear_control_signal(OoshShell *shell) {
  if (shell == NULL) {
    return;
  }

  shell->control_signal = OOSH_CONTROL_SIGNAL_NONE;
  shell->control_levels = 0;
}

int oosh_shell_raise_control_signal(OoshShell *shell, OoshControlSignalKind kind, int levels) {
  if (shell == NULL || kind == OOSH_CONTROL_SIGNAL_NONE || levels <= 0) {
    return 1;
  }

  shell->control_signal = kind;
  shell->control_levels = levels;
  return 0;
}

int oosh_shell_run_exit_trap(OoshShell *shell, char *out, size_t out_size) {
  int status;

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  if (!shell->traps[OOSH_TRAP_EXIT].active ||
      shell->traps[OOSH_TRAP_EXIT].command[0] == '\0' ||
      shell->running_exit_trap) {
    return 0;
  }

  shell->running_exit_trap = 1;
  status = oosh_shell_execute_line(shell, shell->traps[OOSH_TRAP_EXIT].command, out, out_size);
  shell->running_exit_trap = 0;
  return status;
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

static int is_command_position(const OoshTokenStream *stream, size_t index) {
  return index == 0 || stream->tokens[index - 1].kind == OOSH_TOKEN_SHELL_PIPE;
}

static int token_is_plain_word(const OoshToken *token) {
  return token != NULL &&
         token->kind == OOSH_TOKEN_WORD &&
         token->text[0] != '\0' &&
         strcmp(token->raw, token->text) == 0;
}

static int expand_aliases_once(
  OoshShell *shell,
  const char *line,
  char *expanded,
  size_t expanded_size,
  int *out_replaced,
  char *error,
  size_t error_size
) {
  OoshTokenStream stream;
  size_t last_position = 0;
  size_t i;

  if (shell == NULL || line == NULL || expanded == NULL || expanded_size == 0 || out_replaced == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  *out_replaced = 0;
  expanded[0] = '\0';

  if (oosh_lex_line(line, &stream, error, error_size) != 0) {
    return 1;
  }

  for (i = 0; i < stream.count; ++i) {
    const OoshToken *token = &stream.tokens[i];
    const char *alias_value;
    size_t token_end;

    if (token->kind == OOSH_TOKEN_EOF) {
      break;
    }

    if (!is_command_position(&stream, i) ||
        !token_is_plain_word(token) ||
        (i + 1 < stream.count && stream.tokens[i + 1].kind == OOSH_TOKEN_ARROW)) {
      continue;
    }

    alias_value = oosh_shell_get_alias(shell, token->text);
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
  OoshShell *shell,
  const char *line,
  char *expanded,
  size_t expanded_size,
  char *error,
  size_t error_size
) {
  char current[OOSH_MAX_LINE];
  char next[OOSH_MAX_LINE];
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
  const char *separator = oosh_platform_path_separator();

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
  OoshPlatformFileInfo info;

  if (path == NULL || path[0] == '\0') {
    return 0;
  }

  if (oosh_platform_stat(path, &info) != 0 || !info.exists || info.is_directory) {
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
      char with_ext[OOSH_MAX_PATH];

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

static int resolve_command_path(const OoshShell *shell, const char *name, char *out, size_t out_size) {
  const char *path_env;
  const char *cursor;
  char resolved[OOSH_MAX_PATH];

  if (shell == NULL || name == NULL || name[0] == '\0' || out == NULL || out_size == 0) {
    return 1;
  }

  if (command_has_path_component(name)) {
    if (oosh_platform_resolve_path(shell->cwd, name, resolved, sizeof(resolved)) != 0) {
      return 1;
    }
    if (try_command_candidate(resolved, out, out_size) == 0) {
      return 0;
    }
    return 1;
  }

  path_env = oosh_shell_get_var(shell, "PATH");
  if (path_env == NULL || path_env[0] == '\0') {
    return 1;
  }

  cursor = path_env;
  while (*cursor != '\0') {
    char directory[OOSH_MAX_PATH];
    char candidate[OOSH_MAX_PATH];
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
    if (oosh_platform_resolve_path(shell->cwd, candidate, resolved, sizeof(resolved)) == 0 &&
        try_command_candidate(resolved, out, out_size) == 0) {
      return 0;
    }

    if (*cursor == separator) {
      cursor++;
    }
  }

  return 1;
}

static int command_help(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  (void) argc;
  (void) argv;
  oosh_shell_print_help(shell, out, out_size);
  return 0;
}

static int command_exit(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  (void) argc;
  (void) argv;
  shell->running = 0;
  copy_string(out, out_size, "bye");
  return 0;
}

static int command_pwd(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  (void) argc;
  (void) argv;
  snprintf(out, out_size, "%s", shell->cwd);
  return 0;
}

static int command_cd(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  const char *target;
  char resolved[OOSH_MAX_PATH];
  char previous_cwd[OOSH_MAX_PATH];

  copy_string(previous_cwd, sizeof(previous_cwd), shell->cwd);

  if (argc >= 2 && strcmp(argv[1], "-") == 0) {
    target = oosh_shell_get_var(shell, "OLDPWD");
    if (target == NULL || target[0] == '\0') {
      snprintf(out, out_size, "OLDPWD is not set");
      return 1;
    }
  } else {
    target = argc >= 2 ? argv[1] : oosh_shell_get_var(shell, "HOME");
    if (target == NULL || target[0] == '\0') {
      target = ".";
    }
  }

  if (oosh_platform_resolve_path(shell->cwd, target, resolved, sizeof(resolved)) != 0) {
    snprintf(out, out_size, "unable to resolve path: %s", target);
    return 1;
  }

  if (oosh_platform_chdir(resolved) != 0) {
    snprintf(out, out_size, "unable to change directory to %s", resolved);
    return 1;
  }

  if (oosh_platform_getcwd(shell->cwd, sizeof(shell->cwd)) != 0) {
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

static int command_inspect(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  OoshObject object;

  if (argc < 2) {
    snprintf(out, out_size, "usage: inspect <path>");
    return 1;
  }

  if (oosh_object_resolve(shell->cwd, argv[1], &object) != 0) {
    snprintf(out, out_size, "unable to inspect %s", argv[1]);
    return 1;
  }

  return oosh_object_call_method(&object, "describe", 0, NULL, out, out_size);
}

static int command_get(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  OoshObject object;

  if (argc < 3) {
    snprintf(out, out_size, "usage: get <path> <property>");
    return 1;
  }

  if (oosh_object_resolve(shell->cwd, argv[1], &object) != 0) {
    snprintf(out, out_size, "unable to resolve %s", argv[1]);
    return 1;
  }

  return oosh_object_get_property(&object, argv[2], out, out_size);
}

static int command_call(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  OoshObject object;

  if (argc < 3) {
    snprintf(out, out_size, "usage: call <path> <method> [args...]");
    return 1;
  }

  if (oosh_object_resolve(shell->cwd, argv[1], &object) != 0) {
    snprintf(out, out_size, "unable to resolve %s", argv[1]);
    return 1;
  }

  return oosh_object_call_method(&object, argv[2], argc - 3, argv + 3, out, out_size);
}

static int command_prompt(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  char path[OOSH_MAX_PATH];

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
    if (oosh_platform_resolve_path(shell->cwd, argv[2], path, sizeof(path)) != 0) {
      snprintf(out, out_size, "unable to resolve config path: %s", argv[2]);
      return 1;
    }
    return oosh_shell_load_config(shell, path, out, out_size);
  }

  snprintf(out, out_size, "unknown prompt command: %s", argv[1]);
  return 1;
}

static int command_plugin(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  OoshLoadedPlugin *plugin;

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
    return oosh_shell_load_plugin(shell, argv[2], out, out_size);
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

  snprintf(out, out_size, "unknown plugin command: %s", argv[1]);
  return 1;
}

static int command_run(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  if (argc < 2) {
    snprintf(out, out_size, "usage: run <external-command> [args...]");
    return 1;
  }

  return oosh_execute_external_command(shell, argc - 1, argv + 1, out, out_size);
}

static int command_let(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  (void) argv;

  if (argc == 1) {
    return format_binding_list(shell, out, out_size);
  }

  snprintf(out, out_size, "let uses whole-line syntax: let <name> = <value-expression>");
  return 1;
}

static int command_function(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  const OoshShellFunction *function_def;
  int i;

  if (argc == 1) {
    return format_function_list(shell, out, out_size);
  }

  if (argc != 2) {
    snprintf(out, out_size, "usage: function [name]");
    return 1;
  }

  function_def = oosh_shell_find_function(shell, argv[1]);
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

static int command_class(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  const OoshClassDef *class_def;
  size_t i;

  if (argc == 1) {
    return format_class_list(shell, out, out_size);
  }

  if (argc != 2) {
    snprintf(out, out_size, "usage: classes [name]");
    return 1;
  }

  class_def = oosh_shell_find_class(shell, argv[1]);
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
    char rendered[OOSH_MAX_OUTPUT];

    if (oosh_value_render(&class_def->properties[i].default_value, rendered, sizeof(rendered)) != 0) {
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

static int command_extend(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  (void) argv;

  if (argc == 1) {
    return format_extension_list(shell, out, out_size);
  }

  snprintf(out, out_size, "extend uses whole-line syntax: extend <target> <property|method> <name> = <block>");
  return 1;
}

static int command_set(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  char name[OOSH_MAX_VAR_NAME];
  char value[OOSH_MAX_VAR_VALUE];

  if (argc == 1) {
    return format_var_list(shell, 0, out, out_size);
  }

  if (argc == 2 && split_assignment(argv[1], name, sizeof(name), value, sizeof(value)) == 0) {
    if (!is_valid_identifier(name) || oosh_shell_set_var(shell, name, value, 0) != 0) {
      snprintf(out, out_size, "unable to set variable: %s", argv[1]);
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
    value[0] = '\0';
  } else if (join_arguments(argc, argv, 2, value, sizeof(value)) != 0) {
    snprintf(out, out_size, "variable value too long");
    return 1;
  }

  if (oosh_shell_set_var(shell, name, value, 0) != 0) {
    snprintf(out, out_size, "unable to set variable: %s", name);
    return 1;
  }

  out[0] = '\0';
  return 0;
}

static int command_export(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  char name[OOSH_MAX_VAR_NAME];
  char value[OOSH_MAX_VAR_VALUE];
  const char *current_value;

  if (argc == 1) {
    return format_var_list(shell, 1, out, out_size);
  }

  if (argc == 2 && split_assignment(argv[1], name, sizeof(name), value, sizeof(value)) == 0) {
    if (!is_valid_identifier(name) || oosh_shell_set_var(shell, name, value, 1) != 0) {
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
    current_value = oosh_shell_get_var(shell, name);
    if (current_value == NULL) {
      current_value = "";
    }
    copy_string(value, sizeof(value), current_value);
  } else if (join_arguments(argc, argv, 2, value, sizeof(value)) != 0) {
    snprintf(out, out_size, "variable value too long");
    return 1;
  }

  if (oosh_shell_set_var(shell, name, value, 1) != 0) {
    snprintf(out, out_size, "unable to export variable: %s", name);
    return 1;
  }

  out[0] = '\0';
  return 0;
}

static int command_unset(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
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

    if (oosh_shell_unset_var(shell, argv[i]) == 0) {
      removed = 1;
    }
    if (oosh_shell_unset_binding(shell, argv[i]) == 0) {
      removed = 1;
    }
    if (!removed) {
      snprintf(out, out_size, "unable to unset variable: %s", argv[i]);
      return 1;
    }
  }

  return 0;
}

static int command_alias(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  char name[OOSH_MAX_VAR_NAME];
  char value[OOSH_MAX_ALIAS_VALUE];
  const char *alias_value;

  if (argc == 1) {
    return format_alias_list(shell, out, out_size);
  }

  if (argc == 2 && split_assignment(argv[1], name, sizeof(name), value, sizeof(value)) == 0) {
    if (!is_valid_alias_name(name) || oosh_shell_set_alias(shell, name, value) != 0) {
      snprintf(out, out_size, "unable to define alias: %s", argv[1]);
      return 1;
    }
    out[0] = '\0';
    return 0;
  }

  if (argc == 2) {
    alias_value = oosh_shell_get_alias(shell, argv[1]);
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

  if (oosh_shell_set_alias(shell, name, value) != 0) {
    snprintf(out, out_size, "unable to define alias: %s", name);
    return 1;
  }

  out[0] = '\0';
  return 0;
}

static int command_unalias(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  int i;

  if (argc < 2) {
    snprintf(out, out_size, "usage: unalias <name> [name...]");
    return 1;
  }

  out[0] = '\0';
  for (i = 1; i < argc; ++i) {
    if (oosh_shell_unset_alias(shell, argv[i]) != 0) {
      snprintf(out, out_size, "alias not found: %s", argv[i]);
      return 1;
    }
  }

  return 0;
}

int oosh_shell_source_file(OoshShell *shell, const char *path, int positional_count, char **positional_args, char *out, size_t out_size) {
  char resolved[OOSH_MAX_PATH];
  FILE *fp;
  char line[OOSH_MAX_LINE];
  char command_buffer[OOSH_MAX_OUTPUT];
  size_t line_number = 0;
  size_t command_start_line = 0;
  int pending_heredoc = 0;
  char saved_program_path[OOSH_MAX_PATH];
  char saved_positional_params[OOSH_MAX_POSITIONAL_PARAMS][OOSH_MAX_VAR_VALUE];
  int saved_positional_count;
  int i;

  if (shell == NULL || path == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  command_buffer[0] = '\0';

  if (oosh_platform_resolve_path(shell->cwd, path, resolved, sizeof(resolved)) != 0) {
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
  for (i = 0; i < shell->positional_count && i < OOSH_MAX_POSITIONAL_PARAMS; ++i) {
    copy_string(saved_positional_params[i], sizeof(saved_positional_params[i]), shell->positional_params[i]);
  }

  copy_string(shell->program_path, sizeof(shell->program_path), resolved);
  shell->positional_count = 0;
  if (positional_count > 0 && positional_args != NULL) {
    int n = positional_count < OOSH_MAX_POSITIONAL_PARAMS ? positional_count : OOSH_MAX_POSITIONAL_PARAMS;
    for (i = 0; i < n; ++i) {
      copy_string(shell->positional_params[i], sizeof(shell->positional_params[i]),
                  positional_args[i] != NULL ? positional_args[i] : "");
    }
    shell->positional_count = n;
  }

  {
    int source_status = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
      char line_output[OOSH_MAX_OUTPUT];
      char parse_error[OOSH_MAX_OUTPUT];
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
      exec_status = oosh_shell_execute_line(shell, command_buffer, line_output, sizeof(line_output));
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
      char parse_error[OOSH_MAX_OUTPUT];

      command_requires_more_input(command_buffer, parse_error, sizeof(parse_error));
      snprintf(out, out_size, "%s:%zu: %s", resolved, command_start_line, parse_error[0] == '\0' ? "incomplete command block" : parse_error);
      source_status = 1;
    }

    fclose(fp);

    /* Restore positional context. */
    copy_string(shell->program_path, sizeof(shell->program_path), saved_program_path);
    shell->positional_count = saved_positional_count;
    for (i = 0; i < saved_positional_count && i < OOSH_MAX_POSITIONAL_PARAMS; ++i) {
      copy_string(shell->positional_params[i], sizeof(shell->positional_params[i]), saved_positional_params[i]);
    }

    return source_status;
  }
}

static int command_source(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  if (argc < 2) {
    snprintf(out, out_size, "usage: source <path> [arg ...]");
    return 1;
  }

  return oosh_shell_source_file(shell, argv[1], argc - 2, argv + 2, out, out_size);
}

static int command_type(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  int i;
  int status = 0;

  if (argc < 2) {
    snprintf(out, out_size, "usage: type <name> [name...]");
    return 1;
  }

  out[0] = '\0';
  for (i = 1; i < argc; ++i) {
    const char *alias_value = oosh_shell_get_alias(shell, argv[i]);
    const OoshValue *binding_value = oosh_shell_get_binding(shell, argv[i]);
    const OoshShellFunction *function_def = oosh_shell_find_function(shell, argv[i]);
    const OoshClassDef *class_def = oosh_shell_find_class(shell, argv[i]);
    const OoshCommandDef *command = find_registered_command(shell, argv[i]);
    char command_path[OOSH_MAX_PATH];
    char line[OOSH_MAX_OUTPUT];

    if (alias_value != NULL) {
      snprintf(line, sizeof(line), "%s is an alias for %s", argv[i], alias_value);
    } else if (binding_value != NULL) {
      snprintf(line, sizeof(line), "%s is a value binding of type %s", argv[i], oosh_value_kind_name(binding_value->kind));
    } else if (function_def != NULL) {
      snprintf(line, sizeof(line), "%s is a shell function", argv[i]);
    } else if (class_def != NULL) {
      snprintf(line, sizeof(line), "%s is a class", argv[i]);
    } else if (command != NULL) {
      if (command->is_plugin_command) {
        snprintf(line, sizeof(line), "%s is a plugin command", argv[i]);
      } else {
        const char *kind_label =
          (command->kind == OOSH_BUILTIN_MUTANT) ? "mutant" :
          (command->kind == OOSH_BUILTIN_MIXED)  ? "mixed"  : "pure";
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

static int command_history(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
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

static int command_jobs(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  size_t i;

  (void) argc;
  (void) argv;

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  oosh_shell_refresh_jobs(shell);
  out[0] = '\0';

  if (shell->job_count == 0) {
    copy_string(out, out_size, "no background jobs");
    return 0;
  }

  {
    /* Determine which jobs get + and - markers (POSIX current/previous) */
    OoshJob *current_job  = NULL;
    OoshJob *previous_job = NULL;
    size_t j;

    for (j = shell->job_count; j > 0; --j) {
      OoshJob *candidate = &shell->jobs[j - 1];
      if (candidate->state == OOSH_JOB_DONE) continue;
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
        if (shell->jobs[j - 1].state == OOSH_JOB_STOPPED) {
          current_job = &shell->jobs[j - 1];
          break;
        }
      }
    }

    for (i = 0; i < shell->job_count; ++i) {
      char line[OOSH_MAX_OUTPUT];
      const char *status = "running";
      char detail[64];
      char marker;

      detail[0] = '\0';
      marker = ' ';
      if (&shell->jobs[i] == current_job)  marker = '+';
      else if (&shell->jobs[i] == previous_job) marker = '-';

      if (shell->jobs[i].state == OOSH_JOB_STOPPED) {
        status = "stopped";
      } else if (shell->jobs[i].state == OOSH_JOB_DONE) {
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

static int command_fg(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  OoshJob *job;
  int job_id = 0;
  int exit_code = 0;
  OoshPlatformProcessState state = OOSH_PLATFORM_PROCESS_UNCHANGED;
  size_t index;
  char error[OOSH_MAX_OUTPUT];

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  oosh_shell_refresh_jobs(shell);
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

  if (job->state == OOSH_JOB_DONE) {
    snprintf(out, out_size, "[%d] already completed %s", job->id, job->command);
    return job->exit_code == 0 ? 0 : 1;
  }

  index = (size_t) (job - shell->jobs);
  error[0] = '\0';
  if (oosh_platform_resume_background_process(&job->process, 1, error, sizeof(error)) != 0) {
    snprintf(out, out_size, "%s", error[0] == '\0' ? "unable to continue job" : error);
    return 1;
  }

  if (oosh_platform_wait_background_process(&job->process, 1, &state, &exit_code) != 0) {
    snprintf(out, out_size, "unable to wait for background job [%d]", job->id);
    return 1;
  }

  if (state == OOSH_PLATFORM_PROCESS_STOPPED) {
    job->state = OOSH_JOB_STOPPED;
    job->exit_code = exit_code;
    snprintf(out, out_size, "[%d] stopped %s", job->id, job->command);
    return 1;
  }

  job->state = OOSH_JOB_DONE;
  job->exit_code = exit_code;
  out[0] = '\0';
  remove_job_at(shell, index);
  return exit_code == 0 ? 0 : 1;
}

static int command_bg(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  OoshJob *job;
  int job_id = 0;

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  oosh_shell_refresh_jobs(shell);
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

  if (job->state == OOSH_JOB_DONE) {
    snprintf(out, out_size, "[%d] already completed %s", job->id, job->command);
    return 1;
  }

  if (job->state == OOSH_JOB_STOPPED) {
    char error[OOSH_MAX_OUTPUT];

    error[0] = '\0';
    if (oosh_platform_resume_background_process(&job->process, 0, error, sizeof(error)) != 0) {
      snprintf(out, out_size, "%s", error[0] == '\0' ? "unable to continue job" : error);
      return 1;
    }
    job->state = OOSH_JOB_RUNNING;
  }

  snprintf(out, out_size, "[%d] running pid=%lld %s", job->id, job->process.pid, job->command);
  return 0;
}

static int command_wait(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  int last_status = 0;
  int i;

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  oosh_shell_refresh_jobs(shell);
  out[0] = '\0';

  if (argc == 1) {
    size_t index = 0;

    if (shell->job_count == 0) {
      copy_string(out, out_size, "no background jobs");
      return 0;
    }

    while (index < shell->job_count) {
      char line[OOSH_MAX_OUTPUT];
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
    OoshJob *job;
    int job_id = 0;
    char line[OOSH_MAX_OUTPUT];
    size_t index;

    if (parse_job_id(argv[i], &job_id) != 0) {
      snprintf(out, out_size, "invalid job id: %s", argv[i]);
      return 1;
    }

    oosh_shell_refresh_jobs(shell);
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

static int command_eval(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  char line[OOSH_MAX_LINE];

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

  return oosh_shell_execute_line(shell, line, out, out_size);
}

static int command_exec(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  char resolved[OOSH_MAX_PATH];
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

  status = oosh_execute_external_command(shell, argc - 1, argv + 1, out, out_size);
  shell->running = 0;
  return status;
}

static int command_trap(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  int i;

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  if (argc == 1) {
    if (!shell->traps[OOSH_TRAP_EXIT].active || shell->traps[OOSH_TRAP_EXIT].command[0] == '\0') {
      copy_string(out, out_size, "no traps");
      return 0;
    }
    snprintf(out, out_size, "trap -- %s EXIT", shell->traps[OOSH_TRAP_EXIT].command);
    return 0;
  }

  if (argc >= 3 && strcmp(argv[1], "-") == 0) {
    for (i = 2; i < argc; ++i) {
      if (strcmp(argv[i], "EXIT") != 0) {
        snprintf(out, out_size, "trap currently supports only EXIT");
        return 1;
      }
    }
    shell->traps[OOSH_TRAP_EXIT].active = 0;
    shell->traps[OOSH_TRAP_EXIT].command[0] = '\0';
    return 0;
  }

  if (argc < 3) {
    snprintf(out, out_size, "usage: trap '<command>' EXIT | trap - EXIT | trap");
    return 1;
  }

  for (i = 2; i < argc; ++i) {
    if (strcmp(argv[i], "EXIT") != 0) {
      snprintf(out, out_size, "trap currently supports only EXIT");
      return 1;
    }
  }

  copy_string(shell->traps[OOSH_TRAP_EXIT].command, sizeof(shell->traps[OOSH_TRAP_EXIT].command), argv[1]);
  shell->traps[OOSH_TRAP_EXIT].active = argv[1][0] != '\0';
  return 0;
}

static int command_true(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  (void) shell;
  (void) argc;
  (void) argv;
  if (out != NULL && out_size > 0) {
    out[0] = '\0';
  }
  return 0;
}

static int command_false(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  (void) shell;
  (void) argc;
  (void) argv;
  if (out != NULL && out_size > 0) {
    out[0] = '\0';
  }
  return 1;
}

static int command_break(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  char line[OOSH_MAX_LINE];

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

  return handle_loop_control_line(shell, line, "break", OOSH_CONTROL_SIGNAL_BREAK, out, out_size);
}

static int command_continue(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  char line[OOSH_MAX_LINE];

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

  return handle_loop_control_line(shell, line, "continue", OOSH_CONTROL_SIGNAL_CONTINUE, out, out_size);
}

static int command_return(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  char line[OOSH_MAX_LINE];
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

static int register_builtin(OoshShell *shell, const char *name, const char *description,
                             OoshCommandFn fn, OoshBuiltinKind kind);

/* E3-S5: builtin <name> [args...] — invoke a shell built-in directly,
   bypassing any user function that has the same name.  Useful inside
   override functions, e.g. `function cd(dir) do ... ; builtin cd $dir ; done`. */
static int command_builtin(OoshShell *shell, int argc, char *argv[], char *out, size_t out_size) {
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

static int register_builtin_commands(OoshShell *shell) {
  /* --- PURE: read-only, never modifies shell state ----------------------- */
  if (register_builtin(shell, "help",      "show commands and expression syntax",       command_help,    OOSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "pwd",       "print current directory",                   command_pwd,     OOSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "type",      "show how a command name resolves",          command_type,    OOSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "history",   "show interactive command history",          command_history, OOSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "jobs",      "list background jobs with state and pid",   command_jobs,    OOSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "true",      "return success",                            command_true,    OOSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "false",     "return failure",                            command_false,   OOSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "inspect",   "print object metadata for a path",         command_inspect, OOSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "get",       "read an object property",                  command_get,     OOSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "call",      "invoke an object method",                  command_call,    OOSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "run",       "execute an external command natively",     command_run,     OOSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "function",  "list or inspect shell functions",          command_function, OOSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "functions", "list or inspect shell functions",          command_function, OOSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "class",     "list defined classes",                     command_class,   OOSH_BUILTIN_PURE) != 0 ||
      register_builtin(shell, "classes",   "list or inspect defined classes",          command_class,   OOSH_BUILTIN_PURE) != 0) {
    return 1;
  }

  /* --- MUTANT: must run in the current shell process --------------------- */
  if (register_builtin(shell, "builtin",  "invoke a built-in directly, bypassing function overrides", command_builtin, OOSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "exit",     "terminate the shell",                            command_exit,     OOSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "quit",     "terminate the shell",                            command_exit,     OOSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "cd",       "change current directory",                       command_cd,       OOSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "set",      "list or assign shell variables",                 command_set,      OOSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "export",   "mark a shell variable for child processes",      command_export,   OOSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "unset",    "remove shell variables",                         command_unset,    OOSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "unalias",  "remove aliases",                                 command_unalias,  OOSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "source",   "execute commands from a file",                   command_source,   OOSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, ".",        "execute commands from a file",                   command_source,   OOSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "eval",     "evaluate a command string in the current shell", command_eval,     OOSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "exec",     "run an external command and terminate the current shell", command_exec, OOSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "trap",     "list or define shell exit traps",                command_trap,     OOSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "break",    "exit the current loop or an outer loop",         command_break,    OOSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "continue", "skip to the next loop iteration",                command_continue, OOSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "return",   "exit the current shell function",                command_return,   OOSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "bg",       "resume a stopped background job",                command_bg,       OOSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "fg",       "resume or wait for a background job in the foreground", command_fg, OOSH_BUILTIN_MUTANT) != 0 ||
      register_builtin(shell, "wait",     "wait for background jobs to complete",           command_wait,     OOSH_BUILTIN_MUTANT) != 0) {
    return 1;
  }

  /* --- MIXED: read-only when listing, mutating when defining/loading ----- */
  if (register_builtin(shell, "alias",   "list or define aliases",                    command_alias,   OOSH_BUILTIN_MIXED) != 0 ||
      register_builtin(shell, "extend",  "list or define object/value extensions",    command_extend,  OOSH_BUILTIN_MIXED) != 0 ||
      register_builtin(shell, "let",     "list or create typed value bindings",       command_let,     OOSH_BUILTIN_MIXED) != 0 ||
      register_builtin(shell, "plugin",  "load, inspect or toggle plugins",           command_plugin,  OOSH_BUILTIN_MIXED) != 0 ||
      register_builtin(shell, "prompt",  "show or load prompt configuration",        command_prompt,  OOSH_BUILTIN_MIXED) != 0) {
    return 1;
  }

  return 0;
}

static int register_builtin_extensions(OoshShell *shell) {
  if (shell == NULL) {
    return 1;
  }

  if (oosh_shell_register_native_method_extension(shell, "any", "print", method_print, 0) != 0) {
    return 1;
  }
  if (oosh_shell_register_native_method_extension(shell, "map", "keys", map_method_keys, 0) != 0) {
    return 1;
  }
  if (oosh_shell_register_native_method_extension(shell, "map", "values", map_method_values, 0) != 0) {
    return 1;
  }
  if (oosh_shell_register_native_method_extension(shell, "map", "entries", map_method_entries, 0) != 0) {
    return 1;
  }
  if (oosh_shell_register_native_method_extension(shell, "map", "get", map_method_get, 0) != 0) {
    return 1;
  }
  if (oosh_shell_register_native_method_extension(shell, "map", "has", map_method_has, 0) != 0) {
    return 1;
  }
  if (oosh_shell_register_native_method_extension(shell, "file", "read_json", method_read_json, 0) != 0) {
    return 1;
  }
  if (oosh_shell_register_native_method_extension(shell, "file", "write_json", method_write_json, 0) != 0) {
    return 1;
  }
  if (oosh_shell_register_native_method_extension(shell, "path", "write_json", method_write_json, 0) != 0) {
    return 1;
  }

  return 0;
}

static int try_load_default_config(OoshShell *shell) {
  const char *env_path = getenv("OOSH_CONFIG");
  const char *home = oosh_shell_get_var(shell, "HOME");
  char path[OOSH_MAX_PATH];
  char output[OOSH_MAX_OUTPUT];

  if (env_path != NULL && env_path[0] != '\0') {
    if (oosh_shell_load_config(shell, env_path, output, sizeof(output)) == 0) {
      return 0;
    }
  }

  if (oosh_shell_load_config(shell, "oosh.conf", output, sizeof(output)) == 0) {
    return 0;
  }

  if (home != NULL && home[0] != '\0') {
    snprintf(path, sizeof(path), "%s/.oosh/prompt.conf", home);
    if (oosh_shell_load_config(shell, path, output, sizeof(output)) == 0) {
      return 0;
    }
  }

  return 0;
}

static int try_load_default_rc(OoshShell *shell) {
  const char *env_path = getenv("OOSH_RC");
  const char *home = oosh_shell_get_var(shell, "HOME");
  char path[OOSH_MAX_PATH];
  char output[OOSH_MAX_OUTPUT];

  if (env_path != NULL && env_path[0] != '\0') {
    if (oosh_shell_source_file(shell, env_path, 0, NULL, output, sizeof(output)) != 0) {
      fprintf(stderr, "oosh: %s\n", output);
    }
    return 0;
  }

  if (home == NULL || home[0] == '\0') {
    return 0;
  }

  snprintf(path, sizeof(path), "%s/.ooshrc", home);
  if (oosh_shell_source_file(shell, path, 0, NULL, output, sizeof(output)) != 0 &&
      strstr(output, "unable to open source file") == NULL) {
    fprintf(stderr, "oosh: %s\n", output);
  }

  return 0;
}

int oosh_shell_init(OoshShell *shell) {
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
  {
    OoshPlatformProcessInfo proc_info;
    memset(&proc_info, 0, sizeof(proc_info));
    if (oosh_platform_get_process_info(&proc_info) == 0) {
      shell->shell_pid = (long long) proc_info.pid;
    }
  }
  copy_string(shell->traps[OOSH_TRAP_EXIT].name, sizeof(shell->traps[OOSH_TRAP_EXIT].name), "EXIT");

  if (oosh_platform_getcwd(shell->cwd, sizeof(shell->cwd)) != 0) {
    copy_string(shell->cwd, sizeof(shell->cwd), ".");
  }

  oosh_prompt_config_init(&shell->prompt);
  initialize_default_variables(shell);
  resolve_history_path(shell);
  load_history(shell);

  if (register_builtin_commands(shell) != 0) {
    return 1;
  }
  if (register_builtin_value_resolvers(shell) != 0) {
    return 1;
  }
  if (register_builtin_extensions(shell) != 0) {
    return 1;
  }

  if (try_load_default_config(shell) != 0) {
    return 1;
  }

  return try_load_default_rc(shell);
}

void oosh_shell_destroy(OoshShell *shell) {
  size_t i;

  if (shell == NULL) {
    return;
  }

  save_history(shell);

  for (i = 0; i < shell->binding_count; ++i) {
    oosh_value_free(&shell->bindings[i].value);
  }
  for (i = 0; i < shell->class_count; ++i) {
    free_class_definition_contents(&shell->classes[i]);
  }
  for (i = 0; i < shell->instance_count; ++i) {
    oosh_value_free(&shell->instances[i].fields);
  }

  for (i = 0; i < shell->job_count; ++i) {
    oosh_platform_close_background_process(&shell->jobs[i].process);
  }

  for (i = 0; i < shell->plugin_count; ++i) {
    OoshPluginShutdownFn shutdown_fn;

    shutdown_fn = (OoshPluginShutdownFn) oosh_platform_library_symbol(shell->plugins[i].handle, "oosh_plugin_shutdown");
    if (shutdown_fn != NULL) {
      shutdown_fn(shell);
    }
    oosh_platform_library_close(shell->plugins[i].handle);
    shell->plugins[i].handle = NULL;
  }
}

int oosh_shell_register_command(OoshShell *shell, const char *name, const char *description, OoshCommandFn fn, int is_plugin_command) {
  size_t i;

  if (shell == NULL || name == NULL || description == NULL || fn == NULL) {
    return 1;
  }

  if (shell->command_count >= OOSH_MAX_COMMANDS) {
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
  shell->commands[shell->command_count].kind = OOSH_BUILTIN_PURE;
  shell->command_count++;
  return 0;
}

/* Internal helper: like oosh_shell_register_command but accepts an explicit
   OoshBuiltinKind so that register_builtin_commands() can classify each
   built-in at registration time. */
static int register_builtin(OoshShell *shell, const char *name, const char *description,
                             OoshCommandFn fn, OoshBuiltinKind kind) {
  if (oosh_shell_register_command(shell, name, description, fn, 0) != 0) {
    return 1;
  }
  /* Overwrite the default kind set by oosh_shell_register_command. */
  shell->commands[shell->command_count - 1].kind = kind;
  return 0;
}

void oosh_shell_write_output(const char *text) {
  write_buffer(text);
}

void oosh_shell_print_help(const OoshShell *shell, char *out, size_t out_size) {
  size_t i;

  if (shell == NULL || out == NULL || out_size == 0) {
    return;
  }

  out[0] = '\0';
  snprintf(out + strlen(out), out_size - strlen(out), "Commands:\n");

  for (i = 0; i < shell->command_count; ++i) {
    if (shell->commands[i].is_plugin_command && !plugin_index_is_active(shell, shell->commands[i].owner_plugin_index)) {
      continue;
    }
    snprintf(
      out + strlen(out),
      out_size - strlen(out),
      "  %-12s %s%s\n",
      shell->commands[i].name,
      shell->commands[i].description,
      shell->commands[i].is_plugin_command ? " [plugin]" : ""
    );
  }

  snprintf(
    out + strlen(out),
    out_size - strlen(out),
    "\nShell State:\n"
    "  set PROJECT oosh\n"
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
    "  source ~/.ooshrc\n"
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
    "  list(1, 20, 3) |> sort(value desc)\n"
    "  plugin load build/oosh_sample_plugin.dylib ; sample() -> name\n"
    "  plugin load build/oosh_sample_plugin.dylib ; text(\"ciao\") |> sample_wrap()\n"
    "\nShell Pipelines:\n"
    "  ls -1 | wc -l\n"
    "  ls include > out.txt\n"
    "  cat < README.md | grep oosh\n"
    "  ls missing 2>&1 | wc -l\n"
    "  ./build/oosh_test_count_lines <<EOF\n"
    "one\n"
    "two\n"
    "EOF\n"
    "  ./build/oosh_test_emit_args hello 3> out.txt 1>&3\n"
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

int oosh_shell_load_config(OoshShell *shell, const char *path, char *out, size_t out_size) {
  OoshPromptConfig config;
  char resolved[OOSH_MAX_PATH];
  size_t i;
  int plugin_status = 0;
  char plugin_output[OOSH_MAX_OUTPUT];

  if (shell == NULL || path == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (oosh_platform_resolve_path(shell->cwd, path, resolved, sizeof(resolved)) != 0) {
    snprintf(out, out_size, "unable to resolve config path: %s", path);
    return 1;
  }

  if (oosh_prompt_config_load(&config, resolved) != 0) {
    snprintf(out, out_size, "unable to load config: %s", resolved);
    return 1;
  }

  shell->prompt = config;

  for (i = 0; i < shell->prompt.plugin_count; ++i) {
    if (oosh_shell_load_plugin(shell, shell->prompt.plugins[i], plugin_output, sizeof(plugin_output)) != 0) {
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

int oosh_shell_execute_line(OoshShell *shell, const char *line, char *out, size_t out_size) {
  OoshAst ast;
  char expanded_line[OOSH_MAX_LINE];
  char parse_error[OOSH_MAX_OUTPUT];
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
    char trimmed_line[OOSH_MAX_LINE];
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
        status = handle_loop_control_line(shell, trimmed_line, "break", OOSH_CONTROL_SIGNAL_BREAK, out, out_size);
        shell->last_status = status;
        return status;
      }
      if (strcmp(trimmed_line, "continue") == 0 ||
          (strncmp(trimmed_line, "continue", 8) == 0 && isspace((unsigned char) trimmed_line[8]))) {
        status = handle_loop_control_line(shell, trimmed_line, "continue", OOSH_CONTROL_SIGNAL_CONTINUE, out, out_size);
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

  if (oosh_parse_line(expanded_line, &ast, parse_error, sizeof(parse_error)) != 0) {
    snprintf(out, out_size, "%s", parse_error[0] == '\0' ? "parse error" : parse_error);
    shell->last_status = 1;
    return 1;
  }

  status = oosh_execute_ast(shell, &ast, out, out_size);
  shell->last_status = status;
  return status;
}

int oosh_shell_run_repl(OoshShell *shell) {
  char line[OOSH_MAX_LINE];
  char command[OOSH_MAX_OUTPUT];
  char prompt[OOSH_MAX_OUTPUT];
  char output[OOSH_MAX_OUTPUT];
  int interactive;

  if (shell == NULL) {
    return 1;
  }

  interactive = oosh_line_editor_is_interactive();
  if (interactive) {
    configure_shell_signals();
  }

  while (shell->running) {
    command[0] = '\0';
    if (interactive) {
      OoshLineReadStatus read_status;

      oosh_prompt_render(&shell->prompt, shell, prompt, sizeof(prompt));
      fputs(prompt, stdout);
      fflush(stdout);

      read_status = oosh_line_editor_read_line(
        shell, prompt, shell->prompt.continuation, repl_needs_more,
        command, sizeof(command)
      );
      if (read_status == OOSH_LINE_READ_EOF) {
        return 0;
      }
      if (read_status == OOSH_LINE_READ_ERROR) {
        return 1;
      }

      /* Detect and report parse errors that slipped through (e.g. heredoc). */
      if (command[0] != '\0') {
        char parse_error[OOSH_MAX_OUTPUT];
        int check = command_requires_more_input(command, parse_error, sizeof(parse_error));
        if (check < 0) {
          char message[OOSH_MAX_OUTPUT];
          snprintf(message, sizeof(message), "oosh: %s",
                   parse_error[0] == '\0' ? "parse error" : parse_error);
          write_buffer(message);
          command[0] = '\0';
        }
      }
    } else {
      int needs_more = 0;
      int pending_heredoc = 0;
      while (1) {
        char parse_error[OOSH_MAX_OUTPUT];

        if (fgets(line, sizeof(line), stdin) == NULL) {
          if (command[0] != '\0') {
            write_buffer("oosh: incomplete command block");
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
          write_buffer("oosh: command block too large");
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
          char message[OOSH_MAX_OUTPUT];
          snprintf(message, sizeof(message), "oosh: %s", parse_error[0] == '\0' ? "parse error" : parse_error);
          write_buffer(message);
          command[0] = '\0';
        }
        break;
      }
    }

    if (command[0] == '\0') {
      continue;
    }

    oosh_shell_history_add(shell, command);
    output[0] = '\0';
    oosh_shell_execute_line(shell, command, output, sizeof(output));
    write_buffer(output);
  }

  return 0;
}
