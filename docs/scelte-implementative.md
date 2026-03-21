# Scelte implementative di arksh

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
- startup file loader per `~/.arkshrc`
- alias expansion prima del parse
- rendering del prompt
- caricamento config e plugin
- plugin autoload da `~/.arksh/plugins.conf`
- comandi `plugin autoload set/unset/list`

### `src/lexer.c`

Lexer del linguaggio `arksh`. Trasforma una linea in token come:

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
- frecce sinistra/destra, Ctrl-A/E
- history locale in memoria e su file
- syntax highlighting in tempo reale (token colorati per categoria)
- autosuggestion dalla history (suggerimento grigio, accettabile con Ctrl-E)
- tab completion contestuale avanzata (vedi sotto)
- integrazione con il prompt gia renderizzato

#### Tab completion — architettura

Il completion e orchestrato da `handle_completion()`, che:

1. Determina la posizione del cursore nella riga (comando, argomento, redirection, stage `|>`, membro `->`, variabile)
2. Raccoglie i candidati chiamando `collect_completion_matches()` (o le varianti specializzate)
3. Se un solo candidato, lo inserisce direttamente
4. Con doppio Tab consecutivo, stampa la lista completa e avanza fino al prefisso comune (`shared_prefix_length()`)

Il doppio Tab e implementato con un flag `prev_tab` (ultima pressione era `Tab`?) passato a `handle_completion()`.

#### Funzionalita di completion implementate

| Funzione C | Comportamento |
|------------|---------------|
| `is_redirection_position()` | Rileva se il cursore segue `>`, `<`, `>>`, `2>` |
| `get_argument_filter()` | Restituisce un filtro `ArkshArgFilter` in base al comando corrente (`DIR`, `SCRIPT`, `PLUGIN`, `NONE`) |
| `collect_file_matches_filtered()` | Lista file/directory rispettando il filtro del comando |
| `collect_flag_matches()` | Propone flag da `s_flag_table[]` per il comando corrente |
| `is_fuzzy_mode()` | Attiva il matching per sottostringa se la ricerca per prefisso non trova nulla |
| `collect_file_matches_fuzzy()` | Come `collect_file_matches()` ma con match per sottostringa |
| `collect_registered_command_matches_fuzzy()` | Come la variante per prefisso ma con sottostringa |

La tabella `s_flag_table[]` e una struttura statica che mappa nomi di comandi noti a elenchi di flag. Aggiungere un comando alla tabella e sufficiente per attivarne il completion dei flag.

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

- `arksh_plugin_init`

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
- caricamento opzionale di `~/.arkshrc`
- override del file startup tramite `ARKSH_RC`
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
6. Per la fase F i job in background vengono eseguiti in una nuova istanza di `arksh -c ...`, cosi possono riusare tutta la grammatica gia supportata senza duplicare l'executor asincrono.
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
plugin=./build/arksh_sample_plugin.so
```

### Motivazione della scelta

- Un file `key=value` e banale da parsare in C.
- La semantica a segmenti e simile ai theme engine piu diffusi.
- E facile migrare in futuro a TOML o DSL piu ricche mantenendo invariata la struttura logica.

## Sistema plugin

### ABI v1

Il plugin esporta:

```c
int arksh_plugin_init(ArkshShell *shell, const ArkshPluginHost *host, ArkshPluginInfo *out_info);
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

### Plugin autoload

I plugin da caricare automaticamente all'avvio vengono letti da `~/.arksh/plugins.conf`, un file con un percorso assoluto per riga (commenti `#` e righe vuote ignorati). Il caricamento avviene in `try_load_plugin_autoload()`, chiamata da `arksh_shell_init()` dopo `try_load_default_config()` e prima di `try_load_default_rc()`.

La lista viene gestita con i sottocomandi:

- `plugin autoload set <path>` — risolve il percorso, verifica i duplicati, appende al file
- `plugin autoload unset <path>` — filtra la riga tramite file temporaneo + `rename` (atomico)
- `plugin autoload list` — stampa le righe attive nel file

**Motivazione della scelta**: usare un file dedicato (`plugins.conf` separato dal `prompt.conf`) mantiene separata la configurazione dei plugin dalla configurazione del prompt. Il formato a percorso assoluto per riga e il piu semplice da scrivere, leggere e gestire in C senza parser aggiuntivi.

## Strategia di test

### Golden test (`.arksh`)

I golden test sono script `.arksh` nella cartella `tests/fixtures/golden/`. CMake li registra come `ctest` entry usando `PASS_REGULAR_EXPRESSION` con un pattern caratteristico dell'output atteso (l'ultima o la piu distintiva riga prodotta dallo script).

Fixture disponibili:

| File | Pattern atteso | Verifica |
|------|----------------|----------|
| `pipeline-chain.arksh` | `13` | sort + take + count |
| `reduce-and-block.arksh` | `\[10,20,30\]` | to_json su lista |
| `string-transform.arksh` | `alpha beta gamma` | split + count e join |
| `if-sequence.arksh` | `step_three` | if/else/fi in sequenza |
| `mixed-shell-and-objects.arksh` | `done` | mix shell e object model |

Motivazione: ogni test usa un singolo pattern per evitare la semantica OR di `PASS_REGULAR_EXPRESSION` con punti e virgola. I pattern sono scelti tra le righe finali per ridurre la dipendenza dall'ordine di esecuzione.

### PTY test (REPL interattiva)

`tests/pty_repl.c` avvia arksh in uno pseudo-terminale con `forkpty()` e verifica il comportamento interattivo. Attivo solo su sistemi POSIX (`if(NOT WIN32)` in CMakeLists).

I 5 test coprono:
1. Prompt presente all'avvio
2. Esecuzione di `echo` in sessione interattiva
3. Continuation prompt su input incompleto
4. Tab completion su prefisso `histor`
5. Ctrl-D su riga vuota termina la shell con exit code 0

`forkpty()` richiede `<util.h>` su macOS (senza librerie aggiuntive) e `<pty.h>` + `-lutil` su Linux. Questa differenza e gestita con `#ifdef __APPLE__` nel sorgente e `if(APPLE)` in CMakeLists.

### Job control smoke test

4 test `ctest` che verificano il job control in modalita batch (`-c ...`). Ogni test usa `PASS_REGULAR_EXPRESSION` per verificare un elemento specifico dell'output di `jobs` o `wait`:

- `arksh_jobctrl_two_jobs`: due job in background, `jobs` mostra 2 entry
- `arksh_jobctrl_wait_all`: `wait` senza argomenti attende tutti i job
- `arksh_jobctrl_done_then_cmd`: messaggio `done` dopo il completamento + comando successivo
- `arksh_jobctrl_current_marker2`: il marcatore `+` identifica il job corrente

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
- i PTY test (`tests/pty_repl.c`) sono esclusi dalla build Windows con `if(NOT WIN32)` in CMakeLists, perche `forkpty()` non e disponibile su quel sistema

### CI multiplatform

Il workflow `.github/workflows/ci.yml` esegue:

1. **Job ASan + UBSan** (Linux, macOS): build Debug con sanitizers abilitati, esecuzione di tutti i test
2. **Job Release** (Linux, macOS, Windows): matrix su `[ubuntu-latest, macos-latest, windows-latest]`, build Release, esecuzione di tutti i test con `--build-config Release`

Il job Release usa `fail-fast: false` per raccogliere risultati su tutte le piattaforme indipendentemente dai fallimenti. Su Windows, CMake rileva MSVC automaticamente; il flag `--config Release` e necessario perche MSVC usa multi-config generator.

### Regola importante

La logica di business non deve mai chiamare direttamente API OS-specifiche. Tutto deve passare da `platform.c`.

## Flusso di esecuzione

1. `main` inizializza `ArkshShell`
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

Elementi gia realizzati (non ripetere):

- linguaggio shell: funzioni, classi, estensioni, heredoc, case/esac, break/continue con argomento numerico, until
- object model: `env()`, `proc()`, `shell()`, `path()`, pipeline oggetti completa con tutti gli stage (where, sort, take, first, count, lines, trim, split, join, reduce, each, render, from_json, to_json, grep, map, filter)
- tab completion: contestuale, redirection-aware, filtrata per comando, flag, doppio Tab, fuzzy/sottostringa
- testing: golden test, PTY test REPL, job control smoke test, unit test lexer/parser/executor/object model, sanitizers, fuzzing
- CI: Linux + macOS + Windows in GitHub Actions

Elementi aperti per una versione piu avanzata:

1. Tipi numerici espliciti (`Integer`, `Float`, `Double`) con regole di promozione (E6-S5)
2. Tipo `Dict` nativo con bridge JSON completo (E6-S6)
3. Stage di encoding (`base64_encode`, `base64_decode`) (E6-S7)
4. JSON parser robusto: posizione errori, unicode, pretty-print (E7)
5. Plugin HTTP/HTTPS con libcurl: resolver `http()`, request builder, response typed-map (E10)
6. Packaging: installazione standard, formula Homebrew, tarball release (E9)

## Contratto da preservare se un'altra AI continua il lavoro

Se il progetto viene esteso, questi principi vanno mantenuti:

1. `platform.c` resta l'unico strato OS-dependent.
2. Il parser produce un AST esplicito, non esegue direttamente.
3. Ogni oggetto espone proprieta e metodi con naming stabile.
4. Il prompt usa segmenti dichiarativi, non stringhe hardcoded.
5. I plugin parlano con il core tramite ABI versionata.

Questo consente ad una persona o ad un'altra AI di implementare il resto senza dover ripensare il progetto da zero.
