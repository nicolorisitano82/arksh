# Scelte implementative di oosh

## Obiettivo

Costruire una shell in C, portabile tra Linux, macOS e Windows, in cui ogni elemento del sistema sia modellato come oggetto con:

- tipo
- proprieta
- metodi
- capability estendibili via plugin

L'MVP presente in questo repository non pretende di essere una shell completa come `bash`, `zsh` o `pwsh`. E invece una base implementabile e leggibile che dimostra:

- modello oggetti
- lexer, AST ed executor base
- esecuzione nativa dei processi esterni con pipe e redirection di base
- quoting shell e modulo di espansione
- prompt configurabile
- ABI plugin
- struttura modulare

## Assunzioni architetturali

1. Il linguaggio host e C11.
2. La shell deve essere compilabile con toolchain moderne su Linux, macOS e Windows.
3. La portabilita non deve dipendere da librerie pesanti; per questo il core usa API standard e piccole astrazioni di piattaforma.
4. Tutto cio che varia per OS deve passare dal modulo `platform`.
5. La shell deve avere un ABI plugin esplicito, piccolo e versionato.
6. L'object model deve essere chiaro anche se l'MVP implementa solo file system, path, device e mount point.

## Struttura dei moduli

### `src/main.c`

Entry point. Gestisce:

- `--help`
- `-c '<command>'`
- REPL interattiva

### `src/shell.c`

Runtime principale. Contiene:

- stato della shell
- variabili locali ed exported
- alias
- registry comandi
- built-in
- startup file loader per `~/.ooshrc`
- alias expansion prima del parse
- rendering del prompt
- caricamento config e plugin

### `src/lexer.c`

Lexer del linguaggio `oosh`. Trasforma una linea in token come:

- `word`
- `string`
- `->`
- `|>`
- `|`
- `;`, `&&`, `||`, `&`
- `<`, `>`, `>>`, `2>`, `2>>`, `2>&1`
- parentesi e virgole

### `src/parser.c`

Parser AST-based. L'MVP riconosce:

- comandi classici tokenizzati per spazi e stringhe quotate
- espressioni oggetto del tipo `path -> property`
- espressioni oggetto del tipo `path -> method(arg1, arg2)`
- pipeline oggetti con source generiche: object expression, `text(...)`, `number(...)`, `bool(...)`, `list(...)`, `capture(...)`
- pipeline shell classiche con stage esterni e redirection per-stage
- liste di comandi con `;`, `&&`, `||`
- job asincroni con `&`
- espressioni condizionali con ternario `condizione ? vero : falso`
- comandi composti `if`, `while`, `for` e `switch`
- sintassi legacy del tipo `obj("path").property`
- sintassi legacy del tipo `obj("path").method(arg1, arg2)`

### `src/executor.c`

Executor dell'AST. Esegue:

- simple command sui built-in registrati o su processi esterni
- value expression per stringhe, numeri, bool, liste e capture di comandi
- operatori espressivi `==`, `+` e ternario `?:`
- object expression
- object pipeline
- shell pipeline classiche con pipe e redirection
- liste di comandi, controllo di flusso base, `switch` e loop elementari

Per `if` e `while` il runtime usa una semantica ibrida:

- prova prima a valutare la condizione come value expression
- se la valutazione riesce, usa la truthiness del valore
- se fallisce, esegue la condizione come comando shell classico e usa l'exit status

Questo consente forme come `if . -> exists` e `if true` senza duplicare il linguaggio.

Questa separazione resta il punto di aggancio per introdurre in seguito:

- job control
- composizione tra pipeline oggetti e processi esterni

### `src/line_editor.c`

Gestisce la UX interattiva della REPL:

- editing della riga
- frecce sinistra/destra
- history locale in memoria
- completion base con `Tab`
- integrazione con il prompt gia renderizzato

### `src/expand.c`

Implementa la fase di espansione lessicale/semantica sopra i token raw:

- single quote
- double quote
- backslash
- `~`
- `$VAR`, `${VAR}`, `$?`
- `$(...)`
- globbing base dei comandi

Le espansioni leggono prima il runtime della shell e poi l'ambiente di processo, cosi una variabile impostata con `set` puo ombreggiare una variabile ambiente omonima.

### `src/object.c`

Implementa il modello oggetti del core.

Oggetto base:

- `kind`
- `path`
- `name`
- `exists`
- `size`
- `hidden`
- `readable`
- `writable`

### `src/platform.c`

Incapsula le differenze tra OS:

- `getcwd`
- `chdir`
- hostname
- stat file system
- children listing
- file reading
- process spawn e pipe
- dynamic library loading

### `src/prompt.c`

Gestisce:

- valori di default del prompt
- parsing file di configurazione
- rendering dei segmenti
- colori ANSI

### `src/plugin.c`

Carica librerie dinamiche e invoca l'entry point ABI:

- `oosh_plugin_init`

## Modello ad oggetti

Il principio e: ogni entita osservabile dal sistema diventa un oggetto risolvibile.

Nell'MVP, lato utente, la forma consigliata e `receiver -> member`.
Internamente la risoluzione supporta sia path sia namespace logici:

- `env("PATH")`
- `proc()`
- `mount("/")`
- `device("/dev/tty")`
- `shell()`

### Tipi core previsti

- `path`
- `file`
- `directory`
- `device`
- `mount`
- `unknown`

### Proprieta base da mantenere stabili

- `type`
- `path`
- `name`
- `exists`
- `size`
- `hidden`
- `readable`
- `writable`

### Metodi core da mantenere stabili

- `children()`
- `read_text(limit)`
- `parent()`
- `describe()`

Questi nomi sono importanti perche definiscono il primo contratto semantico della shell.

## Stato shell introdotto nella fase D

Per avvicinare il runtime alle basi di una shell tradizionale sono stati introdotti:

- tabella variabili shell con flag `exported`
- tabella alias
- built-in `set`, `export`, `unset`, `alias`, `unalias`, `source`, `.`, `type`
- caricamento opzionale di `~/.ooshrc`
- override del file startup tramite `OOSH_RC`
- history persistente su file
- line editor interattivo minimale senza dipendenze esterne
- job table in memoria
- built-in `jobs`, `fg`, `bg`, `true`, `false`
- controllo di flusso con `;`, `&&`, `||`, `&`
- job state espliciti (`running`, `stopped`, `done`) e resume dei job stoppati su POSIX

### Motivazioni

1. Separare variabili shell e ambiente permette di costruire scripting e rc file senza sporcare ogni assegnazione nel processo figlio.
2. Gli alias sono implementati come espansione testuale pre-parse perche e la scelta piu semplice e compatibile con la semantica classica.
3. `source` deve lavorare nello stesso runtime e non in un processo figlio, altrimenti non potrebbe aggiornare directory corrente, alias o variabili.
4. `type` e un built-in strategico per capire come un nome viene risolto mentre la grammatica evolve.
5. Per la fase E e stato preferito un line editor integrato invece di `readline`, cosi il core resta leggero e portabile anche dove la dipendenza non e disponibile.
6. Per la fase F i job in background vengono eseguiti in una nuova istanza di `oosh -c ...`, cosi possono riusare tutta la grammatica gia supportata senza duplicare l'executor asincrono.
7. Nella tranche successiva del job control POSIX i background job usano process group dedicati, cosi `fg`, `bg` e `Ctrl-Z` possono lavorare su unita coerenti lato terminale.

## Perche un object model e non solo comandi

Una shell object-oriented permette:

- introspezione consistente
- autocomplete semanticamente ricca
- pipeline basate su oggetti invece che su sole stringhe
- plugin che aggiungono metodi e tipi senza cambiare la grammatica base

Esempio di evoluzione futura:

```text
/tmp -> children() |> where(type == "file") |> sort(size desc)
```

L'MVP ora implementa una pipeline oggetti/valori con operatori `where`, `sort`, `take`, `first`, `count`, `render`, `lines` ed `each`.

## Prompt stile oh-my-zsh

Il prompt non e hardcoded. Viene composto da segmenti.

Segmenti base implementati:

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

Formato config attuale:

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
plugin=./build/oosh_sample_plugin.so
```

### Motivazione della scelta

- Un file `key=value` e banale da parsare in C.
- La semantica a segmenti e simile ai theme engine piu diffusi.
- E facile migrare in futuro a TOML o DSL piu ricche mantenendo invariata la struttura logica.

## Sistema plugin

### ABI v1

Il plugin esporta:

```c
int oosh_plugin_init(OoshShell *shell, const OoshPluginHost *host, OoshPluginInfo *out_info);
```

Il core espone al plugin:

- `api_version`
- `register_command(...)`
- `register_property_extension(...)`
- `register_method_extension(...)`

### Motivazione

L'ABI v1 resta piccola per tre ragioni:

1. minimizza il rischio di rottura binaria
2. rende semplice scrivere plugin in C
3. lascia spazio ad ABI v2 e v3

Le estensioni registrate entrano in un registry runtime comune usato anche dal linguaggio `extend ...`.

Regola di dispatch dell'MVP:

1. il core prova prima proprieta e metodi built-in
2. se il membro non esiste, il runtime consulta il registry delle estensioni
3. l'estensione puo essere definita come block nel linguaggio o come callback nativa da plugin

Questo permette di mantenere il core piccolo ma estendibile senza cambiare la grammatica base.

## Strategia cross-platform

### Linux e macOS

Usano:

- `stat`
- `opendir/readdir`
- `dlopen/dlsym`
- `getcwd/gethostname/chdir`

### Windows

Usa:

- `_stat`
- `FindFirstFileA`
- `LoadLibraryA/GetProcAddress`
- `_getcwd/_chdir`
- `CreateProcessA`

Nota sullo stato attuale:

- Linux, macOS e Windows condividono lo stesso contract per process execution, redirection e shell pipeline multi-stage
- il globbing degli argomenti comando passa dal platform layer, cosi il comportamento resta definito anche sui build Windows
- i built-in dentro pipeline shell multi-stage restano invece un limite del livello executor, non del layer OS-specifico

### Regola importante

La logica di business non deve mai chiamare direttamente API OS-specifiche. Tutto deve passare da `platform.c`.

## Flusso di esecuzione

1. `main` inizializza `OoshShell`
2. `shell_init` registra built-in e carica config di default
3. ogni linea passa al lexer
4. il parser costruisce un AST esplicito
5. l'executor valuta l'AST
6. il risultato viene renderizzato come testo

## Gestione errori

Linee guida:

- ogni funzione pubblica ritorna `0` su successo e `!= 0` su errore
- i messaggi utente vengono scritti nel buffer `out`
- gli errori di parsing non devono mandare in crash la REPL
- i plugin non devono poter sovrascrivere comandi esistenti

## Scelte dati

Nell'MVP si usano array statici e buffer fixed-size.

Motivazione:

- facilita debug
- evita ownership complessa
- rende il codice piu semplice da leggere da un'altra AI o da una persona

Trade-off:

- meno elasticita
- limiti duri su numero di comandi, plugin e argomenti

Per una versione piu avanzata si puo passare a:

- vettori dinamici
- arena allocator
- string builder dedicato

## Sintassi: cosa e implementato e cosa no

Implementato nel codice:

- `inspect <path>`
- `get <path> <property>`
- `call <path> <method> [args...]`
- `path -> property`
- `path -> method(...)`
- `path -> children() |> where(...) |> sort(...)`
- `obj("...").property`
- `obj("...").method(...)`
- `obj("...").children() |> where(...) |> sort(...)`
- `take(n)`, `first()`, `count()`, `render()`
- `prompt show`
- `prompt load <path>`
- `plugin load <path>`
- `plugin list`

Definito come direzione futura, ma non ancora implementato:

- filtri declarativi
- nuovi resolver di oggetto filesystem-specifici oltre a `env()`, `proc()` e `shell()`
- registrazione di nuovi tipi object-aware completi via plugin

## Roadmap suggerita per una implementazione piena

1. Estendere il linguaggio shell verso funzioni, grouping, subshell, heredoc completi e special built-in restanti.
2. Espandere i namespace object-aware oltre a `env()`, `proc()` e `shell()`.
3. Implementare tab completion con introspezione dei metodi.
4. Separare renderer testuale e renderer strutturato.
5. Espandere ulteriormente l'ABI plugin oltre a comandi, estensioni, resolver e stage.
6. Aggiungere test cross-platform.
7. Introdurre pipeline miste tra processi esterni e valori object-aware.

## Contratto da preservare se un'altra AI continua il lavoro

Se il progetto viene esteso, questi principi vanno mantenuti:

1. `platform.c` resta l'unico strato OS-dependent.
2. Il parser produce un AST esplicito, non esegue direttamente.
3. Ogni oggetto espone proprieta e metodi con naming stabile.
4. Il prompt usa segmenti dichiarativi, non stringhe hardcoded.
5. I plugin parlano con il core tramite ABI versionata.

Questo consente ad una persona o ad un'altra AI di implementare il resto senza dover ripensare il progetto da zero.
