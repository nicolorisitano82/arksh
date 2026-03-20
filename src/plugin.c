#include <stdio.h>
#include <string.h>

#include "arksh/platform.h"
#include "arksh/plugin.h"
#include "arksh/shell.h"

static int host_register_command(ArkshShell *shell, const char *name, const char *description, ArkshCommandFn fn) {
  return arksh_shell_register_command(shell, name, description, fn, 1);
}

static int host_register_property_extension(ArkshShell *shell, const char *target, const char *name, ArkshExtensionPropertyFn fn) {
  return arksh_shell_register_native_property_extension(shell, target, name, fn, 1);
}

static int host_register_method_extension(ArkshShell *shell, const char *target, const char *name, ArkshExtensionMethodFn fn) {
  return arksh_shell_register_native_method_extension(shell, target, name, fn, 1);
}

static int host_register_value_resolver(ArkshShell *shell, const char *name, ArkshValueResolverFn fn) {
  return arksh_shell_register_value_resolver(shell, name, fn, 1);
}

static int host_register_pipeline_stage(ArkshShell *shell, const char *name, ArkshPipelineStageFn fn) {
  return arksh_shell_register_pipeline_stage(shell, name, fn, 1);
}

static int host_register_type_descriptor(ArkshShell *shell, const char *type_name, const char *description) {
  return arksh_shell_register_type_descriptor(shell, type_name, description);
}

static void copy_string(char *dest, size_t dest_size, const char *src) {
  if (dest_size == 0) {
    return;
  }

  snprintf(dest, dest_size, "%s", src == NULL ? "" : src);
}

int arksh_shell_load_plugin(ArkshShell *shell, const char *path, char *out, size_t out_size) {
  char resolved[ARKSH_MAX_PATH];
  void *handle;
  ArkshPluginInitFn init_fn;
  ArkshPluginInfo info;
  ArkshPluginHost host;
  size_t i;
  int status;

  if (shell == NULL || path == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (shell->plugin_count >= ARKSH_MAX_PLUGINS) {
    snprintf(out, out_size, "plugin limit reached");
    return 1;
  }

  if (arksh_platform_resolve_path(shell->cwd, path, resolved, sizeof(resolved)) != 0) {
    snprintf(out, out_size, "unable to resolve plugin path: %s", path);
    return 1;
  }

  for (i = 0; i < shell->plugin_count; ++i) {
    if (strcmp(shell->plugins[i].path, resolved) == 0) {
      snprintf(
        out,
        out_size,
        "plugin already loaded: %s [%s]",
        resolved,
        shell->plugins[i].active ? "enabled" : "disabled"
      );
      return 0;
    }
  }

  handle = arksh_platform_library_open(resolved);
  if (handle == NULL) {
    snprintf(out, out_size, "unable to open plugin %s: %s", resolved, arksh_platform_last_error());
    return 1;
  }

  init_fn = (ArkshPluginInitFn) arksh_platform_library_symbol(handle, "arksh_plugin_init");
  if (init_fn == NULL) {
    snprintf(out, out_size, "missing arksh_plugin_init in %s: %s", resolved, arksh_platform_last_error());
    arksh_platform_library_close(handle);
    return 1;
  }

  memset(&info, 0, sizeof(info));
  host.api_version = ARKSH_PLUGIN_API_VERSION;
  host.register_command = host_register_command;
  host.register_property_extension = host_register_property_extension;
  host.register_method_extension = host_register_method_extension;
  host.register_value_resolver = host_register_value_resolver;
  host.register_pipeline_stage = host_register_pipeline_stage;
  host.register_type_descriptor = host_register_type_descriptor;

  memset(&shell->plugins[shell->plugin_count], 0, sizeof(shell->plugins[shell->plugin_count]));
  copy_string(shell->plugins[shell->plugin_count].path, sizeof(shell->plugins[shell->plugin_count].path), resolved);
  shell->plugins[shell->plugin_count].handle = handle;
  shell->plugins[shell->plugin_count].active = 1;
  shell->loading_plugin_index = (int) shell->plugin_count;

  status = init_fn(shell, &host, &info);
  shell->loading_plugin_index = -1;
  if (status != 0) {
    snprintf(out, out_size, "plugin init failed for %s", resolved);
    memset(&shell->plugins[shell->plugin_count], 0, sizeof(shell->plugins[shell->plugin_count]));
    arksh_platform_library_close(handle);
    return 1;
  }

  copy_string(shell->plugins[shell->plugin_count].name, sizeof(shell->plugins[shell->plugin_count].name), info.name[0] == '\0' ? "unnamed-plugin" : info.name);
  copy_string(shell->plugins[shell->plugin_count].version, sizeof(shell->plugins[shell->plugin_count].version), info.version[0] == '\0' ? "0.0.0" : info.version);
  copy_string(shell->plugins[shell->plugin_count].description, sizeof(shell->plugins[shell->plugin_count].description), info.description);
  shell->plugin_count++;

  snprintf(out, out_size, "plugin loaded: %s (%s)", info.name[0] == '\0' ? "unnamed-plugin" : info.name, resolved);
  return 0;
}
