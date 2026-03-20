#include <stdio.h>
#include <string.h>

#include "arksh/plugin.h"

static int hello_plugin_command(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  const char *name = "world";

  (void) shell;

  if (argc >= 2) {
    name = argv[1];
  }

  snprintf(out, out_size, "hello from plugin, %s", name);
  return 0;
}

static int sample_map_add_string(ArkshValue *map, const char *key, const char *text) {
  ArkshValue entry;
  int status;

  arksh_value_set_string(&entry, text);
  status = arksh_value_map_set(map, key, &entry);
  arksh_value_free(&entry);
  return status;
}

static int sample_map_add_bool(ArkshValue *map, const char *key, int boolean) {
  ArkshValue entry;
  int status;

  arksh_value_set_boolean(&entry, boolean);
  status = arksh_value_map_set(map, key, &entry);
  arksh_value_free(&entry);
  return status;
}

static int sample_value_resolver(
  ArkshShell *shell,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  (void) shell;
  (void) args;

  if (out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (argc != 0) {
    snprintf(error, error_size, "sample() does not accept arguments");
    return 1;
  }

  arksh_value_set_map(out_value);
  if (sample_map_add_string(out_value, "name", "sample-plugin") != 0 ||
      sample_map_add_string(out_value, "version", "0.2.0") != 0 ||
      sample_map_add_string(out_value, "resolver", "sample()") != 0 ||
      sample_map_add_string(out_value, "stage", "sample_wrap()") != 0 ||
      sample_map_add_bool(out_value, "loaded", 1) != 0) {
    snprintf(error, error_size, "sample() result is too large");
    return 1;
  }

  return 0;
}

static int sample_wrap_stage(
  ArkshShell *shell,
  ArkshValue *value,
  const char *raw_args,
  char *error,
  size_t error_size
) {
  char rendered[ARKSH_MAX_OUTPUT];
  char wrapped[ARKSH_MAX_OUTPUT];
  const char *label = "sample";

  (void) shell;

  if (value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (raw_args != NULL && raw_args[0] != '\0') {
    label = raw_args;
  }
  if (arksh_value_render(value, rendered, sizeof(rendered)) != 0) {
    snprintf(error, error_size, "unable to render sample_wrap() input");
    return 1;
  }

  snprintf(wrapped, sizeof(wrapped), "%s:%s", label, rendered);
  arksh_value_set_string(value, wrapped);
  return 0;
}

static int sample_directory_tag(ArkshShell *shell, const ArkshValue *receiver, ArkshValue *out_value, char *error, size_t error_size) {
  (void) shell;

  if (receiver == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (receiver->kind != ARKSH_VALUE_OBJECT || receiver->object.kind != ARKSH_OBJECT_DIRECTORY) {
    snprintf(error, error_size, "sample_tag is only valid on directory objects");
    return 1;
  }

  arksh_value_set_string(out_value, "sample-plugin:directory");
  return 0;
}

static int sample_object_label(
  ArkshShell *shell,
  const ArkshValue *receiver,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  char prefix[128] = "label";
  char rendered[ARKSH_MAX_OUTPUT];

  (void) shell;

  if (receiver == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (receiver->kind != ARKSH_VALUE_OBJECT) {
    snprintf(error, error_size, "sample_label() is only valid on filesystem objects");
    return 1;
  }

  if (argc >= 1) {
    if (arksh_value_render(&args[0], prefix, sizeof(prefix)) != 0) {
      snprintf(error, error_size, "unable to render sample_label() prefix");
      return 1;
    }
  }

  snprintf(
    rendered,
    sizeof(rendered),
    "%s:%s:%s",
    prefix,
    arksh_object_kind_name(receiver->object.kind),
    receiver->object.path
  );
  arksh_value_set_string(out_value, rendered);
  return 0;
}

ARKSH_PLUGIN_EXPORT int arksh_plugin_init(ArkshShell *shell, const ArkshPluginHost *host, ArkshPluginInfo *out_info) {
  if (shell == NULL || host == NULL || out_info == NULL) {
    return 1;
  }

  if (host->api_version != ARKSH_PLUGIN_API_VERSION ||
      host->register_command == NULL ||
      host->register_property_extension == NULL ||
      host->register_method_extension == NULL ||
      host->register_value_resolver == NULL ||
      host->register_pipeline_stage == NULL ||
      host->register_type_descriptor == NULL) {
    return 1;
  }

  snprintf(out_info->name, sizeof(out_info->name), "sample-plugin");
  snprintf(out_info->version, sizeof(out_info->version), "0.2.0");
  snprintf(out_info->description, sizeof(out_info->description), "Plugin di esempio con comando, extension, resolver e stage");

  if (host->register_command(shell, "hello-plugin", "sample plugin greeting command", hello_plugin_command) != 0) {
    return 1;
  }
  if (host->register_value_resolver(shell, "sample", sample_value_resolver) != 0) {
    return 1;
  }
  if (host->register_pipeline_stage(shell, "sample_wrap", sample_wrap_stage) != 0) {
    return 1;
  }
  if (host->register_property_extension(shell, "directory", "sample_tag", sample_directory_tag) != 0) {
    return 1;
  }
  return host->register_method_extension(shell, "object", "sample_label", sample_object_label);
}
