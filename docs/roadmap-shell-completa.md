# Roadmap per rendere `arksh` una shell completa e utilizzabile

## Obiettivo

Portare `arksh` da MVP object-aware a shell quotidianamente usabile su Linux, macOS e Windows, con:

- comportamento prevedibile
- buona ergonomia interattiva
- scripting affidabile
- parita cross-platform dichiarata e verificata
- modello object-aware integrato senza rompere la shell classica

Questo documento non descrive solo "cosa manca", ma anche:

- perche serve
- in quale ordine conviene implementarlo
- quali moduli toccare
- quali criteri usare per considerare completata una fase

## Definizione di "shell completa e utilizzabile"

Per questo progetto, una shell puo dirsi completa e utilizzabile quando soddisfa almeno questi requisiti:

1. puo essere usata come shell interattiva primaria per task comuni di sviluppo e amministrazione
2. puo eseguire script shell non banali in modo stabile
3. ha una semantica chiara per quoting, espansioni, redirection, pipeline, job control e startup
4. ha una UX interattiva comparabile almeno alle basi di `bash` o `zsh`
5. mantiene un vantaggio distintivo nel modello object-aware senza creare incoerenze con la shell classica

## Stato attuale sintetico

Oggi `arksh` ha gia una base solida:

- lexer, parser AST-based ed executor separati
- shell pipeline e redirection di base
- controllo di flusso con `if`, `while`, `for`, ternario e `switch`
- object model per filesystem e valori typed
- pipeline object-aware con block
- prompt configurabile
- plugin runtime
- editor riga MVP, history e completion base
- job control iniziale

Questa base e buona, ma non ancora sufficiente per una shell "di produzione".

## Gap principali

Le aree che oggi impediscono a `arksh` di essere una shell completa sono queste:

### 1. Linguaggio shell incompleto

Mancano ancora costrutti e semantiche importanti:

- funzioni shell
- subshell vere `( ... )`
- grouping `{ ...; }`
- `case` stile POSIX classico
- `until`
- `break`, `continue`, `return`
- `exec`, `eval`, `trap`, `wait`
- heredoc e here-string
- redirection su file descriptor arbitrari

### 2. Integrazione incompleta tra shell classica e object model

Il modello object-aware e forte, ma oggi resta ancora separato in piu punti:

- le object expression non si compongono ancora liberamente con tutta la grammatica shell
- i built-in non entrano ancora bene nelle pipeline shell multi-stage
- il bridge tra output processo e valori object-aware e ancora esplicito, non organico
- mancano namespace object-aware piu ricchi oltre a `env()`, `proc()` e `shell()`

### 3. UX interattiva ancora MVP

L'editor riga e usabile, ma per un uso quotidiano mancano:

- reverse search della history
- movimenti per parola
- kill/yank ring
- undo
- multiline editing vero
- syntax highlighting
- autosuggestion
- completion piu intelligente e contestuale

### 4. Job control non ancora a livello di shell matura

La base attuale funziona, ma servono ancora:

- gestione completa dei process group nelle pipeline foreground
- resume e stop coerenti per job composti
- `wait` e interrogazione robusta degli status
- semantica coerente tra POSIX e Windows

### 5. Robustezza ingegneristica

Per un prodotto affidabile servono:

- piu strutture dinamiche e meno limiti fissi
- test di stress
- test di compatibilita comportamentale
- sanitizers
- fuzzing del parser
- CI multipiattaforma
- benchmark e profiling

### 6. Ecosistema e packaging

Una shell usabile davvero richiede anche:

- installazione chiara
- layout standard di config/cache/data
- versioning ABI dei plugin piu rigoroso
- policy di trust dei plugin
- release reproducible
- documentazione operativa e troubleshooting

## Principio guida

Il principio architetturale da mantenere e questo:

> `arksh` deve restare una shell classica completa, sopra cui il modello object-aware agisce come estensione coerente, non come linguaggio separato.

In pratica:

- ogni script shell normale deve avere una semantica stabile
- le estensioni object-aware devono potersi inserire nel flusso senza rompere le aspettative di una shell tradizionale
- tutto cio che e OS-specifico deve restare confinato in `platform.c`

## Ordine raccomandato di implementazione

L'ordine giusto non e "aggiungere feature a caso", ma ridurre prima i rischi architetturali.

### Fase 1. Completare il linguaggio shell di base

Questa e la priorita assoluta.

#### Da implementare

- funzioni shell
- `break`, `continue`, `return`
- subshell `( ... )`
- grouping `{ ...; }`
- `case`
- `until`
- heredoc `<<`
- redirection su fd arbitrari
- `exec`, `eval`, `wait`

#### Moduli da toccare

- [src/lexer.c](/Users/nicolo/Desktop/arksh/src/lexer.c)
- [src/parser.c](/Users/nicolo/Desktop/arksh/src/parser.c)
- [include/arksh/ast.h](/Users/nicolo/Desktop/arksh/include/arksh/ast.h)
- [src/executor.c](/Users/nicolo/Desktop/arksh/src/executor.c)
- [src/shell.c](/Users/nicolo/Desktop/arksh/src/shell.c)

#### Criteri di completamento

- uno script shell moderatamente complesso puo girare senza work-around
- REPL e `source` gestiscono blocchi annidati correttamente
- redirection e grouping hanno semantica coerente

### Fase 2. Completare quoting ed espansioni

Questa e la seconda priorita, perche influenza tutto.

#### Da implementare

- field splitting completo via `IFS`
- parameter expansion avanzata
- positional parameters: `$0`, `$1`, `$@`, `$*`, `$#`, `$?`, `$$`, `$!`, `$-`
- arithmetic expansion
- quote removal completa
- globbing coerente in tutti i contesti previsti

#### Moduli da toccare

- [src/expand.c](/Users/nicolo/Desktop/arksh/src/expand.c)
- [src/executor.c](/Users/nicolo/Desktop/arksh/src/executor.c)
- [src/shell.c](/Users/nicolo/Desktop/arksh/src/shell.c)

#### Criteri di completamento

- gli stessi input quotati producono lo stesso risultato su Linux, macOS e Windows
- script con espansioni annidate funzionano in modo prevedibile

### Fase 3. Far entrare i built-in dentro le pipeline shell

Questo e uno dei gap pratici piu evidenti.

#### Da implementare

- esecuzione dei built-in in pipeline shell multi-stage
- semantica chiara tra built-in che devono mutare lo stato shell e built-in che possono essere processati in subprocess
- uniformita tra pipe, redirection e background

#### Moduli da toccare

- [src/executor.c](/Users/nicolo/Desktop/arksh/src/executor.c)
- [src/platform.c](/Users/nicolo/Desktop/arksh/src/platform.c)
- [src/shell.c](/Users/nicolo/Desktop/arksh/src/shell.c)

#### Criteri di completamento

- casi come `history | grep foo`, `type ls | cat`, `plugin list | cat` devono funzionare
- documentare chiaramente quali built-in mutano stato e quando

### Fase 4. Job control e TTY di livello produzione

#### Da implementare

- gestione completa dei process group per pipeline foreground e background
- `wait`
- stop/resume corretti dei job composti
- segnalazione robusta di job `running`, `stopped`, `done`, `terminated`
- parity Windows esplicita o comportamento equivalente ben dichiarato

#### Moduli da toccare

- [src/platform.c](/Users/nicolo/Desktop/arksh/src/platform.c)
- [src/shell.c](/Users/nicolo/Desktop/arksh/src/shell.c)
- [src/executor.c](/Users/nicolo/Desktop/arksh/src/executor.c)

#### Criteri di completamento

- `jobs`, `fg`, `bg`, `wait`, `Ctrl-C`, `Ctrl-Z` funzionano in sessioni reali
- nessun job zombie o terminale lasciato in stato corrotto

### Fase 5. Portare la UX interattiva a livello quotidiano

#### Da implementare

- reverse search history
- editing multilinea
- movimenti per parola
- kill/yank
- undo
- completion contestuale piu ricca
- completion di funzioni, alias, env var, binding typed e membri plugin
- opzionalmente syntax highlighting e autosuggestion

#### Moduli da toccare

- [src/line_editor.c](/Users/nicolo/Desktop/arksh/src/line_editor.c)
- [src/shell.c](/Users/nicolo/Desktop/arksh/src/shell.c)
- [src/prompt.c](/Users/nicolo/Desktop/arksh/src/prompt.c)

#### Criteri di completamento

- la REPL e usabile per sessioni lunghe senza frustrazione
- un utente abituale di `bash` o `zsh` non percepisce il line editor come un grosso passo indietro

### Fase 6. Rafforzare il modello object-aware

Questa e la fase che trasforma `arksh` da shell "simile alle altre" a shell distintiva.

#### Da implementare

- namespace aggiuntivi come `fs()`, `net()`, `sys()`, `user()`, `time()`
- resolver plugin che introducono nuovi tipi object-aware completi
- stage pipeline plugin con capability meglio tipizzate
- conversioni piu naturali tra output comando e valori
- object pipeline piu espressive: `map`, `filter`, `group_by`, `flat_map`, `sum`, `min`, `max`

#### Moduli da toccare

- [src/object.c](/Users/nicolo/Desktop/arksh/src/object.c)
- [src/executor.c](/Users/nicolo/Desktop/arksh/src/executor.c)
- [src/plugin.c](/Users/nicolo/Desktop/arksh/src/plugin.c)
- [include/arksh/plugin.h](/Users/nicolo/Desktop/arksh/include/arksh/plugin.h)
- [src/shell.c](/Users/nicolo/Desktop/arksh/src/shell.c)

#### Criteri di completamento

- i plugin possono aggiungere non solo comandi, ma veri domini di oggetti
- il modello typed resta coerente e introspezionabile

### Fase 7. Portare il JSON e i dati strutturati a livello prodotto

Oggi il supporto JSON e gia utile, ma per uso serio deve crescere ancora.

#### Da implementare

- parser e serializer robusti per casi edge
- supporto a numeri, stringhe, array e mappe molto grandi
- errori migliori con location
- bridge piu fluido JSON <-> valori object-aware
- metodi per query e trasformazioni sui dati strutturati

#### Moduli da toccare

- [src/object.c](/Users/nicolo/Desktop/arksh/src/object.c)
- [src/executor.c](/Users/nicolo/Desktop/arksh/src/executor.c)

#### Criteri di completamento

- round-trip JSON affidabile
- gestione stabile di dati annidati reali

### Fase 8. Qualita, test e stabilita

Questa fase deve procedere in parallelo, ma va formalizzata.

#### Da implementare

- test unitari per lexer/parser/executor/object model
- test golden per output di script
- test interattivi PTY
- matrice CI su Linux/macOS/Windows
- ASan/UBSan in CI
- fuzzing del parser e dell'expander
- test di regressione per plugin ABI

#### Moduli da toccare

- [CMakeLists.txt](/Users/nicolo/Desktop/arksh/CMakeLists.txt)
- cartella `tests/`
- workflow CI da aggiungere

#### Criteri di completamento

- ogni release candidata gira su tutti i target supportati
- i crash per input malformato diventano non accettabili

### Fase 9. Packaging e distribuzione

#### Da implementare

- install target CMake
- layout standard per config, cache, history, plugin
- pacchetti per Homebrew, apt, winget o equivalente
- versioning del formato config
- versioning ABI plugin
- release notes e changelog

#### Moduli da toccare

- [CMakeLists.txt](/Users/nicolo/Desktop/arksh/CMakeLists.txt)
- struttura di packaging da aggiungere
- documentazione in `README` e `docs`

#### Criteri di completamento

- l'utente puo installare, aggiornare e rimuovere `arksh` senza build manuale

## Decisioni architetturali da prendere subito

Prima di continuare a sommare feature, conviene fissare queste decisioni:

### A. Modalita compatibilita shell

Conviene prevedere una modalita esplicita:

- `--posix`
- oppure `set -o posix`

Questo permette di mantenere estensioni object-aware avanzate senza confondere il comportamento della shell classica.

### B. Confine tra valore e comando

Va definito meglio quando un testo deve essere interpretato come:

- comando shell
- value expression
- object expression

La regola deve essere semplice e stabile, altrimenti scripting e REPL diventano fragili.

### C. Politica dei built-in in pipeline

Serve distinguere:

- built-in puri, eseguibili anche in contesto pipeline
- built-in che mutano lo stato shell e devono restare nel processo principale

### D. ABI plugin

L'ABI va stabilizzata con:

- numero di versione
- capability flags
- ownership chiara della memoria
- regole di compatibilita forward/backward

## Cosa non fare

Per evitare debito tecnico pesante:

- non aggiungere nuova grammatica con altri `if` e `strchr` sparsi fuori dal parser
- non introdurre logica OS-specifica fuori da `platform.c`
- non fare crescere il line editor con patch locali senza un modello interno piu chiaro
- non estendere i plugin senza chiarire ownership e ciclo di vita dei valori
- non dichiarare "shell completa" prima di avere CI multipiattaforma e test PTY seri

## Roadmap pratica consigliata

Se bisogna scegliere un ordine operativo concreto, questo e il piu sensato:

1. completare linguaggio shell e redirection
2. completare espansioni e semantica dei parametri
3. integrare built-in nelle pipeline shell
4. chiudere job control e TTY
5. alzare il livello del line editor
6. rafforzare object model e plugin typed
7. portare test, CI e sanitizers a livello produzione
8. distribuire e versionare

## Definizione di "versione 1.0"

`arksh` puo puntare a una `1.0` quando soddisfa tutti questi punti:

- shell interattiva stabile per uso quotidiano
- scripting shell affidabile su casi non banali
- pipeline, redirection e built-in coerenti
- job control stabile almeno sui target POSIX
- comportamento dichiarato e testato su Windows
- plugin ABI versionata
- documentation completa per installazione, uso e debugging
- test automatici robusti e CI verde multipiattaforma

## Criterio finale di successo

Il criterio giusto non e "ha tante feature", ma questo:

> un utente puo aprire `arksh`, usarla per lavorare per ore, eseguire script veri, estenderla con plugin, e non sentirla fragile o incompleta.

Se una feature sembra brillante ma non aiuta questo obiettivo, va rimandata.
