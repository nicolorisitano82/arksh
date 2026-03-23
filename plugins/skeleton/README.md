# Skeleton Plugin per arksh

Questa cartella contiene un plugin-template pensato per essere copiato e adattato.

## Cosa mostra

- registrazione di un comando plugin
- registrazione di una proprieta custom
- registrazione di un metodo custom
- registrazione di un value resolver
- registrazione di uno stage di pipeline
- struttura minima di `arksh_plugin_init(...)`

## File principale

- `plugins/skeleton/skeleton_plugin.c`

## Build manuale

macOS:

```bash
cc -std=c11 -Wall -Wextra -pedantic -Iinclude -dynamiclib -undefined dynamic_lookup plugins/skeleton/skeleton_plugin.c -o build/arksh_skeleton_plugin.dylib
```

Linux:

```bash
cc -std=c11 -Wall -Wextra -pedantic -Iinclude -shared -fPIC plugins/skeleton/skeleton_plugin.c -o build/arksh_skeleton_plugin.so
```

Windows:

```bash
cc -std=c11 -Wall -Wextra -pedantic -Iinclude -shared plugins/skeleton/skeleton_plugin.c -o build/arksh_skeleton_plugin.dll
```

## Uso previsto

1. copia la cartella `plugins/skeleton`
2. rinomina file, simboli e metadati del plugin
3. sostituisci gli stub con logica reale
4. carica il plugin con `plugin load ...`

## API per plugin author (v5)

### Versione ABI

L'ABI corrente e `ARKSH_PLUGIN_ABI_MAJOR 5` / `ARKSH_PLUGIN_ABI_MINOR 0`.
Ogni plugin dovrebbe esportare sia `arksh_plugin_query(...)` sia `arksh_plugin_init(...)`:

```c
ARKSH_PLUGIN_EXPORT int arksh_plugin_query(ArkshPluginInfo *out_info) {
  memset(out_info, 0, sizeof(*out_info));
  strncpy(out_info->name, "my-plugin", sizeof(out_info->name) - 1);
  out_info->abi_major = ARKSH_PLUGIN_ABI_MAJOR;
  out_info->abi_minor = ARKSH_PLUGIN_ABI_MINOR;
  out_info->required_host_capabilities =
      ARKSH_PLUGIN_CAP_COMMANDS | ARKSH_PLUGIN_CAP_VALUE_RESOLVERS;
  out_info->plugin_capabilities = out_info->required_host_capabilities;
  return 0;
}

if (host->abi_major != ARKSH_PLUGIN_ABI_MAJOR ||
    host->abi_minor < ARKSH_PLUGIN_ABI_MINOR) { return 1; }
```

`plugin info ...` e `plugin list` mostrano ABI e capability negoziate.

### Registrare un value resolver

```c
host->register_value_resolver(shell, "myns", "breve descrizione (max 160 car.)", my_resolver_fn);
```

La descrizione appare in `help resolvers` e in `help myns`. Limitarla a una frase.

### Registrare uno stage di pipeline

```c
host->register_pipeline_stage(shell, "my_stage", "breve descrizione", my_stage_fn);
```

Firma del callback:

```c
int my_stage_fn(ArkshShell *shell, ArkshValue *value,
                const char *raw_args, char *error, size_t error_size);
```

- `value` è il valore in ingresso; modificarlo in-place o liberarlo con `arksh_value_free` e riassegnare.
- `raw_args` è la stringa grezza degli argomenti dello stage (es. `"3"` per `|> my_stage(3)`).
- Restituire 0 per successo, 1 per errore (scrivere il messaggio in `error`).

### Registrare un tipo custom

```c
host->register_type_descriptor(shell, "MyType", "breve descrizione del tipo");
```

Dopo la registrazione il tipo appare in `help types`. Per creare istanze di quel tipo:

```c
arksh_value_set_typed_map(value, "MyType");
```

Le extension di proprietà e metodi registrate con `target = "MyType"` saranno disponibili
automaticamente su qualunque valore con `__type__ == "MyType"`.

### Introspezione a runtime

Un plugin può leggere i metadati registrati dagli altri plugin tramite i campi pubblici di `ArkshShell`:

```c
for (i = 0; i < shell->pipeline_stage_count; i++) {
    printf("%s: %s\n", shell->pipeline_stages[i].name,
                       shell->pipeline_stages[i].description);
}
```

Stessa struttura per `shell->value_resolvers[]` e `shell->type_descriptors[]`.

## Membri registrati dallo skeleton

- comando: `skeleton-info`
- resolver: `skeleton_namespace()`
- stage: `skeleton_stage(...)`
- proprieta: `skeleton_badge`
- metodo: `skeleton_action(...)`

## Esempi di utilizzo

```text
plugin load build/arksh_skeleton_plugin.dylib
skeleton-info
skeleton-info roadmap
skeleton_namespace()
. -> skeleton_badge
text("ciao") |> skeleton_stage(todo)
let action = text("preview")
README.md -> skeleton_action(action)
```
