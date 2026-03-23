#include <stdio.h>
#include <string.h>

#include "arksh/plugin.h"

/*
 * Plugin template per arksh.
 *
 * Obiettivo:
 * - mostrare la struttura minima di un plugin
 * - far vedere dove registrare comandi, proprieta, metodi, resolver e stage
 * - lasciare stub semplici da sostituire con logica reale
 *
 * Copia questo file in una nuova cartella plugin e personalizza:
 * - nome plugin
 * - versione
 * - descrizione
 * - nomi dei membri registrati
 * - logica interna dei callback
 */

static int skeleton_info_command(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  const char *topic = "overview";

  (void) shell;

  if (argc >= 2) {
    topic = argv[1];
  }

  snprintf(
    out,
    out_size,
    "skeleton-plugin stub command, topic=%s\n"
    "TODO: sostituisci questo comando con la logica del tuo dominio",
    topic
  );
  return 0;
}

static int skeleton_namespace_resolver(
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
    snprintf(error, error_size, "skeleton_namespace() non accetta argomenti");
    return 1;
  }

  /*
   * TODO:
   * - esporre uno stato typed del tuo dominio
   * - restituire mappe o liste annidate
   */
  arksh_value_set_string(out_value, "TODO: implementa un resolver typed per il tuo dominio");
  return 0;
}

static int skeleton_pipeline_stage(
  ArkshShell *shell,
  ArkshValue *value,
  const char *raw_args,
  char *error,
  size_t error_size
) {
  char rendered[ARKSH_MAX_OUTPUT];
  char wrapped[ARKSH_MAX_OUTPUT];

  (void) shell;

  if (value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (arksh_value_render(value, rendered, sizeof(rendered)) != 0) {
    snprintf(error, error_size, "unable to render value for skeleton_stage");
    return 1;
  }

  snprintf(
    wrapped,
    sizeof(wrapped),
    "TODO[%s]:%s",
    raw_args != NULL && raw_args[0] != '\0' ? raw_args : "stage",
    rendered
  );
  arksh_value_set_string(value, wrapped);
  return 0;
}

static int skeleton_object_badge(
  ArkshShell *shell,
  const ArkshValue *receiver,
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  char rendered[ARKSH_MAX_OUTPUT];

  (void) shell;

  if (receiver == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (arksh_value_render(receiver, rendered, sizeof(rendered)) != 0) {
    snprintf(error, error_size, "unable to render receiver for skeleton_badge");
    return 1;
  }

  /*
   * TODO:
   * - validare il tipo del receiver
   * - leggere stato esterno
   * - trasformare il valore in un oggetto o valore piu ricco
   */
  {
    char result[ARKSH_MAX_OUTPUT];
    snprintf(result, sizeof(result), "skeleton:%s", rendered);
    arksh_value_set_string(out_value, result);
  }
  return 0;
}

static int skeleton_object_action(
  ArkshShell *shell,
  const ArkshValue *receiver,
  int argc,
  const ArkshValue *args,
  ArkshValue *out_value,
  char *error,
  size_t error_size
) {
  char receiver_text[ARKSH_MAX_OUTPUT];
  char action_text[128] = "default-action";

  (void) shell;

  if (receiver == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (arksh_value_render(receiver, receiver_text, sizeof(receiver_text)) != 0) {
    snprintf(error, error_size, "unable to render receiver for skeleton_action");
    return 1;
  }

  if (argc >= 1 && arksh_value_render(&args[0], action_text, sizeof(action_text)) != 0) {
    snprintf(error, error_size, "unable to render skeleton_action argument");
    return 1;
  }

  /*
   * TODO:
   * - convertire gli argomenti in parametri tipizzati
   * - invocare API esterne o logica di business
   * - restituire stringa, numero, bool, lista o oggetto
   */
  {
    char result[ARKSH_MAX_OUTPUT];
    snprintf(result, sizeof(result), "stub:%s:%s", action_text, receiver_text);
    arksh_value_set_string(out_value, result);
  }
  return 0;
}

ARKSH_PLUGIN_EXPORT int arksh_plugin_query(ArkshPluginInfo *out_info) {
  if (out_info == NULL) {
    return 1;
  }

  memset(out_info, 0, sizeof(*out_info));
  snprintf(out_info->name, sizeof(out_info->name), "skeleton-plugin");
  snprintf(out_info->version, sizeof(out_info->version), "0.1.0");
  snprintf(
    out_info->description,
    sizeof(out_info->description),
    "Template plugin che registra un comando, una proprieta e un metodo stub"
  );
  out_info->abi_major = ARKSH_PLUGIN_ABI_MAJOR;
  out_info->abi_minor = ARKSH_PLUGIN_ABI_MINOR;
  out_info->required_host_capabilities =
    ARKSH_PLUGIN_CAP_COMMANDS |
    ARKSH_PLUGIN_CAP_PROPERTY_EXTENSIONS |
    ARKSH_PLUGIN_CAP_METHOD_EXTENSIONS |
    ARKSH_PLUGIN_CAP_VALUE_RESOLVERS |
    ARKSH_PLUGIN_CAP_PIPELINE_STAGES |
    ARKSH_PLUGIN_CAP_TYPE_DESCRIPTORS;
  out_info->plugin_capabilities = out_info->required_host_capabilities;
  return 0;
}

ARKSH_PLUGIN_EXPORT int arksh_plugin_init(ArkshShell *shell, const ArkshPluginHost *host, ArkshPluginInfo *out_info) {
  if (shell == NULL || host == NULL || out_info == NULL) {
    return 1;
  }

  if (host->abi_major != ARKSH_PLUGIN_ABI_MAJOR ||
      host->abi_minor < ARKSH_PLUGIN_ABI_MINOR ||
      host->register_command == NULL ||
      host->register_property_extension == NULL ||
      host->register_method_extension == NULL ||
      host->register_value_resolver == NULL ||
      host->register_pipeline_stage == NULL ||
      host->register_type_descriptor == NULL) {
    return 1;
  }

  if (arksh_plugin_query(out_info) != 0) {
    return 1;
  }

  if (host->register_command(shell, "skeleton-info", "template command exported by skeleton-plugin", skeleton_info_command) != 0) {
    return 1;
  }

  /*
   * Esempio di resolver:
   * - accessibile come `skeleton_namespace()`
   * - utile per esporre uno stato typed dal plugin
   */
  if (host->register_value_resolver(shell, "skeleton_namespace", "skeleton namespace (sostituire con una descrizione breve)", skeleton_namespace_resolver) != 0) {
    return 1;
  }

  /*
   * Esempio di stage:
   * - accessibile come `|> skeleton_stage(...)`
   * - utile per trasformare valori o collezioni dentro una pipeline
   */
  if (host->register_pipeline_stage(shell, "skeleton_stage", "skeleton stage (sostituire con una descrizione breve)", skeleton_pipeline_stage) != 0) {
    return 1;
  }

  /*
   * Esempio di proprieta:
   * - target "object": disponibile su qualunque valore filesystem object-aware
   * - nome accessibile da shell: <receiver> -> skeleton_badge
   */
  if (host->register_property_extension(shell, "object", "skeleton_badge", skeleton_object_badge) != 0) {
    return 1;
  }

  /*
   * Esempio di metodo:
   * - target "object"
   * - nome accessibile da shell: <receiver> -> skeleton_action(...)
   */
  return host->register_method_extension(shell, "object", "skeleton_action", skeleton_object_action);
}
