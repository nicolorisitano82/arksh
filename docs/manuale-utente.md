# Manuale Utente di oosh

## 1. Cos'e oosh

`oosh` e una shell object-oriented.

L'idea di base e semplice:

- file, directory, device e mount point vengono trattati come oggetti
- stringhe, numeri, booleani e liste sono valori object-aware di prima classe
- ogni oggetto ha proprieta, come `type`, `path`, `size`
- ogni oggetto ha metodi, come `children()`, `read_text()`, `parent()`
- le pipeline passano oggetti, valori e liste, non solo testo

Esempi:

```text
. -> type
. -> children()
README.md -> read_text(256)
. -> children() |> where(type == "file")
list(1, 20, 3) |> sort(value desc)
capture("pwd") |> lines() |> first()
```

## 2. Avvio rapido

### 2.1 Build

Con `cmake`:

```bash
cmake -S . -B build
cmake --build build
```

Build manuale verificata su macOS:

```bash
mkdir -p build
cc -std=c11 -Wall -Wextra -pedantic -Iinclude src/line_editor.c src/main.c src/executor.c src/expand.c src/lexer.c src/object.c src/parser.c src/platform.c src/plugin.c src/prompt.c src/shell.c -o build/oosh
cc -std=c11 -Wall -Wextra -pedantic -Iinclude -dynamiclib -undefined dynamic_lookup plugins/sample/sample_plugin.c -o build/oosh_sample_plugin.dylib
```

Su Linux e Windows la build del plugin va adattata all'estensione del sistema:

- Linux: `.so`
- macOS: `.dylib`
- Windows: `.dll`

### 2.2 Avvio interattivo

```bash
./build/oosh
```

### 2.3 Eseguire un solo comando

```bash
./build/oosh -c '. -> type'
```

### 2.4 Script di esempio

Nel repository ci sono otto script `.oosh` pronti da usare:

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

## 3. Modello mentale

Quando usi `oosh`, puoi pensare in tre modi:

1. comandi tradizionali, come `pwd` o `inspect .`
2. espressioni oggetto, come `. -> type`
3. pipeline oggetti, come `. -> children() |> where(type == "file")`
4. pipeline shell tradizionali, come `ls -1 | wc -l`
5. valori object-aware, come `text("ciao")`, `number(42)`, `list(1, 2, 3)` o `map("a", 1)`
6. namespace di sistema, come `env()`, `proc()` e `shell()`

La forma piu importante e questa:

```text
selettore -> membro
```

Il `membro` puo essere:

- una proprieta: `. -> type`
- un metodo: `. -> children()`

La sintassi storica `obj("...").membro` resta supportata, ma non e piu quella consigliata.

## 4. Comandi disponibili

### `help`

Mostra i comandi e alcuni esempi di sintassi.

```text
help
```

### `exit` e `quit`

Terminano la shell.

```text
exit
quit
```

### `pwd`

Mostra la directory corrente.

```text
pwd
```

### `cd`

Cambia la directory corrente.

```text
cd src
cd ..
cd /tmp
cd -
```

Se non passi un argomento, `oosh` prova a usare `HOME`.
Con `cd -` prova a usare `OLDPWD`.

### `set`, `export` e `unset`

Gestiscono le variabili della shell.

```text
set PROJECT oosh
set LIMIT=128
set
export PROJECT_ROOT "$PWD"
export PATH
unset LIMIT
```

Regole pratiche:

- `set` crea o aggiorna una variabile locale della shell
- `export` marca una variabile per i processi figli
- `unset` la rimuove dal runtime di `oosh` e dall'ambiente esportato
- nelle espansioni, una variabile locale di `oosh` ha precedenza su una omonima dell'ambiente

### `let`

Crea binding tipizzati per valori object-aware: oggetti filesystem, stringhe, numeri, booleani, liste e block.

```text
let files = . -> children()
let is_file = [:it | it -> type == "file"]
let get_name = [:it | it -> name]
let
type is_file
is_file -> arity
files |> where(is_file) |> each(get_name) |> take(5)
```

Regole pratiche:

- `let` e separato da `set`: `set` resta per variabili shell testuali, `let` per valori semantici
- il lato destro puo essere una value expression, una object expression, una object pipeline oppure un block literal
- `let` senza argomenti mostra i binding tipizzati attivi
- `unset nome` rimuove sia una variabile shell sia un eventuale binding tipizzato con lo stesso nome

### `function` e `functions`

Definiscono e ispezionano funzioni shell dichiarative.

Sintassi:

```text
function <nome>(<parametri...>) do
...
endfunction
```

Esempi:

```text
function greet(name) do
text("hello %s") -> print(name)
endfunction

function pair(left, right) do
text("%s:%s") -> print(left, right)
endfunction

greet nicolo
pair shell demo
function
functions greet
type greet
```

Regole pratiche:

- i parametri sono named e si dichiarano tra parentesi, separati da virgole
- la chiamata e shell-style, quindi `greet nicolo` e non `greet("nicolo")`
- ogni parametro viene esposto come variabile shell testuale e come binding tipizzato stringa
- al termine della funzione, `vars` e `bindings` tornano allo stato precedente
- ridefinire una funzione con lo stesso nome sostituisce la definizione precedente
- il valore di ritorno della funzione, per ora, coincide con status e output dell'ultimo comando eseguito nel body

### `class` e `classes`

Definiscono e ispezionano classi custom con proprieta, metodi, istanziazione e ereditarieta multipla.

Sintassi:

```text
class <Nome> [extends <Base1>, <Base2>, ...] do
...
endclass
```

Esempi:

```text
class Named do
property name = text("unnamed")
method rename = [:self :next | self -> set("name", next)]
endclass

class Document extends Named do
property kind = text("doc")
method init = [:self :name | self -> set("name", name)]
method label = [:self | text("%s:%s") -> print(self -> kind, self -> name)]
endclass

let doc = Document(text("manuale"))
doc -> label()
doc -> isa("Named")
class
classes Document
type Document
```

Regole pratiche:

- il corpo classe accetta `property nome = espressione` e `method nome = block`
- l'istanziazione usa la stessa sintassi dei value resolver, per esempio `Document()` oppure `Document(text("manuale"))`
- se esiste un metodo `init`, viene invocato automaticamente sul nuovo oggetto
- dentro i metodi, il receiver arriva tipicamente come primo parametro, per esempio `:self`
- `self -> set("campo", valore)` aggiorna una proprieta dell'istanza e restituisce l'istanza stessa
- `istanza -> isa("Base")` verifica ereditarieta diretta o indiretta
- la risoluzione di proprieta e metodi cerca prima nella classe corrente e poi nelle basi da sinistra verso destra
- in caso di conflitto tra basi multiple, la base piu a sinistra ha precedenza su quelle a destra

### `extend`

Aggiunge proprieta e metodi custom a valori e oggetti.

Sintassi:

```text
extend <target> property <nome> = <block>
extend <target> method <nome> = <block>
extend
```

Esempi:

```text
extend directory property child_count = [:it | it -> children() |> count()]
extend object method label = [:it :prefix | prefix]

. -> child_count
README.md -> label("doc")
extend
```

Regole pratiche:

- per una `property` il block deve avere esattamente un parametro: il receiver
- per un `method` il primo parametro e il receiver, gli altri sono gli argomenti del metodo
- i target supportati nell'MVP sono `any`, `string`, `number`, `bool`, `object`, `block`, `list`, `path`, `file`, `directory`, `device`, `mount`
- il core della shell mantiene precedenza; l'estensione entra in gioco quando una proprieta o un metodo non esistono gia nel runtime base

### `alias` e `unalias`

Definiscono alias testuali in stile shell tradizionale.

```text
alias ll="ls -1"
alias gs="git status"
alias
type ll
unalias gs
```

Gli alias vengono espansi all'inizio della linea e dopo ogni pipeline shell `|`.
Non vengono applicati alle espressioni object-aware come `. -> children()`.

### `source` e `.`

Eseguono un file di comandi nel contesto della shell corrente.

```text
source examples/ooshrc
. examples/ooshrc
```

Questo e il modo giusto per caricare:

- alias
- variabili shell
- export
- configurazioni condivise tra sessioni
- blocchi multilinea con `if`, `while`, `until`, `for` e `switch`
- blocchi multilinea con `function ... endfunction`
- heredoc shell-style come `<<EOF ... EOF`

All'avvio `oosh` prova a caricare automaticamente:

1. il file indicato da `OOSH_RC`, se la variabile ambiente e presente
2. altrimenti `~/.ooshrc`

### `if`, `while`, `until`, `for`, `break`, `continue`, `return`, ternario, `switch` e `case`

`oosh` supporta ora un primo blocco di scripting shell-style.

Forme single-line:

```text
if true ; then text("ok") -> print() ; fi
if bool(false) ; then text("no") -> print() ; else text("yes") -> print() ; fi
bool(true) ? "yes" : "no"
while false ; do text("no") -> print() ; done
while true ; do text("tick") -> print() ; break ; done
until true ; do text("retry") -> print() ; done
for n in list(1, 2, 3) ; do n -> value ; done
for n in list(1, 2, 3) ; do n -> print() ; continue ; done
for name in a b c ; do text("%s") -> print($name) ; done
case text("demo.txt") in *.md) text("md") -> print() ;; *.txt) text("txt") -> print() ;; *) text("other") -> print() ;; esac
switch . -> type ; case "directory" ; then text("dir") -> print() ; default ; then text("other") -> print() ; endswitch

function greet(name) do
return text("hi")
endfunction
```

Forme multilinea, comode in REPL e nei file `source`:

```text
if . -> exists
then
text("yes") -> print()
else
text("no") -> print()
fi

for entry in . -> children() |> take(3)
do
entry -> name
done

until false
do
text("retry") -> print()
break
done

switch . -> type
case "directory"
then
text("dir") -> print()
default
then
text("other") -> print()
endswitch
```

Regole pratiche:

- sulla singola riga usa `;` prima di `then` e `do`, tra i branch di `switch` e tra i branch `case ... ;;`
- su piu righe il newline e sufficiente
- `condizione ? vero : falso` produce un valore e puo essere usato ovunque sia ammessa una value expression
- `if`, `while` e `until` provano prima a valutare una value expression e ne usano la truthiness
- se la condizione non e una value expression valida, viene eseguita come comando shell classico e si usa il suo exit status
- `break [count]` esce dal loop corrente o da piu loop annidati
- `continue [count]` salta all'iterazione successiva del loop corrente o di un loop esterno
- `for` accetta sia sorgenti typed, come `list(...)` o `. -> children()`, sia shell words classiche come `a b c`
- la variabile del `for` esiste sia come binding tipizzato sia come variabile shell testuale, quindi puoi usare sia `entry -> name` sia `$entry`
- `return [value-expression]` termina la funzione shell corrente; il valore opzionale diventa l'output della chiamata
- `switch` confronta il valore della expression con i `case` usando il rendering dei valori; `default` e opzionale ma, se presente, deve essere l'ultimo branch
- `case ... in ... esac` fa pattern matching shell-style con `*`, `?`, `[]` e alternative separate da `|`
- nei file `source`, la forma oggi piu stabile per `case` resta quella single-line

### `eval`, `exec`, `wait` e `trap`

Completano il primo set di built-in di scripting piu vicini alla shell classica.

```text
eval "text(\"hi\") -> print()"
sleep 1 & wait %1
trap "text(\"bye\") -> print()" EXIT
```

Regole pratiche:

- `eval "..."` riesegue la stringa nel contesto shell corrente, quindi vede alias, variabili, binding, funzioni e classi gia caricati
- `exec cmd ...` esegue un comando esterno e poi termina la shell con lo stesso status finale
- `wait` aspetta il job piu recente; `wait %1` aspetta un job specifico
- `trap` supporta per ora il caso minimo `EXIT`
- `trap - EXIT` rimuove il trap registrato

### `plugin`

Gestisce i plugin caricati a runtime.

```text
plugin list
plugin info sample-plugin
plugin disable sample-plugin
plugin enable sample-plugin
```

Regole pratiche:

- `plugin list` mostra nome, versione, descrizione, path e stato `enabled/disabled`
- `plugin disable ...` spegne i comandi e le estensioni registrate da quel plugin senza scaricare la libreria
- `plugin enable ...` le riattiva
- `plugin info ...` stampa il dettaglio completo di un plugin caricato

### `type`

Mostra come `oosh` risolve un nome.

```text
type help
type ll
type ls
```

L'output puo indicare se il nome e:

- un alias
- un built-in della shell
- un comando plugin
- un eseguibile trovato nel `PATH`

### `history`

Mostra la history della sessione interattiva.

```text
history
```

La history viene salvata automaticamente in:

1. `OOSH_HISTORY`, se la variabile ambiente e impostata
2. altrimenti `~/.oosh/history`

### `jobs`, `fg` e `bg`

Gestiscono i background job della shell.

```text
sleep 5 &
jobs
fg
bg
```

Stato attuale dell'MVP:

- `&` lancia un comando o una pipeline in background
- `jobs` mostra i job in esecuzione, stoppati o gia completati, con pid
- `fg` porta in foreground il job piu recente, o un job specifico con `fg 1` oppure `fg %1`
- `bg` riprende un job stoppato e lo rimette in esecuzione in background
- sui build POSIX, `Ctrl-Z` mentre un job e in `fg` lo ferma e lo lascia visibile in `jobs`

Nota pratica:

- il job control migliorato vale oggi soprattutto per i job lanciati con `&` e poi gestiti con `jobs` / `fg` / `bg`
- la parita completa con `zsh` per tutti i foreground pipeline diretti resta un passo successivo

### `true` e `false`

Sono built-in minimi utili per composizione e scripting base.

```text
true
false
true && text("ok") -> print()
false || text("recovered") -> print()
```

### `inspect`

Stampa una descrizione completa di un oggetto del file system.

```text
inspect .
inspect README.md
inspect /tmp
```

Esempio di output:

```text
type=file
path=/Users/nicolo/Desktop/oosh/README.md
name=README.md
exists=true
size=2099
hidden=false
readable=true
writable=true
```

### `get`

Legge una proprieta specifica di un oggetto.

```text
get README.md size
get . type
get . path
```

### `call`

Invoca un metodo su un oggetto.

```text
call . children
call README.md read_text 128
call README.md parent
```

### `prompt`

Gestisce la configurazione del prompt.

```text
prompt show
prompt load examples/oosh.conf
```

### `plugin`

Carica plugin dinamici e mostra quelli gia caricati.

```text
plugin list
plugin load build/oosh_sample_plugin.dylib
```

### `run`

Esegue un comando esterno in modo nativo.

```text
run ls
run git status
```

Oggi puoi anche lanciare direttamente comandi esterni senza `run`:

```text
ls
git status
```

`run` resta utile come alias esplicito quando vuoi distinguere un comando esterno da un built-in o da un'espressione oggetto.

### Quoting ed espansioni

`oosh` supporta ora:

- single quote: testo letterale
- double quote: testo con espansioni
- backslash: escape del carattere successivo
- espansione `~`
- espansione variabili shell/ambiente: `$VAR`, `${VAR}`, `$?`
- command substitution: `$(...)`
- globbing base su argomenti comando: `*`, `?`, `[]`

Esempi:

```text
cd ~
text("%s") -> print("$HOME")
text("%s") -> print('$HOME')
text("%s") -> print(hello\ world)
text("%s") -> print($(pwd))
ls *.md
"$PWD/README.md" -> type
README.md -> read_text($LIMIT)
```

Regole pratiche:

- nelle single quote non viene espanso nulla
- nelle double quote vengono espansi variabili e `$(...)`, ma non il globbing
- il globbing si applica solo agli argomenti comando normali, non ai selettori oggetto e non ai target di redirection
- i selettori object-aware supportano `~`, variabili e command substitution
- se una variabile esiste sia in `oosh` sia nell'ambiente, vince quella definita nella shell

### Liste di comandi e controllo di flusso base

`oosh` supporta ora anche gli operatori shell classici:

```text
pwd ; ls
true && text("ok") -> print()
false || text("fallback") -> print()
sleep 5 & jobs
```

Regole pratiche:

- `cmd1 ; cmd2`: esegue sempre `cmd2` dopo `cmd1`
- `cmd1 && cmd2`: esegue `cmd2` solo se `cmd1` ha successo
- `cmd1 || cmd2`: esegue `cmd2` solo se `cmd1` fallisce
- `cmd &`: lancia `cmd` in background
- su POSIX puoi poi usare `fg`, `bg` e `Ctrl-Z` sui job lanciati con `&`

## 4.1 Editing interattivo

In REPL `oosh` supporta ora un editor riga di base:

- freccia su e giu: navigazione history
- freccia sinistra e destra: movimento del cursore
- `Backspace`: cancellazione a sinistra
- `Tab`: completion base di built-in, alias, plugin e path
- `Tab` dopo `->`: mostra proprieta e metodi disponibili per l'oggetto corrente
- `Ctrl-A`: inizio riga
- `Ctrl-E`: fine riga
- `Ctrl-C`: interrompe il comando foreground o cancella la riga corrente
- `Ctrl-Z`: su POSIX ferma un job in `fg` e lo rimette nella job table
- `Ctrl-D`: esce dalla shell se la riga e vuota

## 5. Espressioni oggetto

### 5.1 Sintassi base

```text
path -> property
path -> method()
path -> method(arg1)
"My File.txt" -> size
```

### 5.2 Proprieta supportate

Le proprieta base attualmente disponibili sono:

- `type`
- `path`
- `name`
- `exists`
- `size`
- `hidden`
- `readable`
- `writable`

Esempi:

```text
. -> type
. -> path
README.md -> size
. -> exists
```

### 5.3 Tipi oggetto principali

I tipi principali oggi sono:

- `file`
- `directory`
- `device`
- `mount`
- `path`
- `unknown`

### 5.4 Metodi supportati

#### `children()`

Disponibile per directory e mount point.

```text
. -> children()
```

Restituisce i figli dell'oggetto.

Fuori da una pipeline produce una vista testuale.
Dentro una pipeline produce una lista di oggetti.

#### `read_text(limit)`

Disponibile per file.

```text
README.md -> read_text(128)
```

Legge fino a `limit` byte di testo dal file.

#### `read_json()`

Disponibile per file JSON o path file-like.

```text
data.json -> read_json()
```

Legge il file, lo interpreta come JSON e restituisce un valore tipizzato.
Supporta scalari JSON, array annidati e object JSON annidati.

#### `write_json(binding)`

Disponibile per file e path file-like.

```text
let payload = list(1, 2, 3)
data.json -> write_json(payload)
```

Serializza il valore passato in JSON e lo scrive sul file di destinazione.

#### `print(...)`

Disponibile per ogni valore object-aware.

```text
text("ciao") -> print()
text("%s:%d") -> print("file", 3)
. -> print()
```

Se il receiver e una stringa, `print(...)` usa quella stringa come formato stile `printf`.
Se il receiver non e una stringa, `print()` senza argomenti rende il valore e lo restituisce come output finale della riga.

#### `parent()`

Disponibile per ogni path risolto.

```text
README.md -> parent()
. -> parent()
```

Fuori da una pipeline restituisce il path padre come testo.
Dentro una pipeline restituisce un oggetto.

#### `describe()`

Stampa una descrizione completa dell'oggetto.

```text
. -> describe()
```

## 6. Pipeline oggetti

## 6.1 Concetto

Le pipeline usano l'operatore:

```text
|>
```

Esempio:

```text
. -> children() |> where(type == "file") |> sort(size desc)
list(1, 20, 3) |> sort(value desc)
capture("pwd") |> lines() |> first()
text(" a, b , c ") |> trim() |> split(",") |> join(" | ")
list(1, 2, 3) |> reduce(number(0), [:acc :n | acc + n])
```

La pipeline:

1. parte da un valore object-aware o da un'espressione oggetto
2. produce un valore intermedio
3. applica trasformazioni
4. renderizza il risultato finale

## 6.2 Regole importanti

- la pipeline puo iniziare con un'espressione oggetto oppure con costruttori come `text(...)`, `number(...)`, `bool(...)`, `list(...)`, `capture(...)`, `capture_lines(...)`
- `where`, `sort`, `take`, `first` ed `each` lavorano su liste
- `count()` funziona su valori singoli, liste e mappe
- `render()` forza la conversione in testo
- `lines()` divide una stringa in una lista di stringhe
- `trim()` pulisce spazi iniziali e finali
- `split()` e `join()` convertono tra stringhe e liste
- `reduce()` combina una lista in un singolo valore usando un block
- `to_json()` serializza un valore come JSON
- `from_json()` interpreta una stringa come JSON
- dopo `render()`, gli stage che si aspettano liste o oggetti non hanno piu senso

## 6.3 Sorgenti di valore supportate

```text
text("ciao")
number(42)
bool(true)
list(1, 2, "tre")
capture("pwd")
capture_lines("ls -1")
env()
proc()
shell()
```

Note pratiche:

- `text(...)` e `string(...)` sono equivalenti
- `array(...)` e un alias di `list(...)`
- `capture(...)` restituisce una stringa con l'output del comando
- `capture_lines(...)` restituisce una lista di stringhe, una per riga
- `env()` restituisce una mappa con l'ambiente effettivo della shell e `env("PATH")` legge una singola chiave
- `proc()` restituisce una mappa sul processo corrente, per esempio `pid`, `ppid`, `cwd`, `host`, `os`
- `shell()` restituisce una mappa con stato runtime, alias, binding, plugin e job
- dentro liste eterogenee puoi usare `type` o `value_type`; sugli item filesystem `type` continua a significare `file`, `directory`, `device`, ...

## 6.3.1 Block literal

I block usano una sintassi ispirata a Smalltalk:

```text
[:it | it -> name]
[:it | it -> type == "file"]
[:n | local next = n + 1 ; next]
```

Sono valori di prima classe:

- possono essere assegnati con `let`
- possono essere passati a `where(...)` ed `each(...)`
- possono essere interrogati come oggetti con proprieta come `type`, `arity`, `source`, `body`

Esempi:

```text
let is_file = [:it | it -> type == "file"]
let get_name = [:it | it -> name]
let sum_step = [:acc :n | local next = acc + n ; next]
is_file -> type
is_file -> source
. -> children() |> where(is_file) |> each(get_name)
list(1, 2, 3) |> reduce(number(0), sum_step)
```

Scope locale nei block:

- `local nome = espressione ; espressione_finale` crea un binding tipizzato visibile solo dentro il block corrente
- i `local` vengono valutati in ordine, quindi un `local` successivo puo usare quelli precedenti
- un `local` puo fare shadowing di un binding esterno o di un parametro del block
- a fine valutazione il binding precedente viene ripristinato automaticamente

## 6.4 Stage supportati

### `where(property == value)`

Filtra una lista.

```text
. -> children() |> where(type == "file")
. -> children() |> where(name == "README.md")
```

Limitazione attuale:

- supporta solo il confronto `==`

Forma aggiuntiva supportata:

```text
let is_file = [:it | it -> type == "file"]
. -> children() |> where(is_file)
. -> children() |> where([:it | it -> type == "file"])
```

### `sort(property asc|desc)`

Ordina una lista per proprieta.

```text
. -> children() |> sort(name asc)
. -> children() |> sort(size desc)
```

Se la proprieta e numerica, come `size`, l'ordinamento e numerico.
Negli altri casi e lessicografico.

### `take(n)`

Mantiene solo i primi `n` elementi.

```text
. -> children() |> take(5)
. -> children() |> where(type == "file") |> sort(size desc) |> take(3)
```

### `first()`

Estrae il primo elemento di una lista e lo trasforma in valore singolo.

```text
. -> children() |> first()
. -> children() |> where(type == "file") |> sort(size desc) |> first()
```

Se la lista e vuota, il comando fallisce.

### `count()`

Conta gli elementi.

```text
. -> children() |> count()
. -> children() |> where(type == "file") |> count()
```

Su un valore singolo restituisce `1`.

### `lines()`

Divide una stringa in una lista di stringhe.

```text
capture("pwd") |> lines()
capture("printf 'one\\ntwo\\n'") |> lines() |> count()
```

### `trim()`

Rimuove spazi iniziali e finali da una stringa.
Se applicato a una lista, rifinisce ogni elemento stringa.

```text
text("  ciao  ") |> trim()
capture("printf '  one  '") |> trim()
```

### `split(separator?)`

Divide una stringa in una lista.

```text
text("a,b,c") |> split(",")
text("uno due tre") |> split()
```

Se ometti il separatore, `split()` usa whitespace.

### `join(separator?)`

Unisce una lista in una stringa.

```text
list("a", "b", "c") |> join("-")
. -> children() |> each(name) |> join(", ")
```

Se ometti il separatore, `join()` concatena senza separatore.

### `reduce(block)` e `reduce(init, block)`

Riduce una lista a un singolo valore.

```text
list(1, 2, 3) |> reduce(number(0), [:acc :n | acc + n])
list("a", "b", "c") |> reduce(text(""), [:acc :it | acc + it])
```

Senza valore iniziale, il primo elemento della lista diventa l'accumulatore iniziale.

### `from_json()`

Interpreta una stringa JSON e la converte in valore tipizzato.

```text
text("{\"a\": [1, 2, {\"b\": true}]}") |> from_json() |> to_json()
```

### `to_json()`

Serializza il valore corrente in una stringa JSON.

```text
list(1, 2, 3) |> to_json()
map("a", list(1, 2, map("b", true))) |> to_json()
text("ciao") |> to_json()
```

### `each(selector)`

Applica una trasformazione a ogni elemento della lista.

```text
. -> children() |> each(name)
. -> children() |> each(type) |> take(5)
. -> children() |> where(type == "file") |> each(parent()) |> first()
capture_lines("printf 'one\\ntwo\\n'") |> each(length)
list(1, 2, "tre") |> each(render())
```

Forme supportate:

- `each(name)` per leggere una proprieta
- `each(parent())` per chiamare un metodo sugli oggetti filesystem
- `each(render())` per forzare la resa testuale di ogni elemento
- `each(binding)` per usare un block assegnato con `let`
- `each([:it | ...])` per usare un block inline

Le proprieta lette dentro `where`, `sort` ed `each` possono arrivare anche da `extend`, non solo dal core built-in.

### `render()`

Converte il valore corrente in testo esplicito.

```text
README.md -> parent() |> render()
. -> children() |> where(type == "file") |> render()
```

Di default il risultato finale di una pipeline viene comunque renderizzato, ma `render()` e utile quando vuoi forzare il passaggio a testo in uno stage intermedio o rendere piu esplicita l'intenzione.

## 7. Pipeline shell e redirection

## 7.1 Concetto

`oosh` ora supporta anche la pipeline shell classica per i processi esterni.

Operatori disponibili:

- `|`
- `<`
- `>`
- `>>`
- `2>`
- `2>&1`
- `<<`
- `<<-`
- `3>`
- `n>&m`
- `n<&m`

Regola pratica:

- usa `|>` quando vuoi trasformare oggetti
- usa `|` quando vuoi collegare processi testuali classici

## 7.2 Esempi

```text
ls -1 | wc -l
cat < README.md | wc -l
ls > out.txt
printf hello >> out.txt
ls missing 2> err.txt
ls missing 2>&1 | wc -l
./oosh_test_count_lines <<EOF
one
two
EOF
./oosh_test_echo_stdin <<-EOF
	one
	two
EOF
./oosh_test_emit_args hello 3> fd3.out 1>&3
./oosh_test_count_lines 3< fdin.txt 0<&3
```

## 7.3 Note operative

- per i comandi esterni la shell usa esecuzione nativa, non `system()`
- l'exit status finale segue l'ultimo processo della pipeline
- se l'ultimo comando non redirige `stdout`, `oosh` ne cattura il testo e lo mostra
- `stderr` resta sul terminale, a meno che tu usi `2>` o `2>&1`
- `<<EOF ... EOF` invia il testo al `stdin` del comando come heredoc
- `<<-EOF ... EOF` fa la stessa cosa ma rimuove i tab iniziali da ogni riga del body
- i redirection su fd arbitrari, come `3>` e `0<&3`, sono verificati oggi sui build POSIX
- su Windows il supporto avanzato resta limitato ai fd standard `0`, `1` e `2`
- heredoc e fd custom oggi sono pensati per i comandi esterni; i built-in restano sul supporto di redirection semplice

## 7.4 Limite attuale dei built-in

I built-in di `oosh`, come `help` o `prompt`, oggi possono:

- essere eseguiti normalmente
- essere rediretti su file, per esempio `help > help.txt`

Non possono ancora stare dentro una pipeline shell multi-stage:

```text
help | wc -l
```

Questo oggi produce un errore esplicito.

## 8. Esempi pratici

### Elencare solo i file della directory corrente

```text
. -> children() |> where(type == "file")
```

### Ordinare i file per dimensione decrescente

```text
. -> children() |> where(type == "file") |> sort(size desc)
```

### Prendere i tre file piu grandi

```text
. -> children() |> where(type == "file") |> sort(size desc) |> take(3)
```

### Ottenere il file piu grande

```text
. -> children() |> where(type == "file") |> sort(size desc) |> first()
```

### Contare quanti elementi ci sono

```text
. -> children() |> count()
```

## 9. Prompt

Il prompt e configurabile tramite file `key=value`.

Esempio:

```ini
theme=aurora
left=userhost,cwd,plugins
right=status,os,date,time
separator= :: 
use_color=1
color.userhost=green
color.cwd=cyan
color.status=yellow
color.os=magenta
color.date=blue
color.time=yellow
```

Caricamento:

```text
prompt load examples/oosh.conf
```

Visualizzazione della configurazione attiva:

```text
prompt show
```

### Segmenti supportati

I segmenti base disponibili sono:

- `user`
- `host`
- `userhost`
- `cwd`
- `status`
- `os`
- `plugins`
- `theme`
- `date`
- `time`
- `datetime`

Formati attuali:

- `date`: `YYYY-MM-DD`
- `time`: `HH:MM:SS`
- `datetime`: `YYYY-MM-DD HH:MM:SS`

### Configurazione automatica

All'avvio, `oosh` prova a caricare in quest'ordine:

1. il file puntato da `OOSH_CONFIG`
2. `oosh.conf` nella directory corrente
3. `~/.oosh/prompt.conf`

## 10. Plugin

I plugin possono aggiungere:

- nuovi comandi
- nuove proprieta e metodi object-aware
- nuovi value resolver come `sample()`
- nuovi stage di pipeline come `sample_wrap()`

### 10.1 Caricare un plugin

```text
plugin load build/oosh_sample_plugin.dylib
```

Su Linux:

```text
plugin load build/oosh_sample_plugin.so
```

Su Windows:

```text
plugin load build/oosh_sample_plugin.dll
```

### 10.2 Elencare i plugin caricati

```text
plugin list
```

### 10.3 Plugin di esempio

Il repository include un plugin di esempio che aggiunge:

```text
hello-plugin
sample()
sample_wrap()
```

Esempio:

```text
hello-plugin Team
sample() -> name
text("ciao") |> sample_wrap()
```

## 11. Flussi d'uso consigliati

### Esplorare una directory

```text
. -> children()
```

### Leggere le proprieta di un file

```text
README.md -> type
README.md -> size
README.md -> path
```

### Ispezionare un file in modo completo

```text
inspect README.md
```

### Leggere le prime righe di un file

```text
README.md -> read_text(256)
```

### Trovare il file piu grande nella directory corrente

```text
. -> children() |> where(type == "file") |> sort(size desc) |> first()
```

## 12. Errori comuni

### `unknown command`

Hai scritto un comando non registrato oppure hai usato una sintassi non riconosciuta come comando.

Controlla con:

```text
help
```

### `parse error`

La sintassi non rispetta il formato atteso.

Controlla:

- parentesi tonde chiuse
- virgolette abbinate
- uso corretto di `|>`

### `where() expects a list`

Stai usando `where()` su un valore che non e una lista.

Esempio errato:

```text
README.md -> type |> where(type == "file")
```

Esempio corretto:

```text
. -> children() |> where(type == "file")
```

### `first() cannot be used on an empty list`

Hai filtrato una lista fino a renderla vuota.

## 13. Limiti attuali dell'MVP

Questa versione non implementa ancora:

- confronti `>`, `<`, `>=`, `<=` in `where()`
- pipeline object-aware verso processi esterni
- built-in dentro pipeline shell multi-stage
- field splitting e semantica POSIX completa dopo espansioni
- nuovi tipi di oggetto risolti da plugin oltre ai resolver/stage gia supportati

Quindi il manuale descrive soprattutto l'uso dell'MVP reale, non la visione completa futura.

## 14. Cheat sheet

```text
 . -> type
help
pwd
cd src
inspect .
get README.md size
call . children
. -> children()
README.md -> read_text(128)
. -> children() |> where(type == "file")
. -> children() |> where(type == "file") |> sort(size desc)
. -> children() |> where(type == "file") |> sort(size desc) |> take(3)
. -> children() |> first()
. -> children() |> count()
ls -1 | wc -l
cat < README.md | wc -l
ls > out.txt
ls missing 2>&1 | wc -l
prompt show
prompt load examples/oosh.conf
plugin list
```
