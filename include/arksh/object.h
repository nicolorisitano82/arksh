#ifndef ARKSH_OBJECT_H
#define ARKSH_OBJECT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ARKSH_MAX_PATH 1024
#define ARKSH_MAX_NAME 128
#define ARKSH_MAX_OUTPUT 8192
#define ARKSH_MAX_ARGS 16
#define ARKSH_MAX_TOKEN 256
#define ARKSH_MAX_COLLECTION_ITEMS 128
#define ARKSH_MAX_VALUE_TEXT 1024
#define ARKSH_MAX_BLOCK_PARAMS 4
#define ARKSH_MAX_BLOCK_SOURCE 1024

typedef enum {
  ARKSH_OBJECT_UNKNOWN = 0,
  ARKSH_OBJECT_PATH,
  ARKSH_OBJECT_FILE,
  ARKSH_OBJECT_DIRECTORY,
  ARKSH_OBJECT_DEVICE,
  ARKSH_OBJECT_MOUNT_POINT
} ArkshObjectKind;

typedef struct {
  ArkshObjectKind kind;
  char path[ARKSH_MAX_PATH];
  char name[ARKSH_MAX_NAME];
  int exists;
  unsigned long long size;
  int hidden;
  int readable;
  int writable;
} ArkshObject;

typedef struct {
  ArkshObject items[ARKSH_MAX_COLLECTION_ITEMS];
  size_t count;
} ArkshObjectCollection;

typedef struct {
  char params[ARKSH_MAX_BLOCK_PARAMS][ARKSH_MAX_NAME];
  int param_count;
  char body[ARKSH_MAX_BLOCK_SOURCE];
  char source[ARKSH_MAX_BLOCK_SOURCE];
} ArkshBlock;

typedef enum {
  ARKSH_VALUE_EMPTY = 0,
  ARKSH_VALUE_STRING,
  ARKSH_VALUE_NUMBER,
  ARKSH_VALUE_BOOLEAN,
  ARKSH_VALUE_OBJECT,
  ARKSH_VALUE_BLOCK,
  ARKSH_VALUE_LIST,
  ARKSH_VALUE_MAP,
  ARKSH_VALUE_CLASS,
  ARKSH_VALUE_INSTANCE,
  /* E6-S5: explicit numeric sub-kinds */
  ARKSH_VALUE_INTEGER,
  ARKSH_VALUE_FLOAT,
  ARKSH_VALUE_DOUBLE,
  ARKSH_VALUE_IMAGINARY,
  /* E6-S6: immutable key-value dictionary */
  ARKSH_VALUE_DICT
} ArkshValueKind;

typedef struct ArkshValue ArkshValue;

typedef struct {
  ArkshValueKind kind;
  char text[ARKSH_MAX_VALUE_TEXT];
  double number;
  int boolean;
  ArkshObject object;
  ArkshBlock block;
  ArkshValue *nested;
} ArkshValueItem;

typedef struct {
  ArkshValueItem items[ARKSH_MAX_COLLECTION_ITEMS];
  size_t count;
} ArkshValueList;

typedef struct {
  char key[ARKSH_MAX_NAME];
  ArkshValueItem value;
} ArkshValueMapEntry;

typedef struct {
  ArkshValueMapEntry entries[ARKSH_MAX_COLLECTION_ITEMS];
  size_t count;
} ArkshValueMap;

struct ArkshValue {
  ArkshValueKind kind;
  char text[ARKSH_MAX_OUTPUT];
  double number;
  int boolean;
  ArkshObject object;
  ArkshBlock block;
  ArkshValueList list;
  ArkshValueMap map;
};

const char *arksh_object_kind_name(ArkshObjectKind kind);
const char *arksh_value_kind_name(ArkshValueKind kind);
void arksh_value_init(ArkshValue *value);
void arksh_value_item_init(ArkshValueItem *item);
void arksh_value_free(ArkshValue *value);
void arksh_value_item_free(ArkshValueItem *item);
int arksh_value_copy(ArkshValue *dest, const ArkshValue *src);
int arksh_value_item_copy(ArkshValueItem *dest, const ArkshValueItem *src);
void arksh_value_set_string(ArkshValue *value, const char *text);
void arksh_value_set_number(ArkshValue *value, double number);
/* E6-S5: explicit numeric sub-kind setters */
void arksh_value_set_integer(ArkshValue *value, double number);
void arksh_value_set_float(ArkshValue *value, double number);
void arksh_value_set_double(ArkshValue *value, double number);
void arksh_value_set_imaginary(ArkshValue *value, double number);
void arksh_value_set_boolean(ArkshValue *value, int boolean);
void arksh_value_set_object(ArkshValue *value, const ArkshObject *object);
void arksh_value_set_block(ArkshValue *value, const ArkshBlock *block);
void arksh_value_set_class(ArkshValue *value, const char *class_name);
void arksh_value_set_instance(ArkshValue *value, const char *class_name, int instance_id);
void arksh_value_set_map(ArkshValue *value);
/* E6-S6: create an empty Dict value (immutable key-value dictionary). */
void arksh_value_set_dict(ArkshValue *value);
/* E6-S2-T1: create a MAP value tagged with a custom type name.
 * The type name is stored as the "__type__" entry in the map and is returned
 * by "-> type".  Extensions registered with this type name as target will
 * match receivers created with this function. */
void arksh_value_set_typed_map(ArkshValue *value, const char *type_name);
int arksh_value_set_from_item(ArkshValue *value, const ArkshValueItem *item);
int arksh_value_item_set_from_value(ArkshValueItem *item, const ArkshValue *value);
int arksh_value_list_append_item(ArkshValue *value, const ArkshValueItem *item);
int arksh_value_list_append_value(ArkshValue *value, const ArkshValue *item_value);
int arksh_value_map_set(ArkshValue *value, const char *key, const ArkshValue *entry_value);
const ArkshValueItem *arksh_value_map_get_item(const ArkshValue *value, const char *key);
int arksh_value_render(const ArkshValue *value, char *out, size_t out_size);
int arksh_value_item_render(const ArkshValueItem *item, char *out, size_t out_size);
int arksh_value_get_property_value(const ArkshValue *value, const char *property, ArkshValue *out_value, char *error, size_t error_size);
int arksh_value_get_property(const ArkshValue *value, const char *property, char *out, size_t out_size);
int arksh_value_item_get_property_value(const ArkshValueItem *item, const char *property, ArkshValue *out_value, char *error, size_t error_size);
int arksh_value_item_get_property(const ArkshValueItem *item, const char *property, char *out, size_t out_size);
int arksh_value_to_json(const ArkshValue *value, char *out, size_t out_size);
int arksh_value_parse_json(const char *text, ArkshValue *out_value, char *error, size_t error_size);
int arksh_object_resolve(const char *cwd, const char *selector, ArkshObject *out_object);
int arksh_object_get_property(const ArkshObject *object, const char *property, char *out, size_t out_size);
int arksh_object_call_method(const ArkshObject *object, const char *method, int argc, char **argv, char *out, size_t out_size);
int arksh_object_call_method_value(const ArkshObject *object, const char *method, int argc, char **argv, ArkshValue *out_value, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
