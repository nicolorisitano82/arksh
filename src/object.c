#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oosh/object.h"
#include "oosh/platform.h"

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

void oosh_value_item_free(OoshValueItem *item) {
  if (item == NULL) {
    return;
  }

  if (item->nested != NULL) {
    oosh_value_free(item->nested);
    free(item->nested);
    item->nested = NULL;
  }

  memset(item, 0, sizeof(*item));
  item->kind = OOSH_VALUE_EMPTY;
}

void oosh_value_free(OoshValue *value) {
  size_t i;

  if (value == NULL) {
    return;
  }

  if (value->kind == OOSH_VALUE_LIST) {
    for (i = 0; i < value->list.count; ++i) {
      oosh_value_item_free(&value->list.items[i]);
    }
  } else if (value->kind == OOSH_VALUE_MAP) {
    for (i = 0; i < value->map.count; ++i) {
      oosh_value_item_free(&value->map.entries[i].value);
    }
  }

  memset(value, 0, sizeof(*value));
  value->kind = OOSH_VALUE_EMPTY;
}

int oosh_value_item_copy(OoshValueItem *dest, const OoshValueItem *src);
int oosh_value_copy(OoshValue *dest, const OoshValue *src);

int oosh_value_item_copy(OoshValueItem *dest, const OoshValueItem *src) {
  if (dest == NULL || src == NULL) {
    return 1;
  }

  oosh_value_item_init(dest);
  dest->kind = src->kind;
  dest->number = src->number;
  dest->boolean = src->boolean;
  dest->object = src->object;
  dest->block = src->block;
  copy_string(dest->text, sizeof(dest->text), src->text);

  if ((src->kind == OOSH_VALUE_LIST || src->kind == OOSH_VALUE_MAP) && src->nested != NULL) {
    dest->nested = (OoshValue *) calloc(1, sizeof(*dest->nested));
    if (dest->nested == NULL) {
      oosh_value_item_free(dest);
      return 1;
    }
    if (oosh_value_copy(dest->nested, src->nested) != 0) {
      oosh_value_item_free(dest);
      return 1;
    }
  }

  return 0;
}

int oosh_value_copy(OoshValue *dest, const OoshValue *src) {
  size_t i;

  if (dest == NULL || src == NULL) {
    return 1;
  }

  oosh_value_init(dest);
  dest->kind = src->kind;
  dest->number = src->number;
  dest->boolean = src->boolean;
  dest->object = src->object;
  dest->block = src->block;
  copy_string(dest->text, sizeof(dest->text), src->text);

  if (src->kind == OOSH_VALUE_LIST) {
    for (i = 0; i < src->list.count; ++i) {
      if (oosh_value_item_copy(&dest->list.items[dest->list.count], &src->list.items[i]) != 0) {
        oosh_value_free(dest);
        return 1;
      }
      dest->list.count++;
    }
  } else if (src->kind == OOSH_VALUE_MAP) {
    for (i = 0; i < src->map.count; ++i) {
      copy_string(dest->map.entries[i].key, sizeof(dest->map.entries[i].key), src->map.entries[i].key);
      if (oosh_value_item_copy(&dest->map.entries[i].value, &src->map.entries[i].value) != 0) {
        oosh_value_free(dest);
        return 1;
      }
      dest->map.count++;
    }
  }

  return 0;
}

int oosh_value_item_set_from_value(OoshValueItem *item, const OoshValue *value) {
  if (item == NULL || value == NULL) {
    return 1;
  }

  oosh_value_item_init(item);
  item->kind = value->kind;
  item->number = value->number;
  item->boolean = value->boolean;
  item->object = value->object;
  item->block = value->block;
  copy_string(item->text, sizeof(item->text), value->text);

  if (value->kind == OOSH_VALUE_LIST || value->kind == OOSH_VALUE_MAP) {
    item->nested = (OoshValue *) calloc(1, sizeof(*item->nested));
    if (item->nested == NULL) {
      oosh_value_item_free(item);
      return 1;
    }
    if (oosh_value_copy(item->nested, value) != 0) {
      oosh_value_item_free(item);
      return 1;
    }
  }

  return 0;
}

static int value_item_to_json_text(const OoshValueItem *item, char *out, size_t out_size) {
  if (item == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  switch (item->kind) {
    case OOSH_VALUE_STRING:
      return append_json_escaped_string(out, out_size, item->text);
    case OOSH_VALUE_NUMBER:
      format_number(item->number, out, out_size);
      return 0;
    case OOSH_VALUE_BOOLEAN:
      copy_string(out, out_size, item->boolean ? "true" : "false");
      return 0;
    case OOSH_VALUE_OBJECT:
      return append_json_escaped_string(out, out_size, item->object.path);
    case OOSH_VALUE_BLOCK:
      return append_json_escaped_string(out, out_size, item->block.source);
    case OOSH_VALUE_CLASS:
      return append_json_escaped_string(out, out_size, item->text);
    case OOSH_VALUE_INSTANCE: {
      char label[OOSH_MAX_OUTPUT];

      snprintf(label, sizeof(label), "%s#%d", item->text, (int) item->number);
      return append_json_escaped_string(out, out_size, label);
    }
    case OOSH_VALUE_EMPTY:
      copy_string(out, out_size, "null");
      return 0;
    case OOSH_VALUE_LIST:
    case OOSH_VALUE_MAP:
      if (item->nested == NULL) {
        return 1;
      }
      return oosh_value_to_json(item->nested, out, out_size);
    default:
      return 1;
  }
}

static int value_to_json_text(const OoshValue *value, char *out, size_t out_size) {
  size_t length = 0;
  size_t i;

  if (value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  switch (value->kind) {
    case OOSH_VALUE_STRING:
      return append_json_escaped_string(out, out_size, value->text);
    case OOSH_VALUE_NUMBER:
      format_number(value->number, out, out_size);
      return 0;
    case OOSH_VALUE_BOOLEAN:
      copy_string(out, out_size, value->boolean ? "true" : "false");
      return 0;
    case OOSH_VALUE_OBJECT:
      return append_json_escaped_string(out, out_size, value->object.path);
    case OOSH_VALUE_BLOCK:
      return append_json_escaped_string(out, out_size, value->block.source);
    case OOSH_VALUE_CLASS:
      return append_json_escaped_string(out, out_size, value->text);
    case OOSH_VALUE_INSTANCE: {
      char label[OOSH_MAX_OUTPUT];

      snprintf(label, sizeof(label), "%s#%d", value->text, (int) value->number);
      return append_json_escaped_string(out, out_size, label);
    }
    case OOSH_VALUE_EMPTY:
      copy_string(out, out_size, "null");
      return 0;
    case OOSH_VALUE_LIST:
      out[0] = '\0';
      if (append_char(out, out_size, &length, '[') != 0) {
        return 1;
      }
      for (i = 0; i < value->list.count; ++i) {
        char item_json[OOSH_MAX_OUTPUT];

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
    case OOSH_VALUE_MAP:
      out[0] = '\0';
      if (append_char(out, out_size, &length, '{') != 0) {
        return 1;
      }
      for (i = 0; i < value->map.count; ++i) {
        char key_json[OOSH_MAX_OUTPUT];
        char item_json[OOSH_MAX_OUTPUT];

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

static int json_parse_value_internal(const char **cursor, OoshValue *out_value, char *error, size_t error_size);
static int json_parse_object(const char **cursor, OoshValue *out_value, char *error, size_t error_size);

static int json_parse_array(const char **cursor, OoshValue *out_value, char *error, size_t error_size) {
  if (cursor == NULL || *cursor == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (**cursor != '[') {
    snprintf(error, error_size, "expected JSON array");
    return 1;
  }

  (*cursor)++;
  oosh_value_init(out_value);
  out_value->kind = OOSH_VALUE_LIST;
  json_skip_ws(cursor);
  if (**cursor == ']') {
    (*cursor)++;
    return 0;
  }

  while (**cursor != '\0') {
    OoshValue parsed_value;

    if (json_parse_value_internal(cursor, &parsed_value, error, error_size) != 0) {
      return 1;
    }
    if (oosh_value_list_append_value(out_value, &parsed_value) != 0) {
      oosh_value_free(&parsed_value);
      if (error[0] == '\0') {
        snprintf(error, error_size, "JSON array is too large");
      }
      return 1;
    }
    oosh_value_free(&parsed_value);

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

static int json_parse_object(const char **cursor, OoshValue *out_value, char *error, size_t error_size) {
  if (cursor == NULL || *cursor == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (**cursor != '{') {
    snprintf(error, error_size, "expected JSON object");
    return 1;
  }

  (*cursor)++;
  oosh_value_init(out_value);
  out_value->kind = OOSH_VALUE_MAP;
  json_skip_ws(cursor);
  if (**cursor == '}') {
    (*cursor)++;
    return 0;
  }

  while (**cursor != '\0') {
    char key[OOSH_MAX_NAME];
    OoshValue parsed_value;

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
    if (oosh_value_map_set(out_value, key, &parsed_value) != 0) {
      oosh_value_free(&parsed_value);
      snprintf(error, error_size, "JSON object is too large");
      return 1;
    }
    oosh_value_free(&parsed_value);

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

static int json_parse_value_internal(const char **cursor, OoshValue *out_value, char *error, size_t error_size) {
  if (cursor == NULL || *cursor == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  oosh_value_init(out_value);
  json_skip_ws(cursor);
  if (**cursor == '"') {
    if (json_parse_string(cursor, out_value->text, sizeof(out_value->text), error, error_size) != 0) {
      return 1;
    }
    out_value->kind = OOSH_VALUE_STRING;
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
    oosh_value_set_boolean(out_value, 1);
    return 0;
  }
  if (strncmp(*cursor, "false", 5) == 0) {
    if (json_parse_literal(cursor, "false") != 0) {
      return 1;
    }
    oosh_value_set_boolean(out_value, 0);
    return 0;
  }
  if (strncmp(*cursor, "null", 4) == 0) {
    if (json_parse_literal(cursor, "null") != 0) {
      return 1;
    }
    oosh_value_init(out_value);
    return 0;
  }

  if (**cursor == '-' || (**cursor >= '0' && **cursor <= '9')) {
    double number;

    if (json_parse_number(cursor, &number, error, error_size) != 0) {
      return 1;
    }
    oosh_value_set_number(out_value, number);
    return 0;
  }

  snprintf(error, error_size, "invalid JSON value");
  return 1;
}

static int render_list_item_brief(const OoshValueItem *item, char *out, size_t out_size) {
  if (item == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  switch (item->kind) {
    case OOSH_VALUE_STRING:
      copy_string(out, out_size, item->text);
      return 0;
    case OOSH_VALUE_NUMBER:
      format_number(item->number, out, out_size);
      return 0;
    case OOSH_VALUE_BOOLEAN:
      copy_string(out, out_size, item->boolean ? "true" : "false");
      return 0;
    case OOSH_VALUE_OBJECT:
      snprintf(out, out_size, "%s\t%s", oosh_object_kind_name(item->object.kind), item->object.path);
      return 0;
    case OOSH_VALUE_BLOCK:
      copy_string(out, out_size, item->block.source);
      return 0;
    case OOSH_VALUE_EMPTY:
      copy_string(out, out_size, "");
      return 0;
    case OOSH_VALUE_LIST:
    case OOSH_VALUE_MAP:
      if (item->nested != NULL && oosh_value_to_json(item->nested, out, out_size) == 0) {
        return 0;
      }
      snprintf(out, out_size, "<unsupported nested item>");
      return 1;
    default:
      snprintf(out, out_size, "<unsupported list item>");
      return 1;
  }
}

const char *oosh_object_kind_name(OoshObjectKind kind) {
  switch (kind) {
    case OOSH_OBJECT_PATH:
      return "path";
    case OOSH_OBJECT_FILE:
      return "file";
    case OOSH_OBJECT_DIRECTORY:
      return "directory";
    case OOSH_OBJECT_DEVICE:
      return "device";
    case OOSH_OBJECT_MOUNT_POINT:
      return "mount";
    case OOSH_OBJECT_UNKNOWN:
    default:
      return "unknown";
  }
}

const char *oosh_value_kind_name(OoshValueKind kind) {
  switch (kind) {
    case OOSH_VALUE_STRING:
      return "string";
    case OOSH_VALUE_NUMBER:
      return "number";
    case OOSH_VALUE_BOOLEAN:
      return "bool";
    case OOSH_VALUE_OBJECT:
      return "object";
    case OOSH_VALUE_BLOCK:
      return "block";
    case OOSH_VALUE_LIST:
      return "list";
    case OOSH_VALUE_MAP:
      return "map";
    case OOSH_VALUE_CLASS:
      return "class";
    case OOSH_VALUE_INSTANCE:
      return "instance";
    case OOSH_VALUE_EMPTY:
    default:
      return "empty";
  }
}

void oosh_value_init(OoshValue *value) {
  if (value == NULL) {
    return;
  }

  memset(value, 0, sizeof(*value));
  value->kind = OOSH_VALUE_EMPTY;
}

void oosh_value_item_init(OoshValueItem *item) {
  if (item == NULL) {
    return;
  }

  memset(item, 0, sizeof(*item));
  item->kind = OOSH_VALUE_EMPTY;
}

void oosh_value_set_string(OoshValue *value, const char *text) {
  if (value == NULL) {
    return;
  }

  oosh_value_init(value);
  value->kind = OOSH_VALUE_STRING;
  copy_string(value->text, sizeof(value->text), text);
}

void oosh_value_set_number(OoshValue *value, double number) {
  if (value == NULL) {
    return;
  }

  oosh_value_init(value);
  value->kind = OOSH_VALUE_NUMBER;
  value->number = number;
}

void oosh_value_set_boolean(OoshValue *value, int boolean) {
  if (value == NULL) {
    return;
  }

  oosh_value_init(value);
  value->kind = OOSH_VALUE_BOOLEAN;
  value->boolean = boolean ? 1 : 0;
}

void oosh_value_set_object(OoshValue *value, const OoshObject *object) {
  if (value == NULL) {
    return;
  }

  oosh_value_init(value);
  value->kind = OOSH_VALUE_OBJECT;
  if (object != NULL) {
    value->object = *object;
  }
}

void oosh_value_set_block(OoshValue *value, const OoshBlock *block) {
  if (value == NULL) {
    return;
  }

  oosh_value_init(value);
  value->kind = OOSH_VALUE_BLOCK;
  if (block != NULL) {
    value->block = *block;
  }
}

void oosh_value_set_class(OoshValue *value, const char *class_name) {
  if (value == NULL) {
    return;
  }

  oosh_value_init(value);
  value->kind = OOSH_VALUE_CLASS;
  copy_string(value->text, sizeof(value->text), class_name);
}

void oosh_value_set_instance(OoshValue *value, const char *class_name, int instance_id) {
  if (value == NULL) {
    return;
  }

  oosh_value_init(value);
  value->kind = OOSH_VALUE_INSTANCE;
  copy_string(value->text, sizeof(value->text), class_name);
  value->number = (double) instance_id;
}

void oosh_value_set_map(OoshValue *value) {
  if (value == NULL) {
    return;
  }

  oosh_value_init(value);
  value->kind = OOSH_VALUE_MAP;
}

int oosh_value_set_from_item(OoshValue *value, const OoshValueItem *item) {
  if (value == NULL || item == NULL) {
    return 1;
  }

  oosh_value_init(value);
  value->kind = item->kind;
  value->number = item->number;
  value->boolean = item->boolean;
  value->object = item->object;
  value->block = item->block;
  copy_string(value->text, sizeof(value->text), item->text);

  if ((item->kind == OOSH_VALUE_LIST || item->kind == OOSH_VALUE_MAP) && item->nested != NULL) {
    return oosh_value_copy(value, item->nested);
  }

  return 0;
}

int oosh_value_list_append_item(OoshValue *value, const OoshValueItem *item) {
  if (value == NULL || item == NULL) {
    return 1;
  }

  if (value->kind == OOSH_VALUE_EMPTY) {
    value->kind = OOSH_VALUE_LIST;
  }

  if (value->kind != OOSH_VALUE_LIST) {
    return 1;
  }

  if (value->list.count >= OOSH_MAX_COLLECTION_ITEMS) {
    return 1;
  }

  if (oosh_value_item_copy(&value->list.items[value->list.count], item) != 0) {
    return 1;
  }
  value->list.count++;
  return 0;
}

int oosh_value_list_append_value(OoshValue *value, const OoshValue *item_value) {
  OoshValueItem item;

  if (value == NULL || item_value == NULL) {
    return 1;
  }

  oosh_value_item_init(&item);
  if (oosh_value_item_set_from_value(&item, item_value) != 0) {
    return 1;
  }
  if (oosh_value_list_append_item(value, &item) != 0) {
    oosh_value_item_free(&item);
    return 1;
  }
  oosh_value_item_free(&item);
  return 0;
}

int oosh_value_map_set(OoshValue *value, const char *key, const OoshValue *entry_value) {
  size_t i;
  OoshValueItem item;

  if (value == NULL || key == NULL || key[0] == '\0' || entry_value == NULL) {
    return 1;
  }

  if (value->kind == OOSH_VALUE_EMPTY) {
    value->kind = OOSH_VALUE_MAP;
  }
  if (value->kind != OOSH_VALUE_MAP) {
    return 1;
  }

  oosh_value_item_init(&item);
  if (oosh_value_item_set_from_value(&item, entry_value) != 0) {
    return 1;
  }

  for (i = 0; i < value->map.count; ++i) {
    if (strcmp(value->map.entries[i].key, key) == 0) {
      oosh_value_item_free(&value->map.entries[i].value);
      value->map.entries[i].value = item;
      return 0;
    }
  }

  if (value->map.count >= OOSH_MAX_COLLECTION_ITEMS) {
    oosh_value_item_free(&item);
    return 1;
  }

  copy_string(value->map.entries[value->map.count].key, sizeof(value->map.entries[value->map.count].key), key);
  value->map.entries[value->map.count].value = item;
  value->map.count++;
  return 0;
}

const OoshValueItem *oosh_value_map_get_item(const OoshValue *value, const char *key) {
  size_t i;

  if (value == NULL || key == NULL || value->kind != OOSH_VALUE_MAP) {
    return NULL;
  }

  for (i = 0; i < value->map.count; ++i) {
    if (strcmp(value->map.entries[i].key, key) == 0) {
      return &value->map.entries[i].value;
    }
  }

  return NULL;
}

int oosh_value_item_render(const OoshValueItem *item, char *out, size_t out_size) {
  if (item == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';
  switch (item->kind) {
    case OOSH_VALUE_STRING:
      copy_string(out, out_size, item->text);
      return 0;
    case OOSH_VALUE_NUMBER:
      format_number(item->number, out, out_size);
      return 0;
    case OOSH_VALUE_BOOLEAN:
      copy_string(out, out_size, item->boolean ? "true" : "false");
      return 0;
    case OOSH_VALUE_OBJECT:
      return oosh_object_call_method(&item->object, "describe", 0, NULL, out, out_size);
    case OOSH_VALUE_BLOCK:
      copy_string(out, out_size, item->block.source);
      return 0;
    case OOSH_VALUE_CLASS:
      snprintf(out, out_size, "class %s", item->text);
      return 0;
    case OOSH_VALUE_INSTANCE:
      snprintf(out, out_size, "%s#%d", item->text, (int) item->number);
      return 0;
    case OOSH_VALUE_EMPTY:
      copy_string(out, out_size, "");
      return 0;
    case OOSH_VALUE_LIST:
    case OOSH_VALUE_MAP:
      if (item->nested != NULL) {
        return oosh_value_render(item->nested, out, out_size);
      }
      snprintf(out, out_size, "unsupported value item");
      return 1;
    default:
      snprintf(out, out_size, "unsupported value item");
      return 1;
  }
}

int oosh_value_render(const OoshValue *value, char *out, size_t out_size) {
  size_t i;

  if (value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';

  switch (value->kind) {
    case OOSH_VALUE_STRING:
      copy_string(out, out_size, value->text);
      return 0;
    case OOSH_VALUE_NUMBER:
      format_number(value->number, out, out_size);
      return 0;
    case OOSH_VALUE_BOOLEAN:
      copy_string(out, out_size, value->boolean ? "true" : "false");
      return 0;
    case OOSH_VALUE_OBJECT:
      return oosh_object_call_method(&value->object, "describe", 0, NULL, out, out_size);
    case OOSH_VALUE_BLOCK:
      copy_string(out, out_size, value->block.source);
      return 0;
    case OOSH_VALUE_CLASS:
      snprintf(out, out_size, "class %s", value->text);
      return 0;
    case OOSH_VALUE_INSTANCE:
      snprintf(out, out_size, "%s#%d", value->text, (int) value->number);
      return 0;
    case OOSH_VALUE_LIST:
      if (value->list.count == 0) {
        copy_string(out, out_size, "empty list");
        return 0;
      }

      for (i = 0; i < value->list.count; ++i) {
        char line[OOSH_MAX_OUTPUT];

        if (render_list_item_brief(&value->list.items[i], line, sizeof(line)) != 0) {
          return 1;
        }
        append_line(out, out_size, line);
      }
      return 0;
    case OOSH_VALUE_MAP:
      if (value->map.count == 0) {
        copy_string(out, out_size, "empty map");
        return 0;
      }
      for (i = 0; i < value->map.count; ++i) {
        char rendered[OOSH_MAX_OUTPUT];
        char line[OOSH_MAX_OUTPUT];

        if (oosh_value_item_render(&value->map.entries[i].value, rendered, sizeof(rendered)) != 0) {
          return 1;
        }
        snprintf(line, sizeof(line), "%s=%s", value->map.entries[i].key, rendered);
        append_line(out, out_size, line);
      }
      return 0;
    case OOSH_VALUE_EMPTY:
    default:
      copy_string(out, out_size, "");
      return 0;
  }
}

int oosh_value_to_json(const OoshValue *value, char *out, size_t out_size) {
  if (value_to_json_text(value, out, out_size) != 0) {
    if (out != NULL && out_size > 0 && out[0] == '\0') {
      snprintf(out, out_size, "unable to serialize value as JSON");
    }
    return 1;
  }

  return 0;
}

int oosh_value_parse_json(const char *text, OoshValue *out_value, char *error, size_t error_size) {
  const char *cursor;

  if (text == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  error[0] = '\0';
  oosh_value_init(out_value);
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

int oosh_value_get_property_value(const OoshValue *value, const char *property, OoshValue *out_value, char *error, size_t error_size) {
  const OoshValueItem *entry;

  if (value == NULL || property == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  error[0] = '\0';
  oosh_value_init(out_value);

  if (strcmp(property, "value_type") == 0 || strcmp(property, "type") == 0) {
    if (value->kind == OOSH_VALUE_OBJECT) {
      char rendered[OOSH_MAX_OUTPUT];

      if (oosh_object_get_property(&value->object, "type", rendered, sizeof(rendered)) != 0) {
        snprintf(error, error_size, "unknown property: %s", property);
        return 1;
      }
      oosh_value_set_string(out_value, rendered);
      return 0;
    }
    oosh_value_set_string(out_value, oosh_value_kind_name(value->kind));
    return 0;
  }

  if (strcmp(property, "value") == 0) {
    char rendered[OOSH_MAX_OUTPUT];

    if (oosh_value_render(value, rendered, sizeof(rendered)) != 0) {
      snprintf(error, error_size, "unknown property: %s", property);
      return 1;
    }
    oosh_value_set_string(out_value, rendered);
    return 0;
  }

  switch (value->kind) {
    case OOSH_VALUE_STRING:
      if (strcmp(property, "text") == 0) {
        oosh_value_set_string(out_value, value->text);
        return 0;
      }
      if (strcmp(property, "length") == 0) {
        oosh_value_set_number(out_value, (double) strlen(value->text));
        return 0;
      }
      break;
    case OOSH_VALUE_NUMBER:
      if (strcmp(property, "number") == 0) {
        oosh_value_set_number(out_value, value->number);
        return 0;
      }
      break;
    case OOSH_VALUE_BOOLEAN:
      if (strcmp(property, "bool") == 0) {
        oosh_value_set_boolean(out_value, value->boolean);
        return 0;
      }
      break;
    case OOSH_VALUE_OBJECT: {
      char rendered[OOSH_MAX_OUTPUT];

      if (oosh_object_get_property(&value->object, property, rendered, sizeof(rendered)) == 0) {
        oosh_value_set_string(out_value, rendered);
        return 0;
      }
      break;
    }
    case OOSH_VALUE_BLOCK:
      if (strcmp(property, "arity") == 0) {
        oosh_value_set_number(out_value, (double) value->block.param_count);
        return 0;
      }
      if (strcmp(property, "source") == 0) {
        oosh_value_set_string(out_value, value->block.source);
        return 0;
      }
      if (strcmp(property, "body") == 0) {
        oosh_value_set_string(out_value, value->block.body);
        return 0;
      }
      if (strcmp(property, "params") == 0) {
        size_t i;
        OoshValue params;

        oosh_value_init(&params);
        params.kind = OOSH_VALUE_LIST;
        for (i = 0; i < (size_t) value->block.param_count; ++i) {
          OoshValue param;

          oosh_value_set_string(&param, value->block.params[i]);
          if (oosh_value_list_append_value(&params, &param) != 0) {
            oosh_value_free(&params);
            snprintf(error, error_size, "unknown property: %s", property);
            return 1;
          }
        }
        *out_value = params;
        return 0;
      }
      break;
    case OOSH_VALUE_CLASS:
      if (strcmp(property, "name") == 0) {
        oosh_value_set_string(out_value, value->text);
        return 0;
      }
      break;
    case OOSH_VALUE_INSTANCE:
      if (strcmp(property, "id") == 0) {
        oosh_value_set_number(out_value, value->number);
        return 0;
      }
      if (strcmp(property, "class") == 0 || strcmp(property, "class_name") == 0) {
        oosh_value_set_string(out_value, value->text);
        return 0;
      }
      break;
    case OOSH_VALUE_LIST:
      if (strcmp(property, "count") == 0 || strcmp(property, "length") == 0) {
        oosh_value_set_number(out_value, (double) value->list.count);
        return 0;
      }
      break;
    case OOSH_VALUE_MAP:
      if (strcmp(property, "count") == 0 || strcmp(property, "length") == 0) {
        oosh_value_set_number(out_value, (double) value->map.count);
        return 0;
      }
      entry = oosh_value_map_get_item(value, property);
      if (entry != NULL) {
        return oosh_value_set_from_item(out_value, entry);
      }
      break;
    case OOSH_VALUE_EMPTY:
    default:
      break;
  }

  snprintf(error, error_size, "unknown property: %s", property);
  return 1;
}

int oosh_value_get_property(const OoshValue *value, const char *property, char *out, size_t out_size) {
  OoshValue property_value;
  char error[OOSH_MAX_OUTPUT];
  int status;

  if (value == NULL || property == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  status = oosh_value_get_property_value(value, property, &property_value, error, sizeof(error));
  if (status != 0) {
    copy_string(out, out_size, error);
    return 1;
  }

  status = oosh_value_render(&property_value, out, out_size);
  oosh_value_free(&property_value);
  return status;
}

int oosh_object_resolve(const char *cwd, const char *selector, OoshObject *out_object) {
  OoshPlatformFileInfo info;

  if (cwd == NULL || selector == NULL || out_object == NULL) {
    return 1;
  }

  memset(out_object, 0, sizeof(*out_object));

  if (oosh_platform_resolve_path(cwd, selector, out_object->path, sizeof(out_object->path)) != 0) {
    return 1;
  }

  oosh_platform_basename(out_object->path, out_object->name, sizeof(out_object->name));

  if (oosh_platform_stat(out_object->path, &info) != 0) {
    return 1;
  }

  out_object->exists = info.exists;
  out_object->size = info.size;
  out_object->hidden = info.hidden;
  out_object->readable = info.readable;
  out_object->writable = info.writable;

  if (!info.exists) {
    out_object->kind = OOSH_OBJECT_PATH;
  } else if (info.is_mount_point) {
    out_object->kind = OOSH_OBJECT_MOUNT_POINT;
  } else if (info.is_directory) {
    out_object->kind = OOSH_OBJECT_DIRECTORY;
  } else if (info.is_device) {
    out_object->kind = OOSH_OBJECT_DEVICE;
  } else if (info.is_file) {
    out_object->kind = OOSH_OBJECT_FILE;
  } else {
    out_object->kind = OOSH_OBJECT_UNKNOWN;
  }

  return 0;
}

int oosh_object_get_property(const OoshObject *object, const char *property, char *out, size_t out_size) {
  if (object == NULL || property == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (strcmp(property, "type") == 0) {
    snprintf(out, out_size, "%s", oosh_object_kind_name(object->kind));
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

int oosh_value_item_get_property_value(const OoshValueItem *item, const char *property, OoshValue *out_value, char *error, size_t error_size) {
  OoshValue temp_value;

  if (item == NULL || property == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if ((item->kind == OOSH_VALUE_LIST || item->kind == OOSH_VALUE_MAP) && item->nested != NULL) {
    return oosh_value_get_property_value(item->nested, property, out_value, error, error_size);
  }

  oosh_value_init(&temp_value);
  temp_value.kind = item->kind;
  copy_string(temp_value.text, sizeof(temp_value.text), item->text);
  temp_value.number = item->number;
  temp_value.boolean = item->boolean;
  temp_value.object = item->object;
  temp_value.block = item->block;
  return oosh_value_get_property_value(&temp_value, property, out_value, error, error_size);
}

int oosh_value_item_get_property(const OoshValueItem *item, const char *property, char *out, size_t out_size) {
  OoshValue property_value;
  char error[OOSH_MAX_OUTPUT];
  int status;

  if (item == NULL || property == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  status = oosh_value_item_get_property_value(item, property, &property_value, error, sizeof(error));
  if (status != 0) {
    copy_string(out, out_size, error);
    return 1;
  }

  status = oosh_value_render(&property_value, out, out_size);
  oosh_value_free(&property_value);
  return status;
}

int oosh_object_call_method(const OoshObject *object, const char *method, int argc, char **argv, char *out, size_t out_size) {
  size_t limit = 4096;
  char parent[OOSH_MAX_PATH];

  if (object == NULL || method == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (strcmp(method, "children") == 0) {
    if (!(object->kind == OOSH_OBJECT_DIRECTORY || object->kind == OOSH_OBJECT_MOUNT_POINT)) {
      snprintf(out, out_size, "children() is only valid on directory-like objects");
      return 1;
    }
    return oosh_platform_list_children(object->path, out, out_size);
  }

  if (strcmp(method, "read_text") == 0) {
    if (object->kind != OOSH_OBJECT_FILE) {
      snprintf(out, out_size, "read_text() is only valid on file objects");
      return 1;
    }
    if (argc > 0 && parse_limit(argv[0], &limit) != 0) {
      snprintf(out, out_size, "read_text(limit) requires a numeric limit");
      return 1;
    }
    return oosh_platform_read_text_file(object->path, limit, out, out_size);
  }

  if (strcmp(method, "parent") == 0) {
    oosh_platform_dirname(object->path, parent, sizeof(parent));
    snprintf(out, out_size, "%s", parent);
    return 0;
  }

  if (strcmp(method, "describe") == 0) {
    snprintf(
      out,
      out_size,
      "type=%s\npath=%s\nname=%s\nexists=%s\nsize=%llu\nhidden=%s\nreadable=%s\nwritable=%s",
      oosh_object_kind_name(object->kind),
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

int oosh_object_call_method_value(const OoshObject *object, const char *method, int argc, char **argv, OoshValue *out_value, char *error, size_t error_size) {
  char child_names[OOSH_MAX_COLLECTION_ITEMS][OOSH_MAX_PATH];
  size_t child_count = 0;
  size_t i;

  if (object == NULL || method == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  oosh_value_init(out_value);
  error[0] = '\0';

  if (strcmp(method, "children") == 0) {
    if (!(object->kind == OOSH_OBJECT_DIRECTORY || object->kind == OOSH_OBJECT_MOUNT_POINT)) {
      snprintf(error, error_size, "children() is only valid on directory-like objects");
      return 1;
    }

    if (oosh_platform_list_children_names(object->path, child_names, OOSH_MAX_COLLECTION_ITEMS, &child_count) != 0) {
      snprintf(error, error_size, "unable to list children for %s", object->path);
      return 1;
    }

    out_value->kind = OOSH_VALUE_LIST;
    out_value->list.count = 0;

    for (i = 0; i < child_count && i < OOSH_MAX_COLLECTION_ITEMS; ++i) {
      char child_path[OOSH_MAX_PATH];
      OoshValueItem item;

      if (oosh_platform_resolve_path(object->path, child_names[i], child_path, sizeof(child_path)) != 0) {
        continue;
      }

      oosh_value_item_init(&item);
      item.kind = OOSH_VALUE_OBJECT;
      if (oosh_object_resolve(object->path, child_path, &item.object) == 0 && oosh_value_list_append_item(out_value, &item) == 0) {
        continue;
      }
    }

    return 0;
  }

  if (strcmp(method, "parent") == 0) {
    char parent[OOSH_MAX_PATH];

    oosh_platform_dirname(object->path, parent, sizeof(parent));
    if (oosh_object_resolve(object->path, parent, &out_value->object) != 0) {
      snprintf(error, error_size, "unable to resolve parent for %s", object->path);
      return 1;
    }

    out_value->kind = OOSH_VALUE_OBJECT;
    return 0;
  }

  if (strcmp(method, "read_text") == 0 || strcmp(method, "describe") == 0) {
    if (oosh_object_call_method(object, method, argc, argv, out_value->text, sizeof(out_value->text)) != 0) {
      copy_string(error, error_size, out_value->text);
      return 1;
    }

    out_value->kind = OOSH_VALUE_STRING;
    return 0;
  }

  snprintf(error, error_size, "unknown method: %s", method);
  return 1;
}
