# Manuale Utente di ARKsh

Versione di riferimento: stato attuale del repository (marzo 2026)

Versione inglese disponibile in [user-manual.md](user-manual.md).

## 1. Cos'è ARKsh

ARKsh è una shell interattiva e linguaggio di scripting scritto in C. Mantiene i flussi classici delle shell Unix per l'esecuzione dei comandi, ma aggiunge un runtime tipizzato e object-aware.

Gli obiettivi principali sono:

- mantenere utilizzabili i workflow shell tradizionali
- rendere interrogabili come oggetti file, directory e risorse di sistema
- trattare valori, liste, mappe, blocchi e output dei comandi come valori di prima classe
- permettere estensioni tramite classi, `extend` e plugin nativi

Esempi immediati:

```text
. -> type
README.md -> read_text(128)
. -> children() |> where(type == "file") |> sort(size desc)
capture("ls /usr") |> lines() |> grep("lib") |> count()
```

## 2. Compilazione e avvio

Compilazione base con CMake:

```bash
cmake -S . -B build
cmake --build build
```

Avvio interattivo:

```bash
./build/arksh
```

Esecuzione di un singolo comando:

```bash
./build/arksh -c '. -> type'
```

Esecuzione di uno script:

```bash
./build/arksh mio_script.arksh
```

Esecuzione di un file nel contesto della shell corrente:

```bash
./build/arksh -c 'source examples/scripts/03-shell-session.arksh'
```

Esecuzione dei test:

```bash
ctest --test-dir build --output-on-failure
```

Benchmark prestazionali ripetibili:

```bash
cmake --build build --target arksh_perf
```

## 3. Modello mentale

ARKsh ha due modalità complementari.

### 3.1 Comandi shell classici

Questi girano come comandi normali o built-in:

```bash
ls -la
grep main src/file.c
echo hello
```

Usa le pipe shell (`|`) quando vuoi stream testuali.

### 3.2 Espressioni di valore tipizzate

Queste producono valori invece di lanciare processi:

```text
text("hello")
number(42)
bool(true)
list(1, 2, 3)
map("name", "arksh")
```

Usa le pipeline oggetto (`|>`) quando vuoi trasformazioni strutturate.

### 3.3 Oggetti filesystem

Un path può essere usato come receiver:

```text
. -> type
. -> children()
README.md -> size
README.md -> parent()
```

### 3.4 Profilazione leggera e benchmark

ARKsh include un piccolo sistema di telemetria utile per il lavoro su CPU, allocazioni e memoria.

Comandi disponibili:

```text
perf show
perf status
perf on
perf off
perf reset
```

Esempi:

```bash
./build/arksh -c 'perf show'
./build/arksh -c 'perf on ; perf reset ; . -> children() |> count() ; perf show'
ARKSH_PERF=1 ./build/arksh -c '. -> children() |> where(type == "file") |> sort(size desc)'
```

I workload benchmark ripetibili vivono in `tests/perf/`, e il target CMake `arksh_perf` li esegue in sequenza.

Nel bundle c'è anche `tests/perf/object-chain.arksh`, dedicato alle chain annidate con `->` e alle chiamate oggetto concatenate.

La baseline iniziale è documentata in [benchmarks-baseline.md](benchmarks-baseline.md).

## 4. Sintassi di base

### 4.1 Sintassi receiver -> membro

Forma generale:

```text
receiver -> property
receiver -> method(arg1, arg2)
```

Esempi:

```text
. -> type
README.md -> read_text(64)
README.md -> permissions
README.md -> chmod("640") -> permissions
tests/fixtures/json/nested.json -> read_json() -> get_path("a[2].b")
```

Le chain top-level con `->` vengono parse-ate come un'unica espressione strutturata, quindi forme come `file -> read_json() -> get_path(...)` non richiedono un `let` intermedio per restare efficienti.

### 4.2 Costruttori di valore

Costruttori comuni:

```text
text("hello")
number(42)
bool(false)
list(1, 2, 3)
map("name", "arksh")
Dict()
Matrix("name", "score")
capture("pwd")
capture_lines("ls -1")
```

### 4.3 Block literal

I block sono valori di prima classe:

```text
[:it | it -> name]
[:acc :n | acc + n]
```

Possono essere salvati in binding tipizzati:

```text
let is_file = [:it | it -> type == "file"]
let get_name = [:it | it -> name]
```

### 4.4 Variabili locali nei block

Usa `local` per creare binding locali al block:

```text
[:acc :n | local next = acc + n ; next]
```

### 4.5 Operatori

Operatori supportati nelle value expression:

```text
+  -  *  /
== != < > <= >=
condition ? true_value : false_value
```

### 4.6 Variabili shell speciali e array associativi

Sono disponibili anche:

```text
text("%s") -> print("$LINENO")
text("%s") -> print("$FUNCNAME")
text("%s") -> print("$BASH_SOURCE")

declare -A colors
colors[sky]=blue
text("%s") -> print("${colors[sky]}")
text("%s") -> print("${!colors[@]}")
```

`declare -A` e `typeset -A` usano internamente i `Dict`: `${name[key]}` legge una entry, `${name[@]}` espande i valori, `${!name[@]}` espande le chiavi.

Esempi:

```text
number(3) + number(4)
number(5) > number(3)
bool(true) ? "yes" : "no"
```

### 4.6 Flag di modalità shell

ARKsh supporta i flag di esecuzione shell più comuni:

```text
set -e
set -u
set -x
set -o pipefail
set +e
set +u
set +x
set +o pipefail
```

Note pratiche:

- `set -e` interrompe l'esecuzione dopo un comando non-condizionale che fallisce
- `set -u` genera errore su espansioni `$VAR` / `${VAR}` non definite, tranne le forme con default come `${VAR:-fallback}`
- `set -x` stampa il comando già espanso su `stderr`, con prefisso `$PS4`
- `set -o pipefail` fa fallire una pipeline shell se fallisce uno qualunque degli stage

Esempio:

```text
set PS4 "TRACE: "
set -x
set -o pipefail
/usr/bin/true | /usr/bin/false | /usr/bin/true
```

Sui runtime POSIX, `stty` e disponibile anche come builtin passthrough:

```text
stty -a
stty echo
stty -echo
```

Il runtime interattivo mantiene uno snapshot centralizzato dello stato TTY pre-raw, quindi se la REPL termina per un segnale anomalo supportato mentre il line editor e in raw mode, il terminale viene ripristinato prima dell'uscita del processo.

## 5. Pipeline

### 5.1 Pipeline oggetti

Le pipeline oggetti usano `|>`.

Esempi:

```text
. -> children() |> where(type == "file") |> sort(size desc)
list(1, 20, 3) |> sort(value desc)
text(" a, b , c ") |> trim() |> split(",") |> join(" | ")
list(1, 2, 3) |> reduce(number(0), [:acc :n | acc + n])
tests/fixtures/json/nested.json -> read_json() -> get_path("a[2].b") |> render()
```

Stage comuni:

- `where(...)`
- `filter(...)`
- `sort(...)`
- `take(n)`
- `first()`
- `count()`
- `sum()`
- `min()`
- `max()`
- `lines()`
- `trim()`
- `split(sep)`
- `join(sep)`
- `grep(pattern)`
- `each(...)`
- `map(...)`
- `flat_map(...)`
- `group_by(...)`
- `reduce(init, block)`
- `render()`
- `to_json()`
- `from_json()`

### 5.2 Pipeline shell

Le pipeline shell usano `|` e lavorano su stream di testo:

```bash
ls -1 | wc -l
cat < README.md | wc -l
ls missing 2>&1 | wc -l
./arksh_test_echo_stdin <<< "hello"
read line <<< "$HOME"
wc -l <(printf "a\nb\n")
printf hi > >(wc -c)
```

`<<<` e una here-string: espande l'argomento, aggiunge un newline finale e lo passa allo `stdin` del comando. E utile sia con comandi esterni sia con built-in che leggono da input, come `read`.

Su runtime POSIX puoi usare anche la sostituzione di processo:
- `<(cmd)` espone l'output di `cmd` come path leggibile
- `>(cmd)` espone un path scrivibile che alimenta lo `stdin` di `cmd`

Per condizioni shell estese puoi usare anche `[[ ... ]]`:

```text
set s demo.txt
[[ "$s" == *.txt ]] && text("match") -> print()
[[ "$s" =~ ^demo\\.[a-z]+$ ]] && text("$BASH_REMATCH") -> print()
```

In sessione interattiva POSIX, `arksh` aggiorna anche i metadati del terminale quando riceve `SIGWINCH`:

```text
shell() -> tty_cols
shell() -> tty_rows
proc() -> tty_cols
proc() -> tty_rows
```

### 5.3 Bridge tra comandi e valori tipizzati

Usa:

```text
capture("cmd")
capture_lines("cmd")
```

Esempi:

```text
capture("pwd")
capture("ls -1") |> lines() |> first()
capture_lines("ls /usr") |> grep("lib") |> count()
```

## 6. Controllo di flusso

### 6.1 if / elif / else / fi

```text
if . -> exists ; then
  text("yes") -> print()
else
  text("no") -> print()
fi
```

### 6.2 while e until

```text
while false ; do
  text("loop") -> print()
done

until true ; do
  text("retry") -> print()
done
```

### 6.3 for

```text
for n in list(1, 2, 3) ; do
  n -> value
done
```

### 6.4 switch

```text
switch . -> type
case "directory"
then
text("dir") -> print()
default
then
text("other") -> print()
endswitch
```

### 6.5 case in stile shell

```text
case text("demo.txt") in
*.md) text("md") -> print() ;;
*.txt) text("txt") -> print() ;;
esac
```

### 6.6 break, continue, return

Si comportano come atteso dentro loop e funzioni:

```text
break
continue
return text("done")
```

## 7. Funzioni

Definizione:

```text
function greet(name) do
  text("hello %s") -> print(name)
endfunction
```

Chiamata shell-style:

```text
greet nicolo
```

Uso di `local` nelle funzioni:

```text
function demo(name) do
  local prefix=hello
  text("%s %s") -> print("$prefix", name)
endfunction
```

Fuori da una funzione shell, `local` fallisce con `local: not in a function`.

Uso di `builtin` per bypassare un override:

```text
builtin pwd
```

## 8. Classi ed estensioni runtime

### 8.1 Classi

```text
class Named do
  property name = text("unnamed")
endclass

class Document extends Named do
  method init = [:self :name | self -> set("name", name)]
endclass
```

Istanziazione:

```text
let doc = Document(text("manual"))
doc -> name
```

### 8.2 Ereditarietà multipla

ARKsh supporta ereditarietà multipla con precedenza sinistra:

```text
class Artifact extends Named, Printable do
  ...
endclass
```

### 8.3 `extend`

Puoi aggiungere proprietà e metodi a receiver built-in:

```text
extend directory property child_count = [:it | it -> children() |> count()]
extend object method label = [:it :prefix | prefix]
```

## 9. Plugin

Caricamento plugin:

```text
plugin load build/arksh_sample_plugin.dylib
```

Ispezione e gestione:

```text
plugin list
plugin info sample-plugin
plugin disable sample-plugin
plugin enable sample-plugin
plugin autoload list
```

Il sample plugin aggiunge un comando, un resolver, uno stage, una proprietà e un metodo. `plugin info`
mostra anche ABI e capability, mentre `plugin list` mostra ABI e capability offerte da ogni plugin caricato.
Il template base è in `plugins/skeleton`.

## 10. Prompt e startup

Directory standard utente:

- config: `ARKSH_CONFIG_HOME`, altrimenti `XDG_CONFIG_HOME/arksh`, altrimenti `~/.config/arksh`
- cache: `ARKSH_CACHE_HOME`, altrimenti `XDG_CACHE_HOME/arksh`, altrimenti `~/.cache/arksh`
- state: `ARKSH_STATE_HOME`, altrimenti `XDG_STATE_HOME/arksh`, altrimenti `~/.local/state/arksh`
- plugin: `ARKSH_PLUGIN_HOME`, altrimenti `XDG_DATA_HOME/arksh/plugins`, altrimenti `~/.local/share/arksh/plugins`

ARKsh carica lo stato iniziale in questo ordine:

1. `ARKSH_RC`, se definita
2. altrimenti `${ARKSH_CONFIG_HOME}/arkshrc` o la config dir standard risolta
3. fallback legacy: `~/.arkshrc`

History:

- `ARKSH_HISTORY`
- altrimenti `${ARKSH_STATE_HOME}/history` o la state dir standard risolta
- fallback legacy: `~/.arksh/history`

Ricerca config prompt:

1. `ARKSH_CONFIG`
2. `arksh.conf` locale
3. `${ARKSH_CONFIG_HOME}/prompt.conf` o la config dir standard risolta
4. fallback legacy: `~/.arksh/prompt.conf`

Prompt di default:

```text
user@host | /percorso/corrente >
```

Esempio di config prompt:

```ini
theme=aurora
left=userhost,cwd,plugins
right=status,os,date,time
separator= ::
use_color=1
```

Opzioni disponibili per `left` e `right`:

- `user` — utente corrente
- `host` — hostname della macchina
- `userhost` — combinazione `user@host`
- `cwd` — directory corrente
- `status` — stato dell'ultimo comando, `ok` oppure `err:N`
- `os` — nome del sistema operativo
- `plugins` — numero di plugin caricati
- `date` — data corrente in formato `YYYY-MM-DD`
- `time` — ora corrente in formato `HH:MM:SS`
- `datetime` — data e ora in formato `YYYY-MM-DD HH:MM:SS`
- `theme` — nome del tema prompt attivo

Nota pratica:

- `left` e `right` accettano gli stessi segmenti, separati da virgole
- puoi usare anche resolver senza argomenti che restituiscono testo, per esempio `git` dal plugin Git prompt
- se un segmento restituisce stringa vuota, viene semplicemente omesso dal prompt

Config prompt con Git:

```ini
theme=default
left=userhost,cwd,git,plugins
right=status,os,date,time
plugin=git-prompt-plugin
color.git=yellow
```

Segnalini stato Git:

- `=` branch pulito
- `*` modifiche o file non tracciati
- `^` branch avanti rispetto all'upstream
- `v` branch indietro rispetto all'upstream
- `~` avanti e indietro insieme
- `!` conflitti
- `:` detached HEAD

Installazione locale:

```bash
cmake --install build
```

Caricamento:

```text
prompt load examples/arksh.conf
prompt load examples/arksh-git.conf
prompt render
```

## 11. Funzionalità interattive

L'editor interattivo include:

- history persistente
- syntax highlighting
- autosuggestion dalla history
- completion contestuale
- completion dei membri oggetto dopo `->`
- completion di stage e resolver

Esempi:

```text
README.md -> <Tab>
. -> children() |> so<Tab>
plugin <Tab>
```

## 12. Built-in utili

Built-in usati più spesso:

- `help`
- `pwd`
- `cd`
- `type`
- `history`
- `set`
- `export`
- `unset`
- `alias`
- `unalias`
- `source`
- `eval`
- `exec`
- `trap`
- `jobs`
- `fg`
- `bg`
- `wait`
- `read`
- `printf`
- `test`
- `[[`
- `let`
- `extend`
- `plugin`
- `prompt`

Uso rapido:

```text
help
help commands
help resolvers
help stages
help types
```

## 13. JSON e dati strutturati

Serializzazione e parse:

```text
map("a", list(1, 2, map("b", true))) |> to_json()
text("{\"a\":[1,2,{\"b\":true}]}") |> from_json()
```

Helper file-oriented su receiver path-like:

```text
data.json -> read_json()
data.json -> write_json(payload)
data.json -> read_json() -> get_path("a[2].b")
let data = data.json -> read_json()
data -> set_path("meta.version", number(2))
list(map("profile", map("name", "alpha")), map("profile", map("name", "beta"))) |> pluck("profile.name")
```

La forma diretta e quella consigliata quando serve una query una tantum:

```text
data.json -> read_json() -> get_path("meta.version")
```

Usa `let` intermedio solo se vuoi riutilizzare piu volte il valore JSON gia parse-ato nello stesso script.

Query e trasformazioni utili:

- `get_path("a[2].b")` legge path annidati con segmenti `.` e indici `[n]`
- `has_path(...)` verifica l'esistenza di un path senza fallire
- `set_path(path, value)` restituisce una copia aggiornata del valore
- `pick("k1", "k2")` estrae solo alcune chiavi da `map` o `dict`
- `merge(other)` combina due `map` o `dict`, con override dei valori del secondo
- `pluck("profile.name")` proietta un campo annidato da ogni elemento di una lista

## 14. Diagnostica e problemi comuni

Problemi tipici:

- `unknown property`: il receiver non espone quella proprietà
- `unknown method`: il receiver non implementa quel metodo
- `... expects a list`: uno stage è stato applicato al tipo sbagliato
- `job not found`: non esiste un job attivo con quel `%n`
- `unable to open source file`: path non risolto o permessi insufficienti

Diagnostica utile:

```text
type ls
plugin list
help sort
help Matrix
```

## 15. Cheat Sheet

```text
. -> type
. -> children() |> where(type == "file") |> sort(size desc)
text(" a, b ") |> trim() |> split(",") |> join(" | ")
list(1, 2, 3) |> reduce(number(0), [:acc :n | acc + n])
capture_lines("ls -1") |> grep("md") |> count()

set PROJECT arksh
export PROJECT_ROOT "$PWD"
alias ll="ls -1"

function greet(name) do
  text("hello %s") -> print(name)
endfunction

class Named do
  property name = text("unnamed")
endclass

extend directory property child_count = [:it | it -> children() |> count()]

sleep 5 &
jobs
fg
```

## 16. Documenti collegati

- [user-manual.md](user-manual.md)
- [sintassi-arksh.md](sintassi-arksh.md)
- [scelte-implementative.md](scelte-implementative.md)
