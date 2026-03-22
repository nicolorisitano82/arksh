#include <stdio.h>
#include <string.h>

#include "arksh/perf.h"
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

static int host_register_value_resolver(ArkshShell *shell, const char *name, const char *description, ArkshValueResolverFn fn) {
  return arksh_shell_register_value_resolver(shell, name, description, fn, 1);
}

static int host_register_pipeline_stage(ArkshShell *shell, const char *name, const char *description, ArkshPipelineStageFn fn) {
  return arksh_shell_register_pipeline_stage(shell, name, description, fn, 1);
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

static int join_plugin_path(const char *directory, const char *name, char *out, size_t out_size) {
  const char *separator = arksh_platform_path_separator();
  size_t dir_len;

  if (directory == NULL || name == NULL || out == NULL || out_size == 0) {
    return 1;
  }

  if (directory[0] == '\0' || strcmp(directory, ".") == 0) {
    copy_string(out, out_size, name);
    return 0;
  }

  dir_len = strlen(directory);
  if (dir_len > 0 && (directory[dir_len - 1] == '/' || directory[dir_len - 1] == '\\')) {
    if (snprintf(out, out_size, "%s%s", directory, name) >= (int) out_size) {
      return 1;
    }
    return 0;
  }

  if (snprintf(out, out_size, "%s%s%s", directory, separator, name) >= (int) out_size) {
    return 1;
  }

  return 0;
}

static int plugin_query_has_path_component(const char *text) {
  if (text == NULL) {
    return 0;
  }

  if (strchr(text, '/') != NULL || strchr(text, '\\') != NULL) {
    return 1;
  }

#ifdef _WIN32
  if (strchr(text, ':') != NULL) {
    return 1;
  }
#endif

  return 0;
}

static int plugin_query_has_library_suffix(const char *text) {
  const char *ext = arksh_platform_plugin_extension();
  size_t text_len;
  size_t ext_len;

  if (text == NULL || ext == NULL) {
    return 0;
  }

  text_len = strlen(text);
  ext_len = strlen(ext);
  if (text_len < ext_len) {
    return 0;
  }

  return strcmp(text + text_len - ext_len, ext) == 0;
}

static void normalize_plugin_stem(const char *query, char *out, size_t out_size) {
  size_t i;
  size_t j = 0;

  if (out == NULL || out_size == 0) {
    return;
  }

  out[0] = '\0';
  if (query == NULL) {
    return;
  }

  for (i = 0; query[i] != '\0' && j + 1 < out_size; ++i) {
    char c = query[i];

    if (c == '-') {
      c = '_';
    }
    out[j++] = c;
  }
  out[j] = '\0';
}

static int resolve_installed_plugin_dir(const ArkshShell *shell, char *out, size_t out_size) {
  char executable_dir[ARKSH_MAX_PATH];
  char candidate[ARKSH_MAX_PATH];

  if (shell == NULL || shell->executable_path[0] == '\0' || out == NULL || out_size == 0) {
    return 1;
  }

  arksh_platform_dirname(shell->executable_path, executable_dir, sizeof(executable_dir));
  if (executable_dir[0] == '\0') {
    return 1;
  }

#ifdef ARKSH_RUNTIME_PLUGIN_RELDIR
  if (join_plugin_path(executable_dir, ARKSH_RUNTIME_PLUGIN_RELDIR, candidate, sizeof(candidate)) != 0) {
    return 1;
  }
  if (arksh_platform_resolve_path(executable_dir, candidate, out, out_size) != 0) {
    return 1;
  }
  return 0;
#else
  (void) candidate;
  out[0] = '\0';
  return 1;
#endif
}

int arksh_shell_resolve_plugin_path(const ArkshShell *shell, const char *query, char *out, size_t out_size) {
  ArkshPlatformFileInfo info;
  char normalized[ARKSH_MAX_NAME];
  char candidate[ARKSH_MAX_PATH];
  char search_dirs[2][ARKSH_MAX_PATH];
  char candidate_names[7][ARKSH_MAX_PATH];
  const char *ext = arksh_platform_plugin_extension();
  int search_dir_count = 0;
  int candidate_count = 0;
  int dir_index;
  int name_index;

  if (shell == NULL || query == NULL || query[0] == '\0' || out == NULL || out_size == 0) {
    return 1;
  }

  if (plugin_query_has_path_component(query)) {
    return arksh_platform_resolve_path(shell->cwd, query, out, out_size);
  }

  if (arksh_platform_resolve_path(shell->cwd, query, out, out_size) == 0) {
    memset(&info, 0, sizeof(info));
    if (arksh_platform_stat(out, &info) == 0 && info.exists && info.is_file) {
      return 0;
    }
  }

  if (shell->plugin_dir[0] != '\0') {
    copy_string(search_dirs[search_dir_count], sizeof(search_dirs[search_dir_count]), shell->plugin_dir);
    search_dir_count++;
  }
  if (search_dir_count < 2 && resolve_installed_plugin_dir(shell, search_dirs[search_dir_count], sizeof(search_dirs[search_dir_count])) == 0) {
    if (search_dir_count == 0 || strcmp(search_dirs[0], search_dirs[search_dir_count]) != 0) {
      search_dir_count++;
    }
  }

  normalize_plugin_stem(query, normalized, sizeof(normalized));

  copy_string(candidate_names[candidate_count++], sizeof(candidate_names[0]), query);
  if (!plugin_query_has_library_suffix(query)) {
    snprintf(candidate_names[candidate_count++], sizeof(candidate_names[0]), "%s%s", query, ext);
  }
  if (normalized[0] != '\0' && strcmp(normalized, query) != 0) {
    copy_string(candidate_names[candidate_count++], sizeof(candidate_names[0]), normalized);
  }
  if (normalized[0] != '\0' && !plugin_query_has_library_suffix(normalized)) {
    snprintf(candidate_names[candidate_count++], sizeof(candidate_names[0]), "%s%s", normalized, ext);
  }
  if (normalized[0] != '\0') {
    snprintf(candidate_names[candidate_count++], sizeof(candidate_names[0]), "%s_plugin%s", normalized, ext);
    snprintf(candidate_names[candidate_count++], sizeof(candidate_names[0]), "arksh_%s%s", normalized, ext);
    snprintf(candidate_names[candidate_count++], sizeof(candidate_names[0]), "arksh_%s_plugin%s", normalized, ext);
  }

  for (dir_index = 0; dir_index < search_dir_count; ++dir_index) {
    for (name_index = 0; name_index < candidate_count; ++name_index) {
      memset(&info, 0, sizeof(info));
      if (join_plugin_path(search_dirs[dir_index], candidate_names[name_index], candidate, sizeof(candidate)) != 0) {
        continue;
      }
      if (arksh_platform_resolve_path(shell->cwd, candidate, out, out_size) != 0) {
        continue;
      }
      if (arksh_platform_stat(out, &info) == 0 && info.exists && info.is_file) {
        return 0;
      }
    }
  }

  return arksh_platform_resolve_path(shell->cwd, query, out, out_size);
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

  if (arksh_shell_resolve_plugin_path(shell, path, resolved, sizeof(resolved)) != 0) {
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
