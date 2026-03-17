#ifndef OOSH_OBJECT_H
#define OOSH_OBJECT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OOSH_MAX_PATH 1024
#define OOSH_MAX_NAME 128
#define OOSH_MAX_OUTPUT 8192
#define OOSH_MAX_ARGS 16
#define OOSH_MAX_TOKEN 256
#define OOSH_MAX_COLLECTION_ITEMS 128
#define OOSH_MAX_VALUE_TEXT 1024
#define OOSH_MAX_BLOCK_PARAMS 4
#define OOSH_MAX_BLOCK_SOURCE 1024

typedef enum {
  OOSH_OBJECT_UNKNOWN = 0,
  OOSH_OBJECT_PATH,
  OOSH_OBJECT_FILE,
  OOSH_OBJECT_DIRECTORY,
  OOSH_OBJECT_DEVICE,
  OOSH_OBJECT_MOUNT_POINT
} OoshObjectKind;

typedef struct {
  OoshObjectKind kind;
  char path[OOSH_MAX_PATH];
  char name[OOSH_MAX_NAME];
  int exists;
  unsigned long long size;
  int hidden;
  int readable;
  int writable;
} OoshObject;

typedef struct {
  OoshObject items[OOSH_MAX_COLLECTION_ITEMS];
  size_t count;
} OoshObjectCollection;

typedef struct {
  char params[OOSH_MAX_BLOCK_PARAMS][OOSH_MAX_NAME];
  int param_count;
  char body[OOSH_MAX_BLOCK_SOURCE];
  char source[OOSH_MAX_BLOCK_SOURCE];
} OoshBlock;

typedef enum {
  OOSH_VALUE_EMPTY = 0,
  OOSH_VALUE_STRING,
  OOSH_VALUE_NUMBER,
  OOSH_VALUE_BOOLEAN,
  OOSH_VALUE_OBJECT,
  OOSH_VALUE_BLOCK,
  OOSH_VALUE_LIST,
  OOSH_VALUE_MAP,
  OOSH_VALUE_CLASS,
  OOSH_VALUE_INSTANCE
} OoshValueKind;

typedef struct OoshValue OoshValue;

typedef struct {
  OoshValueKind kind;
  char text[OOSH_MAX_VALUE_TEXT];
  double number;
  int boolean;
  OoshObject object;
  OoshBlock block;
  OoshValue *nested;
} OoshValueItem;

typedef struct {
  OoshValueItem items[OOSH_MAX_COLLECTION_ITEMS];
  size_t count;
} OoshValueList;

typedef struct {
  char key[OOSH_MAX_NAME];
  OoshValueItem value;
} OoshValueMapEntry;

typedef struct {
  OoshValueMapEntry entries[OOSH_MAX_COLLECTION_ITEMS];
  size_t count;
} OoshValueMap;

struct OoshValue {
  OoshValueKind kind;
  char text[OOSH_MAX_OUTPUT];
  double number;
  int boolean;
  OoshObject object;
  OoshBlock block;
  OoshValueList list;
  OoshValueMap map;
};

const char *oosh_object_kind_name(OoshObjectKind kind);
const char *oosh_value_kind_name(OoshValueKind kind);
void oosh_value_init(OoshValue *value);
void oosh_value_item_init(OoshValueItem *item);
void oosh_value_free(OoshValue *value);
void oosh_value_item_free(OoshValueItem *item);
int oosh_value_copy(OoshValue *dest, const OoshValue *src);
int oosh_value_item_copy(OoshValueItem *dest, const OoshValueItem *src);
void oosh_value_set_string(OoshValue *value, const char *text);
void oosh_value_set_number(OoshValue *value, double number);
void oosh_value_set_boolean(OoshValue *value, int boolean);
void oosh_value_set_object(OoshValue *value, const OoshObject *object);
void oosh_value_set_block(OoshValue *value, const OoshBlock *block);
void oosh_value_set_class(OoshValue *value, const char *class_name);
void oosh_value_set_instance(OoshValue *value, const char *class_name, int instance_id);
void oosh_value_set_map(OoshValue *value);
int oosh_value_set_from_item(OoshValue *value, const OoshValueItem *item);
int oosh_value_item_set_from_value(OoshValueItem *item, const OoshValue *value);
int oosh_value_list_append_item(OoshValue *value, const OoshValueItem *item);
int oosh_value_list_append_value(OoshValue *value, const OoshValue *item_value);
int oosh_value_map_set(OoshValue *value, const char *key, const OoshValue *entry_value);
const OoshValueItem *oosh_value_map_get_item(const OoshValue *value, const char *key);
int oosh_value_render(const OoshValue *value, char *out, size_t out_size);
int oosh_value_item_render(const OoshValueItem *item, char *out, size_t out_size);
int oosh_value_get_property_value(const OoshValue *value, const char *property, OoshValue *out_value, char *error, size_t error_size);
int oosh_value_get_property(const OoshValue *value, const char *property, char *out, size_t out_size);
int oosh_value_item_get_property_value(const OoshValueItem *item, const char *property, OoshValue *out_value, char *error, size_t error_size);
int oosh_value_item_get_property(const OoshValueItem *item, const char *property, char *out, size_t out_size);
int oosh_value_to_json(const OoshValue *value, char *out, size_t out_size);
int oosh_value_parse_json(const char *text, OoshValue *out_value, char *error, size_t error_size);
int oosh_object_resolve(const char *cwd, const char *selector, OoshObject *out_object);
int oosh_object_get_property(const OoshObject *object, const char *property, char *out, size_t out_size);
int oosh_object_call_method(const OoshObject *object, const char *method, int argc, char **argv, char *out, size_t out_size);
int oosh_object_call_method_value(const OoshObject *object, const char *method, int argc, char **argv, OoshValue *out_value, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
