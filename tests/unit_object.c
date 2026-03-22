/* E8-S1-T4: unit tests for the arksh object model (object.h / object.c).
 *
 * Tests value lifecycle, type setters, list/map operations, rendering,
 * JSON round-trip and object resolution — all without a live shell instance.
 *
 * Standalone executable — returns 0 on success, 1 on any failure.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

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
  EXPECT(strcmp(v.text, "hello") == 0,  "set_string: text == 'hello'");
  arksh_value_free(&v);
}

static void test_set_string_empty(void) {
  ArkshValue v;
  arksh_value_init(&v);
  arksh_value_set_string(&v, "");
  EXPECT(v.kind == ARKSH_VALUE_STRING, "set_string empty: kind == STRING");
  EXPECT(v.text[0] == '\0', "set_string empty: text is empty");
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
  EXPECT(strcmp(v.text, "MyClass") == 0, "set_class: text == 'MyClass'");
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
  EXPECT(ti != NULL && strcmp(ti->text, "Point") == 0, "set_typed_map: __type__ == 'Point'");
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
  EXPECT(strcmp(list.list.items[0].text, "alpha") == 0, "list append: first item text == 'alpha'");

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
  EXPECT(got != NULL && strcmp(got->text, "mapval") == 0, "map get: text == 'mapval'");

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
  EXPECT(got != NULL && strcmp(got->text, "second") == 0, "map overwrite: value updated");

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
  EXPECT(strcmp(dst.text, "roundtrip") == 0, "roundtrip: dst text == 'roundtrip'");

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
  EXPECT(strcmp(parsed.text, "hello") == 0, "json string: roundtrip text == 'hello'");

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
  EXPECT(strcmp(v.text, "A") == 0, "json unicode ascii: text == 'A'");
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
  EXPECT((unsigned char) v.text[0] == 0xC3, "json unicode multibyte: byte 0 == 0xC3");
  EXPECT((unsigned char) v.text[1] == 0xA9, "json unicode multibyte: byte 1 == 0xA9");
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
  v.kind = ARKSH_VALUE_STRING;
  v.text[0] = '\x01'; v.text[1] = '\0';
  int rc = arksh_value_to_json(&v, json, sizeof(json));
  EXPECT(rc == 0, "json ctrl escaped: to_json rc == 0");
  EXPECT(strstr(json, "\\u0001") != NULL, "json ctrl escaped: contains \\u0001");
  arksh_value_free(&v);
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

/* ------------------------------------------------------------------ main */

int main(void) {
  /* init / kind names */
  test_value_init_is_empty();
  test_item_init_is_empty();
  test_value_kind_names();
  test_object_kind_names();

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

  /* object properties */
  test_object_property_type();
  test_object_property_name();

  if (g_failures > 0) {
    fprintf(stderr, "%d test(s) FAILED\n", g_failures);
    return 1;
  }

  printf("unit_object: all tests passed\n");
  return 0;
}
