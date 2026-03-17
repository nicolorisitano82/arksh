# Skeleton Plugin per oosh

Questa cartella contiene un plugin-template pensato per essere copiato e adattato.

## Cosa mostra

- registrazione di un comando plugin
- registrazione di una proprieta custom
- registrazione di un metodo custom
- registrazione di un value resolver
- registrazione di uno stage di pipeline
- struttura minima di `oosh_plugin_init(...)`

## File principale

- `plugins/skeleton/skeleton_plugin.c`

## Build manuale

macOS:

```bash
cc -std=c11 -Wall -Wextra -pedantic -Iinclude -dynamiclib -undefined dynamic_lookup plugins/skeleton/skeleton_plugin.c -o build/oosh_skeleton_plugin.dylib
```

Linux:

```bash
cc -std=c11 -Wall -Wextra -pedantic -Iinclude -shared -fPIC plugins/skeleton/skeleton_plugin.c -o build/oosh_skeleton_plugin.so
```

Windows:

```bash
cc -std=c11 -Wall -Wextra -pedantic -Iinclude -shared plugins/skeleton/skeleton_plugin.c -o build/oosh_skeleton_plugin.dll
```

## Uso previsto

1. copia la cartella `plugins/skeleton`
2. rinomina file, simboli e metadati del plugin
3. sostituisci gli stub con logica reale
4. carica il plugin con `plugin load ...`

## Membri registrati dallo skeleton

- comando: `skeleton-info`
- resolver: `skeleton_namespace()`
- stage: `skeleton_stage(...)`
- proprieta: `skeleton_badge`
- metodo: `skeleton_action(...)`

## Esempi di utilizzo

```text
plugin load build/oosh_skeleton_plugin.dylib
skeleton-info
skeleton-info roadmap
skeleton_namespace()
. -> skeleton_badge
text("ciao") |> skeleton_stage(todo)
let action = text("preview")
README.md -> skeleton_action(action)
```
