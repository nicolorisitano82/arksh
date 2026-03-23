#include <string.h>

#include "arksh/plugin.h"

#define NEED_CAP_PLUGIN_FUTURE_CAP (1ull << 60)

ARKSH_PLUGIN_EXPORT int arksh_plugin_query(ArkshPluginInfo *out_info) {
  if (out_info == NULL) {
    return 1;
  }

  memset(out_info, 0, sizeof(*out_info));
  strncpy(out_info->name, "need-cap-plugin", sizeof(out_info->name) - 1);
  strncpy(out_info->version, "0.0.1", sizeof(out_info->version) - 1);
  strncpy(out_info->description, "Test plugin with unsupported capability requirement", sizeof(out_info->description) - 1);
  out_info->abi_major = ARKSH_PLUGIN_ABI_MAJOR;
  out_info->abi_minor = ARKSH_PLUGIN_ABI_MINOR;
  out_info->required_host_capabilities = NEED_CAP_PLUGIN_FUTURE_CAP;
  out_info->plugin_capabilities = ARKSH_PLUGIN_CAP_COMMANDS;
  return 0;
}

ARKSH_PLUGIN_EXPORT int arksh_plugin_init(
  ArkshShell *shell,
  const ArkshPluginHost *host,
  ArkshPluginInfo *out_info
) {
  (void) shell;
  (void) host;
  return arksh_plugin_query(out_info);
}
