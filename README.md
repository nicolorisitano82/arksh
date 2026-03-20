# `ARKsh` è una shell object-oriented in C, pensata per Linux, macOS e Windows.

`ARKsh` è una shell object-oriented in C, pensata per Linux, macOS e Windows.

Archetype Shell - Every resource is an object. Every object has a class.

L'idea di base è che ogni elemento del sistema sia un oggetto interrogabile tramite proprietà e metodi:

```text
. -> type
. -> children()
README.md -> read_text(256)
. -> children() |> where(type == "file") |> sort(size desc)
```

## Funzionalità implementate

- REPL interattiva e modalità `-c`
- Modello oggetti per file, directory, device, mount point e path astratti
- Lexer, AST ed executor completi per il linguaggio della shell
- Parser per espressioni `path -> property` e `path -> method(...)`
- Runtime object-aware per stringhe, numeri, booleani, liste e mappe
- Letterali booleani `true` e `false` come value expression di prima classe
- Operatori binari `+`, `-`, `*`, `/`, `==`, `!=`, `<`, `>`, `<=`, `>=` in value expressions
- Operatore ternario `condizione ? valore : valore`
- Block literal Smalltalk-style `[:param | body]` come valori di prima classe, assegnabili con `let`
- Estensioni di oggetti e valori con `extend`, definibili nel linguaggio o da plugin nativi
- Pipeline oggetti `|>` con `where`, `sort`, `take`, `first`, `count`, `render`, `lines`, `trim`, `split`, `join`, `each`, `reduce`, `grep`, `to_json`, `from_json`
- Bridge shell/object: `<comando esterno> |> <stage>` usa stdout come sorgente object pipeline
- Esecuzione nativa di comandi esterni con pipe shell `|`, heredoc `<<` / `<<-` e redirection completa
- Liste di comandi con `;`, `&&`, `||` e background job con `&`
- Controllo di flusso `if`, `elif`, `else`, `while`, `until`, `for`, `break`, `continue`, `return`, ternario `?:`, `switch` e `case`
- Quoting shell con single quote, double quote e backslash
- Espansioni `~`, variabili shell/ambiente `$VAR` / `${VAR}` / `$?`, command substitution `$(...)` e globbing
- Stato shell con `set`, `export`, `unset`, `alias`, `unalias`, `source`, `type`, `eval`, `exec`, `wait`, `trap`
- Funzioni shell dichiarative con parametri named, `return` e scope locale per variabili e binding
- Override di comandi built-in tramite funzioni omonime; `builtin <nome>` per chiamare il built-in originale
- Classi custom con `class ... endclass`, istanziazione, proprietà, metodi ed ereditarietà multipla a precedenza sinistra
- Job control POSIX con process group completi per pipeline foreground: `setpgid`, `tcsetpgrp`, `Ctrl-Z` → `jobs`, `fg` (con cessione del terminale al pgid), `bg`
- Caricamento automatico di `~/.arkshrc` o del file indicato da `ARKSH_RC`
- Editor di riga interattivo con syntax highlighting e autosuggestion da history
- Tab completion contestuale con indicatori di tipo: comandi, funzioni `(fn)`, alias `(@)`, binding `(let)`, variabili `$`, stage pipeline, proprietà e metodi oggetto
- History persistente su file con built-in `history`
- Prompt configurabile con segmenti stile theme engine
- Sistema plugin con ABI C stabile per aggiungere comandi, proprietà, metodi, resolver e stage
- Documentazione architetturale e reference manual completo

## Piattaforme supportate

| Piattaforma | Compilatore | Build type | CI |
|-------------|-------------|------------|----|
| Linux (Ubuntu 22.04+) | gcc ≥ 11 | Debug + Release | Ubuntu Latest |
| macOS (13 Ventura+) | clang ≥ 15 (Xcode) | Debug + Release | macOS Latest |
| Windows (10/11) | MSVC ≥ 19.38 (VS 2022) | Release | Windows Latest |

Requisiti minimi: CMake ≥ 3.20, compilatore C11.

## Build

```bash
cmake -S . -B build
cmake --build build
```

Fallback manuale verificato su macOS:

```bash
mkdir -p build
cc -std=c11 -Wall -Wextra -pedantic -Iinclude \
   src/line_editor.c src/main.c src/executor.c src/expand.c \
   src/lexer.c src/object.c src/parser.c src/platform.c \
   src/plugin.c src/prompt.c src/shell.c -o build/arksh
cc -std=c11 -Wall -Wextra -pedantic -Iinclude -dynamiclib -undefined dynamic_lookup \
   plugins/sample/sample_plugin.c -o build/arksh_sample_plugin.dylib
cc -std=c11 -Wall -Wextra -pedantic -Iinclude -dynamiclib -undefined dynamic_lookup \
   plugins/skeleton/skeleton_plugin.c -o build/arksh_skeleton_plugin.dylib
```

Su Linux sostituire `-dynamiclib -undefined dynamic_lookup` con `-shared -fPIC`; su Windows usare `.dll`.

## Esecuzione rapida

```bash
./build/arksh                                         # REPL interattiva
./build/arksh -c '. -> type'                          # singolo comando
./build/arksh -c '. -> children() |> where(type == "file") |> sort(size desc)'
./build/arksh -c 'list(1, 20, 3) |> sort(value desc)'
./build/arksh -c 'capture("pwd") |> lines() |> first()'
./build/arksh -c 'text(" a, b , c ") |> trim() |> split(",") |> join(" | ")'
./build/arksh -c 'list(1, 2, 3) |> reduce(number(0), [:acc :n | acc + n])'
./build/arksh -c 'list(1, 2, 3) |> to_json()'
./build/arksh -c 'text("{\"a\":[1,2,{\"b\":true}]}") |> from_json() |> to_json()'
./build/arksh -c 'capture_lines("ls /usr") |> grep("lib") |> count()'
./build/arksh -c 'env() -> HOME'
./build/arksh -c 'proc() -> pid'
./build/arksh -c 'shell() -> plugins |> count()'
./build/arksh -c 'let is_file = [:it | it -> type == "file"] ; . -> children() |> where(is_file) |> each([:it | it -> name]) |> take(5)'
./build/arksh -c 'extend directory property child_count = [:it | it -> children() |> count()]'
./build/arksh -c '. -> child_count'
./build/arksh -c 'true && text("ok") -> print()'
./build/arksh -c 'false || text("recovered") -> print()'
./build/arksh -c 'bool(true) ? "yes" : "no"'
./build/arksh -c 'number(3) + number(4)'
./build/arksh -c 'if . -> exists ; then text("yes") -> print() ; else text("no") -> print() ; fi'
./build/arksh -c 'for n in list(1, 2, 3) ; do n -> value ; done'
./build/arksh -c 'switch . -> type ; case "directory" ; then text("dir") -> print() ; default ; then text("other") -> print() ; endswitch'
./build/arksh -c 'eval "text(\"hi\") -> print()"'
./build/arksh -c 'trap "text(\"bye\") -> print()" EXIT ; text("run") -> print()'
./build/arksh -c 'ls -1 | wc -l'
./build/arksh -c 'cat < README.md | wc -l'
./build/arksh -c 'ls missing 2>&1 | wc -l'
./build/arksh -c $'./build/arksh_test_count_lines <<EOF\none\ntwo\nEOF'
./build/arksh -c 'sleep 1 & jobs'
./build/arksh -c 'let payload = map("a", list(1, 2, map("b", true))) ; /tmp/arksh.json -> write_json(payload) ; /tmp/arksh.json -> read_json() |> to_json()'
./build/arksh -c 'plugin load build/arksh_sample_plugin.dylib ; sample() -> name'
./build/arksh -c 'plugin load build/arksh_sample_plugin.dylib ; text("ciao") |> sample_wrap()'
./build/arksh -c 'history'
./build/arksh -c 'type ls'
./build/arksh -c 'prompt load examples/arksh.conf'
```

## Script di esempio

Nel repository sono presenti undici script `.arksh`:

```bash
./build/arksh -c 'source examples/scripts/01-filesystem-tour.arksh'
./build/arksh -c 'source examples/scripts/02-values-blocks-and-extensions.arksh'
./build/arksh -c 'source examples/scripts/03-shell-session.arksh'
./build/arksh -c 'source examples/scripts/04-control-flow.arksh'
./build/arksh -c 'source examples/scripts/05-shell-functions.arksh'
./build/arksh -c 'source examples/scripts/06-classes.arksh'
./build/arksh -c 'source examples/scripts/07-case-and-builtins.arksh'
./build/arksh -c 'source examples/scripts/08-redirections-and-heredoc.arksh'
./build/arksh -c 'source examples/scripts/09-binary-operators.arksh'
./build/arksh -c 'source examples/scripts/10-shell-object-bridge.arksh'
./build/arksh -c 'source examples/scripts/11-command-override.arksh'
```

Coprono:

| Script | Contenuto |
|--------|-----------|
| `01` | Object model filesystem e pipeline `\|>` |
| `02` | Valori tipizzati, `let`, block e `extend` |
| `03` | Stato della shell, prompt, alias, sourcing e controllo di flusso |
| `04` | Scripting con `if`, `while`, `until`, `for`, ternario `?:`, `switch`, `case` |
| `05` | Funzioni shell: definizione, ridefinizione, introspezione e chiamata |
| `06` | Classi custom con `property`, `method`, `init` ed ereditarietà multipla |
| `07` | Built-in di scripting: `eval`, `wait`, `trap EXIT` |
| `08` | Heredoc `<<` / `<<-` e redirection avanzata su file descriptor |
| `09` | Operatori binari `+`, `-`, `*`, `/`, confronto e ternario |
| `10` | Bridge shell/object: output comandi esterni come sorgente `\|>` |
| `11` | Override di built-in tramite funzioni e uso di `builtin` |

## Riferimento rapido di sintassi

```text
# Espressioni oggetto
. -> type
README.md -> size
. -> children()
README.md -> read_text(256)

# Pipeline oggetti
. -> children() |> where(type == "file") |> sort(size desc)
list(1, 20, 3) |> sort(value desc)
capture("pwd") |> lines() |> first()
text(" a, b , c ") |> trim() |> split(",") |> join(" | ")
list(1, 2, 3) |> reduce(number(0), [:acc :n | acc + n])
capture_lines("ls /usr") |> grep("lib")

# Valori tipizzati
text("ciao")
number(42)
bool(true)
true
false
list(1, 2, "tre")
map("chiave", "valore")
capture("cmd")

# Operatori
number(3) + number(4)
text("a") + text("b")
number(5) > number(3)
true ? "sì" : "no"

# Funzioni e classi
function greet(name) do
  text("hello %s") -> print(name)
endfunction
greet nicolo
builtin pwd

# Job control
sleep 5 &
jobs
fg
bg

# Pipeline shell
ls -1 | wc -l
cat < README.md | wc -l
ls > out.txt
ls missing 2>&1 | wc -l

# Variabili e alias
set PROJECT arksh
export PROJECT_ROOT "$PWD"
alias ll="ls -1"
let files = . -> children()
```

## Documentazione

- [manuale-utente.md](docs/manuale-utente.md) — reference manual completo
- [sintassi-arksh.md](docs/sintassi-arksh.md) — specifica della sintassi
- [scelte-implementative.md](docs/scelte-implementative.md) — note architetturali
- [roadmap-shell-completa.md](docs/roadmap-shell-completa.md) — visione a lungo termine
- [backlog-implementazione.md](docs/backlog-implementazione.md) — stato e prossimi passi
- [parser-dispatch.md](docs/parser-dispatch.md) — albero di dispatch del parser

## Startup e configurazione

All'avvio arksh carica in ordine:

1. `ARKSH_RC` se definita nell'ambiente
2. altrimenti `~/.arkshrc`

La history viene salvata in `ARKSH_HISTORY` o in `~/.arksh/history`.

La configurazione del prompt viene cercata in `ARKSH_CONFIG`, poi `arksh.conf` locale, poi `~/.arksh/prompt.conf`.

Esempio di `~/.arkshrc` minimale:

```text
set PROJECT arksh
export PROJECT_ROOT "$PWD"
alias ll="ls -1"
prompt load ~/.arksh/prompt.conf
```

## Plugin

Dopo la build il plugin di esempio è disponibile come libreria dinamica:

- Linux: `build/arksh_sample_plugin.so`
- macOS: `build/arksh_sample_plugin.dylib`
- Windows: `build/arksh_sample_plugin.dll`

Caricamento:

```bash
printf 'plugin load build/arksh_sample_plugin.dylib\nhello-plugin Team\nexit\n' | ./build/arksh
```

Il plugin registra il comando `hello-plugin`, il resolver `sample()`, lo stage `sample_wrap()`, la proprietà `sample_tag` sui directory object e il metodo `sample_label(...)` sugli object filesystem.

```text
plugin list
plugin info sample-plugin
plugin disable sample-plugin
plugin enable sample-plugin
```

Il template per un nuovo plugin è in [plugins/skeleton](plugins/skeleton).

## Configurazione prompt

```ini
theme=aurora
left=userhost,cwd,plugins
right=status,os,date,time
separator= ::
use_color=1
color.userhost=green
color.cwd=cyan
color.status=yellow
color.date=blue
color.time=yellow
```

```bash
./build/arksh -c 'prompt load examples/arksh.conf'
```
