#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arksh/expand.h"
#include "arksh/executor.h"
#include "arksh/lexer.h"
#include "arksh/object.h"
#include "arksh/parser.h"
#include "arksh/perf.h"
#include "arksh/platform.h"
#include "arksh/shell.h"

static void copy_string(char *dest, size_t dest_size, const char *src) {
  if (dest_size == 0) {
    return;
  }

  snprintf(dest, dest_size, "%s", src == NULL ? "" : src);
}

static void trim_in_place(char *text) {
  size_t start = 0;
  size_t end;
  size_t len;

  if (text == NULL) {
    return;
  }

  len = strlen(text);
  while (start < len && isspace((unsigned char) text[start])) {
    start++;
  }

  end = len;
  while (end > start && isspace((unsigned char) text[end - 1])) {
    end--;
  }

  if (start > 0) {
    memmove(text, text + start, end - start);
  }
  text[end - start] = '\0';
}

static void copy_trimmed_range(const char *text, size_t start, size_t end, char *out, size_t out_size) {
  char buffer[ARKSH_MAX_LINE];
  size_t len;

  if (text == NULL || out == NULL || out_size == 0 || end < start) {
    return;
  }

  len = end - start;
  if (len >= sizeof(buffer)) {
    len = sizeof(buffer) - 1;
  }

  memcpy(buffer, text + start, len);
  buffer[len] = '\0';
  trim_in_place(buffer);
  copy_string(out, out_size, buffer);
}

static void strip_matching_quotes(char *text) {
  size_t len;

  if (text == NULL) {
    return;
  }

  len = strlen(text);
  if (len >= 2 && ((text[0] == '"' && text[len - 1] == '"') || (text[0] == '\'' && text[len - 1] == '\''))) {
    memmove(text, text + 1, len - 2);
    text[len - 2] = '\0';
  }
}

static void build_command_argv(const char argv_src[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN], int argc, char *argv_out[ARKSH_MAX_ARGS]) {
  int i;

  for (i = 0; i < ARKSH_MAX_ARGS; ++i) {
    argv_out[i] = NULL;
  }

  for (i = 0; i < argc && i < ARKSH_MAX_ARGS; ++i) {
    argv_out[i] = (char *) argv_src[i];
  }
}

static int append_output_segment(char *dest, size_t dest_size, const char *segment) {
  size_t current_len;
  size_t segment_len;

  if (dest == NULL || dest_size == 0 || segment == NULL || segment[0] == '\0') {
    return 0;
  }

  current_len = strlen(dest);
  segment_len = strlen(segment);
  if (current_len > 0) {
    if (current_len + 1 >= dest_size) {
      return 1;
    }
    dest[current_len++] = '\n';
    dest[current_len] = '\0';
  }

  if (current_len + segment_len >= dest_size) {
    return 1;
  }

  memcpy(dest + current_len, segment, segment_len + 1);
  return 0;
}

static void *allocate_temp_buffer(size_t count, size_t item_size, const char *label, char *out, size_t out_size) {
  void *buffer;

  if (count == 0 || item_size == 0) {
    return NULL;
  }

  arksh_perf_note_temp_buffer(count, item_size);
  buffer = arksh_scratch_alloc_active_zero(count, item_size);
  if (buffer == NULL && out != NULL && out_size > 0) {
    snprintf(out, out_size, "unable to allocate %s", label == NULL ? "temporary buffer" : label);
  }
  return buffer;
}

static int expand_single_word(
  ArkshShell *shell,
  const char *raw,
  ArkshExpandMode mode,
  char *out,
  size_t out_size,
  char *error,
  size_t error_size
) {
  char values[1][ARKSH_MAX_TOKEN];
  int count = 0;

  if (out == NULL || out_size == 0 || error == NULL || error_size == 0) {
    return 1;
  }

  if (arksh_expand_word(shell, raw, mode, values, 1, &count, error, error_size) != 0) {
    return 1;
  }

  if (count != 1) {
    snprintf(error, error_size, "expansion produced an unexpected number of values");
    return 1;
  }

  copy_string(out, out_size, values[0]);
  return 0;
}

static int expand_command_arguments(
  ArkshShell *shell,
  const char raw_argv[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN],
  int argc,
  char expanded_argv[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN],
  int *out_argc,
  char *error,
  size_t error_size
) {
  int expanded_count = 0;
  int i;

  if (raw_argv == NULL || expanded_argv == NULL || out_argc == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  for (i = 0; i < argc; ++i) {
    char values[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
    int value_count = 0;
    ArkshExpandMode mode = i == 0 ? ARKSH_EXPAND_MODE_COMMAND_NAME : ARKSH_EXPAND_MODE_COMMAND;

    if (arksh_expand_word(shell, raw_argv[i], mode, values, ARKSH_MAX_ARGS, &value_count, error, error_size) != 0) {
      return 1;
    }

    if (expanded_count + value_count > ARKSH_MAX_ARGS) {
      snprintf(error, error_size, "expanded command has too many arguments");
      return 1;
    }

    for (int j = 0; j < value_count; ++j) {
      copy_string(expanded_argv[expanded_count], sizeof(expanded_argv[expanded_count]), values[j]);
      expanded_count++;
    }
  }

  *out_argc = expanded_count;
  return 0;
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

static int write_text_file(const char *path, const char *text, int append, char *out, size_t out_size) {
  char platform_error[ARKSH_MAX_OUTPUT];

  if (path == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (arksh_platform_write_text_file(path, text, append, platform_error, sizeof(platform_error)) != 0) {
    snprintf(out, out_size, "unable to open redirect target: %s", path);
    return 1;
  }

  return 0;
}

static int parse_unsigned_number(const char *text, size_t *out_value) {
  char *endptr = NULL;
  unsigned long value;

  if (text == NULL || out_value == NULL || text[0] == '\0') {
    return 1;
  }

  value = strtoul(text, &endptr, 10);
  if (endptr == text || *endptr != '\0') {
    return 1;
  }

  *out_value = (size_t) value;
  return 0;
}

static int find_top_level_equality_operator(const char *text, size_t *out_index);

static int parse_where_condition(const char *raw_args, char *property, size_t property_size, char *value, size_t value_size) {
  size_t operator_index;
  char left[ARKSH_MAX_NAME];
  char right[ARKSH_MAX_TOKEN];

  if (raw_args == NULL || property == NULL || value == NULL) {
    return 1;
  }

  if (find_top_level_equality_operator(raw_args, &operator_index) != 0) {
    return 1;
  }

  copy_trimmed_range(raw_args, 0, operator_index, left, sizeof(left));
  copy_string(property, property_size, left);
  copy_trimmed_range(raw_args, operator_index + 2, strlen(raw_args), right, sizeof(right));
  strip_matching_quotes(right);
  copy_string(value, value_size, right);
  return property[0] == '\0' || value[0] == '\0' ? 1 : 0;
}

static int is_unsigned_text(const char *text) {
  size_t i;

  if (text == NULL || text[0] == '\0') {
    return 0;
  }

  for (i = 0; text[i] != '\0'; ++i) {
    if (!isdigit((unsigned char) text[i])) {
      return 0;
    }
  }

  return 1;
}

typedef struct {
  int is_render;
  ArkshMemberKind member_kind;
  char name[ARKSH_MAX_NAME];
  char argv[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
  char raw_argv[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
  int argc;
} ArkshEachSelector;

typedef struct {
  int existed;
  char name[ARKSH_MAX_NAME];
  ArkshValue previous;
} ArkshBlockBindingSnapshot;

typedef struct {
  ArkshShellVar vars[ARKSH_MAX_SHELL_VARS];
  size_t var_count;
  ArkshValueBinding bindings[ARKSH_MAX_VALUE_BINDINGS];
  size_t binding_count;
  char positional_params[ARKSH_MAX_POSITIONAL_PARAMS][ARKSH_MAX_VAR_VALUE];
  int positional_count;
} ArkshFunctionScopeSnapshot;

typedef struct {
  int running;
  int last_status;
  int next_instance_id;
  int next_job_id;
  int loading_plugin_index;
  char cwd[ARKSH_MAX_PATH];
  ArkshPromptConfig prompt;
  ArkshShellVar vars[ARKSH_MAX_SHELL_VARS];
  size_t var_count;
  ArkshValueBinding bindings[ARKSH_MAX_VALUE_BINDINGS];
  size_t binding_count;
  ArkshAlias aliases[ARKSH_MAX_ALIASES];
  size_t alias_count;
  ArkshShellFunction functions[ARKSH_MAX_FUNCTIONS];
  size_t function_count;
  ArkshClassDef classes[ARKSH_MAX_CLASSES];
  size_t class_count;
  ArkshClassInstance instances[ARKSH_MAX_INSTANCES];
  size_t instance_count;
  ArkshObjectExtension extensions[ARKSH_MAX_EXTENSIONS];
  size_t extension_count;
  ArkshValueResolverDef value_resolvers[ARKSH_MAX_VALUE_RESOLVERS];
  size_t value_resolver_count;
  ArkshPipelineStageDef pipeline_stages[ARKSH_MAX_PIPELINE_STAGE_HANDLERS];
  size_t pipeline_stage_count;
  ArkshCommandDef commands[ARKSH_MAX_COMMANDS];
  size_t command_count;
  ArkshLoadedPlugin plugins[ARKSH_MAX_PLUGINS];
  size_t plugin_count;
  ArkshJob jobs[ARKSH_MAX_JOBS];
  size_t job_count;
} ArkshShellStateSnapshot;

#define ARKSH_MAX_BLOCK_LOCALS ARKSH_MAX_ARGS

static int parse_number_text(const char *text, double *out_value) {
  char *endptr = NULL;

  if (text == NULL || out_value == NULL || text[0] == '\0') {
    return 1;
  }

  *out_value = strtod(text, &endptr);
  return endptr == text || *endptr != '\0' ? 1 : 0;
}

static int is_identifier_text(const char *text) {
  size_t i;

  if (text == NULL || text[0] == '\0') {
    return 0;
  }

  if (!(isalpha((unsigned char) text[0]) || text[0] == '_')) {
    return 0;
  }

  for (i = 1; text[i] != '\0'; ++i) {
    if (!(isalnum((unsigned char) text[i]) || text[i] == '_')) {
      return 0;
    }
  }

  return 1;
}

static int is_builtin_object_method_name(const char *name) {
  return name != NULL &&
         (strcmp(name, "children") == 0 ||
          strcmp(name, "read_text") == 0 ||
          strcmp(name, "parent") == 0 ||
          strcmp(name, "describe") == 0);
}

static int is_value_token_kind(ArkshTokenKind kind) {
  return kind == ARKSH_TOKEN_WORD || kind == ARKSH_TOKEN_STRING;
}

static int set_item_from_text(const char *text, ArkshValueItem *out_item) {
  double number;

  if (text == NULL || out_item == NULL) {
    return 1;
  }

  arksh_value_item_init(out_item);
  if (parse_number_text(text, &number) == 0) {
    out_item->kind = ARKSH_VALUE_NUMBER;
    out_item->number = number;
    return 0;
  }

  if (strcmp(text, "true") == 0 || strcmp(text, "false") == 0) {
    out_item->kind = ARKSH_VALUE_BOOLEAN;
    out_item->boolean = strcmp(text, "true") == 0;
    return 0;
  }

  out_item->kind = ARKSH_VALUE_STRING;
  copy_string(out_item->text, sizeof(out_item->text), text);
  return 0;
}

static int set_item_from_value(const ArkshValue *value, ArkshValueItem *out_item, char *out, size_t out_size) {
  if (value == NULL || out_item == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (arksh_value_item_set_from_value(out_item, value) != 0) {
    snprintf(out, out_size, "unable to convert value to list item");
    return 1;
  }

  return 0;
}

static int evaluate_token_argument_value(ArkshShell *shell, const char *raw_text, ArkshValue *out_value, char *out, size_t out_size) {
  const ArkshValue *binding_value;
  char expanded[ARKSH_MAX_TOKEN];
  ArkshObject object;
  double number;

  if (shell == NULL || raw_text == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  binding_value = arksh_shell_get_binding(shell, raw_text);
  if (binding_value != NULL) {
    return arksh_value_copy(out_value, binding_value);
  }

  {
    char nested_error[ARKSH_MAX_OUTPUT];

    nested_error[0] = '\0';
    if (arksh_evaluate_line_value(shell, raw_text, out_value, nested_error, sizeof(nested_error)) == 0) {
      return 0;
    }
  }

  if (expand_single_word(shell, raw_text, ARKSH_EXPAND_MODE_OBJECT_ARGUMENT, expanded, sizeof(expanded), out, out_size) != 0) {
    return 1;
  }

  if (parse_number_text(expanded, &number) == 0) {
    arksh_value_set_number(out_value, number);
    return 0;
  }

  if (strcmp(expanded, "true") == 0 || strcmp(expanded, "false") == 0) {
    arksh_value_set_boolean(out_value, strcmp(expanded, "true") == 0);
    return 0;
  }

  if (arksh_object_resolve(shell->cwd, expanded, &object) == 0) {
    arksh_value_set_object(out_value, &object);
    return 0;
  }

  arksh_value_set_string(out_value, expanded);
  return 0;
}

static int value_is_truthy(const ArkshValue *value) {
  if (value == NULL) {
    return 0;
  }

  switch (value->kind) {
    case ARKSH_VALUE_BOOLEAN:
      return value->boolean != 0;
    case ARKSH_VALUE_NUMBER:
    case ARKSH_VALUE_INTEGER:
    case ARKSH_VALUE_FLOAT:
    case ARKSH_VALUE_DOUBLE:
    case ARKSH_VALUE_IMAGINARY:
      return value->number != 0.0;
    case ARKSH_VALUE_STRING:
      return value->text[0] != '\0' && strcmp(value->text, "false") != 0 && strcmp(value->text, "0") != 0;
    case ARKSH_VALUE_OBJECT:
    case ARKSH_VALUE_BLOCK:
      return 1;
    case ARKSH_VALUE_LIST:
      return value->list.count > 0;
    case ARKSH_VALUE_MAP:
    case ARKSH_VALUE_DICT:
      return value->map.count > 0;
    case ARKSH_VALUE_MATRIX:
      return value->matrix != NULL && value->matrix->row_count > 0;
    case ARKSH_VALUE_EMPTY:
    default:
      return 0;
  }
}

static int compare_rendered_values(const ArkshValue *left, const ArkshValue *right, int *out_equal, char *out, size_t out_size) {
  char left_rendered[ARKSH_MAX_OUTPUT];
  char right_rendered[ARKSH_MAX_OUTPUT];

  if (left == NULL || right == NULL || out_equal == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (arksh_value_render(left, left_rendered, sizeof(left_rendered)) != 0 ||
      arksh_value_render(right, right_rendered, sizeof(right_rendered)) != 0) {
    snprintf(out, out_size, "unable to compare values");
    return 1;
  }

  *out_equal = strcmp(left_rendered, right_rendered) == 0;
  return 0;
}

static int find_top_level_equality_operator(const char *text, size_t *out_index) {
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
    if (c == '[') {
      bracket_depth++;
      continue;
    }
    if (c == ']' && bracket_depth > 0) {
      bracket_depth--;
      continue;
    }
    if (paren_depth == 0 && bracket_depth == 0 && c == '=' && text[i + 1] == '=') {
      *out_index = i;
      return 0;
    }
  }

  return 1;
}

static int find_top_level_ternary_operator(const char *text, size_t *out_question, size_t *out_colon) {
  size_t i;
  char quote = '\0';
  int paren_depth = 0;
  int bracket_depth = 0;
  int saw_question = 0;

  if (text == NULL || out_question == NULL || out_colon == NULL) {
    return 1;
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
    if (c == '[') {
      bracket_depth++;
      continue;
    }
    if (c == ']' && bracket_depth > 0) {
      bracket_depth--;
      continue;
    }

    if (paren_depth == 0 && bracket_depth == 0) {
      if (c == '?' && !saw_question) {
        *out_question = i;
        saw_question = 1;
      } else if (c == ':' && saw_question) {
        *out_colon = i;
        return 0;
      }
    }
  }

  return 1;
}

static int split_top_level_arguments(const char *text, char parts[][ARKSH_MAX_LINE], int max_parts, int *out_count) {
  size_t i;
  size_t start = 0;
  char quote = '\0';
  int paren_depth = 0;
  int bracket_depth = 0;
  int count = 0;

  if (text == NULL || parts == NULL || max_parts <= 0 || out_count == NULL) {
    return 1;
  }

  *out_count = 0;
  for (i = 0; ; ++i) {
    char c = text[i];
    int at_end = c == '\0';

    if (!at_end && quote != '\0') {
      if (c == quote) {
        quote = '\0';
      } else if (quote == '"' && c == '\\' && text[i + 1] != '\0') {
        i++;
      }
      continue;
    }

    if (!at_end) {
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
    }

    if (at_end || (c == ',' && quote == '\0' && paren_depth == 0 && bracket_depth == 0)) {
      if (count >= max_parts) {
        return 1;
      }
      copy_trimmed_range(text, start, i, parts[count], ARKSH_MAX_LINE);
      count++;
      if (at_end) {
        break;
      }
      start = i + 1;
    }
  }

  *out_count = count;
  return 0;
}

static int find_top_level_plus_positions(const char *text, size_t positions[], size_t max_positions, size_t *out_count) {
  size_t i;
  char quote = '\0';
  int paren_depth = 0;
  int bracket_depth = 0;
  size_t count = 0;

  if (text == NULL || positions == NULL || out_count == NULL) {
    return 1;
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
    if (c == '[') {
      bracket_depth++;
      continue;
    }
    if (c == ']' && bracket_depth > 0) {
      bracket_depth--;
      continue;
    }
    if (c == '+' && paren_depth == 0 && bracket_depth == 0) {
      if (count >= max_positions) {
        return 1;
      }
      positions[count++] = i;
    }
  }

  *out_count = count;
  return 0;
}

static int find_next_top_level_semicolon(const char *text, size_t start, size_t *out_index) {
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
    if (c == '[') {
      bracket_depth++;
      continue;
    }
    if (c == ']' && bracket_depth > 0) {
      bracket_depth--;
      continue;
    }
    if (c == ';' && paren_depth == 0 && bracket_depth == 0) {
      *out_index = i;
      return 0;
    }
  }

  return 1;
}

static int find_top_level_assignment_operator(const char *text, size_t *out_index) {
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

static int parse_block_local_statement(
  const char *text,
  char *name,
  size_t name_size,
  char *expression,
  size_t expression_size,
  char *out,
  size_t out_size
) {
  char trimmed[ARKSH_MAX_LINE];
  char left[ARKSH_MAX_LINE];
  size_t operator_index = 0;

  if (text == NULL || name == NULL || name_size == 0 || expression == NULL || expression_size == 0 || out == NULL || out_size == 0) {
    return 1;
  }

  copy_string(trimmed, sizeof(trimmed), text);
  trim_in_place(trimmed);
  if (strncmp(trimmed, "local", 5) != 0 || (trimmed[5] != '\0' && !isspace((unsigned char) trimmed[5]))) {
    return 1;
  }

  if (find_top_level_assignment_operator(trimmed + 5, &operator_index) != 0) {
    snprintf(out, out_size, "local requires syntax like local name = expression");
    return 2;
  }

  copy_trimmed_range(trimmed, 5, 5 + operator_index, left, sizeof(left));
  copy_trimmed_range(trimmed, 5 + operator_index + 1, strlen(trimmed), expression, expression_size);

  if (!is_identifier_text(left)) {
    snprintf(out, out_size, "invalid local binding name: %s", left[0] == '\0' ? "<empty>" : left);
    return 2;
  }
  if (expression[0] == '\0') {
    snprintf(out, out_size, "local binding requires a right-hand expression");
    return 2;
  }

  copy_string(name, name_size, left);
  return 0;
}

static int add_values(const ArkshValue *left, const ArkshValue *right, ArkshValue *out_value, char *out, size_t out_size) {
  char left_text[ARKSH_MAX_OUTPUT];
  char right_text[ARKSH_MAX_OUTPUT];

  if (left == NULL || right == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (left->kind == ARKSH_VALUE_NUMBER && right->kind == ARKSH_VALUE_NUMBER) {
    arksh_value_set_number(out_value, left->number + right->number);
    return 0;
  }

  if (arksh_value_render(left, left_text, sizeof(left_text)) != 0 || arksh_value_render(right, right_text, sizeof(right_text)) != 0) {
    snprintf(out, out_size, "unable to evaluate '+' expression");
    return 1;
  }

  if (snprintf(out_value->text, sizeof(out_value->text), "%s%s", left_text, right_text) >= (int) sizeof(out_value->text)) {
    snprintf(out, out_size, "string result is too large");
    return 1;
  }
  out_value->kind = ARKSH_VALUE_STRING;
  return 0;
}

static int evaluate_additive_expression(ArkshShell *shell, const char *text, ArkshValue *out_value, char *out, size_t out_size) {
  size_t positions[ARKSH_MAX_ARGS];
  size_t plus_count = 0;
  size_t segment_start = 0;
  size_t i;
  ArkshValue *temp_values;

  if (shell == NULL || text == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (find_top_level_plus_positions(text, positions, ARKSH_MAX_ARGS, &plus_count) != 0 || plus_count == 0) {
    return 1;
  }

  temp_values = (ArkshValue *) allocate_temp_buffer(2, sizeof(*temp_values), "additive expression values", out, out_size);
  if (temp_values == NULL) {
    return 1;
  }

  for (i = 0; i <= plus_count; ++i) {
    char segment[ARKSH_MAX_LINE];
    size_t segment_end = i < plus_count ? positions[i] : strlen(text);

    copy_trimmed_range(text, segment_start, segment_end, segment, sizeof(segment));
    if (segment[0] == '\0') {
      free(temp_values);
      snprintf(out, out_size, "invalid '+' expression");
      return 1;
    }

    if (arksh_evaluate_line_value(shell, segment, &temp_values[i == 0 ? 0 : 1], out, out_size) != 0) {
      free(temp_values);
      return 1;
    }

    if (i > 0 && add_values(&temp_values[0], &temp_values[1], &temp_values[0], out, out_size) != 0) {
      free(temp_values);
      return 1;
    }

    segment_start = segment_end + 1;
  }

  *out_value = temp_values[0];
  free(temp_values);
  return 0;
}

static int evaluate_ast_value(ArkshShell *shell, const ArkshAst *ast, ArkshValue *value, char *out, size_t out_size);
static int resolve_stage_block_argument(ArkshShell *shell, const char *text, ArkshBlock *out_block, char *out, size_t out_size);
static int evaluate_expression_text(ArkshShell *shell, const char *text, ArkshValue *out_value, char *out, size_t out_size);
static int evaluate_block_body(ArkshShell *shell, const char *body, ArkshValue *out_value, ArkshBlockBindingSnapshot *local_snapshots, int *out_local_count, char *out, size_t out_size);
static int render_value_for_shell_var(const ArkshValue *value, char *out, size_t out_size);
static int evaluate_line_value_heap(ArkshShell *shell, const char *line, ArkshValue *value, char *out, size_t out_size);
static int evaluate_binary_expr_iterative(ArkshShell *shell, const char *text, ArkshValue *value, char *out, size_t out_size);

static int evaluate_value_text_core(ArkshShell *shell, const char *text, ArkshValue *out_value, char *out, size_t out_size) {
  ArkshAst *ast;
  char parse_error[ARKSH_MAX_OUTPUT];
  int result;

  if (shell == NULL || text == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  ast = (ArkshAst *) arksh_scratch_alloc_active_zero(1, sizeof(*ast));
  if (ast == NULL) {
    snprintf(out, out_size, "out of memory");
    return 1;
  }

  parse_error[0] = '\0';
  if (arksh_parse_value_line(text, ast, parse_error, sizeof(parse_error)) != 0) {
    if (parse_error[0] != '\0') {
      copy_string(out, out_size, parse_error);
    }
    return 1;
  }

  result = evaluate_ast_value(shell, ast, out_value, out, out_size);
  return result;
}

static int evaluate_expression_atom(ArkshShell *shell, const char *text, ArkshValue *out_value, char *out, size_t out_size) {
  char trimmed[ARKSH_MAX_LINE];
  char expanded[ARKSH_MAX_OUTPUT];
  ArkshObject object;
  double number;

  if (shell == NULL || text == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  copy_string(trimmed, sizeof(trimmed), text);
  trim_in_place(trimmed);
  if (trimmed[0] == '\0') {
    arksh_value_init(out_value);
    return 0;
  }

  if (evaluate_value_text_core(shell, trimmed, out_value, out, out_size) == 0) {
    return 0;
  }

  out[0] = '\0';
  if (expand_single_word(shell, trimmed, ARKSH_EXPAND_MODE_OBJECT_ARGUMENT, expanded, sizeof(expanded), out, out_size) != 0) {
    return 1;
  }

  if (parse_number_text(expanded, &number) == 0) {
    arksh_value_set_number(out_value, number);
    return 0;
  }
  if (strcmp(expanded, "true") == 0 || strcmp(expanded, "false") == 0) {
    arksh_value_set_boolean(out_value, strcmp(expanded, "true") == 0);
    return 0;
  }
  if (arksh_object_resolve(shell->cwd, expanded, &object) == 0) {
    arksh_value_set_object(out_value, &object);
    return 0;
  }

  arksh_value_set_string(out_value, expanded);
  return 0;
}

static int evaluate_expression_text(ArkshShell *shell, const char *text, ArkshValue *out_value, char *out, size_t out_size) {
  char trimmed[ARKSH_MAX_LINE];
  size_t question_index = 0;
  size_t colon_index = 0;
  size_t eq_index = 0;

  if (shell == NULL || text == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  copy_string(trimmed, sizeof(trimmed), text);
  trim_in_place(trimmed);
  if (trimmed[0] == '\0') {
    arksh_value_init(out_value);
    return 0;
  }

  if (find_top_level_ternary_operator(trimmed, &question_index, &colon_index) == 0) {
    char condition[ARKSH_MAX_LINE];
    char true_branch[ARKSH_MAX_LINE];
    char false_branch[ARKSH_MAX_LINE];
    ArkshValue *condition_value;
    int condition_truthy;

    copy_trimmed_range(trimmed, 0, question_index, condition, sizeof(condition));
    copy_trimmed_range(trimmed, question_index + 1, colon_index, true_branch, sizeof(true_branch));
    copy_trimmed_range(trimmed, colon_index + 1, strlen(trimmed), false_branch, sizeof(false_branch));
    if (condition[0] == '\0' || true_branch[0] == '\0' || false_branch[0] == '\0') {
      snprintf(out, out_size, "ternary expression requires condition, true branch and false branch");
      return 1;
    }

    condition_value = (ArkshValue *) allocate_temp_buffer(1, sizeof(*condition_value), "ternary condition", out, out_size);
    if (condition_value == NULL) {
      return 1;
    }

    if (evaluate_expression_text(shell, condition, condition_value, out, out_size) != 0) {
      arksh_value_free(condition_value);
      free(condition_value);
      return 1;
    }

    condition_truthy = value_is_truthy(condition_value);
    arksh_value_free(condition_value);
    free(condition_value);
    return evaluate_expression_text(shell, condition_truthy ? true_branch : false_branch, out_value, out, out_size);
  }

  if (find_top_level_equality_operator(trimmed, &eq_index) == 0) {
    char left_text[ARKSH_MAX_LINE];
    char right_text[ARKSH_MAX_LINE];
    ArkshValue *comparison_values;
    int is_equal = 0;

    copy_trimmed_range(trimmed, 0, eq_index, left_text, sizeof(left_text));
    copy_trimmed_range(trimmed, eq_index + 2, strlen(trimmed), right_text, sizeof(right_text));
    if (left_text[0] == '\0' || right_text[0] == '\0') {
      snprintf(out, out_size, "invalid equality expression");
      return 1;
    }

    comparison_values = (ArkshValue *) allocate_temp_buffer(2, sizeof(*comparison_values), "comparison values", out, out_size);
    if (comparison_values == NULL) {
      return 1;
    }

    if (evaluate_expression_text(shell, left_text, &comparison_values[0], out, out_size) != 0) {
      arksh_value_free(&comparison_values[0]);
      arksh_value_free(&comparison_values[1]);
      free(comparison_values);
      return 1;
    }
    if (evaluate_expression_text(shell, right_text, &comparison_values[1], out, out_size) != 0) {
      arksh_value_free(&comparison_values[0]);
      arksh_value_free(&comparison_values[1]);
      free(comparison_values);
      return 1;
    }

    if (compare_rendered_values(&comparison_values[0], &comparison_values[1], &is_equal, out, out_size) != 0) {
      arksh_value_free(&comparison_values[0]);
      arksh_value_free(&comparison_values[1]);
      free(comparison_values);
      return 1;
    }

    arksh_value_set_boolean(out_value, is_equal);
    arksh_value_free(&comparison_values[0]);
    arksh_value_free(&comparison_values[1]);
    free(comparison_values);
    return 0;
  }

  if (evaluate_additive_expression(shell, trimmed, out_value, out, out_size) == 0) {
    return 0;
  }

  out[0] = '\0';
  return evaluate_expression_atom(shell, trimmed, out_value, out, out_size);
}

static int snapshot_binding(
  ArkshShell *shell,
  const char *name,
  ArkshBlockBindingSnapshot *snapshot,
  char *out,
  size_t out_size
) {
  const ArkshValue *existing;

  if (shell == NULL || name == NULL || snapshot == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  memset(snapshot, 0, sizeof(*snapshot));
  copy_string(snapshot->name, sizeof(snapshot->name), name);
  existing = arksh_shell_get_binding(shell, name);
  if (existing == NULL) {
    return 0;
  }

  snapshot->existed = 1;
  if (arksh_value_copy(&snapshot->previous, existing) != 0) {
    snprintf(out, out_size, "unable to snapshot binding: %s", name);
    return 1;
  }

  return 0;
}

static int bind_block_arguments(
  ArkshShell *shell,
  const ArkshBlock *block,
  const ArkshValue *args,
  int argc,
  ArkshBlockBindingSnapshot snapshots[ARKSH_MAX_BLOCK_PARAMS],
  char *out,
  size_t out_size
) {
  int i;

  if (shell == NULL || block == NULL || snapshots == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (argc != block->param_count) {
    snprintf(out, out_size, "block expects %d arguments, got %d", block->param_count, argc);
    return 1;
  }

  for (i = 0; i < block->param_count; ++i) {
    if (snapshot_binding(shell, block->params[i], &snapshots[i], out, out_size) != 0) {
      int rollback;

      for (rollback = i - 1; rollback >= 0; --rollback) {
        if (snapshots[rollback].existed) {
          arksh_shell_set_binding(shell, snapshots[rollback].name, &snapshots[rollback].previous);
        } else {
          arksh_shell_unset_binding(shell, snapshots[rollback].name);
        }
        arksh_value_free(&snapshots[rollback].previous);
      }
      return 1;
    }
    if (arksh_shell_set_binding(shell, block->params[i], &args[i]) != 0) {
      int rollback;

      for (rollback = i - 1; rollback >= 0; --rollback) {
        if (snapshots[rollback].existed) {
          arksh_shell_set_binding(shell, snapshots[rollback].name, &snapshots[rollback].previous);
        } else {
          arksh_shell_unset_binding(shell, snapshots[rollback].name);
        }
        arksh_value_free(&snapshots[rollback].previous);
      }
      arksh_value_free(&snapshots[i].previous);
      snprintf(out, out_size, "unable to bind block argument: %s", block->params[i]);
      return 1;
    }
  }

  return 0;
}

static void restore_block_arguments(ArkshShell *shell, ArkshBlockBindingSnapshot snapshots[ARKSH_MAX_BLOCK_PARAMS], int count) {
  int i;

  if (shell == NULL || snapshots == NULL) {
    return;
  }

  for (i = count - 1; i >= 0; --i) {
    if (snapshots[i].name[0] == '\0') {
      continue;
    }
    if (snapshots[i].existed) {
      arksh_shell_set_binding(shell, snapshots[i].name, &snapshots[i].previous);
    } else {
      arksh_shell_unset_binding(shell, snapshots[i].name);
    }
    arksh_value_free(&snapshots[i].previous);
  }
}

static void free_function_scope_snapshot(ArkshFunctionScopeSnapshot *snapshot) {
  size_t i;

  if (snapshot == NULL) {
    return;
  }

  for (i = 0; i < snapshot->binding_count; ++i) {
    arksh_value_free(&snapshot->bindings[i].value);
  }

  memset(snapshot, 0, sizeof(*snapshot));
}

static int snapshot_function_scope(
  ArkshShell *shell,
  ArkshFunctionScopeSnapshot *snapshot,
  char *out,
  size_t out_size
) {
  size_t i;

  if (shell == NULL || snapshot == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->var_count = shell->var_count;
  memcpy(snapshot->vars, shell->vars, shell->var_count * sizeof(shell->vars[0]));
  snapshot->binding_count = shell->binding_count;

  for (i = 0; i < shell->binding_count; ++i) {
    copy_string(snapshot->bindings[i].name, sizeof(snapshot->bindings[i].name), shell->bindings[i].name);
    if (arksh_value_copy(&snapshot->bindings[i].value, &shell->bindings[i].value) != 0) {
      free_function_scope_snapshot(snapshot);
      snprintf(out, out_size, "unable to snapshot function scope");
      return 1;
    }
  }

  snapshot->positional_count = shell->positional_count;
  for (i = 0; i < (size_t) shell->positional_count && i < ARKSH_MAX_POSITIONAL_PARAMS; ++i) {
    copy_string(snapshot->positional_params[i], sizeof(snapshot->positional_params[i]), shell->positional_params[i]);
  }

  return 0;
}

static int restore_function_scope(
  ArkshShell *shell,
  ArkshFunctionScopeSnapshot *snapshot,
  char *out,
  size_t out_size
) {
  size_t i;

  if (shell == NULL || snapshot == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  while (shell->var_count > 0) {
    char name[ARKSH_MAX_VAR_NAME];

    copy_string(name, sizeof(name), shell->vars[shell->var_count - 1].name);
    if (arksh_shell_unset_var(shell, name) != 0) {
      snprintf(out, out_size, "unable to restore function variable scope");
      return 1;
    }
  }

  for (i = 0; i < snapshot->var_count; ++i) {
    if (arksh_shell_set_var(shell, snapshot->vars[i].name, snapshot->vars[i].value, snapshot->vars[i].exported) != 0) {
      snprintf(out, out_size, "unable to restore function variable scope");
      return 1;
    }
  }

  for (i = 0; i < shell->binding_count; ++i) {
    arksh_value_free(&shell->bindings[i].value);
  }
  shell->binding_count = 0;

  for (i = 0; i < snapshot->binding_count; ++i) {
    copy_string(shell->bindings[i].name, sizeof(shell->bindings[i].name), snapshot->bindings[i].name);
    if (arksh_value_copy(&shell->bindings[i].value, &snapshot->bindings[i].value) != 0) {
      snprintf(out, out_size, "unable to restore function binding scope");
      return 1;
    }
    shell->binding_count++;
  }

  shell->positional_count = snapshot->positional_count;
  for (i = 0; i < (size_t) snapshot->positional_count && i < ARKSH_MAX_POSITIONAL_PARAMS; ++i) {
    copy_string(shell->positional_params[i], sizeof(shell->positional_params[i]), snapshot->positional_params[i]);
  }

  return 0;
}

static void free_class_definition_contents_snapshot(ArkshClassDef *class_def) {
  size_t i;

  if (class_def == NULL) {
    return;
  }

  for (i = 0; i < class_def->property_count; ++i) {
    arksh_value_free(&class_def->properties[i].default_value);
  }

  memset(class_def, 0, sizeof(*class_def));
}

static int copy_class_definition_snapshot(ArkshClassDef *dest, const ArkshClassDef *src) {
  size_t i;

  if (dest == NULL || src == NULL) {
    return 1;
  }

  memset(dest, 0, sizeof(*dest));
  copy_string(dest->name, sizeof(dest->name), src->name);
  copy_string(dest->source, sizeof(dest->source), src->source);
  dest->base_count = src->base_count;
  for (i = 0; i < (size_t) src->base_count && i < ARKSH_MAX_CLASS_BASES; ++i) {
    copy_string(dest->bases[i], sizeof(dest->bases[i]), src->bases[i]);
  }

  dest->property_count = src->property_count;
  for (i = 0; i < src->property_count; ++i) {
    copy_string(dest->properties[i].name, sizeof(dest->properties[i].name), src->properties[i].name);
    if (arksh_value_copy(&dest->properties[i].default_value, &src->properties[i].default_value) != 0) {
      free_class_definition_contents_snapshot(dest);
      return 1;
    }
  }

  dest->method_count = src->method_count;
  for (i = 0; i < src->method_count; ++i) {
    copy_string(dest->methods[i].name, sizeof(dest->methods[i].name), src->methods[i].name);
    dest->methods[i].block = src->methods[i].block;
  }

  return 0;
}

static int copy_class_instance_snapshot(ArkshClassInstance *dest, const ArkshClassInstance *src) {
  if (dest == NULL || src == NULL) {
    return 1;
  }

  memset(dest, 0, sizeof(*dest));
  dest->id = src->id;
  copy_string(dest->class_name, sizeof(dest->class_name), src->class_name);
  if (arksh_value_copy(&dest->fields, &src->fields) != 0) {
    memset(dest, 0, sizeof(*dest));
    return 1;
  }

  return 0;
}

static void free_shell_state_snapshot(ArkshShellStateSnapshot *snapshot) {
  size_t i;

  if (snapshot == NULL) {
    return;
  }

  for (i = 0; i < snapshot->binding_count; ++i) {
    arksh_value_free(&snapshot->bindings[i].value);
  }
  for (i = 0; i < snapshot->class_count; ++i) {
    free_class_definition_contents_snapshot(&snapshot->classes[i]);
  }
  for (i = 0; i < snapshot->instance_count; ++i) {
    arksh_value_free(&snapshot->instances[i].fields);
  }

  memset(snapshot, 0, sizeof(*snapshot));
}

static int snapshot_shell_state(
  ArkshShell *shell,
  ArkshShellStateSnapshot *snapshot,
  char *out,
  size_t out_size
) {
  size_t i;

  if (shell == NULL || snapshot == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->running = shell->running;
  snapshot->last_status = shell->last_status;
  snapshot->next_instance_id = shell->next_instance_id;
  snapshot->next_job_id = shell->next_job_id;
  snapshot->loading_plugin_index = shell->loading_plugin_index;
  copy_string(snapshot->cwd, sizeof(snapshot->cwd), shell->cwd);
  snapshot->prompt = shell->prompt;

  snapshot->var_count = shell->var_count;
  memcpy(snapshot->vars, shell->vars, shell->var_count * sizeof(shell->vars[0]));

  snapshot->binding_count = shell->binding_count;
  for (i = 0; i < shell->binding_count; ++i) {
    copy_string(snapshot->bindings[i].name, sizeof(snapshot->bindings[i].name), shell->bindings[i].name);
    if (arksh_value_copy(&snapshot->bindings[i].value, &shell->bindings[i].value) != 0) {
      free_shell_state_snapshot(snapshot);
      snprintf(out, out_size, "unable to snapshot subshell bindings");
      return 1;
    }
  }

  snapshot->alias_count = shell->alias_count;
  memcpy(snapshot->aliases, shell->aliases, shell->alias_count * sizeof(shell->aliases[0]));

  snapshot->function_count = shell->function_count;
  memcpy(snapshot->functions, shell->functions, shell->function_count * sizeof(shell->functions[0]));

  snapshot->class_count = shell->class_count;
  for (i = 0; i < shell->class_count; ++i) {
    if (copy_class_definition_snapshot(&snapshot->classes[i], &shell->classes[i]) != 0) {
      free_shell_state_snapshot(snapshot);
      snprintf(out, out_size, "unable to snapshot subshell classes");
      return 1;
    }
  }

  snapshot->instance_count = shell->instance_count;
  for (i = 0; i < shell->instance_count; ++i) {
    if (copy_class_instance_snapshot(&snapshot->instances[i], &shell->instances[i]) != 0) {
      free_shell_state_snapshot(snapshot);
      snprintf(out, out_size, "unable to snapshot subshell instances");
      return 1;
    }
  }

  snapshot->extension_count = shell->extension_count;
  memcpy(snapshot->extensions, shell->extensions, shell->extension_count * sizeof(shell->extensions[0]));

  snapshot->value_resolver_count = shell->value_resolver_count;
  memcpy(snapshot->value_resolvers, shell->value_resolvers, shell->value_resolver_count * sizeof(shell->value_resolvers[0]));

  snapshot->pipeline_stage_count = shell->pipeline_stage_count;
  memcpy(snapshot->pipeline_stages, shell->pipeline_stages, shell->pipeline_stage_count * sizeof(shell->pipeline_stages[0]));

  snapshot->command_count = shell->command_count;
  memcpy(snapshot->commands, shell->commands, shell->command_count * sizeof(shell->commands[0]));

  snapshot->plugin_count = shell->plugin_count;
  memcpy(snapshot->plugins, shell->plugins, shell->plugin_count * sizeof(shell->plugins[0]));

  snapshot->job_count = shell->job_count;
  memcpy(snapshot->jobs, shell->jobs, shell->job_count * sizeof(shell->jobs[0]));
  return 0;
}

static void close_subshell_side_effects(ArkshShell *shell, const ArkshShellStateSnapshot *snapshot) {
  size_t i;

  if (shell == NULL || snapshot == NULL) {
    return;
  }

  for (i = snapshot->job_count; i < shell->job_count; ++i) {
    arksh_platform_close_background_process(&shell->jobs[i].process);
  }

  for (i = snapshot->plugin_count; i < shell->plugin_count; ++i) {
    ArkshPluginShutdownFn shutdown_fn;

    if (shell->plugins[i].handle == NULL) {
      continue;
    }

    shutdown_fn = (ArkshPluginShutdownFn) arksh_platform_library_symbol(shell->plugins[i].handle, "arksh_plugin_shutdown");
    if (shutdown_fn != NULL) {
      shutdown_fn(shell);
    }
    arksh_platform_library_close(shell->plugins[i].handle);
    shell->plugins[i].handle = NULL;
  }
}

static void clear_shell_state_for_restore(ArkshShell *shell) {
  size_t i;

  if (shell == NULL) {
    return;
  }

  for (i = 0; i < shell->binding_count; ++i) {
    arksh_value_free(&shell->bindings[i].value);
  }
  shell->binding_count = 0;

  for (i = 0; i < shell->class_count; ++i) {
    free_class_definition_contents_snapshot(&shell->classes[i]);
  }
  shell->class_count = 0;

  for (i = 0; i < shell->instance_count; ++i) {
    arksh_value_free(&shell->instances[i].fields);
    memset(&shell->instances[i], 0, sizeof(shell->instances[i]));
  }
  shell->instance_count = 0;
}

static int restore_shell_state(
  ArkshShell *shell,
  const ArkshShellStateSnapshot *snapshot,
  char *out,
  size_t out_size
) {
  size_t i;

  if (shell == NULL || snapshot == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  close_subshell_side_effects(shell, snapshot);
  clear_shell_state_for_restore(shell);

  if (snapshot->cwd[0] != '\0' && arksh_platform_chdir(snapshot->cwd) != 0) {
    snprintf(out, out_size, "unable to restore subshell working directory");
    return 1;
  }

  shell->running = snapshot->running;
  shell->last_status = snapshot->last_status;
  shell->next_instance_id = snapshot->next_instance_id;
  shell->next_job_id = snapshot->next_job_id;
  shell->loading_plugin_index = snapshot->loading_plugin_index;
  copy_string(shell->cwd, sizeof(shell->cwd), snapshot->cwd);
  shell->prompt = snapshot->prompt;

  shell->var_count = snapshot->var_count;
  memcpy(shell->vars, snapshot->vars, snapshot->var_count * sizeof(snapshot->vars[0]));

  shell->binding_count = 0;
  for (i = 0; i < snapshot->binding_count; ++i) {
    copy_string(shell->bindings[i].name, sizeof(shell->bindings[i].name), snapshot->bindings[i].name);
    if (arksh_value_copy(&shell->bindings[i].value, &snapshot->bindings[i].value) != 0) {
      snprintf(out, out_size, "unable to restore subshell bindings");
      return 1;
    }
    shell->binding_count++;
  }

  shell->alias_count = snapshot->alias_count;
  memcpy(shell->aliases, snapshot->aliases, snapshot->alias_count * sizeof(snapshot->aliases[0]));

  shell->function_count = snapshot->function_count;
  memcpy(shell->functions, snapshot->functions, snapshot->function_count * sizeof(snapshot->functions[0]));

  shell->class_count = 0;
  for (i = 0; i < snapshot->class_count; ++i) {
    if (copy_class_definition_snapshot(&shell->classes[i], &snapshot->classes[i]) != 0) {
      snprintf(out, out_size, "unable to restore subshell classes");
      return 1;
    }
    shell->class_count++;
  }

  shell->instance_count = 0;
  for (i = 0; i < snapshot->instance_count; ++i) {
    if (copy_class_instance_snapshot(&shell->instances[i], &snapshot->instances[i]) != 0) {
      snprintf(out, out_size, "unable to restore subshell instances");
      return 1;
    }
    shell->instance_count++;
  }

  shell->extension_count = snapshot->extension_count;
  memcpy(shell->extensions, snapshot->extensions, snapshot->extension_count * sizeof(snapshot->extensions[0]));

  shell->value_resolver_count = snapshot->value_resolver_count;
  memcpy(shell->value_resolvers, snapshot->value_resolvers, snapshot->value_resolver_count * sizeof(snapshot->value_resolvers[0]));

  shell->pipeline_stage_count = snapshot->pipeline_stage_count;
  memcpy(shell->pipeline_stages, snapshot->pipeline_stages, snapshot->pipeline_stage_count * sizeof(snapshot->pipeline_stages[0]));

  shell->command_count = snapshot->command_count;
  memcpy(shell->commands, snapshot->commands, snapshot->command_count * sizeof(snapshot->commands[0]));

  shell->plugin_count = snapshot->plugin_count;
  memcpy(shell->plugins, snapshot->plugins, snapshot->plugin_count * sizeof(snapshot->plugins[0]));

  shell->job_count = snapshot->job_count;
  memcpy(shell->jobs, snapshot->jobs, snapshot->job_count * sizeof(snapshot->jobs[0]));
  return 0;
}

static int bind_function_parameters(
  ArkshShell *shell,
  const ArkshShellFunction *function_def,
  const ArkshSimpleCommandNode *command,
  char *out,
  size_t out_size
) {
  ArkshValue *args;
  int i;

  if (shell == NULL || function_def == NULL || command == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  /* E1-S7-T4: param_count == -1 means POSIX-style function (no named params,
     accepts any number of positional arguments). */
  if (function_def->param_count == -1) {
    int posix_argc = command->argc - 1;
    if (posix_argc > ARKSH_MAX_POSITIONAL_PARAMS) {
      snprintf(out, out_size, "function %s: too many arguments", function_def->name);
      return 1;
    }
    shell->positional_count = posix_argc;
    for (i = 0; i < posix_argc; ++i) {
      copy_string(shell->positional_params[i], sizeof(shell->positional_params[i]), command->argv[i + 1]);
    }
    return 0;
  }

  if (command->argc - 1 != function_def->param_count) {
    snprintf(
      out,
      out_size,
      "function %s expects %d arguments, got %d",
      function_def->name,
      function_def->param_count,
      command->argc - 1
    );
    return 1;
  }

  args = (ArkshValue *) allocate_temp_buffer(
    function_def->param_count > 0 ? (size_t) function_def->param_count : 1,
    sizeof(*args),
    "function arguments",
    out,
    out_size
  );
  if (args == NULL) {
    return 1;
  }

  /* Set positional parameters ($1, $2, …) from call arguments. */
  shell->positional_count = function_def->param_count;
  for (i = 0; i < function_def->param_count && i < ARKSH_MAX_POSITIONAL_PARAMS; ++i) {
    copy_string(shell->positional_params[i], sizeof(shell->positional_params[i]), command->argv[i + 1]);
  }

  for (i = 0; i < function_def->param_count; ++i) {
    char rendered[ARKSH_MAX_OUTPUT];

    arksh_value_set_string(&args[i], command->argv[i + 1]);
    if (arksh_shell_set_binding(shell, function_def->params[i], &args[i]) != 0) {
      snprintf(out, out_size, "unable to bind function parameter: %s", function_def->params[i]);
      goto cleanup;
    }
    if (render_value_for_shell_var(&args[i], rendered, sizeof(rendered)) != 0) {
      snprintf(out, out_size, "unable to render function parameter: %s", function_def->params[i]);
      goto cleanup;
    }
    if (arksh_shell_set_var(shell, function_def->params[i], rendered, 0) != 0) {
      snprintf(out, out_size, "unable to assign function parameter: %s", function_def->params[i]);
      goto cleanup;
    }
  }

  for (i = 0; i < function_def->param_count; ++i) {
    arksh_value_free(&args[i]);
  }
  free(args);
  return 0;

cleanup:
  for (i = 0; i < function_def->param_count; ++i) {
    arksh_value_free(&args[i]);
  }
  free(args);
  return 1;
}

static int evaluate_block_body(
  ArkshShell *shell,
  const char *body,
  ArkshValue *out_value,
  ArkshBlockBindingSnapshot *local_snapshots,
  int *out_local_count,
  char *out,
  size_t out_size
) {
  size_t segment_start = 0;
  char trimmed_body[ARKSH_MAX_BLOCK_SOURCE];
  int local_count = 0;

  if (shell == NULL || body == NULL || out_value == NULL || out_local_count == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  copy_string(trimmed_body, sizeof(trimmed_body), body);
  trim_in_place(trimmed_body);
  if (trimmed_body[0] == '\0') {
    snprintf(out, out_size, "block literal requires a body");
    return 1;
  }

  while (trimmed_body[segment_start] != '\0') {
    size_t separator_index = 0;
    char segment[ARKSH_MAX_BLOCK_SOURCE];
    char local_name[ARKSH_MAX_NAME];
    char local_expression[ARKSH_MAX_BLOCK_SOURCE];
    int local_status;

    if (find_next_top_level_semicolon(trimmed_body, segment_start, &separator_index) != 0) {
      separator_index = strlen(trimmed_body);
    }

    copy_trimmed_range(trimmed_body, segment_start, separator_index, segment, sizeof(segment));
    if (segment[0] == '\0') {
      if (trimmed_body[separator_index] == '\0') {
        break;
      }
      segment_start = separator_index + 1;
      continue;
    }

    local_status = parse_block_local_statement(
      segment,
      local_name,
      sizeof(local_name),
      local_expression,
      sizeof(local_expression),
      out,
      out_size
    );

    if (local_status == 0) {
      ArkshValue *local_value;

      if (local_count >= ARKSH_MAX_BLOCK_LOCALS) {
        snprintf(out, out_size, "too many local bindings inside block");
        return 1;
      }
      if (trimmed_body[separator_index] == '\0') {
        snprintf(out, out_size, "block local declarations require a final expression");
        return 1;
      }
      if (local_snapshots == NULL) {
        snprintf(out, out_size, "unable to allocate local block scope");
        return 1;
      }
      if (snapshot_binding(shell, local_name, &local_snapshots[local_count], out, out_size) != 0) {
        return 1;
      }
      local_count++;

      local_value = (ArkshValue *) allocate_temp_buffer(1, sizeof(*local_value), "block local value", out, out_size);
      if (local_value == NULL) {
        return 1;
      }

      if (evaluate_expression_text(shell, local_expression, local_value, out, out_size) != 0) {
        arksh_value_free(local_value);
        free(local_value);
        return 1;
      }
      if (arksh_shell_set_binding(shell, local_name, local_value) != 0) {
        arksh_value_free(local_value);
        free(local_value);
        snprintf(out, out_size, "unable to bind local value: %s", local_name);
        return 1;
      }

      arksh_value_free(local_value);
      free(local_value);
      segment_start = separator_index + 1;
      continue;
    }

    if (local_status == 2) {
      return 1;
    }

    *out_local_count = local_count;
    return evaluate_expression_text(shell, trimmed_body + segment_start, out_value, out, out_size);
  }

  snprintf(out, out_size, "block local declarations require a final expression");
  return 1;
}

static int evaluate_block(ArkshShell *shell, const ArkshBlock *block, const ArkshValue *args, int argc, ArkshValue *out_value, char *out, size_t out_size) {
  ArkshBlockBindingSnapshot *snapshots;
  ArkshBlockBindingSnapshot *local_snapshots;
  int local_count = 0;
  int status;

  if (shell == NULL || block == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  snapshots = (ArkshBlockBindingSnapshot *) allocate_temp_buffer(
    ARKSH_MAX_BLOCK_PARAMS,
    sizeof(*snapshots),
    "block binding snapshots",
    out,
    out_size
  );
  if (snapshots == NULL) {
    return 1;
  }

  if (bind_block_arguments(shell, block, args, argc, snapshots, out, out_size) != 0) {
    free(snapshots);
    return 1;
  }

  local_snapshots = (ArkshBlockBindingSnapshot *) allocate_temp_buffer(
    ARKSH_MAX_BLOCK_LOCALS,
    sizeof(*local_snapshots),
    "block local snapshots",
    out,
    out_size
  );
  if (local_snapshots == NULL) {
    restore_block_arguments(shell, snapshots, block->param_count);
    free(snapshots);
    return 1;
  }

  out[0] = '\0';
  status = evaluate_block_body(shell, block->body, out_value, local_snapshots, &local_count, out, out_size);

  restore_block_arguments(shell, local_snapshots, local_count);
  free(local_snapshots);
  restore_block_arguments(shell, snapshots, block->param_count);
  free(snapshots);
  return status;
}

int arksh_execute_block(
  ArkshShell *shell,
  const ArkshBlock *block,
  const ArkshValue *args,
  int argc,
  ArkshValue *out_value,
  char *out,
  size_t out_size
) {
  return evaluate_block(shell, block, args, argc, out_value, out, out_size);
}

static int evaluate_extension_args(
  ArkshShell *shell,
  const char raw_argv[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN],
  int argc,
  ArkshValue args[ARKSH_MAX_ARGS],
  char *out,
  size_t out_size
) {
  int i;

  if (shell == NULL || raw_argv == NULL || out == NULL || out_size == 0) {
    return 1;
  }
  if (argc > 0 && args == NULL) {
    return 1;
  }

  for (i = 0; i < argc; ++i) {
    if (evaluate_token_argument_value(shell, raw_argv[i], &args[i], out, out_size) != 0) {
      return 1;
    }
  }

  return 0;
}

static int evaluate_token_argument_text(
  ArkshShell *shell,
  const char *raw_text,
  char *arg_text,
  size_t arg_text_size,
  char *out,
  size_t out_size
) {
  ArkshValue *value;
  int status;

  if (shell == NULL || raw_text == NULL || arg_text == NULL || arg_text_size == 0 || out == NULL || out_size == 0) {
    return 1;
  }

  value = (ArkshValue *) allocate_temp_buffer(1, sizeof(*value), "object method argument", out, out_size);
  if (value == NULL) {
    return 1;
  }

  if (evaluate_token_argument_value(shell, raw_text, value, out, out_size) != 0) {
    free(value);
    return 1;
  }

  status = arksh_value_render(value, arg_text, arg_text_size);
  free(value);
  if (status != 0 && out[0] == '\0') {
    snprintf(out, out_size, "unable to render object method argument");
  }
  return status;
}

static int resolve_receiver_value(
  ArkshShell *shell,
  const char *raw_selector,
  ArkshValue *out_value,
  char *out,
  size_t out_size
) {
  char selector[ARKSH_MAX_PATH];
  ArkshObject object;
  char nested_error[ARKSH_MAX_OUTPUT];

  if (shell == NULL || raw_selector == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  nested_error[0] = '\0';
  if (arksh_evaluate_line_value(shell, raw_selector, out_value, nested_error, sizeof(nested_error)) == 0) {
    return 0;
  }

  if (expand_single_word(shell, raw_selector, ARKSH_EXPAND_MODE_OBJECT_SELECTOR, selector, sizeof(selector), out, out_size) != 0) {
    return 1;
  }

  {
    const ArkshValue *binding = arksh_shell_get_binding(shell, selector);

    if (binding != NULL) {
      return arksh_value_copy(out_value, binding);
    }
  }

  if (arksh_object_resolve(shell->cwd, selector, &object) != 0) {
    snprintf(out, out_size, "unable to resolve selector: %s", selector);
    return 1;
  }

  arksh_value_set_object(out_value, &object);
  return 0;
}

static int extension_target_matches(const ArkshObjectExtension *extension, const ArkshValue *receiver) {
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
      const ArkshValueItem *type_entry;
      if (receiver->kind != ARKSH_VALUE_MAP) return 0;
      type_entry = arksh_value_map_get_item(receiver, "__type__");
      if (type_entry == NULL || type_entry->kind != ARKSH_VALUE_STRING) return 0;
      return strcmp(type_entry->text, extension->target_name) == 0;
    }
    default:
      return 0;
  }
}

static const ArkshObjectExtension *find_extension(
  const ArkshShell *shell,
  const ArkshValue *receiver,
  ArkshMemberKind member_kind,
  const char *name
) {
  size_t i;

  if (shell == NULL || receiver == NULL || name == NULL) {
    return NULL;
  }

  for (i = shell->extension_count; i > 0; --i) {
    const ArkshObjectExtension *extension = &shell->extensions[i - 1];

    if (extension->member_kind != member_kind || strcmp(extension->name, name) != 0) {
      continue;
    }
    if (extension->is_plugin_extension && !plugin_index_is_active(shell, extension->owner_plugin_index)) {
      continue;
    }
    if (extension_target_matches(extension, receiver)) {
      return extension;
    }
  }

  return NULL;
}

static int invoke_extension_property_value(
  ArkshShell *shell,
  const ArkshValue *receiver,
  const char *name,
  ArkshValue *out_value,
  char *out,
  size_t out_size
) {
  const ArkshObjectExtension *extension;

  if (shell == NULL || receiver == NULL || name == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  extension = find_extension(shell, receiver, ARKSH_MEMBER_PROPERTY, name);
  if (extension == NULL) {
    snprintf(out, out_size, "unknown property: %s", name);
    return 1;
  }

  if (extension->impl_kind == ARKSH_EXTENSION_IMPL_NATIVE) {
    return extension->property_fn == NULL ? 1 : extension->property_fn(shell, receiver, out_value, out, out_size);
  }

  return evaluate_block(shell, &extension->block, receiver, 1, out_value, out, out_size);
}

static int invoke_extension_property_text(
  ArkshShell *shell,
  const ArkshValue *receiver,
  const char *name,
  char *out,
  size_t out_size
) {
  ArkshValue *value;
  int status;

  value = (ArkshValue *) allocate_temp_buffer(1, sizeof(*value), "extension property value", out, out_size);
  if (value == NULL) {
    return 1;
  }

  if (invoke_extension_property_value(shell, receiver, name, value, out, out_size) != 0) {
    free(value);
    return 1;
  }

  status = arksh_value_render(value, out, out_size);
  free(value);
  return status;
}

static int invoke_extension_method_value(
  ArkshShell *shell,
  const ArkshValue *receiver,
  const char *name,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *out,
  size_t out_size
) {
  const ArkshObjectExtension *extension;

  if (shell == NULL || receiver == NULL || name == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  extension = find_extension(shell, receiver, ARKSH_MEMBER_METHOD, name);
  if (extension == NULL) {
    snprintf(out, out_size, "unknown method: %s", name);
    return 1;
  }

  if (extension->impl_kind == ARKSH_EXTENSION_IMPL_NATIVE) {
    return extension->method_fn == NULL ? 1 : extension->method_fn(shell, receiver, argc, args, out_value, out, out_size);
  }

  {
    ArkshValue *block_args;
    int i;

    if (argc + 1 > ARKSH_MAX_ARGS) {
      snprintf(out, out_size, "too many method arguments");
      return 1;
    }

    block_args = (ArkshValue *) allocate_temp_buffer((size_t) argc + 1, sizeof(*block_args), "extension method block arguments", out, out_size);
    if (block_args == NULL) {
      return 1;
    }

    block_args[0] = *receiver;
    for (i = 0; i < argc; ++i) {
      block_args[i + 1] = args[i];
    }
    i = evaluate_block(shell, &extension->block, block_args, argc + 1, out_value, out, out_size);
    free(block_args);
    return i;
  }
}

static int invoke_extension_method_text(
  ArkshShell *shell,
  const ArkshValue *receiver,
  const char *name,
  int argc,
  const ArkshValue *args,
  char *out,
  size_t out_size
) {
  ArkshValue *value;
  int status;

  value = (ArkshValue *) allocate_temp_buffer(1, sizeof(*value), "extension method value", out, out_size);
  if (value == NULL) {
    return 1;
  }

  if (invoke_extension_method_value(shell, receiver, name, argc, args, value, out, out_size) != 0) {
    free(value);
    return 1;
  }

  status = arksh_value_render(value, out, out_size);
  free(value);
  return status;
}

static int error_has_prefix(const char *text, const char *prefix) {
  return text != NULL && prefix != NULL && strncmp(text, prefix, strlen(prefix)) == 0;
}

static int get_property_value_with_shell(
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

  if (receiver->kind == ARKSH_VALUE_CLASS || receiver->kind == ARKSH_VALUE_INSTANCE) {
    if (arksh_shell_get_class_property_value(shell, receiver, property, out_value, out, out_size) == 0) {
      return 0;
    }
    if (!error_has_prefix(out, "unknown property:")) {
      return 1;
    }
    out[0] = '\0';
  }

  return arksh_value_get_property_value(receiver, property, out_value, out, out_size);
}

static int get_property_text_with_shell(
  ArkshShell *shell,
  const ArkshValue *receiver,
  const char *property,
  char *out,
  size_t out_size
) {
  ArkshValue *value;
  int status;

  if (shell == NULL || receiver == NULL || property == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  value = (ArkshValue *) allocate_temp_buffer(1, sizeof(*value), "property text value", out, out_size);
  if (value == NULL) {
    return 1;
  }

  if (get_property_value_with_shell(shell, receiver, property, value, out, out_size) != 0) {
    free(value);
    return 1;
  }

  status = arksh_value_render(value, out, out_size);
  arksh_value_free(value);
  free(value);
  return status;
}

static int get_item_property_text(
  ArkshShell *shell,
  const ArkshValueItem *item,
  const char *property,
  char *out,
  size_t out_size
) {
  ArkshValue *receiver;
  int status;

  if (item == NULL || property == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (arksh_value_item_get_property(item, property, out, out_size) == 0) {
    return 0;
  }

  receiver = (ArkshValue *) allocate_temp_buffer(1, sizeof(*receiver), "item property receiver", out, out_size);
  if (receiver == NULL) {
    return 1;
  }

  if (shell == NULL || arksh_value_set_from_item(receiver, item) != 0) {
    free(receiver);
    snprintf(out, out_size, "unknown property: %s", property);
    return 1;
  }

  status = get_property_text_with_shell(shell, receiver, property, out, out_size);
  if (status != 0 && error_has_prefix(out, "unknown property:")) {
    status = invoke_extension_property_text(shell, receiver, property, out, out_size);
  }
  free(receiver);
  return status;
}

static int compare_items_by_property(
  ArkshShell *shell,
  const ArkshValueItem *left,
  const ArkshValueItem *right,
  const char *property,
  int *out_cmp,
  char *out,
  size_t out_size
) {
  char left_value[256];
  char right_value[256];

  if (left == NULL || right == NULL || property == NULL || out_cmp == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (get_item_property_text(shell, left, property, left_value, sizeof(left_value)) != 0 ||
      get_item_property_text(shell, right, property, right_value, sizeof(right_value)) != 0) {
    snprintf(out, out_size, "unknown property for sort: %s", property);
    return 1;
  }

  if (is_unsigned_text(left_value) && is_unsigned_text(right_value)) {
    unsigned long long left_number = strtoull(left_value, NULL, 10);
    unsigned long long right_number = strtoull(right_value, NULL, 10);

    if (left_number < right_number) {
      *out_cmp = -1;
    } else if (left_number > right_number) {
      *out_cmp = 1;
    } else {
      *out_cmp = 0;
    }
    return 0;
  }

  *out_cmp = strcmp(left_value, right_value);
  return 0;
}

static int render_value_in_place(ArkshValue *value, char *out, size_t out_size) {
  char rendered[ARKSH_MAX_OUTPUT];

  if (value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (arksh_value_render(value, rendered, sizeof(rendered)) != 0) {
    snprintf(out, out_size, "unable to render pipeline value");
    return 1;
  }

  arksh_value_free(value);
  arksh_value_set_string(value, rendered);
  return 0;
}

static int call_bound_value(
  ArkshShell *shell,
  const ArkshValue *bound_value,
  const ArkshObjectExpressionNode *expression,
  ArkshValue *out_value,
  char *out,
  size_t out_size
) {
  ArkshValue *args = NULL;
  int i;
  int status;

  if (shell == NULL || bound_value == NULL || expression == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (strcmp(expression->member, "call") == 0 && bound_value->kind == ARKSH_VALUE_BLOCK) {
    args = (ArkshValue *) allocate_temp_buffer((size_t) expression->argc, sizeof(*args), "bound value arguments", out, out_size);
    if (expression->argc > 0 && args == NULL) {
      return 1;
    }
    for (i = 0; i < expression->argc; ++i) {
      if (evaluate_token_argument_value(shell, expression->raw_argv[i], &args[i], out, out_size) != 0) {
        free(args);
        return 1;
      }
    }

    status = evaluate_block(shell, &bound_value->block, args, expression->argc, out_value, out, out_size);
    free(args);
    return status;
  }

  if (bound_value->kind == ARKSH_VALUE_OBJECT && is_builtin_object_method_name(expression->member)) {
    char expanded_args[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
    char *argv[ARKSH_MAX_ARGS];
    int expanded_argc = 0;

    for (i = 0; i < expression->argc; ++i) {
      if (evaluate_token_argument_text(shell, expression->raw_argv[i], expanded_args[i], sizeof(expanded_args[i]), out, out_size) != 0) {
        return 1;
      }
      expanded_argc++;
    }

    build_command_argv(expanded_args, expanded_argc, argv);
    return arksh_object_call_method_value(&bound_value->object, expression->member, expanded_argc, argv, out_value, out, out_size);
  }

  if (bound_value->kind == ARKSH_VALUE_CLASS || bound_value->kind == ARKSH_VALUE_INSTANCE) {
    args = (ArkshValue *) allocate_temp_buffer((size_t) expression->argc, sizeof(*args), "class method arguments", out, out_size);
    if (expression->argc > 0 && args == NULL) {
      return 1;
    }

    if (evaluate_extension_args(shell, expression->raw_argv, expression->argc, args, out, out_size) != 0) {
      free(args);
      return 1;
    }

    status = arksh_shell_call_class_method(shell, bound_value, expression->member, expression->argc, args, out_value, out, out_size);
    free(args);
    if (status == 0 || !error_has_prefix(out, "unknown method:")) {
      return status;
    }
    out[0] = '\0';
  }

  args = (ArkshValue *) allocate_temp_buffer((size_t) expression->argc, sizeof(*args), "bound value extension arguments", out, out_size);
  if (expression->argc > 0 && args == NULL) {
    return 1;
  }

  if (evaluate_extension_args(shell, expression->raw_argv, expression->argc, args, out, out_size) != 0) {
    free(args);
    return 1;
  }

  status = invoke_extension_method_value(shell, bound_value, expression->member, expression->argc, args, out_value, out, out_size);
  free(args);
  return status;
}

static int evaluate_object_expression_text(ArkshShell *shell, const ArkshObjectExpressionNode *expression, char *out, size_t out_size) {
  ArkshValue *receiver;
  ArkshValue *result;
  int status;

  if (shell == NULL || expression == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  receiver = (ArkshValue *) allocate_temp_buffer(1, sizeof(*receiver), "object expression receiver", out, out_size);
  if (receiver == NULL) {
    return 1;
  }
  result = (ArkshValue *) allocate_temp_buffer(1, sizeof(*result), "object expression result", out, out_size);
  if (result == NULL) {
    free(receiver);
    return 1;
  }

  if (resolve_receiver_value(shell, expression->raw_selector, receiver, out, out_size) != 0) {
    free(result);
    free(receiver);
    return 1;
  }

  if (expression->member_kind == ARKSH_MEMBER_PROPERTY) {
    status = get_property_value_with_shell(shell, receiver, expression->member, result, out, out_size);
    if (status != 0 && error_has_prefix(out, "unknown property:")) {
      status = invoke_extension_property_value(shell, receiver, expression->member, result, out, out_size);
    }
    if (status != 0) {
      arksh_value_free(receiver);
      free(result);
      free(receiver);
      return 1;
    }
    status = arksh_value_render(result, out, out_size);
    arksh_value_free(result);
    arksh_value_free(receiver);
    free(result);
    free(receiver);
    return status;
  }

  status = call_bound_value(shell, receiver, expression, result, out, out_size);
  arksh_value_free(receiver);
  free(receiver);
  if (status != 0) {
    free(result);
    return 1;
  }

  status = arksh_value_render(result, out, out_size);
  arksh_value_free(result);
  free(result);
  return status;
}

static int evaluate_object_expression_value(ArkshShell *shell, const ArkshObjectExpressionNode *expression, ArkshValue *value, char *out, size_t out_size) {
  ArkshValue *receiver;
  int status;

  if (shell == NULL || expression == NULL || value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  receiver = (ArkshValue *) allocate_temp_buffer(1, sizeof(*receiver), "object expression receiver value", out, out_size);
  if (receiver == NULL) {
    return 1;
  }

  if (resolve_receiver_value(shell, expression->raw_selector, receiver, out, out_size) != 0) {
    free(receiver);
    return 1;
  }

  arksh_value_free(value);
  if (expression->member_kind == ARKSH_MEMBER_PROPERTY) {
    status = get_property_value_with_shell(shell, receiver, expression->member, value, out, out_size);
    if (status != 0 && error_has_prefix(out, "unknown property:")) {
      status = invoke_extension_property_value(shell, receiver, expression->member, value, out, out_size);
    }
    arksh_value_free(receiver);
    free(receiver);
    return status;
  }

  status = call_bound_value(shell, receiver, expression, value, out, out_size);
  arksh_value_free(receiver);
  free(receiver);
  return status;
}

static int split_text_lines_into_value(const char *text, ArkshValue *out_value) {
  const char *cursor;

  if (text == NULL || out_value == NULL) {
    return 1;
  }

  arksh_value_free(out_value);
  out_value->kind = ARKSH_VALUE_LIST;
  cursor = text;

  while (*cursor != '\0') {
    const char *line_end = strchr(cursor, '\n');
    ArkshValueItem *item = (ArkshValueItem *) arksh_scratch_alloc_active_zero(1, sizeof(*item));
    size_t len;
    char line[ARKSH_MAX_VALUE_TEXT];

    if (item == NULL) {
      return 1;
    }

    if (line_end == NULL) {
      line_end = cursor + strlen(cursor);
    }

    len = (size_t) (line_end - cursor);
    if (len >= sizeof(line)) {
      len = sizeof(line) - 1;
    }

    memcpy(line, cursor, len);
    line[len] = '\0';
    arksh_value_item_init(item);
    item->kind = ARKSH_VALUE_STRING;
    copy_string(item->text, sizeof(item->text), line);
    if (arksh_value_list_append_item(out_value, item) != 0) {
      return 1;
    }
    if (*line_end == '\0') {
      break;
    }

    cursor = line_end + 1;
    if (*cursor == '\0') {
      break;
    }
  }

  return 0;
}

static int append_string_item_to_value(ArkshValue *out_value, const char *text) {
  ArkshValueItem *item;

  if (out_value == NULL || text == NULL) {
    return 1;
  }

  item = (ArkshValueItem *) arksh_scratch_alloc_active_zero(1, sizeof(*item));
  if (item == NULL) {
    return 1;
  }
  arksh_value_item_init(item);
  item->kind = ARKSH_VALUE_STRING;
  copy_string(item->text, sizeof(item->text), text);
  if (arksh_value_list_append_item(out_value, item) != 0) {
    return 1;
  }
  return 0;
}

static int split_text_whitespace_into_value(const char *text, ArkshValue *out_value) {
  const char *cursor;

  if (text == NULL || out_value == NULL) {
    return 1;
  }

  arksh_value_free(out_value);
  out_value->kind = ARKSH_VALUE_LIST;
  cursor = text;

  while (*cursor != '\0') {
    const char *start;
    const char *end;
    char token[ARKSH_MAX_VALUE_TEXT];
    size_t len;

    while (*cursor != '\0' && isspace((unsigned char) *cursor)) {
      cursor++;
    }
    if (*cursor == '\0') {
      break;
    }

    start = cursor;
    while (*cursor != '\0' && !isspace((unsigned char) *cursor)) {
      cursor++;
    }
    end = cursor;

    len = (size_t) (end - start);
    if (len >= sizeof(token)) {
      len = sizeof(token) - 1;
    }
    memcpy(token, start, len);
    token[len] = '\0';
    if (append_string_item_to_value(out_value, token) != 0) {
      return 1;
    }
  }

  return 0;
}

static int split_text_delimiter_into_value(const char *text, const char *separator, ArkshValue *out_value) {
  const char *cursor;
  size_t separator_len;

  if (text == NULL || separator == NULL || out_value == NULL || separator[0] == '\0') {
    return 1;
  }

  arksh_value_free(out_value);
  out_value->kind = ARKSH_VALUE_LIST;
  cursor = text;
  separator_len = strlen(separator);

  while (1) {
    const char *match = strstr(cursor, separator);
    char token[ARKSH_MAX_VALUE_TEXT];
    size_t len;

    if (match == NULL) {
      match = cursor + strlen(cursor);
    }

    len = (size_t) (match - cursor);
    if (len >= sizeof(token)) {
      len = sizeof(token) - 1;
    }
    memcpy(token, cursor, len);
    token[len] = '\0';
    if (append_string_item_to_value(out_value, token) != 0) {
      return 1;
    }

    if (*match == '\0') {
      break;
    }

    cursor = match + separator_len;
    if (*cursor == '\0') {
      if (append_string_item_to_value(out_value, "") != 0) {
        return 1;
      }
      break;
    }
  }

  return 0;
}

static int evaluate_stage_argument_to_text(ArkshShell *shell, const char *text, char *out_text, size_t out_text_size, char *out, size_t out_size) {
  ArkshValue *value;
  int status;

  if (shell == NULL || text == NULL || out_text == NULL || out_text_size == 0 || out == NULL || out_size == 0) {
    return 1;
  }

  value = (ArkshValue *) allocate_temp_buffer(1, sizeof(*value), "stage argument value", out, out_size);
  if (value == NULL) {
    return 1;
  }

  if (arksh_evaluate_line_value(shell, text, value, out, out_size) != 0) {
    free(value);
    return 1;
  }

  status = arksh_value_render(value, out_text, out_text_size);
  free(value);
  if (status != 0 && out[0] == '\0') {
    snprintf(out, out_size, "unable to render stage argument");
  }
  return status;
}

static int execute_capture_source(ArkshShell *shell, const char *raw_command, int split_lines, ArkshValue *value, char *out, size_t out_size) {
  char command_text[ARKSH_MAX_LINE];
  char capture_output[ARKSH_MAX_OUTPUT];

  if (shell == NULL || raw_command == NULL || value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (expand_single_word(shell, raw_command, ARKSH_EXPAND_MODE_COMMAND, command_text, sizeof(command_text), out, out_size) != 0) {
    return 1;
  }

  capture_output[0] = '\0';
  shell->force_capture = 1;
  if (arksh_shell_execute_line(shell, command_text, capture_output, sizeof(capture_output)) != 0) {
    shell->force_capture = 0;
    if (capture_output[0] != '\0') {
      copy_string(out, out_size, capture_output);
    } else {
      snprintf(out, out_size, "capture() command failed: %s", command_text);
    }
    return 1;
  }
  shell->force_capture = 0;

  if (split_lines) {
    if (split_text_lines_into_value(capture_output, value) != 0) {
      snprintf(out, out_size, "unable to split captured output into lines");
      return 1;
    }
    return 0;
  }

  arksh_value_free(value);
  arksh_value_set_string(value, capture_output);
  return 0;
}

static int invoke_value_resolver(
  ArkshShell *shell,
  const ArkshValueSourceNode *source,
  ArkshValue *value,
  char *out,
  size_t out_size
) {
  const ArkshValueResolverDef *resolver;
  const ArkshClassDef *class_def;
  ArkshValue *args = NULL;
  int i;
  int status;

  if (shell == NULL || source == NULL || value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  resolver = arksh_shell_find_value_resolver(shell, source->text);
  class_def = arksh_shell_find_class(shell, source->text);
  if ((resolver == NULL || resolver->fn == NULL) && class_def == NULL) {
    snprintf(out, out_size, "unknown value resolver: %s", source->text);
    return 1;
  }

  args = (ArkshValue *) allocate_temp_buffer((size_t) source->argc, sizeof(*args), "value resolver arguments", out, out_size);
  if (source->argc > 0 && args == NULL) {
    return 1;
  }

  for (i = 0; i < source->argc; ++i) {
    if (evaluate_token_argument_value(shell, source->raw_argv[i], &args[i], out, out_size) != 0) {
      int rollback;

      for (rollback = 0; rollback < i; ++rollback) {
        arksh_value_free(&args[rollback]);
      }
      free(args);
      return 1;
    }
  }

  if (resolver != NULL && resolver->fn != NULL) {
    status = resolver->fn(shell, source->argc, args, value, out, out_size);
  } else {
    status = arksh_shell_instantiate_class(shell, source->text, source->argc, args, value, out, out_size);
  }
  for (i = 0; i < source->argc; ++i) {
    arksh_value_free(&args[i]);
  }
  free(args);
  return status;
}

static int evaluate_value_source(ArkshShell *shell, const ArkshValueSourceNode *source, ArkshValue *value, char *out, size_t out_size) {
  int i;

  if (shell == NULL || source == NULL || value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  switch (source->kind) {
    case ARKSH_VALUE_SOURCE_OBJECT_EXPRESSION:
      return evaluate_object_expression_value(shell, &source->object_expression, value, out, out_size);
    case ARKSH_VALUE_SOURCE_BINDING: {
      const ArkshValue *binding = arksh_shell_get_binding(shell, source->binding);

      if (binding == NULL) {
        if (arksh_shell_find_class(shell, source->binding) != NULL) {
          arksh_value_free(value);
          arksh_value_set_class(value, source->binding);
          return 0;
        }
        snprintf(out, out_size, "value binding not found: %s", source->binding);
        return 1;
      }
      return arksh_value_copy(value, binding);
    }
    case ARKSH_VALUE_SOURCE_STRING_LITERAL: {
      char expanded[ARKSH_MAX_OUTPUT];

      if (expand_single_word(shell, source->raw_text, ARKSH_EXPAND_MODE_OBJECT_ARGUMENT, expanded, sizeof(expanded), out, out_size) != 0) {
        return 1;
      }
      arksh_value_free(value);
      arksh_value_set_string(value, expanded);
      return 0;
    }
    case ARKSH_VALUE_SOURCE_NUMBER_LITERAL: {
      char expanded[ARKSH_MAX_TOKEN];
      double number;

      if (expand_single_word(shell, source->raw_text, ARKSH_EXPAND_MODE_OBJECT_ARGUMENT, expanded, sizeof(expanded), out, out_size) != 0) {
        return 1;
      }
      if (parse_number_text(expanded, &number) != 0) {
        snprintf(out, out_size, "invalid numeric source: %s", expanded);
        return 1;
      }
      arksh_value_free(value);
      arksh_value_set_number(value, number);
      return 0;
    }
    case ARKSH_VALUE_SOURCE_BOOLEAN_LITERAL: {
      char expanded[ARKSH_MAX_TOKEN];

      if (expand_single_word(shell, source->raw_text, ARKSH_EXPAND_MODE_OBJECT_ARGUMENT, expanded, sizeof(expanded), out, out_size) != 0) {
        return 1;
      }
      if (strcmp(expanded, "true") != 0 && strcmp(expanded, "false") != 0) {
        snprintf(out, out_size, "bool() expects true or false");
        return 1;
      }
      arksh_value_free(value);
      arksh_value_set_boolean(value, strcmp(expanded, "true") == 0);
      return 0;
    }
    case ARKSH_VALUE_SOURCE_LIST_LITERAL:
      arksh_value_free(value);
      value->kind = ARKSH_VALUE_LIST;
      for (i = 0; i < source->argc; ++i) {
        ArkshValue *item_value = (ArkshValue *) allocate_temp_buffer(1, sizeof(*item_value), "list literal item", out, out_size);

        if (item_value == NULL) {
          return 1;
        }
        if (evaluate_token_argument_value(shell, source->raw_argv[i], item_value, out, out_size) != 0) {
          free(item_value);
          return 1;
        }
        if (arksh_value_list_append_value(value, item_value) != 0) {
          arksh_value_free(item_value);
          free(item_value);
          snprintf(out, out_size, "unable to append list item");
          return 1;
        }
        arksh_value_free(item_value);
        free(item_value);
      }
      return 0;
    case ARKSH_VALUE_SOURCE_BLOCK_LITERAL:
      arksh_value_set_block(value, &source->block);
      return 0;
    case ARKSH_VALUE_SOURCE_CAPTURE_TEXT:
      return execute_capture_source(shell, source->raw_text, 0, value, out, out_size);
    case ARKSH_VALUE_SOURCE_CAPTURE_LINES:
      return execute_capture_source(shell, source->raw_text, 1, value, out, out_size);
    case ARKSH_VALUE_SOURCE_CAPTURE_SHELL: {
      /* E3-S3 bridge: execute source->raw_text as a shell line verbatim and
         capture stdout as a text value.  Unlike CAPTURE_TEXT, no
         quote-stripping is applied — raw_text is already the plain command.
         force_capture ensures stdout is collected even in interactive REPL. */
      char capture_output[ARKSH_MAX_OUTPUT];
      int bridge_status;

      capture_output[0] = '\0';
      shell->force_capture = 1;
      bridge_status = arksh_shell_execute_line(shell, source->raw_text, capture_output, sizeof(capture_output));
      shell->force_capture = 0;
      if (bridge_status != 0) {
        if (capture_output[0] != '\0') {
          copy_string(out, out_size, capture_output);
        } else {
          snprintf(out, out_size, "shell command failed in pipeline source: %s", source->raw_text);
        }
        return 1;
      }
      arksh_value_free(value);
      arksh_value_set_string(value, capture_output);
      return 0;
    }
    case ARKSH_VALUE_SOURCE_TERNARY:
      return evaluate_expression_text(shell, source->raw_text, value, out, out_size);
    case ARKSH_VALUE_SOURCE_RESOLVER_CALL:
      return invoke_value_resolver(shell, source, value, out, out_size);
    case ARKSH_VALUE_SOURCE_BINARY_OP:
      /* Delegate to the iterative evaluator which re-scans the full
         expression text and evaluates all operands without recursion,
         preventing stack overflow on long arithmetic chains. */
      return evaluate_binary_expr_iterative(shell, source->text, value, out, out_size);
    default:
      snprintf(out, out_size, "unsupported value source");
      return 1;
  }
}

static int resolve_stage_block_argument(ArkshShell *shell, const char *text, ArkshBlock *out_block, char *out, size_t out_size) {
  ArkshValue *value;
  int status;

  if (shell == NULL || text == NULL || out_block == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  value = (ArkshValue *) allocate_temp_buffer(1, sizeof(*value), "stage block value", out, out_size);
  if (value == NULL) {
    return 1;
  }

  if (arksh_evaluate_line_value(shell, text, value, out, out_size) != 0) {
    free(value);
    return 1;
  }

  if (value->kind != ARKSH_VALUE_BLOCK) {
    free(value);
    snprintf(out, out_size, "stage expects a block value");
    return 1;
  }

  *out_block = value->block;
  status = 0;
  free(value);
  return status;
}

static void discard_list_tail(ArkshValue *value, size_t keep_count) {
  size_t i;

  if (value == NULL || value->kind != ARKSH_VALUE_LIST || keep_count >= value->list.count) {
    return;
  }

  for (i = keep_count; i < value->list.count; ++i) {
    arksh_value_item_free(&value->list.items[i]);
  }
  value->list.count = keep_count;
}

static void move_list_item(ArkshValue *value, size_t dest_index, size_t src_index) {
  if (value == NULL || value->kind != ARKSH_VALUE_LIST || dest_index == src_index) {
    return;
  }

  value->list.items[dest_index] = value->list.items[src_index];
  arksh_value_item_init(&value->list.items[src_index]);
}

static int apply_where_stage(ArkshShell *shell, ArkshValue *value, const ArkshPipelineStageNode *stage, char *out, size_t out_size) {
  char property[ARKSH_MAX_NAME];
  char expected[ARKSH_MAX_TOKEN];
  size_t i;

  if (shell == NULL || value == NULL || stage == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (value->kind != ARKSH_VALUE_LIST) {
    snprintf(out, out_size, "where() expects a list");
    return 1;
  }

  if (parse_where_condition(stage->raw_args, property, sizeof(property), expected, sizeof(expected)) != 0) {
    ArkshBlock block;
    ArkshValue *temp_values;
    size_t write_index = 0;

    if (resolve_stage_block_argument(shell, stage->raw_args, &block, out, out_size) != 0) {
      snprintf(out, out_size, "where() expects syntax like where(type == \"file\") or where(block)");
      return 1;
    }

    temp_values = (ArkshValue *) allocate_temp_buffer(2, sizeof(*temp_values), "where() block values", out, out_size);
    if (temp_values == NULL) {
      return 1;
    }

    for (i = 0; i < value->list.count; ++i) {
      if (arksh_value_set_from_item(&temp_values[0], &value->list.items[i]) != 0) {
        free(temp_values);
        snprintf(out, out_size, "unable to prepare where() block argument");
        return 1;
      }
      if (evaluate_block(shell, &block, &temp_values[0], 1, &temp_values[1], out, out_size) != 0) {
        free(temp_values);
        return 1;
      }
      if (value_is_truthy(&temp_values[1])) {
        move_list_item(value, write_index, i);
        write_index++;
      }
    }

    discard_list_tail(value, write_index);
    free(temp_values);
    return 0;
  }

  {
    size_t write_index = 0;

  for (i = 0; i < value->list.count; ++i) {
    char actual[256];

    if (get_item_property_text(shell, &value->list.items[i], property, actual, sizeof(actual)) != 0) {
      snprintf(out, out_size, "unknown property for where(): %s", property);
      return 1;
    }

    if (strcmp(actual, expected) == 0) {
      move_list_item(value, write_index, i);
      write_index++;
    }
    }

    discard_list_tail(value, write_index);
  }
  return 0;
}

static int apply_sort_stage(ArkshShell *shell, ArkshValue *value, const ArkshPipelineStageNode *stage, char *out, size_t out_size) {
  char property[ARKSH_MAX_NAME];
  char direction[16] = "asc";
  int parsed;
  int descending = 0;
  size_t i;
  size_t j;

  if (value->kind != ARKSH_VALUE_LIST) {
    snprintf(out, out_size, "sort() expects a list");
    return 1;
  }

  parsed = sscanf(stage->raw_args, "%127s %15s", property, direction);
  if (parsed < 1) {
    snprintf(out, out_size, "sort() expects syntax like sort(size desc)");
    return 1;
  }

  if (parsed >= 2) {
    if (strcmp(direction, "desc") == 0) {
      descending = 1;
    } else if (strcmp(direction, "asc") != 0) {
      snprintf(out, out_size, "sort() direction must be asc or desc");
      return 1;
    }
  }

  for (i = 0; i < value->list.count; ++i) {
    for (j = i + 1; j < value->list.count; ++j) {
      int cmp;

      if (compare_items_by_property(shell, &value->list.items[i], &value->list.items[j], property, &cmp, out, out_size) != 0) {
        return 1;
      }

      if ((descending && cmp < 0) || (!descending && cmp > 0)) {
        ArkshValueItem temp = value->list.items[i];
        value->list.items[i] = value->list.items[j];
        value->list.items[j] = temp;
      }
    }
  }

  return 0;
}

static int apply_take_stage(ArkshValue *value, const ArkshPipelineStageNode *stage, char *out, size_t out_size) {
  size_t limit;

  if (value->kind != ARKSH_VALUE_LIST) {
    snprintf(out, out_size, "take() expects a list");
    return 1;
  }

  if (parse_unsigned_number(stage->raw_args, &limit) != 0) {
    snprintf(out, out_size, "take() expects a numeric limit");
    return 1;
  }

  if (limit < value->list.count) {
    discard_list_tail(value, limit);
  }
  return 0;
}

static int apply_first_stage(ArkshValue *value, char *out, size_t out_size) {
  if (value->kind != ARKSH_VALUE_LIST) {
    snprintf(out, out_size, "first() expects a list");
    return 1;
  }

  if (value->list.count == 0) {
    snprintf(out, out_size, "first() cannot be used on an empty list");
    return 1;
  }

  {
    ArkshValueItem item;
    int status;

    arksh_value_item_init(&item);
    if (arksh_value_item_copy(&item, &value->list.items[0]) != 0) {
      snprintf(out, out_size, "unable to extract first() item");
      return 1;
    }
    arksh_value_free(value);
    status = arksh_value_set_from_item(value, &item);
    arksh_value_item_free(&item);
    return status;
  }
}

static int apply_count_stage(ArkshValue *value, char *out, size_t out_size) {
  (void) out;
  (void) out_size;

  if (value->kind == ARKSH_VALUE_LIST) {
    size_t count = value->list.count;
    arksh_value_free(value);
    arksh_value_set_number(value, (double) count);
    return 0;
  }

  if (value->kind == ARKSH_VALUE_MAP || value->kind == ARKSH_VALUE_DICT) {
    size_t count = value->map.count;
    arksh_value_free(value);
    arksh_value_set_number(value, (double) count);
    return 0;
  }

  if (value->kind == ARKSH_VALUE_STRING || value->kind == ARKSH_VALUE_NUMBER ||
      value->kind == ARKSH_VALUE_BOOLEAN || value->kind == ARKSH_VALUE_OBJECT) {
    arksh_value_free(value);
    arksh_value_set_number(value, 1.0);
    return 0;
  }

  snprintf(out, out_size, "count() expects a value, list or map");
  return 1;
}

static int apply_lines_stage(ArkshValue *value, char *out, size_t out_size) {
  char original[ARKSH_MAX_OUTPUT];

  if (value->kind != ARKSH_VALUE_STRING) {
    snprintf(out, out_size, "lines() expects a string");
    return 1;
  }

  copy_string(original, sizeof(original), value->text);
  if (split_text_lines_into_value(original, value) != 0) {
    snprintf(out, out_size, "unable to split string into lines");
    return 1;
  }

  return 0;
}

static int apply_grep_stage(ArkshShell *shell, ArkshValue *value, const ArkshPipelineStageNode *stage, char *out, size_t out_size) {
  char pattern[ARKSH_MAX_TOKEN];
  char item_text[ARKSH_MAX_OUTPUT];
  size_t write_index;
  size_t i;

  if (shell == NULL || value == NULL || stage == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (stage->raw_args[0] == '\0') {
    snprintf(out, out_size, "grep() requires a pattern argument");
    return 1;
  }

  if (evaluate_stage_argument_to_text(shell, stage->raw_args, pattern, sizeof(pattern), out, out_size) != 0) {
    return 1;
  }

  /* If given a string, split into lines first so grep works line-by-line. */
  if (value->kind == ARKSH_VALUE_STRING) {
    char original[ARKSH_MAX_OUTPUT];
    copy_string(original, sizeof(original), value->text);
    if (split_text_lines_into_value(original, value) != 0) {
      snprintf(out, out_size, "grep() unable to split string into lines");
      return 1;
    }
  }

  if (value->kind != ARKSH_VALUE_LIST) {
    snprintf(out, out_size, "grep() expects a string or list");
    return 1;
  }

  write_index = 0;
  for (i = 0; i < value->list.count; ++i) {
    item_text[0] = '\0';
    if (arksh_value_item_render(&value->list.items[i], item_text, sizeof(item_text)) != 0) {
      continue;
    }
    if (strstr(item_text, pattern) != NULL) {
      move_list_item(value, write_index, i);
      write_index++;
    }
  }
  discard_list_tail(value, write_index);
  return 0;
}

static int apply_trim_stage(ArkshValue *value, char *out, size_t out_size) {
  size_t i;

  if (value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (value->kind == ARKSH_VALUE_STRING) {
    trim_in_place(value->text);
    return 0;
  }

  if (value->kind != ARKSH_VALUE_LIST) {
    snprintf(out, out_size, "trim() expects a string or list");
    return 1;
  }

  for (i = 0; i < value->list.count; ++i) {
    if (value->list.items[i].kind == ARKSH_VALUE_STRING) {
      trim_in_place(value->list.items[i].text);
    }
  }
  return 0;
}

static int apply_split_stage(ArkshShell *shell, ArkshValue *value, const ArkshPipelineStageNode *stage, char *out, size_t out_size) {
  char original[ARKSH_MAX_OUTPUT];
  char separator[ARKSH_MAX_OUTPUT];

  if (shell == NULL || value == NULL || stage == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (value->kind != ARKSH_VALUE_STRING) {
    snprintf(out, out_size, "split() expects a string");
    return 1;
  }

  copy_string(original, sizeof(original), value->text);
  if (stage->raw_args[0] == '\0') {
    if (split_text_whitespace_into_value(original, value) != 0) {
      snprintf(out, out_size, "unable to split string");
      return 1;
    }
    return 0;
  }

  if (evaluate_stage_argument_to_text(shell, stage->raw_args, separator, sizeof(separator), out, out_size) != 0) {
    return 1;
  }
  if (separator[0] == '\0') {
    snprintf(out, out_size, "split() separator cannot be empty");
    return 1;
  }
  if (split_text_delimiter_into_value(original, separator, value) != 0) {
    snprintf(out, out_size, "unable to split string");
    return 1;
  }

  return 0;
}

static int apply_join_stage(ArkshShell *shell, ArkshValue *value, const ArkshPipelineStageNode *stage, char *out, size_t out_size) {
  char separator[ARKSH_MAX_OUTPUT];
  size_t i;
  size_t used = 0;

  if (shell == NULL || value == NULL || stage == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (value->kind != ARKSH_VALUE_LIST) {
    snprintf(out, out_size, "join() expects a list");
    return 1;
  }

  separator[0] = '\0';
  if (stage->raw_args[0] != '\0' && evaluate_stage_argument_to_text(shell, stage->raw_args, separator, sizeof(separator), out, out_size) != 0) {
    return 1;
  }

  out[0] = '\0';
  for (i = 0; i < value->list.count; ++i) {
    char rendered[ARKSH_MAX_OUTPUT];
    size_t rendered_len;
    size_t separator_len = strlen(separator);

    if (arksh_value_item_render(&value->list.items[i], rendered, sizeof(rendered)) != 0) {
      snprintf(out, out_size, "unable to render join() item");
      return 1;
    }

    if (i > 0) {
      if (used + separator_len >= sizeof(value->text)) {
        snprintf(out, out_size, "join() result is too large");
        return 1;
      }
      memcpy(value->text + used, separator, separator_len);
      used += separator_len;
    }

    rendered_len = strlen(rendered);
    if (used + rendered_len >= sizeof(value->text)) {
      snprintf(out, out_size, "join() result is too large");
      return 1;
    }
    memcpy(value->text + used, rendered, rendered_len);
    used += rendered_len;
  }

  value->text[used] = '\0';
  value->kind = ARKSH_VALUE_STRING;
  return 0;
}

static int path_has_nested_segments(const char *path) {
  if (path == NULL) {
    return 0;
  }
  return strchr(path, '.') != NULL || strchr(path, '[') != NULL || strchr(path, ']') != NULL;
}

static int apply_pluck_stage(ArkshShell *shell, ArkshValue *value, const ArkshPipelineStageNode *stage, char *out, size_t out_size) {
  char path[ARKSH_MAX_OUTPUT];
  ArkshValue *result;
  size_t i;

  if (shell == NULL || value == NULL || stage == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (value->kind != ARKSH_VALUE_LIST) {
    snprintf(out, out_size, "pluck() expects a list");
    return 1;
  }
  if (stage->raw_args[0] == '\0') {
    snprintf(out, out_size, "pluck() expects a path argument");
    return 1;
  }
  if (evaluate_stage_argument_to_text(shell, stage->raw_args, path, sizeof(path), out, out_size) != 0) {
    return 1;
  }

  result = (ArkshValue *) allocate_temp_buffer(1, sizeof(*result), "pluck() result", out, out_size);
  if (result == NULL) {
    return 1;
  }

  arksh_value_init(result);
  result->kind = ARKSH_VALUE_LIST;

  for (i = 0; i < value->list.count; ++i) {
    ArkshValue *item_value;
    ArkshValue *picked_value;
    ArkshValueItem *picked_item;
    int found = 0;

    item_value = (ArkshValue *) allocate_temp_buffer(1, sizeof(*item_value), "pluck() item value", out, out_size);
    picked_value = (ArkshValue *) allocate_temp_buffer(1, sizeof(*picked_value), "pluck() picked value", out, out_size);
    picked_item = (ArkshValueItem *) allocate_temp_buffer(1, sizeof(*picked_item), "pluck() picked item", out, out_size);
    if (item_value == NULL || picked_value == NULL || picked_item == NULL) {
      free(item_value);
      free(picked_value);
      free(picked_item);
      arksh_value_free(result);
      free(result);
      return 1;
    }

    arksh_value_init(item_value);
    arksh_value_init(picked_value);
    arksh_value_item_init(picked_item);

    if (arksh_value_set_from_item(item_value, &value->list.items[i]) != 0) {
      arksh_value_free(item_value);
      arksh_value_free(picked_value);
      arksh_value_item_free(picked_item);
      free(item_value);
      free(picked_value);
      free(picked_item);
      arksh_value_free(result);
      free(result);
      snprintf(out, out_size, "unable to prepare pluck() item");
      return 1;
    }

    if ((item_value->kind == ARKSH_VALUE_MAP || item_value->kind == ARKSH_VALUE_DICT || item_value->kind == ARKSH_VALUE_LIST) &&
        arksh_value_get_path(item_value, path, picked_value, &found, out, out_size) != 0) {
      arksh_value_free(item_value);
      arksh_value_free(picked_value);
      arksh_value_item_free(picked_item);
      free(item_value);
      free(picked_value);
      free(picked_item);
      arksh_value_free(result);
      free(result);
      return 1;
    }

    if (!found && !path_has_nested_segments(path)) {
      int status = arksh_value_get_property_value(item_value, path, picked_value, out, out_size);
      if (status == 0) {
        found = 1;
      } else if (error_has_prefix(out, "unknown property:")) {
        out[0] = '\0';
        arksh_value_free(picked_value);
        arksh_value_init(picked_value);
      } else {
        arksh_value_free(item_value);
        arksh_value_free(picked_value);
        arksh_value_item_free(picked_item);
        free(item_value);
        free(picked_value);
        free(picked_item);
        arksh_value_free(result);
        free(result);
        return 1;
      }
    }

    if (set_item_from_value(picked_value, picked_item, out, out_size) != 0 || arksh_value_list_append_item(result, picked_item) != 0) {
      arksh_value_free(item_value);
      arksh_value_free(picked_value);
      arksh_value_item_free(picked_item);
      free(item_value);
      free(picked_value);
      free(picked_item);
      arksh_value_free(result);
      free(result);
      snprintf(out, out_size, "pluck() output list is too large");
      return 1;
    }

    arksh_value_free(item_value);
    arksh_value_free(picked_value);
    arksh_value_item_free(picked_item);
    free(item_value);
    free(picked_value);
    free(picked_item);
  }

  arksh_value_free(value);
  if (arksh_value_copy(value, result) != 0) {
    arksh_value_free(result);
    free(result);
    snprintf(out, out_size, "unable to finalize pluck() result");
    return 1;
  }
  arksh_value_free(result);
  free(result);
  return 0;
}

static int apply_to_json_stage(ArkshValue *value, char *out, size_t out_size) {
  char json[ARKSH_MAX_OUTPUT];

  if (value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (arksh_value_to_json(value, json, sizeof(json)) != 0) {
    snprintf(out, out_size, "unable to serialize value as JSON");
    return 1;
  }

  arksh_value_free(value);
  arksh_value_set_string(value, json);
  return 0;
}

static int apply_from_json_stage(ArkshValue *value, char *out, size_t out_size) {
  ArkshValue *parsed;

  if (value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (value->kind != ARKSH_VALUE_STRING) {
    snprintf(out, out_size, "from_json() expects a string");
    return 1;
  }

  parsed = (ArkshValue *) allocate_temp_buffer(1, sizeof(*parsed), "from_json parsed value", out, out_size);
  if (parsed == NULL) {
    return 1;
  }
  if (arksh_value_parse_json(value->text, parsed, out, out_size) != 0) {
    free(parsed);
    return 1;
  }

  arksh_value_free(value);
  *value = *parsed;
  free(parsed);
  return 0;
}

static int apply_reduce_stage(ArkshShell *shell, ArkshValue *value, const ArkshPipelineStageNode *stage, char *out, size_t out_size) {
  char args[2][ARKSH_MAX_LINE];
  int arg_count = 0;
  ArkshBlock block;
  ArkshValue *temp_values;
  size_t index = 0;

  if (shell == NULL || value == NULL || stage == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (value->kind != ARKSH_VALUE_LIST) {
    snprintf(out, out_size, "reduce() expects a list");
    return 1;
  }

  if (stage->raw_args[0] == '\0' || split_top_level_arguments(stage->raw_args, args, 2, &arg_count) != 0 || arg_count < 1 || arg_count > 2) {
    snprintf(out, out_size, "reduce() expects reduce(block) or reduce(init, block)");
    return 1;
  }

  if (resolve_stage_block_argument(shell, args[arg_count - 1], &block, out, out_size) != 0) {
    snprintf(out, out_size, "reduce() expects a block as the last argument");
    return 1;
  }

  temp_values = (ArkshValue *) allocate_temp_buffer(3, sizeof(*temp_values), "reduce() values", out, out_size);
  if (temp_values == NULL) {
    return 1;
  }

  if (arg_count == 2) {
    if (arksh_evaluate_line_value(shell, args[0], &temp_values[0], out, out_size) != 0) {
      free(temp_values);
      return 1;
    }
  } else {
    if (value->list.count == 0) {
      free(temp_values);
      snprintf(out, out_size, "reduce() without an init cannot be used on an empty list");
      return 1;
    }
    if (arksh_value_set_from_item(&temp_values[0], &value->list.items[0]) != 0) {
      free(temp_values);
      snprintf(out, out_size, "unable to prepare reduce() accumulator");
      return 1;
    }
    index = 1;
  }

  for (; index < value->list.count; ++index) {
    if (arksh_value_set_from_item(&temp_values[1], &value->list.items[index]) != 0) {
      free(temp_values);
      snprintf(out, out_size, "unable to prepare reduce() item");
      return 1;
    }
    if (evaluate_block(shell, &block, temp_values, 2, &temp_values[2], out, out_size) != 0) {
      free(temp_values);
      return 1;
    }
    temp_values[0] = temp_values[2];
  }

  *value = temp_values[0];
  free(temp_values);
  return 0;
}

static int parse_each_selector(const char *text, ArkshEachSelector *out_selector, char *error, size_t error_size) {
  ArkshTokenStream stream;
  size_t index = 0;

  if (text == NULL || out_selector == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  memset(out_selector, 0, sizeof(*out_selector));
  if (arksh_lex_line(text, &stream, error, error_size) != 0) {
    return 1;
  }

  if (stream.tokens[index].kind != ARKSH_TOKEN_WORD) {
    snprintf(error, error_size, "each() expects a property or method name");
    return 1;
  }

  copy_string(out_selector->name, sizeof(out_selector->name), stream.tokens[index].text);
  index++;

  if (stream.tokens[index].kind == ARKSH_TOKEN_EOF) {
    if (strcmp(out_selector->name, "render") == 0) {
      out_selector->is_render = 1;
    } else {
      out_selector->member_kind = ARKSH_MEMBER_PROPERTY;
    }
    return 0;
  }

  if (stream.tokens[index].kind != ARKSH_TOKEN_LPAREN) {
    snprintf(error, error_size, "each() expects syntax like each(name) or each(parent())");
    return 1;
  }

  if (strcmp(out_selector->name, "render") == 0) {
    out_selector->is_render = 1;
  } else {
    out_selector->member_kind = ARKSH_MEMBER_METHOD;
  }
  index++;

  while (stream.tokens[index].kind != ARKSH_TOKEN_RPAREN) {
    if (stream.tokens[index].kind == ARKSH_TOKEN_EOF) {
      snprintf(error, error_size, "unterminated each() selector");
      return 1;
    }

    if (!is_value_token_kind(stream.tokens[index].kind)) {
      snprintf(error, error_size, "invalid each() argument token: %s", arksh_token_kind_name(stream.tokens[index].kind));
      return 1;
    }

    if (out_selector->argc >= ARKSH_MAX_ARGS) {
      snprintf(error, error_size, "too many each() arguments");
      return 1;
    }

    copy_string(out_selector->argv[out_selector->argc], sizeof(out_selector->argv[out_selector->argc]), stream.tokens[index].text);
    copy_string(out_selector->raw_argv[out_selector->argc], sizeof(out_selector->raw_argv[out_selector->argc]), stream.tokens[index].raw);
    out_selector->argc++;
    index++;

    if (stream.tokens[index].kind == ARKSH_TOKEN_COMMA) {
      index++;
      continue;
    }

    if (stream.tokens[index].kind != ARKSH_TOKEN_RPAREN) {
      snprintf(error, error_size, "expected ',' or ')' in each() arguments");
      return 1;
    }
  }

  index++;
  if (stream.tokens[index].kind != ARKSH_TOKEN_EOF) {
    snprintf(error, error_size, "unexpected token after each() selector");
    return 1;
  }

  return 0;
}

static int apply_each_selector_to_item(ArkshShell *shell, const ArkshEachSelector *selector, const ArkshValueItem *item, ArkshValue *out_value, char *out, size_t out_size) {
  if (shell == NULL || selector == NULL || item == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (selector->is_render) {
    char rendered[ARKSH_MAX_OUTPUT];

    if (arksh_value_item_render(item, rendered, sizeof(rendered)) != 0) {
      snprintf(out, out_size, "unable to render item inside each()");
      return 1;
    }
    arksh_value_set_string(out_value, rendered);
    return 0;
  }

  if (selector->member_kind == ARKSH_MEMBER_PROPERTY) {
    char property_value[ARKSH_MAX_OUTPUT];

    if (get_item_property_text(shell, item, selector->name, property_value, sizeof(property_value)) != 0) {
      snprintf(out, out_size, "unknown property in each(): %s", selector->name);
      return 1;
    }

    arksh_value_set_string(out_value, property_value);
    return 0;
  }

  if (item->kind == ARKSH_VALUE_OBJECT && is_builtin_object_method_name(selector->name)) {
    char expanded_args[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
    char *argv[ARKSH_MAX_ARGS];
    int i;

    for (i = 0; i < selector->argc; ++i) {
      if (expand_single_word(shell, selector->raw_argv[i], ARKSH_EXPAND_MODE_OBJECT_ARGUMENT, expanded_args[i], sizeof(expanded_args[i]), out, out_size) != 0) {
        return 1;
      }
    }

    build_command_argv(expanded_args, selector->argc, argv);
    return arksh_object_call_method_value(&item->object, selector->name, selector->argc, argv, out_value, out, out_size);
  }

  {
    ArkshValue *receiver;
    ArkshValue *args;
    int status;

    receiver = (ArkshValue *) allocate_temp_buffer(1, sizeof(*receiver), "each() receiver", out, out_size);
    if (receiver == NULL) {
      return 1;
    }

    args = (ArkshValue *) allocate_temp_buffer((size_t) selector->argc, sizeof(*args), "each() arguments", out, out_size);
    if (selector->argc > 0 && args == NULL) {
      free(receiver);
      return 1;
    }

    if (arksh_value_set_from_item(receiver, item) != 0) {
      free(args);
      free(receiver);
      snprintf(out, out_size, "each(%s) method calls are not supported on this item", selector->name);
      return 1;
    }
    if (evaluate_extension_args(shell, selector->raw_argv, selector->argc, args, out, out_size) != 0) {
      free(args);
      free(receiver);
      return 1;
    }
    status = invoke_extension_method_value(shell, receiver, selector->name, selector->argc, args, out_value, out, out_size);
    free(args);
    free(receiver);
    return status;
  }
}

static int apply_each_stage(ArkshShell *shell, ArkshValue *value, const ArkshPipelineStageNode *stage, char *out, size_t out_size) {
  ArkshEachSelector selector;
  ArkshValue *result;
  size_t i;

  if (shell == NULL || value == NULL || stage == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (value->kind != ARKSH_VALUE_LIST) {
    snprintf(out, out_size, "each() expects a list");
    return 1;
  }

  {
    ArkshBlock block;

    if (resolve_stage_block_argument(shell, stage->raw_args, &block, out, out_size) == 0) {
      ArkshValue *temp_values;

      result = (ArkshValue *) allocate_temp_buffer(1, sizeof(*result), "each() result", out, out_size);
      if (result == NULL) {
        return 1;
      }
      temp_values = (ArkshValue *) allocate_temp_buffer(2, sizeof(*temp_values), "each() block values", out, out_size);
      if (temp_values == NULL) {
        free(result);
        return 1;
      }

      arksh_value_init(result);
      result->kind = ARKSH_VALUE_LIST;

      for (i = 0; i < value->list.count; ++i) {
        ArkshValueItem *item = (ArkshValueItem *) allocate_temp_buffer(1, sizeof(*item), "each() block item", out, out_size);

        if (item == NULL) {
          free(temp_values);
          arksh_value_free(result);
          free(result);
          return 1;
        }

        if (arksh_value_set_from_item(&temp_values[0], &value->list.items[i]) != 0) {
          free(item);
          free(temp_values);
          arksh_value_free(result);
          free(result);
          snprintf(out, out_size, "unable to prepare each() block argument");
          return 1;
        }
        if (evaluate_block(shell, &block, &temp_values[0], 1, &temp_values[1], out, out_size) != 0) {
          arksh_value_free(&temp_values[0]);
          free(temp_values);
          arksh_value_free(result);
          free(result);
          return 1;
        }
        if (set_item_from_value(&temp_values[1], item, out, out_size) != 0 || arksh_value_list_append_item(result, item) != 0) {
          arksh_value_item_free(item);
          free(item);
          arksh_value_free(&temp_values[0]);
          arksh_value_free(&temp_values[1]);
          free(temp_values);
          arksh_value_free(result);
          free(result);
          snprintf(out, out_size, "each() output list is too large");
          return 1;
        }
        arksh_value_item_free(item);
        free(item);
        arksh_value_free(&temp_values[0]);
        arksh_value_free(&temp_values[1]);
      }

      arksh_value_free(value);
      if (arksh_value_copy(value, result) != 0) {
        arksh_value_free(result);
        free(temp_values);
        free(result);
        snprintf(out, out_size, "unable to finalize each() block result");
        return 1;
      }
      arksh_value_free(result);
      free(temp_values);
      free(result);
      return 0;
    }

    out[0] = '\0';
  }

  if (parse_each_selector(stage->raw_args, &selector, out, out_size) != 0) {
    return 1;
  }

  result = (ArkshValue *) allocate_temp_buffer(1, sizeof(*result), "each() selector result", out, out_size);
  if (result == NULL) {
    return 1;
  }

  arksh_value_init(result);
  result->kind = ARKSH_VALUE_LIST;

  for (i = 0; i < value->list.count; ++i) {
    ArkshValue *mapped;
    ArkshValueItem *item;

    mapped = (ArkshValue *) allocate_temp_buffer(1, sizeof(*mapped), "each() mapped value", out, out_size);
    if (mapped == NULL) {
      free(result);
      return 1;
    }
    item = (ArkshValueItem *) allocate_temp_buffer(1, sizeof(*item), "each() selector item", out, out_size);
    if (item == NULL) {
      free(mapped);
      free(result);
      return 1;
    }

    if (apply_each_selector_to_item(shell, &selector, &value->list.items[i], mapped, out, out_size) != 0) {
      arksh_value_free(mapped);
      free(mapped);
      free(item);
      arksh_value_free(result);
      free(result);
      return 1;
    }
    if (set_item_from_value(mapped, item, out, out_size) != 0 || arksh_value_list_append_item(result, item) != 0) {
      arksh_value_item_free(item);
      free(item);
      arksh_value_free(mapped);
      free(mapped);
      arksh_value_free(result);
      free(result);
      snprintf(out, out_size, "each() output list is too large");
      return 1;
    }
    arksh_value_item_free(item);
    free(item);
    arksh_value_free(mapped);
    free(mapped);
  }

  arksh_value_free(value);
  if (arksh_value_copy(value, result) != 0) {
    arksh_value_free(result);
    free(result);
    snprintf(out, out_size, "unable to finalize each() result");
    return 1;
  }
  arksh_value_free(result);
  free(result);
  return 0;
}

/* ── E6-S3-T1: map ───────────────────────────────────────────────────────── */

static int apply_map_stage(ArkshShell *shell, ArkshValue *value, const ArkshPipelineStageNode *stage, char *out, size_t out_size) {
  ArkshBlock block;
  ArkshValue *result;
  ArkshValue *temp_values;
  size_t i;

  if (shell == NULL || value == NULL || stage == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (value->kind != ARKSH_VALUE_LIST) {
    snprintf(out, out_size, "map() expects a list");
    return 1;
  }

  if (resolve_stage_block_argument(shell, stage->raw_args, &block, out, out_size) != 0) {
    snprintf(out, out_size, "map() expects a block: map([:it | ...])");
    return 1;
  }

  result = (ArkshValue *) allocate_temp_buffer(1, sizeof(*result), "map() result", out, out_size);
  if (result == NULL) {
    return 1;
  }

  temp_values = (ArkshValue *) allocate_temp_buffer(2, sizeof(*temp_values), "map() block values", out, out_size);
  if (temp_values == NULL) {
    free(result);
    return 1;
  }

  arksh_value_init(result);
  result->kind = ARKSH_VALUE_LIST;

  for (i = 0; i < value->list.count; ++i) {
    ArkshValueItem *item = (ArkshValueItem *) allocate_temp_buffer(1, sizeof(*item), "map() item", out, out_size);

    if (item == NULL) {
      free(temp_values);
      arksh_value_free(result);
      free(result);
      return 1;
    }

    if (arksh_value_set_from_item(&temp_values[0], &value->list.items[i]) != 0) {
      free(item);
      free(temp_values);
      arksh_value_free(result);
      free(result);
      snprintf(out, out_size, "unable to prepare map() block argument");
      return 1;
    }
    if (evaluate_block(shell, &block, &temp_values[0], 1, &temp_values[1], out, out_size) != 0) {
      arksh_value_free(&temp_values[0]);
      free(temp_values);
      arksh_value_free(result);
      free(result);
      return 1;
    }
    if (set_item_from_value(&temp_values[1], item, out, out_size) != 0 ||
        arksh_value_list_append_item(result, item) != 0) {
      arksh_value_item_free(item);
      free(item);
      arksh_value_free(&temp_values[0]);
      arksh_value_free(&temp_values[1]);
      free(temp_values);
      arksh_value_free(result);
      free(result);
      snprintf(out, out_size, "map() output list is too large");
      return 1;
    }
    arksh_value_item_free(item);
    free(item);
    arksh_value_free(&temp_values[0]);
    arksh_value_free(&temp_values[1]);
  }

  arksh_value_free(value);
  if (arksh_value_copy(value, result) != 0) {
    arksh_value_free(result);
    free(temp_values);
    free(result);
    snprintf(out, out_size, "unable to finalize map() result");
    return 1;
  }
  arksh_value_free(result);
  free(temp_values);
  free(result);
  return 0;
}

/* ── E6-S3-T3: flat_map ──────────────────────────────────────────────────── */

static int apply_flat_map_stage(ArkshShell *shell, ArkshValue *value, const ArkshPipelineStageNode *stage, char *out, size_t out_size) {
  ArkshBlock block;
  ArkshValue *result;
  ArkshValue *temp_values;
  size_t i;
  size_t j;

  if (shell == NULL || value == NULL || stage == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (value->kind != ARKSH_VALUE_LIST) {
    snprintf(out, out_size, "flat_map() expects a list");
    return 1;
  }

  if (resolve_stage_block_argument(shell, stage->raw_args, &block, out, out_size) != 0) {
    snprintf(out, out_size, "flat_map() expects a block: flat_map([:it | ...])");
    return 1;
  }

  result = (ArkshValue *) allocate_temp_buffer(1, sizeof(*result), "flat_map() result", out, out_size);
  if (result == NULL) {
    return 1;
  }

  temp_values = (ArkshValue *) allocate_temp_buffer(2, sizeof(*temp_values), "flat_map() block values", out, out_size);
  if (temp_values == NULL) {
    free(result);
    return 1;
  }

  arksh_value_init(result);
  result->kind = ARKSH_VALUE_LIST;

  for (i = 0; i < value->list.count; ++i) {
    if (arksh_value_set_from_item(&temp_values[0], &value->list.items[i]) != 0) {
      free(temp_values);
      arksh_value_free(result);
      free(result);
      snprintf(out, out_size, "unable to prepare flat_map() block argument");
      return 1;
    }
    if (evaluate_block(shell, &block, &temp_values[0], 1, &temp_values[1], out, out_size) != 0) {
      arksh_value_free(&temp_values[0]);
      free(temp_values);
      arksh_value_free(result);
      free(result);
      return 1;
    }

    if (temp_values[1].kind == ARKSH_VALUE_LIST) {
      for (j = 0; j < temp_values[1].list.count; ++j) {
        if (arksh_value_list_append_item(result, &temp_values[1].list.items[j]) != 0) {
          arksh_value_free(&temp_values[0]);
          arksh_value_free(&temp_values[1]);
          free(temp_values);
          arksh_value_free(result);
          free(result);
          snprintf(out, out_size, "flat_map() output list is too large");
          return 1;
        }
      }
    } else {
      ArkshValueItem *item = (ArkshValueItem *) allocate_temp_buffer(1, sizeof(*item), "flat_map() item", out, out_size);

      if (item == NULL) {
        arksh_value_free(&temp_values[0]);
        arksh_value_free(&temp_values[1]);
        free(temp_values);
        arksh_value_free(result);
        free(result);
        return 1;
      }
      if (set_item_from_value(&temp_values[1], item, out, out_size) != 0 ||
          arksh_value_list_append_item(result, item) != 0) {
        arksh_value_item_free(item);
        free(item);
        arksh_value_free(&temp_values[0]);
        arksh_value_free(&temp_values[1]);
        free(temp_values);
        arksh_value_free(result);
        free(result);
        snprintf(out, out_size, "flat_map() output list is too large");
        return 1;
      }
      arksh_value_item_free(item);
      free(item);
    }

    arksh_value_free(&temp_values[0]);
    arksh_value_free(&temp_values[1]);
  }

  arksh_value_free(value);
  if (arksh_value_copy(value, result) != 0) {
    arksh_value_free(result);
    free(temp_values);
    free(result);
    snprintf(out, out_size, "unable to finalize flat_map() result");
    return 1;
  }
  arksh_value_free(result);
  free(temp_values);
  free(result);
  return 0;
}

/* ── E6-S3-T4: group_by ──────────────────────────────────────────────────── */

static int apply_group_by_stage(ArkshShell *shell, ArkshValue *value, const ArkshPipelineStageNode *stage, char *out, size_t out_size) {
  ArkshBlock block;
  int use_block = 0;
  char property[ARKSH_MAX_NAME];
  ArkshValue *result;
  ArkshValue *temp_vals;
  size_t i;

  if (shell == NULL || value == NULL || stage == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (value->kind != ARKSH_VALUE_LIST) {
    snprintf(out, out_size, "group_by() expects a list");
    return 1;
  }

  if (resolve_stage_block_argument(shell, stage->raw_args, &block, out, out_size) == 0) {
    use_block = 1;
    out[0] = '\0';
  } else {
    out[0] = '\0';
    if (sscanf(stage->raw_args, "%127s", property) != 1 || property[0] == '\0') {
      snprintf(out, out_size, "group_by() expects a property name or block");
      return 1;
    }
  }

  result = (ArkshValue *) allocate_temp_buffer(1, sizeof(*result), "group_by() result", out, out_size);
  if (result == NULL) {
    return 1;
  }
  arksh_value_set_map(result);

  temp_vals = (ArkshValue *) allocate_temp_buffer(3, sizeof(*temp_vals), "group_by() temp", out, out_size);
  if (temp_vals == NULL) {
    free(result);
    return 1;
  }

  for (i = 0; i < value->list.count; ++i) {
    char key[ARKSH_MAX_TOKEN];
    const ArkshValueItem *existing;

    if (use_block) {
      if (arksh_value_set_from_item(&temp_vals[0], &value->list.items[i]) != 0) {
        free(temp_vals);
        arksh_value_free(result);
        free(result);
        snprintf(out, out_size, "unable to prepare group_by() block argument");
        return 1;
      }
      if (evaluate_block(shell, &block, &temp_vals[0], 1, &temp_vals[1], out, out_size) != 0) {
        arksh_value_free(&temp_vals[0]);
        free(temp_vals);
        arksh_value_free(result);
        free(result);
        return 1;
      }
      if (arksh_value_render(&temp_vals[1], key, sizeof(key)) != 0) {
        arksh_value_free(&temp_vals[0]);
        arksh_value_free(&temp_vals[1]);
        free(temp_vals);
        arksh_value_free(result);
        free(result);
        snprintf(out, out_size, "group_by(): unable to render block result as key");
        return 1;
      }
      arksh_value_free(&temp_vals[0]);
      arksh_value_free(&temp_vals[1]);
    } else {
      if (get_item_property_text(shell, &value->list.items[i], property, key, sizeof(key)) != 0) {
        free(temp_vals);
        arksh_value_free(result);
        free(result);
        snprintf(out, out_size, "group_by(): unknown property: %s", property);
        return 1;
      }
    }

    arksh_value_init(&temp_vals[2]);
    existing = arksh_value_map_get_item(result, key);
    if (existing != NULL) {
      if (arksh_value_set_from_item(&temp_vals[2], existing) != 0) {
        free(temp_vals);
        arksh_value_free(result);
        free(result);
        snprintf(out, out_size, "group_by(): unable to read existing group");
        return 1;
      }
    } else {
      temp_vals[2].kind = ARKSH_VALUE_LIST;
    }

    if (arksh_value_list_append_item(&temp_vals[2], &value->list.items[i]) != 0) {
      arksh_value_free(&temp_vals[2]);
      free(temp_vals);
      arksh_value_free(result);
      free(result);
      snprintf(out, out_size, "group_by(): group list is too large");
      return 1;
    }
    if (arksh_value_map_set(result, key, &temp_vals[2]) != 0) {
      arksh_value_free(&temp_vals[2]);
      free(temp_vals);
      arksh_value_free(result);
      free(result);
      snprintf(out, out_size, "group_by(): too many groups");
      return 1;
    }
    arksh_value_free(&temp_vals[2]);
  }

  free(temp_vals);
  arksh_value_free(value);
  if (arksh_value_copy(value, result) != 0) {
    arksh_value_free(result);
    free(result);
    snprintf(out, out_size, "unable to finalize group_by() result");
    return 1;
  }
  arksh_value_free(result);
  free(result);
  return 0;
}

/* ── E6-S3-T5: sum / min / max ───────────────────────────────────────────── */

static int item_as_number(ArkshShell *shell, const ArkshValueItem *item, const char *property, double *out_number, char *out, size_t out_size) {
  if (property != NULL && property[0] != '\0') {
    char text[ARKSH_MAX_TOKEN];

    if (get_item_property_text(shell, item, property, text, sizeof(text)) != 0) {
      snprintf(out, out_size, "sum/min/max: unknown property: %s", property);
      return 1;
    }
    *out_number = atof(text);
    return 0;
  }

  if (item->kind == ARKSH_VALUE_NUMBER) {
    *out_number = item->number;
    return 0;
  }
  if (item->kind == ARKSH_VALUE_STRING) {
    *out_number = atof(item->text);
    return 0;
  }
  snprintf(out, out_size, "sum/min/max: item is not a number");
  return 1;
}

static int apply_sum_stage(ArkshShell *shell, ArkshValue *value, const ArkshPipelineStageNode *stage, char *out, size_t out_size) {
  double total = 0.0;
  size_t i;

  if (value->kind != ARKSH_VALUE_LIST) {
    snprintf(out, out_size, "sum() expects a list");
    return 1;
  }

  for (i = 0; i < value->list.count; ++i) {
    double n;

    if (item_as_number(shell, &value->list.items[i], stage->raw_args, &n, out, out_size) != 0) {
      return 1;
    }
    total += n;
  }
  arksh_value_free(value);
  arksh_value_set_number(value, total);
  return 0;
}

static int apply_min_stage(ArkshShell *shell, ArkshValue *value, const ArkshPipelineStageNode *stage, char *out, size_t out_size) {
  double best;
  size_t i;

  if (value->kind != ARKSH_VALUE_LIST) {
    snprintf(out, out_size, "min() expects a list");
    return 1;
  }
  if (value->list.count == 0) {
    snprintf(out, out_size, "min() cannot be used on an empty list");
    return 1;
  }

  if (item_as_number(shell, &value->list.items[0], stage->raw_args, &best, out, out_size) != 0) {
    return 1;
  }
  for (i = 1; i < value->list.count; ++i) {
    double n;

    if (item_as_number(shell, &value->list.items[i], stage->raw_args, &n, out, out_size) != 0) {
      return 1;
    }
    if (n < best) {
      best = n;
    }
  }
  arksh_value_free(value);
  arksh_value_set_number(value, best);
  return 0;
}

static int apply_max_stage(ArkshShell *shell, ArkshValue *value, const ArkshPipelineStageNode *stage, char *out, size_t out_size) {
  double best;
  size_t i;

  if (value->kind != ARKSH_VALUE_LIST) {
    snprintf(out, out_size, "max() expects a list");
    return 1;
  }
  if (value->list.count == 0) {
    snprintf(out, out_size, "max() cannot be used on an empty list");
    return 1;
  }

  if (item_as_number(shell, &value->list.items[0], stage->raw_args, &best, out, out_size) != 0) {
    return 1;
  }
  for (i = 1; i < value->list.count; ++i) {
    double n;

    if (item_as_number(shell, &value->list.items[i], stage->raw_args, &n, out, out_size) != 0) {
      return 1;
    }
    if (n > best) {
      best = n;
    }
  }
  arksh_value_free(value);
  arksh_value_set_number(value, best);
  return 0;
}

/* E6-S7: Base64 encode/decode stages (RFC 4648, pure C, no external deps) */

static const char base64_alphabet[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int apply_base64_encode_stage(ArkshValue *value, char *out, size_t out_size) {
  const unsigned char *src;
  size_t src_len;
  size_t i;
  size_t dst_pos = 0;
  unsigned int b0, b1, b2;
  /* Max input that fits: every 3 input bytes → 4 output chars.
   * Output must fit in ARKSH_MAX_OUTPUT - 1 characters. */
  char encoded[ARKSH_MAX_OUTPUT];

  if (value == NULL || out == NULL || out_size == 0) {
    return 1;
  }
  if (value->kind != ARKSH_VALUE_STRING) {
    snprintf(out, out_size, "base64_encode: expects a string");
    return 1;
  }

  src = (const unsigned char *) value->text;
  src_len = strlen(value->text);

  if (src_len == 0) {
    arksh_value_free(value);
    arksh_value_set_string(value, "");
    return 0;
  }

  /* Check that encoded output fits */
  if (((src_len + 2) / 3) * 4 >= sizeof(encoded)) {
    snprintf(out, out_size, "base64_encode: input too long");
    return 1;
  }

  for (i = 0; i < src_len; i += 3) {
    b0 = src[i];
    b1 = (i + 1 < src_len) ? src[i + 1] : 0;
    b2 = (i + 2 < src_len) ? src[i + 2] : 0;

    encoded[dst_pos++] = base64_alphabet[(b0 >> 2) & 0x3F];
    encoded[dst_pos++] = base64_alphabet[((b0 << 4) | (b1 >> 4)) & 0x3F];
    encoded[dst_pos++] = (i + 1 < src_len) ? base64_alphabet[((b1 << 2) | (b2 >> 6)) & 0x3F] : '=';
    encoded[dst_pos++] = (i + 2 < src_len) ? base64_alphabet[b2 & 0x3F] : '=';
  }
  encoded[dst_pos] = '\0';

  arksh_value_free(value);
  arksh_value_set_string(value, encoded);
  return 0;
}

static int apply_base64_decode_stage(ArkshValue *value, char *out, size_t out_size) {
  static const signed char decode_table[256] = {
    /* Build the reverse lookup for the Base64 alphabet. */
    /* 0x00-0x2B */
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    /* '+' = 0x2B → 62 */
    62,
    -1,-1,-1,
    /* '/' = 0x2F → 63 */
    63,
    /* '0'-'9' = 0x30-0x39 → 52-61 */
    52,53,54,55,56,57,58,59,60,61,
    -1,-1,-1,
    /* '=' = 0x3D → padding, -2 */
    -2,
    -1,-1,-1,
    /* 'A'-'Z' = 0x41-0x5A → 0-25 */
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,
    -1,-1,-1,-1,-1,-1,
    /* 'a'-'z' = 0x61-0x7A → 26-51 */
    26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,
    /* rest */
    -1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
  };

  const char *src;
  size_t src_len;
  size_t i;
  size_t dst_pos = 0;
  signed char c0, c1, c2, c3;
  char decoded[ARKSH_MAX_OUTPUT];

  if (value == NULL || out == NULL || out_size == 0) {
    return 1;
  }
  if (value->kind != ARKSH_VALUE_STRING) {
    snprintf(out, out_size, "base64_decode: expects a string");
    return 1;
  }

  src = value->text;
  src_len = strlen(src);

  if (src_len == 0) {
    arksh_value_free(value);
    arksh_value_set_string(value, "");
    return 0;
  }

  if (src_len % 4 != 0) {
    snprintf(out, out_size, "base64_decode: invalid input length (must be multiple of 4)");
    return 1;
  }

  for (i = 0; i < src_len; i += 4) {
    c0 = decode_table[(unsigned char) src[i]];
    c1 = decode_table[(unsigned char) src[i + 1]];
    c2 = decode_table[(unsigned char) src[i + 2]];
    c3 = decode_table[(unsigned char) src[i + 3]];

    /* c0 and c1 must be real base64 digits (0-63); padding here is invalid */
    if (c0 < 0) {
      snprintf(out, out_size, "base64_decode: invalid character at position %d", (int) i);
      return 1;
    }
    if (c1 < 0) {
      snprintf(out, out_size, "base64_decode: invalid character at position %d", (int)(i + 1));
      return 1;
    }
    /* c2 and c3 may be -2 (padding '=') but not -1 (invalid char) */
    if (c2 == -1) {
      snprintf(out, out_size, "base64_decode: invalid character at position %d", (int)(i + 2));
      return 1;
    }
    if (c3 == -1) {
      snprintf(out, out_size, "base64_decode: invalid character at position %d", (int)(i + 3));
      return 1;
    }

    if (dst_pos + 3 >= sizeof(decoded)) {
      snprintf(out, out_size, "base64_decode: output too long");
      return 1;
    }

    decoded[dst_pos++] = (char)(((unsigned char) c0 << 2) | ((unsigned char) c1 >> 4));
    if (c2 != -2) {
      decoded[dst_pos++] = (char)((((unsigned char) c1 & 0x0F) << 4) | ((unsigned char) c2 >> 2));
    }
    if (c3 != -2) {
      decoded[dst_pos++] = (char)((((unsigned char) c2 & 0x03) << 6) | (unsigned char) c3);
    }
  }
  decoded[dst_pos] = '\0';

  arksh_value_free(value);
  arksh_value_set_string(value, decoded);
  return 0;
}

static int apply_pipeline_stage(ArkshShell *shell, ArkshValue *value, const ArkshPipelineStageNode *stage, char *out, size_t out_size) {
  const ArkshPipelineStageDef *handler;

  if (strcmp(stage->name, "where") == 0) {
    return apply_where_stage(shell, value, stage, out, out_size);
  }

  if (strcmp(stage->name, "sort") == 0) {
    return apply_sort_stage(shell, value, stage, out, out_size);
  }

  if (strcmp(stage->name, "take") == 0) {
    return apply_take_stage(value, stage, out, out_size);
  }

  if (strcmp(stage->name, "first") == 0) {
    return apply_first_stage(value, out, out_size);
  }

  if (strcmp(stage->name, "count") == 0) {
    return apply_count_stage(value, out, out_size);
  }

  if (strcmp(stage->name, "lines") == 0) {
    return apply_lines_stage(value, out, out_size);
  }

  if (strcmp(stage->name, "trim") == 0) {
    return apply_trim_stage(value, out, out_size);
  }

  if (strcmp(stage->name, "grep") == 0) {
    return apply_grep_stage(shell, value, stage, out, out_size);
  }

  if (strcmp(stage->name, "split") == 0) {
    return apply_split_stage(shell, value, stage, out, out_size);
  }

  if (strcmp(stage->name, "join") == 0) {
    return apply_join_stage(shell, value, stage, out, out_size);
  }

  if (strcmp(stage->name, "pluck") == 0) {
    return apply_pluck_stage(shell, value, stage, out, out_size);
  }

  if (strcmp(stage->name, "reduce") == 0) {
    return apply_reduce_stage(shell, value, stage, out, out_size);
  }

  if (strcmp(stage->name, "from_json") == 0) {
    return apply_from_json_stage(value, out, out_size);
  }

  if (strcmp(stage->name, "to_json") == 0) {
    return apply_to_json_stage(value, out, out_size);
  }

  if (strcmp(stage->name, "each") == 0) {
    return apply_each_stage(shell, value, stage, out, out_size);
  }

  if (strcmp(stage->name, "render") == 0) {
    return render_value_in_place(value, out, out_size);
  }

  /* E6-S3 stages */
  if (strcmp(stage->name, "map") == 0) {
    return apply_map_stage(shell, value, stage, out, out_size);
  }

  if (strcmp(stage->name, "filter") == 0) {
    return apply_where_stage(shell, value, stage, out, out_size);
  }

  if (strcmp(stage->name, "flat_map") == 0) {
    return apply_flat_map_stage(shell, value, stage, out, out_size);
  }

  if (strcmp(stage->name, "group_by") == 0) {
    return apply_group_by_stage(shell, value, stage, out, out_size);
  }

  if (strcmp(stage->name, "sum") == 0) {
    return apply_sum_stage(shell, value, stage, out, out_size);
  }

  if (strcmp(stage->name, "min") == 0) {
    return apply_min_stage(shell, value, stage, out, out_size);
  }

  if (strcmp(stage->name, "max") == 0) {
    return apply_max_stage(shell, value, stage, out, out_size);
  }

  /* E6-S7: encoding stages */
  if (strcmp(stage->name, "base64_encode") == 0) {
    return apply_base64_encode_stage(value, out, out_size);
  }

  if (strcmp(stage->name, "base64_decode") == 0) {
    return apply_base64_decode_stage(value, out, out_size);
  }

  /* E6-S8: matrix pipeline stages */
  if (strcmp(stage->name, "transpose") == 0) {
    ArkshMatrix *src, *dst;
    ArkshValue *result;
    size_t r, c;
    const char *row_names[ARKSH_MAX_MATRIX_ROWS];
    char name_buf[ARKSH_MAX_MATRIX_ROWS][16];

    if (value->kind != ARKSH_VALUE_MATRIX || value->matrix == NULL) {
      snprintf(out, out_size, "transpose: value must be a matrix");
      return 1;
    }
    src = value->matrix;
    /* New column names are "row_0", "row_1", ... */
    for (r = 0; r < src->row_count && r < ARKSH_MAX_MATRIX_ROWS; ++r) {
      snprintf(name_buf[r], sizeof(name_buf[r]), "row_%zu", r);
      row_names[r] = name_buf[r];
    }
    result = (ArkshValue *) allocate_temp_buffer(1, sizeof(*result), "transpose result", out, out_size);
    if (result == NULL) {
      return 1;
    }
    arksh_value_set_matrix(result, row_names, src->row_count);
    dst = result->matrix;
    dst->row_count = src->col_count;
    for (r = 0; r < src->col_count; ++r) {
      /* Each new row r corresponds to old column r */
      for (c = 0; c < src->row_count && c < (size_t)ARKSH_MAX_MATRIX_COLS; ++c) {
        dst->rows[r][c] = src->rows[c][r];
      }
    }
    arksh_value_free(value);
    *value = *result;
    free(result);
    return 0;
  }

  if (strcmp(stage->name, "fill_na") == 0) {
    ArkshMatrix *m;
    const char *raw_args_str = stage->raw_args;
    char col_name[ARKSH_MAX_NAME];
    char fill_val[ARKSH_MAX_MATRIX_CELL_TEXT];
    int col_idx;
    size_t row;

    if (value->kind != ARKSH_VALUE_MATRIX || value->matrix == NULL) {
      snprintf(out, out_size, "fill_na: value must be a matrix");
      return 1;
    }
    if (raw_args_str == NULL || raw_args_str[0] == '\0') {
      snprintf(out, out_size, "fill_na: usage fill_na(col_name, value)");
      return 1;
    }
    /* Parse two comma-separated args from raw_args */
    {
      const char *comma = strchr(raw_args_str, ',');
      size_t name_len;
      if (comma == NULL) {
        snprintf(out, out_size, "fill_na: usage fill_na(col_name, value)");
        return 1;
      }
      name_len = (size_t)(comma - raw_args_str);
      if (name_len >= ARKSH_MAX_NAME) name_len = ARKSH_MAX_NAME - 1;
      strncpy(col_name, raw_args_str, name_len);
      col_name[name_len] = '\0';
      /* Trim surrounding quotes/spaces */
      {
        char *p = col_name;
        while (*p == ' ' || *p == '"' || *p == '\'') p++;
        char *e = p + strlen(p) - 1;
        while (e > p && (*e == ' ' || *e == '"' || *e == '\'')) *e-- = '\0';
        memmove(col_name, p, strlen(p) + 1);
      }
      comma++;
      while (*comma == ' ') comma++;
      copy_string(fill_val, sizeof(fill_val), comma);
      {
        char *e = fill_val + strlen(fill_val) - 1;
        while (e >= fill_val && (*e == ' ' || *e == '"' || *e == '\'')) *e-- = '\0';
        char *p = fill_val;
        while (*p == '"' || *p == '\'') p++;
        memmove(fill_val, p, strlen(p) + 1);
      }
    }
    m = value->matrix;
    col_idx = -1;
    for (row = 0; row < m->col_count; ++row) {
      if (strcmp(m->col_names[row], col_name) == 0) { col_idx = (int)row; break; }
    }
    if (col_idx < 0) {
      snprintf(out, out_size, "fill_na: column \"%s\" not found", col_name);
      return 1;
    }
    for (row = 0; row < m->row_count; ++row) {
      ArkshMatrixCell *cell = &m->rows[row][(size_t)col_idx];
      if (cell->kind == ARKSH_VALUE_EMPTY || cell->text[0] == '\0') {
        cell->kind = ARKSH_VALUE_STRING;
        copy_string(cell->text, ARKSH_MAX_MATRIX_CELL_TEXT, fill_val);
      }
    }
    return 0;
  }

  handler = arksh_shell_find_pipeline_stage(shell, stage->name);
  if (handler != NULL && handler->fn != NULL) {
    return handler->fn(shell, value, stage->raw_args, out, out_size);
  }

  /* T5: fallback — try an extension method with this name */
  {
    char ext_err[ARKSH_MAX_OUTPUT];
    ArkshValue *ext_result = (ArkshValue *) arksh_scratch_alloc_active_zero(1, sizeof(ArkshValue));
    if (ext_result != NULL) {
      if (invoke_extension_method_value(shell, value, stage->name, 0, NULL, ext_result, ext_err, sizeof(ext_err)) == 0) {
        arksh_value_free(value);
        *value = *ext_result;
        return 0;
      }
      arksh_value_free(ext_result);
    }
  }

  snprintf(out, out_size, "unknown pipeline stage: %s", stage->name);
  return 1;
}

static int populate_process_spec_from_stage(ArkshShell *shell, const ArkshCommandStageNode *stage, ArkshPlatformProcessSpec *spec, char *out, size_t out_size) {
  char expanded_argv[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
  int expanded_argc = 0;
  size_t i;

  if (shell == NULL || stage == NULL || spec == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (expand_command_arguments(shell, stage->raw_argv, stage->argc, expanded_argv, &expanded_argc, out, out_size) != 0) {
    return 1;
  }

  memset(spec, 0, sizeof(*spec));
  spec->argc = expanded_argc;
  for (i = 0; i < (size_t) expanded_argc && i < ARKSH_MAX_ARGS; ++i) {
    copy_string(spec->argv[i], sizeof(spec->argv[i]), expanded_argv[i]);
  }

  for (i = 0; i < stage->redirection_count; ++i) {
    char expanded_target[ARKSH_MAX_TOKEN];
    ArkshPlatformRedirectionSpec *redirect;

    if (spec->redirection_count >= ARKSH_MAX_REDIRECTIONS) {
      snprintf(out, out_size, "too many process redirections");
      return 1;
    }

    redirect = &spec->redirections[spec->redirection_count++];
    memset(redirect, 0, sizeof(*redirect));
    redirect->fd = stage->redirections[i].fd;
    redirect->target_fd = stage->redirections[i].target_fd;
    redirect->heredoc_strip_tabs = stage->redirections[i].heredoc_strip_tabs;

    switch (stage->redirections[i].kind) {
      case ARKSH_REDIRECT_INPUT:
      case ARKSH_REDIRECT_FD_INPUT:
        if (expand_single_word(shell, stage->redirections[i].raw_target, ARKSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        redirect->input_mode = 1;
        copy_string(redirect->path, sizeof(redirect->path), expanded_target);
        break;
      case ARKSH_REDIRECT_OUTPUT_TRUNCATE:
      case ARKSH_REDIRECT_FD_OUTPUT_TRUNCATE:
        if (expand_single_word(shell, stage->redirections[i].raw_target, ARKSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        redirect->input_mode = 0;
        redirect->append_mode = 0;
        copy_string(redirect->path, sizeof(redirect->path), expanded_target);
        break;
      case ARKSH_REDIRECT_OUTPUT_APPEND:
      case ARKSH_REDIRECT_FD_OUTPUT_APPEND:
        if (expand_single_word(shell, stage->redirections[i].raw_target, ARKSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        redirect->input_mode = 0;
        redirect->append_mode = 1;
        copy_string(redirect->path, sizeof(redirect->path), expanded_target);
        break;
      case ARKSH_REDIRECT_ERROR_TRUNCATE:
        if (expand_single_word(shell, stage->redirections[i].raw_target, ARKSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        redirect->input_mode = 0;
        redirect->append_mode = 0;
        copy_string(redirect->path, sizeof(redirect->path), expanded_target);
        break;
      case ARKSH_REDIRECT_ERROR_APPEND:
        if (expand_single_word(shell, stage->redirections[i].raw_target, ARKSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        redirect->input_mode = 0;
        redirect->append_mode = 1;
        copy_string(redirect->path, sizeof(redirect->path), expanded_target);
        break;
      case ARKSH_REDIRECT_ERROR_TO_OUTPUT:
      case ARKSH_REDIRECT_FD_DUP_INPUT:
      case ARKSH_REDIRECT_FD_DUP_OUTPUT:
        break;
      case ARKSH_REDIRECT_FD_CLOSE:
        redirect->close_target = 1;
        break;
      case ARKSH_REDIRECT_HEREDOC:
        redirect->input_mode = 1;
        copy_string(redirect->text, sizeof(redirect->text), stage->redirections[i].heredoc_body);
        break;
      default:
        snprintf(out, out_size, "unsupported redirection kind");
        return 1;
    }
  }

  return 0;
}

static int redirection_affects_stdin(const ArkshRedirectionNode *redirection) {
  if (redirection == NULL) {
    return 0;
  }

  switch (redirection->kind) {
    case ARKSH_REDIRECT_INPUT:
    case ARKSH_REDIRECT_HEREDOC:
      return 1;
    case ARKSH_REDIRECT_FD_INPUT:
    case ARKSH_REDIRECT_FD_DUP_INPUT:
    case ARKSH_REDIRECT_FD_CLOSE:
      return redirection->fd == 0;
    default:
      return 0;
  }
}

static int process_spec_has_stdin_redirection(const ArkshPlatformProcessSpec *spec) {
  size_t i;

  if (spec == NULL) {
    return 0;
  }

  for (i = 0; i < spec->redirection_count; ++i) {
    if (spec->redirections[i].fd == 0) {
      return 1;
    }
  }

  return 0;
}

static int append_input_redirection_to_process_spec(
  ArkshShell *shell,
  const ArkshRedirectionNode *redirection,
  ArkshPlatformProcessSpec *spec,
  char *out,
  size_t out_size
) {
  ArkshPlatformRedirectionSpec *redirect;

  if (shell == NULL || redirection == NULL || spec == NULL || out == NULL || out_size == 0) {
    return 1;
  }
  if (spec->redirection_count >= ARKSH_MAX_REDIRECTIONS) {
    snprintf(out, out_size, "too many process redirections");
    return 1;
  }

  redirect = &spec->redirections[spec->redirection_count++];
  memset(redirect, 0, sizeof(*redirect));
  redirect->fd = redirection->fd;
  redirect->target_fd = redirection->target_fd;
  redirect->heredoc_strip_tabs = redirection->heredoc_strip_tabs;

  switch (redirection->kind) {
    case ARKSH_REDIRECT_INPUT:
    case ARKSH_REDIRECT_FD_INPUT:
      if (expand_single_word(shell, redirection->raw_target, ARKSH_EXPAND_MODE_REDIRECT_TARGET, redirect->path, sizeof(redirect->path), out, out_size) != 0) {
        return 1;
      }
      redirect->input_mode = 1;
      return 0;
    case ARKSH_REDIRECT_HEREDOC:
      redirect->input_mode = 1;
      copy_string(redirect->text, sizeof(redirect->text), redirection->heredoc_body);
      return 0;
    case ARKSH_REDIRECT_FD_DUP_INPUT:
      return 0;
    case ARKSH_REDIRECT_FD_CLOSE:
      redirect->close_target = 1;
      return 0;
    default:
      snprintf(out, out_size, "unsupported input redirection kind");
      return 1;
  }
}

static int apply_inherited_input_redirection(
  ArkshShell *shell,
  ArkshPlatformProcessSpec *spec,
  char *out,
  size_t out_size
) {
  if (shell == NULL || spec == NULL || out == NULL || out_size == 0) {
    return 1;
  }
  if (!shell->inherited_input_active || !redirection_affects_stdin(&shell->inherited_input_redirection)) {
    return 0;
  }
  if (process_spec_has_stdin_redirection(spec)) {
    return 0;
  }

  return append_input_redirection_to_process_spec(shell, &shell->inherited_input_redirection, spec, out, out_size);
}

int arksh_execute_external_command(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  ArkshPlatformProcessSpec spec;
  ArkshPlatformAsyncProcess stopped_process;
  int exit_code = 0;
  int status;
  int i;

  if (shell == NULL || argv == NULL || out == NULL || out_size == 0 || argc <= 0) {
    return 1;
  }

  memset(&spec, 0, sizeof(spec));
  spec.argc = argc;

  for (i = 0; i < argc && i < ARKSH_MAX_ARGS; ++i) {
    copy_string(spec.argv[i], sizeof(spec.argv[i]), argv[i]);
  }
  if (apply_inherited_input_redirection(shell, &spec, out, out_size) != 0) {
    return 1;
  }

  memset(&stopped_process, 0, sizeof(stopped_process));
  status = arksh_platform_run_process_pipeline(shell->cwd, &spec, 1, out, out_size, &exit_code, &stopped_process, shell->force_capture);
  if (status != 0) {
    if (out[0] == '\0') {
      snprintf(out, out_size, "unable to execute external command: %s", argv[0]);
    }
    return 1;
  }

  /* E4-S1: foreground pipeline stopped — add to job table */
  if (stopped_process.pgid != 0 && shell->job_count < ARKSH_MAX_JOBS) {
    memset(&shell->jobs[shell->job_count], 0, sizeof(shell->jobs[0]));
    shell->jobs[shell->job_count].id      = shell->next_job_id++;
    shell->jobs[shell->job_count].state   = ARKSH_JOB_STOPPED;
    shell->jobs[shell->job_count].process = stopped_process;
    copy_string(shell->jobs[shell->job_count].command, sizeof(shell->jobs[0].command), argv[0]);
    shell->job_count++;
    out[0] = '\0';
    return 1;
  }

  return exit_code == 0 ? 0 : 1;
}

static int execute_builtin_with_redirection(
  ArkshShell *shell,
  const ArkshCommandDef *command_def,
  const ArkshCommandStageNode *stage,
  char *out,
  size_t out_size
) {
  char expanded_argv[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
  char *argv[ARKSH_MAX_ARGS];
  char command_output[ARKSH_MAX_OUTPUT];
  int expanded_argc = 0;
  size_t i;

  if (shell == NULL || command_def == NULL || stage == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (expand_command_arguments(shell, stage->raw_argv, stage->argc, expanded_argv, &expanded_argc, out, out_size) != 0) {
    return 1;
  }

  build_command_argv(expanded_argv, expanded_argc, argv);
  if (command_def->fn(shell, expanded_argc, argv, command_output, sizeof(command_output)) != 0) {
    copy_string(out, out_size, command_output);
    return 1;
  }

  for (i = 0; i < stage->redirection_count; ++i) {
    char expanded_target[ARKSH_MAX_TOKEN];

    switch (stage->redirections[i].kind) {
      case ARKSH_REDIRECT_INPUT:
      case ARKSH_REDIRECT_HEREDOC:
        /* Built-ins do not read from stdin; ignore input redirections. */
        break;
      case ARKSH_REDIRECT_OUTPUT_TRUNCATE:
        if (expand_single_word(shell, stage->redirections[i].raw_target, ARKSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        if (write_text_file(expanded_target, command_output, 0, out, out_size) != 0) {
          return 1;
        }
        command_output[0] = '\0';
        break;
      case ARKSH_REDIRECT_OUTPUT_APPEND:
        if (expand_single_word(shell, stage->redirections[i].raw_target, ARKSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        if (write_text_file(expanded_target, command_output, 1, out, out_size) != 0) {
          return 1;
        }
        command_output[0] = '\0';
        break;
      case ARKSH_REDIRECT_ERROR_TRUNCATE:
        /* On success stderr is empty; truncate/create the target file. */
        if (expand_single_word(shell, stage->redirections[i].raw_target, ARKSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        if (write_text_file(expanded_target, "", 0, out, out_size) != 0) {
          return 1;
        }
        break;
      case ARKSH_REDIRECT_ERROR_APPEND:
        /* On success stderr is empty; nothing to append. */
        break;
      case ARKSH_REDIRECT_ERROR_TO_OUTPUT:
        /* On success stderr is empty; nothing to merge into stdout. */
        break;
      case ARKSH_REDIRECT_FD_INPUT:
      case ARKSH_REDIRECT_FD_OUTPUT_TRUNCATE:
      case ARKSH_REDIRECT_FD_OUTPUT_APPEND:
      case ARKSH_REDIRECT_FD_DUP_INPUT:
      case ARKSH_REDIRECT_FD_DUP_OUTPUT:
      case ARKSH_REDIRECT_FD_CLOSE:
        /* FD-level redirections are not applicable to built-ins; ignore. */
        break;
      default:
        break;
    }
  }

  copy_string(out, out_size, command_output);
  return 0;
}

static int execute_function_definition(
  ArkshShell *shell,
  const ArkshFunctionCommandNode *function_node,
  char *out,
  size_t out_size
) {
  if (shell == NULL || function_node == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (arksh_shell_set_function(shell, function_node) != 0) {
    snprintf(out, out_size, "unable to define function: %s", function_node->name);
    return 1;
  }

  out[0] = '\0';
  return 0;
}

static int execute_class_definition(
  ArkshShell *shell,
  const ArkshClassCommandNode *class_node,
  char *out,
  size_t out_size
) {
  if (shell == NULL || class_node == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  return arksh_shell_set_class(shell, class_node, out, out_size);
}

static int execute_shell_function(
  ArkshShell *shell,
  const ArkshShellFunction *function_def,
  const ArkshSimpleCommandNode *command,
  char *out,
  size_t out_size
) {
  ArkshFunctionScopeSnapshot *snapshot;
  int status;

  if (shell == NULL || function_def == NULL || command == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  snapshot = (ArkshFunctionScopeSnapshot *) allocate_temp_buffer(1, sizeof(*snapshot), "function scope snapshot", out, out_size);
  if (snapshot == NULL) {
    return 1;
  }

  if (snapshot_function_scope(shell, snapshot, out, out_size) != 0) {
    free(snapshot);
    return 1;
  }
  if (bind_function_parameters(shell, function_def, command, out, out_size) != 0) {
    restore_function_scope(shell, snapshot, out, out_size);
    free_function_scope_snapshot(snapshot);
    free(snapshot);
    return 1;
  }

  shell->function_depth++;
  status = arksh_shell_execute_line(shell, function_def->body, out, out_size);
  shell->function_depth--;
  if (status == 0 && shell->control_signal == ARKSH_CONTROL_SIGNAL_RETURN) {
    arksh_shell_clear_control_signal(shell);
  }
  if (restore_function_scope(shell, snapshot, out, out_size) != 0) {
    free_function_scope_snapshot(snapshot);
    free(snapshot);
    return 1;
  }

  free_function_scope_snapshot(snapshot);
  free(snapshot);
  return status;
}

/* E1-S7-T1/T2: split a POSIX assignment token NAME=value.
   Returns 1 and fills name_buf / *value_out on success, 0 otherwise. */
static int split_posix_assignment(const char *s, char *name_buf, size_t name_buf_size, const char **value_out) {
  size_t i;
  if (s == NULL || name_buf == NULL || value_out == NULL) return 0;
  if (!isalpha((unsigned char)s[0]) && s[0] != '_') return 0;
  for (i = 1; s[i] != '\0' && s[i] != '='; i++) {
    if (!isalnum((unsigned char)s[i]) && s[i] != '_') return 0;
  }
  if (s[i] != '=') return 0;
  if (name_buf_size <= i) return 0;
  memcpy(name_buf, s, i);
  name_buf[i] = '\0';
  *value_out = s + i + 1;
  return 1;
}

static int execute_simple_command(ArkshShell *shell, const ArkshSimpleCommandNode *command, char *out, size_t out_size) {
  char expanded_argv[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
  char *argv[ARKSH_MAX_ARGS];
  const ArkshCommandDef *command_def;
  const ArkshShellFunction *function_def;
  int expanded_argc = 0;

  if (shell == NULL || command == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (expand_command_arguments(shell, command->raw_argv, command->argc, expanded_argv, &expanded_argc, out, out_size) != 0) {
    return 1;
  }

  if (expanded_argc == 0) {
    return 0;
  }

  /* E1-S7-T1: POSIX standalone assignment — VAR=value [VAR2=value2 ...]
     If every argument is a NAME=value token, set variables and return. */
  {
    int ai, all_assignments = 1;
    for (ai = 0; ai < expanded_argc; ai++) {
      char name[ARKSH_MAX_TOKEN];
      const char *value;
      if (!split_posix_assignment(expanded_argv[ai], name, sizeof(name), &value)) {
        all_assignments = 0;
        break;
      }
    }
    if (all_assignments) {
      for (ai = 0; ai < expanded_argc; ai++) {
        char name[ARKSH_MAX_TOKEN];
        const char *value;
        split_posix_assignment(expanded_argv[ai], name, sizeof(name), &value);
        if (arksh_shell_set_var(shell, name, value, 0) != 0) {
          snprintf(out, out_size, "assignment: cannot set '%s'", name);
          return 1;
        }
      }
      return 0;
    }
  }

  /* E1-S7-T2: VAR=val cmd — leading assignments become env vars for the
     child process; they are NOT set in the current shell environment. */
  {
    int prefix_count = 0, ai;
    for (ai = 0; ai < expanded_argc; ai++) {
      char name[ARKSH_MAX_TOKEN];
      const char *value;
      if (!split_posix_assignment(expanded_argv[ai], name, sizeof(name), &value)) break;
      prefix_count++;
    }
    if (prefix_count > 0 && prefix_count < expanded_argc) {
      /* Rebuild argv without the assignment prefix, temporarily export vars. */
      char saved_values[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
      int saved_existed[ARKSH_MAX_ARGS];
      char names[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
      int result, ri;

      for (ai = 0; ai < prefix_count; ai++) {
        const char *value;
        const char *existing;
        split_posix_assignment(expanded_argv[ai], names[ai], sizeof(names[ai]), &value);
        existing = arksh_shell_get_var(shell, names[ai]);
        if (existing != NULL) {
          saved_existed[ai] = 1;
          copy_string(saved_values[ai], sizeof(saved_values[ai]), existing);
        } else {
          saved_existed[ai] = 0;
          saved_values[ai][0] = '\0';
        }
        arksh_shell_set_var(shell, names[ai], value, 1);
      }

      /* Execute the command portion */
      {
        char sub_expanded[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
        char *sub_argv[ARKSH_MAX_ARGS];
        int sub_argc = expanded_argc - prefix_count;
        for (ri = 0; ri < sub_argc; ri++) {
          copy_string(sub_expanded[ri], sizeof(sub_expanded[ri]), expanded_argv[prefix_count + ri]);
        }
        build_command_argv(sub_expanded, sub_argc, sub_argv);

        function_def = arksh_shell_find_function(shell, sub_argv[0]);
        command_def = find_registered_command(shell, sub_argv[0]);

        if (function_def != NULL) {
          ArkshSimpleCommandNode expanded_command;
          memset(&expanded_command, 0, sizeof(expanded_command));
          expanded_command.argc = sub_argc;
          for (ri = 0; ri < sub_argc; ri++) {
            copy_string(expanded_command.argv[ri], sizeof(expanded_command.argv[ri]), sub_expanded[ri]);
            copy_string(expanded_command.raw_argv[ri], sizeof(expanded_command.raw_argv[ri]), sub_expanded[ri]);
          }
          result = execute_shell_function(shell, function_def, &expanded_command, out, out_size);
        } else if (command_def != NULL) {
          result = command_def->fn(shell, sub_argc, sub_argv, out, out_size);
        } else {
          result = arksh_execute_external_command(shell, sub_argc, sub_argv, out, out_size);
        }
      }

      /* Restore previous values (un-export or restore) */
      for (ai = 0; ai < prefix_count; ai++) {
        if (saved_existed[ai]) {
          arksh_shell_set_var(shell, names[ai], saved_values[ai], 0);
        } else {
          arksh_shell_unset_var(shell, names[ai]);
        }
      }
      return result;
    }
  }

  /* E1-S6-T2: xtrace — print expanded command to stderr before execution. */
  if (shell->opt_xtrace) {
    int ti;
    fprintf(stderr, "+");
    for (ti = 0; ti < expanded_argc; ti++) {
      fprintf(stderr, " %s", expanded_argv[ti]);
    }
    fprintf(stderr, "\n");
    fflush(stderr);
  }

  function_def = arksh_shell_find_function(shell, expanded_argv[0]);
  command_def = find_registered_command(shell, expanded_argv[0]);
  build_command_argv(expanded_argv, expanded_argc, argv);

  /* E3-S5: user functions override built-ins. Use `builtin <name>` to call
     the original built-in directly when inside an override function. */
  if (function_def != NULL) {
    ArkshSimpleCommandNode expanded_command;
    int i;

    memset(&expanded_command, 0, sizeof(expanded_command));
    expanded_command.argc = expanded_argc;
    for (i = 0; i < expanded_argc; ++i) {
      copy_string(expanded_command.argv[i], sizeof(expanded_command.argv[i]), expanded_argv[i]);
      copy_string(expanded_command.raw_argv[i], sizeof(expanded_command.raw_argv[i]), expanded_argv[i]);
    }
    return execute_shell_function(shell, function_def, &expanded_command, out, out_size);
  }
  if (command_def != NULL) {
    return command_def->fn(shell, expanded_argc, argv, out, out_size);
  }

  return arksh_execute_external_command(shell, expanded_argc, argv, out, out_size);
}

static int execute_shell_pipeline(ArkshShell *shell, const ArkshCommandPipelineNode *pipeline, char *out, size_t out_size) {
  ArkshPlatformProcessSpec specs[ARKSH_MAX_PIPELINE_STAGES];
  ArkshPlatformAsyncProcess stopped_process;
  size_t i;
  int exit_code = 0;

  if (shell == NULL || pipeline == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  /* ── Single-stage shortcut ─────────────────────────────────────────────── */
  if (pipeline->stage_count == 1) {
    char expanded_name[ARKSH_MAX_TOKEN];
    const ArkshCommandDef *command_def;

    if (expand_single_word(shell, pipeline->stages[0].raw_argv[0], ARKSH_EXPAND_MODE_COMMAND_NAME, expanded_name, sizeof(expanded_name), out, out_size) != 0) {
      return 1;
    }
    command_def = find_registered_command(shell, expanded_name);
    if (command_def != NULL) {
      return execute_builtin_with_redirection(shell, command_def, &pipeline->stages[0], out, out_size);
    }
    memset(&specs[0], 0, sizeof(specs[0]));
    if (populate_process_spec_from_stage(shell, &pipeline->stages[0], &specs[0], out, out_size) != 0) {
      return 1;
    }
    if (apply_inherited_input_redirection(shell, &specs[0], out, out_size) != 0) {
      return 1;
    }
    memset(&stopped_process, 0, sizeof(stopped_process));
    if (arksh_platform_run_process_pipeline(shell->cwd, specs, 1, out, out_size, &exit_code, &stopped_process, shell->force_capture) != 0) {
      if (out[0] == '\0') snprintf(out, out_size, "failed to execute shell pipeline");
      return 1;
    }
    if (stopped_process.pgid != 0 && shell->job_count < ARKSH_MAX_JOBS) {
      memset(&shell->jobs[shell->job_count], 0, sizeof(shell->jobs[0]));
      shell->jobs[shell->job_count].id      = shell->next_job_id++;
      shell->jobs[shell->job_count].state   = ARKSH_JOB_STOPPED;
      shell->jobs[shell->job_count].process = stopped_process;
      copy_string(shell->jobs[shell->job_count].command, sizeof(shell->jobs[0].command), specs[0].argv[0]);
      shell->job_count++;
      out[0] = '\0';
      return 1;
    }
    return exit_code == 0 ? 0 : 1;
  }

  /* ── Multi-stage: check stage 0 for built-in ───────────────────────────── */
  {
    char expanded_name_0[ARKSH_MAX_TOKEN];
    const ArkshCommandDef *command_def_0;

    if (expand_single_word(shell, pipeline->stages[0].raw_argv[0], ARKSH_EXPAND_MODE_COMMAND_NAME, expanded_name_0, sizeof(expanded_name_0), out, out_size) != 0) {
      return 1;
    }
    command_def_0 = find_registered_command(shell, expanded_name_0);

    if (command_def_0 != NULL) {
      if (command_def_0->kind != ARKSH_BUILTIN_PURE) {
        /* T2: MUTANT/MIXED built-ins modify shell state and cannot be piped */
        snprintf(out, out_size,
          "built-in '%s' modifies shell state and cannot be used as a pipeline source",
          expanded_name_0);
        return 1;
      }

      /* T1: PURE built-in at stage 0 — run it, inject output into stage 1 */
      {
        char builtin_out[ARKSH_MAX_OUTPUT];
        ArkshPlatformRedirectionSpec *stdin_inject;

        builtin_out[0] = '\0';
        if (execute_builtin_with_redirection(shell, command_def_0, &pipeline->stages[0], builtin_out, sizeof(builtin_out)) != 0) {
          copy_string(out, out_size, builtin_out);
          return 1;
        }

        /* Populate specs for the remaining external stages */
        for (i = 1; i < pipeline->stage_count; ++i) {
          char expanded_name[ARKSH_MAX_TOKEN];
          const ArkshCommandDef *cmd;

          if (expand_single_word(shell, pipeline->stages[i].raw_argv[0], ARKSH_EXPAND_MODE_COMMAND_NAME, expanded_name, sizeof(expanded_name), out, out_size) != 0) {
            return 1;
          }
          cmd = find_registered_command(shell, expanded_name);
          if (cmd != NULL) {
            snprintf(out, out_size, "built-in '%s' cannot appear after stage 0 in a shell pipeline", expanded_name);
            return 1;
          }
          if (populate_process_spec_from_stage(shell, &pipeline->stages[i], &specs[i - 1], out, out_size) != 0) {
            return 1;
          }
        }

        /* Inject built-in output as text stdin into the first external stage */
        if (specs[0].redirection_count < ARKSH_MAX_REDIRECTIONS) {
          stdin_inject = &specs[0].redirections[specs[0].redirection_count++];
          memset(stdin_inject, 0, sizeof(*stdin_inject));
          stdin_inject->fd = 0;
          stdin_inject->input_mode = 1;
          copy_string(stdin_inject->text, sizeof(stdin_inject->text), builtin_out);
        }

        memset(&stopped_process, 0, sizeof(stopped_process));
        if (arksh_platform_run_process_pipeline(shell->cwd, specs, pipeline->stage_count - 1, out, out_size, &exit_code, &stopped_process, shell->force_capture) != 0) {
          if (out[0] == '\0') snprintf(out, out_size, "failed to execute shell pipeline");
          return 1;
        }
        if (stopped_process.pgid != 0 && shell->job_count < ARKSH_MAX_JOBS) {
          memset(&shell->jobs[shell->job_count], 0, sizeof(shell->jobs[0]));
          shell->jobs[shell->job_count].id      = shell->next_job_id++;
          shell->jobs[shell->job_count].state   = ARKSH_JOB_STOPPED;
          shell->jobs[shell->job_count].process = stopped_process;
          copy_string(shell->jobs[shell->job_count].command, sizeof(shell->jobs[0].command), specs[0].argv[0]);
          shell->job_count++;
          out[0] = '\0';
          return 1;
        }
        return exit_code == 0 ? 0 : 1;
      }
    }
  }

  /* ── All-external multi-stage pipeline ─────────────────────────────────── */
  for (i = 0; i < pipeline->stage_count; ++i) {
    char expanded_name[ARKSH_MAX_TOKEN];
    const ArkshCommandDef *command_def;

    if (expand_single_word(shell, pipeline->stages[i].raw_argv[0], ARKSH_EXPAND_MODE_COMMAND_NAME, expanded_name, sizeof(expanded_name), out, out_size) != 0) {
      return 1;
    }
    command_def = find_registered_command(shell, expanded_name);
    if (command_def != NULL) {
      snprintf(out, out_size, "built-in '%s' cannot appear in the middle or end of a shell pipeline", expanded_name);
      return 1;
    }
    if (populate_process_spec_from_stage(shell, &pipeline->stages[i], &specs[i], out, out_size) != 0) {
      return 1;
    }
  }

  if (apply_inherited_input_redirection(shell, &specs[0], out, out_size) != 0) {
    return 1;
  }

  memset(&stopped_process, 0, sizeof(stopped_process));
  if (arksh_platform_run_process_pipeline(shell->cwd, specs, pipeline->stage_count, out, out_size, &exit_code, &stopped_process, shell->force_capture) != 0) {
    if (out[0] == '\0') {
      snprintf(out, out_size, "failed to execute shell pipeline");
    }
    return 1;
  }

  if (stopped_process.pgid != 0 && shell->job_count < ARKSH_MAX_JOBS) {
    memset(&shell->jobs[shell->job_count], 0, sizeof(shell->jobs[0]));
    shell->jobs[shell->job_count].id      = shell->next_job_id++;
    shell->jobs[shell->job_count].state   = ARKSH_JOB_STOPPED;
    shell->jobs[shell->job_count].process = stopped_process;
    copy_string(shell->jobs[shell->job_count].command, sizeof(shell->jobs[0].command), specs[0].argv[0]);
    shell->job_count++;
    out[0] = '\0';
    return 1;
  }

  return exit_code == 0 ? 0 : 1;
}

static int apply_compound_command_redirections(
  ArkshShell *shell,
  const ArkshRedirectionNode *redirections,
  size_t redirection_count,
  int status,
  const char *command_output,
  char *out,
  size_t out_size
) {
  char stdout_text[ARKSH_MAX_OUTPUT];
  char stderr_text[ARKSH_MAX_OUTPUT];
  size_t i;

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  copy_string(stdout_text, sizeof(stdout_text), status == 0 ? command_output : "");
  copy_string(stderr_text, sizeof(stderr_text), status == 0 ? "" : command_output);

  for (i = 0; i < redirection_count; ++i) {
    char expanded_target[ARKSH_MAX_TOKEN];

    switch (redirections[i].kind) {
      case ARKSH_REDIRECT_INPUT:
      case ARKSH_REDIRECT_HEREDOC:
        break;
      case ARKSH_REDIRECT_OUTPUT_TRUNCATE:
        if (expand_single_word(shell, redirections[i].raw_target, ARKSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        if (write_text_file(expanded_target, stdout_text, 0, out, out_size) != 0) {
          return 1;
        }
        stdout_text[0] = '\0';
        break;
      case ARKSH_REDIRECT_OUTPUT_APPEND:
        if (expand_single_word(shell, redirections[i].raw_target, ARKSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        if (write_text_file(expanded_target, stdout_text, 1, out, out_size) != 0) {
          return 1;
        }
        stdout_text[0] = '\0';
        break;
      case ARKSH_REDIRECT_ERROR_TRUNCATE:
        if (expand_single_word(shell, redirections[i].raw_target, ARKSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        if (write_text_file(expanded_target, stderr_text, 0, out, out_size) != 0) {
          return 1;
        }
        stderr_text[0] = '\0';
        break;
      case ARKSH_REDIRECT_ERROR_APPEND:
        if (expand_single_word(shell, redirections[i].raw_target, ARKSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        if (write_text_file(expanded_target, stderr_text, 1, out, out_size) != 0) {
          return 1;
        }
        stderr_text[0] = '\0';
        break;
      case ARKSH_REDIRECT_ERROR_TO_OUTPUT:
        if (append_output_segment(stdout_text, sizeof(stdout_text), stderr_text) != 0) {
          snprintf(out, out_size, "combined command output too large");
          return 1;
        }
        stderr_text[0] = '\0';
        break;
      case ARKSH_REDIRECT_FD_INPUT:
      case ARKSH_REDIRECT_FD_DUP_INPUT:
      case ARKSH_REDIRECT_FD_CLOSE:
        if (redirections[i].fd == 0) {
          break;
        }
        snprintf(out, out_size, "unsupported redirection kind");
        return 1;
      default:
        snprintf(out, out_size, "unsupported redirection kind");
        return 1;
    }
  }

  if (status == 0) {
    copy_string(out, out_size, stdout_text);
  } else if (stderr_text[0] != '\0') {
    copy_string(out, out_size, stderr_text);
  } else {
    copy_string(out, out_size, stdout_text);
  }
  return 0;
}

static int execute_compound_body(
  ArkshShell *shell,
  const ArkshCompoundCommandNode *compound,
  char *out,
  size_t out_size
) {
  char command_output[ARKSH_MAX_OUTPUT];
  int previous_inherited_input_active;
  ArkshRedirectionNode previous_inherited_input;
  size_t i;
  int status;

  if (shell == NULL || compound == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  previous_inherited_input_active = shell->inherited_input_active;
  previous_inherited_input = shell->inherited_input_redirection;
  shell->inherited_input_active = previous_inherited_input_active;
  shell->inherited_input_redirection = previous_inherited_input;
  for (i = 0; i < compound->redirection_count; ++i) {
    if (!redirection_affects_stdin(&compound->redirections[i])) {
      continue;
    }
    shell->inherited_input_active = 1;
    shell->inherited_input_redirection = compound->redirections[i];
  }

  command_output[0] = '\0';
  status = arksh_shell_execute_line(shell, compound->body, command_output, sizeof(command_output));
  shell->inherited_input_active = previous_inherited_input_active;
  shell->inherited_input_redirection = previous_inherited_input;
  if (apply_compound_command_redirections(shell, compound->redirections, compound->redirection_count, status, command_output, out, out_size) != 0) {
    return 1;
  }

  return status;
}

static int execute_group_command(
  ArkshShell *shell,
  const ArkshCompoundCommandNode *compound,
  char *out,
  size_t out_size
) {
  return execute_compound_body(shell, compound, out, out_size);
}

static int execute_subshell_command(
  ArkshShell *shell,
  const ArkshCompoundCommandNode *compound,
  char *out,
  size_t out_size
) {
  ArkshShellStateSnapshot *snapshot;
  char command_output[ARKSH_MAX_OUTPUT];
  int status;

  if (shell == NULL || compound == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  snapshot = (ArkshShellStateSnapshot *) allocate_temp_buffer(1, sizeof(*snapshot), "subshell state snapshot", out, out_size);
  if (snapshot == NULL) {
    return 1;
  }
  if (snapshot_shell_state(shell, snapshot, out, out_size) != 0) {
    free(snapshot);
    return 1;
  }

  command_output[0] = '\0';
  status = execute_compound_body(shell, compound, command_output, sizeof(command_output));
  if (restore_shell_state(shell, snapshot, out, out_size) != 0) {
    free_shell_state_snapshot(snapshot);
    free(snapshot);
    return 1;
  }

  free_shell_state_snapshot(snapshot);
  free(snapshot);
  copy_string(out, out_size, command_output);
  return status;
}

static int execute_object_pipeline(ArkshShell *shell, const ArkshObjectPipelineNode *pipeline, char *out, size_t out_size) {
  ArkshValue *value;
  size_t i;
  int status;

  if (shell == NULL || pipeline == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  value = (ArkshValue *) allocate_temp_buffer(1, sizeof(*value), "object pipeline value", out, out_size);
  if (value == NULL) {
    return 1;
  }

  if (evaluate_value_source(shell, &pipeline->source, value, out, out_size) != 0) {
    free(value);
    return 1;
  }

  for (i = 0; i < pipeline->stage_count; ++i) {
    if (apply_pipeline_stage(shell, value, &pipeline->stages[i], out, out_size) != 0) {
      free(value);
      return 1;
    }
  }

  status = arksh_value_render(value, out, out_size);
  free(value);
  return status;
}

static int evaluate_object_pipeline_value(ArkshShell *shell, const ArkshObjectPipelineNode *pipeline, ArkshValue *value, char *out, size_t out_size) {
  size_t i;

  if (shell == NULL || pipeline == NULL || value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (evaluate_value_source(shell, &pipeline->source, value, out, out_size) != 0) {
    return 1;
  }

  for (i = 0; i < pipeline->stage_count; ++i) {
    if (apply_pipeline_stage(shell, value, &pipeline->stages[i], out, out_size) != 0) {
      return 1;
    }
  }

  return 0;
}

static int execute_value_expression(ArkshShell *shell, const ArkshValueSourceNode *source, char *out, size_t out_size) {
  ArkshValue *value;
  int status;

  if (shell == NULL || source == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  value = (ArkshValue *) allocate_temp_buffer(1, sizeof(*value), "value expression", out, out_size);
  if (value == NULL) {
    return 1;
  }

  if (evaluate_value_source(shell, source, value, out, out_size) != 0) {
    free(value);
    return 1;
  }

  status = arksh_value_render(value, out, out_size);
  free(value);
  return status;
}

static int evaluate_ast_value(ArkshShell *shell, const ArkshAst *ast, ArkshValue *value, char *out, size_t out_size) {
  if (shell == NULL || ast == NULL || value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  arksh_value_free(value);

  switch (ast->kind) {
    case ARKSH_AST_EMPTY:
      return 0;
    case ARKSH_AST_VALUE_EXPRESSION:
      return evaluate_value_source(shell, &ast->as.value_expression, value, out, out_size);
    case ARKSH_AST_OBJECT_EXPRESSION:
      return evaluate_object_expression_value(shell, &ast->as.object_expression, value, out, out_size);
    case ARKSH_AST_OBJECT_PIPELINE:
      return evaluate_object_pipeline_value(shell, &ast->as.pipeline, value, out, out_size);
    default:
      snprintf(out, out_size, "expression does not produce a value");
      return 1;
  }
}

/* Heap-allocated variant used by the BINARY_OP evaluator.
   ArkshAst is ~675KB (the command_pipeline union member contains 8 stages each
   with a 8KB heredoc_body buffer).  Calling arksh_evaluate_line_value
   recursively would put a fresh ArkshAst on the stack for every operand;
   for a 5-term chain that is 4 × 675KB ≈ 2.7MB which can overflow the
   default macOS thread stack.  This wrapper allocates the AST on the heap so
   the stack cost per recursion level stays small. */
static int evaluate_line_value_heap(ArkshShell *shell, const char *line, ArkshValue *value, char *out, size_t out_size) {
  ArkshAst *ast;
  char trimmed[ARKSH_MAX_LINE];
  char parse_error[ARKSH_MAX_OUTPUT];
  const ArkshValue *binding;
  int result;

  if (shell == NULL || line == NULL || value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  copy_string(trimmed, sizeof(trimmed), line);
  trim_in_place(trimmed);
  arksh_value_free(value);

  if (trimmed[0] == '\0') {
    return 0;
  }

  binding = arksh_shell_get_binding(shell, trimmed);
  if (binding != NULL) {
    return arksh_value_copy(value, binding);
  }
  if (arksh_shell_find_class(shell, trimmed) != NULL) {
    arksh_value_free(value);
    arksh_value_set_class(value, trimmed);
    return 0;
  }

  ast = (ArkshAst *) arksh_scratch_alloc_active_zero(1, sizeof(*ast));
  if (ast == NULL) {
    snprintf(out, out_size, "out of memory");
    return 1;
  }

  parse_error[0] = '\0';
  if (arksh_parse_value_line(trimmed, ast, parse_error, sizeof(parse_error)) != 0) {
    snprintf(out, out_size, "%s", parse_error[0] == '\0' ? "parse error" : parse_error);
    return 1;
  }

  result = evaluate_ast_value(shell, ast, value, out, out_size);
  return result;
}

/* Collect top-level binary operator positions from text (same scanning rules
   as find_top_level_binary_op).  Returns count of operators found, or -1 if
   the capacity 'max_ops' was exceeded. */
static int collect_top_level_binary_ops(const char *text, size_t *out_pos, char *out_op, int max_ops) {
  size_t i;
  char quote = '\0';
  int paren_depth = 0;
  int bracket_depth = 0;
  int n = 0;

  for (i = 0; text[i] != '\0'; ++i) {
    char c = text[i];

    if (quote != '\0') {
      if (c == quote) quote = '\0';
      else if (quote == '"' && c == '\\' && text[i + 1] != '\0') i++;
      continue;
    }
    if (c == '"' || c == '\'') { quote = c; continue; }
    if (c == '(') { paren_depth++; continue; }
    if (c == ')' && paren_depth > 0) { paren_depth--; continue; }
    if (c == '[') { bracket_depth++; continue; }
    if (c == ']' && bracket_depth > 0) { bracket_depth--; continue; }
    if (paren_depth != 0 || bracket_depth != 0) continue;

    if (c == '+' || c == '*' || c == '/') {
      if (n >= max_ops) return -1;
      out_pos[n] = i; out_op[n] = c; n++;
    } else if (c == '-') {
      if (text[i + 1] == '>' || text[i + 1] == '-') { i++; continue; }
      if (n >= max_ops) return -1;
      out_pos[n] = i; out_op[n] = '-'; n++;
    }
  }
  return n;
}

/* Apply a binary operator between two ArkshValues in-place (result → *lv).
   Returns 0 on success.  On error writes to out and returns 1. */
/* E6-S5: return true if this kind participates in the numeric promotion system */
static int is_numeric_kind(ArkshValueKind k) {
  return k == ARKSH_VALUE_NUMBER   ||
         k == ARKSH_VALUE_INTEGER  ||
         k == ARKSH_VALUE_FLOAT    ||
         k == ARKSH_VALUE_DOUBLE   ||
         k == ARKSH_VALUE_IMAGINARY;
}

/* E6-S5: promotion hierarchy — INTEGER < FLOAT < DOUBLE <= NUMBER; IMAGINARY wins */
static ArkshValueKind promote_kinds(ArkshValueKind a, ArkshValueKind b) {
  if (a == ARKSH_VALUE_IMAGINARY || b == ARKSH_VALUE_IMAGINARY) {
    return ARKSH_VALUE_IMAGINARY; /* handled specially by caller */
  }
  /* NUMBER is equivalent to DOUBLE */
  if (a == ARKSH_VALUE_NUMBER) a = ARKSH_VALUE_DOUBLE;
  if (b == ARKSH_VALUE_NUMBER) b = ARKSH_VALUE_DOUBLE;

  if (a == ARKSH_VALUE_DOUBLE || b == ARKSH_VALUE_DOUBLE) return ARKSH_VALUE_DOUBLE;
  if (a == ARKSH_VALUE_FLOAT  || b == ARKSH_VALUE_FLOAT)  return ARKSH_VALUE_FLOAT;
  return ARKSH_VALUE_INTEGER;
}

static int apply_binary_op(ArkshShell *shell, ArkshValue *lv, char op, ArkshValue *rv, char *out, size_t out_size) {
  if (lv->kind == ARKSH_VALUE_NUMBER && rv->kind == ARKSH_VALUE_NUMBER) {
    double result;
    switch (op) {
      case '+': result = lv->number + rv->number; break;
      case '-': result = lv->number - rv->number; break;
      case '*': result = lv->number * rv->number; break;
      case '/':
        if (rv->number == 0.0) { snprintf(out, out_size, "division by zero"); return 1; }
        result = lv->number / rv->number; break;
      default:
        snprintf(out, out_size, "unknown binary operator: %c", op); return 1;
    }
    arksh_value_free(lv);
    arksh_value_set_number(lv, result);
    return 0;
  }

  /* E6-S5: explicit numeric kinds with promotion and imaginary arithmetic */
  if (is_numeric_kind(lv->kind) && is_numeric_kind(rv->kind)) {
    ArkshValueKind lk = lv->kind, rk = rv->kind;
    double la = lv->number, ra = rv->number;
    ArkshValueKind result_kind;
    double result;

    /* ---- Imaginary special cases ---- */
    if (lk == ARKSH_VALUE_IMAGINARY && rk == ARKSH_VALUE_IMAGINARY) {
      switch (op) {
        case '+': result = la + ra; result_kind = ARKSH_VALUE_IMAGINARY; break;
        case '-': result = la - ra; result_kind = ARKSH_VALUE_IMAGINARY; break;
        case '*': result = -(la * ra); result_kind = ARKSH_VALUE_DOUBLE; break; /* i*i = -1 */
        case '/':
          if (ra == 0.0) { snprintf(out, out_size, "division by zero"); return 1; }
          result = la / ra; result_kind = ARKSH_VALUE_DOUBLE; break; /* i cancels */
        default: snprintf(out, out_size, "unknown binary operator: %c", op); return 1;
      }
    } else if (lk == ARKSH_VALUE_IMAGINARY) {
      /* Imaginary op Real */
      switch (op) {
        case '+': case '-':
          /* Real + Imaginary or Imaginary - Real: produce string "a±bi" */
          {
            char buf[128];
            if (op == '+') snprintf(buf, sizeof(buf), "%.15g+%.15gi", ra, la);
            else           snprintf(buf, sizeof(buf), "%.15g%.15gi",  la - ra, 0.0); /* uncommon */
            arksh_value_free(lv);
            arksh_value_set_string(lv, buf);
            return 0;
          }
        case '*': result = la * ra; result_kind = ARKSH_VALUE_IMAGINARY; break;
        case '/':
          if (ra == 0.0) { snprintf(out, out_size, "division by zero"); return 1; }
          result = la / ra; result_kind = ARKSH_VALUE_IMAGINARY; break;
        default: snprintf(out, out_size, "unknown binary operator: %c", op); return 1;
      }
    } else if (rk == ARKSH_VALUE_IMAGINARY) {
      /* Real op Imaginary */
      switch (op) {
        case '+': case '-':
          {
            char buf[128];
            if (op == '+') snprintf(buf, sizeof(buf), "%.15g+%.15gi", la, ra);
            else           snprintf(buf, sizeof(buf), "%.15g-%.15gi", la, ra);
            arksh_value_free(lv);
            arksh_value_set_string(lv, buf);
            return 0;
          }
        case '*': result = la * ra; result_kind = ARKSH_VALUE_IMAGINARY; break;
        case '/':
          if (ra == 0.0) { snprintf(out, out_size, "division by zero"); return 1; }
          result = -(la / ra); result_kind = ARKSH_VALUE_IMAGINARY; break; /* r/(bi) = -ri/b */
        default: snprintf(out, out_size, "unknown binary operator: %c", op); return 1;
      }
    } else {
      /* ---- Standard numeric promotion ---- */
      result_kind = promote_kinds(lk, rk);
      switch (op) {
        case '+': result = la + ra; break;
        case '-': result = la - ra; break;
        case '*': result = la * ra; break;
        case '/':
          if (ra == 0.0) { snprintf(out, out_size, "division by zero"); return 1; }
          result = la / ra; break;
        default: snprintf(out, out_size, "unknown binary operator: %c", op); return 1;
      }
    }

    arksh_value_free(lv);
    switch (result_kind) {
      case ARKSH_VALUE_INTEGER:   arksh_value_set_integer(lv, result);   break;
      case ARKSH_VALUE_FLOAT:     arksh_value_set_float(lv, result);     break;
      case ARKSH_VALUE_IMAGINARY: arksh_value_set_imaginary(lv, result); break;
      default:                    arksh_value_set_double(lv, result);    break;
    }
    return 0;
  }

  /* Extension method fallback. */
  {
    const char *method_name;
    ArkshValue *result_val;
    int status;
    switch (op) {
      case '+': method_name = "__add__"; break;
      case '-': method_name = "__sub__"; break;
      case '*': method_name = "__mul__"; break;
      case '/': method_name = "__div__"; break;
      default:
        snprintf(out, out_size, "unknown binary operator: %c", op); return 1;
    }
    result_val = (ArkshValue *) allocate_temp_buffer(1, sizeof(*result_val), "binary op extension result", out, out_size);
    if (result_val == NULL) {
      return 1;
    }
    arksh_value_init(result_val);
    status = invoke_extension_method_value(shell, lv, method_name, 1, rv, result_val, out, out_size);
    if (status != 0) { arksh_value_free(result_val); free(result_val); return 1; }
    arksh_value_free(lv);
    *lv = *result_val;
    free(result_val);
    return 0;
  }
}

/* Iterative binary expression evaluator.
   Scans 'text' for all top-level binary operators, evaluates each operand
   segment via evaluate_line_value_heap (heap-allocated parse), then applies
   operators in two passes: first '*'/'/' (higher precedence), then '+'/'-'.
   This is O(n) in the number of terms with constant recursion depth. */
static int evaluate_binary_expr_iterative(ArkshShell *shell, const char *text, ArkshValue *value, char *out, size_t out_size) {
#define MAX_BINOP_TERMS 32
  size_t op_pos[MAX_BINOP_TERMS - 1];
  char   op_chr[MAX_BINOP_TERMS - 1];
  ArkshValue *terms[MAX_BINOP_TERMS];
  char   ops[MAX_BINOP_TERMS - 1]; /* surviving operators after mul/div pass */
  int n_ops, n_terms, i, j;
  size_t prev, end;
  char seg[ARKSH_MAX_LINE];
  int status = 0;

  n_ops = collect_top_level_binary_ops(text, op_pos, op_chr, MAX_BINOP_TERMS - 1);
  if (n_ops < 0) {
    snprintf(out, out_size, "too many operators in arithmetic expression");
    return 1;
  }
  if (n_ops == 0) {
    return evaluate_line_value_heap(shell, text, value, out, out_size);
  }

  n_terms = n_ops + 1;
  for (i = 0; i < n_terms; i++) terms[i] = NULL;
  for (i = 0; i < n_ops; i++) ops[i] = op_chr[i];

  /* Evaluate each operand segment. */
  prev = 0;
  for (i = 0; i < n_terms; i++) {
    end = (i < n_ops) ? op_pos[i] : strlen(text);
    copy_trimmed_range(text, prev, end, seg, sizeof(seg));
    prev = (i < n_ops) ? op_pos[i] + 1 : end;

    terms[i] = (ArkshValue *) arksh_scratch_alloc_active_zero(1, sizeof(ArkshValue));
    if (terms[i] == NULL) {
      snprintf(out, out_size, "out of memory"); status = 1; goto cleanup;
    }
    arksh_value_init(terms[i]);
    if (evaluate_line_value_heap(shell, seg, terms[i], out, out_size) != 0) {
      status = 1; goto cleanup;
    }
  }

  /* First pass: apply * and / (higher precedence), left-to-right.
     Combine terms[j] op terms[j+1] into terms[j]; remove terms[j+1] and ops[j]. */
  j = 0;
  while (j < n_ops) {
    if (ops[j] == '*' || ops[j] == '/') {
      if (apply_binary_op(shell, terms[j], ops[j], terms[j + 1], out, out_size) != 0) {
        status = 1; goto cleanup;
      }
      arksh_value_free(terms[j + 1]); free(terms[j + 1]);
      memmove(&terms[j + 1], &terms[j + 2], (size_t)(n_terms - j - 2) * sizeof(ArkshValue *));
      memmove(&ops[j],       &ops[j + 1],   (size_t)(n_ops  - j - 1) * sizeof(char));
      n_ops--;
      n_terms--;
      /* re-visit position j in case the next op is also mul/div */
    } else {
      j++;
    }
  }

  /* Second pass: apply + and -, left-to-right. */
  while (n_ops > 0) {
    if (apply_binary_op(shell, terms[0], ops[0], terms[1], out, out_size) != 0) {
      status = 1; goto cleanup;
    }
    arksh_value_free(terms[1]); free(terms[1]);
    memmove(&terms[1], &terms[2], (size_t)(n_terms - 2) * sizeof(ArkshValue *));
    memmove(&ops[0],   &ops[1],   (size_t)(n_ops  - 1) * sizeof(char));
    n_ops--;
    n_terms--;
  }

  /* Result is terms[0]. */
  if (arksh_value_copy(value, terms[0]) != 0) {
    snprintf(out, out_size, "out of memory"); status = 1;
  }

cleanup:
  for (i = 0; i < n_terms; i++) {
    if (terms[i] != NULL) { arksh_value_free(terms[i]); free(terms[i]); terms[i] = NULL; }
  }
  return status;
#undef MAX_BINOP_TERMS
}

int arksh_evaluate_line_value(ArkshShell *shell, const char *line, ArkshValue *value, char *out, size_t out_size) {
  /* Delegate to the heap-allocated variant to avoid placing the 691 KB ArkshAst
     on the call stack.  Nested resolver evaluation (map inside list inside map…)
     would otherwise overflow the stack after just two or three levels. */
  return evaluate_line_value_heap(shell, line, value, out, out_size);
}

static int should_run_list_entry(ArkshListCondition condition, int previous_status) {
  switch (condition) {
    case ARKSH_LIST_CONDITION_ON_SUCCESS:
      return previous_status == 0;
    case ARKSH_LIST_CONDITION_ON_FAILURE:
      return previous_status != 0;
    case ARKSH_LIST_CONDITION_ALWAYS:
    default:
      return 1;
  }
}

typedef enum {
  ARKSH_LOOP_CONTROL_NONE = 0,
  ARKSH_LOOP_CONTROL_BREAK,
  ARKSH_LOOP_CONTROL_CONTINUE,
  ARKSH_LOOP_CONTROL_PROPAGATE
} ArkshLoopControlAction;

static ArkshLoopControlAction resolve_loop_control(ArkshShell *shell) {
  ArkshControlSignalKind kind;

  if (shell == NULL || shell->control_signal == ARKSH_CONTROL_SIGNAL_NONE) {
    return ARKSH_LOOP_CONTROL_NONE;
  }
  if (shell->control_signal == ARKSH_CONTROL_SIGNAL_RETURN) {
    return ARKSH_LOOP_CONTROL_PROPAGATE;
  }

  kind = shell->control_signal;
  if (shell->control_levels > 0) {
    shell->control_levels--;
  }

  if (shell->control_levels <= 0) {
    arksh_shell_clear_control_signal(shell);
    return kind == ARKSH_CONTROL_SIGNAL_BREAK ? ARKSH_LOOP_CONTROL_BREAK : ARKSH_LOOP_CONTROL_CONTINUE;
  }

  return ARKSH_LOOP_CONTROL_PROPAGATE;
}

static int execute_command_list(ArkshShell *shell, const ArkshCommandListNode *list, char *out, size_t out_size) {
  int previous_status = 0;
  size_t i;

  if (shell == NULL || list == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';

  for (i = 0; i < list->count; ++i) {
    char segment_output[ARKSH_MAX_OUTPUT];

    if (!should_run_list_entry(list->entries[i].condition, previous_status)) {
      continue;
    }

    segment_output[0] = '\0';
    if (list->entries[i].run_in_background) {
      previous_status = arksh_shell_start_background_job(shell, list->entries[i].text, segment_output, sizeof(segment_output));
    } else {
      previous_status = arksh_shell_execute_line(shell, list->entries[i].text, segment_output, sizeof(segment_output));
    }

    if (append_output_segment(out, out_size, segment_output) != 0) {
      snprintf(out, out_size, "combined command output too large");
      return 1;
    }

    if (!shell->running) {
      break;
    }
    if (shell->control_signal != ARKSH_CONTROL_SIGNAL_NONE) {
      break;
    }
  }

  return previous_status;
}

static int render_value_for_shell_var(const ArkshValue *value, char *out, size_t out_size) {
  if (value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  switch (value->kind) {
    case ARKSH_VALUE_STRING:
      copy_string(out, out_size, value->text);
      return 0;
    case ARKSH_VALUE_NUMBER:
      snprintf(out, out_size, "%.15g", value->number);
      return 0;
    case ARKSH_VALUE_BOOLEAN:
      copy_string(out, out_size, value->boolean ? "true" : "false");
      return 0;
    case ARKSH_VALUE_OBJECT:
      copy_string(out, out_size, value->object.path);
      return 0;
    case ARKSH_VALUE_BLOCK:
      copy_string(out, out_size, value->block.source);
      return 0;
    case ARKSH_VALUE_LIST:
    case ARKSH_VALUE_EMPTY:
    default:
      return arksh_value_render(value, out, out_size);
  }
}

static int evaluate_condition_status(ArkshShell *shell, const char *text, int *out_status, char *out, size_t out_size) {
  ArkshValue *value;
  int status;

  if (shell == NULL || text == NULL || out_status == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  value = (ArkshValue *) allocate_temp_buffer(1, sizeof(*value), "condition value", out, out_size);
  if (value == NULL) {
    return 1;
  }

  status = arksh_evaluate_line_value(shell, text, value, out, out_size);
  if (status == 0) {
    *out_status = value_is_truthy(value) ? 0 : 1;
    free(value);
    out[0] = '\0';
    return 0;
  }

  free(value);
  out[0] = '\0';
  *out_status = arksh_shell_execute_line(shell, text, out, out_size);
  return 0;
}

static int evaluate_switch_operand(ArkshShell *shell, const char *text, ArkshValue *out_value, char *out, size_t out_size) {
  char expanded[ARKSH_MAX_OUTPUT];
  const char *p;
  int has_paren_before_space = 0;

  if (shell == NULL || text == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  /* E1-S7-T5: determine whether the case expression is an arksh resolver call
     (e.g. text("directory"), list(...)) or a plain shell word (e.g. "hello",
     "$var").  Resolver calls have '(' before any whitespace; everything else
     is expanded as a POSIX shell word so that bare identifiers like "hello"
     remain the literal string "hello" rather than being resolved as variable
     bindings (ARKSH_VALUE_SOURCE_BINDING). */
  for (p = text; *p != '\0' && !isspace((unsigned char) *p); p++) {
    if (*p == '(') {
      has_paren_before_space = 1;
      break;
    }
  }

  if (has_paren_before_space) {
    return evaluate_expression_atom(shell, text, out_value, out, out_size);
  }

  /* Plain shell word: expand $var, $((arith)), $(cmd), ~, but no IFS
     splitting or glob expansion (per POSIX §2.13). */
  if (expand_single_word(shell, text, ARKSH_EXPAND_MODE_COMMAND_NAME, expanded, sizeof(expanded), out, out_size) != 0) {
    return evaluate_expression_atom(shell, text, out_value, out, out_size);
  }

  arksh_value_set_string(out_value, expanded);
  return 0;
}

static int case_char_class_matches(const char *pattern, size_t *index, char text_char) {
  int matched = 0;
  int negate = 0;
  size_t cursor;

  if (pattern == NULL || index == NULL || pattern[*index] != '[') {
    return 0;
  }

  cursor = *index + 1;
  if (pattern[cursor] == '!' || pattern[cursor] == '^') {
    negate = 1;
    cursor++;
  }

  while (pattern[cursor] != '\0' && pattern[cursor] != ']') {
    char start = pattern[cursor];
    char end = start;

    if (start == '\\' && pattern[cursor + 1] != '\0') {
      cursor++;
      start = pattern[cursor];
      end = start;
    }

    if (pattern[cursor + 1] == '-' && pattern[cursor + 2] != '\0' && pattern[cursor + 2] != ']') {
      cursor += 2;
      end = pattern[cursor];
      if (end == '\\' && pattern[cursor + 1] != '\0') {
        cursor++;
        end = pattern[cursor];
      }
    }

    if (text_char >= start && text_char <= end) {
      matched = 1;
    }
    cursor++;
  }

  if (pattern[cursor] != ']') {
    return 0;
  }

  *index = cursor + 1;
  return negate ? !matched : matched;
}

static int shell_pattern_matches(const char *pattern, const char *text) {
  size_t pattern_index = 0;
  size_t text_index = 0;

  if (pattern == NULL || text == NULL) {
    return 0;
  }

  while (pattern[pattern_index] != '\0') {
    char current = pattern[pattern_index];

    if (current == '*') {
      while (pattern[pattern_index] == '*') {
        pattern_index++;
      }
      if (pattern[pattern_index] == '\0') {
        return 1;
      }
      while (1) {
        if (shell_pattern_matches(pattern + pattern_index, text + text_index)) {
          return 1;
        }
        if (text[text_index] == '\0') {
          return 0;
        }
        text_index++;
      }
    }

    if (text[text_index] == '\0') {
      return 0;
    }

    if (current == '?') {
      pattern_index++;
      text_index++;
      continue;
    }

    if (current == '[') {
      size_t class_index = pattern_index;

      if (!case_char_class_matches(pattern, &class_index, text[text_index])) {
        return 0;
      }
      pattern_index = class_index;
      text_index++;
      continue;
    }

    if (current == '\\' && pattern[pattern_index + 1] != '\0') {
      pattern_index++;
      current = pattern[pattern_index];
    }

    if (current != text[text_index]) {
      return 0;
    }

    pattern_index++;
    text_index++;
  }

  return text[text_index] == '\0';
}

static int split_case_pattern_alternatives(
  const char *patterns_text,
  char patterns[][ARKSH_MAX_LINE],
  size_t max_patterns,
  size_t *out_count
) {
  ArkshTokenStream stream;
  char error[ARKSH_MAX_OUTPUT];
  size_t segment_start = 0;
  size_t count = 0;
  size_t i;

  if (patterns_text == NULL || patterns == NULL || max_patterns == 0 || out_count == NULL) {
    return 1;
  }

  *out_count = 0;
  error[0] = '\0';
  if (arksh_lex_line(patterns_text, &stream, error, sizeof(error)) != 0) {
    copy_trimmed_range(patterns_text, 0, strlen(patterns_text), patterns[0], ARKSH_MAX_LINE);
    strip_matching_quotes(patterns[0]);
    *out_count = patterns[0][0] == '\0' ? 0 : 1;
    return 0;
  }

  for (i = 0; i < stream.count; ++i) {
    if (stream.tokens[i].kind == ARKSH_TOKEN_SHELL_PIPE || stream.tokens[i].kind == ARKSH_TOKEN_EOF) {
      if (count >= max_patterns) {
        return 1;
      }

      copy_trimmed_range(
        patterns_text,
        segment_start,
        stream.tokens[i].kind == ARKSH_TOKEN_EOF ? strlen(patterns_text) : stream.tokens[i].position,
        patterns[count],
        ARKSH_MAX_LINE
      );
      strip_matching_quotes(patterns[count]);
      if (patterns[count][0] != '\0') {
        count++;
      }

      if (stream.tokens[i].kind == ARKSH_TOKEN_EOF) {
        break;
      }
      segment_start = stream.tokens[i].position + strlen(stream.tokens[i].raw);
    }
  }

  *out_count = count;
  return 0;
}

static int case_branch_matches(const char *patterns_text, const char *switch_text) {
  char patterns[ARKSH_MAX_CASE_BRANCHES][ARKSH_MAX_LINE];
  size_t pattern_count = 0;
  size_t i;

  if (patterns_text == NULL || switch_text == NULL) {
    return 0;
  }
  if (split_case_pattern_alternatives(patterns_text, patterns, ARKSH_MAX_CASE_BRANCHES, &pattern_count) != 0) {
    return 0;
  }

  for (i = 0; i < pattern_count; ++i) {
    if (shell_pattern_matches(patterns[i], switch_text)) {
      return 1;
    }
  }

  return 0;
}

static int execute_if_command(ArkshShell *shell, const ArkshIfCommandNode *command, char *out, size_t out_size) {
  char segment_output[ARKSH_MAX_OUTPUT];
  int condition_status;
  int branch_status = 0;

  if (shell == NULL || command == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  segment_output[0] = '\0';
  shell->in_condition++;
  if (evaluate_condition_status(shell, command->condition, &condition_status, segment_output, sizeof(segment_output)) != 0) {
    shell->in_condition--;
    return 1;
  }
  shell->in_condition--;
  if (append_output_segment(out, out_size, segment_output) != 0) {
    snprintf(out, out_size, "combined command output too large");
    return 1;
  }

  if (!shell->running) {
    return condition_status;
  }
  if (shell->control_signal != ARKSH_CONTROL_SIGNAL_NONE) {
    return condition_status;
  }

  if (condition_status == 0) {
    segment_output[0] = '\0';
    branch_status = arksh_shell_execute_line(shell, command->then_branch, segment_output, sizeof(segment_output));
    if (append_output_segment(out, out_size, segment_output) != 0) {
      snprintf(out, out_size, "combined command output too large");
      return 1;
    }
    return branch_status;
  }

  if (command->has_else_branch) {
    segment_output[0] = '\0';
    branch_status = arksh_shell_execute_line(shell, command->else_branch, segment_output, sizeof(segment_output));
    if (append_output_segment(out, out_size, segment_output) != 0) {
      snprintf(out, out_size, "combined command output too large");
      return 1;
    }
    return branch_status;
  }

  return 0;
}

static int execute_while_command(ArkshShell *shell, const ArkshWhileCommandNode *command, char *out, size_t out_size) {
  int last_body_status = 0;
  int final_status = 0;

  if (shell == NULL || command == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  shell->loop_depth++;
  while (shell->running) {
    char segment_output[ARKSH_MAX_OUTPUT];
    int condition_status;

    segment_output[0] = '\0';
    shell->in_condition++;
    if (evaluate_condition_status(shell, command->condition, &condition_status, segment_output, sizeof(segment_output)) != 0) {
      shell->in_condition--;
      shell->loop_depth--;
      return 1;
    }
    shell->in_condition--;
    if (append_output_segment(out, out_size, segment_output) != 0) {
      snprintf(out, out_size, "combined command output too large");
      return 1;
    }
    if (!shell->running) {
      final_status = condition_status;
      goto done;
    }
    if (shell->control_signal != ARKSH_CONTROL_SIGNAL_NONE) {
      final_status = condition_status;
      goto done;
    }
    if (condition_status != 0) {
      final_status = last_body_status;
      goto done;
    }

    segment_output[0] = '\0';
    last_body_status = arksh_shell_execute_line(shell, command->body, segment_output, sizeof(segment_output));
    if (append_output_segment(out, out_size, segment_output) != 0) {
      shell->loop_depth--;
      snprintf(out, out_size, "combined command output too large");
      return 1;
    }

    switch (resolve_loop_control(shell)) {
      case ARKSH_LOOP_CONTROL_BREAK:
        final_status = 0;
        goto done;
      case ARKSH_LOOP_CONTROL_CONTINUE:
        last_body_status = 0;
        continue;
      case ARKSH_LOOP_CONTROL_PROPAGATE:
        final_status = last_body_status;
        goto done;
      case ARKSH_LOOP_CONTROL_NONE:
      default:
        break;
    }
  }

  final_status = last_body_status;

done:
  shell->loop_depth--;
  return final_status;
}

static int execute_until_command(ArkshShell *shell, const ArkshUntilCommandNode *command, char *out, size_t out_size) {
  int last_body_status = 0;
  int final_status = 0;

  if (shell == NULL || command == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  shell->loop_depth++;
  while (shell->running) {
    char segment_output[ARKSH_MAX_OUTPUT];
    int condition_status;

    segment_output[0] = '\0';
    shell->in_condition++;
    if (evaluate_condition_status(shell, command->condition, &condition_status, segment_output, sizeof(segment_output)) != 0) {
      shell->in_condition--;
      shell->loop_depth--;
      return 1;
    }
    shell->in_condition--;
    if (append_output_segment(out, out_size, segment_output) != 0) {
      snprintf(out, out_size, "combined command output too large");
      return 1;
    }
    if (!shell->running) {
      final_status = condition_status;
      goto done;
    }
    if (shell->control_signal != ARKSH_CONTROL_SIGNAL_NONE) {
      final_status = condition_status;
      goto done;
    }
    if (condition_status == 0) {
      final_status = last_body_status;
      goto done;
    }

    segment_output[0] = '\0';
    last_body_status = arksh_shell_execute_line(shell, command->body, segment_output, sizeof(segment_output));
    if (append_output_segment(out, out_size, segment_output) != 0) {
      shell->loop_depth--;
      snprintf(out, out_size, "combined command output too large");
      return 1;
    }

    switch (resolve_loop_control(shell)) {
      case ARKSH_LOOP_CONTROL_BREAK:
        final_status = 0;
        goto done;
      case ARKSH_LOOP_CONTROL_CONTINUE:
        last_body_status = 0;
        continue;
      case ARKSH_LOOP_CONTROL_PROPAGATE:
        final_status = last_body_status;
        goto done;
      case ARKSH_LOOP_CONTROL_NONE:
      default:
        break;
    }
  }

  final_status = last_body_status;

done:
  shell->loop_depth--;
  return final_status;
}

static int append_for_source_word(
  ArkshShell *shell,
  const char *raw_word,
  ArkshValue *iterable,
  char *out,
  size_t out_size
) {
  char expanded_words[ARKSH_MAX_ARGS][ARKSH_MAX_TOKEN];
  int expanded_count = 0;
  int i;

  if (shell == NULL || raw_word == NULL || iterable == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (arksh_expand_word(shell, raw_word, ARKSH_EXPAND_MODE_COMMAND, expanded_words, ARKSH_MAX_ARGS, &expanded_count, out, out_size) != 0) {
    return 1;
  }

  for (i = 0; i < expanded_count; ++i) {
    ArkshValueItem *item = (ArkshValueItem *) allocate_temp_buffer(1, sizeof(*item), "for-loop source item", out, out_size);

    if (item == NULL) {
      return 1;
    }
    if (set_item_from_text(expanded_words[i], item) != 0 || arksh_value_list_append_item(iterable, item) != 0) {
      arksh_value_item_free(item);
      free(item);
      snprintf(out, out_size, "unable to append for-loop item");
      return 1;
    }
    arksh_value_item_free(item);
    free(item);
  }

  return 0;
}

static int evaluate_for_source(ArkshShell *shell, const char *text, ArkshValue *iterable, char *out, size_t out_size) {
  ArkshValue *value;
  int status;

  if (shell == NULL || text == NULL || iterable == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  value = (ArkshValue *) allocate_temp_buffer(1, sizeof(*value), "for-loop source", out, out_size);
  if (value == NULL) {
    return 1;
  }

  status = arksh_evaluate_line_value(shell, text, value, out, out_size);
  if (status == 0) {
    *iterable = *value;
    free(value);
    return 0;
  }

  free(value);
  out[0] = '\0';

  {
    ArkshTokenStream stream;
    char parse_error[ARKSH_MAX_OUTPUT];
    size_t i;

    if (arksh_lex_line(text, &stream, parse_error, sizeof(parse_error)) != 0) {
      snprintf(out, out_size, "%s", parse_error[0] == '\0' ? "invalid for-loop source" : parse_error);
      return 1;
    }

    arksh_value_init(iterable);
    iterable->kind = ARKSH_VALUE_LIST;
    for (i = 0; i < stream.count; ++i) {
      if (stream.tokens[i].kind == ARKSH_TOKEN_EOF) {
        break;
      }
      if (!is_value_token_kind(stream.tokens[i].kind)) {
        snprintf(out, out_size, "for-loop source only supports value expressions or shell words");
        return 1;
      }
      if (append_for_source_word(shell, stream.tokens[i].raw, iterable, out, out_size) != 0) {
        return 1;
      }
    }
  }

  return 0;
}

static int assign_for_loop_variable(ArkshShell *shell, const char *name, const ArkshValue *value, char *out, size_t out_size) {
  char rendered[ARKSH_MAX_OUTPUT];

  if (shell == NULL || name == NULL || value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (arksh_shell_set_binding(shell, name, value) != 0) {
    snprintf(out, out_size, "unable to bind for-loop variable: %s", name);
    return 1;
  }

  if (render_value_for_shell_var(value, rendered, sizeof(rendered)) != 0) {
    snprintf(out, out_size, "unable to render for-loop variable: %s", name);
    return 1;
  }

  if (arksh_shell_set_var(shell, name, rendered, 0) != 0) {
    snprintf(out, out_size, "unable to assign for-loop variable: %s", name);
    return 1;
  }

  return 0;
}

static int execute_for_command(ArkshShell *shell, const ArkshForCommandNode *command, char *out, size_t out_size) {
  ArkshValue *iterable;
  int last_body_status = 0;
  int final_status = 0;
  size_t i;

  if (shell == NULL || command == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  iterable = (ArkshValue *) allocate_temp_buffer(1, sizeof(*iterable), "for-loop iterable", out, out_size);
  if (iterable == NULL) {
    return 1;
  }

  if (evaluate_for_source(shell, command->source, iterable, out, out_size) != 0) {
    free(iterable);
    return 1;
  }

  out[0] = '\0';
  shell->loop_depth++;
  if (iterable->kind == ARKSH_VALUE_LIST) {
    for (i = 0; i < iterable->list.count && shell->running; ++i) {
      ArkshValue *current_value;
      char segment_output[ARKSH_MAX_OUTPUT];

      current_value = (ArkshValue *) allocate_temp_buffer(1, sizeof(*current_value), "for-loop current value", out, out_size);
      if (current_value == NULL) {
        shell->loop_depth--;
        free(iterable);
        return 1;
      }
      if (arksh_value_set_from_item(current_value, &iterable->list.items[i]) != 0) {
        free(current_value);
        free(iterable);
        snprintf(out, out_size, "unable to prepare for-loop item");
        return 1;
      }
      if (assign_for_loop_variable(shell, command->variable, current_value, out, out_size) != 0) {
        arksh_value_free(current_value);
        free(current_value);
        shell->loop_depth--;
        free(iterable);
        return 1;
      }

      segment_output[0] = '\0';
      last_body_status = arksh_shell_execute_line(shell, command->body, segment_output, sizeof(segment_output));
      if (append_output_segment(out, out_size, segment_output) != 0) {
        arksh_value_free(current_value);
        free(current_value);
        shell->loop_depth--;
        free(iterable);
        snprintf(out, out_size, "combined command output too large");
        return 1;
      }

      switch (resolve_loop_control(shell)) {
        case ARKSH_LOOP_CONTROL_BREAK:
          final_status = 0;
          arksh_value_free(current_value);
          free(current_value);
          free(iterable);
          shell->loop_depth--;
          return final_status;
        case ARKSH_LOOP_CONTROL_CONTINUE:
          arksh_value_free(current_value);
          free(current_value);
          last_body_status = 0;
          continue;
        case ARKSH_LOOP_CONTROL_PROPAGATE:
          final_status = last_body_status;
          arksh_value_free(current_value);
          free(current_value);
          free(iterable);
          shell->loop_depth--;
          return final_status;
        case ARKSH_LOOP_CONTROL_NONE:
        default:
          break;
      }
      arksh_value_free(current_value);
      free(current_value);
    }
  } else if (iterable->kind != ARKSH_VALUE_EMPTY) {
    char segment_output[ARKSH_MAX_OUTPUT];

    if (assign_for_loop_variable(shell, command->variable, iterable, out, out_size) != 0) {
      shell->loop_depth--;
      free(iterable);
      return 1;
    }

    segment_output[0] = '\0';
    last_body_status = arksh_shell_execute_line(shell, command->body, segment_output, sizeof(segment_output));
    if (append_output_segment(out, out_size, segment_output) != 0) {
      shell->loop_depth--;
      free(iterable);
      snprintf(out, out_size, "combined command output too large");
      return 1;
    }

    switch (resolve_loop_control(shell)) {
      case ARKSH_LOOP_CONTROL_BREAK:
      case ARKSH_LOOP_CONTROL_CONTINUE:
        last_body_status = 0;
        break;
      case ARKSH_LOOP_CONTROL_PROPAGATE:
        shell->loop_depth--;
        free(iterable);
        return last_body_status;
      case ARKSH_LOOP_CONTROL_NONE:
      default:
        break;
    }
  }

  shell->loop_depth--;
  free(iterable);
  return last_body_status;
}

static int execute_switch_command(ArkshShell *shell, const ArkshSwitchCommandNode *command, char *out, size_t out_size) {
  ArkshValue *switch_value;
  size_t i;

  if (shell == NULL || command == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  switch_value = (ArkshValue *) allocate_temp_buffer(1, sizeof(*switch_value), "switch value", out, out_size);
  if (switch_value == NULL) {
    return 1;
  }

  if (evaluate_switch_operand(shell, command->expression, switch_value, out, out_size) != 0) {
    arksh_value_free(switch_value);
    free(switch_value);
    return 1;
  }

  out[0] = '\0';
  for (i = 0; i < command->case_count; ++i) {
    ArkshValue *case_value;
    int matches = 0;

    case_value = (ArkshValue *) allocate_temp_buffer(1, sizeof(*case_value), "switch case value", out, out_size);
    if (case_value == NULL) {
      free(switch_value);
      return 1;
    }

    if (evaluate_switch_operand(shell, command->cases[i].match, case_value, out, out_size) != 0) {
      arksh_value_free(case_value);
      free(case_value);
      arksh_value_free(switch_value);
      free(switch_value);
      return 1;
    }

    if (compare_rendered_values(switch_value, case_value, &matches, out, out_size) != 0) {
      arksh_value_free(case_value);
      free(case_value);
      arksh_value_free(switch_value);
      free(switch_value);
      return 1;
    }

    arksh_value_free(case_value);
    free(case_value);
    if (matches) {
      int status;

      status = arksh_shell_execute_line(shell, command->cases[i].body, out, out_size);
      arksh_value_free(switch_value);
      free(switch_value);
      return status;
    }
  }

  arksh_value_free(switch_value);
  free(switch_value);
  if (command->has_default_branch) {
    return arksh_shell_execute_line(shell, command->default_branch, out, out_size);
  }

  return 0;
}

static int execute_case_command(ArkshShell *shell, const ArkshCaseCommandNode *command, char *out, size_t out_size) {
  ArkshValue *case_value;
  char rendered[ARKSH_MAX_OUTPUT];
  size_t i;

  if (shell == NULL || command == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  case_value = (ArkshValue *) allocate_temp_buffer(1, sizeof(*case_value), "case value", out, out_size);
  if (case_value == NULL) {
    return 1;
  }

  if (evaluate_switch_operand(shell, command->expression, case_value, out, out_size) != 0) {
    arksh_value_free(case_value);
    free(case_value);
    return 1;
  }
  if (arksh_value_render(case_value, rendered, sizeof(rendered)) != 0) {
    arksh_value_free(case_value);
    free(case_value);
    snprintf(out, out_size, "unable to render case expression");
    return 1;
  }

  arksh_value_free(case_value);
  free(case_value);
  out[0] = '\0';

  for (i = 0; i < command->branch_count; ++i) {
    if (case_branch_matches(command->branches[i].patterns, rendered)) {
      if (command->branches[i].body[0] == '\0') {
        return 0;
      }
      return arksh_shell_execute_line(shell, command->branches[i].body, out, out_size);
    }
  }

  return 0;
}

int arksh_execute_ast(ArkshShell *shell, const ArkshAst *ast, char *out, size_t out_size) {
  ArkshScratchFrame scratch_frame;
  int status;

  if (shell == NULL || ast == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  arksh_scratch_frame_begin(shell, &scratch_frame);
  out[0] = '\0';

  switch (ast->kind) {
    case ARKSH_AST_EMPTY:
      status = 0;
      break;
    case ARKSH_AST_SIMPLE_COMMAND:
      status = execute_simple_command(shell, &ast->as.command, out, out_size);
      break;
    case ARKSH_AST_VALUE_EXPRESSION:
      status = execute_value_expression(shell, &ast->as.value_expression, out, out_size);
      break;
    case ARKSH_AST_OBJECT_EXPRESSION:
      status = evaluate_object_expression_text(shell, &ast->as.object_expression, out, out_size);
      break;
    case ARKSH_AST_OBJECT_PIPELINE:
      status = execute_object_pipeline(shell, &ast->as.pipeline, out, out_size);
      break;
    case ARKSH_AST_COMMAND_PIPELINE:
      status = execute_shell_pipeline(shell, &ast->as.command_pipeline, out, out_size);
      break;
    case ARKSH_AST_COMMAND_LIST:
      status = execute_command_list(shell, &ast->as.command_list, out, out_size);
      break;
    case ARKSH_AST_GROUP_COMMAND:
      status = execute_group_command(shell, &ast->as.group_command, out, out_size);
      break;
    case ARKSH_AST_SUBSHELL_COMMAND:
      status = execute_subshell_command(shell, &ast->as.subshell_command, out, out_size);
      break;
    case ARKSH_AST_IF_COMMAND:
      status = execute_if_command(shell, &ast->as.if_command, out, out_size);
      break;
    case ARKSH_AST_WHILE_COMMAND:
      status = execute_while_command(shell, &ast->as.while_command, out, out_size);
      break;
    case ARKSH_AST_UNTIL_COMMAND:
      status = execute_until_command(shell, &ast->as.until_command, out, out_size);
      break;
    case ARKSH_AST_FOR_COMMAND:
      status = execute_for_command(shell, &ast->as.for_command, out, out_size);
      break;
    case ARKSH_AST_CASE_COMMAND:
      status = execute_case_command(shell, &ast->as.case_command, out, out_size);
      break;
    case ARKSH_AST_SWITCH_COMMAND:
      status = execute_switch_command(shell, &ast->as.switch_command, out, out_size);
      break;
    case ARKSH_AST_FUNCTION_COMMAND:
      status = execute_function_definition(shell, &ast->as.function_command, out, out_size);
      break;
    case ARKSH_AST_CLASS_COMMAND:
      status = execute_class_definition(shell, &ast->as.class_command, out, out_size);
      break;
    default:
      snprintf(out, out_size, "unsupported AST node kind");
      status = 1;
      break;
  }

  arksh_scratch_frame_end(&scratch_frame);
  return status;
}
