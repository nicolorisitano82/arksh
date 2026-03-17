# oosh

`oosh` e uno skeleton in C per una shell object-oriented, pensata per Linux, macOS e Windows.

L'idea di base e che ogni elemento del sistema sia un oggetto interrogabile tramite proprieta e metodi:

```text
. -> type
. -> children()
README.md -> read_text(256)
```

Lo stato attuale del repository implementa un MVP compilabile con:

- REPL interattiva e modalita `-c`
- modello oggetti per file, directory, device, mount point e path astratti
- lexer, AST ed executor base per il linguaggio della shell
- parser per espressioni `path -> property` e `path -> method(...)`
- runtime object-aware anche per stringhe, numeri, booleani e liste
- block literal Smalltalk-style come valori di prima classe, assegnabili con `let`
- estensioni di oggetti e valori, definibili nel linguaggio con `extend` o da plugin nativi
- pipeline oggetti con `where`, `sort`, `take`, `first`, `count`, `render`, `lines` e `each`
- esecuzione nativa di comandi esterni con pipe shell, heredoc e redirection
- liste di comandi con `;`, `&&`, `||` e background jobs con `&`
- controllo di flusso base con `if`, `while`, `until`, `for`, `break`, `continue`, `return`, ternario `?:`, `switch` e `case`
- quoting shell con single quote, double quote e backslash
- espansioni base con `~`, variabili shell/ambiente, `$(...)` e globbing
- stato shell con `set`, `export`, `unset`, `alias`, `unalias`, `source`, `type`, `eval`, `exec`, `wait`, `trap`
- funzioni shell dichiarative con parametri named, `return` e scope locale per variabili e binding
- classi custom con `class ... endclass`, istanziazione, proprieta, metodi ed ereditarieta multipla a precedenza sinistra
- job control POSIX migliorato con `jobs`, `fg`, `bg`, `true`, `false`, resume dei job stoppati e `Ctrl-Z` sui job foreground
- caricamento automatico di `~/.ooshrc` o di un file indicato da `OOSH_RC`
- editor riga interattivo con frecce, `Tab`, `Ctrl-A`, `Ctrl-E`
- REPL e `source` con blocchi multilinea per `if` / `while` / `until` / `for` / `switch`
- REPL e `source` con blocchi multilinea anche per `function ... endfunction`
- completion contestuale dei membri oggetto dopo `->`
- history persistente su file con built-in `history`
- prompt configurabile con segmenti stile theme engine
- sistema plugin con ABI C stabile per aggiungere comandi
- documentazione architetturale e proposta di sintassi

## Build

```bash
cmake -S . -B build
cmake --build build
```

Fallback manuale verificato su macOS:

```bash
mkdir -p build
cc -std=c11 -Wall -Wextra -pedantic -Iinclude src/line_editor.c src/main.c src/executor.c src/expand.c src/lexer.c src/object.c src/parser.c src/platform.c src/plugin.c src/prompt.c src/shell.c -o build/oosh
cc -std=c11 -Wall -Wextra -pedantic -Iinclude -dynamiclib -undefined dynamic_lookup plugins/sample/sample_plugin.c -o build/oosh_sample_plugin.dylib
cc -std=c11 -Wall -Wextra -pedantic -Iinclude -dynamiclib -undefined dynamic_lookup plugins/skeleton/skeleton_plugin.c -o build/oosh_skeleton_plugin.dylib
```

## Esecuzione

```bash
./build/oosh
./build/oosh -c '. -> type'
./build/oosh -c 'inspect .'
./build/oosh -c '. -> children() |> where(type == "file") |> sort(size desc)'
./build/oosh -c '. -> children() |> each(name) |> take(5)'
./build/oosh -c 'list(1, 20, 3) |> sort(value desc)'
./build/oosh -c 'capture("pwd")'
./build/oosh -c 'capture("pwd") |> lines() |> first()'
./build/oosh -c 'text(" a, b , c ") |> trim() |> split(",") |> join(" | ")'
./build/oosh -c 'list(1, 2, 3) |> reduce(number(0), [:acc :n | acc + n])'
./build/oosh -c 'list(1, 2, 3) |> to_json()'
./build/oosh -c 'text("{\"a\":[1,2,{\"b\":true}]}") |> from_json() |> to_json()'
./build/oosh -c 'let payload = map("a", list(1, 2, map("b", true))) ; /tmp/oosh.json -> write_json(payload) ; /tmp/oosh.json -> read_json() |> to_json()'
./build/oosh -c 'env() -> HOME'
./build/oosh -c 'proc() -> pid'
./build/oosh -c 'shell() -> plugins |> count()'
./build/oosh -c 'let is_file = [:it | it -> type == "file"] ; . -> children() |> where(is_file) |> each([:it | it -> name]) |> take(5)'
./build/oosh -c 'extend directory property child_count = [:it | it -> children() |> count()]'
./build/oosh -c '. -> child_count'
./build/oosh -c 'plugin load build/oosh_sample_plugin.dylib ; sample() -> name'
./build/oosh -c 'plugin load build/oosh_sample_plugin.dylib ; text("ciao") |> sample_wrap()'
./build/oosh -c 'ls -1 | wc -l'
./build/oosh -c 'cat < README.md | wc -l'
./build/oosh -c 'ls missing 2>&1 | wc -l'
./build/oosh -c $'./build/oosh_test_count_lines <<EOF\none\ntwo\nEOF'
./build/oosh -c './build/oosh_test_emit_args hello 3> fd3.out 1>&3 ; fd3.out -> read_text(64)'
./build/oosh -c 'true && text("ok") -> print()'
./build/oosh -c 'false || text("recovered") -> print()'
./build/oosh -c 'bool(true) ? "yes" : "no"'
./build/oosh -c 'if . -> exists ; then text("yes") -> print() ; else text("no") -> print() ; fi'
./build/oosh -c 'for n in list(1, 2, 3) ; do n -> value ; done'
./build/oosh -c 'until true ; do text("retry") -> print() ; done'
./build/oosh -c 'case text("demo.txt") in *.md) text("md") -> print() ;; *.txt) text("txt") -> print() ;; esac'
./build/oosh -c 'switch . -> type ; case "directory" ; then text("dir") -> print() ; default ; then text("other") -> print() ; endswitch'
./build/oosh -c 'eval "text(\"hi\") -> print()"'
./build/oosh -c 'trap "text(\"bye\") -> print()" EXIT ; text("run") -> print()'
./build/oosh -c 'source examples/scripts/06-classes.oosh'
./build/oosh -c 'source examples/scripts/07-case-and-builtins.oosh'
./build/oosh -c 'source examples/scripts/08-redirections-and-heredoc.oosh'
./build/oosh -c 'sleep 1 & jobs'
./build/oosh -c 'text("%s") -> print("$HOME")'
./build/oosh -c 'text("%s") -> print($(pwd))'
./build/oosh -c 'history'
./build/oosh -c 'type ls'
./build/oosh -c 'source examples/scripts/05-shell-functions.oosh'
./build/oosh -c 'prompt load examples/oosh.conf'
```

## Script di esempio

Nel repository trovi otto script `.oosh` gia pronti da leggere o caricare con `source`:

```bash
./build/oosh -c 'source examples/scripts/01-filesystem-tour.oosh'
./build/oosh -c 'source examples/scripts/02-values-blocks-and-extensions.oosh'
./build/oosh -c 'source examples/scripts/03-shell-session.oosh'
./build/oosh -c 'source examples/scripts/04-control-flow.oosh'
./build/oosh -c 'source examples/scripts/05-shell-functions.oosh'
./build/oosh -c 'source examples/scripts/06-classes.oosh'
./build/oosh -c 'source examples/scripts/07-case-and-builtins.oosh'
./build/oosh -c 'source examples/scripts/08-redirections-and-heredoc.oosh'
```

Coprono:

- object model filesystem e pipeline `|>`
- valori tipizzati, `let`, block e `extend`
- stato della shell, prompt, alias, sourcing e controllo di flusso
- scripting base con `if`, `while`, `until`, `for`, ternario `?:`, `switch`, `case` e blocchi multilinea
- funzioni shell con definizione, ridefinizione, introspezione e chiamata
- classi custom con `property`, `method`, `init`, `classes` e ereditarieta multipla
- built-in shell di scripting come `eval`, `wait` e `trap EXIT`
- heredoc `<<` / `<<-` e redirection avanzata su file descriptor

## Documentazione

- [manuale-utente.md](docs/manuale-utente.md)
- [sintassi-oosh.md](docs/sintassi-oosh.md)
- [scelte-implementative.md](docs/scelte-implementative.md)
- [roadmap-shell-completa.md](docs/roadmap-shell-completa.md)
- [backlog-implementazione.md](docs/backlog-implementazione.md)

Note rapide:

- `|>` resta la pipeline object-aware
- `text(...)`, `number(...)`, `bool(...)`, `list(...)`, `capture(...)` e `capture_lines(...)` producono valori object-aware
- `let nome = espressione` crea binding tipizzati per oggetti, liste, stringhe, numeri e block
- `function nome(param1, param2) do ... endfunction` definisce funzioni shell chiamabili come `nome valore1 valore2`
- `class Nome extends Base1, Base2 do ... endclass` definisce classi custom con ereditarieta multipla
- le classi si istanziano come value resolver, per esempio `Documento(text("readme"))`
- le istanze espongono proprieta con `-> nome`, mutazione con `-> set("nome", valore)` e test di tipo con `-> isa("Base")`
- i block usano sintassi `[:arg | body]` e sono oggetti interrogabili con `-> type`, `-> arity`, `-> source`
- dentro i block puoi usare `local nome = espressione ; espressione_finale` per creare binding tipizzati limitati allo scope del block
- `extend <target> property ... = <block>` e `extend <target> method ... = <block>` aggiungono membri custom
- `|`, `<`, `>`, `>>`, `2>`, `2>&1`, `<<`, `<<-`, `3>`, `n>&m` e `n<&m` sono la pipeline shell testuale
- `;`, `&&` e `||` concatenano comandi con semantica shell classica
- `if`, `while`, `until`, `for`, `switch` e `case` usano sintassi shell classica; sulla singola riga serve `;` prima di `then` e `do`, tra i branch di `switch` e tra i branch `case ... ;;`
- `case ... in ... esac` usa pattern shell con `*`, `?`, `[]` e alternative separate da `|`
- nei file `source`, la forma oggi piu stabile per `case` resta quella single-line
- `eval` riesegue una riga costruita dinamicamente nel contesto shell corrente
- `exec` esegue un comando esterno e poi termina la shell con lo stesso status
- `wait` aspetta la fine del job piu recente o di uno specifico come `wait %1`
- `trap` supporta per ora il trap minimo `EXIT`
- le funzioni shell usano parametri named, ricevono argomenti shell testuali e ripristinano `vars` e `bindings` al termine della chiamata
- `&` lancia un comando in background
- `jobs` mostra stato e pid dei job
- `fg` porta in foreground il job piu recente o uno specifico come `fg %2`
- `bg` riprende un job stoppato
- sui build POSIX, `Ctrl-Z` mentre un job e in `fg` lo ferma e lo rimette nella job table
- `where(...)` accetta sia `where(type == "file")` sia `where(block)`
- `each(...)` accetta sia la forma breve `each(name)` sia `each(block)`
- `lines()` divide una stringa in una lista di stringhe
- `trim()`, `split(...)`, `join(...)` e `reduce(...)` estendono le pipeline su stringhe e liste
- `local` dentro i block fa shadowing dei binding esterni e li ripristina a fine valutazione
- `map(...)`, `env()`, `proc()` e `shell()` introducono namespace e valori strutturati non filesystem
- `to_json()` serializza un valore come JSON e `from_json()` lo rilegge da una stringa JSON, incluse mappe e annidamenti
- i file supportano `read_json()` e `write_json(binding)` per round-trip JSON typed
- `print()` e un metodo disponibile su ogni valore; sulle stringhe funziona come un formatter in stile `printf`
- i plugin possono aggiungere comandi, proprieta, metodi, value resolver e stage di pipeline
- `~`, `$VAR`, `${VAR}` e `$(...)` vengono espansi
- le variabili shell locali hanno precedenza sull'ambiente nelle espansioni
- `*`, `?` e `[]` vengono espansi come glob nei normali argomenti comando
- le pipeline shell multi-stage e il globbing degli argomenti comando passano dallo stesso layer cross-platform anche sul build Windows
- heredoc e supportato anche nei file caricati con `source`
- i fd custom oltre `0/1/2` sono verificati sui build POSIX; su Windows il supporto resta limitato ai fd standard
- heredoc e fd custom oggi sono supportati sui comandi esterni; i built-in restano sul set semplice di redirection
- i built-in funzionano da soli o con redirection singola, ma non ancora dentro pipeline shell multi-stage

## Stato shell e startup

`oosh` supporta ora un primo runtime da shell tradizionale:

```text
set PROJECT oosh
export PROJECT_ROOT "$PWD"
alias ll="ls -1"
let files = . -> children()
let is_file = [:it | it -> type == "file"]
let get_name = [:it | it -> name]
function greet(name) do
  text("hello %s") -> print(name)
endfunction
files |> where(is_file) |> each(get_name) |> take(5)
extend directory property child_count = [:it | it -> children() |> count()]
extend object method echo = [:it :value | value]
extend
history
function
functions greet
sleep 5 &
jobs
fg
bg
plugin list
plugin disable sample-plugin
plugin enable sample-plugin
type ll
source examples/ooshrc
```

All'avvio la shell prova a caricare:

1. il file indicato da `OOSH_RC`, se presente
2. altrimenti `~/.ooshrc`

Nel repository c'e anche un esempio minimale in [examples/ooshrc](examples/ooshrc).

In REPL sono ora disponibili anche:

- freccia su/giu per navigare la history
- freccia sinistra/destra per muovere il cursore
- `Tab` per completion base di built-in, plugin e path
- `Tab` dopo `->` per vedere proprieta e metodi dell'oggetto corrente

Per esempio:

```text
README.md -> <Tab>
README.md -> par<Tab>
let entry = . -> children() |> first()
entry -> <Tab>
```
- `Ctrl-A` e `Ctrl-E` per andare a inizio/fine riga

La history viene salvata in `~/.oosh/history`, oppure nel path indicato da `OOSH_HISTORY`.

## Plugin di esempio

Dopo la build, il plugin di esempio e disponibile come libreria dinamica:

- Linux: `build/oosh_sample_plugin.so`
- macOS: `build/oosh_sample_plugin.dylib`
- Windows: `build/oosh_sample_plugin.dll`

Caricamento:

```bash
printf 'plugin load build/oosh_sample_plugin.dylib\nhello-plugin Team\nexit\n' | ./build/oosh
```

Il plugin di esempio registra anche:

- `sample_tag` come proprieta sui directory object
- `sample_label(...)` come metodo sugli object del filesystem

Il comando `plugin` supporta ora anche:

- `plugin list` per nome, versione, descrizione e stato runtime
- `plugin info <name|path>` per il dettaglio di un plugin caricato
- `plugin disable <name|path>` per spegnere comandi ed estensioni del plugin
- `plugin enable <name|path>` per riattivarli senza ricaricare la libreria

Nel repository trovi anche un template copiabile in [plugins/skeleton](plugins/skeleton), con:

- comando stub
- proprieta custom stub
- metodo custom stub
- README dedicato con build e uso

Su Linux e Windows va usata l'estensione del sistema operativo corrispondente.

## Configurazione prompt

Esempio di file:

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

Il file puo essere caricato con:

```bash
./build/oosh -c 'prompt load examples/oosh.conf'
```
