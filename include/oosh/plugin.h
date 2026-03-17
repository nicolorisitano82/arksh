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

typedef struct {
  unsigned int api_version;
  int (*register_command)(OoshShell *shell, const char *name, const char *description, OoshCommandFn fn);
  int (*register_property_extension)(OoshShell *shell, const char *target, const char *name, OoshExtensionPropertyFn fn);
  int (*register_method_extension)(OoshShell *shell, const char *target, const char *name, OoshExtensionMethodFn fn);
  int (*register_value_resolver)(OoshShell *shell, const char *name, OoshValueResolverFn fn);
  int (*register_pipeline_stage)(OoshShell *shell, const char *name, OoshPipelineStageFn fn);
} OoshPluginHost;

#define OOSH_PLUGIN_API_VERSION 2

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
