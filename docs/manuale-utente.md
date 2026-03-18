# Manuale Utente di oosh

Versione di riferimento: attuale (marzo 2026)

---

## Indice

1. [Cos'e oosh](#1-cose-oosh)
2. [Avvio rapido](#2-avvio-rapido)
3. [Modello mentale](#3-modello-mentale)
4. [Linguaggio — Sintassi di base](#4-linguaggio--sintassi-di-base)
   - 4.1 [Token e lessico](#41-token-e-lessico)
   - 4.2 [Quoting](#42-quoting)
   - 4.3 [Espansioni](#43-espansioni)
   - 4.4 [Operatori lista comandi](#44-operatori-lista-comandi)
   - 4.5 [Operatori binari e confronto](#45-operatori-binari-e-confronto-in-value-expressions)
   - 4.6 [Operatore ternario](#46-operatore-ternario)
5. [Espressioni oggetto](#5-espressioni-oggetto)
   - 5.1 [Sintassi selettore → membro](#51-sintassi-selettore--membro)
   - 5.2 [Tipi di oggetto filesystem](#52-tipi-di-oggetto-filesystem)
   - 5.3 [Proprieta filesystem](#53-proprieta-filesystem)
   - 5.4 [Metodi filesystem](#54-metodi-filesystem)
6. [Tipi di valore e costruttori](#6-tipi-di-valore-e-costruttori)
   - 6.1 [text / string](#61-text--string)
   - 6.2 [number](#62-number)
   - 6.3 [bool e letterali booleani](#63-bool-e-letterali-booleani)
   - 6.4 [list / array](#64-list--array)
   - 6.5 [map](#65-map)
   - 6.6 [capture e capture_lines](#66-capture-e-capture_lines)
   - 6.7 [Block literal](#67-block-literal)
   - 6.8 [env, proc, shell](#68-env-proc-shell)
7. [Pipeline oggetti (|>)](#7-pipeline-oggetti-)
   - 7.1 [Concetto e sintassi](#71-concetto-e-sintassi)
   - 7.2 [Bridge shell/object](#72-bridge-shellobject)
   - 7.3 [Riferimento completo degli stage](#73-riferimento-completo-degli-stage)
8. [Pipeline shell e redirection](#8-pipeline-shell-e-redirection)
   - 8.1 [Operatore |](#81-operatore-)
   - 8.2 [Redirection](#82-redirection)
   - 8.3 [Heredoc](#83-heredoc)
   - 8.4 [Background (&)](#84-background-)
   - 8.5 [Limiti dei built-in nelle pipeline shell](#85-limiti-dei-built-in-nelle-pipeline-shell)
9. [Controllo di flusso](#9-controllo-di-flusso)
   - 9.1 [if / elif / else / fi](#91-if--elif--else--fi)
   - 9.2 [while / done](#92-while--done)
   - 9.3 [until / done](#93-until--done)
   - 9.4 [for / in / do / done](#94-for--in--do--done)
   - 9.5 [break e continue](#95-break-e-continue)
   - 9.6 [switch / case / default / endswitch](#96-switch--case--default--endswitch)
   - 9.7 [case … in … esac](#97-case--in--esac)
   - 9.8 [return](#98-return)
   - 9.9 [Ternario](#99-ternario)
10. [Funzioni](#10-funzioni)
    - 10.1 [Definizione e chiamata](#101-definizione-e-chiamata)
    - 10.2 [Parametri e scope](#102-parametri-e-scope)
    - 10.3 [Override di built-in e uso di builtin](#103-override-di-built-in-e-uso-di-builtin)
11. [Classi](#11-classi)
    - 11.1 [Definizione](#111-definizione)
    - 11.2 [Proprieta e metodi](#112-proprieta-e-metodi)
    - 11.3 [Istanziazione e metodo init](#113-istanziazione-e-metodo-init)
    - 11.4 [Ereditarieta multipla](#114-ereditarieta-multipla)
    - 11.5 [Introspezione](#115-introspezione)
12. [Estensioni (extend)](#12-estensioni-extend)
13. [Riferimento comandi built-in](#13-riferimento-comandi-built-in)
14. [Job control](#14-job-control)
    - 14.1 [Background job (&)](#141-background-job-)
    - 14.2 [Visualizzazione (jobs)](#142-visualizzazione-jobs)
    - 14.3 [Foreground (fg)](#143-foreground-fg)
    - 14.4 [Background (bg)](#144-background-bg)
    - 14.5 [Ctrl-Z e process group](#145-ctrl-z-e-process-group)
15. [Editor di riga interattivo](#15-editor-di-riga-interattivo)
    - 15.1 [Tasti supportati](#151-tasti-supportati)
    - 15.2 [Syntax highlighting](#152-syntax-highlighting)
    - 15.3 [Autosuggestion](#153-autosuggestion)
16. [Tab completion](#16-tab-completion)
    - 16.1 [Completion per contesto](#161-completion-per-contesto)
    - 16.2 [Indicatori di tipo](#162-indicatori-di-tipo)
17. [Prompt](#17-prompt)
    - 17.1 [Configurazione](#171-configurazione)
    - 17.2 [Segmenti disponibili](#172-segmenti-disponibili)
    - 17.3 [Caricamento automatico](#173-caricamento-automatico)
18. [Plugin](#18-plugin)
    - 18.1 [Caricare un plugin](#181-caricare-un-plugin)
    - 18.2 [Comandi plugin](#182-comandi-plugin)
    - 18.3 [Creare un plugin (cenni)](#183-creare-un-plugin-cenni)
19. [Avvio e configurazione](#19-avvio-e-configurazione)
    - 19.1 [File RC](#191-file-rc)
    - 19.2 [History](#192-history)
    - 19.3 [Variabili di ambiente speciali](#193-variabili-di-ambiente-speciali)
20. [Errori frequenti e diagnostica](#20-errori-frequenti-e-diagnostica)
21. [Cheat sheet](#21-cheat-sheet)

---

## 1. Cos'e oosh

oosh e una shell interattiva e linguaggio di scripting orientato agli oggetti, progettata per essere compatibile con le abitudini Unix consolidate, pur aggiungendo un livello oggetti ricco e un sistema di tipi integrato.

L'obiettivo principale e consentire all'utente di trattare le entita del filesystem, i valori di programma e l'output dei processi come oggetti strutturati, senza abbandonare la sintassi tradizionale delle shell POSIX per i comandi di sistema ordinari.

Le caratteristiche principali di oosh sono:

- **Pipeline oggetti**: un operatore `|>` distinto permette di concatenare trasformazioni su valori tipizzati, separando nettamente il flusso di testo tradizionale dalle elaborazioni strutturate.
- **Tipi di valore**: stringhe, numeri, booleani, liste, mappe e blocchi sono valori di prima classe, costruibili con costruttori espliciti e manipolabili in espressioni.
- **Oggetti filesystem**: ogni percorso del filesystem diventa un oggetto con proprieta e metodi, interrogabile con l'operatore `->`.
- **Classi ed estensioni**: e possibile definire classi con ereditarieta multipla ed estendere qualsiasi tipo built-in con proprieta e metodi aggiuntivi.
- **Sistema di plugin**: l'ABI C stabile permette di aggiungere comandi, stage pipeline, tipi e resolver senza ricompilare la shell.
- **Editor di riga avanzato**: syntax highlighting in tempo reale, autosuggestion dalla history e tab completion contestuale.

oosh non e uno strato di compatibilita su bash o zsh. E una shell autonoma con il proprio interprete, che punta ad essere pienamente usabile come shell di sistema e come linguaggio di automazione.

---

## 2. Avvio rapido

### Compilazione

Il sorgente di oosh usa un sistema di build basato su CMake. Per compilare:

```bash
mkdir build
cd build
cmake ..
make
```

In alternativa, se si vuole un build con AddressSanitizer (utile per sviluppo e debug):

```bash
mkdir build-asan
cd build-asan
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON ..
make
```

L'eseguibile risultante si trova in `build/oosh` (o `build-asan/oosh`).

### Prima esecuzione

Per avviare la shell in modo interattivo:

```bash
./build/oosh
```

Il prompt predefinito mostra utente, host e directory corrente. Per uscire:

```bash
exit
```

oppure con `Ctrl-D` su riga vuota.

### Esecuzione di uno script

Passare il percorso dello script come primo argomento:

```bash
./build/oosh mio_script.osh
```

Lo script viene eseguito nel contesto corrente della shell. Non occorre nessun shebang speciale, ma per rendere lo script eseguibile direttamente e possibile usare:

```bash
#!/usr/bin/env oosh
```

### Primo script di esempio

Creare il file `esempio.osh`:

```bash
#!/usr/bin/env oosh

# Stampa il percorso corrente come oggetto
let cwd_obj = path(".")
cwd_obj -> describe()

# Lista tutti i file non nascosti nella directory corrente
let files = path(".") -> children()
files |> where(hidden == false) |> sort(name asc) |> each(:it | it -> name) |> render()

# Cattura l'output di un comando e lo elabora
let righe = capture_lines("ls /usr/bin")
righe |> grep("ssh") |> count()
```

Eseguirlo:

```bash
./build/oosh esempio.osh
```

---

## 3. Modello mentale

Per usare oosh efficacemente e utile comprendere il modello concettuale su cui e costruita.

### Comandi shell classici

Tutto cio che sembra un comando tradizionale funziona come ci si aspetta:

```bash
ls -la /tmp
grep "pattern" file.txt
echo "ciao mondo"
```

Questi comandi vengono eseguiti come processi figli. oosh gestisce l'eredita dell'ambiente, la redirection e le pipeline testuali esattamente come una shell POSIX.

### Espressioni oggetto

Accanto ai comandi, oosh introduce le **espressioni oggetto**. Un'espressione oggetto e una stringa di token che produce un valore tipizzato anziche eseguire un processo. Le espressioni oggetto compaiono:

- nel secondo membro di `let nome = <espressione>`
- come argomenti di stage pipeline
- come condizione di `if`, `while`, `for`
- dentro block literal `[:param | ...]`

```bash
let n = number(42)
let s = text("ciao")
let lista = list(1, 2, 3)
```

### Pipeline oggetti

L'operatore `|>` connette una sorgente di valori a una serie di stage che la trasformano:

```bash
list(10, 3, 7, 1) |> sort(asc) |> take(2) |> render()
```

La sorgente puo essere un valore tipizzato costruito in linea oppure l'output di un comando esterno (bridge shell/object).

### Pipeline shell

L'operatore `|` connette lo stdout di un processo allo stdin del successivo, esattamente come in bash:

```bash
ps aux | grep nginx | wc -l
```

### Valori di prima classe

Blocchi, liste, mappe e numeri sono valori che possono essere assegnati a binding, passati a funzioni e restituiti da metodi. Non esiste una distinzione sintattica rigida tra "dati" e "codice": un block literal e un valore come gli altri.

---

## 4. Linguaggio — Sintassi di base

### 4.1 Token e lessico

oosh distingue i seguenti tipi di token:

- **Keyword**: `if`, `then`, `else`, `elif`, `fi`, `while`, `until`, `for`, `in`, `do`, `done`, `function`, `endfunction`, `class`, `extends`, `endclass`, `extend`, `return`, `break`, `continue`, `switch`, `case`, `default`, `endswitch`, `esac`, `true`, `false`
- **Identificatori**: sequenze di lettere, cifre e `_`, non inizianti con cifra
- **Letterali stringa**: `'...'` e `"..."`
- **Letterali numerici**: sequenze di cifre, opzionalmente con punto decimale
- **Operatori**: `|>`, `|`, `->`, `=`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `+`, `-`, `*`, `/`, `?`, `:`, `;`, `&&`, `||`, `&`, `(`, `)`, `[`, `]`
- **Commenti**: da `#` a fine riga

I token sono separati da spazi o da operatori. Il newline equivale a `;` come separatore di istruzione in quasi tutti i contesti, con alcune eccezioni (corpo di blocco multilinea, continuazione dopo `\`).

Una riga puo essere continuata sulla successiva terminando con `\`:

```bash
let risultato = number(1) + \
                number(2)
```

### 4.2 Quoting

Il quoting controlla quali espansioni vengono eseguite su una stringa.

#### Single quote

Il single quote `'...'` e il quoting piu forte: il contenuto e trattato come testo letterale, senza alcuna espansione. Nemmeno il backslash e speciale all'interno.

```bash
echo 'Il valore di $HOME e letterale'
# stampa: Il valore di $HOME e letterale

echo 'Nessuna $(sostituzione) qui'
# stampa: Nessuna $(sostituzione) qui
```

Non e possibile inserire un single quote dentro un single-quoted string. Per farlo si usa la concatenazione:

```bash
echo 'it'"'"'s fine'
# stampa: it's fine
```

#### Double quote

Il double quote `"..."` permette espansioni di variabili e command substitution, ma inibisce il globbing e la word splitting:

```bash
nome="mondo"
echo "Ciao $nome"
# stampa: Ciao mondo

echo "Directory: $(pwd)"
# stampa: Directory: /home/utente

echo "Home: $HOME"
# stampa: Home: /home/utente
```

All'interno dei double quote, il backslash e speciale prima di `$`, `` ` ``, `"`, `\` e newline.

#### Backslash

Fuori dalle stringhe quotate, il backslash escapa il carattere immediatamente successivo, trattandolo come letterale:

```bash
echo Ciao\ Mondo
# stampa: Ciao Mondo (lo spazio non e un separatore di token)

ls file\ con\ spazi.txt
```

Un backslash prima del newline continua la riga sulla successiva.

### 4.3 Espansioni

#### Espansione di variabili shell

`$NOME` o `${NOME}` espande il valore della variabile shell (o di ambiente) chiamata `NOME`. Le variabili locali hanno precedenza su quelle di ambiente.

```bash
set SALUTO ciao
echo $SALUTO
# stampa: ciao

export PREFISSO=/usr
echo ${PREFISSO}/bin
# stampa: /usr/bin
```

La forma `${NOME}` e utile quando il nome e ambiguo:

```bash
set X abc
echo ${X}def
# stampa: abcdef

echo $Xdef
# espande $Xdef (probabilmente vuoto, non $X seguita da def)
```

#### Exit status ($?)

`$?` si espande all'exit status dell'ultimo comando eseguito. Il valore e `0` in caso di successo, diverso da zero in caso di errore.

```bash
ls /tmp
echo "exit status: $?"
# stampa: exit status: 0

ls /percorso/inesistente 2>/dev/null
echo "exit status: $?"
# stampa: exit status: 1 (o altro valore non zero)
```

#### Command substitution ($(…))

`$(comando)` esegue il comando e sostituisce l'intera espressione con lo stdout del comando, con eventuale rimozione del newline finale.

```bash
echo "Utente corrente: $(whoami)"
echo "Data: $(date +%Y-%m-%d)"

let lista_file = "$(ls /tmp)"
```

#### Tilde (~)

`~` si espande nella directory home dell'utente corrente (il valore di `$HOME`).

```bash
cd ~
ls ~/documenti
```

#### Globbing

I caratteri `*`, `?` e `[...]` negli argomenti dei comandi vengono espansi in elenchi di percorsi corrispondenti.

```bash
ls *.txt
# lista tutti i file .txt nella directory corrente

rm foto_?.jpg
# rimuove foto_1.jpg, foto_a.jpg, ecc.

ls [abc]*.md
# file che iniziano con a, b o c e terminano con .md
```

Il globbing e attivo negli argomenti dei comandi ordinari. Non e attivo all'interno dei selettori oggetto (dopo `->`) ne nei target di redirection.

### 4.4 Operatori lista comandi

I seguenti operatori separano o condizionano l'esecuzione di comandi in sequenza.

#### Punto e virgola (;)

Esegue i comandi in sequenza, indipendentemente dall'exit status:

```bash
mkdir /tmp/prova ; cd /tmp/prova ; ls
```

#### AND logico (&&)

Il secondo comando viene eseguito solo se il primo ha avuto successo (exit status 0):

```bash
mkdir /tmp/nuova_dir && echo "Directory creata"
cd /tmp/nuova_dir && ls
```

#### OR logico (||)

Il secondo comando viene eseguito solo se il primo ha fallito (exit status non zero):

```bash
ls /percorso/inesistente || echo "Percorso non trovato"
mkdir /tmp/dir || echo "Impossibile creare la directory"
```

#### Combinazioni

Gli operatori possono essere concatenati:

```bash
mkdir /tmp/test && cd /tmp/test && touch file.txt || echo "Qualcosa e andato storto"
```

#### Ampersand (&)

Lancia il comando in background (vedi sezione 14):

```bash
sleep 60 &
```

### 4.5 Operatori binari e confronto in value expressions

All'interno delle espressioni oggetto (value expressions) sono disponibili operatori binari per aritmetica e confronto.

#### Operatori aritmetici

Operano su valori `number`:

```bash
let a = number(10)
let b = number(3)
let somma = a + b       # 13
let diff  = a - b       # 7
let prod  = a * b       # 30
let quot  = a / b       # 3.333...
```

#### Concatenazione di stringhe (+)

L'operatore `+` su valori `text` o `string` produce concatenazione:

```bash
let saluto = text("Ciao") + text(", ") + text("mondo")
saluto -> print()
# stampa: Ciao, mondo
```

#### Operatori di confronto

Producono un valore `bool`:

```bash
let uguale   = number(5) == number(5)    # true
let diverso  = number(5) != number(3)    # true
let minore   = number(2) < number(8)     # true
let maggiore = number(9) > number(4)     # true
let minEq    = number(5) <= number(5)    # true
let magEq    = number(6) >= number(7)    # false
```

Il risultato di un confronto puo essere usato come condizione in `if` e `while`:

```bash
let x = number(42)
if x > number(10)
then
  echo "x e maggiore di 10"
fi
```

### 4.6 Operatore ternario

La sintassi e:

```
condizione ? valore_vero : valore_falso
```

Produce `valore_vero` se la condizione e truthy, altrimenti `valore_falso`. Puo essere usato in qualsiasi punto in cui e attesa una value expression.

```bash
let eta = number(20)
let stato = eta >= number(18) ? text("maggiorenne") : text("minorenne")
stato -> print()
# stampa: maggiorenne
```

Il ternario e valutato eager: entrambi i rami vengono parsati, ma solo quello scelto viene valutato a runtime. Puo essere annidato, sebbene per chiarezza si preferisca `switch` o `if` in casi complessi.

```bash
let n = number(0)
let descr = n > number(0) ? text("positivo") : n < number(0) ? text("negativo") : text("zero")
```

---

## 5. Espressioni oggetto

### 5.1 Sintassi selettore → membro

L'operatore `->` accede a una proprieta o invoca un metodo su un oggetto. La sintassi generale e:

```
oggetto -> proprieta
oggetto -> metodo(argomenti)
```

Dove `oggetto` e un valore (binding, percorso, risultato di espressione) e `proprieta` o `metodo` e il nome del membro da accedere.

```bash
let f = path("/etc/hosts")
f -> name
# risultato: "hosts"

f -> size
# risultato: numero (dimensione in byte)

f -> read_text(10)
# risultato: prime 10 righe del file come stringa
```

L'operatore `->` puo essere concatenato:

```bash
path("/etc") -> children() -> first() -> name
```

Nella shell interattiva, il tab completion dopo `->` mostra le proprieta e i metodi disponibili per il tipo dell'oggetto.

### 5.2 Tipi di oggetto filesystem

oosh riconosce i seguenti tipi di oggetto filesystem, usati come valore della proprieta `type`:

| Tipo | Descrizione |
|------|-------------|
| `file` | File regolare |
| `directory` | Directory |
| `device` | File di dispositivo (block o char) |
| `mount` | Punto di mount |
| `path` | Percorso generico (tipo non ancora determinato o inesistente) |
| `unknown` | Tipo non riconoscibile |

Il costruttore `path("...")` crea un oggetto che rappresenta il percorso specificato. La proprieta `type` riflette il tipo reale rilevato a runtime.

```bash
let p = path("/etc/hosts")
p -> type
# file

let d = path("/etc")
d -> type
# directory

let np = path("/percorso/inesistente")
np -> exists
# false
np -> type
# path (o unknown)
```

### 5.3 Proprieta filesystem

Le seguenti proprieta sono disponibili su tutti gli oggetti filesystem:

| Proprieta | Tipo restituito | Descrizione |
|-----------|-----------------|-------------|
| `type` | string | Tipo dell'oggetto (`file`, `directory`, ecc.) |
| `path` | string | Percorso assoluto completo |
| `name` | string | Nome (ultima componente del percorso) |
| `exists` | bool | `true` se il percorso esiste nel filesystem |
| `size` | number | Dimensione in byte |
| `hidden` | bool | `true` se il nome inizia con `.` |
| `readable` | bool | `true` se il processo ha permesso di lettura |
| `writable` | bool | `true` se il processo ha permesso di scrittura |

Esempi:

```bash
let f = path("/etc/passwd")
f -> name
# passwd

f -> hidden
# false

f -> readable
# true (dipende dai permessi reali)

f -> size
# numero di byte

let nascosto = path("/home/utente/.bashrc")
nascosto -> hidden
# true
```

### 5.4 Metodi filesystem

#### children()

Disponibile sugli oggetti `directory`. Restituisce una lista di oggetti filesystem che rappresentano il contenuto della directory.

```bash
let dir = path("/etc")
let contenuto = dir -> children()
contenuto |> count()
# numero di voci nella directory

contenuto |> where(type == "file") |> sort(name asc)
```

#### read_text(limit)

Disponibile sugli oggetti `file`. Legge il contenuto testuale del file. Il parametro `limit` (opzionale) specifica il numero massimo di righe da leggere.

```bash
let f = path("/etc/hosts")
f -> read_text()
# intero contenuto come stringa

f -> read_text(5)
# prime 5 righe
```

#### read_json()

Disponibile sugli oggetti `file`. Legge il contenuto del file e lo interpreta come JSON, restituendo un valore tipizzato (map, list, ecc.).

```bash
let conf = path("config.json")
let dati = conf -> read_json()
dati -> get("chiave")
```

#### write_json(binding)

Disponibile sugli oggetti `file`. Serializza il valore `binding` come JSON e lo scrive nel file.

```bash
let conf = path("output.json")
let dati = map("nome", text("oosh"), "versione", number(1))
conf -> write_json(dati)
```

#### parent()

Restituisce l'oggetto filesystem che rappresenta la directory padre del percorso corrente.

```bash
let f = path("/etc/hosts")
f -> parent()
# oggetto directory per /etc

f -> parent() -> name
# etc
```

#### describe()

Stampa una descrizione leggibile dell'oggetto con tutte le proprieta principali. Utile per l'esplorazione interattiva.

```bash
path("/etc/hosts") -> describe()
# stampa tipo, percorso, dimensione, permessi, ecc.
```

#### print(…)

Disponibile su tutti i valori. Stampa il valore. Se il ricevitore e una stringa, accetta un formato printf-style.

```bash
let s = text("Ciao %s, hai %d messaggi")
s -> print("Mario", 5)
# stampa: Ciao Mario, hai 5 messaggi

let n = number(42)
n -> print()
# stampa: 42
```

---

## 6. Tipi di valore e costruttori

### 6.1 text / string

`text("...")` e `string("...")` creano un valore stringa tipizzato. Le due forme sono equivalenti.

```bash
let s1 = text("Ciao mondo")
let s2 = string("altro testo")

s1 -> print()
# stampa: Ciao mondo
```

Le stringhe tipizzate supportano l'operatore `+` per la concatenazione e possono essere passate alle pipeline oggetti:

```bash
let frase = text("  testo con spazi  ")
frase |> trim() |> render()
# stampa: testo con spazi

text("uno:due:tre") |> split(":") |> count()
# 3
```

### 6.2 number

`number(n)` crea un valore numerico. Accetta un intero o un decimale.

```bash
let intero   = number(42)
let decimale = number(3.14)
let negativo = number(-7)
```

I valori numerici supportano gli operatori aritmetici `+`, `-`, `*`, `/` e i confronti `==`, `!=`, `<`, `>`, `<=`, `>=`.

```bash
let a = number(10)
let b = number(4)
let c = a * b + number(2)
c -> print()
# stampa: 42
```

### 6.3 bool e letterali booleani

`bool(true)` e `bool(false)` creano valori booleani. I letterali `true` e `false` possono essere usati direttamente come value expression senza il costruttore `bool(...)`.

```bash
let vero  = true
let falso = false
let esplicito = bool(true)
```

I valori booleani sono usati come risultato dei confronti e come condizioni nei costrutti di controllo del flusso.

La **truthiness** di un valore in un contesto condizionale segue queste regole:
- `bool(false)` e falsy
- `number(0)` e falsy
- `text("")` (stringa vuota) e falsy
- Tutti gli altri valori tipizzati sono truthy
- Un exit status 0 e truthy (successo del comando)
- Un exit status non zero e falsy

```bash
let flag = true
if flag
then
  echo "flag e vero"
fi
```

### 6.4 list / array

`list(v1, v2, ...)` e `array(v1, v2, ...)` creano una lista eterogenea. I due costruttori sono equivalenti. Gli elementi possono essere di qualsiasi tipo.

```bash
let numeri  = list(number(1), number(2), number(3))
let mista   = list(text("a"), number(1), true)
let vuota   = list()
```

Le liste sono la sorgente primaria per le pipeline oggetti:

```bash
list(number(5), number(1), number(3)) |> sort(asc) |> render()
# 1
# 3
# 5
```

### 6.5 map

`map("k1", v1, "k2", v2, ...)` crea una mappa chiave-valore. Le chiavi devono essere stringhe, i valori possono essere di qualsiasi tipo.

```bash
let persona = map(
  "nome",   text("Mario"),
  "eta",    number(30),
  "attivo", true
)
```

Le mappe possono essere lette con `->` (accesso alla proprieta per chiave) oppure serializzate in JSON:

```bash
persona -> get("nome")
# Mario

persona |> to_json() |> render()
# {"nome":"Mario","eta":30,"attivo":true}
```

### 6.6 capture e capture_lines

`capture("cmd ...")` esegue il comando specificato e restituisce l'intero stdout come valore `text` (stringa tipizzata), con il newline finale rimosso.

```bash
let versione = capture("uname -r")
versione -> print()
# stampa il kernel corrente, es: 6.1.0

let data_oggi = capture("date +%Y-%m-%d")
```

`capture_lines("cmd ...")` esegue il comando e restituisce lo stdout come `list` di righe (una stringa per riga).

```bash
let processi = capture_lines("ps aux")
processi |> count()
# numero di righe

processi |> grep("nginx") |> render()
# filtra le righe che contengono "nginx"
```

Differenze rispetto a `$(...)`:
- `$(...)` e una variabile shell (stringa grezza)
- `capture(...)` restituisce un valore `text` tipizzato usabile in pipeline oggetti
- `capture_lines(...)` restituisce direttamente una `list`, evitando uno split manuale

### 6.7 Block literal

Un block literal e una funzione anonima come valore di prima classe. La sintassi e:

```
[:param1 :param2 | corpo]
```

I parametri sono prefissati con `:`. Il corpo e una sequenza di espressioni. Il valore del block e il risultato dell'ultima espressione.

```bash
let raddoppia = [:x | x * number(2)]
let triplica  = [:x | x * number(3)]
```

I block possono essere passati a stage come `each` e `reduce`, o a metodi che accettano un block come argomento:

```bash
list(number(1), number(2), number(3)) |> each([:it | it * number(2)])
# 2, 4, 6

list(number(1), number(2), number(3)) |> reduce(number(0), [:acc :it | acc + it])
# 6
```

#### Variabili locali dentro un block

Dentro un block si puo usare `local nome = expr` per creare binding con scope limitato al block stesso. La visibilita e quella del block corrente, non del contesto chiamante.

```bash
let calcola = [:x |
  local quadrato = x * x
  local cubo = quadrato * x
  cubo
]
```

#### Proprieta dei block

I block hanno le seguenti proprieta accessibili con `->`:

| Proprieta | Descrizione |
|-----------|-------------|
| `type` | Sempre `"block"` |
| `arity` | Numero di parametri |
| `source` | Stringa con il sorgente del block |
| `body` | Rappresentazione interna del corpo |

```bash
let b = [:a :b | a + b]
b -> arity
# 2
b -> type
# block
```

### 6.8 env, proc, shell

#### env()

`env()` restituisce una mappa dell'intero ambiente del processo corrente. `env("CHIAVE")` restituisce il valore della singola variabile di ambiente come stringa tipizzata.

```bash
let tutto_env = env()
tutto_env |> to_json() |> render()

let home = env("HOME")
home -> print()
# stampa la home directory

let path_val = env("PATH")
path_val |> split(":") |> count()
# numero di directory nel PATH
```

#### proc()

`proc()` restituisce una mappa con informazioni sul processo corrente:

| Chiave | Descrizione |
|--------|-------------|
| `pid` | PID del processo oosh corrente |
| `ppid` | PID del processo padre |
| `cwd` | Directory di lavoro corrente |
| `host` | Nome host |
| `os` | Sistema operativo |

```bash
let info = proc()
info -> get("pid") -> print()
info -> get("cwd") -> print()
info -> get("host") -> print()
```

#### shell()

`shell()` restituisce una mappa con informazioni sullo stato runtime della shell corrente:

| Chiave | Descrizione |
|--------|-------------|
| `alias` | Lista degli alias definiti |
| `binding` | Lista dei binding `let` attivi |
| `plugin` | Lista dei plugin caricati |
| `job` | Lista dei job attivi |

```bash
let stato = shell()
stato -> get("plugin") -> render()
stato -> get("binding") -> render()
```

---

## 7. Pipeline oggetti (|>)

### 7.1 Concetto e sintassi

L'operatore `|>` e il cuore del sistema di trasformazione strutturata di oosh. Prende un **valore sorgente** sul lato sinistro e lo passa a una serie di **stage** separati da `|>`.

```
sorgente |> stage1 |> stage2 |> ... |> stageN
```

Il valore prodotto da ogni stage diventa l'ingresso dello stage successivo. Il risultato finale e il valore prodotto dall'ultimo stage.

Una pipeline oggetti non e una pipeline testuale: non crea processi figli e non si basa su pipe Unix. Lavora interamente in-process su valori tipizzati.

```bash
# Esempio completo: filtra, ordina, prende i primi 3, stampa il nome
path("/etc") -> children()
  |> where(type == "file")
  |> sort(name asc)
  |> take(3)
  |> each(:it | it -> name)
  |> render()
```

### 7.2 Bridge shell/object

Quando un **comando esterno** (o una pipeline shell) appare come sorgente prima di `|>`, oosh cattura automaticamente lo stdout del processo e lo tratta come una stringa, convertendola in ingresso per la pipeline oggetti.

```bash
ls /usr/bin |> grep("ssh") |> count()
# conta i file in /usr/bin che contengono "ssh" nel nome

ps aux |> lines() |> grep("python") |> count()
```

In questo contesto, il bridge si comporta come se si fosse usato `capture_lines(...)` sulla pipeline shell, producendo una lista di righe su cui applicare gli stage. Lo stage `lines()` e spesso il primo stage usato dopo un comando esterno per spezzare lo stdout in righe.

Nota: la sorgente del bridge deve essere un comando esterno, non un built-in. I built-in non possono stare nella posizione sorgente di una pipeline shell (`builtin | wc -l` da errore, vedi sezione 8.5).

### 7.3 Riferimento completo degli stage

#### where(condizione)

Filtra gli elementi di una lista. Accetta due forme:

**Forma selettore**: `where(proprieta == valore)` o `where(proprieta != valore)`. Confronta la proprieta dell'elemento con il valore dato.

```bash
path("/etc") -> children()
  |> where(type == "file")

path("/home/utente") -> children()
  |> where(hidden == false)
```

**Forma block**: `where([:it | condizione])`. Il block riceve l'elemento e deve restituire un bool.

```bash
list(number(1), number(2), number(3), number(4))
  |> where([:it | it > number(2)])
# restituisce: 3, 4
```

Limite attuale: nella forma selettore sono supportati solo `==` e `!=`. Per confronti `<`, `>`, `<=`, `>=` usare la forma block.

#### sort(proprieta asc|desc)

Ordina una lista in base alla proprieta specificata. La direzione puo essere `asc` (crescente) o `desc` (decrescente).

```bash
path("/tmp") -> children()
  |> sort(size desc)

path("/etc") -> children()
  |> sort(name asc)
```

Per liste di valori semplici (numeri, stringhe) la direzione puo essere specificata direttamente:

```bash
list(number(3), number(1), number(2))
  |> sort(asc)
```

#### take(n)

Restituisce i primi `n` elementi della lista.

```bash
path("/etc") -> children()
  |> sort(name asc)
  |> take(5)
```

Se la lista ha meno di `n` elementi, restituisce tutti gli elementi disponibili senza errore.

#### first()

Restituisce il primo elemento della lista come valore singolo (non come lista di un elemento).

```bash
path("/etc") -> children()
  |> sort(name asc)
  |> first()
  |> name
```

#### count()

Conta gli elementi della lista. Se applicato a un valore singolo (non lista), restituisce sempre `1`.

```bash
path("/etc") -> children() |> count()
# numero di voci nella directory

capture_lines("ps aux") |> grep("python") |> count()
```

#### lines()

Divide una stringa in una lista di righe, usando il newline come separatore. Usato spesso come primo stage dopo un bridge shell/object.

```bash
capture("cat /etc/hosts") |> lines() |> count()

ls /usr/bin |> lines() |> grep("git") |> sort(asc)
```

#### trim()

Su una stringa: rimuove gli spazi (e altri whitespace) all'inizio e alla fine.

Su una lista: applica `trim()` a ogni elemento stringa della lista.

```bash
text("  ciao  ") |> trim() |> render()
# stampa: ciao

capture_lines("cat /etc/hosts") |> trim() |> grep("localhost")
```

#### split(sep?)

Divide una stringa in una lista di parti usando il separatore `sep`. Se `sep` non e specificato, divide sullo whitespace (spazi, tab).

```bash
text("a:b:c") |> split(":") |> count()
# 3

text("uno due tre") |> split() |> render()
# uno
# due
# tre

env("PATH") |> split(":") |> sort(asc) |> render()
```

#### join(sep?)

Unisce una lista di stringhe in una singola stringa, usando `sep` come separatore. Se `sep` non e specificato, concatena senza separatore.

```bash
list(text("a"), text("b"), text("c")) |> join(":") |> render()
# a:b:c

list(text("uno"), text("due")) |> join() |> render()
# unoddue
```

#### reduce(init, block) / reduce(block)

Applica il block di accumulazione a ogni elemento della lista, accumulando un risultato. La forma con `init` usa il valore fornito come accumulatore iniziale. La forma senza `init` usa il primo elemento come accumulatore iniziale.

Il block riceve due parametri: l'accumulatore e l'elemento corrente. Deve restituire il nuovo accumulatore.

```bash
list(number(1), number(2), number(3), number(4))
  |> reduce(number(0), [:acc :it | acc + it])
# 10 (somma)

list(number(1), number(2), number(3))
  |> reduce([:acc :it | acc * it])
# 6 (prodotto, usando il primo elemento come accumulatore)
```

Esempio: costruire una stringa da una lista:

```bash
list(text("uno"), text("due"), text("tre"))
  |> reduce(text(""), [:acc :it | acc + text(" ") + it])
  |> trim()
  |> render()
# uno due tre
```

#### each(proprieta) / each(metodo()) / each(block)

Trasforma ogni elemento della lista applicando una proprieta, un metodo o un block.

**Forma proprieta**: estrae la proprieta indicata da ogni elemento.

```bash
path("/etc") -> children() |> each(name)
# lista dei nomi dei file
```

**Forma metodo**: chiama il metodo indicato su ogni elemento.

```bash
path("/etc") -> children() |> each(describe())
```

**Forma block**: applica il block a ogni elemento.

```bash
list(number(1), number(2), number(3))
  |> each([:it | it * number(2)])
# 2, 4, 6
```

#### render()

Forza la conversione del valore corrente in testo e lo stampa sullo stdout. Se il valore e una lista, ogni elemento viene stampato su una riga separata. Termina sempre la pipeline stampando il risultato.

```bash
list(text("a"), text("b"), text("c")) |> render()
# a
# b
# c

text("ciao") |> render()
# ciao
```

#### from_json()

Interpreta una stringa come JSON e restituisce il valore tipizzato corrispondente (map, list, number, text, bool).

```bash
text('{"nome":"Mario","eta":30}')
  |> from_json()
  |> get("nome")
  |> render()
# Mario
```

#### to_json()

Serializza il valore corrente in una stringa JSON.

```bash
map("a", number(1), "b", text("due"))
  |> to_json()
  |> render()
# {"a":1,"b":"due"}

list(number(1), number(2), number(3))
  |> to_json()
  |> render()
# [1,2,3]
```

#### grep("pattern")

Filtra le righe o gli elementi di una lista che contengono il pattern come sottostringa. La corrispondenza e case-sensitive.

```bash
capture_lines("ps aux") |> grep("nginx") |> render()

path("/etc") -> children()
  |> each(name)
  |> grep("conf")
  |> render()
```

Per corrispondenze piu complesse (case-insensitive, regex avanzate) usare `where(block)`.

---

## 8. Pipeline shell e redirection

### 8.1 Operatore |

L'operatore `|` connette lo stdout di un processo allo stdin del successivo, creando una pipeline testuale tra processi. E la pipeline classica Unix.

```bash
ls -la /etc | grep "^d" | wc -l
# conta le directory in /etc

cat /var/log/syslog | grep "ERROR" | tail -50
```

I comandi in una pipeline shell vengono eseguiti in parallelo come processi figli separati, connessi da pipe del sistema operativo.

### 8.2 Redirection

oosh supporta la redirection completa in stile POSIX.

#### Redirection in input (<)

Redirige lo stdin del comando da un file:

```bash
sort < lista.txt
wc -l < documento.txt
```

#### Redirection in output (>)

Redirige lo stdout verso un file, sovrascrivendolo:

```bash
ls /tmp > lista.txt
echo "nuovo contenuto" > file.txt
```

#### Append (>>)

Redirige lo stdout verso un file, aggiungendo alla fine:

```bash
echo "riga aggiuntiva" >> log.txt
date >> timestamp.log
```

#### Redirection stderr (2>)

Redirige lo stderr verso un file:

```bash
comando_pericoloso 2> errori.log
ls /percorso/inesistente 2> /dev/null
```

#### Redirection stderr su stdout (2>&1)

Unisce stderr e stdout nello stesso flusso:

```bash
comando 2>&1 | tee tutto.log
ls /tmp /inesistente > tutto.txt 2>&1
```

#### Redirection file descriptor arbitrari (n>, n>&m, n<&m)

Disponibile solo su sistemi POSIX (Linux, macOS, BSD):

```bash
comando 3> file_fd3.txt
comando 4>&1
```

### 8.3 Heredoc

L'heredoc permette di fornire un blocco di testo multilinea come stdin di un comando.

#### Heredoc standard (<<EOF...EOF)

```bash
cat <<EOF
Riga uno
Riga due
Riga tre
EOF
```

Le espansioni di variabili e command substitution sono attive all'interno dell'heredoc:

```bash
nome="Mario"
cat <<EOF
Ciao $nome
La data e: $(date)
EOF
```

#### Heredoc con indentazione (<<-EOF...EOF)

Il carattere `-` dopo `<<` fa si che i tab iniziali di ogni riga siano rimossi, utile per l'indentazione nei script:

```bash
if true
then
  cat <<-EOF
    Questa riga e indentata nel codice
    ma non nell'output
  EOF
fi
```

### 8.4 Background (&)

Aggiungere `&` alla fine di un comando o pipeline lo esegue in background, restituendo immediatamente il controllo al prompt:

```bash
sleep 120 &
# [1] 12345

rsync -av /sorgente /destinazione &
# [2] 12346
```

Il job viene aggiunto alla lista dei job (vedi sezione 14). oosh segnala il completamento del job quando diventa rilevabile.

### 8.5 Limiti dei built-in nelle pipeline shell

I comandi built-in di oosh non possono essere usati nelle posizioni intermedie o finali di una pipeline shell multi-stage. Il motivo e che i built-in non sono processi separati e non hanno uno stdin/stdout connettibile a pipe del sistema operativo.

```bash
# ERRORE: cd non e un processo, non puo stare in una pipeline
ls | cd /tmp

# ERRORE: let non puo ricevere dati da pipe
cat file.txt | let contenuto
```

Per lavorare con l'output di un comando in un built-in, usare la command substitution o le pipeline oggetti:

```bash
# Corretto: cattura l'output e lo usa in un let
let righe = capture_lines("ls /tmp")

# Corretto: pipeline oggetti
ls /tmp |> grep("test") |> count()
```

---

## 9. Controllo di flusso

### 9.1 if / elif / else / fi

La struttura `if` valuta una condizione e, a seconda del risultato, esegue uno o l'altro ramo.

La condizione puo essere:
1. Una value expression (viene valutata per truthiness)
2. Un comando shell (viene valutato per exit status: 0 = successo = true)

**Forma su una riga** (con `;`):

```bash
if test -f /etc/hosts ; then echo "esiste" ; fi
```

**Forma multilinea** (con newline):

```bash
if test -f /etc/hosts
then
  echo "il file esiste"
fi
```

**Con else**:

```bash
let x = number(10)
if x > number(5)
then
  echo "x e maggiore di 5"
else
  echo "x e al massimo 5"
fi
```

**Con elif**:

```bash
let voto = number(75)
if voto >= number(90)
then
  echo "Ottimo"
elif voto >= number(70)
then
  echo "Buono"
elif voto >= number(60)
then
  echo "Sufficiente"
else
  echo "Insufficiente"
fi
```

### 9.2 while / done

Esegue il corpo ripetutamente finche la condizione e vera.

```bash
let i = number(0)
while i < number(5)
do
  i -> print()
  let i = i + number(1)
done
```

Con un comando come condizione:

```bash
while test -f /tmp/lock
do
  echo "In attesa..."
  sleep 1
done
```

### 9.3 until / done

Esegue il corpo ripetutamente finche la condizione diventa vera (l'opposto di `while`).

```bash
let i = number(10)
until i == number(0)
do
  i -> print()
  let i = i - number(1)
done
```

### 9.4 for / in / do / done

Itera su una sorgente. La sorgente puo essere una lista tipizzata o un insieme di shell words.

**Iterazione su lista tipizzata**:

```bash
let nomi = list(text("Mario"), text("Luigi"), text("Giovanni"))
for nome in nomi
do
  echo "Ciao $nome"
done
```

**Iterazione su shell words** (espansione glob, array shell):

```bash
for file in *.txt
do
  echo "File: $file"
  wc -l $file
done
```

**Iterazione su output di comando**:

```bash
for dir in $(ls -d /etc/*/); do
  echo "Directory: $dir"
done
```

**Iterazione su range numerico esplicito**:

```bash
for i in 1 2 3 4 5
do
  echo "Iterazione $i"
done
```

### 9.5 break e continue

`break [n]` interrompe il loop corrente. Se `n` e specificato, interrompe gli `n` loop piu interni.

`continue [n]` salta alla prossima iterazione. Se `n` e specificato, agisce sugli `n` loop piu interni.

```bash
let i = number(0)
while i < number(10)
do
  let i = i + number(1)
  if i == number(5)
  then
    continue
  fi
  if i == number(8)
  then
    break
  fi
  i -> print()
done
# stampa: 1 2 3 4 6 7
```

**break con argomento numerico**:

```bash
for i in 1 2 3
do
  for j in a b c
  do
    if test "$i" = "2" -a "$j" = "b"
    then
      break 2
    fi
    echo "$i-$j"
  done
done
# interrompe entrambi i loop quando i=2 e j=b
```

### 9.6 switch / case / default / endswitch

`switch` confronta un valore con una serie di casi. Ogni caso viene eseguito se il valore corrisponde. `default` cattura tutti i casi non corrispondenti.

```bash
let giorno = text("lunedi")

switch giorno
case "lunedi"
then
  echo "Inizio settimana"
case "venerdi"
then
  echo "Fine settimana lavorativa"
case "sabato"
case "domenica"
then
  echo "Weekend"
default
then
  echo "Giorno intermedio"
endswitch
```

### 9.7 case … in … esac

La forma `case ... in ... esac` e la sintassi POSIX classica per pattern matching. Supporta glob pattern nelle espressioni.

```bash
case $variabile in
  "valore_esatto")
    echo "corrisponde esattamente"
    ;;
  pattern*)
    echo "inizia con 'pattern'"
    ;;
  *.txt|*.md)
    echo "file testo o markdown"
    ;;
  *)
    echo "default: nessun pattern corrisponde"
    ;;
esac
```

Esempio pratico:

```bash
case $(uname -s) in
  Linux)
    echo "Sistema Linux"
    ;;
  Darwin)
    echo "macOS"
    ;;
  *)
    echo "Sistema sconosciuto"
    ;;
esac
```

### 9.8 return

`return [espressione]` termina la funzione corrente e, se specificato, restituisce il valore dell'espressione come risultato della funzione.

```bash
function massimo(a, b) do
  if a > b
  then
    return a
  fi
  return b
endfunction

let m = massimo 10 7
m -> print()
# 10
```

Senza argomento, `return` termina la funzione con il valore prodotto dall'ultima espressione eseguita.

```bash
function saluta(nome) do
  text("Ciao ") + text($nome)
endfunction
```

### 9.9 Ternario

Il ternario e gia descritto nella sezione 4.6. Puo essere usato in qualsiasi posizione dove e attesa una value expression, anche all'interno di altri costrutti:

```bash
for file in *.txt
do
  let tipo = path($file) -> size > number(1000) ? text("grande") : text("piccolo")
  echo "$file e $tipo"
done
```

---

## 10. Funzioni

### 10.1 Definizione e chiamata

Le funzioni si definiscono con la parola chiave `function`:

```bash
function saluta(nome) do
  echo "Ciao, $nome!"
endfunction
```

La chiamata usa la sintassi shell-style: nome della funzione seguito dagli argomenti separati da spazi:

```bash
saluta Mario
# stampa: Ciao, Mario!
```

Le funzioni possono accettare piu parametri:

```bash
function somma(a, b) do
  let risultato = number($a) + number($b)
  risultato -> print()
endfunction

somma 10 32
# stampa: 42
```

Per visualizzare tutte le funzioni definite:

```bash
function
# oppure
functions
```

Per visualizzare il corpo di una funzione specifica:

```bash
function saluta
```

### 10.2 Parametri e scope

Ogni parametro della funzione e disponibile:
1. Come variabile shell con il nome del parametro (accessibile con `$nome`)
2. Come binding tipizzato stringa (accessibile come value expression con il nome diretto)

Al termine della chiamata, le variabili shell e i binding vengono ripristinati al loro stato prima della chiamata. Le funzioni non inquinano il contesto chiamante.

```bash
function mostra_info(percorso) do
  let p = path($percorso)
  echo "Percorso: $percorso"
  echo "Tipo: $(p -> type)"
  echo "Esiste: $(p -> exists)"
endfunction

mostra_info /etc/hosts
```

Le variabili create con `set` dentro una funzione sono locali alla chiamata:

```bash
function test_scope() do
  set VARIABILE_LOCALE "solo qui"
  echo $VARIABILE_LOCALE
endfunction

test_scope
echo "$VARIABILE_LOCALE"
# stringa vuota: la variabile non e visibile fuori dalla funzione
```

### 10.3 Override di built-in e uso di builtin

E possibile ridefinire qualsiasi built-in creando una funzione con lo stesso nome:

```bash
function pwd() do
  echo "Directory corrente: $(builtin pwd)"
endfunction

pwd
# stampa: Directory corrente: /home/utente
```

Per chiamare il built-in originale anche quando esiste una funzione omonima, usare il comando `builtin`:

```bash
builtin pwd
# chiama il built-in pwd originale, ignorando la funzione

builtin cd /tmp
builtin echo "testo"
```

`builtin` e utile anche per distinguere esplicitamente nel codice quando si intende usare la funzionalita nativa della shell, non un eventuale override.

---

## 11. Classi

### 11.1 Definizione

Una classe si definisce con la parola chiave `class`:

```bash
class Persona do
  property nome = text("sconosciuto")
  property eta  = number(0)

  method saluta = [:self |
    text("Ciao, sono ") + self -> nome
  ]
endclass
```

La definizione specifica:
- `property nome = espressione` — proprieta con valore iniziale
- `method nome = [:self :arg | ...]` — metodo; il primo parametro e sempre `self`

### 11.2 Proprieta e metodi

Le proprieta sono accessibili con `->` sull'istanza:

```bash
let p = Persona()
p -> nome
# sconosciuto

p -> eta
# 0
```

I metodi vengono chiamati allo stesso modo:

```bash
p -> saluta()
# Ciao, sono sconosciuto
```

Per modificare una proprieta si usa `-> set("campo", valore)`. Il metodo restituisce l'istanza aggiornata:

```bash
let p2 = p -> set("nome", text("Mario")) -> set("eta", number(30))
p2 -> nome
# Mario
p2 -> eta
# 30
```

### 11.3 Istanziazione e metodo init

Per creare un'istanza si usa il nome della classe come funzione:

```bash
let p = Persona()
```

Se la classe definisce un metodo `init`, viene chiamato automaticamente durante l'istanziazione con gli argomenti forniti:

```bash
class Punto do
  property x = number(0)
  property y = number(0)

  method init = [:self :x :y |
    self -> set("x", x) -> set("y", y)
  ]

  method distanza_quadrata_origine = [:self |
    (self -> x * self -> x) + (self -> y * self -> y)
  ]
endclass

let p = Punto(number(3), number(4))
p -> x
# 3
p -> y
# 4
p -> distanza_quadrata_origine()
# 25
```

### 11.4 Ereditarieta multipla

Una classe puo estendere una o piu classi base:

```bash
class Animale do
  property nome = text("animale")
  method voce = [:self | text("...") ]
endclass

class Cane extends Animale do
  method voce = [:self | text("Bau!") ]
  method abbaia = [:self | self -> voce() -> print() ]
endclass

class Gatto extends Animale do
  method voce = [:self | text("Miao!") ]
endclass

class CaneGatto extends Cane, Gatto do
  # eredita da Cane e Gatto
endclass
```

La risoluzione dei metodi cerca prima nella classe corrente, poi nelle basi da sinistra a destra. In caso di conflitto tra due basi, vince quella specificata per prima nell'elenco `extends`.

```bash
let cg = CaneGatto()
cg -> voce()
# Bau! (da Cane, prima nell'elenco extends)
```

### 11.5 Introspezione

Il metodo `-> isa("NomeClasse")` verifica se un'istanza e di una certa classe o ne discende:

```bash
let d = Cane()
d -> isa("Cane")
# true
d -> isa("Animale")
# true (per ereditarieta)
d -> isa("Gatto")
# false
```

Per visualizzare tutte le classi definite:

```bash
class
# oppure
classes
```

Per visualizzare la definizione di una classe specifica:

```bash
class Persona
```

---

## 12. Estensioni (extend)

Il comando `extend` permette di aggiungere proprieta e metodi a tipi esistenti, senza modificarne il codice sorgente. E il meccanismo di monkey-patching tipizzato di oosh.

**Sintassi**:

```bash
extend target property nome = [:it | espressione]
extend target method nome = [:it :arg | espressione]
```

Il primo parametro e sempre `:it` (il ricevitore), seguito dagli eventuali argomenti per i metodi.

**Target supportati**:

| Target | Descrizione |
|--------|-------------|
| `any` | Qualsiasi valore |
| `string` | Valori text/string |
| `number` | Valori numerici |
| `bool` | Valori booleani |
| `object` | Oggetti generici |
| `block` | Block literal |
| `list` | Liste |
| `path` | Oggetti path generico |
| `file` | File |
| `directory` | Directory |
| `device` | Device |
| `mount` | Mount point |

**Esempi**:

```bash
# Aggiunge una proprieta "doppio" ai numeri
extend number property doppio = [:it |
  it * number(2)
]

number(21) -> doppio
# 42

# Aggiunge un metodo "ripeti" alle stringhe
extend string method ripeti = [:it :n |
  list() |> reduce(number($n), [:acc :_ | acc + it])
]

# Aggiunge una proprieta alle directory
extend directory property file_count = [:it |
  it -> children() |> where(type == "file") |> count()
]

path("/etc") -> file_count
# numero di file regolari in /etc

# Aggiunge una proprieta a tutti i valori
extend any property tipo_stringa = [:it |
  text(it -> type)
]
```

**Priorita**: il core ha precedenza. `extend` entra in gioco solo per proprieta e metodi che non esistono nel core. Non e possibile sovrascrivere proprieta native con `extend`.

Per visualizzare tutte le estensioni definite:

```bash
extend
```

---

## 13. Riferimento comandi built-in

I built-in sono comandi implementati direttamente nella shell, non come processi esterni. Vengono eseguiti nel contesto corrente della shell e possono modificare lo stato interno (variabili, directory corrente, ecc.).

### alias

Definisce un alias che sostituisce un comando. La sintassi e `alias nome="comando"`.

```bash
alias ls="ls --color=auto"
alias ll="ls -la"
alias ..="cd .."
alias grep="grep --color=auto"
```

Senza argomenti, lista tutti gli alias definiti:

```bash
alias
```

### bg

Riprende in background un job stoppato (vedi sezione 14.4).

```bash
bg        # riprende il job piu recente
bg %2     # riprende il job numero 2
```

### break

Interrompe il loop corrente. Con argomento numerico, interrompe gli `n` loop piu interni:

```bash
break     # interrompe il loop interno
break 2   # interrompe i 2 loop piu interni
```

### builtin

Chiama il built-in originale bypassando eventuali funzioni omonime:

```bash
builtin pwd
builtin cd /tmp
builtin echo "testo"
```

Utile quando si ridefinisce un built-in con una funzione e si vuole comunque accedere all'implementazione nativa.

### call

Invoca un metodo su un oggetto filesystem dal contesto shell:

```bash
call /etc/hosts read_text
call /etc describe
call /tmp children
```

### cd

Cambia la directory di lavoro corrente.

```bash
cd /tmp           # vai a /tmp
cd                # vai alla HOME
cd ~              # equivalente a cd
cd -              # torna alla directory precedente (usa OLDPWD)
cd percorso/rel   # percorso relativo alla directory corrente
```

Aggiorna automaticamente le variabili `PWD` e `OLDPWD`.

### class / classes

Senza argomenti, lista tutte le classi definite. Con un nome, mostra la definizione della classe:

```bash
class               # lista tutte le classi
class Persona       # mostra la definizione di Persona
classes             # alias di class
classes Punto
```

### continue

Avanza alla prossima iterazione del loop corrente:

```bash
continue      # prossima iterazione del loop interno
continue 2    # agisce sui 2 loop piu interni
```

### eval

Valuta la stringa fornita come codice oosh nel contesto corrente:

```bash
eval "echo ciao"
eval "let x = number(42)"

# Utile per costruire comandi dinamicamente
let cmd = text("echo ") + text("dinamico")
eval "$cmd"
```

### exec

Sostituisce il processo shell corrente con il comando specificato (non crea un processo figlio). Dopo `exec`, il processo oosh non esiste piu:

```bash
exec /bin/bash    # sostituisce oosh con bash
exec env -i sh    # shell pulita senza ambiente
```

### exit / quit

Esce dalla shell con il codice di uscita specificato (default 0):

```bash
exit        # esce con status 0
exit 1      # esce con status 1
exit 42     # esce con status 42
quit        # alias per exit
```

### export

Esporta una variabile all'ambiente dei processi figli:

```bash
export PATH="/usr/local/bin:$PATH"
export EDITOR=vim
export VARIABILE valore
```

Senza argomenti, lista le variabili esportate.

### extend

Gestisce le estensioni (vedi sezione 12). Senza argomenti, lista tutte le estensioni definite:

```bash
extend
extend string property maiuscolo = [:it | ...]
```

### false

Esce con status 1 (fallimento). Utile nelle condizioni e nei test:

```bash
false
echo $?    # 1
```

### fg

Porta un job in foreground, cedendogli il terminale (vedi sezione 14.3):

```bash
fg        # job piu recente
fg %1     # job numero 1
```

### function / functions

Senza argomenti, lista le funzioni definite. Con un nome, mostra il corpo:

```bash
function               # lista tutte le funzioni
function saluta        # mostra il corpo di "saluta"
functions              # alias di function
```

### get

Legge una proprieta di un oggetto filesystem:

```bash
get /etc/hosts name
get /tmp size
get /etc type
```

### help

Mostra l'aiuto generale della shell o di un comando specifico:

```bash
help
help cd
help let
```

### history

Mostra la history dei comandi della sessione corrente:

```bash
history
```

### inspect

Stampa tutte le proprieta di un oggetto filesystem in modo leggibile:

```bash
inspect /etc/hosts
inspect /tmp
inspect /dev/null
```

### jobs

Lista i job attivi, stoppati e completati con PID e PGID:

```bash
jobs
# [1]  Running    sleep 100  (pid: 1234, pgid: 1234)
# [2]  Stopped    vim file   (pid: 5678, pgid: 5678)
```

### let

Crea un binding tipizzato. Senza argomenti, lista tutti i binding attivi:

```bash
let x = number(42)
let nome = text("Mario")
let flag = true
let lista = list(number(1), number(2), number(3))

let          # lista tutti i binding attivi
```

### plugin

Gestisce i plugin della shell:

```bash
plugin load /percorso/plugin.so   # carica un plugin
plugin list                        # lista i plugin caricati
plugin info nome_plugin            # informazioni su un plugin
plugin disable nome_plugin         # disabilita un plugin
plugin enable nome_plugin          # riabilita un plugin
```

### prompt

Gestisce la configurazione del prompt:

```bash
prompt show                     # mostra la configurazione attuale
prompt load /percorso/conf      # carica una configurazione prompt
```

### pwd

Stampa la directory di lavoro corrente:

```bash
pwd
# /home/utente
```

### return

Termina la funzione corrente, opzionalmente con un valore:

```bash
return              # termina senza valore esplicito
return number(42)   # termina con il valore 42
return text("ok")   # termina con la stringa "ok"
```

### run

Esegue esplicitamente un comando esterno, bypassando alias e funzioni:

```bash
run ls -la
run /usr/bin/env python3 script.py
```

Utile quando un alias o una funzione hanno lo stesso nome di un eseguibile e si vuole chiamare direttamente l'eseguibile.

### set

Imposta una variabile shell. Senza argomenti, lista tutte le variabili:

```bash
set NOME valore
set NUMERO 42
set           # lista tutte le variabili shell
```

Le variabili impostate con `set` non sono esportate automaticamente ai processi figli; usare `export` per quello.

### source / .

Esegue un file nel contesto corrente della shell (non in un sottoprocesso):

```bash
source ~/.ooshrc
source /percorso/script.osh argomento1 argomento2
. ~/.profile
```

Gli argomenti opzionali sono disponibili come `$1`, `$2`, ecc. dentro il file sorgente.

### trap

Registra un comando da eseguire quando si verifica un evento:

```bash
trap "echo 'Uscita!'" EXIT     # esegui alla chiusura della shell
trap - EXIT                     # rimuove il trap EXIT
```

Limite attuale: e supportato solo l'evento `EXIT`.

### true

Esce con status 0 (successo). Utile nelle condizioni:

```bash
true
echo $?    # 0

while true
do
  # loop infinito controllato da break
  sleep 1
  break
done
```

### type

Mostra come oosh risolve un nome (in quale categoria cade):

```bash
type ls
# ls is /bin/ls (external)

type cd
# cd is a built-in

type ll
# ll is an alias for 'ls -la'

type saluta
# saluta is a function
```

### unalias

Rimuove un alias definito:

```bash
unalias ll
unalias ..
```

### unset

Rimuove una variabile shell e/o un binding tipizzato:

```bash
unset VARIABILE
unset nome_binding
```

### wait

Attende il completamento di un job in background:

```bash
wait        # attende il job piu recente
wait %2     # attende il job numero 2
```

Restituisce l'exit status del job atteso.

---

## 14. Job control

### 14.1 Background job (&)

Qualsiasi comando o pipeline puo essere eseguito in background aggiungendo `&` alla fine:

```bash
wget https://example.com/file.zip &
# [1] 12345

find / -name "*.log" -mtime +30 &
# [2] 12346
```

oosh assegna un numero progressivo al job (mostrato in `[n]`) e il PID del processo. Il controllo torna immediatamente al prompt.

### 14.2 Visualizzazione (jobs)

Il comando `jobs` mostra lo stato di tutti i job della sessione corrente:

```bash
jobs
# [1]  Running    wget https://example.com/file.zip  (pid: 12345, pgid: 12345)
# [2]  Stopped    vim documento.txt                   (pid: 12346, pgid: 12346)
# [3]  Done       sleep 5                             (pid: 12347, pgid: 12347)
```

Gli stati possibili sono:
- `Running`: il processo e in esecuzione in background
- `Stopped`: il processo e stato fermato (con Ctrl-Z o SIGSTOP)
- `Done`: il processo e terminato (rimane visibile finche non viene raccolto)

`shell()` restituisce anche la lista dei job attivi come valore tipizzato:

```bash
shell() -> get("job") |> render()
```

### 14.3 Foreground (fg)

`fg` porta un job in foreground, cedendogli il controllo del terminale:

```bash
fg         # porta il job piu recente in foreground
fg %1      # porta il job numero 1 in foreground
fg %2      # porta il job numero 2 in foreground
```

oosh usa `tcsetpgrp` per cedere correttamente il terminale al process group del job, e lo riprende al termine o quando il job viene stoppato di nuovo.

### 14.4 Background (bg)

`bg` riprende l'esecuzione in background di un job stoppato:

```bash
bg         # riprende il job piu recente stoppato
bg %2      # riprende il job numero 2
```

Il job riceve il segnale `SIGCONT` e torna in stato `Running`.

### 14.5 Ctrl-Z e process group

Premere `Ctrl-Z` su un job in foreground gli invia il segnale `SIGTSTP`, che lo ferma. Il job viene automaticamente aggiunto alla lista dei job con stato `Stopped`:

```
vim documento.txt
^Z
[1]  Stopped    vim documento.txt
```

oosh gestisce i process group correttamente: quando una pipeline e in foreground, tutti i processi della pipeline appartengono allo stesso process group (il PGID coincide con il PID del primo processo). Alla fermata, l'intero gruppo viene fermato, non solo il primo processo.

oosh rileva il segnale `WIFSTOPPED` nella risposta di `waitpid` e aggiunge automaticamente il job alla lista senza richiedere azioni esplicite dell'utente.

---

## 15. Editor di riga interattivo

### 15.1 Tasti supportati

L'editor di riga interattivo di oosh supporta i seguenti comandi da tastiera:

| Tasto | Azione |
|-------|--------|
| Freccia su | Comando precedente nella history |
| Freccia giu | Comando successivo nella history |
| Freccia sinistra | Muove il cursore a sinistra di un carattere |
| Freccia destra | Muove il cursore a destra di un carattere |
| `Ctrl-A` | Sposta il cursore all'inizio della riga |
| `Ctrl-E` | Sposta il cursore alla fine della riga |
| `Ctrl-C` | Annulla la riga corrente (se nessun job in foreground); interrompe il job in foreground |
| `Ctrl-D` | Esce dalla shell se la riga e vuota; cancella il carattere sotto il cursore altrimenti |
| `Ctrl-Z` | Ferma il job in foreground (POSIX) e lo aggiunge ai job stoppati |
| `Backspace` | Cancella il carattere immediatamente a sinistra del cursore |
| `Tab` | Attiva il completion contestuale (vedi sezione 16) |

### 15.2 Syntax highlighting

oosh evidenzia la riga di input in tempo reale, colorando i token in base al loro tipo:

| Categoria | Colore |
|-----------|--------|
| Comandi noti (built-in, funzioni, alias, eseguibili PATH) | Verde |
| Keyword (`if`, `then`, `while`, `function`, ecc.) | Grassetto |
| Stringhe (single e double quote) | Giallo |
| Commenti (da `#` a fine riga) | Grigio |
| Variabili (`$VAR`, `${VAR}`) | Ciano |

Il colore verde dei comandi indica che oosh ha risolto il nome con successo. Un comando non riconosciuto (non built-in, non in PATH, non funzione, non alias) viene mostrato senza colore speciale.

### 15.3 Autosuggestion

oosh mostra un suggerimento grigio dopo il cursore: e il suffisso della voce di history piu recente che inizia con il testo corrente nel buffer.

Esempio: se la history contiene `ls -la /home/utente` e l'utente scrive `ls`, il suggerimento mostra ` -la /home/utente` in grigio.

Per accettare il suggerimento:
- Freccia destra: accetta il suggerimento carattere per carattere
- `Ctrl-E`: accetta l'intero suggerimento

Se nessuna voce di history corrisponde, nessun suggerimento viene mostrato.

---

## 16. Tab completion

### 16.1 Completion per contesto

La pressione di `Tab` attiva il completion contestuale, che offre suggerimenti diversi a seconda di dove si trova il cursore nella riga di input.

**All'inizio della riga** (posizione del comando):

Il completion offre:
- Comandi built-in, con indicatore `(cmd)`
- Funzioni definite, con indicatore `(fn)`
- Alias definiti, con indicatore `(@)`
- Eseguibili trovati nel `PATH`

```
oosh> ls<Tab>
ls        (cmd)
lsblk     /usr/bin/lsblk
lscpu     /usr/bin/lscpu
lsof      /usr/bin/lsof
```

**Dopo `$`** (espansione variabile):

Mostra le variabili shell disponibili, con il prefisso `$`:

```
oosh> echo $H<Tab>
$HOME
$HISTFILE
$HOST
```

**Dopo `let nome =`** (posizione di binding):

Mostra i binding tipizzati attivi, con indicatore `(let)`:

```
oosh> let nuova = <Tab>
x     (let)
lista (let)
nome  (let)
```

**Dopo `|>`** (stage pipeline):

Mostra gli stage disponibili per la pipeline oggetti:

```
oosh> list() |> <Tab>
where(
sort(
take(
first()
count()
lines()
trim()
split(
join(
reduce(
each(
render()
from_json()
to_json()
grep(
```

**Dopo `->`** (accesso membro):

Mostra le proprieta e i metodi disponibili per il tipo dell'oggetto corrente, con il tipo indicato tra parentesi:

```
oosh> path("/etc") -><Tab>
name         (string)
type         (string)
size         (number)
exists       (bool)
hidden       (bool)
readable     (bool)
writable     (bool)
children()   (method)
parent()     (method)
describe()   (method)
read_text(   (method)
```

### 16.2 Indicatori di tipo

Il completion mostra indicatori di tipo accanto a ogni voce per aiutare l'utente a capire la natura del suggerimento:

| Indicatore | Significato |
|------------|-------------|
| `(cmd)` | Comando built-in |
| `(fn)` | Funzione definita dall'utente |
| `(@)` | Alias |
| `(let)` | Binding tipizzato attivo |
| `(string)` | Proprieta di tipo stringa |
| `(number)` | Proprieta di tipo numero |
| `(bool)` | Proprieta di tipo booleano |
| `(method)` | Metodo |

---

## 17. Prompt

### 17.1 Configurazione

Il prompt di oosh e configurabile tramite un file di testo in formato `chiave=valore`. I parametri principali sono:

```
theme=aurora
left=userhost,cwd,plugins
right=status,os,date,time
separator= ::
use_color=1
color.userhost=green
color.cwd=cyan
color.status=yellow
color.os=blue
color.date=white
color.time=white
```

**Parametri disponibili**:

| Parametro | Valori esempio | Descrizione |
|-----------|----------------|-------------|
| `theme` | `aurora`, `minimal`, `classic` | Tema grafico globale |
| `left` | lista separata da virgole | Segmenti del prompt sinistro |
| `right` | lista separata da virgole | Segmenti del prompt destro |
| `separator` | stringa | Separatore tra segmenti |
| `use_color` | `0` o `1` | Abilita/disabilita i colori ANSI |
| `color.SEGMENTO` | `green`, `cyan`, `red`, ecc. | Colore per il segmento specificato |

I colori disponibili sono i nomi ANSI standard: `black`, `red`, `green`, `yellow`, `blue`, `magenta`, `cyan`, `white`, piu i varianti `bright_*`.

### 17.2 Segmenti disponibili

I segmenti sono le unita di informazione mostrate nel prompt. Ogni segmento puo essere inserito nel prompt sinistro o destro.

| Segmento | Descrizione | Esempio di output |
|----------|-------------|-------------------|
| `user` | Nome utente corrente | `mario` |
| `host` | Nome host (hostname) | `mio-pc` |
| `userhost` | Utente e host combinati | `mario@mio-pc` |
| `cwd` | Directory di lavoro corrente | `/home/mario/progetto` |
| `status` | Exit status dell'ultimo comando | `0` o `1` |
| `os` | Sistema operativo | `Linux` |
| `plugins` | Lista plugin caricati | `git ssh` |
| `theme` | Nome del tema attivo | `aurora` |
| `date` | Data corrente | `2026-03-19` |
| `time` | Ora corrente | `14:32:07` |
| `datetime` | Data e ora combinate | `2026-03-19 14:32:07` |

**Formati data e ora**:
- `date`: sempre nel formato `YYYY-MM-DD`
- `time`: sempre nel formato `HH:MM:SS`

### 17.3 Caricamento automatico

oosh cerca la configurazione del prompt nel seguente ordine:

1. Il percorso specificato dalla variabile di ambiente `OOSH_CONFIG`
2. `oosh.conf` nella directory corrente (o nella directory del progetto)
3. `~/.oosh/prompt.conf` (configurazione utente globale)

Se nessuno di questi file esiste, viene usata la configurazione di default.

Per caricare manualmente una configurazione:

```bash
prompt load /percorso/mia-configurazione.conf
```

Per visualizzare la configurazione attiva:

```bash
prompt show
```

---

## 18. Plugin

### 18.1 Caricare un plugin

I plugin sono librerie condivise (`.so` su Linux, `.dylib` su macOS) compilate contro l'ABI C stabile di oosh. Per caricare un plugin:

```bash
plugin load /percorso/al/plugin.so
```

oosh verifica la compatibilita dell'ABI e carica il plugin nel contesto corrente. I simboli esportati dal plugin diventano immediatamente disponibili.

Un plugin puo essere caricato automaticamente aggiungendo la riga `plugin load ...` al file RC di avvio.

### 18.2 Comandi plugin

```bash
plugin list
# mostra tutti i plugin caricati con nome e versione

plugin info nome_plugin
# informazioni dettagliate: versione, comandi aggiunti, stage, proprieta, ecc.

plugin disable nome_plugin
# disabilita il plugin (rimane caricato ma i suoi simboli non sono attivi)

plugin enable nome_plugin
# riabilita un plugin precedentemente disabilitato
```

### 18.3 Creare un plugin (cenni)

Un plugin oosh e una libreria C che esporta una struttura di registrazione. Il template di partenza si trova in `plugins/skeleton/` nel sorgente di oosh.

Un plugin puo registrare:
- **Nuovi comandi built-in**: funzioni C che vengono chiamate come comandi shell
- **Nuove proprieta**: accessibili con `->` su tipi specifici
- **Nuovi metodi**: invocabili con `->nome()` su tipi specifici
- **Nuovi value resolver**: logica per interpretare espressioni come valori tipizzati
- **Nuovi stage pipeline**: stage aggiuntivi usabili dopo `|>`

L'ABI e stabile: i plugin compilati per una versione di oosh rimangono compatibili con versioni future (salvo aggiornamenti maggiori dell'ABI documentati nel changelog).

Struttura minima di un plugin:

```c
#include "oosh_plugin.h"

static int cmd_mio_comando(oosh_ctx *ctx, int argc, char **argv) {
    oosh_print(ctx, "Ciao dal plugin!\n");
    return 0;
}

static oosh_plugin_def plugin = {
    .name    = "mio_plugin",
    .version = "1.0.0",
    .commands = (oosh_command_def[]) {
        { "mio_comando", cmd_mio_comando, "Descrizione del comando" },
        { NULL }
    },
};

OOSH_PLUGIN_EXPORT(&plugin);
```

---

## 19. Avvio e configurazione

### 19.1 File RC

All'avvio, oosh esegue automaticamente un file di inizializzazione. L'ordine di ricerca e:

1. Il percorso specificato dalla variabile di ambiente `OOSH_RC`
2. `~/.ooshrc` (file RC utente standard)

Se nessuno dei due esiste, la shell si avvia senza file RC.

Il file RC puo contenere qualsiasi comando oosh valido: alias, funzioni, binding, caricamento plugin, configurazione del prompt, esportazione di variabili.

Esempio di `~/.ooshrc`:

```bash
# Alias comuni
alias ll="ls -la"
alias la="ls -A"
alias grep="grep --color=auto"

# Esportazioni
export EDITOR=vim
export PAGER=less
export LANG=it_IT.UTF-8

# Funzioni utili
function mkcd(dir) do
  mkdir -p $dir && cd $dir
endfunction

# Plugin
plugin load ~/.oosh/plugins/git.so

# Prompt
prompt load ~/.oosh/prompt.conf

# Binding globali
let home_dir = path("~")
```

### 19.2 History

La history dei comandi viene salvata in un file. L'ordine di ricerca del file di history e:

1. Il percorso specificato dalla variabile di ambiente `OOSH_HISTORY`
2. `~/.oosh/history` (percorso di default)

La history e condivisa tra sessioni: i comandi di una sessione diventano disponibili nelle sessioni successive. L'editor di riga usa la history per l'autosuggestion e per la navigazione con le frecce.

Il comando `history` mostra la history della sessione corrente:

```bash
history
```

### 19.3 Variabili di ambiente speciali

Le seguenti variabili di ambiente hanno significato speciale per oosh:

| Variabile | Descrizione |
|-----------|-------------|
| `OOSH_RC` | Percorso alternativo al file RC di avvio |
| `OOSH_HISTORY` | Percorso alternativo al file di history |
| `OOSH_CONFIG` | Percorso alternativo alla configurazione prompt |
| `HOME` | Directory home dell'utente (usata da `cd` senza argomenti e da `~`) |
| `PATH` | Percorsi in cui cercare eseguibili |
| `PWD` | Directory di lavoro corrente (aggiornata automaticamente da `cd`) |
| `OLDPWD` | Directory precedente (usata da `cd -`) |
| `EDITOR` | Editor di testo preferito (usato da alcuni plugin) |
| `PAGER` | Pager preferito (usato da alcuni comandi) |

---

## 20. Errori frequenti e diagnostica

Questa sezione raccoglie gli errori piu comuni e come interpretarli.

### Built-in in posizione intermedia di pipeline shell

**Errore**: tentativo di usare un built-in come stadio intermedio o finale di una pipeline shell.

```bash
ls | cd /tmp
# ERRORE: cd e un built-in e non puo ricevere dati da una pipe
```

**Soluzione**: usare una pipeline oggetti o la command substitution.

```bash
# Corretto: usa il bridge shell/object
let items = capture_lines("ls -d /tmp/*/")
items |> first() |> render()
```

### Confronto con operatori relazionali in where() forma selettore

**Errore**: usare `<`, `>`, `<=`, `>=` nella forma selettore di `where()`.

```bash
lista |> where(size > number(1000))
# ERRORE: la forma selettore supporta solo == e !=
```

**Soluzione**: usare la forma block.

```bash
lista |> where([:it | it -> size > number(1000)])
```

### Trap con eventi non supportati

**Errore**: registrare un trap per un evento diverso da `EXIT`.

```bash
trap "comando" INT
# ERRORE o ignorato: solo EXIT e supportato attualmente
```

**Soluzione**: usare solo `trap "cmd" EXIT`.

### Redirection avanzata su sistemi non POSIX

**Errore**: usare file descriptor > 2 su sistemi che non supportano la redirection POSIX di fd arbitrari.

```bash
comando 3> file.txt
# puo non funzionare su sistemi non POSIX
```

**Nota**: questa funzionalita e supportata solo su sistemi POSIX (Linux, macOS, BSD).

### Confusione tra pipeline shell (|) e pipeline oggetti (|>)

Un errore comune e usare `|` dove si intende `|>` o viceversa.

```bash
# SBAGLIATO: tenta di passare l'output di ls come testo a where()
ls /etc | where(type == "file")

# CORRETTO: usa il bridge shell/object con |>
ls /etc |> where(type == "file")
# oppure
path("/etc") -> children() |> where(type == "file")
```

### Variabile shell vs binding tipizzato

Le variabili shell (`set NOME valore`) sono stringhe grezze. I binding (`let nome = espressione`) sono valori tipizzati. Non confondere i due sistemi:

```bash
set N 42
# N e una stringa shell "42"

let n = number(42)
# n e un numero tipizzato

# Per operazioni aritmetiche, usare sempre number():
let n = number($N)
let risultato = n + number(1)
```

### extend non sovrascrive il core

```bash
extend string property name = [:it | text("sovrascrittura") ]
# NON funziona: il core ha sempre la precedenza
```

Il core ha sempre la precedenza. `extend` aggiunge proprieta e metodi nuovi, non sostituisce quelli esistenti.

### Diagnostica con type e inspect

Per capire come oosh risolve un nome:

```bash
type ls
type mia_funzione
type alias_definito
```

Per ispezionare le proprieta di un oggetto filesystem:

```bash
inspect /etc/hosts
inspect /tmp
```

Per vedere lo stato completo della shell come valore tipizzato:

```bash
shell() |> to_json() |> render()
```

Per verificare i binding attivi:

```bash
let
```

Per verificare le funzioni definite:

```bash
function
```

---

## 21. Cheat sheet

Tabella compatta di riferimento rapido per i costrutti piu usati.

### Costruttori di valore

| Costrutto | Risultato |
|-----------|-----------|
| `text("...")` | Stringa tipizzata |
| `string("...")` | Stringa tipizzata (alias) |
| `number(n)` | Numero tipizzato |
| `true` / `false` | Booleano (letterale) |
| `bool(true)` | Booleano (costruttore) |
| `list(v1, v2)` | Lista eterogenea |
| `array(v1, v2)` | Lista eterogenea (alias) |
| `map("k", v)` | Mappa chiave-valore |
| `capture("cmd")` | stdout come text |
| `capture_lines("cmd")` | stdout come list di righe |
| `[:p | body]` | Block literal |
| `env()` | Mappa ambiente |
| `env("K")` | Valore singolo ambiente |
| `proc()` | Mappa processo |
| `shell()` | Mappa runtime shell |
| `path("...")` | Oggetto filesystem |

### Stage pipeline oggetti

| Stage | Effetto |
|-------|---------|
| `where(prop == val)` | Filtra per proprieta (solo == e !=) |
| `where(block)` | Filtra con logica custom |
| `sort(prop asc)` / `sort(prop desc)` | Ordina |
| `take(n)` | Primi n elementi |
| `first()` | Primo elemento singolo |
| `count()` | Conta elementi |
| `lines()` | Stringa in lista di righe |
| `trim()` | Rimuove whitespace iniziale e finale |
| `split(sep)` | Stringa in lista di parti |
| `join(sep)` | Lista in stringa |
| `reduce(init, block)` | Accumulazione con valore iniziale |
| `reduce(block)` | Accumulazione usando il primo elemento |
| `each(prop)` | Estrae proprieta da ogni elemento |
| `each(block)` | Trasforma ogni elemento con block |
| `render()` | Stampa e termina la pipeline |
| `from_json()` | Stringa JSON in valore tipizzato |
| `to_json()` | Valore in stringa JSON |
| `grep("pat")` | Filtra per sottostringa (case-sensitive) |

### Controllo di flusso

| Costrutto | Sintassi compatta |
|-----------|-------------------|
| if | `if cond ; then ... ; fi` |
| if/else | `if cond ; then ... ; else ... ; fi` |
| if/elif/else | `if cond ; then ... ; elif cond ; then ... ; else ... ; fi` |
| while | `while cond ; do ... ; done` |
| until | `until cond ; do ... ; done` |
| for | `for v in sorgente ; do ... ; done` |
| switch | `switch v ; case "x" ; then ... ; default ; then ... ; endswitch` |
| case/esac | `case $v in pat) ... ;; *) ... ;; esac` |
| ternario | `cond ? v_vero : v_falso` |
| break | `break [n]` |
| continue | `continue [n]` |
| return | `return [espressione]` |

### Operatori

| Operatore | Significato |
|-----------|-------------|
| `\|` | Pipeline testuale tra processi |
| `\|>` | Pipeline oggetti (typed) |
| `->` | Accesso membro oggetto |
| `;` | Sequenza comandi |
| `&&` | AND: esegui se il precedente ha avuto successo |
| `\|\|` | OR: esegui se il precedente ha fallito |
| `&` | Esegui in background |
| `<` | Redirection input da file |
| `>` | Redirection output su file (sovrascrittura) |
| `>>` | Redirection output su file (append) |
| `2>` | Redirection stderr su file |
| `2>&1` | Unisci stderr su stdout |
| `+` | Addizione numerica o concatenazione stringa |
| `-` | Sottrazione |
| `*` | Moltiplicazione |
| `/` | Divisione |
| `==` | Uguaglianza |
| `!=` | Diversita |
| `<` `>` `<=` `>=` | Confronto relazionale |

### Proprieta filesystem comuni

| Proprieta | Tipo | Descrizione |
|-----------|------|-------------|
| `type` | string | Tipo: `file`, `directory`, `device`, `mount`, `path`, `unknown` |
| `path` | string | Percorso assoluto |
| `name` | string | Nome (ultima componente) |
| `exists` | bool | Esiste nel filesystem |
| `size` | number | Dimensione in byte |
| `hidden` | bool | Nome inizia con `.` |
| `readable` | bool | Permesso di lettura |
| `writable` | bool | Permesso di scrittura |

### Built-in principali

| Comando | Uso rapido |
|---------|------------|
| `cd path` | Cambia directory |
| `pwd` | Stampa directory corrente |
| `set NOME val` | Variabile shell |
| `export NOME val` | Esporta variabile all'ambiente figlio |
| `unset NOME` | Rimuove variabile o binding |
| `let nome = expr` | Binding tipizzato |
| `alias n="cmd"` | Definisce alias |
| `unalias n` | Rimuove alias |
| `source path` | Esegue file nel contesto corrente |
| `eval "str"` | Valuta stringa come codice oosh |
| `exec cmd` | Sostituisce shell con il comando |
| `exit [n]` | Esce con status n |
| `type nome` | Risolve nome: built-in, funzione, alias, eseguibile |
| `inspect path` | Stampa proprieta oggetto filesystem |
| `history` | History della sessione corrente |
| `jobs` | Lista job attivi, stoppati, completati |
| `fg [%n]` | Porta job in foreground |
| `bg [%n]` | Riprende job stoppato in background |
| `wait [%n]` | Attende completamento job |
| `trap "cmd" EXIT` | Esegue cmd alla chiusura della shell |
| `builtin nome` | Bypassa funzioni omonime e chiama il built-in |
| `true` / `false` | Exit status 0 / 1 |
| `plugin load path` | Carica plugin da file .so |
| `prompt load path` | Carica configurazione prompt |
| `help [cmd]` | Aiuto generale o su un comando specifico |

### Tasti editor di riga

| Tasto | Azione |
|-------|--------|
| Freccia su / giu | Naviga la history |
| Freccia sinistra / destra | Muove il cursore |
| `Ctrl-A` | Inizio della riga |
| `Ctrl-E` | Fine della riga |
| `Ctrl-C` | Annulla la riga corrente o interrompe il job in foreground |
| `Ctrl-D` | Esce dalla shell (su riga vuota) |
| `Ctrl-Z` | Ferma il job in foreground |
| `Tab` | Completion contestuale |
| `Backspace` | Cancella il carattere a sinistra del cursore |

---

*Fine del manuale utente di oosh.*
