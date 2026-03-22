#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arksh/object.h"
#include "arksh/platform.h"

static void copy_string(char *dest, size_t dest_size, const char *src) {
  if (dest_size == 0) {
    return;
  }

  snprintf(dest, dest_size, "%s", src == NULL ? "" : src);
}

static void append_line(char *out, size_t out_size, const char *line) {
  if (out == NULL || out_size == 0 || line == NULL) {
    return;
  }

  if (out[0] != '\0') {
    snprintf(out + strlen(out), out_size - strlen(out), "\n");
  }
  snprintf(out + strlen(out), out_size - strlen(out), "%s", line);
}

static int parse_limit(const char *text, size_t *out_limit) {
  char *endptr = NULL;
  unsigned long value;

  if (text == NULL || text[0] == '\0') {
    return 1;
  }

  value = strtoul(text, &endptr, 10);
  if (endptr == text || *endptr != '\0') {
    return 1;
  }

  *out_limit = (size_t) value;
  return 0;
}

static void format_number(double number, char *out, size_t out_size) {
  if (out == NULL || out_size == 0) {
    return;
  }

  snprintf(out, out_size, "%.15g", number);
}

/* E6-S5: render a numeric value according to its explicit kind */
static void format_number_by_kind(ArkshValueKind kind, double number, char *out, size_t out_size) {
  if (out == NULL || out_size == 0) {
    return;
  }

  switch (kind) {
    case ARKSH_VALUE_INTEGER:
      snprintf(out, out_size, "%lld", (long long) number);
      break;
    case ARKSH_VALUE_FLOAT:
      snprintf(out, out_size, "%.7g", (double)(float) number);
      break;
    case ARKSH_VALUE_DOUBLE:
      snprintf(out, out_size, "%.15g", number);
      break;
    case ARKSH_VALUE_IMAGINARY: {
      char buf[64];
      snprintf(buf, sizeof(buf), "%.15g", number);
      snprintf(out, out_size, "%si", buf);
      break;
    }
    default:
      format_number(number, out, out_size);
      break;
  }
}

static int append_char(char *out, size_t out_size, size_t *length, char c) {
  if (out == NULL || length == NULL || out_size == 0) {
    return 1;
  }

  if (*length + 1 >= out_size) {
    return 1;
  }

  out[*length] = c;
  (*length)++;
  out[*length] = '\0';
  return 0;
}

static int append_text(char *out, size_t out_size, size_t *length, const char *text) {
  size_t text_len;

  if (out == NULL || length == NULL || text == NULL || out_size == 0) {
    return 1;
  }

  text_len = strlen(text);
  if (*length + text_len >= out_size) {
    return 1;
  }

  memcpy(out + *length, text, text_len);
  *length += text_len;
  out[*length] = '\0';
  return 0;
}

static int append_json_escaped_string(char *out, size_t out_size, const char *text) {
  size_t length = 0;
  size_t i;

  if (out == NULL || out_size == 0 || text == NULL) {
    return 1;
  }

  out[0] = '\0';
  if (append_char(out, out_size, &length, '"') != 0) {
    return 1;
  }

  for (i = 0; text[i] != '\0'; ++i) {
    unsigned char c = (unsigned char) text[i];

    switch (c) {
      case '"':
        if (append_text(out, out_size, &length, "\\\"") != 0) {
          return 1;
        }
        break;
      case '\\':
        if (append_text(out, out_size, &length, "\\\\") != 0) {
          return 1;
        }
        break;
      case '\b':
        if (append_text(out, out_size, &length, "\\b") != 0) {
          return 1;
        }
        break;
      case '\f':
        if (append_text(out, out_size, &length, "\\f") != 0) {
          return 1;
        }
        break;
      case '\n':
        if (append_text(out, out_size, &length, "\\n") != 0) {
          return 1;
        }
        break;
      case '\r':
        if (append_text(out, out_size, &length, "\\r") != 0) {
          return 1;
        }
        break;
      case '\t':
        if (append_text(out, out_size, &length, "\\t") != 0) {
          return 1;
        }
        break;
      default:
        if (c < 0x20) {
          return 1;
        }
        if (append_char(out, out_size, &length, (char) c) != 0) {
          return 1;
        }
        break;
    }
  }

  return append_char(out, out_size, &length, '"');
}

void arksh_value_item_free(ArkshValueItem *item) {
  if (item == NULL) {
    return;
  }

  if (item->nested != NULL) {
    arksh_value_free(item->nested);
    free(item->nested);
    item->nested = NULL;
  }

  memset(item, 0, sizeof(*item));
  item->kind = ARKSH_VALUE_EMPTY;
}

void arksh_value_free(ArkshValue *value) {
  size_t i;

  if (value == NULL) {
    return;
  }

  if (value->kind == ARKSH_VALUE_LIST) {
    for (i = 0; i < value->list.count; ++i) {
      arksh_value_item_free(&value->list.items[i]);
    }
  } else if (value->kind == ARKSH_VALUE_MAP || value->kind == ARKSH_VALUE_DICT) {
    for (i = 0; i < value->map.count; ++i) {
      arksh_value_item_free(&value->map.entries[i].value);
    }
  } else if (value->kind == ARKSH_VALUE_MATRIX) {
    free(value->matrix);
  }

  memset(value, 0, sizeof(*value));
  value->kind = ARKSH_VALUE_EMPTY;
}

int arksh_value_item_copy(ArkshValueItem *dest, const ArkshValueItem *src);
int arksh_value_copy(ArkshValue *dest, const ArkshValue *src);

int arksh_value_item_copy(ArkshValueItem *dest, const ArkshValueItem *src) {
  if (dest == NULL || src == NULL) {
    return 1;
  }

  arksh_value_item_init(dest);
  dest->kind = src->kind;
  dest->number = src->number;
  dest->boolean = src->boolean;
  dest->object = src->object;
  dest->block = src->block;
  copy_string(dest->text, sizeof(dest->text), src->text);

  if ((src->kind == ARKSH_VALUE_LIST || src->kind == ARKSH_VALUE_MAP || src->kind == ARKSH_VALUE_DICT) && src->nested != NULL) {
    dest->nested = (ArkshValue *) calloc(1, sizeof(*dest->nested));
    if (dest->nested == NULL) {
      arksh_value_item_free(dest);
      return 1;
    }
    if (arksh_value_copy(dest->nested, src->nested) != 0) {
      arksh_value_item_free(dest);
      return 1;
    }
  }

  return 0;
}

int arksh_value_copy(ArkshValue *dest, const ArkshValue *src) {
  size_t i;

  if (dest == NULL || src == NULL) {
    return 1;
  }

  arksh_value_init(dest);
  dest->kind = src->kind;
  dest->number = src->number;
  dest->boolean = src->boolean;
  dest->object = src->object;
  dest->block = src->block;
  copy_string(dest->text, sizeof(dest->text), src->text);

  if (src->kind == ARKSH_VALUE_LIST) {
    for (i = 0; i < src->list.count; ++i) {
      if (arksh_value_item_copy(&dest->list.items[dest->list.count], &src->list.items[i]) != 0) {
        arksh_value_free(dest);
        return 1;
      }
      dest->list.count++;
    }
  } else if (src->kind == ARKSH_VALUE_MAP || src->kind == ARKSH_VALUE_DICT) {
    for (i = 0; i < src->map.count; ++i) {
      copy_string(dest->map.entries[i].key, sizeof(dest->map.entries[i].key), src->map.entries[i].key);
      if (arksh_value_item_copy(&dest->map.entries[i].value, &src->map.entries[i].value) != 0) {
        arksh_value_free(dest);
        return 1;
      }
      dest->map.count++;
    }
  } else if (src->kind == ARKSH_VALUE_MATRIX) {
    if (src->matrix != NULL) {
      dest->matrix = (ArkshMatrix *) calloc(1, sizeof(ArkshMatrix));
      if (dest->matrix == NULL) {
        arksh_value_init(dest);
        return 1;
      }
      *dest->matrix = *src->matrix;
    }
  }

  return 0;
}

int arksh_value_item_set_from_value(ArkshValueItem *item, const ArkshValue *value) {
  if (item == NULL || value == NULL) {
    return 1;
  }

  arksh_value_item_init(item);
  item->kind = value->kind;
  item->number = value->number;
  item->boolean = value->boolean;
  item->object = value->object;
  item->block = value->block;
  copy_string(item->text, sizeof(item->text), value->text);

  if (value->kind == ARKSH_VALUE_LIST || value->kind == ARKSH_VALUE_MAP || value->kind == ARKSH_VALUE_DICT) {
    item->nested = (ArkshValue *) calloc(1, sizeof(*item->nested));
    if (item->nested == NULL) {
      arksh_value_item_free(item);
      return 1;
    }
    if (arksh_value_copy(item->nested, value) != 0) {
      arksh_value_item_free(item);
      return 1;
    }
  }

  return 0;
}

static int value_item_to_json_text(const ArkshValueItem *item, char *out, size_t out_size) {
  if (item == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  switch (item->kind) {
    case ARKSH_VALUE_STRING:
      return append_json_escaped_string(out, out_size, item->text);
    case ARKSH_VALUE_NUMBER:
      format_number(item->number, out, out_size);
      return 0;
    case ARKSH_VALUE_BOOLEAN:
      copy_string(out, out_size, item->boolean ? "true" : "false");
      return 0;
    case ARKSH_VALUE_OBJECT:
      return append_json_escaped_string(out, out_size, item->object.path);
    case ARKSH_VALUE_BLOCK:
      return append_json_escaped_string(out, out_size, item->block.source);
    case ARKSH_VALUE_CLASS:
      return append_json_escaped_string(out, out_size, item->text);
    case ARKSH_VALUE_INSTANCE: {
      char label[ARKSH_MAX_OUTPUT];

      snprintf(label, sizeof(label), "%s#%d", item->text, (int) item->number);
      return append_json_escaped_string(out, out_size, label);
    }
    case ARKSH_VALUE_EMPTY:
      copy_string(out, out_size, "null");
      return 0;
    case ARKSH_VALUE_LIST:
    case ARKSH_VALUE_MAP:
    case ARKSH_VALUE_DICT:
      if (item->nested == NULL) {
        return 1;
      }
      return arksh_value_to_json(item->nested, out, out_size);
    /* E6-S5: explicit numeric sub-kinds → JSON number (imaginary as string) */
    case ARKSH_VALUE_INTEGER:
    case ARKSH_VALUE_FLOAT:
    case ARKSH_VALUE_DOUBLE:
      format_number_by_kind(item->kind, item->number, out, out_size);
      return 0;
    case ARKSH_VALUE_IMAGINARY: {
      char imag_buf[64];
      format_number_by_kind(ARKSH_VALUE_IMAGINARY, item->number, imag_buf, sizeof(imag_buf));
      return append_json_escaped_string(out, out_size, imag_buf);
    }
    default:
      return 1;
  }
}

static int value_to_json_text(const ArkshValue *value, char *out, size_t out_size) {
  size_t length = 0;
  size_t i;

  if (value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  switch (value->kind) {
    case ARKSH_VALUE_STRING:
      return append_json_escaped_string(out, out_size, value->text);
    case ARKSH_VALUE_NUMBER:
      format_number(value->number, out, out_size);
      return 0;
    case ARKSH_VALUE_BOOLEAN:
      copy_string(out, out_size, value->boolean ? "true" : "false");
      return 0;
    case ARKSH_VALUE_OBJECT:
      return append_json_escaped_string(out, out_size, value->object.path);
    case ARKSH_VALUE_BLOCK:
      return append_json_escaped_string(out, out_size, value->block.source);
    case ARKSH_VALUE_CLASS:
      return append_json_escaped_string(out, out_size, value->text);
    case ARKSH_VALUE_INSTANCE: {
      char label[ARKSH_MAX_OUTPUT];

      snprintf(label, sizeof(label), "%s#%d", value->text, (int) value->number);
      return append_json_escaped_string(out, out_size, label);
    }
    case ARKSH_VALUE_EMPTY:
      copy_string(out, out_size, "null");
      return 0;
    case ARKSH_VALUE_LIST:
      out[0] = '\0';
      if (append_char(out, out_size, &length, '[') != 0) {
        return 1;
      }
      for (i = 0; i < value->list.count; ++i) {
        char item_json[ARKSH_MAX_OUTPUT];

        if (value_item_to_json_text(&value->list.items[i], item_json, sizeof(item_json)) != 0) {
          return 1;
        }
        if (i > 0 && append_char(out, out_size, &length, ',') != 0) {
          return 1;
        }
        if (append_text(out, out_size, &length, item_json) != 0) {
          return 1;
        }
      }
      return append_char(out, out_size, &length, ']');
    case ARKSH_VALUE_MAP:
    case ARKSH_VALUE_DICT:
      out[0] = '\0';
      if (append_char(out, out_size, &length, '{') != 0) {
        return 1;
      }
      for (i = 0; i < value->map.count; ++i) {
        char key_json[ARKSH_MAX_OUTPUT];
        char item_json[ARKSH_MAX_OUTPUT];

        if (append_json_escaped_string(key_json, sizeof(key_json), value->map.entries[i].key) != 0 ||
            value_item_to_json_text(&value->map.entries[i].value, item_json, sizeof(item_json)) != 0) {
          return 1;
        }
        if (i > 0 && append_char(out, out_size, &length, ',') != 0) {
          return 1;
        }
        if (append_text(out, out_size, &length, key_json) != 0 ||
            append_char(out, out_size, &length, ':') != 0 ||
            append_text(out, out_size, &length, item_json) != 0) {
          return 1;
        }
      }
      return append_char(out, out_size, &length, '}');
    /* E6-S5: explicit numeric sub-kinds */
    case ARKSH_VALUE_INTEGER:
    case ARKSH_VALUE_FLOAT:
    case ARKSH_VALUE_DOUBLE:
      format_number_by_kind(value->kind, value->number, out, out_size);
      return 0;
    case ARKSH_VALUE_IMAGINARY: {
      char imag_buf[64];
      format_number_by_kind(ARKSH_VALUE_IMAGINARY, value->number, imag_buf, sizeof(imag_buf));
      return append_json_escaped_string(out, out_size, imag_buf);
    }
    default:
      return 1;
  }
}

static void json_skip_ws(const char **cursor) {
  if (cursor == NULL || *cursor == NULL) {
    return;
  }

  while (**cursor != '\0' && (**cursor == ' ' || **cursor == '\t' || **cursor == '\n' || **cursor == '\r')) {
    (*cursor)++;
  }
}

static int json_parse_literal(const char **cursor, const char *literal) {
  size_t literal_len;

  if (cursor == NULL || *cursor == NULL || literal == NULL) {
    return 1;
  }

  literal_len = strlen(literal);
  if (strncmp(*cursor, literal, literal_len) != 0) {
    return 1;
  }

  *cursor += literal_len;
  return 0;
}

static int json_parse_string(const char **cursor, char *out, size_t out_size, char *error, size_t error_size) {
  size_t length = 0;

  if (cursor == NULL || *cursor == NULL || out == NULL || out_size == 0 || error == NULL || error_size == 0) {
    return 1;
  }

  if (**cursor != '"') {
    snprintf(error, error_size, "expected JSON string");
    return 1;
  }

  (*cursor)++;
  out[0] = '\0';

  while (**cursor != '\0' && **cursor != '"') {
    char c = **cursor;

    if (c == '\\') {
      (*cursor)++;
      c = **cursor;
      if (c == '\0') {
        snprintf(error, error_size, "unterminated JSON escape");
        return 1;
      }
      switch (c) {
        case '"':
        case '\\':
        case '/':
          break;
        case 'b':
          c = '\b';
          break;
        case 'f':
          c = '\f';
          break;
        case 'n':
          c = '\n';
          break;
        case 'r':
          c = '\r';
          break;
        case 't':
          c = '\t';
          break;
        case 'u':
          snprintf(error, error_size, "unicode escapes are not supported yet");
          return 1;
        default:
          snprintf(error, error_size, "invalid JSON escape");
          return 1;
      }
    }

    if (append_char(out, out_size, &length, c) != 0) {
      snprintf(error, error_size, "JSON string is too large");
      return 1;
    }
    (*cursor)++;
  }

  if (**cursor != '"') {
    snprintf(error, error_size, "unterminated JSON string");
    return 1;
  }

  (*cursor)++;
  return 0;
}

static int json_is_number_delimiter(char c) {
  return c == '\0' || c == ',' || c == ']' || c == '}' || c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int json_parse_number(const char **cursor, double *out_number, char *error, size_t error_size) {
  char *endptr = NULL;

  if (cursor == NULL || *cursor == NULL || out_number == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  *out_number = strtod(*cursor, &endptr);
  if (endptr == *cursor || !json_is_number_delimiter(*endptr)) {
    snprintf(error, error_size, "invalid JSON number");
    return 1;
  }

  *cursor = endptr;
  return 0;
}

static int json_parse_value_internal(const char **cursor, ArkshValue *out_value, char *error, size_t error_size);
static int json_parse_object(const char **cursor, ArkshValue *out_value, char *error, size_t error_size);

static int json_parse_array(const char **cursor, ArkshValue *out_value, char *error, size_t error_size) {
  if (cursor == NULL || *cursor == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (**cursor != '[') {
    snprintf(error, error_size, "expected JSON array");
    return 1;
  }

  (*cursor)++;
  arksh_value_init(out_value);
  out_value->kind = ARKSH_VALUE_LIST;
  json_skip_ws(cursor);
  if (**cursor == ']') {
    (*cursor)++;
    return 0;
  }

  while (**cursor != '\0') {
    ArkshValue parsed_value;

    if (json_parse_value_internal(cursor, &parsed_value, error, error_size) != 0) {
      return 1;
    }
    if (arksh_value_list_append_value(out_value, &parsed_value) != 0) {
      arksh_value_free(&parsed_value);
      if (error[0] == '\0') {
        snprintf(error, error_size, "JSON array is too large");
      }
      return 1;
    }
    arksh_value_free(&parsed_value);

    json_skip_ws(cursor);
    if (**cursor == ']') {
      (*cursor)++;
      return 0;
    }
    if (**cursor != ',') {
      snprintf(error, error_size, "expected ',' or ']' in JSON array");
      return 1;
    }
    (*cursor)++;
    json_skip_ws(cursor);
  }

  snprintf(error, error_size, "unterminated JSON array");
  return 1;
}

static int json_parse_object(const char **cursor, ArkshValue *out_value, char *error, size_t error_size) {
  if (cursor == NULL || *cursor == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (**cursor != '{') {
    snprintf(error, error_size, "expected JSON object");
    return 1;
  }

  (*cursor)++;
  arksh_value_init(out_value);
  out_value->kind = ARKSH_VALUE_MAP;
  json_skip_ws(cursor);
  if (**cursor == '}') {
    (*cursor)++;
    return 0;
  }

  while (**cursor != '\0') {
    char key[ARKSH_MAX_NAME];
    ArkshValue parsed_value;

    if (**cursor != '"') {
      snprintf(error, error_size, "expected JSON object key");
      return 1;
    }
    if (json_parse_string(cursor, key, sizeof(key), error, error_size) != 0) {
      return 1;
    }

    json_skip_ws(cursor);
    if (**cursor != ':') {
      snprintf(error, error_size, "expected ':' after JSON object key");
      return 1;
    }
    (*cursor)++;
    json_skip_ws(cursor);

    if (json_parse_value_internal(cursor, &parsed_value, error, error_size) != 0) {
      return 1;
    }
    if (arksh_value_map_set(out_value, key, &parsed_value) != 0) {
      arksh_value_free(&parsed_value);
      snprintf(error, error_size, "JSON object is too large");
      return 1;
    }
    arksh_value_free(&parsed_value);

    json_skip_ws(cursor);
    if (**cursor == '}') {
      (*cursor)++;
      return 0;
    }
    if (**cursor != ',') {
      snprintf(error, error_size, "expected ',' or '}' in JSON object");
      return 1;
    }
    (*cursor)++;
    json_skip_ws(cursor);
  }

  snprintf(error, error_size, "unterminated JSON object");
  return 1;
}

static int json_parse_value_internal(const char **cursor, ArkshValue *out_value, char *error, size_t error_size) {
  if (cursor == NULL || *cursor == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  arksh_value_init(out_value);
  json_skip_ws(cursor);
  if (**cursor == '"') {
    if (json_parse_string(cursor, out_value->text, sizeof(out_value->text), error, error_size) != 0) {
      return 1;
    }
    out_value->kind = ARKSH_VALUE_STRING;
    return 0;
  }
  if (**cursor == '[') {
    return json_parse_array(cursor, out_value, error, error_size);
  }
  if (**cursor == '{') {
    return json_parse_object(cursor, out_value, error, error_size);
  }
  if (strncmp(*cursor, "true", 4) == 0) {
    if (json_parse_literal(cursor, "true") != 0) {
      return 1;
    }
    arksh_value_set_boolean(out_value, 1);
    return 0;
  }
  if (strncmp(*cursor, "false", 5) == 0) {
    if (json_parse_literal(cursor, "false") != 0) {
      return 1;
    }
    arksh_value_set_boolean(out_value, 0);
    return 0;
  }
  if (strncmp(*cursor, "null", 4) == 0) {
    if (json_parse_literal(cursor, "null") != 0) {
      return 1;
    }
    arksh_value_init(out_value);
    return 0;
  }

  if (**cursor == '-' || (**cursor >= '0' && **cursor <= '9')) {
    double number;

    if (json_parse_number(cursor, &number, error, error_size) != 0) {
      return 1;
    }
    arksh_value_set_number(out_value, number);
    return 0;
  }

  snprintf(error, error_size, "invalid JSON value");
  return 1;
}

static int render_list_item_brief(const ArkshValueItem *item, char *out, size_t out_size) {
  if (item == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  switch (item->kind) {
    case ARKSH_VALUE_STRING:
      copy_string(out, out_size, item->text);
      return 0;
    case ARKSH_VALUE_NUMBER:
      format_number(item->number, out, out_size);
      return 0;
    case ARKSH_VALUE_BOOLEAN:
      copy_string(out, out_size, item->boolean ? "true" : "false");
      return 0;
    case ARKSH_VALUE_OBJECT:
      snprintf(out, out_size, "%s\t%s", arksh_object_kind_name(item->object.kind), item->object.path);
      return 0;
    case ARKSH_VALUE_BLOCK:
      copy_string(out, out_size, item->block.source);
      return 0;
    case ARKSH_VALUE_EMPTY:
      copy_string(out, out_size, "");
      return 0;
    case ARKSH_VALUE_LIST:
    case ARKSH_VALUE_MAP:
      if (item->nested != NULL && arksh_value_to_json(item->nested, out, out_size) == 0) {
        return 0;
      }
      snprintf(out, out_size, "<unsupported nested item>");
      return 1;
    /* E6-S5: explicit numeric sub-kinds */
    case ARKSH_VALUE_INTEGER:
    case ARKSH_VALUE_FLOAT:
    case ARKSH_VALUE_DOUBLE:
    case ARKSH_VALUE_IMAGINARY:
      format_number_by_kind(item->kind, item->number, out, out_size);
      return 0;
    default:
      snprintf(out, out_size, "<unsupported list item>");
      return 1;
  }
}

const char *arksh_object_kind_name(ArkshObjectKind kind) {
  switch (kind) {
    case ARKSH_OBJECT_PATH:
      return "path";
    case ARKSH_OBJECT_FILE:
      return "file";
    case ARKSH_OBJECT_DIRECTORY:
      return "directory";
    case ARKSH_OBJECT_DEVICE:
      return "device";
    case ARKSH_OBJECT_MOUNT_POINT:
      return "mount";
    case ARKSH_OBJECT_UNKNOWN:
    default:
      return "unknown";
  }
}

const char *arksh_value_kind_name(ArkshValueKind kind) {
  switch (kind) {
    case ARKSH_VALUE_STRING:
      return "string";
    case ARKSH_VALUE_NUMBER:
      return "number";
    case ARKSH_VALUE_BOOLEAN:
      return "bool";
    case ARKSH_VALUE_OBJECT:
      return "object";
    case ARKSH_VALUE_BLOCK:
      return "block";
    case ARKSH_VALUE_LIST:
      return "list";
    case ARKSH_VALUE_MAP:
      return "map";
    case ARKSH_VALUE_CLASS:
      return "class";
    case ARKSH_VALUE_INSTANCE:
      return "instance";
    case ARKSH_VALUE_INTEGER:
      return "integer";
    case ARKSH_VALUE_FLOAT:
      return "float";
    case ARKSH_VALUE_DOUBLE:
      return "double";
    case ARKSH_VALUE_IMAGINARY:
      return "imaginary";
    case ARKSH_VALUE_DICT:
      return "dict";
    case ARKSH_VALUE_MATRIX:
      return "matrix";
    case ARKSH_VALUE_EMPTY:
    default:
      return "empty";
  }
}

void arksh_value_init(ArkshValue *value) {
  if (value == NULL) {
    return;
  }

  memset(value, 0, sizeof(*value));
  value->kind = ARKSH_VALUE_EMPTY;
}

void arksh_value_item_init(ArkshValueItem *item) {
  if (item == NULL) {
    return;
  }

  memset(item, 0, sizeof(*item));
  item->kind = ARKSH_VALUE_EMPTY;
}

void arksh_value_set_string(ArkshValue *value, const char *text) {
  if (value == NULL) {
    return;
  }

  arksh_value_init(value);
  value->kind = ARKSH_VALUE_STRING;
  copy_string(value->text, sizeof(value->text), text);
}

void arksh_value_set_number(ArkshValue *value, double number) {
  if (value == NULL) {
    return;
  }

  arksh_value_init(value);
  value->kind = ARKSH_VALUE_NUMBER;
  value->number = number;
}

/* E6-S5: explicit numeric sub-kind setters */
void arksh_value_set_integer(ArkshValue *value, double number) {
  if (value == NULL) { return; }
  arksh_value_init(value);
  value->kind = ARKSH_VALUE_INTEGER;
  value->number = (double)(long long) number;
}

void arksh_value_set_float(ArkshValue *value, double number) {
  if (value == NULL) { return; }
  arksh_value_init(value);
  value->kind = ARKSH_VALUE_FLOAT;
  value->number = (double)(float) number;
}

void arksh_value_set_double(ArkshValue *value, double number) {
  if (value == NULL) { return; }
  arksh_value_init(value);
  value->kind = ARKSH_VALUE_DOUBLE;
  value->number = number;
}

void arksh_value_set_imaginary(ArkshValue *value, double number) {
  if (value == NULL) { return; }
  arksh_value_init(value);
  value->kind = ARKSH_VALUE_IMAGINARY;
  value->number = number;
}

void arksh_value_set_boolean(ArkshValue *value, int boolean) {
  if (value == NULL) {
    return;
  }

  arksh_value_init(value);
  value->kind = ARKSH_VALUE_BOOLEAN;
  value->boolean = boolean ? 1 : 0;
}

void arksh_value_set_object(ArkshValue *value, const ArkshObject *object) {
  if (value == NULL) {
    return;
  }

  arksh_value_init(value);
  value->kind = ARKSH_VALUE_OBJECT;
  if (object != NULL) {
    value->object = *object;
  }
}

void arksh_value_set_block(ArkshValue *value, const ArkshBlock *block) {
  if (value == NULL) {
    return;
  }

  arksh_value_init(value);
  value->kind = ARKSH_VALUE_BLOCK;
  if (block != NULL) {
    value->block = *block;
  }
}

void arksh_value_set_class(ArkshValue *value, const char *class_name) {
  if (value == NULL) {
    return;
  }

  arksh_value_init(value);
  value->kind = ARKSH_VALUE_CLASS;
  copy_string(value->text, sizeof(value->text), class_name);
}

void arksh_value_set_instance(ArkshValue *value, const char *class_name, int instance_id) {
  if (value == NULL) {
    return;
  }

  arksh_value_init(value);
  value->kind = ARKSH_VALUE_INSTANCE;
  copy_string(value->text, sizeof(value->text), class_name);
  value->number = (double) instance_id;
}

void arksh_value_set_map(ArkshValue *value) {
  if (value == NULL) {
    return;
  }

  arksh_value_init(value);
  value->kind = ARKSH_VALUE_MAP;
}

/* E6-S6: Dict — immutable key-value dictionary, stored in the map field. */
void arksh_value_set_dict(ArkshValue *value) {
  if (value == NULL) {
    return;
  }

  arksh_value_init(value);
  value->kind = ARKSH_VALUE_DICT;
}

/* E6-S8 */
void arksh_value_set_matrix(ArkshValue *value, const char **col_names, size_t col_count) {
  size_t i;

  if (value == NULL) {
    return;
  }

  arksh_value_free(value);
  value->kind = ARKSH_VALUE_MATRIX;
  value->matrix = (ArkshMatrix *) calloc(1, sizeof(ArkshMatrix));
  if (value->matrix == NULL) {
    value->kind = ARKSH_VALUE_EMPTY;
    return;
  }

  if (col_count > ARKSH_MAX_MATRIX_COLS) {
    col_count = ARKSH_MAX_MATRIX_COLS;
  }
  value->matrix->col_count = col_count;
  for (i = 0; i < col_count; ++i) {
    if (col_names != NULL && col_names[i] != NULL) {
      strncpy(value->matrix->col_names[i], col_names[i], ARKSH_MAX_NAME - 1);
      value->matrix->col_names[i][ARKSH_MAX_NAME - 1] = '\0';
    }
  }
}

void arksh_value_set_typed_map(ArkshValue *value, const char *type_name) {
  ArkshValue tag;

  if (value == NULL || type_name == NULL) {
    return;
  }

  arksh_value_init(value);
  value->kind = ARKSH_VALUE_MAP;

  /* Store the type tag as a string entry; ignore allocation failures here
   * since the caller checks properties independently. */
  arksh_value_set_string(&tag, type_name);
  arksh_value_map_set(value, "__type__", &tag);
  arksh_value_free(&tag);
}

int arksh_value_set_from_item(ArkshValue *value, const ArkshValueItem *item) {
  if (value == NULL || item == NULL) {
    return 1;
  }

  arksh_value_init(value);
  value->kind = item->kind;
  value->number = item->number;
  value->boolean = item->boolean;
  value->object = item->object;
  value->block = item->block;
  copy_string(value->text, sizeof(value->text), item->text);

  if ((item->kind == ARKSH_VALUE_LIST || item->kind == ARKSH_VALUE_MAP || item->kind == ARKSH_VALUE_DICT) && item->nested != NULL) {
    return arksh_value_copy(value, item->nested);
  }

  return 0;
}

int arksh_value_list_append_item(ArkshValue *value, const ArkshValueItem *item) {
  if (value == NULL || item == NULL) {
    return 1;
  }

  if (value->kind == ARKSH_VALUE_EMPTY) {
    value->kind = ARKSH_VALUE_LIST;
  }

  if (value->kind != ARKSH_VALUE_LIST) {
    return 1;
  }

  if (value->list.count >= ARKSH_MAX_COLLECTION_ITEMS) {
    return 1;
  }

  if (arksh_value_item_copy(&value->list.items[value->list.count], item) != 0) {
    return 1;
  }
  value->list.count++;
  return 0;
}

int arksh_value_list_append_value(ArkshValue *value, const ArkshValue *item_value) {
  ArkshValueItem item;

  if (value == NULL || item_value == NULL) {
    return 1;
  }

  arksh_value_item_init(&item);
  if (arksh_value_item_set_from_value(&item, item_value) != 0) {
    return 1;
  }
  if (arksh_value_list_append_item(value, &item) != 0) {
    arksh_value_item_free(&item);
    return 1;
  }
  arksh_value_item_free(&item);
  return 0;
}

int arksh_value_map_set(ArkshValue *value, const char *key, const ArkshValue *entry_value) {
  size_t i;
  ArkshValueItem item;

  if (value == NULL || key == NULL || key[0] == '\0' || entry_value == NULL) {
    return 1;
  }

  if (value->kind == ARKSH_VALUE_EMPTY) {
    value->kind = ARKSH_VALUE_MAP;
  }
  if (value->kind != ARKSH_VALUE_MAP && value->kind != ARKSH_VALUE_DICT) {
    return 1;
  }

  arksh_value_item_init(&item);
  if (arksh_value_item_set_from_value(&item, entry_value) != 0) {
    return 1;
  }

  for (i = 0; i < value->map.count; ++i) {
    if (strcmp(value->map.entries[i].key, key) == 0) {
      arksh_value_item_free(&value->map.entries[i].value);
      value->map.entries[i].value = item;
      return 0;
    }
  }

  if (value->map.count >= ARKSH_MAX_COLLECTION_ITEMS) {
    arksh_value_item_free(&item);
    return 1;
  }

  copy_string(value->map.entries[value->map.count].key, sizeof(value->map.entries[value->map.count].key), key);
  value->map.entries[value->map.count].value = item;
  value->map.count++;
  return 0;
}

const ArkshValueItem *arksh_value_map_get_item(const ArkshValue *value, const char *key) {
  size_t i;

  if (value == NULL || key == NULL || (value->kind != ARKSH_VALUE_MAP && value->kind != ARKSH_VALUE_DICT)) {
    return NULL;
  }

  for (i = 0; i < value->map.count; ++i) {
    if (strcmp(value->map.entries[i].key, key) == 0) {
      return &value->map.entries[i].value;
    }
  }

  return NULL;
}

int arksh_value_item_render(const ArkshValueItem *item, char *out, size_t out_size) {
  if (item == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  switch (item->kind) {
    case ARKSH_VALUE_STRING:
      copy_string(out, out_size, item->text);
      return 0;
    case ARKSH_VALUE_NUMBER:
      format_number(item->number, out, out_size);
      return 0;
    case ARKSH_VALUE_BOOLEAN:
      copy_string(out, out_size, item->boolean ? "true" : "false");
      return 0;
    case ARKSH_VALUE_OBJECT:
      return arksh_object_call_method(&item->object, "describe", 0, NULL, out, out_size);
    case ARKSH_VALUE_BLOCK:
      copy_string(out, out_size, item->block.source);
      return 0;
    case ARKSH_VALUE_CLASS:
      snprintf(out, out_size, "class %s", item->text);
      return 0;
    case ARKSH_VALUE_INSTANCE:
      snprintf(out, out_size, "%s#%d", item->text, (int) item->number);
      return 0;
    case ARKSH_VALUE_EMPTY:
      copy_string(out, out_size, "");
      return 0;
    case ARKSH_VALUE_LIST:
    case ARKSH_VALUE_MAP:
    case ARKSH_VALUE_DICT:
      if (item->nested != NULL) {
        return arksh_value_render(item->nested, out, out_size);
      }
      snprintf(out, out_size, "unsupported value item");
      return 1;
    /* E6-S5: explicit numeric sub-kinds */
    case ARKSH_VALUE_INTEGER:
    case ARKSH_VALUE_FLOAT:
    case ARKSH_VALUE_DOUBLE:
    case ARKSH_VALUE_IMAGINARY:
      format_number_by_kind(item->kind, item->number, out, out_size);
      return 0;
    default:
      snprintf(out, out_size, "unsupported value item");
      return 1;
  }
}

int arksh_value_render(const ArkshValue *value, char *out, size_t out_size) {
  size_t i;

  if (value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';

  switch (value->kind) {
    case ARKSH_VALUE_STRING:
      copy_string(out, out_size, value->text);
      return 0;
    case ARKSH_VALUE_NUMBER:
      format_number(value->number, out, out_size);
      return 0;
    case ARKSH_VALUE_BOOLEAN:
      copy_string(out, out_size, value->boolean ? "true" : "false");
      return 0;
    case ARKSH_VALUE_OBJECT:
      return arksh_object_call_method(&value->object, "describe", 0, NULL, out, out_size);
    case ARKSH_VALUE_BLOCK:
      copy_string(out, out_size, value->block.source);
      return 0;
    case ARKSH_VALUE_CLASS:
      snprintf(out, out_size, "class %s", value->text);
      return 0;
    case ARKSH_VALUE_INSTANCE:
      snprintf(out, out_size, "%s#%d", value->text, (int) value->number);
      return 0;
    case ARKSH_VALUE_LIST:
      if (value->list.count == 0) {
        copy_string(out, out_size, "empty list");
        return 0;
      }

      for (i = 0; i < value->list.count; ++i) {
        char line[ARKSH_MAX_OUTPUT];

        if (render_list_item_brief(&value->list.items[i], line, sizeof(line)) != 0) {
          return 1;
        }
        append_line(out, out_size, line);
      }
      return 0;
    case ARKSH_VALUE_MAP:
      if (value->map.count == 0) {
        copy_string(out, out_size, "empty map");
        return 0;
      }
      for (i = 0; i < value->map.count; ++i) {
        char rendered[ARKSH_MAX_OUTPUT];
        char line[ARKSH_MAX_OUTPUT];

        if (arksh_value_item_render(&value->map.entries[i].value, rendered, sizeof(rendered)) != 0) {
          return 1;
        }
        snprintf(line, sizeof(line), "%s=%s", value->map.entries[i].key, rendered);
        append_line(out, out_size, line);
      }
      return 0;
    /* E6-S6: Dict renders as {key: value, ...} */
    case ARKSH_VALUE_DICT: {
      size_t length = 0;

      if (value->map.count == 0) {
        copy_string(out, out_size, "{}");
        return 0;
      }
      out[0] = '\0';
      if (append_char(out, out_size, &length, '{') != 0) {
        return 1;
      }
      for (i = 0; i < value->map.count; ++i) {
        char item_buf[ARKSH_MAX_OUTPUT];

        if (i > 0) {
          if (append_char(out, out_size, &length, ',') != 0 ||
              append_char(out, out_size, &length, ' ') != 0) {
            return 1;
          }
        }
        if (append_text(out, out_size, &length, value->map.entries[i].key) != 0 ||
            append_char(out, out_size, &length, ':') != 0 ||
            append_char(out, out_size, &length, ' ') != 0) {
          return 1;
        }
        if (arksh_value_item_render(&value->map.entries[i].value, item_buf, sizeof(item_buf)) != 0) {
          return 1;
        }
        if (append_text(out, out_size, &length, item_buf) != 0) {
          return 1;
        }
      }
      return append_char(out, out_size, &length, '}');
    }
    /* E6-S5: explicit numeric sub-kinds */
    case ARKSH_VALUE_INTEGER:
    case ARKSH_VALUE_FLOAT:
    case ARKSH_VALUE_DOUBLE:
    case ARKSH_VALUE_IMAGINARY:
      format_number_by_kind(value->kind, value->number, out, out_size);
      return 0;
    /* E6-S8: Matrix renders as a text table */
    case ARKSH_VALUE_MATRIX: {
      size_t row, col;
      size_t col_widths[ARKSH_MAX_MATRIX_COLS];
      ArkshMatrix *m = value->matrix;

      if (m == NULL || m->col_count == 0) {
        copy_string(out, out_size, "empty matrix");
        return 0;
      }

      /* Compute display width for each column */
      for (col = 0; col < m->col_count; ++col) {
        col_widths[col] = strlen(m->col_names[col]);
        for (row = 0; row < m->row_count; ++row) {
          const ArkshMatrixCell *c = &m->rows[row][col];
          size_t cw;
          if (c->kind == ARKSH_VALUE_NUMBER ||
              c->kind == ARKSH_VALUE_INTEGER ||
              c->kind == ARKSH_VALUE_FLOAT ||
              c->kind == ARKSH_VALUE_DOUBLE) {
            char num[32];
            snprintf(num, sizeof(num), "%.6g", c->number);
            cw = strlen(num);
          } else {
            cw = strlen(c->text);
          }
          if (cw > col_widths[col]) {
            col_widths[col] = cw;
          }
        }
        if (col_widths[col] > 20) col_widths[col] = 20;
        if (col_widths[col] < 1) col_widths[col] = 1;
      }

      out[0] = '\0';
      /* Header row */
      for (col = 0; col < m->col_count; ++col) {
        char cell[24];
        if (col > 0) strncat(out, "  ", out_size - strlen(out) - 1);
        snprintf(cell, sizeof(cell), "%-*.*s", (int)col_widths[col], (int)col_widths[col], m->col_names[col]);
        strncat(out, cell, out_size - strlen(out) - 1);
      }
      strncat(out, "\n", out_size - strlen(out) - 1);
      /* Separator */
      for (col = 0; col < m->col_count; ++col) {
        char sep[24];
        size_t k;
        if (col > 0) strncat(out, "  ", out_size - strlen(out) - 1);
        for (k = 0; k < col_widths[col] && k < sizeof(sep) - 1; ++k) sep[k] = '-';
        sep[col_widths[col] < sizeof(sep) - 1 ? col_widths[col] : sizeof(sep) - 1] = '\0';
        strncat(out, sep, out_size - strlen(out) - 1);
      }
      /* Data rows */
      for (row = 0; row < m->row_count; ++row) {
        strncat(out, "\n", out_size - strlen(out) - 1);
        for (col = 0; col < m->col_count; ++col) {
          const ArkshMatrixCell *c = &m->rows[row][col];
          char cell[24];
          if (col > 0) strncat(out, "  ", out_size - strlen(out) - 1);
          if (c->kind == ARKSH_VALUE_NUMBER ||
              c->kind == ARKSH_VALUE_INTEGER ||
              c->kind == ARKSH_VALUE_FLOAT ||
              c->kind == ARKSH_VALUE_DOUBLE) {
            char num[32];
            snprintf(num, sizeof(num), "%.6g", c->number);
            snprintf(cell, sizeof(cell), "%-*.*s", (int)col_widths[col], (int)col_widths[col], num);
          } else {
            snprintf(cell, sizeof(cell), "%-*.*s", (int)col_widths[col], (int)col_widths[col], c->text);
          }
          strncat(out, cell, out_size - strlen(out) - 1);
        }
        if (strlen(out) + 128 >= out_size && row + 1 < m->row_count) {
          char trunc[64];
          snprintf(trunc, sizeof(trunc), "\n... (%zu more rows)", m->row_count - row - 1);
          strncat(out, trunc, out_size - strlen(out) - 1);
          break;
        }
      }
      return 0;
    }
    case ARKSH_VALUE_EMPTY:
    default:
      copy_string(out, out_size, "");
      return 0;
  }
}

int arksh_value_to_json(const ArkshValue *value, char *out, size_t out_size) {
  if (value_to_json_text(value, out, out_size) != 0) {
    if (out != NULL && out_size > 0 && out[0] == '\0') {
      snprintf(out, out_size, "unable to serialize value as JSON");
    }
    return 1;
  }

  return 0;
}

int arksh_value_parse_json(const char *text, ArkshValue *out_value, char *error, size_t error_size) {
  const char *cursor;

  if (text == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  error[0] = '\0';
  arksh_value_init(out_value);
  cursor = text;
  if (json_parse_value_internal(&cursor, out_value, error, error_size) != 0) {
    return 1;
  }

  json_skip_ws(&cursor);
  if (*cursor != '\0') {
    snprintf(error, error_size, "unexpected trailing JSON content");
    return 1;
  }

  return 0;
}

int arksh_value_get_property_value(const ArkshValue *value, const char *property, ArkshValue *out_value, char *error, size_t error_size) {
  const ArkshValueItem *entry;

  if (value == NULL || property == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  error[0] = '\0';
  arksh_value_init(out_value);

  if (strcmp(property, "value_type") == 0 || strcmp(property, "type") == 0) {
    if (value->kind == ARKSH_VALUE_OBJECT) {
      char rendered[ARKSH_MAX_OUTPUT];

      if (arksh_object_get_property(&value->object, "type", rendered, sizeof(rendered)) != 0) {
        snprintf(error, error_size, "unknown property: %s", property);
        return 1;
      }
      arksh_value_set_string(out_value, rendered);
      return 0;
    }
    /* E6-S2-T1: typed maps carry a __type__ tag that overrides the generic
     * "map" kind name, allowing plugins to expose custom named types. */
    if (value->kind == ARKSH_VALUE_MAP) {
      const ArkshValueItem *type_entry = arksh_value_map_get_item(value, "__type__");
      if (type_entry != NULL && type_entry->kind == ARKSH_VALUE_STRING) {
        arksh_value_set_string(out_value, type_entry->text);
        return 0;
      }
    }
    arksh_value_set_string(out_value, arksh_value_kind_name(value->kind));
    return 0;
  }

  if (strcmp(property, "value") == 0) {
    char rendered[ARKSH_MAX_OUTPUT];

    if (arksh_value_render(value, rendered, sizeof(rendered)) != 0) {
      snprintf(error, error_size, "unknown property: %s", property);
      return 1;
    }
    arksh_value_set_string(out_value, rendered);
    return 0;
  }

  switch (value->kind) {
    case ARKSH_VALUE_STRING:
      if (strcmp(property, "text") == 0) {
        arksh_value_set_string(out_value, value->text);
        return 0;
      }
      if (strcmp(property, "length") == 0) {
        arksh_value_set_number(out_value, (double) strlen(value->text));
        return 0;
      }
      break;
    case ARKSH_VALUE_NUMBER:
      if (strcmp(property, "number") == 0) {
        arksh_value_set_number(out_value, value->number);
        return 0;
      }
      /* E6-S5: conversion from plain number */
      if (strcmp(property, "to_integer") == 0) {
        arksh_value_set_integer(out_value, value->number);
        return 0;
      }
      if (strcmp(property, "to_float") == 0) {
        arksh_value_set_float(out_value, value->number);
        return 0;
      }
      if (strcmp(property, "to_double") == 0) {
        arksh_value_set_double(out_value, value->number);
        return 0;
      }
      break;
    /* E6-S5: Integer, Float, Double */
    case ARKSH_VALUE_INTEGER:
    case ARKSH_VALUE_FLOAT:
    case ARKSH_VALUE_DOUBLE: {
      if (strcmp(property, "bits") == 0) {
        int bits = (value->kind == ARKSH_VALUE_INTEGER) ? 64
                 : (value->kind == ARKSH_VALUE_FLOAT)   ? 32
                 :                                        64;
        arksh_value_set_number(out_value, (double) bits);
        return 0;
      }
      if (strcmp(property, "to_integer") == 0) {
        arksh_value_set_integer(out_value, value->number);
        return 0;
      }
      if (strcmp(property, "to_float") == 0) {
        arksh_value_set_float(out_value, value->number);
        return 0;
      }
      if (strcmp(property, "to_double") == 0) {
        arksh_value_set_double(out_value, value->number);
        return 0;
      }
      /* numeric alias */
      if (strcmp(property, "number") == 0) {
        arksh_value_set_number(out_value, value->number);
        return 0;
      }
      break;
    }
    /* E6-S5: Imaginary */
    case ARKSH_VALUE_IMAGINARY: {
      if (strcmp(property, "real") == 0) {
        arksh_value_set_double(out_value, 0.0);
        return 0;
      }
      if (strcmp(property, "imag") == 0) {
        arksh_value_set_double(out_value, value->number);
        return 0;
      }
      if (strcmp(property, "conjugate") == 0) {
        arksh_value_set_imaginary(out_value, -value->number);
        return 0;
      }
      if (strcmp(property, "magnitude") == 0) {
        double mag = value->number < 0.0 ? -value->number : value->number;
        arksh_value_set_double(out_value, mag);
        return 0;
      }
      if (strcmp(property, "bits") == 0) {
        arksh_value_set_number(out_value, 64.0);
        return 0;
      }
      if (strcmp(property, "to_integer") == 0) {
        /* imaginary part discarded, return 0 (real part) */
        arksh_value_set_integer(out_value, 0.0);
        return 0;
      }
      if (strcmp(property, "to_float") == 0) {
        arksh_value_set_float(out_value, 0.0);
        return 0;
      }
      if (strcmp(property, "to_double") == 0) {
        arksh_value_set_double(out_value, 0.0);
        return 0;
      }
      break;
    }
    case ARKSH_VALUE_BOOLEAN:
      if (strcmp(property, "bool") == 0) {
        arksh_value_set_boolean(out_value, value->boolean);
        return 0;
      }
      break;
    case ARKSH_VALUE_OBJECT: {
      char rendered[ARKSH_MAX_OUTPUT];

      if (strcmp(property, "child_count") == 0 &&
          (value->object.kind == ARKSH_OBJECT_DIRECTORY || value->object.kind == ARKSH_OBJECT_MOUNT_POINT)) {
        char child_names[ARKSH_MAX_COLLECTION_ITEMS][ARKSH_MAX_PATH];
        size_t count = 0;

        if (arksh_platform_list_children_names(value->object.path, child_names, ARKSH_MAX_COLLECTION_ITEMS, &count) != 0) {
          snprintf(error, error_size, "unable to count children for %s", value->object.path);
          return 1;
        }
        arksh_value_set_number(out_value, (double) count);
        return 0;
      }

      if (arksh_object_get_property(&value->object, property, rendered, sizeof(rendered)) == 0) {
        arksh_value_set_string(out_value, rendered);
        return 0;
      }
      break;
    }
    case ARKSH_VALUE_BLOCK:
      if (strcmp(property, "arity") == 0) {
        arksh_value_set_number(out_value, (double) value->block.param_count);
        return 0;
      }
      if (strcmp(property, "source") == 0) {
        arksh_value_set_string(out_value, value->block.source);
        return 0;
      }
      if (strcmp(property, "body") == 0) {
        arksh_value_set_string(out_value, value->block.body);
        return 0;
      }
      if (strcmp(property, "params") == 0) {
        size_t i;
        ArkshValue params;

        arksh_value_init(&params);
        params.kind = ARKSH_VALUE_LIST;
        for (i = 0; i < (size_t) value->block.param_count; ++i) {
          ArkshValue param;

          arksh_value_set_string(&param, value->block.params[i]);
          if (arksh_value_list_append_value(&params, &param) != 0) {
            arksh_value_free(&params);
            snprintf(error, error_size, "unknown property: %s", property);
            return 1;
          }
        }
        *out_value = params;
        return 0;
      }
      break;
    case ARKSH_VALUE_CLASS:
      if (strcmp(property, "name") == 0) {
        arksh_value_set_string(out_value, value->text);
        return 0;
      }
      break;
    case ARKSH_VALUE_INSTANCE:
      if (strcmp(property, "id") == 0) {
        arksh_value_set_number(out_value, value->number);
        return 0;
      }
      if (strcmp(property, "class") == 0 || strcmp(property, "class_name") == 0) {
        arksh_value_set_string(out_value, value->text);
        return 0;
      }
      break;
    case ARKSH_VALUE_LIST:
      if (strcmp(property, "count") == 0 || strcmp(property, "length") == 0) {
        arksh_value_set_number(out_value, (double) value->list.count);
        return 0;
      }
      break;
    case ARKSH_VALUE_MAP:
      if (strcmp(property, "count") == 0 || strcmp(property, "length") == 0) {
        arksh_value_set_number(out_value, (double) value->map.count);
        return 0;
      }
      entry = arksh_value_map_get_item(value, property);
      if (entry != NULL) {
        return arksh_value_set_from_item(out_value, entry);
      }
      break;
    /* E6-S6: Dict — count and direct property access */
    case ARKSH_VALUE_DICT:
      if (strcmp(property, "count") == 0 || strcmp(property, "length") == 0) {
        arksh_value_set_number(out_value, (double) value->map.count);
        return 0;
      }
      entry = arksh_value_map_get_item(value, property);
      if (entry != NULL) {
        return arksh_value_set_from_item(out_value, entry);
      }
      break;
    case ARKSH_VALUE_EMPTY:
    default:
      break;
  }

  snprintf(error, error_size, "unknown property: %s", property);
  return 1;
}

int arksh_value_get_property(const ArkshValue *value, const char *property, char *out, size_t out_size) {
  ArkshValue property_value;
  char error[ARKSH_MAX_OUTPUT];
  int status;

  if (value == NULL || property == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  status = arksh_value_get_property_value(value, property, &property_value, error, sizeof(error));
  if (status != 0) {
    copy_string(out, out_size, error);
    return 1;
  }

  status = arksh_value_render(&property_value, out, out_size);
  arksh_value_free(&property_value);
  return status;
}

int arksh_object_resolve(const char *cwd, const char *selector, ArkshObject *out_object) {
  ArkshPlatformFileInfo info;

  if (cwd == NULL || selector == NULL || out_object == NULL) {
    return 1;
  }

  memset(out_object, 0, sizeof(*out_object));

  if (arksh_platform_resolve_path(cwd, selector, out_object->path, sizeof(out_object->path)) != 0) {
    return 1;
  }

  arksh_platform_basename(out_object->path, out_object->name, sizeof(out_object->name));

  if (arksh_platform_stat(out_object->path, &info) != 0) {
    return 1;
  }

  out_object->exists = info.exists;
  out_object->size = info.size;
  out_object->hidden = info.hidden;
  out_object->readable = info.readable;
  out_object->writable = info.writable;

  if (!info.exists) {
    out_object->kind = ARKSH_OBJECT_PATH;
  } else if (info.is_mount_point) {
    out_object->kind = ARKSH_OBJECT_MOUNT_POINT;
  } else if (info.is_directory) {
    out_object->kind = ARKSH_OBJECT_DIRECTORY;
  } else if (info.is_device) {
    out_object->kind = ARKSH_OBJECT_DEVICE;
  } else if (info.is_file) {
    out_object->kind = ARKSH_OBJECT_FILE;
  } else {
    out_object->kind = ARKSH_OBJECT_UNKNOWN;
  }

  return 0;
}

int arksh_object_get_property(const ArkshObject *object, const char *property, char *out, size_t out_size) {
  if (object == NULL || property == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (strcmp(property, "type") == 0) {
    snprintf(out, out_size, "%s", arksh_object_kind_name(object->kind));
    return 0;
  }

  if (strcmp(property, "path") == 0) {
    snprintf(out, out_size, "%s", object->path);
    return 0;
  }

  if (strcmp(property, "name") == 0) {
    snprintf(out, out_size, "%s", object->name);
    return 0;
  }

  if (strcmp(property, "exists") == 0) {
    snprintf(out, out_size, "%s", object->exists ? "true" : "false");
    return 0;
  }

  if (strcmp(property, "size") == 0) {
    snprintf(out, out_size, "%llu", object->size);
    return 0;
  }

  if (strcmp(property, "hidden") == 0) {
    snprintf(out, out_size, "%s", object->hidden ? "true" : "false");
    return 0;
  }

  if (strcmp(property, "readable") == 0) {
    snprintf(out, out_size, "%s", object->readable ? "true" : "false");
    return 0;
  }

  if (strcmp(property, "writable") == 0) {
    snprintf(out, out_size, "%s", object->writable ? "true" : "false");
    return 0;
  }

  snprintf(out, out_size, "unknown property: %s", property);
  return 1;
}

int arksh_value_item_get_property_value(const ArkshValueItem *item, const char *property, ArkshValue *out_value, char *error, size_t error_size) {
  ArkshValue temp_value;

  if (item == NULL || property == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if ((item->kind == ARKSH_VALUE_LIST || item->kind == ARKSH_VALUE_MAP) && item->nested != NULL) {
    return arksh_value_get_property_value(item->nested, property, out_value, error, error_size);
  }

  arksh_value_init(&temp_value);
  temp_value.kind = item->kind;
  copy_string(temp_value.text, sizeof(temp_value.text), item->text);
  temp_value.number = item->number;
  temp_value.boolean = item->boolean;
  temp_value.object = item->object;
  temp_value.block = item->block;
  return arksh_value_get_property_value(&temp_value, property, out_value, error, error_size);
}

int arksh_value_item_get_property(const ArkshValueItem *item, const char *property, char *out, size_t out_size) {
  ArkshValue property_value;
  char error[ARKSH_MAX_OUTPUT];
  int status;

  if (item == NULL || property == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  status = arksh_value_item_get_property_value(item, property, &property_value, error, sizeof(error));
  if (status != 0) {
    copy_string(out, out_size, error);
    return 1;
  }

  status = arksh_value_render(&property_value, out, out_size);
  arksh_value_free(&property_value);
  return status;
}

int arksh_object_call_method(const ArkshObject *object, const char *method, int argc, char **argv, char *out, size_t out_size) {
  size_t limit = 4096;
  char parent[ARKSH_MAX_PATH];

  if (object == NULL || method == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (strcmp(method, "children") == 0) {
    if (!(object->kind == ARKSH_OBJECT_DIRECTORY || object->kind == ARKSH_OBJECT_MOUNT_POINT)) {
      snprintf(out, out_size, "children() is only valid on directory-like objects");
      return 1;
    }
    return arksh_platform_list_children(object->path, out, out_size);
  }

  if (strcmp(method, "read_text") == 0) {
    if (object->kind != ARKSH_OBJECT_FILE) {
      snprintf(out, out_size, "read_text() is only valid on file objects");
      return 1;
    }
    if (argc > 0 && parse_limit(argv[0], &limit) != 0) {
      snprintf(out, out_size, "read_text(limit) requires a numeric limit");
      return 1;
    }
    return arksh_platform_read_text_file(object->path, limit, out, out_size);
  }

  if (strcmp(method, "parent") == 0) {
    arksh_platform_dirname(object->path, parent, sizeof(parent));
    snprintf(out, out_size, "%s", parent);
    return 0;
  }

  if (strcmp(method, "describe") == 0) {
    snprintf(
      out,
      out_size,
      "type=%s\npath=%s\nname=%s\nexists=%s\nsize=%llu\nhidden=%s\nreadable=%s\nwritable=%s",
      arksh_object_kind_name(object->kind),
      object->path,
      object->name,
      object->exists ? "true" : "false",
      object->size,
      object->hidden ? "true" : "false",
      object->readable ? "true" : "false",
      object->writable ? "true" : "false"
    );
    return 0;
  }

  snprintf(out, out_size, "unknown method: %s", method);
  return 1;
}

int arksh_object_call_method_value(const ArkshObject *object, const char *method, int argc, char **argv, ArkshValue *out_value, char *error, size_t error_size) {
  char child_names[ARKSH_MAX_COLLECTION_ITEMS][ARKSH_MAX_PATH];
  size_t child_count = 0;
  size_t i;

  if (object == NULL || method == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  arksh_value_init(out_value);
  error[0] = '\0';

  if (strcmp(method, "children") == 0) {
    if (!(object->kind == ARKSH_OBJECT_DIRECTORY || object->kind == ARKSH_OBJECT_MOUNT_POINT)) {
      snprintf(error, error_size, "children() is only valid on directory-like objects");
      return 1;
    }

    if (arksh_platform_list_children_names(object->path, child_names, ARKSH_MAX_COLLECTION_ITEMS, &child_count) != 0) {
      snprintf(error, error_size, "unable to list children for %s", object->path);
      return 1;
    }

    out_value->kind = ARKSH_VALUE_LIST;
    out_value->list.count = 0;

    for (i = 0; i < child_count && i < ARKSH_MAX_COLLECTION_ITEMS; ++i) {
      char child_path[ARKSH_MAX_PATH];
      ArkshValueItem item;

      if (arksh_platform_resolve_path(object->path, child_names[i], child_path, sizeof(child_path)) != 0) {
        continue;
      }

      arksh_value_item_init(&item);
      item.kind = ARKSH_VALUE_OBJECT;
      if (arksh_object_resolve(object->path, child_path, &item.object) == 0 && arksh_value_list_append_item(out_value, &item) == 0) {
        continue;
      }
    }

    return 0;
  }

  if (strcmp(method, "parent") == 0) {
    char parent[ARKSH_MAX_PATH];

    arksh_platform_dirname(object->path, parent, sizeof(parent));
    if (arksh_object_resolve(object->path, parent, &out_value->object) != 0) {
      snprintf(error, error_size, "unable to resolve parent for %s", object->path);
      return 1;
    }

    out_value->kind = ARKSH_VALUE_OBJECT;
    return 0;
  }

  if (strcmp(method, "read_text") == 0 || strcmp(method, "describe") == 0) {
    if (arksh_object_call_method(object, method, argc, argv, out_value->text, sizeof(out_value->text)) != 0) {
      copy_string(error, error_size, out_value->text);
      return 1;
    }

    out_value->kind = ARKSH_VALUE_STRING;
    return 0;
  }

  snprintf(error, error_size, "unknown method: %s", method);
  return 1;
}
