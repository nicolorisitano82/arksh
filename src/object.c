#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arksh/object.h"
#include "arksh/perf.h"
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

static ArkshValue *allocate_value(char *error, size_t error_size) {
  ArkshValue *value = (ArkshValue *) calloc(1, sizeof(*value));

  if (value == NULL && error != NULL && error_size > 0) {
    snprintf(error, error_size, "out of memory");
  }
  return value;
}

static ArkshValueItem *allocate_value_item(char *error, size_t error_size) {
  ArkshValueItem *item = (ArkshValueItem *) calloc(1, sizeof(*item));

  if (item == NULL && error != NULL && error_size > 0) {
    snprintf(error, error_size, "out of memory");
  }
  return item;
}

static char *duplicate_string_owned(const char *src) {
  size_t length;
  char *copy;

  if (src == NULL || src[0] == '\0') {
    return NULL;
  }

  length = strlen(src);
  copy = (char *) calloc(length + 1, sizeof(*copy));
  if (copy == NULL) {
    return NULL;
  }
  memcpy(copy, src, length);
  copy[length] = '\0';
  return copy;
}

static ArkshObject *duplicate_object_owned(const ArkshObject *src) {
  ArkshObject *copy;

  if (src == NULL) {
    return NULL;
  }

  copy = (ArkshObject *) calloc(1, sizeof(*copy));
  if (copy == NULL) {
    return NULL;
  }
  *copy = *src;
  return copy;
}

static ArkshBlock *duplicate_block_owned(const ArkshBlock *src) {
  ArkshBlock *copy;

  if (src == NULL) {
    return NULL;
  }

  copy = (ArkshBlock *) calloc(1, sizeof(*copy));
  if (copy == NULL) {
    return NULL;
  }
  *copy = *src;
  return copy;
}

static void free_item_scalar_payload(ArkshValueItem *item) {
  if (item == NULL) {
    return;
  }

  free(item->text);
  item->text = NULL;
  free(item->object);
  item->object = NULL;
  free(item->block);
  item->block = NULL;
}

static void free_value_scalar_payload(ArkshValue *value) {
  if (value == NULL) {
    return;
  }

  free(value->text);
  value->text = NULL;
  free(value->object);
  value->object = NULL;
  free(value->block);
  value->block = NULL;
}

static int assign_value_text(ArkshValue *value, const char *text) {
  char *copy;

  if (value == NULL) {
    return 1;
  }

  copy = duplicate_string_owned(text);
  free(value->text);
  value->text = copy;
  if (text != NULL && text[0] != '\0' && value->text == NULL) {
    return 1;
  }
  return 0;
}

static int assign_item_text(ArkshValueItem *item, const char *text) {
  char *copy;

  if (item == NULL) {
    return 1;
  }

  copy = duplicate_string_owned(text);
  free(item->text);
  item->text = copy;
  if (text != NULL && text[0] != '\0' && item->text == NULL) {
    return 1;
  }
  return 0;
}

static int assign_value_object(ArkshValue *value, const ArkshObject *object) {
  ArkshObject *copy;

  if (value == NULL) {
    return 1;
  }

  copy = duplicate_object_owned(object);
  free(value->object);
  value->object = copy;
  if (object != NULL && value->object == NULL) {
    return 1;
  }
  return 0;
}

static int assign_item_object(ArkshValueItem *item, const ArkshObject *object) {
  ArkshObject *copy;

  if (item == NULL) {
    return 1;
  }

  copy = duplicate_object_owned(object);
  free(item->object);
  item->object = copy;
  if (object != NULL && item->object == NULL) {
    return 1;
  }
  return 0;
}

static int assign_value_block(ArkshValue *value, const ArkshBlock *block) {
  ArkshBlock *copy;

  if (value == NULL) {
    return 1;
  }

  copy = duplicate_block_owned(block);
  free(value->block);
  value->block = copy;
  if (block != NULL && value->block == NULL) {
    return 1;
  }
  return 0;
}

static int assign_item_block(ArkshValueItem *item, const ArkshBlock *block) {
  ArkshBlock *copy;

  if (item == NULL) {
    return 1;
  }

  copy = duplicate_block_owned(block);
  free(item->block);
  item->block = copy;
  if (block != NULL && item->block == NULL) {
    return 1;
  }
  return 0;
}

static const ArkshObject EMPTY_OBJECT_VALUE = {0};
static const ArkshBlock EMPTY_BLOCK_VALUE = {0};
static const unsigned int ARKSH_VALUE_SIGNATURE = 0x41564c55u;
static const unsigned int ARKSH_VALUE_ITEM_SIGNATURE = 0x41564954u;

static int value_is_initialized(const ArkshValue *value) {
  return value != NULL && value->signature == ARKSH_VALUE_SIGNATURE;
}

static int item_is_initialized(const ArkshValueItem *item) {
  return item != NULL && item->signature == ARKSH_VALUE_ITEM_SIGNATURE;
}

static int ensure_list_capacity(ArkshValue *value, size_t min_capacity) {
  ArkshValueItem *items;
  size_t new_capacity;
  size_t old_capacity;

  if (value == NULL) {
    return 1;
  }

  if (value->list.capacity >= min_capacity) {
    return 0;
  }

  old_capacity = value->list.capacity;
  new_capacity = old_capacity > 0 ? old_capacity : 8;
  while (new_capacity < min_capacity) {
    if (new_capacity > ((size_t) -1) / 2) {
      return 1;
    }
    new_capacity *= 2;
  }

  items = (ArkshValueItem *) realloc(value->list.items, new_capacity * sizeof(*items));
  if (items == NULL) {
    return 1;
  }
  if (new_capacity > old_capacity) {
    memset(items + old_capacity, 0, (new_capacity - old_capacity) * sizeof(*items));
  }
  value->list.items = items;
  value->list.capacity = new_capacity;
  return 0;
}

static int ensure_map_capacity(ArkshValue *value, size_t min_capacity) {
  ArkshValueMapEntry *entries;
  size_t new_capacity;
  size_t old_capacity;

  if (value == NULL) {
    return 1;
  }

  if (value->map.capacity >= min_capacity) {
    return 0;
  }

  old_capacity = value->map.capacity;
  new_capacity = old_capacity > 0 ? old_capacity : 8;
  while (new_capacity < min_capacity) {
    if (new_capacity > ((size_t) -1) / 2) {
      return 1;
    }
    new_capacity *= 2;
  }

  entries = (ArkshValueMapEntry *) realloc(value->map.entries, new_capacity * sizeof(*entries));
  if (entries == NULL) {
    return 1;
  }
  if (new_capacity > old_capacity) {
    memset(entries + old_capacity, 0, (new_capacity - old_capacity) * sizeof(*entries));
  }
  value->map.entries = entries;
  value->map.capacity = new_capacity;
  return 0;
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

static void format_permission_octal(unsigned int permissions, char *out, size_t out_size) {
  unsigned int masked = permissions & 07777u;

  if (out == NULL || out_size == 0) {
    return;
  }

  if ((masked & 07000u) != 0u) {
    snprintf(out, out_size, "%04o", masked);
  } else {
    snprintf(out, out_size, "%03o", masked & 0777u);
  }
}

static void format_permission_rwx(unsigned int permissions, char *out, size_t out_size) {
  unsigned int masked = permissions & 07777u;

  if (out == NULL || out_size == 0) {
    return;
  }

  snprintf(
    out,
    out_size,
    "%c%c%c%c%c%c%c%c%c",
    (masked & 0400u) ? 'r' : '-',
    (masked & 0200u) ? 'w' : '-',
    (masked & 04000u) ? ((masked & 0100u) ? 's' : 'S') : ((masked & 0100u) ? 'x' : '-'),
    (masked & 0040u) ? 'r' : '-',
    (masked & 0020u) ? 'w' : '-',
    (masked & 02000u) ? ((masked & 0010u) ? 's' : 'S') : ((masked & 0010u) ? 'x' : '-'),
    (masked & 0004u) ? 'r' : '-',
    (masked & 0002u) ? 'w' : '-',
    (masked & 01000u) ? ((masked & 0001u) ? 't' : 'T') : ((masked & 0001u) ? 'x' : '-')
  );
}

static int parse_octal_permissions(const char *text, unsigned int *out_permissions) {
  unsigned int value = 0u;
  size_t i;

  if (text == NULL || out_permissions == NULL || text[0] == '\0') {
    return 1;
  }

  for (i = 0; text[i] != '\0'; ++i) {
    if (text[i] < '0' || text[i] > '7') {
      return 1;
    }
    value = (value * 8u) + (unsigned int) (text[i] - '0');
    if (value > 07777u) {
      return 1;
    }
  }

  if (i < 3u || i > 4u) {
    return 1;
  }

  *out_permissions = value;
  return 0;
}

static int parse_rwx_permissions(const char *text, unsigned int *out_permissions) {
  unsigned int value = 0u;

  if (text == NULL || out_permissions == NULL || strlen(text) != 9u) {
    return 1;
  }

  if (text[0] == 'r') { value |= 0400u; } else if (text[0] != '-') { return 1; }
  if (text[1] == 'w') { value |= 0200u; } else if (text[1] != '-') { return 1; }
  if (text[2] == 'x') { value |= 0100u; }
  else if (text[2] == 's') { value |= 0100u | 04000u; }
  else if (text[2] == 'S') { value |= 04000u; }
  else if (text[2] != '-') { return 1; }

  if (text[3] == 'r') { value |= 0040u; } else if (text[3] != '-') { return 1; }
  if (text[4] == 'w') { value |= 0020u; } else if (text[4] != '-') { return 1; }
  if (text[5] == 'x') { value |= 0010u; }
  else if (text[5] == 's') { value |= 0010u | 02000u; }
  else if (text[5] == 'S') { value |= 02000u; }
  else if (text[5] != '-') { return 1; }

  if (text[6] == 'r') { value |= 0004u; } else if (text[6] != '-') { return 1; }
  if (text[7] == 'w') { value |= 0002u; } else if (text[7] != '-') { return 1; }
  if (text[8] == 'x') { value |= 0001u; }
  else if (text[8] == 't') { value |= 0001u | 01000u; }
  else if (text[8] == 'T') { value |= 01000u; }
  else if (text[8] != '-') { return 1; }

  *out_permissions = value;
  return 0;
}

static int parse_permissions_spec(const char *text, unsigned int *out_permissions) {
  if (parse_octal_permissions(text, out_permissions) == 0) {
    return 0;
  }
  return parse_rwx_permissions(text, out_permissions);
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

/* JSON-safe number formatter: NaN and Infinity map to null per RFC 8259 */
static void format_number_json(double number, char *out, size_t out_size) {
  if (isnan(number) || isinf(number)) {
    snprintf(out, out_size, "null");
    return;
  }
  format_number(number, out, out_size);
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
          char esc[8];
          snprintf(esc, sizeof(esc), "\\u%04x", (unsigned int) c);
          if (append_text(out, out_size, &length, esc) != 0) {
            return 1;
          }
          break;
        }
        if (append_char(out, out_size, &length, (char) c) != 0) {
          return 1;
        }
        break;
    }
  }

  return append_char(out, out_size, &length, '"');
}

#define ARKSH_MAX_VALUE_PATH_SEGMENTS 128

typedef struct {
  char text[ARKSH_MAX_NAME];
  int bracketed;
} ArkshValuePathSegment;

static int value_is_map_like(const ArkshValue *value) {
  return value != NULL && (value->kind == ARKSH_VALUE_MAP || value->kind == ARKSH_VALUE_DICT);
}

static ArkshValueKind map_like_kind_for(const ArkshValue *value) {
  if (value != NULL && value->kind == ARKSH_VALUE_DICT) {
    return ARKSH_VALUE_DICT;
  }
  return ARKSH_VALUE_MAP;
}

static void set_map_like_kind(ArkshValue *value, ArkshValueKind kind) {
  if (kind == ARKSH_VALUE_DICT) {
    arksh_value_set_dict(value);
  } else {
    arksh_value_set_map(value);
  }
}

static int parse_path_index(const char *text, size_t *out_index) {
  size_t value = 0;
  size_t i;

  if (text == NULL || text[0] == '\0' || out_index == NULL) {
    return 1;
  }

  for (i = 0; text[i] != '\0'; ++i) {
    unsigned char c = (unsigned char) text[i];
    if (!isdigit(c)) {
      return 1;
    }
    value = value * 10u + (size_t) (c - '0');
  }

  *out_index = value;
  return 0;
}

static int parse_value_path_segments(
  const char *path,
  ArkshValuePathSegment segments[],
  size_t max_segments,
  size_t *out_count,
  char *error,
  size_t error_size
) {
  const char *cursor;
  size_t count = 0;

  if (path == NULL || segments == NULL || max_segments == 0 || out_count == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  cursor = path;
  while (*cursor != '\0' && isspace((unsigned char) *cursor)) {
    cursor++;
  }

  while (*cursor != '\0') {
    size_t len = 0;

    while (*cursor == '.') {
      cursor++;
    }
    if (*cursor == '\0') {
      break;
    }
    if (count >= max_segments) {
      snprintf(error, error_size, "value path is too deep");
      return 1;
    }

    memset(&segments[count], 0, sizeof(segments[count]));
    if (*cursor == '[') {
      cursor++;
      segments[count].bracketed = 1;
      while (*cursor != '\0' && *cursor != ']') {
        if (len + 1 >= sizeof(segments[count].text)) {
          snprintf(error, error_size, "value path segment is too long");
          return 1;
        }
        segments[count].text[len++] = *cursor++;
      }
      if (*cursor != ']') {
        snprintf(error, error_size, "unterminated [index] in value path");
        return 1;
      }
      cursor++;
      if (segments[count].text[0] == '\0') {
        snprintf(error, error_size, "empty [index] in value path");
        return 1;
      }
      count++;
      continue;
    }

    while (*cursor != '\0' && *cursor != '.' && *cursor != '[') {
      if (len + 1 >= sizeof(segments[count].text)) {
        snprintf(error, error_size, "value path segment is too long");
        return 1;
      }
      segments[count].text[len++] = *cursor++;
    }

    if (segments[count].text[0] == '\0') {
      snprintf(error, error_size, "empty segment in value path");
      return 1;
    }
    count++;
  }

  if (count == 0) {
    snprintf(error, error_size, "value path must not be empty");
    return 1;
  }

  *out_count = count;
  return 0;
}

static int replace_value_item(ArkshValueItem *item, const ArkshValue *value) {
  if (item == NULL || value == NULL) {
    return 1;
  }

  arksh_value_item_free(item);
  return arksh_value_item_set_from_value(item, value);
}

static int set_path_recursive(
  const ArkshValue *current,
  ArkshValueKind default_kind,
  const ArkshValuePathSegment segments[],
  size_t segment_count,
  size_t segment_index,
  const ArkshValue *replacement,
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  const ArkshValue *base = current;
  ArkshValue temp_base;

  if (segments == NULL || replacement == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (segment_index >= segment_count) {
    return arksh_value_copy(out_value, replacement);
  }

  arksh_value_init(&temp_base);
  if (base == NULL || base->kind == ARKSH_VALUE_EMPTY) {
    if (segments[segment_index].bracketed) {
      snprintf(error, error_size, "set_path() cannot auto-create a list at [%s]", segments[segment_index].text);
      return 1;
    }
    set_map_like_kind(&temp_base, default_kind);
    base = &temp_base;
  }

  if (value_is_map_like(base)) {
    const ArkshValueItem *entry = arksh_value_map_get_item(base, segments[segment_index].text);
    ArkshValue nested;
    ArkshValue updated;
    int status;

    arksh_value_init(&nested);
    arksh_value_init(&updated);
    if (entry != NULL && arksh_value_set_from_item(&nested, entry) != 0) {
      arksh_value_free(&temp_base);
      snprintf(error, error_size, "set_path() failed to copy nested value");
      return 1;
    }

    status = set_path_recursive(
      entry != NULL ? &nested : NULL,
      map_like_kind_for(base),
      segments,
      segment_count,
      segment_index + 1,
      replacement,
      &updated,
      error,
      error_size
    );
    if (status != 0) {
      arksh_value_free(&nested);
      arksh_value_free(&updated);
      arksh_value_free(&temp_base);
      return 1;
    }

    if (arksh_value_copy(out_value, base) != 0 || arksh_value_map_set(out_value, segments[segment_index].text, &updated) != 0) {
      arksh_value_free(&nested);
      arksh_value_free(&updated);
      arksh_value_free(&temp_base);
      snprintf(error, error_size, "set_path() failed to update map");
      return 1;
    }

    arksh_value_free(&nested);
    arksh_value_free(&updated);
    arksh_value_free(&temp_base);
    return 0;
  }

  if (base->kind == ARKSH_VALUE_LIST) {
    size_t index;
    ArkshValue nested;
    ArkshValue updated;

    if (parse_path_index(segments[segment_index].text, &index) != 0) {
      arksh_value_free(&temp_base);
      snprintf(error, error_size, "set_path() expected a list index, got %s", segments[segment_index].text);
      return 1;
    }
    if (index >= base->list.count) {
      arksh_value_free(&temp_base);
      snprintf(error, error_size, "set_path() list index out of range: %s", segments[segment_index].text);
      return 1;
    }
    if (arksh_value_copy(out_value, base) != 0) {
      arksh_value_free(&temp_base);
      snprintf(error, error_size, "set_path() failed to copy list");
      return 1;
    }
    if (segment_index + 1 == segment_count) {
      if (replace_value_item(&out_value->list.items[index], replacement) != 0) {
        arksh_value_free(&temp_base);
        snprintf(error, error_size, "set_path() failed to replace list item");
        return 1;
      }
      arksh_value_free(&temp_base);
      return 0;
    }

    arksh_value_init(&nested);
    arksh_value_init(&updated);
    if (arksh_value_set_from_item(&nested, &base->list.items[index]) != 0) {
      arksh_value_free(out_value);
      arksh_value_free(&temp_base);
      snprintf(error, error_size, "set_path() failed to copy list item");
      return 1;
    }
    if (set_path_recursive(&nested, ARKSH_VALUE_MAP, segments, segment_count, segment_index + 1, replacement, &updated, error, error_size) != 0) {
      arksh_value_free(&nested);
      arksh_value_free(&updated);
      arksh_value_free(out_value);
      arksh_value_free(&temp_base);
      return 1;
    }
    if (replace_value_item(&out_value->list.items[index], &updated) != 0) {
      arksh_value_free(&nested);
      arksh_value_free(&updated);
      arksh_value_free(out_value);
      arksh_value_free(&temp_base);
      snprintf(error, error_size, "set_path() failed to update list item");
      return 1;
    }

    arksh_value_free(&nested);
    arksh_value_free(&updated);
    arksh_value_free(&temp_base);
    return 0;
  }

  arksh_value_free(&temp_base);
  snprintf(error, error_size, "set_path() cannot descend into a %s value", arksh_value_kind_name(base->kind));
  return 1;
}

void arksh_value_item_free(ArkshValueItem *item) {
  if (item == NULL) {
    return;
  }

  if (!item_is_initialized(item)) {
    memset(item, 0, sizeof(*item));
    item->signature = ARKSH_VALUE_ITEM_SIGNATURE;
    item->kind = ARKSH_VALUE_EMPTY;
    return;
  }

  if (item->nested != NULL) {
    arksh_value_free(item->nested);
    free(item->nested);
    item->nested = NULL;
  }

  free_item_scalar_payload(item);

  memset(item, 0, sizeof(*item));
  item->signature = ARKSH_VALUE_ITEM_SIGNATURE;
  item->kind = ARKSH_VALUE_EMPTY;
}

void arksh_value_free(ArkshValue *value) {
  size_t i;

  if (value == NULL) {
    return;
  }

  if (!value_is_initialized(value)) {
    memset(value, 0, sizeof(*value));
    value->signature = ARKSH_VALUE_SIGNATURE;
    value->kind = ARKSH_VALUE_EMPTY;
    return;
  }

  if (value->kind == ARKSH_VALUE_LIST) {
    for (i = 0; i < value->list.count; ++i) {
      arksh_value_item_free(&value->list.items[i]);
    }
    free(value->list.items);
  } else if (value->kind == ARKSH_VALUE_MAP || value->kind == ARKSH_VALUE_DICT) {
    for (i = 0; i < value->map.count; ++i) {
      arksh_value_item_free(&value->map.entries[i].value);
    }
    free(value->map.entries);
  } else if (value->kind == ARKSH_VALUE_MATRIX) {
    free(value->matrix);
  }

  free_value_scalar_payload(value);

  memset(value, 0, sizeof(*value));
  value->signature = ARKSH_VALUE_SIGNATURE;
  value->kind = ARKSH_VALUE_EMPTY;
}

int arksh_value_item_copy(ArkshValueItem *dest, const ArkshValueItem *src);
int arksh_value_copy(ArkshValue *dest, const ArkshValue *src);

int arksh_value_item_copy(ArkshValueItem *dest, const ArkshValueItem *src) {
  if (dest == NULL || src == NULL) {
    return 1;
  }

  if (dest == src) {
    return 0;
  }

  if (item_is_initialized(dest)) {
    arksh_value_item_free(dest);
  } else {
    arksh_value_item_init(dest);
  }
  dest->kind = src->kind;
  dest->number = src->number;
  dest->boolean = src->boolean;
  if (assign_item_object(dest, src->object) != 0 ||
      assign_item_block(dest, src->block) != 0 ||
      assign_item_text(dest, src->text) != 0) {
    arksh_value_item_free(dest);
    return 1;
  }

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

  arksh_perf_note_value_copy();

  if (dest == src) {
    return 0;
  }

  if (value_is_initialized(dest)) {
    arksh_value_free(dest);
  } else {
    arksh_value_init(dest);
  }
  dest->kind = src->kind;
  dest->number = src->number;
  dest->boolean = src->boolean;
  if (assign_value_object(dest, src->object) != 0 ||
      assign_value_block(dest, src->block) != 0 ||
      assign_value_text(dest, src->text) != 0) {
    arksh_value_free(dest);
    return 1;
  }

  if (src->kind == ARKSH_VALUE_LIST) {
    if (src->list.count > 0 && ensure_list_capacity(dest, src->list.count) != 0) {
      arksh_value_free(dest);
      return 1;
    }
    for (i = 0; i < src->list.count; ++i) {
      if (arksh_value_item_copy(&dest->list.items[dest->list.count], &src->list.items[i]) != 0) {
        arksh_value_free(dest);
        return 1;
      }
      dest->list.count++;
    }
  } else if (src->kind == ARKSH_VALUE_MAP || src->kind == ARKSH_VALUE_DICT) {
    if (src->map.count > 0 && ensure_map_capacity(dest, src->map.count) != 0) {
      arksh_value_free(dest);
      return 1;
    }
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

  if (item_is_initialized(item)) {
    arksh_value_item_free(item);
  } else {
    arksh_value_item_init(item);
  }
  item->kind = value->kind;
  item->number = value->number;
  item->boolean = value->boolean;
  if (assign_item_object(item, value->object) != 0 ||
      assign_item_block(item, value->block) != 0 ||
      assign_item_text(item, value->text) != 0) {
    arksh_value_item_free(item);
    return 1;
  }

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
      return append_json_escaped_string(out, out_size, arksh_value_item_text_cstr(item));
    case ARKSH_VALUE_NUMBER:
      format_number_json(item->number, out, out_size);
      return 0;
    case ARKSH_VALUE_BOOLEAN:
      copy_string(out, out_size, item->boolean ? "true" : "false");
      return 0;
    case ARKSH_VALUE_OBJECT:
      return append_json_escaped_string(out, out_size, arksh_value_item_object_ref(item)->path);
    case ARKSH_VALUE_BLOCK:
      return append_json_escaped_string(out, out_size, arksh_value_item_block_ref(item)->source);
    case ARKSH_VALUE_CLASS:
      return append_json_escaped_string(out, out_size, arksh_value_item_text_cstr(item));
    case ARKSH_VALUE_INSTANCE: {
      char label[ARKSH_MAX_OUTPUT];

      snprintf(label, sizeof(label), "%s#%d", arksh_value_item_text_cstr(item), (int) item->number);
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
      return append_json_escaped_string(out, out_size, arksh_value_text_cstr(value));
    case ARKSH_VALUE_NUMBER:
      format_number_json(value->number, out, out_size);
      return 0;
    case ARKSH_VALUE_BOOLEAN:
      copy_string(out, out_size, value->boolean ? "true" : "false");
      return 0;
    case ARKSH_VALUE_OBJECT:
      return append_json_escaped_string(out, out_size, arksh_value_object_ref(value)->path);
    case ARKSH_VALUE_BLOCK:
      return append_json_escaped_string(out, out_size, arksh_value_block_ref(value)->source);
    case ARKSH_VALUE_CLASS:
      return append_json_escaped_string(out, out_size, arksh_value_text_cstr(value));
    case ARKSH_VALUE_INSTANCE: {
      char label[ARKSH_MAX_OUTPUT];

      snprintf(label, sizeof(label), "%s#%d", arksh_value_text_cstr(value), (int) value->number);
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
    /* E7-S1-T3: Matrix serializes as JSON array of row-objects */
    case ARKSH_VALUE_MATRIX: {
      size_t r, c;
      if (value->matrix == NULL || value->matrix->row_count == 0) {
        copy_string(out, out_size, "[]");
        return 0;
      }
      out[0] = '\0';
      if (append_char(out, out_size, &length, '[') != 0) {
        return 1;
      }
      for (r = 0; r < value->matrix->row_count; ++r) {
        char obj_buf[ARKSH_MAX_OUTPUT];
        size_t obj_len = 0;

        if (r > 0 && append_char(out, out_size, &length, ',') != 0) {
          return 1;
        }
        obj_buf[0] = '\0';
        if (append_char(obj_buf, sizeof(obj_buf), &obj_len, '{') != 0) {
          return 1;
        }
        for (c = 0; c < value->matrix->col_count; ++c) {
          const ArkshMatrixCell *cell = &value->matrix->rows[r][c];
          char key_json[ARKSH_MAX_NAME + 4];
          char cell_json[ARKSH_MAX_MATRIX_CELL_TEXT + 8];

          if (append_json_escaped_string(key_json, sizeof(key_json), value->matrix->col_names[c]) != 0) {
            return 1;
          }
          switch (cell->kind) {
            case ARKSH_VALUE_NUMBER:
              format_number_json(cell->number, cell_json, sizeof(cell_json));
              break;
            case ARKSH_VALUE_BOOLEAN:
              copy_string(cell_json, sizeof(cell_json), cell->boolean ? "true" : "false");
              break;
            case ARKSH_VALUE_EMPTY:
              copy_string(cell_json, sizeof(cell_json), "null");
              break;
            default:
              if (append_json_escaped_string(cell_json, sizeof(cell_json), cell->text) != 0) {
                return 1;
              }
              break;
          }
          if (c > 0 && append_char(obj_buf, sizeof(obj_buf), &obj_len, ',') != 0) {
            return 1;
          }
          if (append_text(obj_buf, sizeof(obj_buf), &obj_len, key_json) != 0 ||
              append_char(obj_buf, sizeof(obj_buf), &obj_len, ':') != 0 ||
              append_text(obj_buf, sizeof(obj_buf), &obj_len, cell_json) != 0) {
            return 1;
          }
        }
        if (append_char(obj_buf, sizeof(obj_buf), &obj_len, '}') != 0 ||
            append_text(out, out_size, &length, obj_buf) != 0) {
          return 1;
        }
      }
      return append_char(out, out_size, &length, ']');
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

static int json_parse_unicode_escape(const char **cursor, char *out, size_t out_size, size_t *length, char *error, size_t error_size);

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
    unsigned char c = (unsigned char) **cursor;

    /* RFC 8259: unescaped control characters (U+0000-U+001F) are not allowed */
    if (c < 0x20 && c != '\\') {
      snprintf(error, error_size, "invalid JSON string: unescaped control character (0x%02x)", (unsigned int) c);
      return 1;
    }

    if (c != '\\') {
      if (append_char(out, out_size, &length, (char) c) != 0) {
        snprintf(error, error_size, "JSON string is too large");
        return 1;
      }
      (*cursor)++;
      continue;
    }

    /* backslash escape: consume '\' then the escape character */
    (*cursor)++;
    c = (unsigned char) **cursor;
    if (c == '\0') {
      snprintf(error, error_size, "unterminated JSON escape");
      return 1;
    }
    (*cursor)++; /* consume escape character */

    switch (c) {
      case '"':
        if (append_char(out, out_size, &length, '"') != 0) {
          snprintf(error, error_size, "JSON string is too large");
          return 1;
        }
        break;
      case '\\':
        if (append_char(out, out_size, &length, '\\') != 0) {
          snprintf(error, error_size, "JSON string is too large");
          return 1;
        }
        break;
      case '/':
        if (append_char(out, out_size, &length, '/') != 0) {
          snprintf(error, error_size, "JSON string is too large");
          return 1;
        }
        break;
      case 'b':
        if (append_char(out, out_size, &length, '\b') != 0) {
          snprintf(error, error_size, "JSON string is too large");
          return 1;
        }
        break;
      case 'f':
        if (append_char(out, out_size, &length, '\f') != 0) {
          snprintf(error, error_size, "JSON string is too large");
          return 1;
        }
        break;
      case 'n':
        if (append_char(out, out_size, &length, '\n') != 0) {
          snprintf(error, error_size, "JSON string is too large");
          return 1;
        }
        break;
      case 'r':
        if (append_char(out, out_size, &length, '\r') != 0) {
          snprintf(error, error_size, "JSON string is too large");
          return 1;
        }
        break;
      case 't':
        if (append_char(out, out_size, &length, '\t') != 0) {
          snprintf(error, error_size, "JSON string is too large");
          return 1;
        }
        break;
      case 'u':
        /* cursor already advanced past 'u'; points at first hex digit */
        if (json_parse_unicode_escape(cursor, out, out_size, &length, error, error_size) != 0) {
          return 1;
        }
        /* cursor already advanced past 4 (or 8) hex digits by the helper */
        continue; /* skip the extra (*cursor)++ that non-unicode cases rely on */
      default:
        snprintf(error, error_size, "invalid JSON escape: '\\%c'", (char) c);
        return 1;
    }
  }

  if (**cursor != '"') {
    snprintf(error, error_size, "unterminated JSON string");
    return 1;
  }

  (*cursor)++;
  return 0;
}

static int json_parse_string_alloc(const char **cursor, char **out_text, char *error, size_t error_size) {
  char buffer[ARKSH_MAX_OUTPUT];

  if (out_text == NULL) {
    return 1;
  }

  *out_text = NULL;
  if (json_parse_string(cursor, buffer, sizeof(buffer), error, error_size) != 0) {
    return 1;
  }
  *out_text = duplicate_string_owned(buffer);
  if (buffer[0] != '\0' && *out_text == NULL) {
    snprintf(error, error_size, "out of memory");
    return 1;
  }
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

#define JSON_MAX_DEPTH 128

static int json_parse_value_internal(const char **cursor, ArkshValue *out_value, char *error, size_t error_size, int depth);
static int json_parse_object(const char **cursor, ArkshValue *out_value, char *error, size_t error_size, int depth);

/* Decode a \uXXXX (or \uXXXX\uXXXX surrogate pair) escape into UTF-8.
   Called with *cursor pointing at the first hex digit (i.e. after 'u'). */
static int json_parse_unicode_escape(const char **cursor, char *out, size_t out_size, size_t *length, char *error, size_t error_size) {
  unsigned int cp = 0;
  int i;
  char hex[5];

  for (i = 0; i < 4; ++i) {
    char h = **cursor;
    if ((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F')) {
      hex[i] = h;
      (*cursor)++;
    } else {
      snprintf(error, error_size, "invalid \\u escape: expected 4 hex digits");
      return 1;
    }
  }
  hex[4] = '\0';
  cp = (unsigned int) strtoul(hex, NULL, 16);

  /* high surrogate: D800-DBFF must be followed by low surrogate DC00-DFFF */
  if (cp >= 0xD800 && cp <= 0xDBFF) {
    unsigned int high = cp;
    unsigned int low;

    if ((*cursor)[0] != '\\' || (*cursor)[1] != 'u') {
      snprintf(error, error_size, "invalid \\u escape: high surrogate not followed by \\uXXXX");
      return 1;
    }
    *cursor += 2;
    for (i = 0; i < 4; ++i) {
      char h = **cursor;
      if ((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F')) {
        hex[i] = h;
        (*cursor)++;
      } else {
        snprintf(error, error_size, "invalid \\u escape: expected 4 hex digits in low surrogate");
        return 1;
      }
    }
    hex[4] = '\0';
    low = (unsigned int) strtoul(hex, NULL, 16);
    if (low < 0xDC00 || low > 0xDFFF) {
      snprintf(error, error_size, "invalid \\u escape: expected low surrogate (DC00-DFFF)");
      return 1;
    }
    cp = 0x10000u + (high - 0xD800u) * 0x400u + (low - 0xDC00u);
  } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
    snprintf(error, error_size, "invalid \\u escape: unexpected low surrogate");
    return 1;
  }

  /* encode codepoint as UTF-8 */
  if (cp <= 0x7F) {
    return append_char(out, out_size, length, (char) cp);
  } else if (cp <= 0x7FF) {
    if (append_char(out, out_size, length, (char) (0xC0u | (cp >> 6))) != 0) return 1;
    return append_char(out, out_size, length, (char) (0x80u | (cp & 0x3Fu)));
  } else if (cp <= 0xFFFF) {
    if (append_char(out, out_size, length, (char) (0xE0u | (cp >> 12))) != 0) return 1;
    if (append_char(out, out_size, length, (char) (0x80u | ((cp >> 6) & 0x3Fu))) != 0) return 1;
    return append_char(out, out_size, length, (char) (0x80u | (cp & 0x3Fu)));
  } else {
    if (append_char(out, out_size, length, (char) (0xF0u | (cp >> 18))) != 0) return 1;
    if (append_char(out, out_size, length, (char) (0x80u | ((cp >> 12) & 0x3Fu))) != 0) return 1;
    if (append_char(out, out_size, length, (char) (0x80u | ((cp >> 6) & 0x3Fu))) != 0) return 1;
    return append_char(out, out_size, length, (char) (0x80u | (cp & 0x3Fu)));
  }
}

static int json_parse_array(const char **cursor, ArkshValue *out_value, char *error, size_t error_size, int depth) {
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
    ArkshValue *parsed_value = allocate_value(error, error_size);

    if (parsed_value == NULL) {
      return 1;
    }
    if (json_parse_value_internal(cursor, parsed_value, error, error_size, depth) != 0) {
      free(parsed_value);
      return 1;
    }
    if (arksh_value_list_append_value(out_value, parsed_value) != 0) {
      arksh_value_free(parsed_value);
      free(parsed_value);
      if (error[0] == '\0') {
        snprintf(error, error_size, "unable to grow JSON array");
      }
      return 1;
    }
    arksh_value_free(parsed_value);
    free(parsed_value);

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

static int json_parse_object(const char **cursor, ArkshValue *out_value, char *error, size_t error_size, int depth) {
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
    ArkshValue *parsed_value = allocate_value(error, error_size);

    if (parsed_value == NULL) {
      return 1;
    }

    if (**cursor != '"') {
      free(parsed_value);
      snprintf(error, error_size, "expected JSON object key");
      return 1;
    }
    if (json_parse_string(cursor, key, sizeof(key), error, error_size) != 0) {
      free(parsed_value);
      return 1;
    }

    json_skip_ws(cursor);
    if (**cursor != ':') {
      free(parsed_value);
      snprintf(error, error_size, "expected ':' after JSON object key");
      return 1;
    }
    (*cursor)++;
    json_skip_ws(cursor);

    if (json_parse_value_internal(cursor, parsed_value, error, error_size, depth) != 0) {
      free(parsed_value);
      return 1;
    }
    if (arksh_value_map_set(out_value, key, parsed_value) != 0) {
      arksh_value_free(parsed_value);
      free(parsed_value);
      snprintf(error, error_size, "unable to grow JSON object");
      return 1;
    }
    arksh_value_free(parsed_value);
    free(parsed_value);

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

static int json_parse_value_internal(const char **cursor, ArkshValue *out_value, char *error, size_t error_size, int depth) {
  if (cursor == NULL || *cursor == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (depth > JSON_MAX_DEPTH) {
    snprintf(error, error_size, "JSON nesting too deep (max %d levels)", JSON_MAX_DEPTH);
    return 1;
  }

  arksh_value_init(out_value);
  json_skip_ws(cursor);
  if (**cursor == '"') {
    char *parsed_text = NULL;

    if (json_parse_string_alloc(cursor, &parsed_text, error, error_size) != 0) {
      return 1;
    }
    out_value->kind = ARKSH_VALUE_STRING;
    out_value->text = parsed_text;
    return 0;
  }
  if (**cursor == '[') {
    return json_parse_array(cursor, out_value, error, error_size, depth + 1);
  }
  if (**cursor == '{') {
    return json_parse_object(cursor, out_value, error, error_size, depth + 1);
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
    const char *num_start = *cursor;
    /* RFC 8259: leading zeros are not allowed (e.g. 01, -01) */
    if (*num_start == '-') num_start++;
    if (num_start[0] == '0' && num_start[1] >= '0' && num_start[1] <= '9') {
      snprintf(error, error_size, "invalid JSON number: leading zero not allowed");
      return 1;
    }
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
      copy_string(out, out_size, arksh_value_item_text_cstr(item));
      return 0;
    case ARKSH_VALUE_NUMBER:
      format_number(item->number, out, out_size);
      return 0;
    case ARKSH_VALUE_BOOLEAN:
      copy_string(out, out_size, item->boolean ? "true" : "false");
      return 0;
    case ARKSH_VALUE_OBJECT:
      snprintf(
        out,
        out_size,
        "%s\t%s",
        arksh_object_kind_name(arksh_value_item_object_ref(item)->kind),
        arksh_value_item_object_ref(item)->path
      );
      return 0;
    case ARKSH_VALUE_BLOCK:
      copy_string(out, out_size, arksh_value_item_block_ref(item)->source);
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
  value->signature = ARKSH_VALUE_SIGNATURE;
  value->kind = ARKSH_VALUE_EMPTY;
}

void arksh_value_item_init(ArkshValueItem *item) {
  if (item == NULL) {
    return;
  }

  memset(item, 0, sizeof(*item));
  item->signature = ARKSH_VALUE_ITEM_SIGNATURE;
  item->kind = ARKSH_VALUE_EMPTY;
}

const char *arksh_value_text_cstr(const ArkshValue *value) {
  return (value != NULL && value->text != NULL) ? value->text : "";
}

const char *arksh_value_item_text_cstr(const ArkshValueItem *item) {
  return (item != NULL && item->text != NULL) ? item->text : "";
}

const ArkshObject *arksh_value_object_ref(const ArkshValue *value) {
  return (value != NULL && value->object != NULL) ? value->object : &EMPTY_OBJECT_VALUE;
}

const ArkshObject *arksh_value_item_object_ref(const ArkshValueItem *item) {
  return (item != NULL && item->object != NULL) ? item->object : &EMPTY_OBJECT_VALUE;
}

const ArkshBlock *arksh_value_block_ref(const ArkshValue *value) {
  return (value != NULL && value->block != NULL) ? value->block : &EMPTY_BLOCK_VALUE;
}

const ArkshBlock *arksh_value_item_block_ref(const ArkshValueItem *item) {
  return (item != NULL && item->block != NULL) ? item->block : &EMPTY_BLOCK_VALUE;
}

void arksh_value_set_string(ArkshValue *value, const char *text) {
  if (value == NULL) {
    return;
  }

  if (value_is_initialized(value)) {
    arksh_value_free(value);
  } else {
    arksh_value_init(value);
  }
  value->kind = ARKSH_VALUE_STRING;
  if (assign_value_text(value, text) != 0) {
    arksh_value_init(value);
  }
}

void arksh_value_set_number(ArkshValue *value, double number) {
  if (value == NULL) {
    return;
  }

  if (value_is_initialized(value)) {
    arksh_value_free(value);
  } else {
    arksh_value_init(value);
  }
  value->kind = ARKSH_VALUE_NUMBER;
  value->number = number;
}

/* E6-S5: explicit numeric sub-kind setters */
void arksh_value_set_integer(ArkshValue *value, double number) {
  if (value == NULL) { return; }
  if (value_is_initialized(value)) arksh_value_free(value); else arksh_value_init(value);
  value->kind = ARKSH_VALUE_INTEGER;
  value->number = (double)(long long) number;
}

void arksh_value_set_float(ArkshValue *value, double number) {
  if (value == NULL) { return; }
  if (value_is_initialized(value)) arksh_value_free(value); else arksh_value_init(value);
  value->kind = ARKSH_VALUE_FLOAT;
  value->number = (double)(float) number;
}

void arksh_value_set_double(ArkshValue *value, double number) {
  if (value == NULL) { return; }
  if (value_is_initialized(value)) arksh_value_free(value); else arksh_value_init(value);
  value->kind = ARKSH_VALUE_DOUBLE;
  value->number = number;
}

void arksh_value_set_imaginary(ArkshValue *value, double number) {
  if (value == NULL) { return; }
  if (value_is_initialized(value)) arksh_value_free(value); else arksh_value_init(value);
  value->kind = ARKSH_VALUE_IMAGINARY;
  value->number = number;
}

void arksh_value_set_boolean(ArkshValue *value, int boolean) {
  if (value == NULL) {
    return;
  }

  if (value_is_initialized(value)) {
    arksh_value_free(value);
  } else {
    arksh_value_init(value);
  }
  value->kind = ARKSH_VALUE_BOOLEAN;
  value->boolean = boolean ? 1 : 0;
}

void arksh_value_set_object(ArkshValue *value, const ArkshObject *object) {
  if (value == NULL) {
    return;
  }

  if (value_is_initialized(value)) {
    arksh_value_free(value);
  } else {
    arksh_value_init(value);
  }
  value->kind = ARKSH_VALUE_OBJECT;
  if (assign_value_object(value, object) != 0) {
    arksh_value_init(value);
  }
}

void arksh_value_set_block(ArkshValue *value, const ArkshBlock *block) {
  if (value == NULL) {
    return;
  }

  if (value_is_initialized(value)) {
    arksh_value_free(value);
  } else {
    arksh_value_init(value);
  }
  value->kind = ARKSH_VALUE_BLOCK;
  if (assign_value_block(value, block) != 0) {
    arksh_value_init(value);
  }
}

void arksh_value_set_class(ArkshValue *value, const char *class_name) {
  if (value == NULL) {
    return;
  }

  if (value_is_initialized(value)) {
    arksh_value_free(value);
  } else {
    arksh_value_init(value);
  }
  value->kind = ARKSH_VALUE_CLASS;
  if (assign_value_text(value, class_name) != 0) {
    arksh_value_init(value);
  }
}

void arksh_value_set_instance(ArkshValue *value, const char *class_name, int instance_id) {
  if (value == NULL) {
    return;
  }

  if (value_is_initialized(value)) {
    arksh_value_free(value);
  } else {
    arksh_value_init(value);
  }
  value->kind = ARKSH_VALUE_INSTANCE;
  if (assign_value_text(value, class_name) != 0) {
    arksh_value_init(value);
    return;
  }
  value->number = (double) instance_id;
}

void arksh_value_set_map(ArkshValue *value) {
  if (value == NULL) {
    return;
  }

  if (value_is_initialized(value)) {
    arksh_value_free(value);
  } else {
    arksh_value_init(value);
  }
  value->kind = ARKSH_VALUE_MAP;
}

/* E6-S6: Dict — immutable key-value dictionary, stored in the map field. */
void arksh_value_set_dict(ArkshValue *value) {
  if (value == NULL) {
    return;
  }

  if (value_is_initialized(value)) {
    arksh_value_free(value);
  } else {
    arksh_value_init(value);
  }
  value->kind = ARKSH_VALUE_DICT;
}

void arksh_value_item_set_string(ArkshValueItem *item, const char *text) {
  if (item == NULL) {
    return;
  }

  if (item_is_initialized(item)) {
    arksh_value_item_free(item);
  } else {
    arksh_value_item_init(item);
  }
  item->kind = ARKSH_VALUE_STRING;
  if (assign_item_text(item, text) != 0) {
    arksh_value_item_init(item);
  }
}

void arksh_value_item_set_object(ArkshValueItem *item, const ArkshObject *object) {
  if (item == NULL) {
    return;
  }

  if (item_is_initialized(item)) {
    arksh_value_item_free(item);
  } else {
    arksh_value_item_init(item);
  }
  item->kind = ARKSH_VALUE_OBJECT;
  if (assign_item_object(item, object) != 0) {
    arksh_value_item_init(item);
  }
}

void arksh_value_item_set_block(ArkshValueItem *item, const ArkshBlock *block) {
  if (item == NULL) {
    return;
  }

  if (item_is_initialized(item)) {
    arksh_value_item_free(item);
  } else {
    arksh_value_item_init(item);
  }
  item->kind = ARKSH_VALUE_BLOCK;
  if (assign_item_block(item, block) != 0) {
    arksh_value_item_init(item);
  }
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
  ArkshValue *tag;

  if (value == NULL || type_name == NULL) {
    return;
  }

  arksh_value_set_map(value);

  /* Store the type tag as a string entry; ignore allocation failures here
   * since the caller checks properties independently. */
  tag = allocate_value(NULL, 0);
  if (tag == NULL) {
    return;
  }
  arksh_value_set_string(tag, type_name);
  arksh_value_map_set(value, "__type__", tag);
  arksh_value_free(tag);
  free(tag);
}

int arksh_value_set_from_item(ArkshValue *value, const ArkshValueItem *item) {
  if (value == NULL || item == NULL) {
    return 1;
  }

  if (value_is_initialized(value)) {
    arksh_value_free(value);
  } else {
    arksh_value_init(value);
  }
  value->kind = item->kind;
  value->number = item->number;
  value->boolean = item->boolean;
  if (assign_value_object(value, item->object) != 0 ||
      assign_value_block(value, item->block) != 0 ||
      assign_value_text(value, item->text) != 0) {
    arksh_value_free(value);
    return 1;
  }

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

  if (ensure_list_capacity(value, value->list.count + 1) != 0) {
    return 1;
  }

  if (arksh_value_item_copy(&value->list.items[value->list.count], item) != 0) {
    return 1;
  }
  value->list.count++;
  return 0;
}

int arksh_value_list_append_value(ArkshValue *value, const ArkshValue *item_value) {
  ArkshValueItem *item;

  if (value == NULL || item_value == NULL) {
    return 1;
  }

  item = allocate_value_item(NULL, 0);
  if (item == NULL) {
    return 1;
  }
  arksh_value_item_init(item);
  if (arksh_value_item_set_from_value(item, item_value) != 0) {
    free(item);
    return 1;
  }
  if (arksh_value_list_append_item(value, item) != 0) {
    arksh_value_item_free(item);
    free(item);
    return 1;
  }
  arksh_value_item_free(item);
  free(item);
  return 0;
}

int arksh_value_map_set(ArkshValue *value, const char *key, const ArkshValue *entry_value) {
  size_t i;
  ArkshValueItem *item;

  if (value == NULL || key == NULL || key[0] == '\0' || entry_value == NULL) {
    return 1;
  }

  if (value->kind == ARKSH_VALUE_EMPTY) {
    value->kind = ARKSH_VALUE_MAP;
  }
  if (value->kind != ARKSH_VALUE_MAP && value->kind != ARKSH_VALUE_DICT) {
    return 1;
  }

  item = allocate_value_item(NULL, 0);
  if (item == NULL) {
    return 1;
  }
  arksh_value_item_init(item);
  if (arksh_value_item_set_from_value(item, entry_value) != 0) {
    free(item);
    return 1;
  }

  for (i = 0; i < value->map.count; ++i) {
    if (strcmp(value->map.entries[i].key, key) == 0) {
      arksh_value_item_free(&value->map.entries[i].value);
      value->map.entries[i].value = *item;
      free(item);
      return 0;
    }
  }

  if (ensure_map_capacity(value, value->map.count + 1) != 0) {
    arksh_value_item_free(item);
    free(item);
    return 1;
  }

  copy_string(value->map.entries[value->map.count].key, sizeof(value->map.entries[value->map.count].key), key);
  value->map.entries[value->map.count].value = *item;
  value->map.count++;
  free(item);
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
      copy_string(out, out_size, arksh_value_item_text_cstr(item));
      return 0;
    case ARKSH_VALUE_NUMBER:
      format_number(item->number, out, out_size);
      return 0;
    case ARKSH_VALUE_BOOLEAN:
      copy_string(out, out_size, item->boolean ? "true" : "false");
      return 0;
    case ARKSH_VALUE_OBJECT:
      return arksh_object_call_method(arksh_value_item_object_ref(item), "describe", 0, NULL, out, out_size);
    case ARKSH_VALUE_BLOCK:
      copy_string(out, out_size, arksh_value_item_block_ref(item)->source);
      return 0;
    case ARKSH_VALUE_CLASS:
      snprintf(out, out_size, "class %s", arksh_value_item_text_cstr(item));
      return 0;
    case ARKSH_VALUE_INSTANCE:
      snprintf(out, out_size, "%s#%d", arksh_value_item_text_cstr(item), (int) item->number);
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

  arksh_perf_note_value_render();

  if (value == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  out[0] = '\0';

  switch (value->kind) {
    case ARKSH_VALUE_STRING:
      copy_string(out, out_size, arksh_value_text_cstr(value));
      return 0;
    case ARKSH_VALUE_NUMBER:
      format_number(value->number, out, out_size);
      return 0;
    case ARKSH_VALUE_BOOLEAN:
      copy_string(out, out_size, value->boolean ? "true" : "false");
      return 0;
    case ARKSH_VALUE_OBJECT:
      return arksh_object_call_method(arksh_value_object_ref(value), "describe", 0, NULL, out, out_size);
    case ARKSH_VALUE_BLOCK:
      copy_string(out, out_size, arksh_value_block_ref(value)->source);
      return 0;
    case ARKSH_VALUE_CLASS:
      snprintf(out, out_size, "class %s", arksh_value_text_cstr(value));
      return 0;
    case ARKSH_VALUE_INSTANCE:
      snprintf(out, out_size, "%s#%d", arksh_value_text_cstr(value), (int) value->number);
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
  size_t offset;
  size_t msg_len;

  if (text == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  error[0] = '\0';
  arksh_value_init(out_value);
  cursor = text;
  if (json_parse_value_internal(&cursor, out_value, error, error_size, 0) != 0) {
    /* append byte offset so callers can pinpoint the error location */
    offset = (size_t) (cursor - text);
    msg_len = strlen(error);
    if (msg_len > 0 && msg_len + 24 < error_size) {
      snprintf(error + msg_len, error_size - msg_len, " (at offset %zu)", offset);
    }
    return 1;
  }

  json_skip_ws(&cursor);
  if (*cursor != '\0') {
    offset = (size_t) (cursor - text);
    snprintf(error, error_size, "unexpected trailing JSON content (at offset %zu)", offset);
    return 1;
  }

  return 0;
}

int arksh_value_get_path(
  const ArkshValue *value,
  const char *path,
  ArkshValue *out_value,
  int *out_found,
  char *error,
  size_t error_size
) {
  ArkshValuePathSegment segments[ARKSH_MAX_VALUE_PATH_SEGMENTS];
  size_t segment_count;
  size_t i;
  ArkshValue current;
  ArkshValue next;
  int found = 1;

  if (value == NULL || path == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  error[0] = '\0';
  if (parse_value_path_segments(path, segments, ARKSH_MAX_VALUE_PATH_SEGMENTS, &segment_count, error, error_size) != 0) {
    return 1;
  }

  arksh_value_init(&current);
  arksh_value_init(&next);
  if (arksh_value_copy(&current, value) != 0) {
    snprintf(error, error_size, "unable to copy value for path lookup");
    return 1;
  }

  for (i = 0; i < segment_count; ++i) {
    if (value_is_map_like(&current)) {
      const ArkshValueItem *entry = arksh_value_map_get_item(&current, segments[i].text);

      if (entry == NULL) {
        found = 0;
        break;
      }
      arksh_value_free(&next);
      if (arksh_value_set_from_item(&next, entry) != 0) {
        arksh_value_free(&current);
        snprintf(error, error_size, "unable to read nested value at %s", segments[i].text);
        return 1;
      }
    } else if (current.kind == ARKSH_VALUE_LIST) {
      size_t index;

      if (parse_path_index(segments[i].text, &index) != 0) {
        found = 0;
        break;
      }
      if (index >= current.list.count) {
        found = 0;
        break;
      }
      arksh_value_free(&next);
      if (arksh_value_set_from_item(&next, &current.list.items[index]) != 0) {
        arksh_value_free(&current);
        snprintf(error, error_size, "unable to read list item at %s", segments[i].text);
        return 1;
      }
    } else {
      found = 0;
      break;
    }

    arksh_value_free(&current);
    if (arksh_value_copy(&current, &next) != 0) {
      arksh_value_free(&next);
      snprintf(error, error_size, "unable to continue nested path lookup");
      return 1;
    }
  }

  if (!found) {
    arksh_value_init(out_value);
    if (out_found != NULL) {
      *out_found = 0;
    }
    arksh_value_free(&current);
    arksh_value_free(&next);
    return 0;
  }

  if (arksh_value_copy(out_value, &current) != 0) {
    arksh_value_free(&current);
    arksh_value_free(&next);
    snprintf(error, error_size, "unable to copy nested value");
    return 1;
  }
  if (out_found != NULL) {
    *out_found = 1;
  }

  arksh_value_free(&current);
  arksh_value_free(&next);
  return 0;
}

int arksh_value_set_path(
  const ArkshValue *value,
  const char *path,
  const ArkshValue *replacement,
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  ArkshValuePathSegment segments[ARKSH_MAX_VALUE_PATH_SEGMENTS];
  size_t segment_count;

  if (value == NULL || path == NULL || replacement == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (!value_is_map_like(value)) {
    snprintf(error, error_size, "set_path() is only valid on map or dict values");
    return 1;
  }
  if (parse_value_path_segments(path, segments, ARKSH_MAX_VALUE_PATH_SEGMENTS, &segment_count, error, error_size) != 0) {
    return 1;
  }
  return set_path_recursive(value, map_like_kind_for(value), segments, segment_count, 0, replacement, out_value, error, error_size);
}

int arksh_value_pick(
  const ArkshValue *value,
  int key_count,
  const char keys[][ARKSH_MAX_NAME],
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  int i;
  const ArkshValueItem *type_entry;

  if (value == NULL || key_count < 0 || (key_count > 0 && keys == NULL) || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (!value_is_map_like(value)) {
    snprintf(error, error_size, "pick() is only valid on map or dict values");
    return 1;
  }

  set_map_like_kind(out_value, map_like_kind_for(value));
  if (value->kind == ARKSH_VALUE_MAP) {
    type_entry = arksh_value_map_get_item(value, "__type__");
    if (type_entry != NULL) {
      ArkshValue type_value;

      arksh_value_init(&type_value);
      if (arksh_value_set_from_item(&type_value, type_entry) != 0 || arksh_value_map_set(out_value, "__type__", &type_value) != 0) {
        arksh_value_free(&type_value);
        snprintf(error, error_size, "pick() failed to preserve type tag");
        return 1;
      }
      arksh_value_free(&type_value);
    }
  }

  for (i = 0; i < key_count; ++i) {
    const ArkshValueItem *entry = arksh_value_map_get_item(value, keys[i]);
    ArkshValue entry_value;

    if (entry == NULL) {
      continue;
    }
    arksh_value_init(&entry_value);
    if (arksh_value_set_from_item(&entry_value, entry) != 0 || arksh_value_map_set(out_value, keys[i], &entry_value) != 0) {
      arksh_value_free(&entry_value);
      snprintf(error, error_size, "pick() failed for key %s", keys[i]);
      return 1;
    }
    arksh_value_free(&entry_value);
  }

  return 0;
}

int arksh_value_merge(
  const ArkshValue *left,
  const ArkshValue *right,
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  size_t i;

  if (left == NULL || right == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (!value_is_map_like(left) || !value_is_map_like(right)) {
    snprintf(error, error_size, "merge() expects map or dict values");
    return 1;
  }
  if (arksh_value_copy(out_value, left) != 0) {
    snprintf(error, error_size, "merge() failed to copy base value");
    return 1;
  }
  for (i = 0; i < right->map.count; ++i) {
    ArkshValue entry_value;

    arksh_value_init(&entry_value);
    if (arksh_value_set_from_item(&entry_value, &right->map.entries[i].value) != 0 ||
        arksh_value_map_set(out_value, right->map.entries[i].key, &entry_value) != 0) {
      arksh_value_free(&entry_value);
      snprintf(error, error_size, "merge() failed at key %s", right->map.entries[i].key);
      return 1;
    }
    arksh_value_free(&entry_value);
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

      if (arksh_object_get_property(arksh_value_object_ref(value), "type", rendered, sizeof(rendered)) != 0) {
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
        arksh_value_set_string(out_value, arksh_value_item_text_cstr(type_entry));
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
        arksh_value_set_string(out_value, arksh_value_text_cstr(value));
        return 0;
      }
      if (strcmp(property, "length") == 0) {
        arksh_value_set_number(out_value, (double) strlen(arksh_value_text_cstr(value)));
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
      const ArkshObject *object_ref = arksh_value_object_ref(value);
      char rendered[ARKSH_MAX_OUTPUT];

      if (strcmp(property, "child_count") == 0 &&
          (object_ref->kind == ARKSH_OBJECT_DIRECTORY || object_ref->kind == ARKSH_OBJECT_MOUNT_POINT)) {
        char child_names[ARKSH_MAX_COLLECTION_ITEMS][ARKSH_MAX_PATH];
        size_t count = 0;

        if (arksh_platform_list_children_names(object_ref->path, child_names, ARKSH_MAX_COLLECTION_ITEMS, &count) != 0) {
          snprintf(error, error_size, "unable to count children for %s", object_ref->path);
          return 1;
        }
        arksh_value_set_number(out_value, (double) count);
        return 0;
      }

      if (arksh_object_get_property(object_ref, property, rendered, sizeof(rendered)) == 0) {
        arksh_value_set_string(out_value, rendered);
        return 0;
      }
      break;
    }
    case ARKSH_VALUE_BLOCK:
      if (strcmp(property, "arity") == 0) {
        arksh_value_set_number(out_value, (double) arksh_value_block_ref(value)->param_count);
        return 0;
      }
      if (strcmp(property, "source") == 0) {
        arksh_value_set_string(out_value, arksh_value_block_ref(value)->source);
        return 0;
      }
      if (strcmp(property, "body") == 0) {
        arksh_value_set_string(out_value, arksh_value_block_ref(value)->body);
        return 0;
      }
      if (strcmp(property, "params") == 0) {
        size_t i;
        const ArkshBlock *block_ref = arksh_value_block_ref(value);
        ArkshValue *params = allocate_value(error, error_size);

        if (params == NULL) {
          return 1;
        }
        arksh_value_init(params);
        params->kind = ARKSH_VALUE_LIST;
        for (i = 0; i < (size_t) block_ref->param_count; ++i) {
          ArkshValue *param = allocate_value(error, error_size);

          if (param == NULL) {
            arksh_value_free(params);
            free(params);
            return 1;
          }
          arksh_value_set_string(param, block_ref->params[i]);
          if (arksh_value_list_append_value(params, param) != 0) {
            arksh_value_free(param);
            free(param);
            arksh_value_free(params);
            free(params);
            snprintf(error, error_size, "unknown property: %s", property);
            return 1;
          }
          arksh_value_free(param);
          free(param);
        }
        *out_value = *params;
        free(params);
        return 0;
      }
      break;
    case ARKSH_VALUE_CLASS:
      if (strcmp(property, "name") == 0) {
        arksh_value_set_string(out_value, arksh_value_text_cstr(value));
        return 0;
      }
      break;
    case ARKSH_VALUE_INSTANCE:
      if (strcmp(property, "id") == 0) {
        arksh_value_set_number(out_value, value->number);
        return 0;
      }
      if (strcmp(property, "class") == 0 || strcmp(property, "class_name") == 0) {
        arksh_value_set_string(out_value, arksh_value_text_cstr(value));
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
  ArkshValue *property_value;
  char error[ARKSH_MAX_OUTPUT];
  int status;

  if (value == NULL || property == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  property_value = (ArkshValue *) calloc(1, sizeof(*property_value));
  if (property_value == NULL) {
    snprintf(out, out_size, "out of memory");
    return 1;
  }

  status = arksh_value_get_property_value(value, property, property_value, error, sizeof(error));
  if (status != 0) {
    free(property_value);
    copy_string(out, out_size, error);
    return 1;
  }

  status = arksh_value_render(property_value, out, out_size);
  arksh_value_free(property_value);
  free(property_value);
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
  out_object->permissions = info.permissions;
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

  if (strcmp(property, "permissions") == 0 || strcmp(property, "permissions_rwx") == 0 || strcmp(property, "mode") == 0) {
    format_permission_rwx(object->permissions, out, out_size);
    return 0;
  }

  if (strcmp(property, "permissions_octal") == 0 || strcmp(property, "mode_octal") == 0) {
    format_permission_octal(object->permissions, out, out_size);
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
  ArkshValue *temp_value;
  int status;

  if (item == NULL || property == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if ((item->kind == ARKSH_VALUE_LIST || item->kind == ARKSH_VALUE_MAP || item->kind == ARKSH_VALUE_DICT) &&
      item->nested != NULL) {
    return arksh_value_get_property_value(item->nested, property, out_value, error, error_size);
  }

  temp_value = (ArkshValue *) calloc(1, sizeof(*temp_value));
  if (temp_value == NULL) {
    snprintf(error, error_size, "out of memory");
    return 1;
  }

  arksh_value_init(temp_value);
  temp_value->kind = item->kind;
  temp_value->number = item->number;
  temp_value->boolean = item->boolean;
  if (assign_value_text(temp_value, item->text) != 0 ||
      assign_value_object(temp_value, item->object) != 0 ||
      assign_value_block(temp_value, item->block) != 0) {
    arksh_value_free(temp_value);
    free(temp_value);
    snprintf(error, error_size, "out of memory");
    return 1;
  }
  status = arksh_value_get_property_value(temp_value, property, out_value, error, error_size);
  arksh_value_free(temp_value);
  free(temp_value);
  return status;
}

int arksh_value_item_get_property(const ArkshValueItem *item, const char *property, char *out, size_t out_size) {
  ArkshValue *property_value;
  char error[ARKSH_MAX_OUTPUT];
  int status;

  if (item == NULL || property == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  property_value = (ArkshValue *) calloc(1, sizeof(*property_value));
  if (property_value == NULL) {
    snprintf(out, out_size, "out of memory");
    return 1;
  }

  status = arksh_value_item_get_property_value(item, property, property_value, error, sizeof(error));
  if (status != 0) {
    free(property_value);
    copy_string(out, out_size, error);
    return 1;
  }

  status = arksh_value_render(property_value, out, out_size);
  arksh_value_free(property_value);
  free(property_value);
  return status;
}

int arksh_object_call_method(const ArkshObject *object, const char *method, int argc, char **argv, char *out, size_t out_size) {
  size_t limit = 4096;
  char parent[ARKSH_MAX_PATH];
  unsigned int permissions = 0u;
  char platform_error[ARKSH_MAX_OUTPUT];
  ArkshObject updated_object;

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

  if (strcmp(method, "chmod") == 0 || strcmp(method, "set_permissions") == 0) {
    if (!object->exists) {
      snprintf(out, out_size, "%s() is only valid on existing filesystem objects", method);
      return 1;
    }
    if (argc != 1 || argv[0] == NULL || parse_permissions_spec(argv[0], &permissions) != 0) {
      snprintf(out, out_size, "%s() expects one permission spec like 755 or rwxr-xr-x", method);
      return 1;
    }
    if (arksh_platform_set_permissions(object->path, permissions, platform_error, sizeof(platform_error)) != 0) {
      copy_string(out, out_size, platform_error);
      return 1;
    }
    if (arksh_object_resolve(".", object->path, &updated_object) != 0) {
      snprintf(out, out_size, "%s", object->path);
      return 0;
    }
    snprintf(out, out_size, "%s", updated_object.path);
    return 0;
  }

  if (strcmp(method, "describe") == 0) {
    char permissions_rwx[16];
    char permissions_octal[16];

    format_permission_rwx(object->permissions, permissions_rwx, sizeof(permissions_rwx));
    format_permission_octal(object->permissions, permissions_octal, sizeof(permissions_octal));
    snprintf(
      out,
      out_size,
      "type=%s\npath=%s\nname=%s\nexists=%s\nsize=%llu\npermissions=%s\npermissions_octal=%s\nhidden=%s\nreadable=%s\nwritable=%s",
      arksh_object_kind_name(object->kind),
      object->path,
      object->name,
      object->exists ? "true" : "false",
      object->size,
      permissions_rwx,
      permissions_octal,
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
  unsigned int permissions = 0u;
  ArkshObject updated_object;

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
      ArkshObject child_object;
      ArkshValueItem *item = allocate_value_item(error, error_size);

      if (item == NULL) {
        return 1;
      }

      if (arksh_platform_resolve_path(object->path, child_names[i], child_path, sizeof(child_path)) != 0) {
        free(item);
        continue;
      }

      arksh_value_item_init(item);
      if (arksh_object_resolve(object->path, child_path, &child_object) == 0) {
        arksh_value_item_set_object(item, &child_object);
      }
      if (item->kind == ARKSH_VALUE_OBJECT && arksh_value_list_append_item(out_value, item) == 0) {
        free(item);
        continue;
      }
      arksh_value_item_free(item);
      free(item);
    }

    return 0;
  }

  if (strcmp(method, "parent") == 0) {
    char parent[ARKSH_MAX_PATH];
    ArkshObject parent_object;

    arksh_platform_dirname(object->path, parent, sizeof(parent));
    if (arksh_object_resolve(object->path, parent, &parent_object) != 0) {
      snprintf(error, error_size, "unable to resolve parent for %s", object->path);
      return 1;
    }

    arksh_value_set_object(out_value, &parent_object);
    return 0;
  }

  if (strcmp(method, "chmod") == 0 || strcmp(method, "set_permissions") == 0) {
    if (!object->exists) {
      snprintf(error, error_size, "%s() is only valid on existing filesystem objects", method);
      return 1;
    }
    if (argc != 1 || argv[0] == NULL || parse_permissions_spec(argv[0], &permissions) != 0) {
      snprintf(error, error_size, "%s() expects one permission spec like 755 or rwxr-xr-x", method);
      return 1;
    }
    if (arksh_platform_set_permissions(object->path, permissions, error, error_size) != 0) {
      return 1;
    }
    if (arksh_object_resolve(".", object->path, &updated_object) != 0) {
      snprintf(error, error_size, "unable to refresh object after chmod");
      return 1;
    }
    arksh_value_set_object(out_value, &updated_object);
    return 0;
  }

  if (strcmp(method, "read_text") == 0 || strcmp(method, "describe") == 0) {
    char rendered[ARKSH_MAX_OUTPUT];

    if (arksh_object_call_method(object, method, argc, argv, rendered, sizeof(rendered)) != 0) {
      copy_string(error, error_size, rendered);
      return 1;
    }

    arksh_value_set_string(out_value, rendered);
    return 0;
  }

  snprintf(error, error_size, "unknown method: %s", method);
  return 1;
}
