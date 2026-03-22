# Studio su CPU e Memoria in ARKsh

## Obiettivo

Questo documento raccoglie uno studio tecnico su come ridurre consumo di CPU, allocazioni heap, copie inutili e memoria residente nel core di `arksh`.

Non e un benchmark numerico: e una analisi architetturale del codice attuale, con una roadmap di ottimizzazione ordinata per rapporto **impatto / rischio / costo**.

La baseline numerica iniziale introdotta in `E12-S1` e documentata in [docs/benchmarks-baseline.md](/Users/nicolo/Desktop/oosh/docs/benchmarks-baseline.md).

## Sintesi esecutiva

I due problemi principali oggi sono:

1. **layout dati troppo larghi**
   `ArkshValue`, `ArkshValueItem` e `ArkshShell` incorporano molti buffer e registry inline, quindi ogni copia costa molto e ogni snapshot ha un prezzo alto in CPU e memoria.

2. **troppe allocazioni e ricopie nel path caldo**
   l'executor, le espansioni, i block, le funzioni e le subshell fanno molte `calloc`, `free`, `arksh_value_copy` e `arksh_value_render` anche per operazioni piccole o ripetitive.

In termini pratici, le ottimizzazioni con ROI piu alto sono:

- introdurre una **scratch arena** o temp allocator per l'executor
- rendere `ArkshShell` piu leggero spostando registry e dati grandi su heap
- rendere `ArkshValue` piu compatto e meno copy-heavy
- evitare parse/eval ricorsivi di stringhe quando il parser potrebbe produrre un AST piu preciso
- ridurre gli snapshot profondi di shell, funzioni e subshell

## Perimetro dell'analisi

L'analisi si basa soprattutto su questi moduli:

- [include/arksh/object.h](/Users/nicolo/Desktop/oosh/include/arksh/object.h)
- [include/arksh/shell.h](/Users/nicolo/Desktop/oosh/include/arksh/shell.h)
- [src/executor.c](/Users/nicolo/Desktop/oosh/src/executor.c)
- [src/expand.c](/Users/nicolo/Desktop/oosh/src/expand.c)
- [src/parser.c](/Users/nicolo/Desktop/oosh/src/parser.c)
- [src/shell.c](/Users/nicolo/Desktop/oosh/src/shell.c)

## Stato attuale

### 1. `ArkshValue` e `ArkshValueItem` sono ancora molto grandi

In [include/arksh/object.h](/Users/nicolo/Desktop/oosh/include/arksh/object.h) ci sono ancora diversi indicatori di pressione memoria:

- `ArkshValue` contiene `text[ARKSH_MAX_OUTPUT]`
- `ArkshValueItem` contiene `text[ARKSH_MAX_VALUE_TEXT]`
- ogni valore incorpora anche `ArkshObject`, `ArkshBlock`, metadata di lista/mappa e un puntatore a matrix
- `ArkshObjectCollection` resta ancora fixed-size
- `ArkshMatrix` ha ancora limiti statici rigidi su righe e colonne

Effetto pratico:

- copiare un valore costa molto
- qualsiasi array di valori pesa molto
- snapshot e restore diventano costosi
- e facile ricadere in stack pressure o heap churn

### 2. `ArkshShell` e troppo densa

In [include/arksh/shell.h](/Users/nicolo/Desktop/oosh/include/arksh/shell.h) `ArkshShell` contiene inline:

- registry comandi
- plugin caricati
- variabili shell
- binding typed
- funzioni
- classi
- istanze
- estensioni
- value resolver
- pipeline stage
- alias
- history
- job table
- positional parameters

Effetto pratico:

- la shell in memoria pesa molto anche quando il contenuto reale e piccolo
- clonare o snapshot-are lo stato costa tantissimo
- qualunque refactor su subshell o function scope paga il costo di una struttura monolitica

### 3. L'executor fa molte allocazioni temporanee

In [src/executor.c](/Users/nicolo/Desktop/oosh/src/executor.c) il helper `allocate_temp_buffer()` usa `calloc()` per ogni buffer temporaneo.

Questo pattern compare in percorsi molto caldi:

- block evaluation
- snapshot locali dei block
- function scope snapshot
- subshell state snapshot
- trasformazioni pipeline come `where`, `each`, `map`, `flat_map`, `reduce`, `group_by`, `pluck`

Effetto pratico:

- molte allocazioni piccole
- frammentazione
- overhead del memory allocator
- pressione sul CPU time anche quando la logica applicativa e semplice

### 4. Snapshot profondi di scope e stato shell

In [src/executor.c](/Users/nicolo/Desktop/oosh/src/executor.c) si vedono copie profonde ripetute:

- `snapshot_function_scope()`
- `snapshot_shell_state()`
- `restore_function_scope()`
- `restore_shell_state()`

Queste funzioni ricopiano:

- variabili shell
- binding typed
- classi
- istanze
- alias
- funzioni
- plugin metadata
- resolver e stage
- job table

Effetto pratico:

- il costo delle subshell non scala bene
- le funzioni shell pagano piu del necessario
- alcune feature "logiche" diventano costose per via del costo di snapshot

### 5. Le espansioni fanno clone di shell

In [src/expand.c](/Users/nicolo/Desktop/oosh/src/expand.c) la command substitution usa `clone_subshell()`, che fa:

- `calloc` di una `ArkshShell`
- assegnazione strutturale
- deep copy dei binding

Effetto pratico:

- `$(...)` diventa piu costoso del dovuto
- il costo cresce con la complessita dello stato shell corrente

### 6. Troppo render/parse/render

Il codice passa spesso per questa catena:

1. risolvo un valore
2. lo renderizzo in testo
3. riparso il testo o lo rivaluto
4. ricostruisco un valore

Questo succede soprattutto fra:

- evaluator
- resolver object-aware
- pipeline stage
- espansioni
- accesso ai membri con `->`

Effetto pratico:

- lavoro CPU inutile
- piu copie stringa
- piu superficie per edge case di parsing

## Hotspot CPU

### A. Allocazioni frequenti nell'executor

Il problema non e una singola allocazione grande, ma il numero di allocazioni piccole e ripetute.

Sintomi:

- `calloc/free` frequenti per buffer temporanei
- costi moltiplicati dentro pipeline e loop
- peggiore latenza sulle operazioni interattive

### B. Copie profonde di valori

`arksh_value_copy()` viene usata ovunque. Se il valore porta con se buffer grandi, ogni copia pesa.

Impatto alto in:

- binding
- pipeline stage
- classi/istanze
- snapshot funzioni e subshell

### C. Snapshot completi di shell

Una subshell o una function call oggi tende a lavorare copiando molto stato. Questo semplifica la correttezza, ma e costoso.

### D. Parse ricorsivo di selector e expression text

Quando una parte del runtime richiama `arksh_evaluate_line_value()` o parse/eval di frammenti testuali, la CPU paga:

- lexing
- parsing
- allocazioni temporanee
- nuove copie

Se il parser producesse nodi piu strutturati a monte, parte di questo costo sparirebbe.

## Hotspot memoria

### A. Strutture troppo larghe

Il principale problema di memoria non e soltanto il leak risk, ma il **payload per oggetto**.

Oggi:

- un valore semplice porta con se buffer grandi anche quando non servono
- la shell intera tiene inline anche sezioni quasi vuote
- alcuni contenitori restano statici anche quando i dati reali sono pochi

### B. Duplicazione di stato

Quando uno snapshot copia binding, classi e istanze, la memoria residente cresce rapidamente.

### C. History e registri inline

La history e altri array statici dentro `ArkshShell` rendono il costo base della shell piu alto del necessario.

## Miglioramenti consigliati

## P0. Misurazione prima del refactor

Prima di toccare i layout principali conviene introdurre metriche minime:

- tempo di startup `arksh -c 'true'`
- tempo di pipeline object-aware su liste piccole, medie e grandi
- tempo di `read_json() -> get_path()`
- numero di allocazioni per comando
- picco RSS su alcuni script di riferimento

Strumenti consigliati:

- macOS: Instruments `Time Profiler` e `Allocations`
- Linux: `perf`, `heaptrack`, `valgrind massif`
- Windows: Visual Studio Profiler o WPA/ETW

Deliverable minimo:

- un documento `benchmarks-baseline.md`
- un set di script benchmark in `tests/perf/`

### Priorita

Molto alta. Senza baseline il rischio e ottimizzare alla cieca.

## P1. Introdurre una scratch arena per executor ed espansioni

### Obiettivo

Ridurre drasticamente il numero di `calloc/free` nel path caldo.

### Intervento

Creare un allocator a frame o arena temporanea:

- una arena per `arksh_shell_execute_line()`
- una arena per pipeline stage e evaluation nested
- reset a fine comando, non `free` per ogni buffer

### Dove intervenire

- [src/executor.c](/Users/nicolo/Desktop/oosh/src/executor.c)
- [src/expand.c](/Users/nicolo/Desktop/oosh/src/expand.c)
- eventuale nuovo modulo `src/arena.c` con [include/arksh/arena.h](/Users/nicolo/Desktop/oosh/include/arksh/arena.h)

### Benefici attesi

- meno CPU spesa nel memory allocator
- meno frammentazione
- meno codice di cleanup sparso

### Rischi

- lifecycle management da progettare bene
- attenzione ai valori che devono sopravvivere oltre il comando corrente

### Priorita

Massima. E il miglior intervento CPU a basso rischio semantico.

## P2. Alleggerire `ArkshValue`

### Obiettivo

Ridurre il costo di copia e il footprint per valore.

### Intervento

Portare `ArkshValue` verso una forma piu compatta:

- usare una union vera per i payload specifici di tipo
- spostare testo lungo su heap
- tenere inline solo small-string o metadata minimi
- separare `ArkshBlock`, map, list, matrix e object payload da un core piu piccolo

Una direzione pratica:

- `ArkshValue` piccolo: `kind`, flags, small inline buffer, union di puntatori/payload piccoli
- `ArkshString`, `ArkshList`, `ArkshMap`, `ArkshMatrix`, `ArkshBlockPayload` su heap

### Dove intervenire

- [include/arksh/object.h](/Users/nicolo/Desktop/oosh/include/arksh/object.h)
- [src/object.c](/Users/nicolo/Desktop/oosh/src/object.c)
- [src/executor.c](/Users/nicolo/Desktop/oosh/src/executor.c)
- [src/shell.c](/Users/nicolo/Desktop/oosh/src/shell.c)

### Benefici attesi

- meno memoria per valore
- copie piu leggere
- meno rischio di stack pressure in futuro

### Rischi

- refactor ampio
- serve disciplina chiara su ownership e free

### Priorita

Molto alta.

## P3. Alleggerire `ArkshShell`

### Obiettivo

Ridurre memoria residente e costo di snapshot.

### Intervento

Spezzare `ArkshShell` in sottostrutture heap-owned:

- `command_registry`
- `plugin_registry`
- `binding_store`
- `function_store`
- `class_store`
- `extension_registry`
- `history_store`
- `job_store`

La shell top-level dovrebbe contenere solo:

- stato runtime essenziale
- puntatori a sottosistemi
- piccoli contatori e flag

### Dove intervenire

- [include/arksh/shell.h](/Users/nicolo/Desktop/oosh/include/arksh/shell.h)
- [src/shell.c](/Users/nicolo/Desktop/oosh/src/shell.c)
- [src/executor.c](/Users/nicolo/Desktop/oosh/src/executor.c)

### Benefici attesi

- meno memoria base per istanza shell
- clone/snapshot molto piu economici
- struttura piu estensibile

### Rischi

- refactor trasversale
- necessita di API piu pulite tra moduli

### Priorita

Molto alta.

## P4. Ridurre snapshot profondi con copy-on-write o frame locali

### Obiettivo

Evitare di copiare l'intero stato shell quando basta isolare un sottoinsieme.

### Intervento

Strategia consigliata:

- per le funzioni: frame locale per `vars`, `bindings`, `positional_params`
- per i block: scope chain locale invece di snapshot/restore di binding individuali
- per le subshell: snapshot per overlay o copy-on-write, non copia completa eager

In pratica:

- introdurre `ArkshScopeFrame`
- i lookup leggono dal frame locale e poi dal parent
- le mutazioni locali non toccano il parent salvo casi espliciti

### Benefici attesi

- meno copie
- meno cleanup
- costo piu lineare e prevedibile

### Rischi

- cambia una parte importante della semantica interna
- va testato molto bene

### Priorita

Alta, ma dopo P1-P3.

## P5. Evitare parse/eval ricorsivi di testo

### Obiettivo

Smettere di rientrare nel parser per operazioni che possono essere modellate in AST.

### Intervento

Estendere il parser per rappresentare meglio:

- chain di accessi `a -> b() -> c`
- argomenti stage/value gia parse-ati
- selector annidati

In questo modo il runtime riceve strutture pronte da valutare, invece di dover:

- renderizzare
- riparsare
- rivalutare

### Dove intervenire

- [src/parser.c](/Users/nicolo/Desktop/oosh/src/parser.c)
- [include/arksh/ast.h](/Users/nicolo/Desktop/oosh/include/arksh/ast.h)
- [src/executor.c](/Users/nicolo/Desktop/oosh/src/executor.c)

### Benefici attesi

- meno CPU per comando
- meno dipendenza da stringhe temporanee
- meno bug edge-case nel parsing ricorsivo

### Priorita

Alta.

## P6. Rendere dinamici i contenitori ancora statici

### Obiettivo

Ridurre memoria sprecata e limiti artificiali.

### Intervento

Completare la transizione verso strutture dinamiche per:

- `ArkshObjectCollection`
- matrix
- registry plugin/estensioni/stage/resolver
- history
- positional parameters

Questo non e solo un lavoro di capienza: riduce anche la memoria "sempre allocata".

### Priorita

Media-alta.

## P7. Ridurre render e copie stringa

### Obiettivo

Diminuire il numero di passaggi testuali intermedi.

### Intervento

- introdurre API che operano su `ArkshValue` senza render automatico
- separare i casi "mi serve testo per output" dai casi "mi serve il valore"
- ridurre `copy_string()` nei path che potrebbero lavorare per riferimento o length-aware

### Priorita

Media.

## P8. Ottimizzazioni mirate su sottosistemi specifici

### Aree candidate

- JSON parser: parsing streaming o con meno copie intermedie
- completion: cache dei candidati invalidata per cwd/registry change
- prompt rendering: cache dei segmenti costosi
- class/instance lookup: eventuale hash map per nome/id invece di scan lineare
- extension, resolver e stage lookup: indicizzazione per nome

### Priorita

Media. Molto utili, ma dopo i refactor strutturali.

## Ordine di esecuzione consigliato

### Fase 1. Misura e preparazione

- baseline benchmark
- contatori allocazioni
- macro opzionali per tracing

### Fase 2. Riduzione allocazioni temporanee

- scratch arena in executor
- scratch arena in expand

### Fase 3. Riduzione memoria strutturale

- alleggerire `ArkshValue`
- alleggerire `ArkshShell`
- rendere dinamici i registri piu pesanti

### Fase 4. Riduzione copie

- frame locali invece di snapshot profondi
- overlay/copy-on-write per subshell

### Fase 5. Riduzione costo parser/runtime

- member chain AST
- meno parse/eval di stringhe
- caching selettivo

### Fase 6. Ottimizzazioni mirate

- lookup indicizzati
- cache prompt/completion
- tuning JSON e matrix

## Criteri di successo

Un miglioramento reale dovrebbe mostrare almeno alcuni di questi risultati:

- meno allocazioni per comando
- meno `calloc/free` nei profiler
- riduzione del tempo di `-c` su pipeline object-aware
- riduzione del tempo su JSON grandi
- riduzione della RSS in REPL e nei test di script
- assenza di regressioni semantiche nella suite esistente

## Benchmark minimi da introdurre

### Shell startup

- `arksh -c 'true'`
- `arksh -c 'list(1,2,3) |> sort(value desc)'`

### Pipeline object-aware

- lista numerica da 1k e 10k elementi
- `children() |> where(...) |> sort(...)`

### JSON

- `read_json() -> get_path(...)`
- `read_json() -> set_path(...) -> to_json()`

### Scope e funzioni

- loop con chiamate funzione piccole
- block con `each/reduce` ripetuti

### Subshell e command substitution

- `$(pwd)`
- `$(cat file)`
- subshell con binding e classi attivi

## Rischi da evitare

- ottimizzare prima del profiling e peggiorare la leggibilita senza beneficio misurabile
- introdurre caching troppo aggressivo e invalidazione fragile
- mischiare nello stesso refactor layout dati, semantica e ottimizzazioni executor
- ridurre i test proprio mentre si tocca ownership e lifetime

## Raccomandazione finale

Se l'obiettivo e migliorare davvero occupazione di CPU e memoria senza destabilizzare il progetto, la sequenza migliore e:

1. misurare
2. introdurre scratch arena
3. snellire `ArkshValue`
4. spezzare `ArkshShell`
5. sostituire gli snapshot profondi con frame e overlay
6. ridurre parse/render ricorsivi

Questa sequenza massimizza il ritorno pratico e riduce il rischio di refactor troppo invasivi fatti nel punto sbagliato del progetto.
