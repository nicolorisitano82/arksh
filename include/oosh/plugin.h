#ifndef OOSH_PLUGIN_H
#define OOSH_PLUGIN_H

#include <stddef.h>

#include "oosh/object.h"

#ifdef __cplusplus
extern "C" {
#endif

struct OoshShell;
typedef struct OoshShell OoshShell;

typedef int (*OoshCommandFn)(OoshShell *shell, int argc, char **argv, char *out, size_t out_size);
typedef int (*OoshExtensionPropertyFn)(OoshShell *shell, const OoshValue *receiver, OoshValue *out_value, char *error, size_t error_size);
typedef int (*OoshExtensionMethodFn)(OoshShell *shell, const OoshValue *receiver, int argc, const OoshValue *args, OoshValue *out_value, char *error, size_t error_size);
typedef int (*OoshValueResolverFn)(OoshShell *shell, int argc, const OoshValue *args, OoshValue *out_value, char *error, size_t error_size);
typedef int (*OoshPipelineStageFn)(OoshShell *shell, OoshValue *value, const char *raw_args, char *error, size_t error_size);

typedef struct {
  char name[64];
  char version[32];
  char description[160];
} OoshPluginInfo;

/*
 * OoshPluginHost — function table passed to oosh_plugin_init().
 *
 * MEMORY OWNERSHIP (E6-S2-T2)
 * ============================
 * All OoshValue* parameters labelled "out_value" are owned by the core.
 * The core calls oosh_value_init() before the plugin callback and
 * oosh_value_free() after it returns (whether successful or not).
 * Plugins must NOT free out_value themselves.
 *
 * Nested values passed to helpers such as oosh_value_map_set() are COPIED
 * into the map by the core — plugins can safely stack-allocate them:
 *
 *   OoshValue entry;
 *   oosh_value_set_string(&entry, "hello");
 *   oosh_value_map_set(map, "key", &entry);
 *   oosh_value_free(&entry);   // safe: map holds its own copy
 *
 * The OoshValue* "receiver" pointer (property/method callbacks) is READ-ONLY.
 * Do not write to it.  The "value" pointer in pipeline stage callbacks IS
 * mutable: replace its content in-place to transform the pipeline value.
 */
typedef struct {
  unsigned int api_version;
  int (*register_command)(OoshShell *shell, const char *name, const char *description, OoshCommandFn fn);
  int (*register_property_extension)(OoshShell *shell, const char *target, const char *name, OoshExtensionPropertyFn fn);
  int (*register_method_extension)(OoshShell *shell, const char *target, const char *name, OoshExtensionMethodFn fn);
  int (*register_value_resolver)(OoshShell *shell, const char *name, OoshValueResolverFn fn);
  int (*register_pipeline_stage)(OoshShell *shell, const char *name, OoshPipelineStageFn fn);
  /* E6-S2-T1: describe a custom named type to the host.  After registration,
   * extensions targeting type_name match typed-map values whose __type__
   * entry equals type_name.  Call oosh_value_set_typed_map() to create them. */
  int (*register_type_descriptor)(OoshShell *shell, const char *type_name, const char *description);
} OoshPluginHost;

#define OOSH_PLUGIN_API_VERSION 3

#ifdef _WIN32
#define OOSH_PLUGIN_EXPORT __declspec(dllexport)
#else
#define OOSH_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

typedef int (*OoshPluginInitFn)(OoshShell *shell, const OoshPluginHost *host, OoshPluginInfo *out_info);
typedef void (*OoshPluginShutdownFn)(OoshShell *shell);

#ifdef __cplusplus
}
#endif

#endif
