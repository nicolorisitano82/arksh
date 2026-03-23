/* E8-S1-T4: unit tests for the arksh object model (object.h / object.c).
 *
 * Tests value lifecycle, type setters, list/map operations, rendering,
 * JSON round-trip and object resolution — all without a live shell instance.
 *
 * Standalone executable — returns 0 on success, 1 on any failure.
 */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "arksh/object.h"

/* ------------------------------------------------------------------ helpers */

static int g_failures = 0;

#define EXPECT(cond, msg)                                              \
  do {                                                                 \
    if (!(cond)) {                                                     \
      fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, (msg)); \
      g_failures++;                                                    \
    }                                                                  \
  } while (0)

/* Render a value to a stack buffer and return it. */
static const char *render(const ArkshValue *v, char *buf, size_t buf_size) {
  buf[0] = '\0';
  arksh_value_render(v, buf, buf_size);
  return buf;
}

static int append_snprintf(char **buffer, size_t *capacity, size_t *length, const char *fmt, ...) {
  int written;
  va_list args;

  if (buffer == NULL || *buffer == NULL || capacity == NULL || length == NULL || fmt == NULL) {
    return 1;
  }

  while (1) {
    size_t remaining = *capacity - *length;

    va_start(args, fmt);
    written = vsnprintf(*buffer + *length, remaining, fmt, args);
    va_end(args);
    if (written < 0) {
      return 1;
    }
    if ((size_t) written < remaining) {
      *length += (size_t) written;
      return 0;
    }
    while (*capacity <= *length + (size_t) written) {
      char *grown;
      *capacity *= 2;
      grown = (char *) realloc(*buffer, *capacity);
      if (grown == NULL) {
        return 1;
      }
      *buffer = grown;
    }
  }
}

static char *build_large_json_array(size_t count) {
  char *buffer = (char *) calloc(1024, 1);
  size_t capacity = 1024;
  size_t length = 0;
  size_t i;

  if (buffer == NULL) {
    return NULL;
  }
  if (append_snprintf(&buffer, &capacity, &length, "[") != 0) {
    free(buffer);
    return NULL;
  }
  for (i = 0; i < count; ++i) {
    if (append_snprintf(&buffer, &capacity, &length, "%s%zu", i == 0 ? "" : ",", i) != 0) {
      free(buffer);
      return NULL;
    }
  }
  if (append_snprintf(&buffer, &capacity, &length, "]") != 0) {
    free(buffer);
    return NULL;
  }
  return buffer;
}

static char *build_large_json_object(size_t count) {
  char *buffer = (char *) calloc(1024, 1);
  size_t capacity = 1024;
  size_t length = 0;
  size_t i;

  if (buffer == NULL) {
    return NULL;
  }
  if (append_snprintf(&buffer, &capacity, &length, "{") != 0) {
    free(buffer);
    return NULL;
  }
  for (i = 0; i < count; ++i) {
    if (append_snprintf(&buffer, &capacity, &length, "%s\"k%04zu\":%zu", i == 0 ? "" : ",", i, i) != 0) {
      free(buffer);
      return NULL;
    }
  }
  if (append_snprintf(&buffer, &capacity, &length, "}") != 0) {
    free(buffer);
    return NULL;
  }
  return buffer;
}

static char *build_nested_json_payload(size_t count) {
  char *buffer = (char *) calloc(1024, 1);
  size_t capacity = 1024;
  size_t length = 0;
  size_t i;

  if (buffer == NULL) {
    return NULL;
  }
  if (append_snprintf(&buffer, &capacity, &length, "{\"rows\":[") != 0) {
    free(buffer);
    return NULL;
  }
  for (i = 0; i < count; ++i) {
    if (append_snprintf(&buffer, &capacity, &length,
                        "%s{\"id\":%zu,\"meta\":{\"even\":%s}}",
                        i == 0 ? "" : ",",
                        i,
                        (i % 2) == 0 ? "true" : "false") != 0) {
      free(buffer);
      return NULL;
    }
  }
  if (append_snprintf(&buffer, &capacity, &length, "],\"total\":%zu}", count) != 0) {
    free(buffer);
    return NULL;
  }
  return buffer;
}

/* ------------------------------------------------------------------ init / kind names */

static void test_value_init_is_empty(void) {
  ArkshValue v;
  arksh_value_init(&v);
  EXPECT(v.kind == ARKSH_VALUE_EMPTY, "init: kind == EMPTY");
}

static void test_item_init_is_empty(void) {
  ArkshValueItem it;
  arksh_value_item_init(&it);
  EXPECT(it.kind == ARKSH_VALUE_EMPTY, "item init: kind == EMPTY");
}

static void test_value_kind_names(void) {
  EXPECT(strcmp(arksh_value_kind_name(ARKSH_VALUE_EMPTY),   "empty")   == 0, "kind name: EMPTY");
  EXPECT(strcmp(arksh_value_kind_name(ARKSH_VALUE_STRING),  "string")  == 0, "kind name: STRING");
  EXPECT(strcmp(arksh_value_kind_name(ARKSH_VALUE_NUMBER),  "number")  == 0, "kind name: NUMBER");
  EXPECT(strcmp(arksh_value_kind_name(ARKSH_VALUE_BOOLEAN), "bool") == 0, "kind name: BOOLEAN");
  EXPECT(strcmp(arksh_value_kind_name(ARKSH_VALUE_LIST),    "list")    == 0, "kind name: LIST");
  EXPECT(strcmp(arksh_value_kind_name(ARKSH_VALUE_MAP),     "map")     == 0, "kind name: MAP");
}

static void test_object_kind_names(void) {
  EXPECT(strcmp(arksh_object_kind_name(ARKSH_OBJECT_FILE),      "file")      == 0, "obj kind: FILE");
  EXPECT(strcmp(arksh_object_kind_name(ARKSH_OBJECT_DIRECTORY), "directory") == 0, "obj kind: DIRECTORY");
}

/* ------------------------------------------------------------------ setters */

static void test_set_string(void) {
  ArkshValue v;
  arksh_value_init(&v);
  arksh_value_set_string(&v, "hello");
  EXPECT(v.kind == ARKSH_VALUE_STRING, "set_string: kind == STRING");
  EXPECT(strcmp(arksh_value_text_cstr(&v), "hello") == 0,  "set_string: text == 'hello'");
  arksh_value_free(&v);
}

static void test_set_string_empty(void) {
  ArkshValue v;
  arksh_value_init(&v);
  arksh_value_set_string(&v, "");
  EXPECT(v.kind == ARKSH_VALUE_STRING, "set_string empty: kind == STRING");
  EXPECT(arksh_value_text_cstr(&v)[0] == '\0', "set_string empty: text is empty");
  arksh_value_free(&v);
}

static void test_set_number(void) {
  ArkshValue v;
  arksh_value_init(&v);
  arksh_value_set_number(&v, 3.14);
  EXPECT(v.kind == ARKSH_VALUE_NUMBER, "set_number: kind == NUMBER");
  EXPECT(fabs(v.number - 3.14) < 1e-9, "set_number: number == 3.14");
  arksh_value_free(&v);
}

static void test_set_number_zero(void) {
  ArkshValue v;
  arksh_value_init(&v);
  arksh_value_set_number(&v, 0.0);
  EXPECT(v.kind == ARKSH_VALUE_NUMBER, "set_number 0: kind == NUMBER");
  EXPECT(v.number == 0.0, "set_number 0: number == 0");
  arksh_value_free(&v);
}

static void test_set_boolean_true(void) {
  ArkshValue v;
  arksh_value_init(&v);
  arksh_value_set_boolean(&v, 1);
  EXPECT(v.kind == ARKSH_VALUE_BOOLEAN, "set_boolean true: kind == BOOLEAN");
  EXPECT(v.boolean != 0, "set_boolean true: boolean non-zero");
  arksh_value_free(&v);
}

static void test_set_boolean_false(void) {
  ArkshValue v;
  arksh_value_init(&v);
  arksh_value_set_boolean(&v, 0);
  EXPECT(v.kind == ARKSH_VALUE_BOOLEAN, "set_boolean false: kind == BOOLEAN");
  EXPECT(v.boolean == 0, "set_boolean false: boolean zero");
  arksh_value_free(&v);
}

static void test_set_class(void) {
  ArkshValue v;
  arksh_value_init(&v);
  arksh_value_set_class(&v, "MyClass");
  EXPECT(v.kind == ARKSH_VALUE_CLASS, "set_class: kind == CLASS");
  EXPECT(strcmp(arksh_value_text_cstr(&v), "MyClass") == 0, "set_class: text == 'MyClass'");
  arksh_value_free(&v);
}

static void test_set_instance(void) {
  ArkshValue v;
  arksh_value_init(&v);
  arksh_value_set_instance(&v, "MyClass", 7);
  EXPECT(v.kind == ARKSH_VALUE_INSTANCE, "set_instance: kind == INSTANCE");
  arksh_value_free(&v);
}

static void test_set_map(void) {
  ArkshValue v;
  arksh_value_init(&v);
  arksh_value_set_map(&v);
  EXPECT(v.kind == ARKSH_VALUE_MAP, "set_map: kind == MAP");
  EXPECT(v.map.count == 0, "set_map: count == 0");
  arksh_value_free(&v);
}

static void test_set_typed_map(void) {
  ArkshValue v;
  arksh_value_init(&v);
  arksh_value_set_typed_map(&v, "Point");
  EXPECT(v.kind == ARKSH_VALUE_MAP, "set_typed_map: kind == MAP");
  /* type name stored as "__type__" entry */
  const ArkshValueItem *ti = arksh_value_map_get_item(&v, "__type__");
  EXPECT(ti != NULL, "set_typed_map: __type__ entry exists");
  EXPECT(ti != NULL && strcmp(arksh_value_item_text_cstr(ti), "Point") == 0, "set_typed_map: __type__ == 'Point'");
  arksh_value_free(&v);
}

/* ------------------------------------------------------------------ copy */

static void test_copy_string(void) {
  ArkshValue src, dst;
  char buf[ARKSH_MAX_OUTPUT];
  arksh_value_init(&src);
  arksh_value_init(&dst);
  arksh_value_set_string(&src, "copy-me");
  int rc = arksh_value_copy(&dst, &src);
  EXPECT(rc == 0, "copy string: rc == 0");
  EXPECT(dst.kind == ARKSH_VALUE_STRING, "copy string: dst kind == STRING");
  EXPECT(strcmp(render(&dst, buf, sizeof(buf)), "copy-me") == 0, "copy string: dst text == src text");
  arksh_value_free(&src);
  arksh_value_free(&dst);
}

static void test_copy_number(void) {
  ArkshValue src, dst;
  arksh_value_init(&src);
  arksh_value_init(&dst);
  arksh_value_set_number(&src, 99.0);
  arksh_value_copy(&dst, &src);
  EXPECT(dst.kind == ARKSH_VALUE_NUMBER, "copy number: kind == NUMBER");
  EXPECT(dst.number == 99.0, "copy number: number == 99");
  arksh_value_free(&src);
  arksh_value_free(&dst);
}

/* ------------------------------------------------------------------ list */

static void test_list_append_value(void) {
  ArkshValue list;
  ArkshValue item;
  char buf[ARKSH_MAX_OUTPUT];
  arksh_value_init(&list);
  arksh_value_init(&item);
  arksh_value_set_string(&list, "");
  list.kind = ARKSH_VALUE_LIST;
  list.list.count = 0;

  arksh_value_set_string(&item, "alpha");
  int rc = arksh_value_list_append_value(&list, &item);
  EXPECT(rc == 0, "list append: rc == 0");
  EXPECT(list.list.count == 1, "list append: count == 1");
  EXPECT(strcmp(arksh_value_item_text_cstr(&list.list.items[0]), "alpha") == 0, "list append: first item text == 'alpha'");

  arksh_value_free(&item);
  arksh_value_free(&list);
}

static void test_list_append_item(void) {
  ArkshValue list;
  ArkshValueItem item;
  arksh_value_init(&list);
  arksh_value_item_init(&item);
  list.kind = ARKSH_VALUE_LIST;
  list.list.count = 0;

  item.kind = ARKSH_VALUE_NUMBER;
  item.number = 5.0;
  int rc = arksh_value_list_append_item(&list, &item);
  EXPECT(rc == 0, "list append item: rc == 0");
  EXPECT(list.list.count == 1, "list append item: count == 1");
  EXPECT(list.list.items[0].number == 5.0, "list append item: item number == 5");

  arksh_value_item_free(&item);
  arksh_value_free(&list);
}

/* ------------------------------------------------------------------ map */

static void test_map_set_get(void) {
  ArkshValue map;
  ArkshValue entry;
  arksh_value_init(&map);
  arksh_value_init(&entry);
  arksh_value_set_map(&map);

  arksh_value_set_string(&entry, "mapval");
  int rc = arksh_value_map_set(&map, "key1", &entry);
  EXPECT(rc == 0, "map set: rc == 0");
  EXPECT(map.map.count == 1, "map set: count == 1");

  const ArkshValueItem *got = arksh_value_map_get_item(&map, "key1");
  EXPECT(got != NULL, "map get: entry found");
  EXPECT(got != NULL && strcmp(arksh_value_item_text_cstr(got), "mapval") == 0, "map get: text == 'mapval'");

  arksh_value_free(&entry);
  arksh_value_free(&map);
}

static void test_map_overwrite(void) {
  ArkshValue map;
  ArkshValue v1, v2;
  arksh_value_init(&map);
  arksh_value_init(&v1);
  arksh_value_init(&v2);
  arksh_value_set_map(&map);

  arksh_value_set_string(&v1, "first");
  arksh_value_map_set(&map, "k", &v1);
  arksh_value_set_string(&v2, "second");
  arksh_value_map_set(&map, "k", &v2);

  EXPECT(map.map.count == 1, "map overwrite: count stays 1");
  const ArkshValueItem *got = arksh_value_map_get_item(&map, "k");
  EXPECT(got != NULL && strcmp(arksh_value_item_text_cstr(got), "second") == 0, "map overwrite: value updated");

  arksh_value_free(&v1);
  arksh_value_free(&v2);
  arksh_value_free(&map);
}

static void test_map_get_missing(void) {
  ArkshValue map;
  arksh_value_init(&map);
  arksh_value_set_map(&map);
  const ArkshValueItem *got = arksh_value_map_get_item(&map, "nope");
  EXPECT(got == NULL, "map get missing: returns NULL");
  arksh_value_free(&map);
}

static void test_map_get_path_nested(void) {
  ArkshValue root;
  ArkshValue list;
  ArkshValue one;
  ArkshValue two;
  ArkshValue nested;
  ArkshValue truthy;
  ArkshValue resolved;
  char error[ARKSH_MAX_OUTPUT];
  int found = 0;

  arksh_value_init(&root);
  arksh_value_init(&list);
  arksh_value_init(&one);
  arksh_value_init(&two);
  arksh_value_init(&nested);
  arksh_value_init(&truthy);
  arksh_value_init(&resolved);

  arksh_value_set_map(&root);
  list.kind = ARKSH_VALUE_LIST;
  arksh_value_set_number(&one, 1.0);
  arksh_value_set_number(&two, 2.0);
  arksh_value_set_map(&nested);
  arksh_value_set_boolean(&truthy, 1);

  EXPECT(arksh_value_list_append_value(&list, &one) == 0, "get_path nested: append first item");
  EXPECT(arksh_value_list_append_value(&list, &two) == 0, "get_path nested: append second item");
  EXPECT(arksh_value_map_set(&nested, "b", &truthy) == 0, "get_path nested: set nested key");
  EXPECT(arksh_value_list_append_value(&list, &nested) == 0, "get_path nested: append nested map");
  EXPECT(arksh_value_map_set(&root, "a", &list) == 0, "get_path nested: set list key");

  error[0] = '\0';
  EXPECT(arksh_value_get_path(&root, "a[2].b", &resolved, &found, error, sizeof(error)) == 0, "get_path nested: rc == 0");
  EXPECT(found == 1, "get_path nested: found == 1");
  EXPECT(resolved.kind == ARKSH_VALUE_BOOLEAN, "get_path nested: kind == BOOLEAN");
  EXPECT(resolved.boolean == 1, "get_path nested: boolean == true");

  arksh_value_free(&resolved);
  arksh_value_free(&truthy);
  arksh_value_free(&nested);
  arksh_value_free(&two);
  arksh_value_free(&one);
  arksh_value_free(&list);
  arksh_value_free(&root);
}

static void test_map_set_path_auto_create(void) {
  ArkshValue root;
  ArkshValue replacement;
  ArkshValue updated;
  ArkshValue resolved;
  char error[ARKSH_MAX_OUTPUT];
  int found = 0;

  arksh_value_init(&root);
  arksh_value_init(&replacement);
  arksh_value_init(&updated);
  arksh_value_init(&resolved);

  arksh_value_set_map(&root);
  arksh_value_set_number(&replacement, 2.0);

  error[0] = '\0';
  EXPECT(arksh_value_set_path(&root, "meta.version", &replacement, &updated, error, sizeof(error)) == 0, "set_path auto create: rc == 0");
  EXPECT(arksh_value_get_path(&updated, "meta.version", &resolved, &found, error, sizeof(error)) == 0, "set_path auto create: get_path rc == 0");
  EXPECT(found == 1, "set_path auto create: found == 1");
  EXPECT(resolved.kind == ARKSH_VALUE_NUMBER, "set_path auto create: kind == NUMBER");
  EXPECT(resolved.number == 2.0, "set_path auto create: value == 2");

  arksh_value_free(&resolved);
  arksh_value_free(&updated);
  arksh_value_free(&replacement);
  arksh_value_free(&root);
}

static void test_map_pick_selected_keys(void) {
  ArkshValue typed_map;
  ArkshValue name_value;
  ArkshValue count_value;
  ArkshValue picked;
  const ArkshValueItem *name_entry;
  const ArkshValueItem *count_entry;
  const ArkshValueItem *type_entry;
  char keys[1][ARKSH_MAX_NAME] = { "name" };
  char error[ARKSH_MAX_OUTPUT];

  arksh_value_init(&typed_map);
  arksh_value_init(&name_value);
  arksh_value_init(&count_value);
  arksh_value_init(&picked);

  arksh_value_set_typed_map(&typed_map, "demo");
  arksh_value_set_string(&name_value, "arksh");
  arksh_value_set_number(&count_value, 3.0);
  EXPECT(arksh_value_map_set(&typed_map, "name", &name_value) == 0, "pick: set name");
  EXPECT(arksh_value_map_set(&typed_map, "count", &count_value) == 0, "pick: set count");

  error[0] = '\0';
  EXPECT(arksh_value_pick(&typed_map, 1, keys, &picked, error, sizeof(error)) == 0, "pick: rc == 0");
  name_entry = arksh_value_map_get_item(&picked, "name");
  count_entry = arksh_value_map_get_item(&picked, "count");
  type_entry = arksh_value_map_get_item(&picked, "__type__");
  EXPECT(name_entry != NULL && strcmp(arksh_value_item_text_cstr(name_entry), "arksh") == 0, "pick: name preserved");
  EXPECT(count_entry == NULL, "pick: count omitted");
  EXPECT(type_entry != NULL && strcmp(arksh_value_item_text_cstr(type_entry), "demo") == 0, "pick: __type__ preserved");

  arksh_value_free(&picked);
  arksh_value_free(&count_value);
  arksh_value_free(&name_value);
  arksh_value_free(&typed_map);
}

static void test_map_merge_override(void) {
  ArkshValue left;
  ArkshValue right;
  ArkshValue name_left;
  ArkshValue name_right;
  ArkshValue status_right;
  ArkshValue merged;
  const ArkshValueItem *name_entry;
  const ArkshValueItem *status_entry;
  char error[ARKSH_MAX_OUTPUT];

  arksh_value_init(&left);
  arksh_value_init(&right);
  arksh_value_init(&name_left);
  arksh_value_init(&name_right);
  arksh_value_init(&status_right);
  arksh_value_init(&merged);

  arksh_value_set_map(&left);
  arksh_value_set_map(&right);
  arksh_value_set_string(&name_left, "old");
  arksh_value_set_string(&name_right, "new");
  arksh_value_set_string(&status_right, "ok");

  EXPECT(arksh_value_map_set(&left, "name", &name_left) == 0, "merge: set left name");
  EXPECT(arksh_value_map_set(&right, "name", &name_right) == 0, "merge: set right name");
  EXPECT(arksh_value_map_set(&right, "status", &status_right) == 0, "merge: set right status");

  error[0] = '\0';
  EXPECT(arksh_value_merge(&left, &right, &merged, error, sizeof(error)) == 0, "merge: rc == 0");
  name_entry = arksh_value_map_get_item(&merged, "name");
  status_entry = arksh_value_map_get_item(&merged, "status");
  EXPECT(name_entry != NULL && strcmp(arksh_value_item_text_cstr(name_entry), "new") == 0, "merge: right value overrides");
  EXPECT(status_entry != NULL && strcmp(arksh_value_item_text_cstr(status_entry), "ok") == 0, "merge: new key added");

  arksh_value_free(&merged);
  arksh_value_free(&status_right);
  arksh_value_free(&name_right);
  arksh_value_free(&name_left);
  arksh_value_free(&right);
  arksh_value_free(&left);
}

/* ------------------------------------------------------------------ render */

static void test_render_string(void) {
  ArkshValue v;
  char buf[ARKSH_MAX_OUTPUT];
  arksh_value_init(&v);
  arksh_value_set_string(&v, "world");
  EXPECT(strcmp(render(&v, buf, sizeof(buf)), "world") == 0, "render string: == 'world'");
  arksh_value_free(&v);
}

static void test_render_number_integer(void) {
  ArkshValue v;
  char buf[ARKSH_MAX_OUTPUT];
  arksh_value_init(&v);
  arksh_value_set_number(&v, 42.0);
  const char *r = render(&v, buf, sizeof(buf));
  EXPECT(strstr(r, "42") != NULL, "render number 42: contains '42'");
  arksh_value_free(&v);
}

static void test_render_boolean_true(void) {
  ArkshValue v;
  char buf[ARKSH_MAX_OUTPUT];
  arksh_value_init(&v);
  arksh_value_set_boolean(&v, 1);
  EXPECT(strcmp(render(&v, buf, sizeof(buf)), "true") == 0, "render true: == 'true'");
  arksh_value_free(&v);
}

static void test_render_boolean_false(void) {
  ArkshValue v;
  char buf[ARKSH_MAX_OUTPUT];
  arksh_value_init(&v);
  arksh_value_set_boolean(&v, 0);
  EXPECT(strcmp(render(&v, buf, sizeof(buf)), "false") == 0, "render false: == 'false'");
  arksh_value_free(&v);
}

/* ------------------------------------------------------------------ value <-> item conversion */

static void test_value_to_item_roundtrip(void) {
  ArkshValue src;
  ArkshValueItem item;
  ArkshValue dst;
  arksh_value_init(&src);
  arksh_value_item_init(&item);
  arksh_value_init(&dst);

  arksh_value_set_string(&src, "roundtrip");
  int rc1 = arksh_value_item_set_from_value(&item, &src);
  int rc2 = arksh_value_set_from_item(&dst, &item);
  EXPECT(rc1 == 0, "value->item conversion: rc == 0");
  EXPECT(rc2 == 0, "item->value conversion: rc == 0");
  EXPECT(dst.kind == ARKSH_VALUE_STRING, "roundtrip: dst kind == STRING");
  EXPECT(strcmp(arksh_value_text_cstr(&dst), "roundtrip") == 0, "roundtrip: dst text == 'roundtrip'");

  arksh_value_free(&src);
  arksh_value_item_free(&item);
  arksh_value_free(&dst);
}

/* ------------------------------------------------------------------ JSON */

static void test_json_string_roundtrip(void) {
  ArkshValue v;
  char json[ARKSH_MAX_OUTPUT];
  ArkshValue parsed;
  char error[256];

  arksh_value_init(&v);
  arksh_value_init(&parsed);

  arksh_value_set_string(&v, "hello");
  int rc1 = arksh_value_to_json(&v, json, sizeof(json));
  EXPECT(rc1 == 0, "json string: to_json rc == 0");
  EXPECT(strstr(json, "hello") != NULL, "json string: output contains 'hello'");

  int rc2 = arksh_value_parse_json(json, &parsed, error, sizeof(error));
  EXPECT(rc2 == 0, "json string: parse_json rc == 0");
  EXPECT(strcmp(arksh_value_text_cstr(&parsed), "hello") == 0, "json string: roundtrip text == 'hello'");

  arksh_value_free(&v);
  arksh_value_free(&parsed);
}

static void test_json_number_roundtrip(void) {
  ArkshValue v;
  char json[ARKSH_MAX_OUTPUT];
  ArkshValue parsed;
  char error[256];

  arksh_value_init(&v);
  arksh_value_init(&parsed);

  arksh_value_set_number(&v, 123.0);
  arksh_value_to_json(&v, json, sizeof(json));
  int rc = arksh_value_parse_json(json, &parsed, error, sizeof(error));
  EXPECT(rc == 0, "json number: parse_json rc == 0");
  EXPECT(parsed.kind == ARKSH_VALUE_NUMBER, "json number: roundtrip kind == NUMBER");
  EXPECT(parsed.number == 123.0, "json number: roundtrip value == 123");

  arksh_value_free(&v);
  arksh_value_free(&parsed);
}

static void test_json_boolean_roundtrip(void) {
  ArkshValue v;
  char json[ARKSH_MAX_OUTPUT];
  ArkshValue parsed;
  char error[256];

  arksh_value_init(&v);
  arksh_value_init(&parsed);

  arksh_value_set_boolean(&v, 1);
  arksh_value_to_json(&v, json, sizeof(json));
  int rc = arksh_value_parse_json(json, &parsed, error, sizeof(error));
  EXPECT(rc == 0, "json bool: parse_json rc == 0");
  EXPECT(parsed.kind == ARKSH_VALUE_BOOLEAN, "json bool: roundtrip kind == BOOLEAN");
  EXPECT(parsed.boolean != 0, "json bool: roundtrip value == true");

  arksh_value_free(&v);
  arksh_value_free(&parsed);
}

static void test_json_parse_error(void) {
  ArkshValue parsed;
  char error[256];
  arksh_value_init(&parsed);
  int rc = arksh_value_parse_json("{invalid}", &parsed, error, sizeof(error));
  EXPECT(rc != 0, "json parse error: rc != 0 on invalid input");
  EXPECT(error[0] != '\0', "json parse error: error message is non-empty");
  arksh_value_free(&parsed);
}

/* ------------------------------------------------------------------ JSON edge cases (E7-S1) */

static void test_json_error_offset(void) {
  ArkshValue parsed;
  char error[256];
  arksh_value_init(&parsed);
  /* offset 1: '{' then 'i' is invalid key */
  int rc = arksh_value_parse_json("{invalid}", &parsed, error, sizeof(error));
  EXPECT(rc != 0, "json offset: rc != 0");
  EXPECT(strstr(error, "at offset") != NULL, "json offset: error contains 'at offset'");
  arksh_value_free(&parsed);
}

static void test_json_unicode_ascii(void) {
  ArkshValue v;
  char error[256];
  arksh_value_init(&v);
  /* \u0041 = 'A' */
  int rc = arksh_value_parse_json("\"\\u0041\"", &v, error, sizeof(error));
  EXPECT(rc == 0, "json unicode ascii: rc == 0");
  EXPECT(v.kind == ARKSH_VALUE_STRING, "json unicode ascii: kind == STRING");
  EXPECT(strcmp(arksh_value_text_cstr(&v), "A") == 0, "json unicode ascii: text == 'A'");
  arksh_value_free(&v);
}

static void test_json_unicode_multibyte(void) {
  ArkshValue v;
  char error[256];
  arksh_value_init(&v);
  /* \u00e9 = 'é' UTF-8: 0xC3 0xA9 */
  int rc = arksh_value_parse_json("\"\\u00e9\"", &v, error, sizeof(error));
  EXPECT(rc == 0, "json unicode multibyte: rc == 0");
  EXPECT(v.kind == ARKSH_VALUE_STRING, "json unicode multibyte: kind == STRING");
  EXPECT((unsigned char) arksh_value_text_cstr(&v)[0] == 0xC3, "json unicode multibyte: byte 0 == 0xC3");
  EXPECT((unsigned char) arksh_value_text_cstr(&v)[1] == 0xA9, "json unicode multibyte: byte 1 == 0xA9");
  arksh_value_free(&v);
}

static void test_json_control_char_rejected(void) {
  ArkshValue v;
  char error[256];
  char input[16];
  arksh_value_init(&v);
  /* embed a raw 0x01 control character inside a JSON string */
  input[0] = '"'; input[1] = 'a'; input[2] = '\x01'; input[3] = '"'; input[4] = '\0';
  int rc = arksh_value_parse_json(input, &v, error, sizeof(error));
  EXPECT(rc != 0, "json control char: rc != 0 on unescaped control char");
  EXPECT(error[0] != '\0', "json control char: error message non-empty");
  arksh_value_free(&v);
}

static void test_json_leading_zero_rejected(void) {
  ArkshValue v;
  char error[256];
  arksh_value_init(&v);
  int rc = arksh_value_parse_json("01", &v, error, sizeof(error));
  EXPECT(rc != 0, "json leading zero: rc != 0");
  EXPECT(error[0] != '\0', "json leading zero: error message non-empty");
  arksh_value_free(&v);
}

static void test_json_nan_as_null(void) {
  ArkshValue v;
  char json[64];
  arksh_value_init(&v);
  arksh_value_set_number(&v, 0.0 / 0.0); /* NaN */
  int rc = arksh_value_to_json(&v, json, sizeof(json));
  EXPECT(rc == 0, "json NaN: to_json rc == 0");
  EXPECT(strcmp(json, "null") == 0, "json NaN: serializes as 'null'");
  arksh_value_free(&v);
}

static void test_json_control_char_escaped_in_output(void) {
  ArkshValue v;
  char json[64];
  arksh_value_init(&v);
  /* string containing a tab (0x09) and a raw 0x01 */
  {
    char raw[2] = {'\x01', '\0'};
    arksh_value_set_string(&v, raw);
  }
  int rc = arksh_value_to_json(&v, json, sizeof(json));
  EXPECT(rc == 0, "json ctrl escaped: to_json rc == 0");
  EXPECT(strstr(json, "\\u0001") != NULL, "json ctrl escaped: contains \\u0001");
  arksh_value_free(&v);
}

static void test_value_layout_sizes(void) {
  EXPECT(sizeof(ArkshValue) <= 128, "layout: ArkshValue stays compact");
  EXPECT(sizeof(ArkshValueItem) <= 64, "layout: ArkshValueItem stays compact");
}

static void test_json_matrix_to_json(void) {
  ArkshValue v;
  char json[ARKSH_MAX_OUTPUT];
  char error[256];
  const char *cols[] = {"name", "score"};
  arksh_value_init(&v);
  arksh_value_set_matrix(&v, cols, 2);
  /* add one row: name="alice", score=95 */
  v.matrix->rows[0][0].kind = ARKSH_VALUE_STRING;
  snprintf(v.matrix->rows[0][0].text, sizeof(v.matrix->rows[0][0].text), "alice");
  v.matrix->rows[0][1].kind = ARKSH_VALUE_NUMBER;
  v.matrix->rows[0][1].number = 95.0;
  v.matrix->row_count = 1;
  int rc = arksh_value_to_json(&v, json, sizeof(json));
  EXPECT(rc == 0, "json matrix: to_json rc == 0");
  EXPECT(strstr(json, "\"name\"") != NULL, "json matrix: contains key 'name'");
  EXPECT(strstr(json, "\"alice\"") != NULL, "json matrix: contains 'alice'");
  EXPECT(strstr(json, "95") != NULL, "json matrix: contains score 95");
  arksh_value_free(&v);
  (void) error;
}

static void test_json_large_array_parse(void) {
  ArkshValue parsed;
  char error[256];
  char *json = build_large_json_array(384);

  arksh_value_init(&parsed);
  EXPECT(json != NULL, "json large array: payload allocated");
  if (json == NULL) {
    return;
  }

  EXPECT(arksh_value_parse_json(json, &parsed, error, sizeof(error)) == 0, "json large array: parse rc == 0");
  EXPECT(parsed.kind == ARKSH_VALUE_LIST, "json large array: kind == LIST");
  EXPECT(parsed.list.count == 384, "json large array: count == 384");
  EXPECT(parsed.list.count > 0 && parsed.list.items[0].number == 0.0, "json large array: first item == 0");
  EXPECT(parsed.list.count == 384 && parsed.list.items[383].number == 383.0, "json large array: last item == 383");

  free(json);
  arksh_value_free(&parsed);
}

static void test_json_large_object_parse(void) {
  ArkshValue parsed;
  char error[256];
  char *json = build_large_json_object(320);
  const ArkshValueItem *entry;

  arksh_value_init(&parsed);
  EXPECT(json != NULL, "json large object: payload allocated");
  if (json == NULL) {
    return;
  }

  EXPECT(arksh_value_parse_json(json, &parsed, error, sizeof(error)) == 0, "json large object: parse rc == 0");
  EXPECT(parsed.kind == ARKSH_VALUE_MAP, "json large object: kind == MAP");
  EXPECT(parsed.map.count == 320, "json large object: count == 320");
  entry = arksh_value_map_get_item(&parsed, "k0319");
  EXPECT(entry != NULL, "json large object: last key exists");
  EXPECT(entry != NULL && entry->number == 319.0, "json large object: last value == 319");

  free(json);
  arksh_value_free(&parsed);
}

static void test_json_large_nested_payload(void) {
  ArkshValue parsed;
  char error[256];
  char *json = build_nested_json_payload(192);
  const ArkshValueItem *rows_item;
  const ArkshValueItem *first_row;
  const ArkshValueItem *meta_item;
  const ArkshValueItem *even_item;

  arksh_value_init(&parsed);
  EXPECT(json != NULL, "json nested payload: payload allocated");
  if (json == NULL) {
    return;
  }

  EXPECT(arksh_value_parse_json(json, &parsed, error, sizeof(error)) == 0, "json nested payload: parse rc == 0");
  rows_item = arksh_value_map_get_item(&parsed, "rows");
  EXPECT(rows_item != NULL && rows_item->nested != NULL, "json nested payload: rows list exists");
  EXPECT(rows_item != NULL && rows_item->nested != NULL && rows_item->nested->list.count == 192, "json nested payload: rows count == 192");
  if (rows_item != NULL && rows_item->nested != NULL && rows_item->nested->list.count > 0) {
    first_row = &rows_item->nested->list.items[0];
    EXPECT(first_row->nested != NULL, "json nested payload: first row nested object");
    meta_item = first_row->nested != NULL ? arksh_value_map_get_item(first_row->nested, "meta") : NULL;
    even_item = meta_item != NULL && meta_item->nested != NULL ? arksh_value_map_get_item(meta_item->nested, "even") : NULL;
    EXPECT(even_item != NULL && even_item->boolean != 0, "json nested payload: nested boolean preserved");
  }

  free(json);
  arksh_value_free(&parsed);
}

static void test_json_large_roundtrip(void) {
  ArkshValue list;
  ArkshValue item;
  ArkshValue parsed;
  char *json;
  size_t i;
  char error[256];

  arksh_value_init(&list);
  arksh_value_init(&item);
  arksh_value_init(&parsed);
  list.kind = ARKSH_VALUE_LIST;

  for (i = 0; i < 300; ++i) {
    arksh_value_set_number(&item, (double) i);
    EXPECT(arksh_value_list_append_value(&list, &item) == 0, "json large roundtrip: append rc == 0");
  }

  json = (char *) calloc(32768, 1);
  EXPECT(json != NULL, "json large roundtrip: buffer allocated");
  if (json != NULL) {
    EXPECT(arksh_value_to_json(&list, json, 32768) == 0, "json large roundtrip: to_json rc == 0");
    EXPECT(arksh_value_parse_json(json, &parsed, error, sizeof(error)) == 0, "json large roundtrip: parse rc == 0");
    EXPECT(parsed.kind == ARKSH_VALUE_LIST, "json large roundtrip: parsed kind == LIST");
    EXPECT(parsed.list.count == 300, "json large roundtrip: parsed count == 300");
    free(json);
  }

  arksh_value_free(&parsed);
  arksh_value_free(&item);
  arksh_value_free(&list);
}

/* ------------------------------------------------------------------ object property */

static void test_object_property_type(void) {
  ArkshObject obj;
  char out[256];
  memset(&obj, 0, sizeof(obj));
  obj.kind = ARKSH_OBJECT_DIRECTORY;
  strncpy(obj.path, "/tmp", sizeof(obj.path) - 1);
  strncpy(obj.name, "tmp", sizeof(obj.name) - 1);
  obj.exists = 1;

  int rc = arksh_object_get_property(&obj, "type", out, sizeof(out));
  EXPECT(rc == 0, "object property type: rc == 0");
  EXPECT(strcmp(out, "directory") == 0, "object property type: == 'directory'");
}

static void test_object_property_name(void) {
  ArkshObject obj;
  char out[256];
  memset(&obj, 0, sizeof(obj));
  obj.kind = ARKSH_OBJECT_FILE;
  strncpy(obj.path, "/tmp/foo.txt", sizeof(obj.path) - 1);
  strncpy(obj.name, "foo.txt", sizeof(obj.name) - 1);
  obj.exists = 1;

  int rc = arksh_object_get_property(&obj, "name", out, sizeof(out));
  EXPECT(rc == 0, "object property name: rc == 0");
  EXPECT(strcmp(out, "foo.txt") == 0, "object property name: == 'foo.txt'");
}

static void test_object_permissions_file(void) {
#ifdef _WIN32
  EXPECT(1, "object permissions file: skipped on Windows");
#else
  char temp_dir[] = "/tmp/arksh_perm_fileXXXXXX";
  char path[ARKSH_MAX_PATH];
  ArkshObject obj;
  ArkshValue result;
  char out[256];
  char error[256];
  char *octal_argv[] = { "600" };
  char *rwx_argv[] = { "rw-r-----" };
  FILE *file;

  EXPECT(mkdtemp(temp_dir) != NULL, "object permissions file: mkdtemp");
  snprintf(path, sizeof(path), "%s/file.txt", temp_dir);
  file = fopen(path, "w");
  EXPECT(file != NULL, "object permissions file: fopen");
  if (file != NULL) {
    fputs("hello\n", file);
    fclose(file);
  }
  EXPECT(chmod(path, 0644) == 0, "object permissions file: chmod 0644");
  EXPECT(arksh_object_resolve(".", path, &obj) == 0, "object permissions file: resolve");
  EXPECT(arksh_object_get_property(&obj, "permissions", out, sizeof(out)) == 0, "object permissions file: rwx property");
  EXPECT(strcmp(out, "rw-r--r--") == 0, "object permissions file: rwx == rw-r--r--");
  EXPECT(arksh_object_get_property(&obj, "permissions_octal", out, sizeof(out)) == 0, "object permissions file: octal property");
  EXPECT(strcmp(out, "644") == 0, "object permissions file: octal == 644");

  arksh_value_init(&result);
  if (arksh_object_call_method_value(&obj, "chmod", 1, octal_argv, &result, error, sizeof(error)) == 0) {
    EXPECT(result.kind == ARKSH_VALUE_OBJECT, "object permissions file: chmod returns object");
    EXPECT(arksh_object_get_property(arksh_value_object_ref(&result), "permissions_octal", out, sizeof(out)) == 0,
           "object permissions file: updated octal property");
    EXPECT(strcmp(out, "600") == 0, "object permissions file: octal == 600");
  } else {
    EXPECT(0, "object permissions file: chmod octal");
  }
  arksh_value_free(&result);

  arksh_value_init(&result);
  if (arksh_object_call_method_value(&obj, "chmod", 1, rwx_argv, &result, error, sizeof(error)) == 0) {
    EXPECT(arksh_object_get_property(arksh_value_object_ref(&result), "permissions", out, sizeof(out)) == 0,
           "object permissions file: updated rwx property");
    EXPECT(strcmp(out, "rw-r-----") == 0, "object permissions file: rwx == rw-r-----");
  } else {
    EXPECT(0, "object permissions file: chmod rwx");
  }
  arksh_value_free(&result);

  unlink(path);
  rmdir(temp_dir);
#endif
}

static void test_object_permissions_directory(void) {
#ifdef _WIN32
  EXPECT(1, "object permissions directory: skipped on Windows");
#else
  char temp_dir[] = "/tmp/arksh_perm_dirXXXXXX";
  ArkshObject obj;
  ArkshValue result;
  char out[256];
  char error[256];
  char *octal_argv[] = { "700" };

  EXPECT(mkdtemp(temp_dir) != NULL, "object permissions dir: mkdtemp");
  EXPECT(chmod(temp_dir, 0755) == 0, "object permissions dir: chmod 0755");
  EXPECT(arksh_object_resolve(".", temp_dir, &obj) == 0, "object permissions dir: resolve");
  EXPECT(arksh_object_get_property(&obj, "permissions", out, sizeof(out)) == 0, "object permissions dir: rwx property");
  EXPECT(strcmp(out, "rwxr-xr-x") == 0, "object permissions dir: rwx == rwxr-xr-x");
  EXPECT(arksh_object_get_property(&obj, "permissions_octal", out, sizeof(out)) == 0, "object permissions dir: octal property");
  EXPECT(strcmp(out, "755") == 0, "object permissions dir: octal == 755");

  arksh_value_init(&result);
  if (arksh_object_call_method_value(&obj, "chmod", 1, octal_argv, &result, error, sizeof(error)) == 0) {
    EXPECT(arksh_object_get_property(arksh_value_object_ref(&result), "permissions_octal", out, sizeof(out)) == 0,
           "object permissions dir: updated octal property");
    EXPECT(strcmp(out, "700") == 0, "object permissions dir: octal == 700");
  } else {
    EXPECT(0, "object permissions dir: chmod 700");
  }
  arksh_value_free(&result);

  chmod(temp_dir, 0755);
  rmdir(temp_dir);
#endif
}

/* ------------------------------------------------------------------ main */

int main(void) {
  /* init / kind names */
  test_value_init_is_empty();
  test_item_init_is_empty();
  test_value_kind_names();
  test_object_kind_names();
  test_value_layout_sizes();

  /* setters */
  test_set_string();
  test_set_string_empty();
  test_set_number();
  test_set_number_zero();
  test_set_boolean_true();
  test_set_boolean_false();
  test_set_class();
  test_set_instance();
  test_set_map();
  test_set_typed_map();

  /* copy */
  test_copy_string();
  test_copy_number();

  /* list */
  test_list_append_value();
  test_list_append_item();

  /* map */
  test_map_set_get();
  test_map_overwrite();
  test_map_get_missing();
  test_map_get_path_nested();
  test_map_set_path_auto_create();
  test_map_pick_selected_keys();
  test_map_merge_override();

  /* render */
  test_render_string();
  test_render_number_integer();
  test_render_boolean_true();
  test_render_boolean_false();

  /* value <-> item */
  test_value_to_item_roundtrip();

  /* JSON */
  test_json_string_roundtrip();
  test_json_number_roundtrip();
  test_json_boolean_roundtrip();
  test_json_parse_error();

  /* JSON edge cases (E7-S1) */
  test_json_error_offset();
  test_json_unicode_ascii();
  test_json_unicode_multibyte();
  test_json_control_char_rejected();
  test_json_leading_zero_rejected();
  test_json_nan_as_null();
  test_json_control_char_escaped_in_output();
  test_json_matrix_to_json();
  test_json_large_array_parse();
  test_json_large_object_parse();
  test_json_large_nested_payload();
  test_json_large_roundtrip();

  /* object properties */
  test_object_property_type();
  test_object_property_name();
  test_object_permissions_file();
  test_object_permissions_directory();

  if (g_failures > 0) {
    fprintf(stderr, "%d test(s) FAILED\n", g_failures);
    return 1;
  }

  printf("unit_object: all tests passed\n");
  return 0;
}
