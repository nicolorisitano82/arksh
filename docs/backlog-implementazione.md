# Backlog incrementale per completare `oosh`

## Scopo

Questo documento traduce la roadmap alta in un backlog operativo che possiamo usare turno per turno.

Ogni punto ha:

- una **epoca**: blocco di lavoro macro e ordinato nel tempo
- una **story**: comportamento utente o risultato osservabile
- uno o piu **task**: lavoro tecnico implementabile in una singola iterazione o in poche iterazioni brevi

L'idea e semplice:

1. tu mi passi un ID, per esempio `E2-S1-T2`
2. io implemento quel punto
3. aggiorno questo file mettendo il flag corretto
4. andiamo avanti fino alla chiusura completa del backlog

## Convenzioni

### Flag

- `[ ]` non iniziato
- `[~]` in corso
- `[x]` completato
- `[!]` bloccato o richiede decisione

### Regola di aggiornamento

- quando tutti i task di una story sono `[x]`, si marca anche la story come `[x]`
- quando tutte le story di una epoca sono `[x]`, si marca anche l'epoca come `[x]`
- se durante l'implementazione emerge una dipendenza nuova, si aggiunge un task con un nuovo ID senza rinumerare quelli esistenti

### Formato ID

- epoca: `E1`
- story: `E1-S1`
- task: `E1-S1-T1`

### Prompt consigliati

- `Implementa E1-S1-T1`
- `Implementa tutta la story E3-S2`
- `Procedi con la prossima task aperta di E5`
- `Aggiorna il backlog dopo aver completato E2-S3-T2`

## Stato di partenza

Questo backlog parte dallo stato attuale del repository, in cui sono gia presenti:

- parser AST-based e executor
- shell pipeline e object pipeline base
- object model typed
- block literal e binding con `let`
- plugin runtime
- prompt configurabile
- history, completion base e job control iniziale
- `if`, `while`, `for`, ternario e `switch`

Il backlog sotto copre **il rimanente** verso una shell completa e usabile.

## Ordine raccomandato

1. `E1` Linguaggio shell completo
2. `E2` Espansioni e semantica dei parametri
3. `E3` Built-in dentro pipeline e integrazione shell/object
4. `E4` Job control e TTY robusti
5. `E5` UX interattiva di livello quotidiano
6. `E6` Object model avanzato e plugin typed
7. `E7` JSON e dati strutturati a livello prodotto
8. `E8` Qualita, test e CI
9. `E9` Packaging, release e documentazione finale

---

## E1. Linguaggio shell completo

Stato epoca: `[x]`

### E1-S1. Funzioni shell

Stato story: `[x]`

- `[x]` `E1-S1-T1` aggiungere nodi AST per definizione e invocazione di funzioni shell
- `[x]` `E1-S1-T2` estendere lexer/parser per sintassi funzione dichiarativa coerente
- `[x]` `E1-S1-T3` aggiungere tabella funzioni nel runtime della shell
- `[x]` `E1-S1-T4` implementare scope, parametri e ritorno del risultato
- `[x]` `E1-S1-T5` aggiungere test per definizione, ridefinizione e chiamata

### E1-S2. Grouping e subshell

Stato story: `[x]`

- `[x]` `E1-S2-T1` aggiungere supporto AST per `{ ...; }`
- `[x]` `E1-S2-T2` aggiungere supporto AST per `( ... )`
- `[x]` `E1-S2-T3` implementare esecuzione grouped nel processo corrente
- `[x]` `E1-S2-T4` implementare esecuzione subshell con stato isolato
- `[x]` `E1-S2-T5` aggiungere test per redirection, env e binding nei due casi

### E1-S3. `break`, `continue`, `return`

Stato story: `[x]`

- `[x]` `E1-S3-T1` introdurre un modello interno per i control signal dell'executor
- `[x]` `E1-S3-T2` aggiungere built-in o nodi dedicati per `break` e `continue`
- `[x]` `E1-S3-T3` implementare `return` dentro funzioni shell
- `[x]` `E1-S3-T4` propagare correttamente i segnali di controllo nei loop annidati
- `[x]` `E1-S3-T5` aggiungere test di regressione su loop e funzioni

### E1-S4. `case`, `until`, `exec`, `eval`, `wait`, `trap`

Stato story: `[x]`

- `[x]` `E1-S4-T1` implementare `case` in stile shell classica
- `[x]` `E1-S4-T2` implementare `until`
- `[x]` `E1-S4-T3` implementare `exec`
- `[x]` `E1-S4-T4` implementare `eval`
- `[x]` `E1-S4-T5` implementare `wait`
- `[x]` `E1-S4-T6` progettare e implementare `trap` minimo

### E1-S5. Heredoc e redirection avanzata

Stato story: `[x]`

- `[x]` `E1-S5-T1` estendere il lexer per `<<` e `<<-`
- `[x]` `E1-S5-T2` aggiungere supporto parser per heredoc
- `[x]` `E1-S5-T3` implementare esecuzione heredoc nel platform layer
- `[x]` `E1-S5-T4` aggiungere redirection su fd arbitrari: `3>`, `n>&m`, `n<&m`
- `[x]` `E1-S5-T5` aggiungere test su pipe, heredoc e fd custom

---

## E2. Espansioni e semantica dei parametri

Stato epoca: `[x]`

### E2-S1. Positional parameters e parametri speciali

Stato story: `[x]`

- `[x]` `E2-S1-T1` aggiungere storage per `$0`, `$1..$N`
- `[x]` `E2-S1-T2` supportare `$@`, `$*`, `$#`
- `[x]` `E2-S1-T3` supportare `$$`, `$!`, `$-`
- `[x]` `E2-S1-T4` allineare `source` e funzioni shell alla semantica dei parametri
- `[x]` `E2-S1-T5` aggiungere test su script e chiamate funzione

### E2-S2. Parameter expansion avanzata

Stato story: `[x]`

- `[x]` `E2-S2-T1` implementare `${var:-default}` e `${var:=default}`
- `[x]` `E2-S2-T2` implementare `${var:+alt}` e `${var:?message}`
- `[x]` `E2-S2-T3` implementare `${#var}`
- `[x]` `E2-S2-T4` implementare forme di trim pattern come `${var%pat}` e `${var%%pat}`
- `[x]` `E2-S2-T5` aggiungere test per combinazioni con quoting

### E2-S3. Field splitting, `IFS` e quote removal

Stato story: `[x]`

- `[x]` `E2-S3-T1` formalizzare l'ordine completo: expansion -> splitting -> quote removal
- `[x]` `E2-S3-T2` introdurre `IFS` nel runtime shell
- `[x]` `E2-S3-T3` applicare field splitting ai contesti corretti
- `[x]` `E2-S3-T4` completare quote removal
- `[x]` `E2-S3-T5` aggiungere suite di test mirata solo per quoting e splitting

### E2-S4. Arithmetic expansion e globbing coerente

Stato story: `[x]`

- `[x]` `E2-S4-T1` implementare arithmetic expansion
- `[x]` `E2-S4-T2` definire i contesti in cui il globbing deve o non deve attivarsi
- `[x]` `E2-S4-T3` allineare comportamento Linux/macOS/Windows sui glob
- `[x]` `E2-S4-T4` aggiungere test cross-platform per pattern edge case

---

## E3. Built-in nelle pipeline e integrazione shell/object

Stato epoca: `[ ]`

### E3-S1. Classificazione dei built-in

Stato story: `[ ]`

- `[ ]` `E3-S1-T1` classificare i built-in in puri, mutanti e misti
- `[ ]` `E3-S1-T2` documentare la politica di esecuzione in pipeline
- `[ ]` `E3-S1-T3` aggiornare il registry comandi con metadata di esecuzione

### E3-S2. Built-in dentro shell pipeline multi-stage

Stato story: `[ ]`

- `[ ]` `E3-S2-T1` permettere built-in puri come stage intermedi di pipeline
- `[ ]` `E3-S2-T2` decidere e implementare il comportamento dei built-in mutanti
- `[ ]` `E3-S2-T3` unificare redirection e pipe per built-in ed esterni
- `[ ]` `E3-S2-T4` aggiungere test su `history`, `type`, `plugin list` e comandi equivalenti

### E3-S3. Bridge shell/object piu naturale

Stato story: `[ ]`

- `[ ]` `E3-S3-T1` progettare una conversione canonica tra stdout testuale e valori typed
- `[ ]` `E3-S3-T2` aggiungere helper o sintassi per passare output processo in pipeline object-aware
- `[ ]` `E3-S3-T3` aggiungere test su composizione shell/object nella stessa riga

### E3-S4. Confine chiaro tra comando, value expression e object expression

Stato story: `[ ]`

- `[ ]` `E3-S4-T1` formalizzare le regole di dispatch in documentazione tecnica
- `[ ]` `E3-S4-T2` ridurre i casi ambigui nel parser
- `[ ]` `E3-S4-T3` aggiungere test di regressione su input ambigui

---

## E4. Job control e TTY robusti

Stato epoca: `[ ]`

### E4-S1. Process group completi per pipeline foreground

Stato story: `[ ]`

- `[ ]` `E4-S1-T1` assegnare process group coerenti a pipeline composte
- `[ ]` `E4-S1-T2` aggiornare `fg` per pipeline e non solo per processi semplici
- `[ ]` `E4-S1-T3` testare stop/resume su pipeline reali

### E4-S2. `wait` e reporting robusto degli status

Stato story: `[ ]`

- `[ ]` `E4-S2-T1` introdurre built-in `wait`
- `[ ]` `E4-S2-T2` tracciare status finali e segnali in modo piu ricco
- `[ ]` `E4-S2-T3` aggiungere test su `wait`, exit code e job terminati

### E4-S3. TTY e segnali affidabili

Stato story: `[ ]`

- `[ ]` `E4-S3-T1` consolidare il restore del terminale su errori e segnali
- `[ ]` `E4-S3-T2` verificare `Ctrl-C` e `Ctrl-Z` in scenari annidati
- `[ ]` `E4-S3-T3` aggiungere smoke test PTY dedicati

### E4-S4. Comportamento equivalente su Windows

Stato story: `[ ]`

- `[ ]` `E4-S4-T1` documentare i limiti POSIX-non-portabili
- `[ ]` `E4-S4-T2` implementare il miglior equivalente possibile per Windows
- `[ ]` `E4-S4-T3` aggiungere test su runner Windows reale

---

## E5. UX interattiva di livello quotidiano

Stato epoca: `[ ]`

### E5-S1. Modello editor multilinea

Stato story: `[x]`

- `[x]` `E5-S1-T1` introdurre buffer multilinea interno
- `[x]` `E5-S1-T2` supportare editing e rendering su piu righe
- `[x]` `E5-S1-T3` allineare il prompt secondario ai blocchi incompleti

### E5-S2. Reverse search e movimenti per parola

Stato story: `[x]`

- `[x]` `E5-S2-T1` implementare reverse search della history
- `[x]` `E5-S2-T2` aggiungere movimenti per parola avanti/indietro
- `[x]` `E5-S2-T3` aggiungere test manuali e automatizzati PTY

### E5-S3. Kill/yank e undo

Stato story: `[ ]`

- `[ ]` `E5-S3-T1` introdurre kill buffer
- `[ ]` `E5-S3-T2` implementare yank
- `[ ]` `E5-S3-T3` implementare undo locale sulla linea

### E5-S4. Completion avanzata

Stato story: `[ ]`

- `[ ]` `E5-S4-T1` completare alias, funzioni e env var
- `[ ]` `E5-S4-T2` completare binding typed e namespace object-aware
- `[ ]` `E5-S4-T3` completare membri plugin e stage pipeline plugin
- `[ ]` `E5-S4-T4` migliorare la presentazione del mini-help contestuale

### E5-S5. Migliorie opzionali di UX

Stato story: `[ ]`

- `[ ]` `E5-S5-T1` valutare syntax highlighting
- `[ ]` `E5-S5-T2` valutare autosuggestion
- `[ ]` `E5-S5-T3` decidere se tenerle nel core o dietro plugin/modulo opzionale

---

## E6. Object model avanzato e plugin typed

Stato epoca: `[ ]`

### E6-S1. Namespace object-aware aggiuntivi

Stato story: `[ ]`

- `[ ]` `E6-S1-T1` progettare `fs()`
- `[ ]` `E6-S1-T2` progettare `user()`
- `[ ]` `E6-S1-T3` progettare `sys()` e `time()`
- `[ ]` `E6-S1-T4` decidere se `net()` entra nel core o come plugin ufficiale

### E6-S2. Plugin che introducono nuovi tipi completi

Stato story: `[ ]`

- `[ ]` `E6-S2-T1` estendere ABI plugin con descrizione di nuovi tipi/value kind
- `[ ]` `E6-S2-T2` chiarire ownership memoria per valori creati dai plugin
- `[ ]` `E6-S2-T3` aggiungere plugin di esempio con tipo custom completo

### E6-S3. Pipeline object-aware piu espressive

Stato story: `[ ]`

- `[ ]` `E6-S3-T1` aggiungere `map`
- `[ ]` `E6-S3-T2` aggiungere `filter` come alias o variante di `where`
- `[ ]` `E6-S3-T3` aggiungere `flat_map`
- `[ ]` `E6-S3-T4` aggiungere `group_by`
- `[ ]` `E6-S3-T5` aggiungere operatori aggregati: `sum`, `min`, `max`

### E6-S4. Introspezione e aiuto typed

Stato story: `[ ]`

- `[ ]` `E6-S4-T1` esporre metadati su resolver, stage e tipi
- `[ ]` `E6-S4-T2` migliorare `help` e completion con introspezione typed
- `[ ]` `E6-S4-T3` aggiungere documentazione per plugin author

---

## E7. JSON e dati strutturati a livello prodotto

Stato epoca: `[ ]`

### E7-S1. Parser/serializer robusti

Stato story: `[ ]`

- `[ ]` `E7-S1-T1` migliorare diagnostica con posizione/offset dell'errore
- `[ ]` `E7-S1-T2` coprire casi edge del parser
- `[ ]` `E7-S1-T3` coprire casi edge del serializer

### E7-S2. Dati grandi e annidati

Stato story: `[ ]`

- `[ ]` `E7-S2-T1` rimuovere o ridurre i limiti fissi piu stretti sui dati JSON
- `[ ]` `E7-S2-T2` aggiungere test su payload grandi
- `[ ]` `E7-S2-T3` verificare stabilita memoria e tempi

### E7-S3. Query e trasformazioni

Stato story: `[ ]`

- `[ ]` `E7-S3-T1` aggiungere accesso piu comodo a mappe annidate
- `[ ]` `E7-S3-T2` aggiungere helper di trasformazione dati strutturati
- `[ ]` `E7-S3-T3` testare round-trip file -> value -> transform -> file

---

## E8. Qualita, test e CI

Stato epoca: `[ ]`

### E8-S1. Test unitari mirati

Stato story: `[ ]`

- `[ ]` `E8-S1-T1` aggiungere test unitari per lexer
- `[ ]` `E8-S1-T2` aggiungere test unitari per parser
- `[ ]` `E8-S1-T3` aggiungere test unitari per executor
- `[ ]` `E8-S1-T4` aggiungere test unitari per object model

### E8-S2. Test golden e PTY

Stato story: `[ ]`

- `[ ]` `E8-S2-T1` creare golden test per script `.oosh`
- `[ ]` `E8-S2-T2` aggiungere PTY test per REPL e line editor
- `[ ]` `E8-S2-T3` aggiungere job control smoke test ripetibili

### E8-S3. Sanitizers e fuzzing

Stato story: `[ ]`

- `[ ]` `E8-S3-T1` aggiungere target ASan/UBSan
- `[ ]` `E8-S3-T2` integrare ASan/UBSan in CI
- `[ ]` `E8-S3-T3` aggiungere fuzzing su lexer/parser/expander

### E8-S4. CI multipiattaforma

Stato story: `[ ]`

- `[ ]` `E8-S4-T1` creare workflow CI Linux
- `[ ]` `E8-S4-T2` creare workflow CI macOS
- `[ ]` `E8-S4-T3` creare workflow CI Windows
- `[ ]` `E8-S4-T4` pubblicare matrice minima di supporto

---

## E9. Packaging, release e documentazione finale

Stato epoca: `[ ]`

### E9-S1. Installazione standard

Stato story: `[ ]`

- `[ ]` `E9-S1-T1` aggiungere target `install` CMake
- `[ ]` `E9-S1-T2` definire directory standard per config, cache, history e plugin
- `[ ]` `E9-S1-T3` allineare il runtime a queste directory

### E9-S2. Packaging per sistemi target

Stato story: `[ ]`

- `[ ]` `E9-S2-T1` preparare formula Homebrew o equivalente
- `[ ]` `E9-S2-T2` preparare pacchetto Linux iniziale
- `[ ]` `E9-S2-T3` preparare strategia Windows (`winget` o installer equivalente)

### E9-S3. ABI plugin e versioning

Stato story: `[ ]`

- `[ ]` `E9-S3-T1` versionare formalmente l'ABI plugin
- `[ ]` `E9-S3-T2` aggiungere capability flags
- `[ ]` `E9-S3-T3` aggiungere test di regressione ABI

### E9-S4. Documentazione finale e troubleshooting

Stato story: `[ ]`

- `[ ]` `E9-S4-T1` scrivere guida installazione
- `[ ]` `E9-S4-T2` scrivere guida scripting
- `[ ]` `E9-S4-T3` scrivere guida plugin author
- `[ ]` `E9-S4-T4` scrivere troubleshooting operativo

### E9-S5. Release process

Stato story: `[ ]`

- `[ ]` `E9-S5-T1` introdurre changelog
- `[ ]` `E9-S5-T2` definire checklist release
- `[ ]` `E9-S5-T3` preparare criteri per dichiarare la `1.0`

---

## Prossimi punti consigliati

Se vuoi procedere con il percorso piu lineare, i prossimi task consigliati sono:

- `E3-S1-T1` (classificazione built-in)
- `E4-S1-T1` (process group pipeline)
- `E5-S3-T1` (completamento comandi / tab completion)

Se invece vuoi puntare prima all'usabilita quotidiana della REPL, il percorso alternativo migliore e:

- `E5-S3-T1` (kill/yank)
- `E5-S4-T1` (completion avanzata)
- `E4-S1-T1` (process group pipeline)

## Regola finale

Non iniziare task di epoche avanzate se esistono blocchi aperti nelle epoche precedenti che cambiano il contratto del parser, dell'expander o del runtime shell. In pratica:

- prima il core
- poi l'ergonomia
- poi il vantaggio distintivo
- infine packaging e release
