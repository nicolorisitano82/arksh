# Sintassi proposta per arksh

Questo documento separa due livelli:

- sintassi gia supportata dall'MVP
- sintassi consigliata per la shell completa

## 1. Sintassi gia supportata

### 1.1 Comandi espliciti

```text
set PROJECT arksh
export PROJECT_ROOT "$PWD"
alias ll="ls -1"
source examples/arkshrc
let files = . -> children()
let is_file = [:it | it -> type == "file"]
let get_name = [:it | it -> name]
let sum_step = [:acc :n | local next = acc + n ; next]
function greet(name) do
text("hello %s") -> print(name)
endfunction
class Named do
property name = text("unnamed")
method rename = [:self :next | self -> set("name", next)]
endclass
classes Named
functions greet
eval 'text("hi") -> print()'
wait %1
trap 'text("bye") -> print()' EXIT
while getopts ":ab:" opt "-abvalue" "-x" ; do echo "$opt:$OPTARG" ; done
umask
umask 077
umask "u=rwx,g=rx,o="
ulimit -n
set -e
set -u
set -x
set -o pipefail
set +e
set +u
set +x
set +o pipefail
extend directory property child_count = [:it | it -> children() |> count()]
extend object method label = [:it :prefix | prefix]
extend
let
history
perf show
perf reset
perf on
perf off
jobs
true && text("ok") -> print()
sleep 5 & jobs
type ls
inspect .
get README.md size
call . children
call README.md read_text 128
prompt show
prompt load examples/arksh.conf
prompt load examples/arksh-git.conf
prompt render
plugin load build/arksh_sample_plugin.so
plugin load git-prompt-plugin
plugin list
plugin info arksh_sample_plugin
plugin enable arksh_sample_plugin
plugin disable arksh_sample_plugin
plugin autoload set /usr/local/lib/arksh/arksh_trash_plugin.dylib
plugin autoload unset /usr/local/lib/arksh/arksh_trash_plugin.dylib
plugin autoload list
```

### 1.2 Espressioni oggetto

```text
. -> type
. -> children()
README.md -> read_text(128)
README.md -> parent()
. -> describe()
data.json -> read_json() -> get_path("a[2].b")
git_info() -> branch
obj(".").type
```

Note pratiche:

- le chain top-level con `->` vengono parse-ate come una sola object expression strutturata
- il receiver puo essere a sua volta il risultato di una chain precedente, senza passare da un `let` intermedio
- l'executor puo quindi evitare round-trip ripetuti `value -> text -> parse -> value` sui member successivi

### 1.3 Pipeline oggetti supportata

```text
. -> children() |> where(type == "file")
. -> children() |> where(type == "file") |> sort(size desc)
. -> children() |> where(type == "file") |> take(5)
. -> children() |> first()
. -> children() |> count()
README.md -> parent() |> render()
text("ciao")
number(42)
bool(true)
list(1, 20, 3) |> sort(value desc)
capture("pwd")
capture("pwd") |> lines() |> first()
text(" a, b , c ") |> trim() |> split(",") |> join(" | ")
list(1, 2, 3) |> reduce(number(0), [:acc :n | acc + n])
list(1, 2, 3) |> to_json()
text("[1, 2, 3]") |> from_json() |> count()
capture_lines("ls -1") |> each(render())
. -> children() |> each(name) |> take(5)
. -> children() |> where([:it | it -> type == "file"]) |> each([:it | it -> name])
let files = . -> children()
files |> where(is_file) |> each(get_name)
data.json -> read_json()
data.json -> read_json() -> get_path("a[2].b")
let data = data.json -> read_json()
data -> set_path("meta.version", number(2))
list(map("profile", map("name", "alpha")), map("profile", map("name", "beta"))) |> pluck("profile.name")
. -> child_count
README.md -> label("doc")
Named() -> type
Named() -> isa("Named")
Named -> methods
```

### 1.4 Pipeline shell e redirection supportate

```text
ls -1 | wc -l
cat < README.md | wc -l
ls > out.txt
printf hello >> out.txt
ls missing 2> err.txt
ls missing 2>&1 | wc -l
./arksh_test_echo_stdin <<< "hello"
diff <(printf "a\n") <(printf "a\n")
read line < <(printf "word\n")
printf hi > >(wc -c)
./arksh_test_count_lines <<EOF
one
two
EOF
./arksh_test_echo_stdin <<-EOF
	one
	two
EOF
./arksh_test_emit_args hello 3> fd3.out 1>&3
./arksh_test_count_lines 3< fdin.txt 0<&3
```

Nota: la sostituzione di processo `<(...)` e `>(...)` è disponibile sul runtime POSIX. In `arksh` viene materializzata come FIFO temporanea e ripulita automaticamente a fine comando.

### 1.4.1 Liste di comandi e background supportati

```text
pwd ; ls
true && text("ok") -> print()
false || text("fallback") -> print()
set -u ; echo ${HOME:-/tmp}
set PS4 "TRACE: " ; set -x ; echo "$HOME"
set -o pipefail ; /usr/bin/true | /usr/bin/false | /usr/bin/true
set s demo.txt ; [[ "$s" == *.txt ]] && text("match") -> print()
set n 42 ; [[ "$n" =~ ^[0-9]+$ ]] && text("$BASH_REMATCH") -> print()
sleep 5 &
sleep 5 & jobs
fg
bg
wait %1
```

### 1.4.2 Controllo di flusso supportato

Sulla singola riga la forma supportata e shell-style, con `;` prima di `then` e `do`:

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

In REPL e nei file caricati con `source` puoi usare anche la forma multilinea:

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

function greet(name)
do
text("hello %s") -> print(name)
endfunction

greet nicolo

class Document extends Named
do
property kind = text("doc")
method init = [:self :name | self -> set("name", name)]
method label = [:self | text("%s:%s") -> print(self -> kind, self -> name)]
endclass

Document(text("manuale")) -> label()
```

Regole pratiche:

- `condizione ? vero : falso` restituisce un valore e funziona dentro `let`, block, argomenti metodo e top-level value expression
- `[[ ... ]]` estende i test shell con `==`/`=` su pattern glob, `=~` su regex POSIX ERE, `!`, `&&`, `||` e parentesi di raggruppamento
- dentro `[[ ... ]]` le espansioni fanno parameter expansion e command substitution, ma non fanno word splitting ne pathname expansion
- quando `=~` ha successo, `$BASH_REMATCH` contiene il match completo; il binding tipizzato `BASH_REMATCH` espone la lista delle catture disponibili
- le condizioni di `if`, `while` e `until` provano prima a valutare una value expression e ne usano la truthiness
- se la condizione non e una value expression valida, `arksh` la esegue come comando shell classico e usa il suo exit status
- `break [count]` interrompe il loop corrente o uno piu esterno
- `continue [count]` passa all'iterazione successiva del loop corrente o di uno piu esterno
- la sorgente di `for` puo essere una lista/value expression oppure una sequenza di shell words
- la variabile del `for` viene esposta sia come binding tipizzato sia come variabile shell testuale
- `switch` usa branch `case ... then ...` e un eventuale `default then ...`, chiusi da `endswitch`; sulla singola riga separa i branch con `;`
- `case ... in ... esac` usa pattern shell classici; ogni branch si chiude con `;;` e puoi usare alternative `pat1|pat2)`
- nei file `source`, la forma oggi piu stabile per `case` resta quella single-line
- `function nome(param...) do ... endfunction` definisce una funzione shell
- le funzioni si invocano come comandi normali: `nome arg1 arg2`
- i parametri funzione vengono esposti sia come binding tipizzati stringa sia come variabili shell testuali
- `return [value-expression]` chiude la funzione corrente e, se presente, restituisce il valore come output
- al termine di una funzione, `vars` e `bindings` vengono ripristinati allo stato esterno
- `eval "..."` riesegue la stringa nel contesto shell corrente
- per `eval` e `trap`, la forma piu robusta sulla singola riga resta quella con apici singoli attorno al comando completo
- `exec cmd ...` esegue il comando e poi termina la shell corrente
- `wait [%job]` aspetta la fine di un background job
- `trap "comando" EXIT` registra un trap minimo eseguito all'uscita della shell
- `class Nome [extends Base1, Base2] do ... endclass` definisce una classe custom
- il corpo classe accetta `property nome = espressione` e `method nome = block`
- l'istanziazione usa la stessa sintassi dei resolver: `Nome()` oppure `Nome(arg1, arg2)`
- se esiste `method init = [:self ... | ...]`, viene chiamato automaticamente durante l'istanziazione
- `self -> set("campo", valore)` aggiorna lo stato dell'istanza
- la lookup di proprieta e metodi segue la classe corrente e poi le basi da sinistra verso destra
- in caso di ereditarieta multipla, le basi piu a sinistra hanno precedenza sulle basi a destra

### 1.5 Quoting ed espansioni supportate

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

Le espansioni leggono prima le variabili definite nella shell e poi l'ambiente.

Note pratiche:

- `<<EOF ... EOF` invia un heredoc al `stdin`
- `<<-EOF ... EOF` rimuove i tab iniziali dal body
- `<<< "text"` invia una stringa espansa seguita da newline al `stdin`
- `3>`, `n>&m` e `n<&m` permettono redirection e duplicazione su file descriptor arbitrari
- i fd custom oltre `0/1/2` sono verificati sui build POSIX

### 1.5.1 Profilazione leggera supportata

```text
perf show
perf status
perf on
perf off
perf reset
```

Uso tipico:

```text
perf on ; perf reset ; . -> children() |> count() ; perf show
```

Con `ARKSH_PERF=1` la telemetria viene attivata fin dall'avvio del processo, utile per benchmark di startup e per il runner `arksh_perf_runner`.

### 1.6 Startup file supportato

All'avvio `arksh` tenta di eseguire:

```text
$ARKSH_RC
~/.arkshrc
```

Se `ARKSH_RC` e impostata, ha precedenza sul file home.

### 1.7 Convenzioni interattive supportate

In REPL l'MVP supporta:

- frecce per muovere il cursore e navigare la history
- `Tab` per completion base
- `Tab` dopo `->` per suggerire proprieta e metodi del receiver
- `Ctrl-A` e `Ctrl-E` per inizio/fine riga
- `Ctrl-C` per interrompere il foreground
- sui build POSIX, `Ctrl-Z` per fermare un job in `fg`

## 2. Sintassi target raccomandata

### 2.1 Principio

Ogni selettore o sorgente produce un valore object-aware oppure una lista di valori.

Accessi:

- `selector.property`
- `selector.method(args)`

### 2.2 Selettori

#### File system

```text
"./src" -> children()
"/etc/hosts" -> read_text(128)
. -> type
README.md -> size
mount("/")
device("/dev/tty")
```

#### Sistema e shell

```text
shell()
env("PATH")
proc()
```

### 2.3 Proprieta

```text
. -> type
/tmp -> exists
/tmp -> name
proc() -> pid
env("PATH")
shell() -> cwd
```

### 2.4 Metodi

```text
. -> children()
/etc/hosts -> read_text(256)
/etc/hosts -> parent()
shell() -> keys()
env() -> keys()
```

## 3. Pipeline oggetti

Per restare coerente con il modello object-oriented, la pipeline dovrebbe passare oggetti e non solo stringhe.

Sintassi proposta:

```text
/tmp -> children() |> where(type == "file") |> sort(size desc)
```

Oppure in forma piu verbosa:

```text
/tmp -> children() |> filter(type == "file") |> map(name, size)
```

Operatori attualmente implementati nell'MVP:

- `where(property == value)`
- `where(block)`
- `sort(property asc|desc)`
- `take(n)`
- `first()`
- `count()`
- `lines()`
- `trim()`
- `split(separator?)`
- `join(separator?)`
- `reduce(block)`
- `reduce(init, block)`
- `from_json()`
- `to_json()`
- `each(selector)`
- `each(block)`
- `render()`

## 4. Variabili e binding tipizzati

Oggi `set NAME value` crea una variabile shell testuale, mentre `export NAME value` la rende visibile ai processi figli.

Per binding semantici piu ricchi, ad esempio oggetti, liste o block assegnati a nomi locali, la sintassi supportata e:

```text
let tmp = . -> children()
let is_file = [:it | it -> type == "file"]
let get_name = [:it | it -> name]
let sum_step = [:acc :n | local next = acc + n ; next]
tmp |> where(is_file) |> each(get_name)
list(1, 2, 3) |> reduce(number(0), sum_step)
is_file -> arity
is_file -> source
```

`let` senza argomenti elenca i binding tipizzati correnti.

`set` resta la sintassi giusta quando vuoi invece una variabile shell testuale:

```text
set LIMIT=128
export LIMIT
```

In questo modello:

- `set` e `export` lavorano su testo ed espansione shell
- `let` lavora su valori object-aware
- `unset nome` rimuove sia la variabile shell sia il binding tipizzato omonimo, se presenti

## 4.1 Estensioni di oggetti

L'MVP supporta estensioni dichiarative nel linguaggio:

```text
extend <target> property <name> = <block>
extend <target> method <name> = <block>
```

Esempi:

```text
extend directory property child_count = [:it | it -> children() |> count()]
```

Regole:

- `property`: block a un solo parametro, il receiver
- `method`: primo parametro = receiver, parametri successivi = argomenti del metodo
- il target puo essere `any`, un value kind (`string`, `number`, `bool`, `object`, `block`, `list`) oppure un object kind (`path`, `file`, `directory`, `device`, `mount`)
- le estensioni vengono usate come fallback dopo i membri built-in del core

## 5. Grammatica EBNF consigliata

```ebnf
line            = command_list | object_pipeline | shell_pipeline | value_expression | object_expression | command ;
command_list    = list_entry , { list_operator , list_entry } ;
list_operator   = ";" | "&&" | "||" | "&" ;
object_pipeline = value_source , { "|>" , stage_call } ;
shell_pipeline  = shell_command , { "|" , shell_command } ;
shell_command   = token , { token | redirection } ;
value_expression = value_source ;
value_source    = object_expression
                | binding
                | "text" , "(" , token , ")"
                | "string" , "(" , token , ")"
                | "number" , "(" , token , ")"
                | "bool" , "(" , token , ")"
                | "list" , "(" , [ token , { "," , token } ] , ")"
                | "array" , "(" , [ token , { "," , token } ] , ")"
                | block_literal
                | "capture" , "(" , token , ")"
                | "capture_lines" , "(" , token , ")" ;
binding         = identifier ;
block_literal   = "[" , { ":" , identifier } , "|" , expression_body , "]" ;
extension_decl  = "extend" , target , ("property" | "method") , identifier , "=" , block_literal ;
redirection     = "<" , token
                | ">" , token
                | ">>" , token
                | "2>" , token
                | "2>>" , token
                | "2>&1"
                | "<<<" , token
                | "<<" , token , heredoc_body
                | "<<-" , token , heredoc_body
                | integer , ">" , token
                | integer , ">>" , token
                | integer , "<" , token
                | integer , ">&" , integer
                | integer , "<&" , integer ;
object_expression = member_access | call ;
expression      = member_access | call | literal | identifier ;
member_access   = primary , "." , identifier ;

Nel body di un block e supportata anche la forma:

```text
local <identifier> = <value-expression> ; <expression_body>
```

Regole pratiche:

- `local` e valido solo nel body di un block
- il binding creato da `local` e typed e usa lo stesso modello di `let`
- lo scope dura solo per la valutazione del block corrente
call            = primary , "." , identifier , "(" , [ argument_list ] , ")" ;
primary         = constructor | identifier | string | number ;
constructor     = identifier , "(" , [ argument_list ] , ")" ;
argument_list   = expression , { "," , expression } ;
command         = identifier , { token } ;
stage_call      = identifier , "(" , [ argument_list ] , ")" | identifier ;
identifier      = letter , { letter | digit | "_" | "-" } ;
string          = '"' , { char } , '"' ;
number          = digit , { digit } ;
token           = string | identifier | number ;
```

## 6. Proprieta e metodi minimi consigliati per tipo

### `file`

Proprieta:

- `name`
- `path`
- `size`
- `extension`
- `exists`
- `readable`
- `writable`

Metodi:

- `read_text(limit)`
- `read_json()`
- `write_json(binding)`
- `get_path(path)`
- `has_path(path)`
- `set_path(path, value)`
- `pick(key1, key2, ...)`
- `merge(other)`
- `read_bytes(limit)`
- `parent()`
- `describe()`

### `directory`

Proprieta:

- `name`
- `path`
- `exists`
- `readable`
- `writable`

Metodi:

- `children()`
- `walk(depth)`
- `parent()`
- `describe()`

### `mount`

Proprieta:

- `path`
- `fs_type`
- `capacity`
- `used`
- `available`

Metodi:

- `children()`
- `stats()`

### `device`

Proprieta:

- `name`
- `path`
- `device_type`
- `exists`

Metodi:

- `open(mode)`
- `describe()`

## 7. Prompt DSL consigliata

L'MVP usa un file `key=value`, ma la sintassi consigliata per una versione piu ricca e:

```text
theme "aurora" {
  left  = [userhost, cwd, git]
  right = [status, os, time]
  separator = " :: "
  colors {
    userhost = green
    cwd      = cyan
    status   = yellow
    git      = red
  }
}

plugin "git"
plugin "docker"
```

Motivazione:

- leggibile
- facilmente parsabile
- abbastanza vicina ai theme engine conosciuti

## 8. Plugin DSL consigliata

Se in futuro si vuole supportare plugin dichiarativi oltre a quelli binari:

```text
plugin "git" from "~/.arksh/plugins/git-plugin.so"
plugin "docker" from "/usr/local/lib/arksh/docker-plugin.so"
```

## 9. Regole di compatibilita consigliate

1. `path -> member` e la sintassi consigliata, ma `obj("path").member` deve restare sempre valido.
2. I nomi di proprieta base non vanno rinominati.
3. I plugin non devono introdurre parole chiave che rompano le espressioni esistenti.
4. La pipeline `|>` va riservata alle trasformazioni object-aware.

## 10. Esempi di uso finale desiderato

```text
let logs = obj("/var/log")
logs.children() |> where(name ~= ".log") |> sort(size desc)
```

```text
mount("/").stats()
```

```text
shell() -> cwd
```

```text
env("PATH")
```

```text
proc() -> pid
```

Questa sintassi rende il progetto estendibile e abbastanza rigoroso da essere implementato sia da una persona sia da un'altra AI.
