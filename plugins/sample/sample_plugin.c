#include <stdio.h>
#include <string.h>

#include "oosh/plugin.h"

static int hello_plugin_command(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  const char *name = "world";

  (void) shell;

  if (argc >= 2) {
    name = argv[1];
  }

  snprintf(out, out_size, "hello from plugin, %s", name);
  return 0;
}

static int sample_map_add_string(OoshValue *map, const char *key, const char *text) {
  OoshValue entry;
  int status;

  oosh_value_set_string(&entry, text);
  status = oosh_value_map_set(map, key, &entry);
  oosh_value_free(&entry);
  return status;
}

static int sample_map_add_bool(OoshValue *map, const char *key, int boolean) {
  OoshValue entry;
  int status;

  oosh_value_set_boolean(&entry, boolean);
  status = oosh_value_map_set(map, key, &entry);
  oosh_value_free(&entry);
  return status;
}

static int sample_value_resolver(
  OoshShell *shell,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
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

  oosh_value_set_map(out_value);
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
  OoshShell *shell,
  OoshValue *value,
  const char *raw_args,
  char *error,
  size_t error_size
) {
  char rendered[OOSH_MAX_OUTPUT];
  char wrapped[OOSH_MAX_OUTPUT];
  const char *label = "sample";

  (void) shell;

  if (value == NULL || error == NULL || error_size == 0) {
    return 1;
  }
  if (raw_args != NULL && raw_args[0] != '\0') {
    label = raw_args;
  }
  if (oosh_value_render(value, rendered, sizeof(rendered)) != 0) {
    snprintf(error, error_size, "unable to render sample_wrap() input");
    return 1;
  }

  snprintf(wrapped, sizeof(wrapped), "%s:%s", label, rendered);
  oosh_value_set_string(value, wrapped);
  return 0;
}

static int sample_directory_tag(OoshShell *shell, const OoshValue *receiver, OoshValue *out_value, char *error, size_t error_size) {
  (void) shell;

  if (receiver == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (receiver->kind != OOSH_VALUE_OBJECT || receiver->object.kind != OOSH_OBJECT_DIRECTORY) {
    snprintf(error, error_size, "sample_tag is only valid on directory objects");
    return 1;
  }

  oosh_value_set_string(out_value, "sample-plugin:directory");
  return 0;
}

static int sample_object_label(
  OoshShell *shell,
  const OoshValue *receiver,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
  char *error,
  size_t error_size
) {
  char prefix[128] = "label";
  char rendered[OOSH_MAX_OUTPUT];

  (void) shell;

  if (receiver == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (receiver->kind != OOSH_VALUE_OBJECT) {
    snprintf(error, error_size, "sample_label() is only valid on filesystem objects");
    return 1;
  }

  if (argc >= 1) {
    if (oosh_value_render(&args[0], prefix, sizeof(prefix)) != 0) {
      snprintf(error, error_size, "unable to render sample_label() prefix");
      return 1;
    }
  }

  snprintf(
    rendered,
    sizeof(rendered),
    "%s:%s:%s",
    prefix,
    oosh_object_kind_name(receiver->object.kind),
    receiver->object.path
  );
  oosh_value_set_string(out_value, rendered);
  return 0;
}

OOSH_PLUGIN_EXPORT int oosh_plugin_init(OoshShell *shell, const OoshPluginHost *host, OoshPluginInfo *out_info) {
  if (shell == NULL || host == NULL || out_info == NULL) {
    return 1;
  }

  if (host->api_version != OOSH_PLUGIN_API_VERSION ||
      host->register_command == NULL ||
      host->register_property_extension == NULL ||
      host->register_method_extension == NULL ||
      host->register_value_resolver == NULL ||
      host->register_pipeline_stage == NULL) {
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
