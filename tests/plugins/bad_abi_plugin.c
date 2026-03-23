#include <string.h>

#include "arksh/plugin.h"

ARKSH_PLUGIN_EXPORT int arksh_plugin_query(ArkshPluginInfo *out_info) {
  if (out_info == NULL) {
    return 1;
  }

  memset(out_info, 0, sizeof(*out_info));
  strncpy(out_info->name, "bad-abi-plugin", sizeof(out_info->name) - 1);
  strncpy(out_info->version, "0.0.1", sizeof(out_info->version) - 1);
  strncpy(out_info->description, "Test plugin with incompatible ABI", sizeof(out_info->description) - 1);
  out_info->abi_major = ARKSH_PLUGIN_ABI_MAJOR + 1;
  out_info->abi_minor = 0;
  out_info->required_host_capabilities = 0;
  out_info->plugin_capabilities = 0;
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
