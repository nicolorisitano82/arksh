#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oosh/expand.h"
#include "oosh/executor.h"
#include "oosh/lexer.h"
#include "oosh/object.h"
#include "oosh/parser.h"
#include "oosh/platform.h"
#include "oosh/shell.h"

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
  char buffer[OOSH_MAX_LINE];
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

static void build_command_argv(const char argv_src[OOSH_MAX_ARGS][OOSH_MAX_TOKEN], int argc, char *argv_out[OOSH_MAX_ARGS]) {
  int i;

  for (i = 0; i < OOSH_MAX_ARGS; ++i) {
    argv_out[i] = NULL;
  }

  for (i = 0; i < argc && i < OOSH_MAX_ARGS; ++i) {
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

  buffer = calloc(count, item_size);
  if (buffer == NULL && out != NULL && out_size > 0) {
    snprintf(out, out_size, "unable to allocate %s", label == NULL ? "temporary buffer" : label);
  }
  return buffer;
}

static int expand_single_word(
  OoshShell *shell,
  const char *raw,
  OoshExpandMode mode,
  char *out,
  size_t out_size,
  char *error,
  size_t error_size
) {
  char values[1][OOSH_MAX_TOKEN];
  int count = 0;

  if (out == NULL || out_size == 0 || error == NULL || error_size == 0) {
    return 1;
  }

  if (oosh_expand_word(shell, raw, mode, values, 1, &count, error, error_size) != 0) {
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
  OoshShell *shell,
  const char raw_argv[OOSH_MAX_ARGS][OOSH_MAX_TOKEN],
  int argc,
  char expanded_argv[OOSH_MAX_ARGS][OOSH_MAX_TOKEN],
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
    char values[OOSH_MAX_ARGS][OOSH_MAX_TOKEN];
    int value_count = 0;
    OoshExpandMode mode = i == 0 ? OOSH_EXPAND_MODE_COMMAND_NAME : OOSH_EXPAND_MODE_COMMAND;

    if (oosh_expand_word(shell, raw_argv[i], mode, values, OOSH_MAX_ARGS, &value_count, error, error_size) != 0) {
      return 1;
    }

    if (expanded_count + value_count > OOSH_MAX_ARGS) {
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

static int write_text_file(const char *path, const char *text, int append, char *out, size_t out_size) {
  char platform_error[OOSH_MAX_OUTPUT];

  if (path == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (oosh_platform_write_text_file(path, text, append, platform_error, sizeof(platform_error)) != 0) {
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
  char left[OOSH_MAX_NAME];
  char right[OOSH_MAX_TOKEN];

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
  OoshMemberKind member_kind;
  char name[OOSH_MAX_NAME];
  char argv[OOSH_MAX_ARGS][OOSH_MAX_TOKEN];
  char raw_argv[OOSH_MAX_ARGS][OOSH_MAX_TOKEN];
  int argc;
} OoshEachSelector;

typedef struct {
  int existed;
  char name[OOSH_MAX_NAME];
  OoshValue previous;
} OoshBlockBindingSnapshot;

typedef struct {
  OoshShellVar vars[OOSH_MAX_SHELL_VARS];
  size_t var_count;
  OoshValueBinding bindings[OOSH_MAX_VALUE_BINDINGS];
  size_t binding_count;
  char positional_params[OOSH_MAX_POSITIONAL_PARAMS][OOSH_MAX_VAR_VALUE];
  int positional_count;
} OoshFunctionScopeSnapshot;

typedef struct {
  int running;
  int last_status;
  int next_instance_id;
  int next_job_id;
  int loading_plugin_index;
  char cwd[OOSH_MAX_PATH];
  OoshPromptConfig prompt;
  OoshShellVar vars[OOSH_MAX_SHELL_VARS];
  size_t var_count;
  OoshValueBinding bindings[OOSH_MAX_VALUE_BINDINGS];
  size_t binding_count;
  OoshAlias aliases[OOSH_MAX_ALIASES];
  size_t alias_count;
  OoshShellFunction functions[OOSH_MAX_FUNCTIONS];
  size_t function_count;
  OoshClassDef classes[OOSH_MAX_CLASSES];
  size_t class_count;
  OoshClassInstance instances[OOSH_MAX_INSTANCES];
  size_t instance_count;
  OoshObjectExtension extensions[OOSH_MAX_EXTENSIONS];
  size_t extension_count;
  OoshValueResolverDef value_resolvers[OOSH_MAX_VALUE_RESOLVERS];
  size_t value_resolver_count;
  OoshPipelineStageDef pipeline_stages[OOSH_MAX_PIPELINE_STAGE_HANDLERS];
  size_t pipeline_stage_count;
  OoshCommandDef commands[OOSH_MAX_COMMANDS];
  size_t command_count;
  OoshLoadedPlugin plugins[OOSH_MAX_PLUGINS];
  size_t plugin_count;
  OoshJob jobs[OOSH_MAX_JOBS];
  size_t job_count;
} OoshShellStateSnapshot;

#define OOSH_MAX_BLOCK_LOCALS OOSH_MAX_ARGS

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

static int is_value_token_kind(OoshTokenKind kind) {
  return kind == OOSH_TOKEN_WORD || kind == OOSH_TOKEN_STRING;
}

static int set_item_from_text(const char *text, OoshValueItem *out_item) {
  double number;

  if (text == NULL || out_item == NULL) {
    return 1;
  }

  oosh_value_item_init(out_item);
  if (parse_number_text(text, &number) == 0) {
    out_item->kind = OOSH_VALUE_NUMBER;
    out_item->number = number;
    return 0;
  }

  if (strcmp(text, "true") == 0 || strcmp(text, "false") == 0) {
    out_item->kind = OOSH_VALUE_BOOLEAN;
    out_item->boolean = strcmp(text, "true") == 0;
    return 0;
  }

  out_item->kind = OOSH_VALUE_STRING;
  copy_string(out_item->text, sizeof(out_item->text), text);
  return 0;
}

static int set_item_from_value(const OoshValue *value, OoshValueItem *out_item, char *out, size_t out_size) {
  if (value == NULL || out_item == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (oosh_value_item_set_from_value(out_item, value) != 0) {
    snprintf(out, out_size, "unable to convert value to list item");
    return 1;
  }

  return 0;
}

static int evaluate_token_argument_value(OoshShell *shell, const char *raw_text, OoshValue *out_value, char *out, size_t out_size) {
  const OoshValue *binding_value;
  char expanded[OOSH_MAX_TOKEN];
  OoshObject object;
  double number;

  if (shell == NULL || raw_text == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  binding_value = oosh_shell_get_binding(shell, raw_text);
  if (binding_value != NULL) {
    return oosh_value_copy(out_value, binding_value);
  }

  {
    char nested_error[OOSH_MAX_OUTPUT];

    nested_error[0] = '\0';
    if (oosh_evaluate_line_value(shell, raw_text, out_value, nested_error, sizeof(nested_error)) == 0) {
      return 0;
    }
  }

  if (expand_single_word(shell, raw_text, OOSH_EXPAND_MODE_OBJECT_ARGUMENT, expanded, sizeof(expanded), out, out_size) != 0) {
    return 1;
  }

  if (parse_number_text(expanded, &number) == 0) {
    oosh_value_set_number(out_value, number);
    return 0;
  }

  if (strcmp(expanded, "true") == 0 || strcmp(expanded, "false") == 0) {
    oosh_value_set_boolean(out_value, strcmp(expanded, "true") == 0);
    return 0;
  }

  if (oosh_object_resolve(shell->cwd, expanded, &object) == 0) {
    oosh_value_set_object(out_value, &object);
    return 0;
  }

  oosh_value_set_string(out_value, expanded);
  return 0;
}

static int value_is_truthy(const OoshValue *value) {
  if (value == NULL) {
    return 0;
  }

  switch (value->kind) {
    case OOSH_VALUE_BOOLEAN:
      return value->boolean != 0;
    case OOSH_VALUE_NUMBER:
      return value->number != 0.0;
    case OOSH_VALUE_STRING:
      return value->text[0] != '\0' && strcmp(value->text, "false") != 0 && strcmp(value->text, "0") != 0;
    case OOSH_VALUE_OBJECT:
    case OOSH_VALUE_BLOCK:
      return 1;
    case OOSH_VALUE_LIST:
      return value->list.count > 0;
    case OOSH_VALUE_EMPTY:
    default:
      return 0;
  }
}

static int compare_rendered_values(const OoshValue *left, const OoshValue *right, int *out_equal, char *out, size_t out_size) {
  char left_rendered[OOSH_MAX_OUTPUT];
  char right_rendered[OOSH_MAX_OUTPUT];

  if (left == NULL || right == NULL || out_equal == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (oosh_value_render(left, left_rendered, sizeof(left_rendered)) != 0 ||
      oosh_value_render(right, right_rendered, sizeof(right_rendered)) != 0) {
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

static int split_top_level_arguments(const char *text, char parts[][OOSH_MAX_LINE], int max_parts, int *out_count) {
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
      copy_trimmed_range(text, start, i, parts[count], OOSH_MAX_LINE);
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
  char trimmed[OOSH_MAX_LINE];
  char left[OOSH_MAX_LINE];
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

static int add_values(const OoshValue *left, const OoshValue *right, OoshValue *out_value, char *out, size_t out_size) {
  char left_text[OOSH_MAX_OUTPUT];
  char right_text[OOSH_MAX_OUTPUT];

  if (left == NULL || right == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (left->kind == OOSH_VALUE_NUMBER && right->kind == OOSH_VALUE_NUMBER) {
    oosh_value_set_number(out_value, left->number + right->number);
    return 0;
  }

  if (oosh_value_render(left, left_text, sizeof(left_text)) != 0 || oosh_value_render(right, right_text, sizeof(right_text)) != 0) {
    snprintf(out, out_size, "unable to evaluate '+' expression");
    return 1;
  }

  if (snprintf(out_value->text, sizeof(out_value->text), "%s%s", left_text, right_text) >= (int) sizeof(out_value->text)) {
    snprintf(out, out_size, "string result is too large");
    return 1;
  }
  out_value->kind = OOSH_VALUE_STRING;
  return 0;
}

static int evaluate_additive_expression(OoshShell *shell, const char *text, OoshValue *out_value, char *out, size_t out_size) {
  size_t positions[OOSH_MAX_ARGS];
  size_t plus_count = 0;
  size_t segment_start = 0;
  size_t i;
  OoshValue *temp_values;

  if (shell == NULL || text == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (find_top_level_plus_positions(text, positions, OOSH_MAX_ARGS, &plus_count) != 0 || plus_count == 0) {
    return 1;
  }

  temp_values = (OoshValue *) allocate_temp_buffer(2, sizeof(*temp_values), "additive expression values", out, out_size);
  if (temp_values == NULL) {
    return 1;
  }

  for (i = 0; i <= plus_count; ++i) {
    char segment[OOSH_MAX_LINE];
    size_t segment_end = i < plus_count ? positions[i] : strlen(text);

    copy_trimmed_range(text, segment_start, segment_end, segment, sizeof(segment));
    if (segment[0] == '\0') {
      free(temp_values);
      snprintf(out, out_size, "invalid '+' expression");
      return 1;
    }

    if (oosh_evaluate_line_value(shell, segment, &temp_values[i == 0 ? 0 : 1], out, out_size) != 0) {
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

static int evaluate_ast_value(OoshShell *shell, const OoshAst *ast, OoshValue *value, char *out, size_t out_size);
static int resolve_stage_block_argument(OoshShell *shell, const char *text, OoshBlock *out_block, char *out, size_t out_size);
static int evaluate_expression_text(OoshShell *shell, const char *text, OoshValue *out_value, char *out, size_t out_size);
static int evaluate_block_body(OoshShell *shell, const char *body, OoshValue *out_value, OoshBlockBindingSnapshot *local_snapshots, int *out_local_count, char *out, size_t out_size);
static int render_value_for_shell_var(const OoshValue *value, char *out, size_t out_size);

static int evaluate_value_text_core(OoshShell *shell, const char *text, OoshValue *out_value, char *out, size_t out_size) {
  OoshAst ast;
  char parse_error[OOSH_MAX_OUTPUT];

  if (shell == NULL || text == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  parse_error[0] = '\0';
  if (oosh_parse_value_line(text, &ast, parse_error, sizeof(parse_error)) != 0) {
    if (parse_error[0] != '\0') {
      copy_string(out, out_size, parse_error);
    }
    return 1;
  }

  return evaluate_ast_value(shell, &ast, out_value, out, out_size);
}

static int evaluate_expression_atom(OoshShell *shell, const char *text, OoshValue *out_value, char *out, size_t out_size) {
  char trimmed[OOSH_MAX_LINE];
  char expanded[OOSH_MAX_OUTPUT];
  OoshObject object;
  double number;

  if (shell == NULL || text == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  copy_string(trimmed, sizeof(trimmed), text);
  trim_in_place(trimmed);
  if (trimmed[0] == '\0') {
    oosh_value_init(out_value);
    return 0;
  }

  if (evaluate_value_text_core(shell, trimmed, out_value, out, out_size) == 0) {
    return 0;
  }

  out[0] = '\0';
  if (expand_single_word(shell, trimmed, OOSH_EXPAND_MODE_OBJECT_ARGUMENT, expanded, sizeof(expanded), out, out_size) != 0) {
    return 1;
  }

  if (parse_number_text(expanded, &number) == 0) {
    oosh_value_set_number(out_value, number);
    return 0;
  }
  if (strcmp(expanded, "true") == 0 || strcmp(expanded, "false") == 0) {
    oosh_value_set_boolean(out_value, strcmp(expanded, "true") == 0);
    return 0;
  }
  if (oosh_object_resolve(shell->cwd, expanded, &object) == 0) {
    oosh_value_set_object(out_value, &object);
    return 0;
  }

  oosh_value_set_string(out_value, expanded);
  return 0;
}

static int evaluate_expression_text(OoshShell *shell, const char *text, OoshValue *out_value, char *out, size_t out_size) {
  char trimmed[OOSH_MAX_LINE];
  size_t question_index = 0;
  size_t colon_index = 0;
  size_t eq_index = 0;

  if (shell == NULL || text == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  copy_string(trimmed, sizeof(trimmed), text);
  trim_in_place(trimmed);
  if (trimmed[0] == '\0') {
    oosh_value_init(out_value);
    return 0;
  }

  if (find_top_level_ternary_operator(trimmed, &question_index, &colon_index) == 0) {
    char condition[OOSH_MAX_LINE];
    char true_branch[OOSH_MAX_LINE];
    char false_branch[OOSH_MAX_LINE];
    OoshValue *condition_value;
    int condition_truthy;

    copy_trimmed_range(trimmed, 0, question_index, condition, sizeof(condition));
    copy_trimmed_range(trimmed, question_index + 1, colon_index, true_branch, sizeof(true_branch));
    copy_trimmed_range(trimmed, colon_index + 1, strlen(trimmed), false_branch, sizeof(false_branch));
    if (condition[0] == '\0' || true_branch[0] == '\0' || false_branch[0] == '\0') {
      snprintf(out, out_size, "ternary expression requires condition, true branch and false branch");
      return 1;
    }

    condition_value = (OoshValue *) allocate_temp_buffer(1, sizeof(*condition_value), "ternary condition", out, out_size);
    if (condition_value == NULL) {
      return 1;
    }

    if (evaluate_expression_text(shell, condition, condition_value, out, out_size) != 0) {
      oosh_value_free(condition_value);
      free(condition_value);
      return 1;
    }

    condition_truthy = value_is_truthy(condition_value);
    oosh_value_free(condition_value);
    free(condition_value);
    return evaluate_expression_text(shell, condition_truthy ? true_branch : false_branch, out_value, out, out_size);
  }

  if (find_top_level_equality_operator(trimmed, &eq_index) == 0) {
    char left_text[OOSH_MAX_LINE];
    char right_text[OOSH_MAX_LINE];
    OoshValue *comparison_values;
    int is_equal = 0;

    copy_trimmed_range(trimmed, 0, eq_index, left_text, sizeof(left_text));
    copy_trimmed_range(trimmed, eq_index + 2, strlen(trimmed), right_text, sizeof(right_text));
    if (left_text[0] == '\0' || right_text[0] == '\0') {
      snprintf(out, out_size, "invalid equality expression");
      return 1;
    }

    comparison_values = (OoshValue *) allocate_temp_buffer(2, sizeof(*comparison_values), "comparison values", out, out_size);
    if (comparison_values == NULL) {
      return 1;
    }

    if (evaluate_expression_text(shell, left_text, &comparison_values[0], out, out_size) != 0) {
      oosh_value_free(&comparison_values[0]);
      oosh_value_free(&comparison_values[1]);
      free(comparison_values);
      return 1;
    }
    if (evaluate_expression_text(shell, right_text, &comparison_values[1], out, out_size) != 0) {
      oosh_value_free(&comparison_values[0]);
      oosh_value_free(&comparison_values[1]);
      free(comparison_values);
      return 1;
    }

    if (compare_rendered_values(&comparison_values[0], &comparison_values[1], &is_equal, out, out_size) != 0) {
      oosh_value_free(&comparison_values[0]);
      oosh_value_free(&comparison_values[1]);
      free(comparison_values);
      return 1;
    }

    oosh_value_set_boolean(out_value, is_equal);
    oosh_value_free(&comparison_values[0]);
    oosh_value_free(&comparison_values[1]);
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
  OoshShell *shell,
  const char *name,
  OoshBlockBindingSnapshot *snapshot,
  char *out,
  size_t out_size
) {
  const OoshValue *existing;

  if (shell == NULL || name == NULL || snapshot == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  memset(snapshot, 0, sizeof(*snapshot));
  copy_string(snapshot->name, sizeof(snapshot->name), name);
  existing = oosh_shell_get_binding(shell, name);
  if (existing == NULL) {
    return 0;
  }

  snapshot->existed = 1;
  if (oosh_value_copy(&snapshot->previous, existing) != 0) {
    snprintf(out, out_size, "unable to snapshot binding: %s", name);
    return 1;
  }

  return 0;
}

static int bind_block_arguments(
  OoshShell *shell,
  const OoshBlock *block,
  const OoshValue *args,
  int argc,
  OoshBlockBindingSnapshot snapshots[OOSH_MAX_BLOCK_PARAMS],
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
          oosh_shell_set_binding(shell, snapshots[rollback].name, &snapshots[rollback].previous);
        } else {
          oosh_shell_unset_binding(shell, snapshots[rollback].name);
        }
        oosh_value_free(&snapshots[rollback].previous);
      }
      return 1;
    }
    if (oosh_shell_set_binding(shell, block->params[i], &args[i]) != 0) {
      int rollback;

      for (rollback = i - 1; rollback >= 0; --rollback) {
        if (snapshots[rollback].existed) {
          oosh_shell_set_binding(shell, snapshots[rollback].name, &snapshots[rollback].previous);
        } else {
          oosh_shell_unset_binding(shell, snapshots[rollback].name);
        }
        oosh_value_free(&snapshots[rollback].previous);
      }
      oosh_value_free(&snapshots[i].previous);
      snprintf(out, out_size, "unable to bind block argument: %s", block->params[i]);
      return 1;
    }
  }

  return 0;
}

static void restore_block_arguments(OoshShell *shell, OoshBlockBindingSnapshot snapshots[OOSH_MAX_BLOCK_PARAMS], int count) {
  int i;

  if (shell == NULL || snapshots == NULL) {
    return;
  }

  for (i = count - 1; i >= 0; --i) {
    if (snapshots[i].name[0] == '\0') {
      continue;
    }
    if (snapshots[i].existed) {
      oosh_shell_set_binding(shell, snapshots[i].name, &snapshots[i].previous);
    } else {
      oosh_shell_unset_binding(shell, snapshots[i].name);
    }
    oosh_value_free(&snapshots[i].previous);
  }
}

static void free_function_scope_snapshot(OoshFunctionScopeSnapshot *snapshot) {
  size_t i;

  if (snapshot == NULL) {
    return;
  }

  for (i = 0; i < snapshot->binding_count; ++i) {
    oosh_value_free(&snapshot->bindings[i].value);
  }

  memset(snapshot, 0, sizeof(*snapshot));
}

static int snapshot_function_scope(
  OoshShell *shell,
  OoshFunctionScopeSnapshot *snapshot,
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
    if (oosh_value_copy(&snapshot->bindings[i].value, &shell->bindings[i].value) != 0) {
      free_function_scope_snapshot(snapshot);
      snprintf(out, out_size, "unable to snapshot function scope");
      return 1;
    }
  }

  snapshot->positional_count = shell->positional_count;
  for (i = 0; i < (size_t) shell->positional_count && i < OOSH_MAX_POSITIONAL_PARAMS; ++i) {
    copy_string(snapshot->positional_params[i], sizeof(snapshot->positional_params[i]), shell->positional_params[i]);
  }

  return 0;
}

static int restore_function_scope(
  OoshShell *shell,
  OoshFunctionScopeSnapshot *snapshot,
  char *out,
  size_t out_size
) {
  size_t i;

  if (shell == NULL || snapshot == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  while (shell->var_count > 0) {
    char name[OOSH_MAX_VAR_NAME];

    copy_string(name, sizeof(name), shell->vars[shell->var_count - 1].name);
    if (oosh_shell_unset_var(shell, name) != 0) {
      snprintf(out, out_size, "unable to restore function variable scope");
      return 1;
    }
  }

  for (i = 0; i < snapshot->var_count; ++i) {
    if (oosh_shell_set_var(shell, snapshot->vars[i].name, snapshot->vars[i].value, snapshot->vars[i].exported) != 0) {
      snprintf(out, out_size, "unable to restore function variable scope");
      return 1;
    }
  }

  for (i = 0; i < shell->binding_count; ++i) {
    oosh_value_free(&shell->bindings[i].value);
  }
  shell->binding_count = 0;

  for (i = 0; i < snapshot->binding_count; ++i) {
    copy_string(shell->bindings[i].name, sizeof(shell->bindings[i].name), snapshot->bindings[i].name);
    if (oosh_value_copy(&shell->bindings[i].value, &snapshot->bindings[i].value) != 0) {
      snprintf(out, out_size, "unable to restore function binding scope");
      return 1;
    }
    shell->binding_count++;
  }

  shell->positional_count = snapshot->positional_count;
  for (i = 0; i < (size_t) snapshot->positional_count && i < OOSH_MAX_POSITIONAL_PARAMS; ++i) {
    copy_string(shell->positional_params[i], sizeof(shell->positional_params[i]), snapshot->positional_params[i]);
  }

  return 0;
}

static void free_class_definition_contents_snapshot(OoshClassDef *class_def) {
  size_t i;

  if (class_def == NULL) {
    return;
  }

  for (i = 0; i < class_def->property_count; ++i) {
    oosh_value_free(&class_def->properties[i].default_value);
  }

  memset(class_def, 0, sizeof(*class_def));
}

static int copy_class_definition_snapshot(OoshClassDef *dest, const OoshClassDef *src) {
  size_t i;

  if (dest == NULL || src == NULL) {
    return 1;
  }

  memset(dest, 0, sizeof(*dest));
  copy_string(dest->name, sizeof(dest->name), src->name);
  copy_string(dest->source, sizeof(dest->source), src->source);
  dest->base_count = src->base_count;
  for (i = 0; i < (size_t) src->base_count && i < OOSH_MAX_CLASS_BASES; ++i) {
    copy_string(dest->bases[i], sizeof(dest->bases[i]), src->bases[i]);
  }

  dest->property_count = src->property_count;
  for (i = 0; i < src->property_count; ++i) {
    copy_string(dest->properties[i].name, sizeof(dest->properties[i].name), src->properties[i].name);
    if (oosh_value_copy(&dest->properties[i].default_value, &src->properties[i].default_value) != 0) {
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

static int copy_class_instance_snapshot(OoshClassInstance *dest, const OoshClassInstance *src) {
  if (dest == NULL || src == NULL) {
    return 1;
  }

  memset(dest, 0, sizeof(*dest));
  dest->id = src->id;
  copy_string(dest->class_name, sizeof(dest->class_name), src->class_name);
  if (oosh_value_copy(&dest->fields, &src->fields) != 0) {
    memset(dest, 0, sizeof(*dest));
    return 1;
  }

  return 0;
}

static void free_shell_state_snapshot(OoshShellStateSnapshot *snapshot) {
  size_t i;

  if (snapshot == NULL) {
    return;
  }

  for (i = 0; i < snapshot->binding_count; ++i) {
    oosh_value_free(&snapshot->bindings[i].value);
  }
  for (i = 0; i < snapshot->class_count; ++i) {
    free_class_definition_contents_snapshot(&snapshot->classes[i]);
  }
  for (i = 0; i < snapshot->instance_count; ++i) {
    oosh_value_free(&snapshot->instances[i].fields);
  }

  memset(snapshot, 0, sizeof(*snapshot));
}

static int snapshot_shell_state(
  OoshShell *shell,
  OoshShellStateSnapshot *snapshot,
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
    if (oosh_value_copy(&snapshot->bindings[i].value, &shell->bindings[i].value) != 0) {
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

static void close_subshell_side_effects(OoshShell *shell, const OoshShellStateSnapshot *snapshot) {
  size_t i;

  if (shell == NULL || snapshot == NULL) {
    return;
  }

  for (i = snapshot->job_count; i < shell->job_count; ++i) {
    oosh_platform_close_background_process(&shell->jobs[i].process);
  }

  for (i = snapshot->plugin_count; i < shell->plugin_count; ++i) {
    OoshPluginShutdownFn shutdown_fn;

    if (shell->plugins[i].handle == NULL) {
      continue;
    }

    shutdown_fn = (OoshPluginShutdownFn) oosh_platform_library_symbol(shell->plugins[i].handle, "oosh_plugin_shutdown");
    if (shutdown_fn != NULL) {
      shutdown_fn(shell);
    }
    oosh_platform_library_close(shell->plugins[i].handle);
    shell->plugins[i].handle = NULL;
  }
}

static void clear_shell_state_for_restore(OoshShell *shell) {
  size_t i;

  if (shell == NULL) {
    return;
  }

  for (i = 0; i < shell->binding_count; ++i) {
    oosh_value_free(&shell->bindings[i].value);
  }
  shell->binding_count = 0;

  for (i = 0; i < shell->class_count; ++i) {
    free_class_definition_contents_snapshot(&shell->classes[i]);
  }
  shell->class_count = 0;

  for (i = 0; i < shell->instance_count; ++i) {
    oosh_value_free(&shell->instances[i].fields);
    memset(&shell->instances[i], 0, sizeof(shell->instances[i]));
  }
  shell->instance_count = 0;
}

static int restore_shell_state(
  OoshShell *shell,
  const OoshShellStateSnapshot *snapshot,
  char *out,
  size_t out_size
) {
  size_t i;

  if (shell == NULL || snapshot == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  close_subshell_side_effects(shell, snapshot);
  clear_shell_state_for_restore(shell);

  if (snapshot->cwd[0] != '\0' && oosh_platform_chdir(snapshot->cwd) != 0) {
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
    if (oosh_value_copy(&shell->bindings[i].value, &snapshot->bindings[i].value) != 0) {
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
  OoshShell *shell,
  const OoshShellFunction *function_def,
  const OoshSimpleCommandNode *command,
  char *out,
  size_t out_size
) {
  OoshValue *args;
  int i;

  if (shell == NULL || function_def == NULL || command == NULL || out == NULL || out_size == 0) {
    return 1;
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

  args = (OoshValue *) allocate_temp_buffer(
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
  for (i = 0; i < function_def->param_count && i < OOSH_MAX_POSITIONAL_PARAMS; ++i) {
    copy_string(shell->positional_params[i], sizeof(shell->positional_params[i]), command->argv[i + 1]);
  }

  for (i = 0; i < function_def->param_count; ++i) {
    char rendered[OOSH_MAX_OUTPUT];

    oosh_value_set_string(&args[i], command->argv[i + 1]);
    if (oosh_shell_set_binding(shell, function_def->params[i], &args[i]) != 0) {
      snprintf(out, out_size, "unable to bind function parameter: %s", function_def->params[i]);
      goto cleanup;
    }
    if (render_value_for_shell_var(&args[i], rendered, sizeof(rendered)) != 0) {
      snprintf(out, out_size, "unable to render function parameter: %s", function_def->params[i]);
      goto cleanup;
    }
    if (oosh_shell_set_var(shell, function_def->params[i], rendered, 0) != 0) {
      snprintf(out, out_size, "unable to assign function parameter: %s", function_def->params[i]);
      goto cleanup;
    }
  }

  for (i = 0; i < function_def->param_count; ++i) {
    oosh_value_free(&args[i]);
  }
  free(args);
  return 0;

cleanup:
  for (i = 0; i < function_def->param_count; ++i) {
    oosh_value_free(&args[i]);
  }
  free(args);
  return 1;
}

static int evaluate_block_body(
  OoshShell *shell,
  const char *body,
  OoshValue *out_value,
  OoshBlockBindingSnapshot *local_snapshots,
  int *out_local_count,
  char *out,
  size_t out_size
) {
  size_t segment_start = 0;
  char trimmed_body[OOSH_MAX_BLOCK_SOURCE];
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
    char segment[OOSH_MAX_BLOCK_SOURCE];
    char local_name[OOSH_MAX_NAME];
    char local_expression[OOSH_MAX_BLOCK_SOURCE];
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
      OoshValue *local_value;

      if (local_count >= OOSH_MAX_BLOCK_LOCALS) {
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

      local_value = (OoshValue *) allocate_temp_buffer(1, sizeof(*local_value), "block local value", out, out_size);
      if (local_value == NULL) {
        return 1;
      }

      if (evaluate_expression_text(shell, local_expression, local_value, out, out_size) != 0) {
        oosh_value_free(local_value);
        free(local_value);
        return 1;
      }
      if (oosh_shell_set_binding(shell, local_name, local_value) != 0) {
        oosh_value_free(local_value);
        free(local_value);
        snprintf(out, out_size, "unable to bind local value: %s", local_name);
        return 1;
      }

      oosh_value_free(local_value);
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

static int evaluate_block(OoshShell *shell, const OoshBlock *block, const OoshValue *args, int argc, OoshValue *out_value, char *out, size_t out_size) {
  OoshBlockBindingSnapshot *snapshots;
  OoshBlockBindingSnapshot *local_snapshots;
  int local_count = 0;
  int status;

  if (shell == NULL || block == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  snapshots = (OoshBlockBindingSnapshot *) allocate_temp_buffer(
    OOSH_MAX_BLOCK_PARAMS,
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

  local_snapshots = (OoshBlockBindingSnapshot *) allocate_temp_buffer(
    OOSH_MAX_BLOCK_LOCALS,
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

int oosh_execute_block(
  OoshShell *shell,
  const OoshBlock *block,
  const OoshValue *args,
  int argc,
  OoshValue *out_value,
  char *out,
  size_t out_size
) {
  return evaluate_block(shell, block, args, argc, out_value, out, out_size);
}

static int evaluate_extension_args(
  OoshShell *shell,
  const char raw_argv[OOSH_MAX_ARGS][OOSH_MAX_TOKEN],
  int argc,
  OoshValue args[OOSH_MAX_ARGS],
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
  OoshShell *shell,
  const char *raw_text,
  char *arg_text,
  size_t arg_text_size,
  char *out,
  size_t out_size
) {
  OoshValue *value;
  int status;

  if (shell == NULL || raw_text == NULL || arg_text == NULL || arg_text_size == 0 || out == NULL || out_size == 0) {
    return 1;
  }

  value = (OoshValue *) allocate_temp_buffer(1, sizeof(*value), "object method argument", out, out_size);
  if (value == NULL) {
    return 1;
  }

  if (evaluate_token_argument_value(shell, raw_text, value, out, out_size) != 0) {
    free(value);
    return 1;
  }

  status = oosh_value_render(value, arg_text, arg_text_size);
  free(value);
  if (status != 0 && out[0] == '\0') {
    snprintf(out, out_size, "unable to render object method argument");
  }
  return status;
}

static int resolve_receiver_value(
  OoshShell *shell,
  const char *raw_selector,
  OoshValue *out_value,
  char *out,
  size_t out_size
) {
  char selector[OOSH_MAX_PATH];
  OoshObject object;
  char nested_error[OOSH_MAX_OUTPUT];

  if (shell == NULL || raw_selector == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  nested_error[0] = '\0';
  if (oosh_evaluate_line_value(shell, raw_selector, out_value, nested_error, sizeof(nested_error)) == 0) {
    return 0;
  }

  if (expand_single_word(shell, raw_selector, OOSH_EXPAND_MODE_OBJECT_SELECTOR, selector, sizeof(selector), out, out_size) != 0) {
    return 1;
  }

  {
    const OoshValue *binding = oosh_shell_get_binding(shell, selector);

    if (binding != NULL) {
      return oosh_value_copy(out_value, binding);
    }
  }

  if (oosh_object_resolve(shell->cwd, selector, &object) != 0) {
    snprintf(out, out_size, "unable to resolve selector: %s", selector);
    return 1;
  }

  oosh_value_set_object(out_value, &object);
  return 0;
}

static int extension_target_matches(const OoshObjectExtension *extension, const OoshValue *receiver) {
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

static const OoshObjectExtension *find_extension(
  const OoshShell *shell,
  const OoshValue *receiver,
  OoshMemberKind member_kind,
  const char *name
) {
  size_t i;

  if (shell == NULL || receiver == NULL || name == NULL) {
    return NULL;
  }

  for (i = shell->extension_count; i > 0; --i) {
    const OoshObjectExtension *extension = &shell->extensions[i - 1];

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
  OoshShell *shell,
  const OoshValue *receiver,
  const char *name,
  OoshValue *out_value,
  char *out,
  size_t out_size
) {
  const OoshObjectExtension *extension;

  if (shell == NULL || receiver == NULL || name == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  extension = find_extension(shell, receiver, OOSH_MEMBER_PROPERTY, name);
  if (extension == NULL) {
    snprintf(out, out_size, "unknown property: %s", name);
    return 1;
  }

  if (extension->impl_kind == OOSH_EXTENSION_IMPL_NATIVE) {
    return extension->property_fn == NULL ? 1 : extension->property_fn(shell, receiver, out_value, out, out_size);
  }

  return evaluate_block(shell, &extension->block, receiver, 1, out_value, out, out_size);
}

static int invoke_extension_property_text(
  OoshShell *shell,
  const OoshValue *receiver,
  const char *name,
  char *out,
  size_t out_size
) {
  OoshValue *value;
  int status;

  value = (OoshValue *) allocate_temp_buffer(1, sizeof(*value), "extension property value", out, out_size);
  if (value == NULL) {
    return 1;
  }

  if (invoke_extension_property_value(shell, receiver, name, value, out, out_size) != 0) {
    free(value);
    return 1;
  }

  status = oosh_value_render(value, out, out_size);
  free(value);
  return status;
}

static int invoke_extension_method_value(
  OoshShell *shell,
  const OoshValue *receiver,
  const char *name,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
  char *out,
  size_t out_size
) {
  const OoshObjectExtension *extension;

  if (shell == NULL || receiver == NULL || name == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  extension = find_extension(shell, receiver, OOSH_MEMBER_METHOD, name);
  if (extension == NULL) {
    snprintf(out, out_size, "unknown method: %s", name);
    return 1;
  }

  if (extension->impl_kind == OOSH_EXTENSION_IMPL_NATIVE) {
    return extension->method_fn == NULL ? 1 : extension->method_fn(shell, receiver, argc, args, out_value, out, out_size);
  }

  {
    OoshValue *block_args;
    int i;

    if (argc + 1 > OOSH_MAX_ARGS) {
      snprintf(out, out_size, "too many method arguments");
      return 1;
    }

    block_args = (OoshValue *) allocate_temp_buffer((size_t) argc + 1, sizeof(*block_args), "extension method block arguments", out, out_size);
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
  OoshShell *shell,
  const OoshValue *receiver,
  const char *name,
  int argc,
  const OoshValue *args,
  char *out,
  size_t out_size
) {
  OoshValue *value;
  int status;

  value = (OoshValue *) allocate_temp_buffer(1, sizeof(*value), "extension method value", out, out_size);
  if (value == NULL) {
    return 1;
  }

  if (invoke_extension_method_value(shell, receiver, name, argc, args, value, out, out_size) != 0) {
    free(value);
    return 1;
  }

  status = oosh_value_render(value, out, out_size);
  free(value);
  return status;
}

static int error_has_prefix(const char *text, const char *prefix) {
  return text != NULL && prefix != NULL && strncmp(text, prefix, strlen(prefix)) == 0;
}

static int get_property_value_with_shell(
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

  if (receiver->kind == OOSH_VALUE_CLASS || receiver->kind == OOSH_VALUE_INSTANCE) {
    if (oosh_shell_get_class_property_value(shell, receiver, property, out_value, out, out_size) == 0) {
      return 0;
    }
    if (!error_has_prefix(out, "unknown property:")) {
      return 1;
    }
    out[0] = '\0';
  }

  return oosh_value_get_property_value(receiver, property, out_value, out, out_size);
}

static int get_property_text_with_shell(
  OoshShell *shell,
  const OoshValue *receiver,
  const char *property,
  char *out,
  size_t out_size
) {
  OoshValue value;
  int status;

  if (shell == NULL || receiver == NULL || property == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (get_property_value_with_shell(shell, receiver, property, &value, out, out_size) != 0) {
    return 1;
  }

  status = oosh_value_render(&value, out, out_size);
  oosh_value_free(&value);
  return status;
}

static int get_item_property_text(
  OoshShell *shell,
  const OoshValueItem *item,
  const char *property,
  char *out,
  size_t out_size
) {
  OoshValue *receiver;
  int status;

  if (item == NULL || property == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (oosh_value_item_get_property(item, property, out, out_size) == 0) {
    return 0;
  }

  receiver = (OoshValue *) allocate_temp_buffer(1, sizeof(*receiver), "item property receiver", out, out_size);
  if (receiver == NULL) {
    return 1;
  }

  if (shell == NULL || oosh_value_set_from_item(receiver, item) != 0) {
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
  OoshShell *shell,
  const OoshValueItem *left,
  const OoshValueItem *right,
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

static int render_value_in_place(OoshValue *value, char *out, size_t out_size) {
  char rendered[OOSH_MAX_OUTPUT];

  if (value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (oosh_value_render(value, rendered, sizeof(rendered)) != 0) {
    snprintf(out, out_size, "unable to render pipeline value");
    return 1;
  }

  oosh_value_set_string(value, rendered);
  return 0;
}

static int call_bound_value(
  OoshShell *shell,
  const OoshValue *bound_value,
  const OoshObjectExpressionNode *expression,
  OoshValue *out_value,
  char *out,
  size_t out_size
) {
  OoshValue *args = NULL;
  int i;
  int status;

  if (shell == NULL || bound_value == NULL || expression == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (strcmp(expression->member, "call") == 0 && bound_value->kind == OOSH_VALUE_BLOCK) {
    args = (OoshValue *) allocate_temp_buffer((size_t) expression->argc, sizeof(*args), "bound value arguments", out, out_size);
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

  if (bound_value->kind == OOSH_VALUE_OBJECT && is_builtin_object_method_name(expression->member)) {
    char expanded_args[OOSH_MAX_ARGS][OOSH_MAX_TOKEN];
    char *argv[OOSH_MAX_ARGS];
    int expanded_argc = 0;

    for (i = 0; i < expression->argc; ++i) {
      if (evaluate_token_argument_text(shell, expression->raw_argv[i], expanded_args[i], sizeof(expanded_args[i]), out, out_size) != 0) {
        return 1;
      }
      expanded_argc++;
    }

    build_command_argv(expanded_args, expanded_argc, argv);
    return oosh_object_call_method_value(&bound_value->object, expression->member, expanded_argc, argv, out_value, out, out_size);
  }

  if (bound_value->kind == OOSH_VALUE_CLASS || bound_value->kind == OOSH_VALUE_INSTANCE) {
    args = (OoshValue *) allocate_temp_buffer((size_t) expression->argc, sizeof(*args), "class method arguments", out, out_size);
    if (expression->argc > 0 && args == NULL) {
      return 1;
    }

    if (evaluate_extension_args(shell, expression->raw_argv, expression->argc, args, out, out_size) != 0) {
      free(args);
      return 1;
    }

    status = oosh_shell_call_class_method(shell, bound_value, expression->member, expression->argc, args, out_value, out, out_size);
    free(args);
    if (status == 0 || !error_has_prefix(out, "unknown method:")) {
      return status;
    }
    out[0] = '\0';
  }

  args = (OoshValue *) allocate_temp_buffer((size_t) expression->argc, sizeof(*args), "bound value extension arguments", out, out_size);
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

static int evaluate_object_expression_text(OoshShell *shell, const OoshObjectExpressionNode *expression, char *out, size_t out_size) {
  OoshValue receiver;
  OoshValue result;
  int status;

  if (shell == NULL || expression == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (resolve_receiver_value(shell, expression->raw_selector, &receiver, out, out_size) != 0) {
    return 1;
  }

  if (expression->member_kind == OOSH_MEMBER_PROPERTY) {
    status = get_property_value_with_shell(shell, &receiver, expression->member, &result, out, out_size);
    if (status != 0 && error_has_prefix(out, "unknown property:")) {
      status = invoke_extension_property_value(shell, &receiver, expression->member, &result, out, out_size);
    }
    if (status != 0) {
      oosh_value_free(&receiver);
      return 1;
    }
    status = oosh_value_render(&result, out, out_size);
    oosh_value_free(&result);
    oosh_value_free(&receiver);
    return status;
  }

  status = call_bound_value(shell, &receiver, expression, &result, out, out_size);
  oosh_value_free(&receiver);
  if (status != 0) {
    return 1;
  }

  status = oosh_value_render(&result, out, out_size);
  oosh_value_free(&result);
  return status;
}

static int evaluate_object_expression_value(OoshShell *shell, const OoshObjectExpressionNode *expression, OoshValue *value, char *out, size_t out_size) {
  OoshValue receiver;
  int status;

  if (shell == NULL || expression == NULL || value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (resolve_receiver_value(shell, expression->raw_selector, &receiver, out, out_size) != 0) {
    return 1;
  }

  oosh_value_init(value);
  if (expression->member_kind == OOSH_MEMBER_PROPERTY) {
    status = get_property_value_with_shell(shell, &receiver, expression->member, value, out, out_size);
    if (status != 0 && error_has_prefix(out, "unknown property:")) {
      status = invoke_extension_property_value(shell, &receiver, expression->member, value, out, out_size);
    }
    oosh_value_free(&receiver);
    return status;
  }

  status = call_bound_value(shell, &receiver, expression, value, out, out_size);
  oosh_value_free(&receiver);
  return status;
}

static int split_text_lines_into_value(const char *text, OoshValue *out_value) {
  const char *cursor;

  if (text == NULL || out_value == NULL) {
    return 1;
  }

  oosh_value_init(out_value);
  out_value->kind = OOSH_VALUE_LIST;
  cursor = text;

  while (*cursor != '\0') {
    const char *line_end = strchr(cursor, '\n');
    OoshValueItem item;
    size_t len;
    char line[OOSH_MAX_VALUE_TEXT];

    if (line_end == NULL) {
      line_end = cursor + strlen(cursor);
    }

    len = (size_t) (line_end - cursor);
    if (len >= sizeof(line)) {
      len = sizeof(line) - 1;
    }

    memcpy(line, cursor, len);
    line[len] = '\0';
    oosh_value_item_init(&item);
    item.kind = OOSH_VALUE_STRING;
    copy_string(item.text, sizeof(item.text), line);
    if (oosh_value_list_append_item(out_value, &item) != 0) {
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

static int append_string_item_to_value(OoshValue *out_value, const char *text) {
  OoshValueItem item;

  if (out_value == NULL || text == NULL) {
    return 1;
  }

  oosh_value_item_init(&item);
  item.kind = OOSH_VALUE_STRING;
  copy_string(item.text, sizeof(item.text), text);
  return oosh_value_list_append_item(out_value, &item);
}

static int split_text_whitespace_into_value(const char *text, OoshValue *out_value) {
  const char *cursor;

  if (text == NULL || out_value == NULL) {
    return 1;
  }

  oosh_value_init(out_value);
  out_value->kind = OOSH_VALUE_LIST;
  cursor = text;

  while (*cursor != '\0') {
    const char *start;
    const char *end;
    char token[OOSH_MAX_VALUE_TEXT];
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

static int split_text_delimiter_into_value(const char *text, const char *separator, OoshValue *out_value) {
  const char *cursor;
  size_t separator_len;

  if (text == NULL || separator == NULL || out_value == NULL || separator[0] == '\0') {
    return 1;
  }

  oosh_value_init(out_value);
  out_value->kind = OOSH_VALUE_LIST;
  cursor = text;
  separator_len = strlen(separator);

  while (1) {
    const char *match = strstr(cursor, separator);
    char token[OOSH_MAX_VALUE_TEXT];
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

static int evaluate_stage_argument_to_text(OoshShell *shell, const char *text, char *out_text, size_t out_text_size, char *out, size_t out_size) {
  OoshValue *value;
  int status;

  if (shell == NULL || text == NULL || out_text == NULL || out_text_size == 0 || out == NULL || out_size == 0) {
    return 1;
  }

  value = (OoshValue *) allocate_temp_buffer(1, sizeof(*value), "stage argument value", out, out_size);
  if (value == NULL) {
    return 1;
  }

  if (oosh_evaluate_line_value(shell, text, value, out, out_size) != 0) {
    free(value);
    return 1;
  }

  status = oosh_value_render(value, out_text, out_text_size);
  free(value);
  if (status != 0 && out[0] == '\0') {
    snprintf(out, out_size, "unable to render stage argument");
  }
  return status;
}

static int execute_capture_source(OoshShell *shell, const char *raw_command, int split_lines, OoshValue *value, char *out, size_t out_size) {
  char command_text[OOSH_MAX_LINE];
  char capture_output[OOSH_MAX_OUTPUT];

  if (shell == NULL || raw_command == NULL || value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (expand_single_word(shell, raw_command, OOSH_EXPAND_MODE_COMMAND, command_text, sizeof(command_text), out, out_size) != 0) {
    return 1;
  }

  capture_output[0] = '\0';
  if (oosh_shell_execute_line(shell, command_text, capture_output, sizeof(capture_output)) != 0) {
    if (capture_output[0] != '\0') {
      copy_string(out, out_size, capture_output);
    } else {
      snprintf(out, out_size, "capture() command failed: %s", command_text);
    }
    return 1;
  }

  if (split_lines) {
    if (split_text_lines_into_value(capture_output, value) != 0) {
      snprintf(out, out_size, "unable to split captured output into lines");
      return 1;
    }
    return 0;
  }

  oosh_value_set_string(value, capture_output);
  return 0;
}

static int invoke_value_resolver(
  OoshShell *shell,
  const OoshValueSourceNode *source,
  OoshValue *value,
  char *out,
  size_t out_size
) {
  const OoshValueResolverDef *resolver;
  const OoshClassDef *class_def;
  OoshValue *args = NULL;
  int i;
  int status;

  if (shell == NULL || source == NULL || value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  resolver = oosh_shell_find_value_resolver(shell, source->text);
  class_def = oosh_shell_find_class(shell, source->text);
  if ((resolver == NULL || resolver->fn == NULL) && class_def == NULL) {
    snprintf(out, out_size, "unknown value resolver: %s", source->text);
    return 1;
  }

  args = (OoshValue *) allocate_temp_buffer((size_t) source->argc, sizeof(*args), "value resolver arguments", out, out_size);
  if (source->argc > 0 && args == NULL) {
    return 1;
  }

  for (i = 0; i < source->argc; ++i) {
    if (evaluate_token_argument_value(shell, source->raw_argv[i], &args[i], out, out_size) != 0) {
      int rollback;

      for (rollback = 0; rollback < i; ++rollback) {
        oosh_value_free(&args[rollback]);
      }
      free(args);
      return 1;
    }
  }

  if (resolver != NULL && resolver->fn != NULL) {
    status = resolver->fn(shell, source->argc, args, value, out, out_size);
  } else {
    status = oosh_shell_instantiate_class(shell, source->text, source->argc, args, value, out, out_size);
  }
  for (i = 0; i < source->argc; ++i) {
    oosh_value_free(&args[i]);
  }
  free(args);
  return status;
}

static int evaluate_value_source(OoshShell *shell, const OoshValueSourceNode *source, OoshValue *value, char *out, size_t out_size) {
  int i;

  if (shell == NULL || source == NULL || value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  switch (source->kind) {
    case OOSH_VALUE_SOURCE_OBJECT_EXPRESSION:
      return evaluate_object_expression_value(shell, &source->object_expression, value, out, out_size);
    case OOSH_VALUE_SOURCE_BINDING: {
      const OoshValue *binding = oosh_shell_get_binding(shell, source->binding);

      if (binding == NULL) {
        if (oosh_shell_find_class(shell, source->binding) != NULL) {
          oosh_value_set_class(value, source->binding);
          return 0;
        }
        snprintf(out, out_size, "value binding not found: %s", source->binding);
        return 1;
      }
      return oosh_value_copy(value, binding);
    }
    case OOSH_VALUE_SOURCE_STRING_LITERAL: {
      char expanded[OOSH_MAX_OUTPUT];

      if (expand_single_word(shell, source->raw_text, OOSH_EXPAND_MODE_OBJECT_ARGUMENT, expanded, sizeof(expanded), out, out_size) != 0) {
        return 1;
      }
      oosh_value_set_string(value, expanded);
      return 0;
    }
    case OOSH_VALUE_SOURCE_NUMBER_LITERAL: {
      char expanded[OOSH_MAX_TOKEN];
      double number;

      if (expand_single_word(shell, source->raw_text, OOSH_EXPAND_MODE_OBJECT_ARGUMENT, expanded, sizeof(expanded), out, out_size) != 0) {
        return 1;
      }
      if (parse_number_text(expanded, &number) != 0) {
        snprintf(out, out_size, "invalid numeric source: %s", expanded);
        return 1;
      }
      oosh_value_set_number(value, number);
      return 0;
    }
    case OOSH_VALUE_SOURCE_BOOLEAN_LITERAL: {
      char expanded[OOSH_MAX_TOKEN];

      if (expand_single_word(shell, source->raw_text, OOSH_EXPAND_MODE_OBJECT_ARGUMENT, expanded, sizeof(expanded), out, out_size) != 0) {
        return 1;
      }
      if (strcmp(expanded, "true") != 0 && strcmp(expanded, "false") != 0) {
        snprintf(out, out_size, "bool() expects true or false");
        return 1;
      }
      oosh_value_set_boolean(value, strcmp(expanded, "true") == 0);
      return 0;
    }
    case OOSH_VALUE_SOURCE_LIST_LITERAL:
      oosh_value_init(value);
      value->kind = OOSH_VALUE_LIST;
      for (i = 0; i < source->argc; ++i) {
        OoshValue item_value;

        if (evaluate_token_argument_value(shell, source->raw_argv[i], &item_value, out, out_size) != 0) {
          return 1;
        }
        if (oosh_value_list_append_value(value, &item_value) != 0) {
          oosh_value_free(&item_value);
          snprintf(out, out_size, "unable to append list item");
          return 1;
        }
        oosh_value_free(&item_value);
      }
      return 0;
    case OOSH_VALUE_SOURCE_BLOCK_LITERAL:
      oosh_value_set_block(value, &source->block);
      return 0;
    case OOSH_VALUE_SOURCE_CAPTURE_TEXT:
      return execute_capture_source(shell, source->raw_text, 0, value, out, out_size);
    case OOSH_VALUE_SOURCE_CAPTURE_LINES:
      return execute_capture_source(shell, source->raw_text, 1, value, out, out_size);
    case OOSH_VALUE_SOURCE_TERNARY:
      return evaluate_expression_text(shell, source->raw_text, value, out, out_size);
    case OOSH_VALUE_SOURCE_RESOLVER_CALL:
      return invoke_value_resolver(shell, source, value, out, out_size);
    default:
      snprintf(out, out_size, "unsupported value source");
      return 1;
  }
}

static int resolve_stage_block_argument(OoshShell *shell, const char *text, OoshBlock *out_block, char *out, size_t out_size) {
  OoshValue *value;
  int status;

  if (shell == NULL || text == NULL || out_block == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  value = (OoshValue *) allocate_temp_buffer(1, sizeof(*value), "stage block value", out, out_size);
  if (value == NULL) {
    return 1;
  }

  if (oosh_evaluate_line_value(shell, text, value, out, out_size) != 0) {
    free(value);
    return 1;
  }

  if (value->kind != OOSH_VALUE_BLOCK) {
    free(value);
    snprintf(out, out_size, "stage expects a block value");
    return 1;
  }

  *out_block = value->block;
  status = 0;
  free(value);
  return status;
}

static int apply_where_stage(OoshShell *shell, OoshValue *value, const OoshPipelineStageNode *stage, char *out, size_t out_size) {
  char property[OOSH_MAX_NAME];
  char expected[OOSH_MAX_TOKEN];
  size_t i;

  if (shell == NULL || value == NULL || stage == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (value->kind != OOSH_VALUE_LIST) {
    snprintf(out, out_size, "where() expects a list");
    return 1;
  }

  if (parse_where_condition(stage->raw_args, property, sizeof(property), expected, sizeof(expected)) != 0) {
    OoshBlock block;
    OoshValue *temp_values;
    size_t write_index = 0;

    if (resolve_stage_block_argument(shell, stage->raw_args, &block, out, out_size) != 0) {
      snprintf(out, out_size, "where() expects syntax like where(type == \"file\") or where(block)");
      return 1;
    }

    temp_values = (OoshValue *) allocate_temp_buffer(2, sizeof(*temp_values), "where() block values", out, out_size);
    if (temp_values == NULL) {
      return 1;
    }

    for (i = 0; i < value->list.count; ++i) {
      if (oosh_value_set_from_item(&temp_values[0], &value->list.items[i]) != 0) {
        free(temp_values);
        snprintf(out, out_size, "unable to prepare where() block argument");
        return 1;
      }
      if (evaluate_block(shell, &block, &temp_values[0], 1, &temp_values[1], out, out_size) != 0) {
        free(temp_values);
        return 1;
      }
      if (value_is_truthy(&temp_values[1])) {
        value->list.items[write_index++] = value->list.items[i];
      }
    }

    value->list.count = write_index;
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
        value->list.items[write_index++] = value->list.items[i];
      }
    }

    value->list.count = write_index;
  }
  return 0;
}

static int apply_sort_stage(OoshShell *shell, OoshValue *value, const OoshPipelineStageNode *stage, char *out, size_t out_size) {
  char property[OOSH_MAX_NAME];
  char direction[16] = "asc";
  int parsed;
  int descending = 0;
  size_t i;
  size_t j;

  if (value->kind != OOSH_VALUE_LIST) {
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
        OoshValueItem temp = value->list.items[i];
        value->list.items[i] = value->list.items[j];
        value->list.items[j] = temp;
      }
    }
  }

  return 0;
}

static int apply_take_stage(OoshValue *value, const OoshPipelineStageNode *stage, char *out, size_t out_size) {
  size_t limit;

  if (value->kind != OOSH_VALUE_LIST) {
    snprintf(out, out_size, "take() expects a list");
    return 1;
  }

  if (parse_unsigned_number(stage->raw_args, &limit) != 0) {
    snprintf(out, out_size, "take() expects a numeric limit");
    return 1;
  }

  if (limit < value->list.count) {
    value->list.count = limit;
  }
  return 0;
}

static int apply_first_stage(OoshValue *value, char *out, size_t out_size) {
  if (value->kind != OOSH_VALUE_LIST) {
    snprintf(out, out_size, "first() expects a list");
    return 1;
  }

  if (value->list.count == 0) {
    snprintf(out, out_size, "first() cannot be used on an empty list");
    return 1;
  }

  return oosh_value_set_from_item(value, &value->list.items[0]);
}

static int apply_count_stage(OoshValue *value, char *out, size_t out_size) {
  (void) out;
  (void) out_size;

  if (value->kind == OOSH_VALUE_LIST) {
    oosh_value_set_number(value, (double) value->list.count);
    return 0;
  }

  if (value->kind == OOSH_VALUE_MAP) {
    oosh_value_set_number(value, (double) value->map.count);
    return 0;
  }

  if (value->kind == OOSH_VALUE_STRING || value->kind == OOSH_VALUE_NUMBER ||
      value->kind == OOSH_VALUE_BOOLEAN || value->kind == OOSH_VALUE_OBJECT) {
    oosh_value_set_number(value, 1.0);
    return 0;
  }

  snprintf(out, out_size, "count() expects a value, list or map");
  return 1;
}

static int apply_lines_stage(OoshValue *value, char *out, size_t out_size) {
  char original[OOSH_MAX_OUTPUT];

  if (value->kind != OOSH_VALUE_STRING) {
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

static int apply_trim_stage(OoshValue *value, char *out, size_t out_size) {
  size_t i;

  if (value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (value->kind == OOSH_VALUE_STRING) {
    trim_in_place(value->text);
    return 0;
  }

  if (value->kind != OOSH_VALUE_LIST) {
    snprintf(out, out_size, "trim() expects a string or list");
    return 1;
  }

  for (i = 0; i < value->list.count; ++i) {
    if (value->list.items[i].kind == OOSH_VALUE_STRING) {
      trim_in_place(value->list.items[i].text);
    }
  }
  return 0;
}

static int apply_split_stage(OoshShell *shell, OoshValue *value, const OoshPipelineStageNode *stage, char *out, size_t out_size) {
  char original[OOSH_MAX_OUTPUT];
  char separator[OOSH_MAX_OUTPUT];

  if (shell == NULL || value == NULL || stage == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (value->kind != OOSH_VALUE_STRING) {
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

static int apply_join_stage(OoshShell *shell, OoshValue *value, const OoshPipelineStageNode *stage, char *out, size_t out_size) {
  char separator[OOSH_MAX_OUTPUT];
  size_t i;
  size_t used = 0;

  if (shell == NULL || value == NULL || stage == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (value->kind != OOSH_VALUE_LIST) {
    snprintf(out, out_size, "join() expects a list");
    return 1;
  }

  separator[0] = '\0';
  if (stage->raw_args[0] != '\0' && evaluate_stage_argument_to_text(shell, stage->raw_args, separator, sizeof(separator), out, out_size) != 0) {
    return 1;
  }

  out[0] = '\0';
  for (i = 0; i < value->list.count; ++i) {
    char rendered[OOSH_MAX_OUTPUT];
    size_t rendered_len;
    size_t separator_len = strlen(separator);

    if (oosh_value_item_render(&value->list.items[i], rendered, sizeof(rendered)) != 0) {
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
  value->kind = OOSH_VALUE_STRING;
  return 0;
}

static int apply_to_json_stage(OoshValue *value, char *out, size_t out_size) {
  char json[OOSH_MAX_OUTPUT];

  if (value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (oosh_value_to_json(value, json, sizeof(json)) != 0) {
    snprintf(out, out_size, "unable to serialize value as JSON");
    return 1;
  }

  oosh_value_set_string(value, json);
  return 0;
}

static int apply_from_json_stage(OoshValue *value, char *out, size_t out_size) {
  OoshValue parsed;

  if (value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (value->kind != OOSH_VALUE_STRING) {
    snprintf(out, out_size, "from_json() expects a string");
    return 1;
  }

  if (oosh_value_parse_json(value->text, &parsed, out, out_size) != 0) {
    return 1;
  }

  *value = parsed;
  return 0;
}

static int apply_reduce_stage(OoshShell *shell, OoshValue *value, const OoshPipelineStageNode *stage, char *out, size_t out_size) {
  char args[2][OOSH_MAX_LINE];
  int arg_count = 0;
  OoshBlock block;
  OoshValue *temp_values;
  size_t index = 0;

  if (shell == NULL || value == NULL || stage == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (value->kind != OOSH_VALUE_LIST) {
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

  temp_values = (OoshValue *) allocate_temp_buffer(3, sizeof(*temp_values), "reduce() values", out, out_size);
  if (temp_values == NULL) {
    return 1;
  }

  if (arg_count == 2) {
    if (oosh_evaluate_line_value(shell, args[0], &temp_values[0], out, out_size) != 0) {
      free(temp_values);
      return 1;
    }
  } else {
    if (value->list.count == 0) {
      free(temp_values);
      snprintf(out, out_size, "reduce() without an init cannot be used on an empty list");
      return 1;
    }
    if (oosh_value_set_from_item(&temp_values[0], &value->list.items[0]) != 0) {
      free(temp_values);
      snprintf(out, out_size, "unable to prepare reduce() accumulator");
      return 1;
    }
    index = 1;
  }

  for (; index < value->list.count; ++index) {
    if (oosh_value_set_from_item(&temp_values[1], &value->list.items[index]) != 0) {
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

static int parse_each_selector(const char *text, OoshEachSelector *out_selector, char *error, size_t error_size) {
  OoshTokenStream stream;
  size_t index = 0;

  if (text == NULL || out_selector == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  memset(out_selector, 0, sizeof(*out_selector));
  if (oosh_lex_line(text, &stream, error, error_size) != 0) {
    return 1;
  }

  if (stream.tokens[index].kind != OOSH_TOKEN_WORD) {
    snprintf(error, error_size, "each() expects a property or method name");
    return 1;
  }

  copy_string(out_selector->name, sizeof(out_selector->name), stream.tokens[index].text);
  index++;

  if (stream.tokens[index].kind == OOSH_TOKEN_EOF) {
    if (strcmp(out_selector->name, "render") == 0) {
      out_selector->is_render = 1;
    } else {
      out_selector->member_kind = OOSH_MEMBER_PROPERTY;
    }
    return 0;
  }

  if (stream.tokens[index].kind != OOSH_TOKEN_LPAREN) {
    snprintf(error, error_size, "each() expects syntax like each(name) or each(parent())");
    return 1;
  }

  if (strcmp(out_selector->name, "render") == 0) {
    out_selector->is_render = 1;
  } else {
    out_selector->member_kind = OOSH_MEMBER_METHOD;
  }
  index++;

  while (stream.tokens[index].kind != OOSH_TOKEN_RPAREN) {
    if (stream.tokens[index].kind == OOSH_TOKEN_EOF) {
      snprintf(error, error_size, "unterminated each() selector");
      return 1;
    }

    if (!is_value_token_kind(stream.tokens[index].kind)) {
      snprintf(error, error_size, "invalid each() argument token: %s", oosh_token_kind_name(stream.tokens[index].kind));
      return 1;
    }

    if (out_selector->argc >= OOSH_MAX_ARGS) {
      snprintf(error, error_size, "too many each() arguments");
      return 1;
    }

    copy_string(out_selector->argv[out_selector->argc], sizeof(out_selector->argv[out_selector->argc]), stream.tokens[index].text);
    copy_string(out_selector->raw_argv[out_selector->argc], sizeof(out_selector->raw_argv[out_selector->argc]), stream.tokens[index].raw);
    out_selector->argc++;
    index++;

    if (stream.tokens[index].kind == OOSH_TOKEN_COMMA) {
      index++;
      continue;
    }

    if (stream.tokens[index].kind != OOSH_TOKEN_RPAREN) {
      snprintf(error, error_size, "expected ',' or ')' in each() arguments");
      return 1;
    }
  }

  index++;
  if (stream.tokens[index].kind != OOSH_TOKEN_EOF) {
    snprintf(error, error_size, "unexpected token after each() selector");
    return 1;
  }

  return 0;
}

static int apply_each_selector_to_item(OoshShell *shell, const OoshEachSelector *selector, const OoshValueItem *item, OoshValue *out_value, char *out, size_t out_size) {
  if (shell == NULL || selector == NULL || item == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (selector->is_render) {
    char rendered[OOSH_MAX_OUTPUT];

    if (oosh_value_item_render(item, rendered, sizeof(rendered)) != 0) {
      snprintf(out, out_size, "unable to render item inside each()");
      return 1;
    }
    oosh_value_set_string(out_value, rendered);
    return 0;
  }

  if (selector->member_kind == OOSH_MEMBER_PROPERTY) {
    char property_value[OOSH_MAX_OUTPUT];

    if (get_item_property_text(shell, item, selector->name, property_value, sizeof(property_value)) != 0) {
      snprintf(out, out_size, "unknown property in each(): %s", selector->name);
      return 1;
    }

    oosh_value_set_string(out_value, property_value);
    return 0;
  }

  if (item->kind == OOSH_VALUE_OBJECT && is_builtin_object_method_name(selector->name)) {
    char expanded_args[OOSH_MAX_ARGS][OOSH_MAX_TOKEN];
    char *argv[OOSH_MAX_ARGS];
    int i;

    for (i = 0; i < selector->argc; ++i) {
      if (expand_single_word(shell, selector->raw_argv[i], OOSH_EXPAND_MODE_OBJECT_ARGUMENT, expanded_args[i], sizeof(expanded_args[i]), out, out_size) != 0) {
        return 1;
      }
    }

    build_command_argv(expanded_args, selector->argc, argv);
    return oosh_object_call_method_value(&item->object, selector->name, selector->argc, argv, out_value, out, out_size);
  }

  {
    OoshValue *receiver;
    OoshValue *args;
    int status;

    receiver = (OoshValue *) allocate_temp_buffer(1, sizeof(*receiver), "each() receiver", out, out_size);
    if (receiver == NULL) {
      return 1;
    }

    args = (OoshValue *) allocate_temp_buffer((size_t) selector->argc, sizeof(*args), "each() arguments", out, out_size);
    if (selector->argc > 0 && args == NULL) {
      free(receiver);
      return 1;
    }

    if (oosh_value_set_from_item(receiver, item) != 0) {
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

static int apply_each_stage(OoshShell *shell, OoshValue *value, const OoshPipelineStageNode *stage, char *out, size_t out_size) {
  OoshEachSelector selector;
  OoshValue *result;
  size_t i;

  if (shell == NULL || value == NULL || stage == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (value->kind != OOSH_VALUE_LIST) {
    snprintf(out, out_size, "each() expects a list");
    return 1;
  }

  {
    OoshBlock block;

    if (resolve_stage_block_argument(shell, stage->raw_args, &block, out, out_size) == 0) {
      OoshValue *temp_values;

      result = (OoshValue *) allocate_temp_buffer(1, sizeof(*result), "each() result", out, out_size);
      if (result == NULL) {
        return 1;
      }
      temp_values = (OoshValue *) allocate_temp_buffer(2, sizeof(*temp_values), "each() block values", out, out_size);
      if (temp_values == NULL) {
        free(result);
        return 1;
      }

      oosh_value_init(result);
      result->kind = OOSH_VALUE_LIST;

      for (i = 0; i < value->list.count; ++i) {
        OoshValueItem item;

        if (oosh_value_set_from_item(&temp_values[0], &value->list.items[i]) != 0) {
          free(temp_values);
          oosh_value_free(result);
          free(result);
          snprintf(out, out_size, "unable to prepare each() block argument");
          return 1;
        }
        if (evaluate_block(shell, &block, &temp_values[0], 1, &temp_values[1], out, out_size) != 0) {
          oosh_value_free(&temp_values[0]);
          free(temp_values);
          oosh_value_free(result);
          free(result);
          return 1;
        }
        if (set_item_from_value(&temp_values[1], &item, out, out_size) != 0 || oosh_value_list_append_item(result, &item) != 0) {
          oosh_value_item_free(&item);
          oosh_value_free(&temp_values[0]);
          oosh_value_free(&temp_values[1]);
          free(temp_values);
          oosh_value_free(result);
          free(result);
          snprintf(out, out_size, "each() output list is too large");
          return 1;
        }
        oosh_value_item_free(&item);
        oosh_value_free(&temp_values[0]);
        oosh_value_free(&temp_values[1]);
      }

      oosh_value_free(value);
      if (oosh_value_copy(value, result) != 0) {
        oosh_value_free(result);
        free(temp_values);
        free(result);
        snprintf(out, out_size, "unable to finalize each() block result");
        return 1;
      }
      oosh_value_free(result);
      free(temp_values);
      free(result);
      return 0;
    }

    out[0] = '\0';
  }

  if (parse_each_selector(stage->raw_args, &selector, out, out_size) != 0) {
    return 1;
  }

  result = (OoshValue *) allocate_temp_buffer(1, sizeof(*result), "each() selector result", out, out_size);
  if (result == NULL) {
    return 1;
  }

  oosh_value_init(result);
  result->kind = OOSH_VALUE_LIST;

  for (i = 0; i < value->list.count; ++i) {
    OoshValue *mapped;
    OoshValueItem item;

    mapped = (OoshValue *) allocate_temp_buffer(1, sizeof(*mapped), "each() mapped value", out, out_size);
    if (mapped == NULL) {
      free(result);
      return 1;
    }

    if (apply_each_selector_to_item(shell, &selector, &value->list.items[i], mapped, out, out_size) != 0) {
      oosh_value_free(mapped);
      free(mapped);
      oosh_value_free(result);
      free(result);
      return 1;
    }
    if (set_item_from_value(mapped, &item, out, out_size) != 0 || oosh_value_list_append_item(result, &item) != 0) {
      oosh_value_item_free(&item);
      oosh_value_free(mapped);
      free(mapped);
      oosh_value_free(result);
      free(result);
      snprintf(out, out_size, "each() output list is too large");
      return 1;
    }
    oosh_value_item_free(&item);
    oosh_value_free(mapped);
    free(mapped);
  }

  oosh_value_free(value);
  if (oosh_value_copy(value, result) != 0) {
    oosh_value_free(result);
    free(result);
    snprintf(out, out_size, "unable to finalize each() result");
    return 1;
  }
  oosh_value_free(result);
  free(result);
  return 0;
}

static int apply_pipeline_stage(OoshShell *shell, OoshValue *value, const OoshPipelineStageNode *stage, char *out, size_t out_size) {
  const OoshPipelineStageDef *handler;

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

  if (strcmp(stage->name, "split") == 0) {
    return apply_split_stage(shell, value, stage, out, out_size);
  }

  if (strcmp(stage->name, "join") == 0) {
    return apply_join_stage(shell, value, stage, out, out_size);
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

  handler = oosh_shell_find_pipeline_stage(shell, stage->name);
  if (handler != NULL && handler->fn != NULL) {
    return handler->fn(shell, value, stage->raw_args, out, out_size);
  }

  snprintf(out, out_size, "unknown pipeline stage: %s", stage->name);
  return 1;
}

static int populate_process_spec_from_stage(OoshShell *shell, const OoshCommandStageNode *stage, OoshPlatformProcessSpec *spec, char *out, size_t out_size) {
  char expanded_argv[OOSH_MAX_ARGS][OOSH_MAX_TOKEN];
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
  for (i = 0; i < (size_t) expanded_argc && i < OOSH_MAX_ARGS; ++i) {
    copy_string(spec->argv[i], sizeof(spec->argv[i]), expanded_argv[i]);
  }

  for (i = 0; i < stage->redirection_count; ++i) {
    char expanded_target[OOSH_MAX_TOKEN];
    OoshPlatformRedirectionSpec *redirect;

    if (spec->redirection_count >= OOSH_MAX_REDIRECTIONS) {
      snprintf(out, out_size, "too many process redirections");
      return 1;
    }

    redirect = &spec->redirections[spec->redirection_count++];
    memset(redirect, 0, sizeof(*redirect));
    redirect->fd = stage->redirections[i].fd;
    redirect->target_fd = stage->redirections[i].target_fd;
    redirect->heredoc_strip_tabs = stage->redirections[i].heredoc_strip_tabs;

    switch (stage->redirections[i].kind) {
      case OOSH_REDIRECT_INPUT:
      case OOSH_REDIRECT_FD_INPUT:
        if (expand_single_word(shell, stage->redirections[i].raw_target, OOSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        redirect->input_mode = 1;
        copy_string(redirect->path, sizeof(redirect->path), expanded_target);
        break;
      case OOSH_REDIRECT_OUTPUT_TRUNCATE:
      case OOSH_REDIRECT_FD_OUTPUT_TRUNCATE:
        if (expand_single_word(shell, stage->redirections[i].raw_target, OOSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        redirect->input_mode = 0;
        redirect->append_mode = 0;
        copy_string(redirect->path, sizeof(redirect->path), expanded_target);
        break;
      case OOSH_REDIRECT_OUTPUT_APPEND:
      case OOSH_REDIRECT_FD_OUTPUT_APPEND:
        if (expand_single_word(shell, stage->redirections[i].raw_target, OOSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        redirect->input_mode = 0;
        redirect->append_mode = 1;
        copy_string(redirect->path, sizeof(redirect->path), expanded_target);
        break;
      case OOSH_REDIRECT_ERROR_TRUNCATE:
        if (expand_single_word(shell, stage->redirections[i].raw_target, OOSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        redirect->input_mode = 0;
        redirect->append_mode = 0;
        copy_string(redirect->path, sizeof(redirect->path), expanded_target);
        break;
      case OOSH_REDIRECT_ERROR_APPEND:
        if (expand_single_word(shell, stage->redirections[i].raw_target, OOSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        redirect->input_mode = 0;
        redirect->append_mode = 1;
        copy_string(redirect->path, sizeof(redirect->path), expanded_target);
        break;
      case OOSH_REDIRECT_ERROR_TO_OUTPUT:
      case OOSH_REDIRECT_FD_DUP_INPUT:
      case OOSH_REDIRECT_FD_DUP_OUTPUT:
        break;
      case OOSH_REDIRECT_FD_CLOSE:
        redirect->close_target = 1;
        break;
      case OOSH_REDIRECT_HEREDOC:
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

static int redirection_affects_stdin(const OoshRedirectionNode *redirection) {
  if (redirection == NULL) {
    return 0;
  }

  switch (redirection->kind) {
    case OOSH_REDIRECT_INPUT:
    case OOSH_REDIRECT_HEREDOC:
      return 1;
    case OOSH_REDIRECT_FD_INPUT:
    case OOSH_REDIRECT_FD_DUP_INPUT:
    case OOSH_REDIRECT_FD_CLOSE:
      return redirection->fd == 0;
    default:
      return 0;
  }
}

static int process_spec_has_stdin_redirection(const OoshPlatformProcessSpec *spec) {
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
  OoshShell *shell,
  const OoshRedirectionNode *redirection,
  OoshPlatformProcessSpec *spec,
  char *out,
  size_t out_size
) {
  OoshPlatformRedirectionSpec *redirect;

  if (shell == NULL || redirection == NULL || spec == NULL || out == NULL || out_size == 0) {
    return 1;
  }
  if (spec->redirection_count >= OOSH_MAX_REDIRECTIONS) {
    snprintf(out, out_size, "too many process redirections");
    return 1;
  }

  redirect = &spec->redirections[spec->redirection_count++];
  memset(redirect, 0, sizeof(*redirect));
  redirect->fd = redirection->fd;
  redirect->target_fd = redirection->target_fd;
  redirect->heredoc_strip_tabs = redirection->heredoc_strip_tabs;

  switch (redirection->kind) {
    case OOSH_REDIRECT_INPUT:
    case OOSH_REDIRECT_FD_INPUT:
      if (expand_single_word(shell, redirection->raw_target, OOSH_EXPAND_MODE_REDIRECT_TARGET, redirect->path, sizeof(redirect->path), out, out_size) != 0) {
        return 1;
      }
      redirect->input_mode = 1;
      return 0;
    case OOSH_REDIRECT_HEREDOC:
      redirect->input_mode = 1;
      copy_string(redirect->text, sizeof(redirect->text), redirection->heredoc_body);
      return 0;
    case OOSH_REDIRECT_FD_DUP_INPUT:
      return 0;
    case OOSH_REDIRECT_FD_CLOSE:
      redirect->close_target = 1;
      return 0;
    default:
      snprintf(out, out_size, "unsupported input redirection kind");
      return 1;
  }
}

static int apply_inherited_input_redirection(
  OoshShell *shell,
  OoshPlatformProcessSpec *spec,
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

int oosh_execute_external_command(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  OoshPlatformProcessSpec spec;
  int exit_code = 0;
  int status;
  int i;

  if (shell == NULL || argv == NULL || out == NULL || out_size == 0 || argc <= 0) {
    return 1;
  }

  memset(&spec, 0, sizeof(spec));
  spec.argc = argc;

  for (i = 0; i < argc && i < OOSH_MAX_ARGS; ++i) {
    copy_string(spec.argv[i], sizeof(spec.argv[i]), argv[i]);
  }
  if (apply_inherited_input_redirection(shell, &spec, out, out_size) != 0) {
    return 1;
  }

  status = oosh_platform_run_process_pipeline(shell->cwd, &spec, 1, out, out_size, &exit_code);
  if (status != 0) {
    if (out[0] == '\0') {
      snprintf(out, out_size, "unable to execute external command: %s", argv[0]);
    }
    return 1;
  }

  return exit_code == 0 ? 0 : 1;
}

static int execute_builtin_with_redirection(
  OoshShell *shell,
  const OoshCommandDef *command_def,
  const OoshCommandStageNode *stage,
  char *out,
  size_t out_size
) {
  char expanded_argv[OOSH_MAX_ARGS][OOSH_MAX_TOKEN];
  char *argv[OOSH_MAX_ARGS];
  char command_output[OOSH_MAX_OUTPUT];
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
    char expanded_target[OOSH_MAX_TOKEN];

    switch (stage->redirections[i].kind) {
      case OOSH_REDIRECT_INPUT:
        break;
      case OOSH_REDIRECT_OUTPUT_TRUNCATE:
        if (expand_single_word(shell, stage->redirections[i].raw_target, OOSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        if (write_text_file(expanded_target, command_output, 0, out, out_size) != 0) {
          return 1;
        }
        command_output[0] = '\0';
        break;
      case OOSH_REDIRECT_OUTPUT_APPEND:
        if (expand_single_word(shell, stage->redirections[i].raw_target, OOSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        if (write_text_file(expanded_target, command_output, 1, out, out_size) != 0) {
          return 1;
        }
        command_output[0] = '\0';
        break;
      case OOSH_REDIRECT_ERROR_TRUNCATE:
      case OOSH_REDIRECT_ERROR_APPEND:
      case OOSH_REDIRECT_ERROR_TO_OUTPUT:
        break;
      default:
        snprintf(out, out_size, "unsupported redirection kind for builtin");
        return 1;
    }
  }

  copy_string(out, out_size, command_output);
  return 0;
}

static int execute_function_definition(
  OoshShell *shell,
  const OoshFunctionCommandNode *function_node,
  char *out,
  size_t out_size
) {
  if (shell == NULL || function_node == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (oosh_shell_set_function(shell, function_node) != 0) {
    snprintf(out, out_size, "unable to define function: %s", function_node->name);
    return 1;
  }

  out[0] = '\0';
  return 0;
}

static int execute_class_definition(
  OoshShell *shell,
  const OoshClassCommandNode *class_node,
  char *out,
  size_t out_size
) {
  if (shell == NULL || class_node == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  return oosh_shell_set_class(shell, class_node, out, out_size);
}

static int execute_shell_function(
  OoshShell *shell,
  const OoshShellFunction *function_def,
  const OoshSimpleCommandNode *command,
  char *out,
  size_t out_size
) {
  OoshFunctionScopeSnapshot *snapshot;
  int status;

  if (shell == NULL || function_def == NULL || command == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  snapshot = (OoshFunctionScopeSnapshot *) allocate_temp_buffer(1, sizeof(*snapshot), "function scope snapshot", out, out_size);
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
  status = oosh_shell_execute_line(shell, function_def->body, out, out_size);
  shell->function_depth--;
  if (status == 0 && shell->control_signal == OOSH_CONTROL_SIGNAL_RETURN) {
    oosh_shell_clear_control_signal(shell);
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

static int execute_simple_command(OoshShell *shell, const OoshSimpleCommandNode *command, char *out, size_t out_size) {
  char expanded_argv[OOSH_MAX_ARGS][OOSH_MAX_TOKEN];
  char *argv[OOSH_MAX_ARGS];
  const OoshCommandDef *command_def;
  const OoshShellFunction *function_def;
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

  function_def = oosh_shell_find_function(shell, expanded_argv[0]);
  command_def = find_registered_command(shell, expanded_argv[0]);
  build_command_argv(expanded_argv, expanded_argc, argv);

  if (command_def != NULL) {
    return command_def->fn(shell, expanded_argc, argv, out, out_size);
  }
  if (function_def != NULL) {
    OoshSimpleCommandNode expanded_command;
    int i;

    memset(&expanded_command, 0, sizeof(expanded_command));
    expanded_command.argc = expanded_argc;
    for (i = 0; i < expanded_argc; ++i) {
      copy_string(expanded_command.argv[i], sizeof(expanded_command.argv[i]), expanded_argv[i]);
      copy_string(expanded_command.raw_argv[i], sizeof(expanded_command.raw_argv[i]), expanded_argv[i]);
    }
    return execute_shell_function(shell, function_def, &expanded_command, out, out_size);
  }

  return oosh_execute_external_command(shell, expanded_argc, argv, out, out_size);
}

static int execute_shell_pipeline(OoshShell *shell, const OoshCommandPipelineNode *pipeline, char *out, size_t out_size) {
  OoshPlatformProcessSpec specs[OOSH_MAX_PIPELINE_STAGES];
  size_t i;
  int exit_code = 0;

  if (shell == NULL || pipeline == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  for (i = 0; i < pipeline->stage_count; ++i) {
    char expanded_name[OOSH_MAX_TOKEN];
    const OoshCommandDef *command_def;

    if (expand_single_word(shell, pipeline->stages[i].raw_argv[0], OOSH_EXPAND_MODE_COMMAND_NAME, expanded_name, sizeof(expanded_name), out, out_size) != 0) {
      return 1;
    }

    command_def = find_registered_command(shell, expanded_name);
    if (command_def != NULL) {
      if (pipeline->stage_count == 1) {
        return execute_builtin_with_redirection(shell, command_def, &pipeline->stages[i], out, out_size);
      }

      snprintf(out, out_size, "built-in commands cannot be used inside shell pipelines yet: %s", expanded_name);
      return 1;
    }

    if (populate_process_spec_from_stage(shell, &pipeline->stages[i], &specs[i], out, out_size) != 0) {
      return 1;
    }
  }

  if (pipeline->stage_count > 0 && apply_inherited_input_redirection(shell, &specs[0], out, out_size) != 0) {
    return 1;
  }

  if (oosh_platform_run_process_pipeline(shell->cwd, specs, pipeline->stage_count, out, out_size, &exit_code) != 0) {
    if (out[0] == '\0') {
      snprintf(out, out_size, "failed to execute shell pipeline");
    }
    return 1;
  }

  return exit_code == 0 ? 0 : 1;
}

static int apply_compound_command_redirections(
  OoshShell *shell,
  const OoshRedirectionNode *redirections,
  size_t redirection_count,
  int status,
  const char *command_output,
  char *out,
  size_t out_size
) {
  char stdout_text[OOSH_MAX_OUTPUT];
  char stderr_text[OOSH_MAX_OUTPUT];
  size_t i;

  if (shell == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  copy_string(stdout_text, sizeof(stdout_text), status == 0 ? command_output : "");
  copy_string(stderr_text, sizeof(stderr_text), status == 0 ? "" : command_output);

  for (i = 0; i < redirection_count; ++i) {
    char expanded_target[OOSH_MAX_TOKEN];

    switch (redirections[i].kind) {
      case OOSH_REDIRECT_INPUT:
      case OOSH_REDIRECT_HEREDOC:
        break;
      case OOSH_REDIRECT_OUTPUT_TRUNCATE:
        if (expand_single_word(shell, redirections[i].raw_target, OOSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        if (write_text_file(expanded_target, stdout_text, 0, out, out_size) != 0) {
          return 1;
        }
        stdout_text[0] = '\0';
        break;
      case OOSH_REDIRECT_OUTPUT_APPEND:
        if (expand_single_word(shell, redirections[i].raw_target, OOSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        if (write_text_file(expanded_target, stdout_text, 1, out, out_size) != 0) {
          return 1;
        }
        stdout_text[0] = '\0';
        break;
      case OOSH_REDIRECT_ERROR_TRUNCATE:
        if (expand_single_word(shell, redirections[i].raw_target, OOSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        if (write_text_file(expanded_target, stderr_text, 0, out, out_size) != 0) {
          return 1;
        }
        stderr_text[0] = '\0';
        break;
      case OOSH_REDIRECT_ERROR_APPEND:
        if (expand_single_word(shell, redirections[i].raw_target, OOSH_EXPAND_MODE_REDIRECT_TARGET, expanded_target, sizeof(expanded_target), out, out_size) != 0) {
          return 1;
        }
        if (write_text_file(expanded_target, stderr_text, 1, out, out_size) != 0) {
          return 1;
        }
        stderr_text[0] = '\0';
        break;
      case OOSH_REDIRECT_ERROR_TO_OUTPUT:
        if (append_output_segment(stdout_text, sizeof(stdout_text), stderr_text) != 0) {
          snprintf(out, out_size, "combined command output too large");
          return 1;
        }
        stderr_text[0] = '\0';
        break;
      case OOSH_REDIRECT_FD_INPUT:
      case OOSH_REDIRECT_FD_DUP_INPUT:
      case OOSH_REDIRECT_FD_CLOSE:
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
  OoshShell *shell,
  const OoshCompoundCommandNode *compound,
  char *out,
  size_t out_size
) {
  char command_output[OOSH_MAX_OUTPUT];
  int previous_inherited_input_active;
  OoshRedirectionNode previous_inherited_input;
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
  status = oosh_shell_execute_line(shell, compound->body, command_output, sizeof(command_output));
  shell->inherited_input_active = previous_inherited_input_active;
  shell->inherited_input_redirection = previous_inherited_input;
  if (apply_compound_command_redirections(shell, compound->redirections, compound->redirection_count, status, command_output, out, out_size) != 0) {
    return 1;
  }

  return status;
}

static int execute_group_command(
  OoshShell *shell,
  const OoshCompoundCommandNode *compound,
  char *out,
  size_t out_size
) {
  return execute_compound_body(shell, compound, out, out_size);
}

static int execute_subshell_command(
  OoshShell *shell,
  const OoshCompoundCommandNode *compound,
  char *out,
  size_t out_size
) {
  OoshShellStateSnapshot *snapshot;
  char command_output[OOSH_MAX_OUTPUT];
  int status;

  if (shell == NULL || compound == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  snapshot = (OoshShellStateSnapshot *) allocate_temp_buffer(1, sizeof(*snapshot), "subshell state snapshot", out, out_size);
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

static int execute_object_pipeline(OoshShell *shell, const OoshObjectPipelineNode *pipeline, char *out, size_t out_size) {
  OoshValue *value;
  size_t i;
  int status;

  if (shell == NULL || pipeline == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  value = (OoshValue *) allocate_temp_buffer(1, sizeof(*value), "object pipeline value", out, out_size);
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

  status = oosh_value_render(value, out, out_size);
  free(value);
  return status;
}

static int evaluate_object_pipeline_value(OoshShell *shell, const OoshObjectPipelineNode *pipeline, OoshValue *value, char *out, size_t out_size) {
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

static int execute_value_expression(OoshShell *shell, const OoshValueSourceNode *source, char *out, size_t out_size) {
  OoshValue *value;
  int status;

  if (shell == NULL || source == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  value = (OoshValue *) allocate_temp_buffer(1, sizeof(*value), "value expression", out, out_size);
  if (value == NULL) {
    return 1;
  }

  if (evaluate_value_source(shell, source, value, out, out_size) != 0) {
    free(value);
    return 1;
  }

  status = oosh_value_render(value, out, out_size);
  free(value);
  return status;
}

static int evaluate_ast_value(OoshShell *shell, const OoshAst *ast, OoshValue *value, char *out, size_t out_size) {
  if (shell == NULL || ast == NULL || value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  oosh_value_init(value);

  switch (ast->kind) {
    case OOSH_AST_EMPTY:
      return 0;
    case OOSH_AST_VALUE_EXPRESSION:
      return evaluate_value_source(shell, &ast->as.value_expression, value, out, out_size);
    case OOSH_AST_OBJECT_EXPRESSION:
      return evaluate_object_expression_value(shell, &ast->as.object_expression, value, out, out_size);
    case OOSH_AST_OBJECT_PIPELINE:
      return evaluate_object_pipeline_value(shell, &ast->as.pipeline, value, out, out_size);
    default:
      snprintf(out, out_size, "expression does not produce a value");
      return 1;
  }
}

int oosh_evaluate_line_value(OoshShell *shell, const char *line, OoshValue *value, char *out, size_t out_size) {
  OoshAst ast;
  char trimmed[OOSH_MAX_LINE];
  char parse_error[OOSH_MAX_OUTPUT];
  const OoshValue *binding;

  if (shell == NULL || line == NULL || value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  copy_string(trimmed, sizeof(trimmed), line);
  trim_in_place(trimmed);
  oosh_value_init(value);

  if (trimmed[0] == '\0') {
    return 0;
  }

  binding = oosh_shell_get_binding(shell, trimmed);
  if (binding != NULL) {
    return oosh_value_copy(value, binding);
  }
  if (oosh_shell_find_class(shell, trimmed) != NULL) {
    oosh_value_set_class(value, trimmed);
    return 0;
  }

  if (oosh_parse_value_line(trimmed, &ast, parse_error, sizeof(parse_error)) != 0) {
    snprintf(out, out_size, "%s", parse_error[0] == '\0' ? "parse error" : parse_error);
    return 1;
  }

  return evaluate_ast_value(shell, &ast, value, out, out_size);
}

static int should_run_list_entry(OoshListCondition condition, int previous_status) {
  switch (condition) {
    case OOSH_LIST_CONDITION_ON_SUCCESS:
      return previous_status == 0;
    case OOSH_LIST_CONDITION_ON_FAILURE:
      return previous_status != 0;
    case OOSH_LIST_CONDITION_ALWAYS:
    default:
      return 1;
  }
}

typedef enum {
  OOSH_LOOP_CONTROL_NONE = 0,
  OOSH_LOOP_CONTROL_BREAK,
  OOSH_LOOP_CONTROL_CONTINUE,
  OOSH_LOOP_CONTROL_PROPAGATE
} OoshLoopControlAction;

static OoshLoopControlAction resolve_loop_control(OoshShell *shell) {
  OoshControlSignalKind kind;

  if (shell == NULL || shell->control_signal == OOSH_CONTROL_SIGNAL_NONE) {
    return OOSH_LOOP_CONTROL_NONE;
  }
  if (shell->control_signal == OOSH_CONTROL_SIGNAL_RETURN) {
    return OOSH_LOOP_CONTROL_PROPAGATE;
  }

  kind = shell->control_signal;
  if (shell->control_levels > 0) {
    shell->control_levels--;
  }

  if (shell->control_levels <= 0) {
    oosh_shell_clear_control_signal(shell);
    return kind == OOSH_CONTROL_SIGNAL_BREAK ? OOSH_LOOP_CONTROL_BREAK : OOSH_LOOP_CONTROL_CONTINUE;
  }

  return OOSH_LOOP_CONTROL_PROPAGATE;
}

static int execute_command_list(OoshShell *shell, const OoshCommandListNode *list, char *out, size_t out_size) {
  int previous_status = 0;
  size_t i;

  if (shell == NULL || list == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';

  for (i = 0; i < list->count; ++i) {
    char segment_output[OOSH_MAX_OUTPUT];

    if (!should_run_list_entry(list->entries[i].condition, previous_status)) {
      continue;
    }

    segment_output[0] = '\0';
    if (list->entries[i].run_in_background) {
      previous_status = oosh_shell_start_background_job(shell, list->entries[i].text, segment_output, sizeof(segment_output));
    } else {
      previous_status = oosh_shell_execute_line(shell, list->entries[i].text, segment_output, sizeof(segment_output));
    }

    if (append_output_segment(out, out_size, segment_output) != 0) {
      snprintf(out, out_size, "combined command output too large");
      return 1;
    }

    if (!shell->running) {
      break;
    }
    if (shell->control_signal != OOSH_CONTROL_SIGNAL_NONE) {
      break;
    }
  }

  return previous_status;
}

static int render_value_for_shell_var(const OoshValue *value, char *out, size_t out_size) {
  if (value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  switch (value->kind) {
    case OOSH_VALUE_STRING:
      copy_string(out, out_size, value->text);
      return 0;
    case OOSH_VALUE_NUMBER:
      snprintf(out, out_size, "%.15g", value->number);
      return 0;
    case OOSH_VALUE_BOOLEAN:
      copy_string(out, out_size, value->boolean ? "true" : "false");
      return 0;
    case OOSH_VALUE_OBJECT:
      copy_string(out, out_size, value->object.path);
      return 0;
    case OOSH_VALUE_BLOCK:
      copy_string(out, out_size, value->block.source);
      return 0;
    case OOSH_VALUE_LIST:
    case OOSH_VALUE_EMPTY:
    default:
      return oosh_value_render(value, out, out_size);
  }
}

static int evaluate_condition_status(OoshShell *shell, const char *text, int *out_status, char *out, size_t out_size) {
  OoshValue *value;
  int status;

  if (shell == NULL || text == NULL || out_status == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  value = (OoshValue *) allocate_temp_buffer(1, sizeof(*value), "condition value", out, out_size);
  if (value == NULL) {
    return 1;
  }

  status = oosh_evaluate_line_value(shell, text, value, out, out_size);
  if (status == 0) {
    *out_status = value_is_truthy(value) ? 0 : 1;
    free(value);
    out[0] = '\0';
    return 0;
  }

  free(value);
  out[0] = '\0';
  *out_status = oosh_shell_execute_line(shell, text, out, out_size);
  return 0;
}

static int evaluate_switch_operand(OoshShell *shell, const char *text, OoshValue *out_value, char *out, size_t out_size) {
  if (shell == NULL || text == NULL || out_value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  return evaluate_expression_atom(shell, text, out_value, out, out_size);
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
  char patterns[][OOSH_MAX_LINE],
  size_t max_patterns,
  size_t *out_count
) {
  OoshTokenStream stream;
  char error[OOSH_MAX_OUTPUT];
  size_t segment_start = 0;
  size_t count = 0;
  size_t i;

  if (patterns_text == NULL || patterns == NULL || max_patterns == 0 || out_count == NULL) {
    return 1;
  }

  *out_count = 0;
  error[0] = '\0';
  if (oosh_lex_line(patterns_text, &stream, error, sizeof(error)) != 0) {
    copy_trimmed_range(patterns_text, 0, strlen(patterns_text), patterns[0], OOSH_MAX_LINE);
    strip_matching_quotes(patterns[0]);
    *out_count = patterns[0][0] == '\0' ? 0 : 1;
    return 0;
  }

  for (i = 0; i < stream.count; ++i) {
    if (stream.tokens[i].kind == OOSH_TOKEN_SHELL_PIPE || stream.tokens[i].kind == OOSH_TOKEN_EOF) {
      if (count >= max_patterns) {
        return 1;
      }

      copy_trimmed_range(
        patterns_text,
        segment_start,
        stream.tokens[i].kind == OOSH_TOKEN_EOF ? strlen(patterns_text) : stream.tokens[i].position,
        patterns[count],
        OOSH_MAX_LINE
      );
      strip_matching_quotes(patterns[count]);
      if (patterns[count][0] != '\0') {
        count++;
      }

      if (stream.tokens[i].kind == OOSH_TOKEN_EOF) {
        break;
      }
      segment_start = stream.tokens[i].position + strlen(stream.tokens[i].raw);
    }
  }

  *out_count = count;
  return 0;
}

static int case_branch_matches(const char *patterns_text, const char *switch_text) {
  char patterns[OOSH_MAX_CASE_BRANCHES][OOSH_MAX_LINE];
  size_t pattern_count = 0;
  size_t i;

  if (patterns_text == NULL || switch_text == NULL) {
    return 0;
  }
  if (split_case_pattern_alternatives(patterns_text, patterns, OOSH_MAX_CASE_BRANCHES, &pattern_count) != 0) {
    return 0;
  }

  for (i = 0; i < pattern_count; ++i) {
    if (shell_pattern_matches(patterns[i], switch_text)) {
      return 1;
    }
  }

  return 0;
}

static int execute_if_command(OoshShell *shell, const OoshIfCommandNode *command, char *out, size_t out_size) {
  char segment_output[OOSH_MAX_OUTPUT];
  int condition_status;
  int branch_status = 0;

  if (shell == NULL || command == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  segment_output[0] = '\0';
  if (evaluate_condition_status(shell, command->condition, &condition_status, segment_output, sizeof(segment_output)) != 0) {
    return 1;
  }
  if (append_output_segment(out, out_size, segment_output) != 0) {
    snprintf(out, out_size, "combined command output too large");
    return 1;
  }

  if (!shell->running) {
    return condition_status;
  }
  if (shell->control_signal != OOSH_CONTROL_SIGNAL_NONE) {
    return condition_status;
  }

  if (condition_status == 0) {
    segment_output[0] = '\0';
    branch_status = oosh_shell_execute_line(shell, command->then_branch, segment_output, sizeof(segment_output));
    if (append_output_segment(out, out_size, segment_output) != 0) {
      snprintf(out, out_size, "combined command output too large");
      return 1;
    }
    return branch_status;
  }

  if (command->has_else_branch) {
    segment_output[0] = '\0';
    branch_status = oosh_shell_execute_line(shell, command->else_branch, segment_output, sizeof(segment_output));
    if (append_output_segment(out, out_size, segment_output) != 0) {
      snprintf(out, out_size, "combined command output too large");
      return 1;
    }
    return branch_status;
  }

  return 0;
}

static int execute_while_command(OoshShell *shell, const OoshWhileCommandNode *command, char *out, size_t out_size) {
  int last_body_status = 0;
  int final_status = 0;

  if (shell == NULL || command == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  shell->loop_depth++;
  while (shell->running) {
    char segment_output[OOSH_MAX_OUTPUT];
    int condition_status;

    segment_output[0] = '\0';
    if (evaluate_condition_status(shell, command->condition, &condition_status, segment_output, sizeof(segment_output)) != 0) {
      return 1;
    }
    if (append_output_segment(out, out_size, segment_output) != 0) {
      snprintf(out, out_size, "combined command output too large");
      return 1;
    }
    if (!shell->running) {
      final_status = condition_status;
      goto done;
    }
    if (shell->control_signal != OOSH_CONTROL_SIGNAL_NONE) {
      final_status = condition_status;
      goto done;
    }
    if (condition_status != 0) {
      final_status = last_body_status;
      goto done;
    }

    segment_output[0] = '\0';
    last_body_status = oosh_shell_execute_line(shell, command->body, segment_output, sizeof(segment_output));
    if (append_output_segment(out, out_size, segment_output) != 0) {
      shell->loop_depth--;
      snprintf(out, out_size, "combined command output too large");
      return 1;
    }

    switch (resolve_loop_control(shell)) {
      case OOSH_LOOP_CONTROL_BREAK:
        final_status = 0;
        goto done;
      case OOSH_LOOP_CONTROL_CONTINUE:
        last_body_status = 0;
        continue;
      case OOSH_LOOP_CONTROL_PROPAGATE:
        final_status = last_body_status;
        goto done;
      case OOSH_LOOP_CONTROL_NONE:
      default:
        break;
    }
  }

  final_status = last_body_status;

done:
  shell->loop_depth--;
  return final_status;
}

static int execute_until_command(OoshShell *shell, const OoshUntilCommandNode *command, char *out, size_t out_size) {
  int last_body_status = 0;
  int final_status = 0;

  if (shell == NULL || command == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  shell->loop_depth++;
  while (shell->running) {
    char segment_output[OOSH_MAX_OUTPUT];
    int condition_status;

    segment_output[0] = '\0';
    if (evaluate_condition_status(shell, command->condition, &condition_status, segment_output, sizeof(segment_output)) != 0) {
      return 1;
    }
    if (append_output_segment(out, out_size, segment_output) != 0) {
      snprintf(out, out_size, "combined command output too large");
      return 1;
    }
    if (!shell->running) {
      final_status = condition_status;
      goto done;
    }
    if (shell->control_signal != OOSH_CONTROL_SIGNAL_NONE) {
      final_status = condition_status;
      goto done;
    }
    if (condition_status == 0) {
      final_status = last_body_status;
      goto done;
    }

    segment_output[0] = '\0';
    last_body_status = oosh_shell_execute_line(shell, command->body, segment_output, sizeof(segment_output));
    if (append_output_segment(out, out_size, segment_output) != 0) {
      shell->loop_depth--;
      snprintf(out, out_size, "combined command output too large");
      return 1;
    }

    switch (resolve_loop_control(shell)) {
      case OOSH_LOOP_CONTROL_BREAK:
        final_status = 0;
        goto done;
      case OOSH_LOOP_CONTROL_CONTINUE:
        last_body_status = 0;
        continue;
      case OOSH_LOOP_CONTROL_PROPAGATE:
        final_status = last_body_status;
        goto done;
      case OOSH_LOOP_CONTROL_NONE:
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
  OoshShell *shell,
  const char *raw_word,
  OoshValue *iterable,
  char *out,
  size_t out_size
) {
  char expanded_words[OOSH_MAX_ARGS][OOSH_MAX_TOKEN];
  int expanded_count = 0;
  int i;

  if (shell == NULL || raw_word == NULL || iterable == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (oosh_expand_word(shell, raw_word, OOSH_EXPAND_MODE_COMMAND, expanded_words, OOSH_MAX_ARGS, &expanded_count, out, out_size) != 0) {
    return 1;
  }

  for (i = 0; i < expanded_count; ++i) {
    OoshValueItem item;

    if (set_item_from_text(expanded_words[i], &item) != 0 || oosh_value_list_append_item(iterable, &item) != 0) {
      snprintf(out, out_size, "unable to append for-loop item");
      return 1;
    }
  }

  return 0;
}

static int evaluate_for_source(OoshShell *shell, const char *text, OoshValue *iterable, char *out, size_t out_size) {
  OoshValue *value;
  int status;

  if (shell == NULL || text == NULL || iterable == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  value = (OoshValue *) allocate_temp_buffer(1, sizeof(*value), "for-loop source", out, out_size);
  if (value == NULL) {
    return 1;
  }

  status = oosh_evaluate_line_value(shell, text, value, out, out_size);
  if (status == 0) {
    *iterable = *value;
    free(value);
    return 0;
  }

  free(value);
  out[0] = '\0';

  {
    OoshTokenStream stream;
    char parse_error[OOSH_MAX_OUTPUT];
    size_t i;

    if (oosh_lex_line(text, &stream, parse_error, sizeof(parse_error)) != 0) {
      snprintf(out, out_size, "%s", parse_error[0] == '\0' ? "invalid for-loop source" : parse_error);
      return 1;
    }

    oosh_value_init(iterable);
    iterable->kind = OOSH_VALUE_LIST;
    for (i = 0; i < stream.count; ++i) {
      if (stream.tokens[i].kind == OOSH_TOKEN_EOF) {
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

static int assign_for_loop_variable(OoshShell *shell, const char *name, const OoshValue *value, char *out, size_t out_size) {
  char rendered[OOSH_MAX_OUTPUT];

  if (shell == NULL || name == NULL || value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (oosh_shell_set_binding(shell, name, value) != 0) {
    snprintf(out, out_size, "unable to bind for-loop variable: %s", name);
    return 1;
  }

  if (render_value_for_shell_var(value, rendered, sizeof(rendered)) != 0) {
    snprintf(out, out_size, "unable to render for-loop variable: %s", name);
    return 1;
  }

  if (oosh_shell_set_var(shell, name, rendered, 0) != 0) {
    snprintf(out, out_size, "unable to assign for-loop variable: %s", name);
    return 1;
  }

  return 0;
}

static int execute_for_command(OoshShell *shell, const OoshForCommandNode *command, char *out, size_t out_size) {
  OoshValue *iterable;
  int last_body_status = 0;
  int final_status = 0;
  size_t i;

  if (shell == NULL || command == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  iterable = (OoshValue *) allocate_temp_buffer(1, sizeof(*iterable), "for-loop iterable", out, out_size);
  if (iterable == NULL) {
    return 1;
  }

  if (evaluate_for_source(shell, command->source, iterable, out, out_size) != 0) {
    free(iterable);
    return 1;
  }

  out[0] = '\0';
  shell->loop_depth++;
  if (iterable->kind == OOSH_VALUE_LIST) {
    for (i = 0; i < iterable->list.count && shell->running; ++i) {
      OoshValue current_value;
      char segment_output[OOSH_MAX_OUTPUT];

      if (oosh_value_set_from_item(&current_value, &iterable->list.items[i]) != 0) {
        free(iterable);
        snprintf(out, out_size, "unable to prepare for-loop item");
        return 1;
      }
      if (assign_for_loop_variable(shell, command->variable, &current_value, out, out_size) != 0) {
        shell->loop_depth--;
        free(iterable);
        return 1;
      }

      segment_output[0] = '\0';
      last_body_status = oosh_shell_execute_line(shell, command->body, segment_output, sizeof(segment_output));
      if (append_output_segment(out, out_size, segment_output) != 0) {
        shell->loop_depth--;
        free(iterable);
        snprintf(out, out_size, "combined command output too large");
        return 1;
      }

      switch (resolve_loop_control(shell)) {
        case OOSH_LOOP_CONTROL_BREAK:
          final_status = 0;
          free(iterable);
          shell->loop_depth--;
          return final_status;
        case OOSH_LOOP_CONTROL_CONTINUE:
          last_body_status = 0;
          continue;
        case OOSH_LOOP_CONTROL_PROPAGATE:
          final_status = last_body_status;
          free(iterable);
          shell->loop_depth--;
          return final_status;
        case OOSH_LOOP_CONTROL_NONE:
        default:
          break;
      }
    }
  } else if (iterable->kind != OOSH_VALUE_EMPTY) {
    char segment_output[OOSH_MAX_OUTPUT];

    if (assign_for_loop_variable(shell, command->variable, iterable, out, out_size) != 0) {
      shell->loop_depth--;
      free(iterable);
      return 1;
    }

    segment_output[0] = '\0';
    last_body_status = oosh_shell_execute_line(shell, command->body, segment_output, sizeof(segment_output));
    if (append_output_segment(out, out_size, segment_output) != 0) {
      shell->loop_depth--;
      free(iterable);
      snprintf(out, out_size, "combined command output too large");
      return 1;
    }

    switch (resolve_loop_control(shell)) {
      case OOSH_LOOP_CONTROL_BREAK:
      case OOSH_LOOP_CONTROL_CONTINUE:
        last_body_status = 0;
        break;
      case OOSH_LOOP_CONTROL_PROPAGATE:
        shell->loop_depth--;
        free(iterable);
        return last_body_status;
      case OOSH_LOOP_CONTROL_NONE:
      default:
        break;
    }
  }

  shell->loop_depth--;
  free(iterable);
  return last_body_status;
}

static int execute_switch_command(OoshShell *shell, const OoshSwitchCommandNode *command, char *out, size_t out_size) {
  OoshValue *switch_value;
  size_t i;

  if (shell == NULL || command == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  switch_value = (OoshValue *) allocate_temp_buffer(1, sizeof(*switch_value), "switch value", out, out_size);
  if (switch_value == NULL) {
    return 1;
  }

  if (evaluate_switch_operand(shell, command->expression, switch_value, out, out_size) != 0) {
    oosh_value_free(switch_value);
    free(switch_value);
    return 1;
  }

  out[0] = '\0';
  for (i = 0; i < command->case_count; ++i) {
    OoshValue *case_value;
    int matches = 0;

    case_value = (OoshValue *) allocate_temp_buffer(1, sizeof(*case_value), "switch case value", out, out_size);
    if (case_value == NULL) {
      free(switch_value);
      return 1;
    }

    if (evaluate_switch_operand(shell, command->cases[i].match, case_value, out, out_size) != 0) {
      oosh_value_free(case_value);
      free(case_value);
      oosh_value_free(switch_value);
      free(switch_value);
      return 1;
    }

    if (compare_rendered_values(switch_value, case_value, &matches, out, out_size) != 0) {
      oosh_value_free(case_value);
      free(case_value);
      oosh_value_free(switch_value);
      free(switch_value);
      return 1;
    }

    oosh_value_free(case_value);
    free(case_value);
    if (matches) {
      int status;

      status = oosh_shell_execute_line(shell, command->cases[i].body, out, out_size);
      oosh_value_free(switch_value);
      free(switch_value);
      return status;
    }
  }

  oosh_value_free(switch_value);
  free(switch_value);
  if (command->has_default_branch) {
    return oosh_shell_execute_line(shell, command->default_branch, out, out_size);
  }

  return 0;
}

static int execute_case_command(OoshShell *shell, const OoshCaseCommandNode *command, char *out, size_t out_size) {
  OoshValue *case_value;
  char rendered[OOSH_MAX_OUTPUT];
  size_t i;

  if (shell == NULL || command == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  case_value = (OoshValue *) allocate_temp_buffer(1, sizeof(*case_value), "case value", out, out_size);
  if (case_value == NULL) {
    return 1;
  }

  if (evaluate_switch_operand(shell, command->expression, case_value, out, out_size) != 0) {
    oosh_value_free(case_value);
    free(case_value);
    return 1;
  }
  if (oosh_value_render(case_value, rendered, sizeof(rendered)) != 0) {
    oosh_value_free(case_value);
    free(case_value);
    snprintf(out, out_size, "unable to render case expression");
    return 1;
  }

  oosh_value_free(case_value);
  free(case_value);
  out[0] = '\0';

  for (i = 0; i < command->branch_count; ++i) {
    if (case_branch_matches(command->branches[i].patterns, rendered)) {
      if (command->branches[i].body[0] == '\0') {
        return 0;
      }
      return oosh_shell_execute_line(shell, command->branches[i].body, out, out_size);
    }
  }

  return 0;
}

int oosh_execute_ast(OoshShell *shell, const OoshAst *ast, char *out, size_t out_size) {
  if (shell == NULL || ast == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';

  switch (ast->kind) {
    case OOSH_AST_EMPTY:
      return 0;
    case OOSH_AST_SIMPLE_COMMAND:
      return execute_simple_command(shell, &ast->as.command, out, out_size);
    case OOSH_AST_VALUE_EXPRESSION:
      return execute_value_expression(shell, &ast->as.value_expression, out, out_size);
    case OOSH_AST_OBJECT_EXPRESSION:
      return evaluate_object_expression_text(shell, &ast->as.object_expression, out, out_size);
    case OOSH_AST_OBJECT_PIPELINE:
      return execute_object_pipeline(shell, &ast->as.pipeline, out, out_size);
    case OOSH_AST_COMMAND_PIPELINE:
      return execute_shell_pipeline(shell, &ast->as.command_pipeline, out, out_size);
    case OOSH_AST_COMMAND_LIST:
      return execute_command_list(shell, &ast->as.command_list, out, out_size);
    case OOSH_AST_GROUP_COMMAND:
      return execute_group_command(shell, &ast->as.group_command, out, out_size);
    case OOSH_AST_SUBSHELL_COMMAND:
      return execute_subshell_command(shell, &ast->as.subshell_command, out, out_size);
    case OOSH_AST_IF_COMMAND:
      return execute_if_command(shell, &ast->as.if_command, out, out_size);
    case OOSH_AST_WHILE_COMMAND:
      return execute_while_command(shell, &ast->as.while_command, out, out_size);
    case OOSH_AST_UNTIL_COMMAND:
      return execute_until_command(shell, &ast->as.until_command, out, out_size);
    case OOSH_AST_FOR_COMMAND:
      return execute_for_command(shell, &ast->as.for_command, out, out_size);
    case OOSH_AST_CASE_COMMAND:
      return execute_case_command(shell, &ast->as.case_command, out, out_size);
    case OOSH_AST_SWITCH_COMMAND:
      return execute_switch_command(shell, &ast->as.switch_command, out, out_size);
    case OOSH_AST_FUNCTION_COMMAND:
      return execute_function_definition(shell, &ast->as.function_command, out, out_size);
    case OOSH_AST_CLASS_COMMAND:
      return execute_class_definition(shell, &ast->as.class_command, out, out_size);
    default:
      snprintf(out, out_size, "unsupported AST node kind");
      return 1;
  }
}
