#include <stdio.h>
#include <string.h>

#include "oosh/plugin.h"

/*
 * Plugin template per oosh.
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

static int skeleton_info_command(OoshShell *shell, int argc, char **argv, char *out, size_t out_size) {
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
    snprintf(error, error_size, "skeleton_namespace() non accetta argomenti");
    return 1;
  }

  /*
   * TODO:
   * - esporre uno stato typed del tuo dominio
   * - restituire mappe o liste annidate
   */
  oosh_value_set_string(out_value, "TODO: implementa un resolver typed per il tuo dominio");
  return 0;
}

static int skeleton_pipeline_stage(
  OoshShell *shell,
  OoshValue *value,
  const char *raw_args,
  char *error,
  size_t error_size
) {
  char rendered[OOSH_MAX_OUTPUT];
  char wrapped[OOSH_MAX_OUTPUT];

  (void) shell;

  if (value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (oosh_value_render(value, rendered, sizeof(rendered)) != 0) {
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
  oosh_value_set_string(value, wrapped);
  return 0;
}

static int skeleton_object_badge(
  OoshShell *shell,
  const OoshValue *receiver,
  OoshValue *out_value,
  char *error,
  size_t error_size
) {
  char rendered[OOSH_MAX_OUTPUT];

  (void) shell;

  if (receiver == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (oosh_value_render(receiver, rendered, sizeof(rendered)) != 0) {
    snprintf(error, error_size, "unable to render receiver for skeleton_badge");
    return 1;
  }

  /*
   * TODO:
   * - validare il tipo del receiver
   * - leggere stato esterno
   * - trasformare il valore in un oggetto o valore piu ricco
   */
  snprintf(out_value->text, sizeof(out_value->text), "skeleton:%s", rendered);
  out_value->kind = OOSH_VALUE_STRING;
  return 0;
}

static int skeleton_object_action(
  OoshShell *shell,
  const OoshValue *receiver,
  int argc,
  const OoshValue *args,
  OoshValue *out_value,
  char *error,
  size_t error_size
) {
  char receiver_text[OOSH_MAX_OUTPUT];
  char action_text[128] = "default-action";

  (void) shell;

  if (receiver == NULL || out_value == NULL || error == NULL || error_size == 0) {
    return 1;
  }

  if (oosh_value_render(receiver, receiver_text, sizeof(receiver_text)) != 0) {
    snprintf(error, error_size, "unable to render receiver for skeleton_action");
    return 1;
  }

  if (argc >= 1 && oosh_value_render(&args[0], action_text, sizeof(action_text)) != 0) {
    snprintf(error, error_size, "unable to render skeleton_action argument");
    return 1;
  }

  /*
   * TODO:
   * - convertire gli argomenti in parametri tipizzati
   * - invocare API esterne o logica di business
   * - restituire stringa, numero, bool, lista o oggetto
   */
  snprintf(out_value->text, sizeof(out_value->text), "stub:%s:%s", action_text, receiver_text);
  out_value->kind = OOSH_VALUE_STRING;
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

  snprintf(out_info->name, sizeof(out_info->name), "skeleton-plugin");
  snprintf(out_info->version, sizeof(out_info->version), "0.1.0");
  snprintf(
    out_info->description,
    sizeof(out_info->description),
    "Template plugin che registra un comando, una proprieta e un metodo stub"
  );

  if (host->register_command(shell, "skeleton-info", "template command exported by skeleton-plugin", skeleton_info_command) != 0) {
    return 1;
  }

  /*
   * Esempio di resolver:
   * - accessibile come `skeleton_namespace()`
   * - utile per esporre uno stato typed dal plugin
   */
  if (host->register_value_resolver(shell, "skeleton_namespace", skeleton_namespace_resolver) != 0) {
    return 1;
  }

  /*
   * Esempio di stage:
   * - accessibile come `|> skeleton_stage(...)`
   * - utile per trasformare valori o collezioni dentro una pipeline
   */
  if (host->register_pipeline_stage(shell, "skeleton_stage", skeleton_pipeline_stage) != 0) {
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
