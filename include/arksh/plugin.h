#ifndef ARKSH_PLUGIN_H
#define ARKSH_PLUGIN_H

#include <stddef.h>

#include "arksh/object.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ArkshShell;
typedef struct ArkshShell ArkshShell;

typedef int (*ArkshCommandFn)(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size);
typedef int (*ArkshExtensionPropertyFn)(ArkshShell *shell, const ArkshValue *receiver, ArkshValue *out_value, char *error, size_t error_size);
typedef int (*ArkshExtensionMethodFn)(ArkshShell *shell, const ArkshValue *receiver, int argc, const ArkshValue *args, ArkshValue *out_value, char *error, size_t error_size);
typedef int (*ArkshValueResolverFn)(ArkshShell *shell, int argc, const ArkshValue *args, ArkshValue *out_value, char *error, size_t error_size);
typedef int (*ArkshPipelineStageFn)(ArkshShell *shell, ArkshValue *value, const char *raw_args, char *error, size_t error_size);

typedef struct {
  char name[64];
  char version[32];
  char description[160];
} ArkshPluginInfo;

/*
 * ArkshPluginHost — function table passed to arksh_plugin_init().
 *
 * MEMORY OWNERSHIP (E6-S2-T2)
 * ============================
 * All ArkshValue* parameters labelled "out_value" are owned by the core.
 * The core calls arksh_value_init() before the plugin callback and
 * arksh_value_free() after it returns (whether successful or not).
 * Plugins must NOT free out_value themselves.
 *
 * Nested values passed to helpers such as arksh_value_map_set() are COPIED
 * into the map by the core — plugins can safely stack-allocate them:
 *
 *   ArkshValue entry;
 *   arksh_value_set_string(&entry, "hello");
 *   arksh_value_map_set(map, "key", &entry);
 *   arksh_value_free(&entry);   // safe: map holds its own copy
 *
 * The ArkshValue* "receiver" pointer (property/method callbacks) is READ-ONLY.
 * Do not write to it.  The "value" pointer in pipeline stage callbacks IS
 * mutable: replace its content in-place to transform the pipeline value.
 */
typedef struct {
  unsigned int api_version;
  int (*register_command)(ArkshShell *shell, const char *name, const char *description, ArkshCommandFn fn);
  int (*register_property_extension)(ArkshShell *shell, const char *target, const char *name, ArkshExtensionPropertyFn fn);
  int (*register_method_extension)(ArkshShell *shell, const char *target, const char *name, ArkshExtensionMethodFn fn);
  int (*register_value_resolver)(ArkshShell *shell, const char *name, const char *description, ArkshValueResolverFn fn);
  int (*register_pipeline_stage)(ArkshShell *shell, const char *name, const char *description, ArkshPipelineStageFn fn);
  /* E6-S2-T1: describe a custom named type to the host.  After registration,
   * extensions targeting type_name match typed-map values whose __type__
   * entry equals type_name.  Call arksh_value_set_typed_map() to create them. */
  int (*register_type_descriptor)(ArkshShell *shell, const char *type_name, const char *description);
} ArkshPluginHost;

#define ARKSH_PLUGIN_API_VERSION 4

#ifdef _WIN32
#define ARKSH_PLUGIN_EXPORT __declspec(dllexport)
#else
#define ARKSH_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

typedef int (*ArkshPluginInitFn)(ArkshShell *shell, const ArkshPluginHost *host, ArkshPluginInfo *out_info);
typedef void (*ArkshPluginShutdownFn)(ArkshShell *shell);

#ifdef __cplusplus
}
#endif

#endif
