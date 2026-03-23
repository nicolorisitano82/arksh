# Plugin arksh: interfaccia in linguaggio naturale via Claude

## Panoramica

Questo documento descrive il progetto e l'implementazione di **claude-nl**, un plugin per arksh che
traduce comandi espressi in linguaggio naturale in operazioni shell eseguibili, sfruttando l'API di
Claude (Anthropic) come motore di comprensione.

### Esempio d'uso

```arksh
# L'utente digita in arksh:
nl copia tutti i file .jpg dalla cartella downloads a foto/archivio

# Il plugin invia a Claude una richiesta di traduzione e ottiene:
#   cp ~/Downloads/*.jpg ~/foto/archivio/
# Chiede conferma, poi esegue il comando tramite arksh_shell_execute_line.
```

---

## Architettura del plugin

```
┌──────────────────────────────────────────────────────────────────┐
│  arksh (core)                                                    │
│                                                                  │
│  ┌──────────────┐    ┌──────────────────────────────────────┐   │
│  │  nl command  │───▶│  claude_nl plugin                    │   │
│  └──────────────┘    │                                      │   │
│                      │  1. costruisce il prompt di sistema  │   │
│  ┌──────────────┐    │  2. chiama l'API di Claude (HTTPS)   │   │
│  │ |> nl_exec   │───▶│  3. riceve il comando tradotto       │   │
│  │ pipeline     │    │  4. chiede conferma (se non --force) │   │
│  └──────────────┘    │  5. esegue via arksh_shell_execute_  │   │
│                      │     line()                           │   │
│                      └──────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────┘
          │
          ▼ HTTP POST /v1/messages
┌─────────────────────┐
│  API Anthropic       │
│  (claude-sonnet-4-6) │
└─────────────────────┘
```

Il plugin si integra con arksh usando l'ABI plugin formalizzata v5 (`ARKSH_PLUGIN_ABI_MAJOR 5`, `ARKSH_PLUGIN_ABI_MINOR 0`) e registra:

| Punto di estensione          | Nome          | Scopo                                      |
|------------------------------|---------------|--------------------------------------------|
| `register_command`           | `nl`          | Traduce ed esegue un comando NL            |
| `register_command`           | `nl-dry`      | Traduce senza eseguire (solo stampa)       |
| `register_command`           | `nl-config`   | Configura API key, modello, lingua         |
| `register_pipeline_stage`    | `nl_exec`     | Stage pipeline: esegue testo come comando  |

---

## Struttura del repository

```
plugins/
  claude-nl/
    claude_nl_plugin.c      # sorgente principale del plugin
    claude_nl_http.c        # helper HTTP (libcurl o subprocess)
    claude_nl_http.h
    claude_nl_config.c      # lettura/scrittura ~/.arksh/claude-nl.conf
    claude_nl_config.h
    CMakeLists.txt
    README.md
```

---

## Implementazione: `arksh_plugin_query` + `arksh_plugin_init`

```c
#include <stdio.h>
#include <string.h>
#include "arksh/plugin.h"
#include "claude_nl_http.h"
#include "claude_nl_config.h"

#define PLUGIN_NAME    "claude-nl"
#define PLUGIN_VERSION "1.0.0"
#define PLUGIN_DESC    "Traduce linguaggio naturale in comandi shell via Claude"

ARKSH_PLUGIN_EXPORT int arksh_plugin_query(ArkshPluginInfo *out_info)
{
  if (out_info == NULL) {
    return 1;
  }

  memset(out_info, 0, sizeof(*out_info));
  strncpy(out_info->name, PLUGIN_NAME, sizeof(out_info->name) - 1);
  strncpy(out_info->version, PLUGIN_VERSION, sizeof(out_info->version) - 1);
  strncpy(out_info->description, PLUGIN_DESC, sizeof(out_info->description) - 1);
  out_info->abi_major = ARKSH_PLUGIN_ABI_MAJOR;
  out_info->abi_minor = ARKSH_PLUGIN_ABI_MINOR;
  out_info->required_host_capabilities =
      ARKSH_PLUGIN_CAP_COMMANDS | ARKSH_PLUGIN_CAP_PIPELINE_STAGES;
  out_info->plugin_capabilities = out_info->required_host_capabilities;
  return 0;
}

ARKSH_PLUGIN_EXPORT int arksh_plugin_init(
    ArkshShell *shell,
    const ArkshPluginHost *host,
    ArkshPluginInfo *out_info)
{
  if (host->abi_major != ARKSH_PLUGIN_ABI_MAJOR ||
      host->abi_minor < ARKSH_PLUGIN_ABI_MINOR) {
    return 1;  /* ABI incompatibile */
  }
  if (arksh_plugin_query(out_info) != 0) {
    return 1;
  }

  host->register_command(shell, "nl",        "Esegui comando in linguaggio naturale",   cmd_nl);
  host->register_command(shell, "nl-dry",    "Traduce NL senza eseguire",               cmd_nl_dry);
  host->register_command(shell, "nl-config", "Configura il plugin claude-nl",           cmd_nl_config);
  host->register_pipeline_stage(shell, "nl_exec", "Esegui testo come comando shell",   stage_nl_exec);

  return 0;
}
```

---

## Costruzione del prompt di sistema

Il prompt di sistema fornisce a Claude il contesto operativo: directory corrente, sistema operativo,
variabili rilevanti. In questo modo la traduzione è precisa e contestualizzata.

```c
static void build_system_prompt(ArkshShell *shell, char *buf, size_t buf_size) {
  char cwd[512] = {0};
  arksh_shell_get_cwd(shell, cwd, sizeof(cwd));  /* funzione interna del plugin via getcwd() */

  snprintf(buf, buf_size,
    "Sei un assistente per una shell Unix chiamata arksh.\n"
    "Il tuo unico compito è tradurre la richiesta dell'utente in UN SOLO comando shell eseguibile.\n"
    "\n"
    "Contesto:\n"
    "  Directory corrente: %s\n"
    "  Sistema operativo: %s\n"
    "\n"
    "Regole:\n"
    "  - Rispondi SOLO con il comando shell, niente spiegazioni.\n"
    "  - Se la richiesta è ambigua, scegli l'interpretazione più sicura.\n"
    "  - Non usare comandi distruttivi senza flag espliciti nella richiesta.\n"
    "  - Usa percorsi relativi alla directory corrente se non specificato diversamente.\n"
    "  - Il comando deve essere eseguibile in bash/zsh standard.\n",
    cwd,
    ARKSH_PLATFORM_NAME   /* macro definita in platform.h: "macOS", "Linux", "Windows" */
  );
}
```

---

## Chiamata all'API di Claude

Il plugin supporta due strategie di trasporto, selezionabili a compile-time:

### Strategia A: libcurl (default)

Richiede `libcurl` come dipendenza. Più portabile e robusta.

```c
/* claude_nl_http.c — semplificato */
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

typedef struct { char *buf; size_t len; size_t cap; } GrowBuf;

static size_t write_cb(void *data, size_t size, size_t nmemb, void *userdata) {
  GrowBuf *g = userdata;
  size_t n = size * nmemb;
  if (g->len + n + 1 > g->cap) {
    g->cap = (g->len + n + 1) * 2;
    g->buf = realloc(g->buf, g->cap);
  }
  memcpy(g->buf + g->len, data, n);
  g->len += n;
  g->buf[g->len] = '\0';
  return n;
}

int claude_nl_translate(
    const char *api_key,
    const char *model,
    const char *system_prompt,
    const char *user_message,
    char *out_command,
    size_t out_size,
    char *out_error,
    size_t err_size)
{
  CURL *curl = curl_easy_init();
  if (!curl) { snprintf(out_error, err_size, "curl init failed"); return 1; }

  /* Costruisce il payload JSON */
  char payload[4096];
  snprintf(payload, sizeof(payload),
    "{"
      "\"model\":\"%s\","
      "\"max_tokens\":256,"
      "\"system\":\"%s\","  /* N.B.: in produzione usare una libreria JSON per l'escaping */
      "\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]"
    "}",
    model, system_prompt, user_message
  );

  GrowBuf resp = {0};
  struct curl_slist *headers = NULL;
  char auth_header[256];
  snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", api_key);
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
  headers = curl_slist_append(headers, auth_header);

  curl_easy_setopt(curl, CURLOPT_URL, "https://api.anthropic.com/v1/messages");
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);

  CURLcode rc = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (rc != CURLE_OK) {
    snprintf(out_error, err_size, "curl error: %s", curl_easy_strerror(rc));
    free(resp.buf);
    return 1;
  }

  /* Estrae il testo dalla risposta — parsing minimale, in produzione usare cJSON */
  /* Il campo è: {"content":[{"type":"text","text":"<COMANDO>"}],...} */
  const char *needle = "\"text\":\"";
  char *start = strstr(resp.buf, needle);
  if (!start) {
    snprintf(out_error, err_size, "risposta inattesa dall'API");
    free(resp.buf);
    return 1;
  }
  start += strlen(needle);
  char *end = strchr(start, '"');
  if (!end) { free(resp.buf); return 1; }
  size_t cmd_len = (size_t)(end - start);
  if (cmd_len >= out_size) cmd_len = out_size - 1;
  memcpy(out_command, start, cmd_len);
  out_command[cmd_len] = '\0';

  free(resp.buf);
  return 0;
}
```

### Strategia B: subprocess al CLI `claude` (fallback senza libcurl)

```c
/* Usa il binario claude CLI se disponibile nel PATH */
int claude_nl_translate_via_cli(
    const char *model,
    const char *system_prompt,
    const char *user_message,
    char *out_command,
    size_t out_size,
    char *out_error,
    size_t err_size)
{
  char cmd[2048];
  snprintf(cmd, sizeof(cmd),
    "claude -p \"%s\\n\\n%s\" --model %s 2>/dev/null",
    system_prompt, user_message, model
  );
  FILE *fp = popen(cmd, "r");
  if (!fp) { snprintf(out_error, err_size, "impossibile avviare claude CLI"); return 1; }
  size_t n = fread(out_command, 1, out_size - 1, fp);
  pclose(fp);
  out_command[n] = '\0';
  /* Rimuove newline finale */
  if (n > 0 && out_command[n-1] == '\n') out_command[n-1] = '\0';
  return 0;
}
```

---

## Implementazione del comando `nl`

```c
static int cmd_nl(ArkshShell *shell, int argc, char **argv, char *out, size_t out_size) {
  if (argc < 2) {
    snprintf(out, out_size, "uso: nl <richiesta in linguaggio naturale>");
    return 1;
  }

  /* Unisce tutti gli argomenti in una frase */
  char user_input[1024] = {0};
  for (int i = 1; i < argc; i++) {
    if (i > 1) strncat(user_input, " ", sizeof(user_input) - strlen(user_input) - 1);
    strncat(user_input, argv[i], sizeof(user_input) - strlen(user_input) - 1);
  }

  ClaudeNlConfig cfg;
  claude_nl_config_load(&cfg);  /* legge ~/.arksh/claude-nl.conf */

  char system_prompt[1024];
  build_system_prompt(shell, system_prompt, sizeof(system_prompt));

  char translated[512] = {0};
  char err[256] = {0};
  int ret = claude_nl_translate(cfg.api_key, cfg.model,
                                 system_prompt, user_input,
                                 translated, sizeof(translated),
                                 err, sizeof(err));
  if (ret != 0) {
    snprintf(out, out_size, "errore traduzione: %s", err);
    return 1;
  }

  /* Mostra il comando e chiede conferma */
  fprintf(stderr, "\033[1;33m→ %s\033[0m\n", translated);
  if (!cfg.auto_confirm) {
    fprintf(stderr, "Eseguire? [s/N] ");
    int c = fgetc(stdin);
    if (c != 's' && c != 'S') {
      snprintf(out, out_size, "annullato");
      return 0;
    }
  }

  /* Esegue il comando tradotto all'interno della sessione arksh corrente */
  ret = arksh_shell_execute_line(shell, translated, out, out_size);
  return ret;
}
```

---

## Implementazione dello stage pipeline `nl_exec`

Lo stage `nl_exec` consente di usare la traduzione NL all'interno di una pipeline:

```arksh
let prompt = "elenca tutti i file modificati oggi"
prompt |> nl_exec
```

```c
static int stage_nl_exec(
    ArkshShell *shell,
    ArkshValue *value,
    const char *raw_args,
    char *error,
    size_t error_size)
{
  (void) raw_args;

  if (value->kind != ARKSH_VALUE_STRING) {
    snprintf(error, error_size, "nl_exec richiede un valore stringa");
    return 1;
  }

  ClaudeNlConfig cfg;
  claude_nl_config_load(&cfg);

  char system_prompt[1024];
  build_system_prompt(shell, system_prompt, sizeof(system_prompt));

  char translated[512] = {0};
  char err[256] = {0};
  int ret = claude_nl_translate(cfg.api_key, cfg.model,
                                 system_prompt, value->string,
                                 translated, sizeof(translated),
                                 err, sizeof(err));
  if (ret != 0) {
    snprintf(error, error_size, "traduzione fallita: %s", err);
    return 1;
  }

  char exec_out[2048] = {0};
  ret = arksh_shell_execute_line(shell, translated, exec_out, sizeof(exec_out));

  /* Sostituisce il valore pipeline con l'output del comando */
  arksh_value_free(value);
  arksh_value_set_string(value, exec_out);
  return ret;
}
```

---

## Configurazione: `~/.arksh/claude-nl.conf`

```ini
# Chiave API Anthropic (obbligatoria)
api_key = sk-ant-XXXXXXXXXXXXXXXXXXXXXXXXXXXX

# Modello da usare (default: claude-sonnet-4-6)
model = claude-sonnet-4-6

# Conferma automatica senza prompt interattivo (default: false)
auto_confirm = false

# Lingua delle richieste (per disambiguazione nel system prompt)
language = italian
```

Il comando `nl-config` consente di impostare i valori da shell:

```arksh
nl-config set api_key sk-ant-XXXX
nl-config set model claude-haiku-4-5-20251001
nl-config set auto_confirm true
nl-config show
```

---

## Sicurezza e considerazioni operative

### Rischi principali

| Rischio | Mitigazione |
|---------|-------------|
| Comandi distruttivi (`rm -rf`, `dd`) generati per errore | Blocklist di pattern pericolosi, revisione obbligatoria prima dell'esecuzione |
| Prompt injection via nomi di file | Sanitizzare l'input utente prima di inserirlo nel payload JSON |
| Esposizione della API key | Salvata in `~/.arksh/claude-nl.conf` con permessi `0600` |
| Costi API inattesi | Aggiungere contatore di richieste con limite configurabile |
| Latenza di rete | Timeout configurabile su libcurl; messaggio di attesa visibile |

### Pattern di comando bloccati di default

Il plugin rifiuta di eseguire comandi che corrispondono a questi pattern anche dopo la traduzione:

```c
static const char *BLOCKED_PATTERNS[] = {
  "rm -rf /",
  "rm -rf ~",
  "mkfs",
  "dd if=",
  "> /dev/",
  ":(){ :|:& };:",  /* fork bomb */
  NULL
};
```

### Modalità dry-run

```arksh
nl-dry cancella tutti i log più vecchi di 30 giorni
# Output: find /var/log -name "*.log" -mtime +30 -delete
# (nessuna esecuzione)
```

---

## Esempi d'uso

```arksh
# Copia file
nl copia tutti i file .jpg dalla cartella downloads a foto/archivio

# Ricerca
nl trova tutti i file modificati nelle ultime 24 ore in questa directory

# Archivio
nl comprimi la cartella progetto in un file tar.gz con la data di oggi nel nome

# Permessi
nl rendi eseguibili tutti gli script .sh in questa cartella

# Rete
nl mostra quali porte sono in ascolto su questo sistema

# Pipeline: trasforma output in comandi
let query = "elenca i processi che usano più di 100MB di memoria"
query |> nl_exec |> lines |> count -> print()
```

---

## Build e installazione

### CMakeLists.txt (plugin)

```cmake
cmake_minimum_required(VERSION 3.16)
project(claude_nl_plugin C)

find_package(CURL REQUIRED)

add_library(claude_nl_plugin SHARED
  claude_nl_plugin.c
  claude_nl_http.c
  claude_nl_config.c
)

target_include_directories(claude_nl_plugin PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${CURL_INCLUDE_DIRS}
)

target_link_libraries(claude_nl_plugin
  ${CURL_LIBRARIES}
)

set_target_properties(claude_nl_plugin PROPERTIES
  PREFIX ""
  OUTPUT_NAME "claude_nl_plugin"
)

install(TARGETS claude_nl_plugin
  LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/arksh/plugins
)
```

### Compilazione

```bash
# Dalla root del repository arksh:
cmake -S plugins/claude-nl -B build-claude-nl -DCMAKE_PREFIX_PATH=/path/to/arksh/install
cmake --build build-claude-nl
cmake --install build-claude-nl
```

### Caricamento in arksh

```arksh
plugin load /usr/local/lib/arksh/plugins/claude_nl_plugin.dylib
# oppure in ~/.arksh/init.arksh:
plugin load claude_nl_plugin   # se nella directory dei plugin di default
```

---

## Compatibilità piattaforme

| Piattaforma | Trasporto HTTP    | Note                                          |
|-------------|-------------------|-----------------------------------------------|
| macOS       | libcurl (sistema) | Disponibile via Homebrew: `brew install curl` |
| Linux       | libcurl           | `apt install libcurl4-openssl-dev`            |
| Windows     | libcurl (vcpkg)   | `vcpkg install curl`; alternativa: WinHTTP   |

Su tutte le piattaforme è disponibile il fallback via CLI `claude` se il binario è nel `PATH`.

---

## Estensioni future

- **nl-history**: registro delle traduzioni con possibilità di riutilizzo
- **nl-learn**: feedback positivo/negativo per affinare il prompt di sistema
- **Multimodale**: accettare screenshot come input (descrizione visiva → comando)
- **nl-alias**: creare alias arksh da frasi NL (`nl-alias "aggiorna il sistema" -> "sudo apt upgrade"`)
- **Integrazione con variabili arksh**: iniettare il valore delle variabili correnti nel prompt di sistema per traduzioni contestuali (`let dir = "/home/utente/foto" ; nl sposta tutti i .raw in formato jpg in $dir`)
