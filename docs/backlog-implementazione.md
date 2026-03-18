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

### E2-S5. Operatori binari in contesto value

Stato story: `[x]`

Permette di scrivere `number(3) + number(4)` o `text("foo") + text("bar")` nel contesto
delle value expression. Gli operatori sono sovraccaricabili per tipo tramite il sistema
di extension (`__add__`, `__sub__`, ecc.). Non tocca il contesto shell.

- `[x]` `E2-S5-T1` aggiungere token `PLUS`, `MINUS`, `STAR`, `SLASH` nel lexer (neutri in contesto shell, operatori in contesto value) — implementato come parsing testuale, senza nuovi token lexer, per preservare glob e path
- `[x]` `E2-S5-T2` aggiungere nodo AST `OOSH_VALUE_SOURCE_BINARY_OP` con campi `left`, `op`, `right`
- `[x]` `E2-S5-T3` implementare parser di espressioni binarie nel contesto value con precedenza corretta (`*`/`/` > `+`/`-`) e guard per evitare false detection su path e comandi shell
- `[x]` `E2-S5-T4` implementare dispatch nell'executor: nativo per NUMBER, fallback su extension method (`__add__`, `__sub__`, `__mul__`, `__div__`) per altri tipi
- `[x]` `E2-S5-T5` aggiungere test su numeri (add, sub, mul, div, prec, assoc) — 6 test aggiunti

---

## E3. Built-in nelle pipeline e integrazione shell/object

Stato epoca: `[~]`

### E3-S1. Classificazione dei built-in

Stato story: `[x]`

- `[x]` `E3-S1-T1` classificare i built-in in puri, mutanti e misti
- `[x]` `E3-S1-T2` documentare la politica di esecuzione in pipeline
- `[x]` `E3-S1-T3` aggiornare il registry comandi con metadata di esecuzione

### E3-S2. Built-in dentro shell pipeline multi-stage

Stato story: `[x]`

- `[x]` `E3-S2-T1` permettere built-in puri come stage intermedi di pipeline (stage 0 PURE → output iniettato come text stdin agli stage esterni successivi)
- `[x]` `E3-S2-T2` decidere e implementare il comportamento dei built-in mutanti (MUTANT/MIXED in pipeline → errore con messaggio chiaro)
- `[x]` `E3-S2-T3` unificare redirection e pipe per built-in ed esterni: `2>`, `2>>`, `2>&1`, heredoc e FD redirections ora gestiti senza errore in `execute_builtin_with_redirection`
- `[x]` `E3-S2-T4` aggiungere test su `pwd | cat`, `history | cat`, `cd | cat` (WILL_FAIL)
- `[x]` `E3-S2-T5` aggiungere fallback in `apply_pipeline_stage`: se il nome dello stage non e riconosciuto, provare come method call sul sistema di extension (`find_extension`); permette overloading di `|>` tramite `extend`

### E3-S3. Bridge shell/object piu naturale

Stato story: `[x]`

- `[x]` `E3-S3-T1` conversione canonica: `text()` / `string()` gia supportati; `capture()` / `capture_lines()` sono gli idiomi espliciti; aggiunto `OOSH_VALUE_SOURCE_CAPTURE_SHELL` per la sintassi diretta
- `[x]` `E3-S3-T2` sintassi diretta: `<comando shell> |> <stage>` — se la sorgente non e riconosciuta come value expression, viene eseguita come comando shell e stdout diventa text value (es. `ls -la |> lines |> count`)
- `[x]` `E3-S3-T3` test: `oosh_shell_obj_bridge_count`, `oosh_shell_obj_bridge_words`, `oosh_shell_obj_bridge_trim`; esempio `10-shell-object-bridge.oosh`

### E3-S4. Confine chiaro tra comando, value expression e object expression

Stato story: `[x]`

- `[x]` `E3-S4-T1` formalizzare le regole di dispatch in documentazione tecnica (`docs/parser-dispatch.md`)
- `[x]` `E3-S4-T2` ridurre i casi ambigui nel parser — riconoscimento `true`/`false` come `BOOLEAN_LITERAL` prima del check `BINDING` in `parse_value_source_text_ex` e `parse_non_object_value_source_tokens`; fix: `true -> value` → `"true"`, `true -> type` → `"bool"`, `while true` / `if true` ancora funzionanti
- `[x]` `E3-S4-T3` test di regressione: `oosh_bool_lit_true_value`, `oosh_bool_lit_false_type`, `oosh_bool_lit_true_type`, `oosh_bool_lit_if_true`, `oosh_bool_lit_if_false` (126/126 pass)

### E3-S5. Overloading e hook dei comandi

Stato story: `[x]`

Permette di ridefinire qualsiasi comando built-in tramite una funzione shell con lo stesso
nome, e di chiamare comunque l'implementazione originale tramite `builtin <nome>`.
Pattern idiomatico (POSIX): `function cd(dir) do ... ; builtin cd $dir ; endfunction`.

- `[x]` `E3-S5-T1` implementare il built-in `builtin` (kind MUTANT): `command_builtin` in `shell.c` itera `shell->commands` e chiama il built-in direttamente; `execute_simple_command` in `executor.c` ora controlla `function_def` prima di `command_def` (funzioni override i built-in)
- `[x]` `E3-S5-T2` documentare il pattern di override — `examples/scripts/11-command-override.oosh` con esempi per `cd`, `pwd` e `builtin` diretto
- `[x]` `E3-S5-T3` test: `oosh_builtin_cd_override` (directory cambia), `oosh_builtin_pwd_override` (funzione prende priorità), `oosh_builtin_escape` (`builtin` bypassa la funzione); 132/132 pass

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

Stato story: `[x]`

- `[x]` `E5-S3-T1` kill buffer (file-static `s_kill_buf`): `^K` kill to EOL, `^U` kill to BOL, `^W` kill word backward
- `[x]` `E5-S3-T2` yank: `^Y` incolla il kill buffer al cursore
- `[x]` `E5-S3-T3` undo locale su singola linea: `^_` ripristina lo stato precedente alla ultima modifica

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

### E6-S5. Tipi numerici espliciti: Integer, Float, Double, Imaginary

Stato story: `[ ]`

Aggiunge costruttori di tipo numerico esplicito come resolver o funzioni di conversione,
separando la semantica di intero, floating-point a precisione singola/doppia e numero
immaginario. L'attuale tipo `number` copre solo double implicito; questa story lo rende
esplicito e affiancabile con precisioni diverse.

- `[ ]` `E6-S5-T1` aggiungere i value kind `OOSH_VALUE_INTEGER`, `OOSH_VALUE_FLOAT`, `OOSH_VALUE_DOUBLE`, `OOSH_VALUE_IMAGINARY` all'enum `OoshValueKind` in `object.h`; aggiornare `oosh_value_render`, `oosh_value_free` e `value_is_truthy` per i nuovi kind
- `[ ]` `E6-S5-T2` implementare i resolver `Integer(x)`, `Float(x)`, `Double(x)`, `Imaginary(x)` in `executor.c` (o `object.c`): parsano l'argomento, eseguono la conversione numerica e restituiscono un `OoshValue` del kind corretto
- `[ ]` `E6-S5-T3` esporre su ogni tipo le proprietà `value`, `type`, `bits` e i metodi `-> to_integer`, `-> to_float`, `-> to_double` per conversioni incrociate
- `[ ]` `E6-S5-T4` aritmetica mista: definire le regole di promozione quando operandi di kind diversi entrano in un `BINARY_OP` (es. `Integer + Float` → `Float`; qualsiasi operando `Imaginary` → `Imaginary`)
- `[ ]` `E6-S5-T5` test: `Integer("42") -> value` → `42`, `Float("3.14") -> type` → `float`, `Imaginary("2") -> value` → `2i`, conversioni incrociate, promozione in espressioni miste

#### Regole aritmetiche per Imaginary()

Un valore `Imaginary(b)` rappresenta il numero puramente immaginario **b·i**, dove `b` è
un `Double` interno. Non esiste un tipo `Complex` separato: la somma di una parte reale e
una immaginaria produce una coppia `(real, imag)` resa come stringa `"a+bi"` oppure,
se si vuole un tipo complex di prima classe, si rimanda a E6-S2 (plugin typed).

**Costruzione**

| Espressione | Risultato | Kind |
|---|---|---|
| `Imaginary(3)` | `3i` | `OOSH_VALUE_IMAGINARY` |
| `Imaginary(-1)` | `-1i` | `OOSH_VALUE_IMAGINARY` |
| `Imaginary(0)` | `0i` | `OOSH_VALUE_IMAGINARY` |

**Addizione e sottrazione**

- `Imaginary(a) + Imaginary(b)` → `Imaginary(a+b)` (rimane immaginario puro)
- `Imaginary(a) - Imaginary(b)` → `Imaginary(a-b)`
- `Real + Imaginary(b)` → stringa `"a+bi"` (nessun kind complex nativo in questa story)
- `Imaginary(b) + Real` → stesso risultato commutativo

**Moltiplicazione**

- `Imaginary(a) * Imaginary(b)` → `Double(-(a*b))` — perché `(ai)(bi) = ab·i² = -ab`
- `Real * Imaginary(b)` → `Imaginary(real*b)`
- `Imaginary(a) * Real` → `Imaginary(a*real)`

**Divisione**

- `Imaginary(a) / Imaginary(b)` → `Double(a/b)` — le unità `i` si cancellano
- `Imaginary(a) / Real` → `Imaginary(a/real)`
- `Real / Imaginary(b)` → `Imaginary(-(real/b))` — perché `r/(bi) = -r·i/b` (moltiplicando per `-i/-i`)

**Promozione di kind in espressioni miste**

Quando almeno un operando è `Imaginary`, il risultato è `Imaginary` (o `Double` per i
casi di cancellazione `i·i` sopra). Per tutti gli altri kind la gerarchia di promozione è:

```
Integer  <  Float  <  Double
```

Esempi: `Integer(2) + Float(1.5)` → `Float(3.5)`; `Float(1) * Double(2)` → `Double(2.0)`.

**Proprietà esposte**

| Proprietà | Tipo restituito | Descrizione |
|---|---|---|
| `-> value` | `Double` | parte immaginaria `b` (senza `i`) |
| `-> type` | `String` | `"imaginary"` |
| `-> real` | `Double` | sempre `0.0` per un immaginario puro |
| `-> imag` | `Double` | alias di `value` |
| `-> conjugate` | `Imaginary` | `Imaginary(-b)` |
| `-> magnitude` | `Double` | `abs(b)` |

**Casi limite**

- `Imaginary(0)` è equivalente a `Double(0)` per truthy (`value_is_truthy` → falso).
- Divisione per zero: `Imaginary(a) / Imaginary(0)` → errore `"division by zero"`.
- Conversione a `Integer`: troncamento della parte immaginaria con warning; `Integer(Imaginary(3))` → `0` (parte reale) con messaggio `"imaginary part discarded"`.

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

Se vuoi procedere con il percorso piu lineare (E3 → E4):

- `E3-S5-T1` (built-in `builtin` — sblocca override comandi, ~20 righe)
- `E4-S1-T1` (process group pipeline — job control robusto)
- `E4-S2-T1` (fg/bg/jobs — gestione job interattiva)

Se vuoi completare E3 rapidamente con i quick win rimasti:

- `E3-S5-T1` (`builtin` command — bypassa funzioni shell, chiama direttamente il built-in)
- `E3-S5-T2` (hook pre/post-comando)

Se vuoi puntare prima all'usabilita quotidiana della REPL:

- `E5-S4-T1` (completion avanzata — tab completion path/comandi)
- `E5-S5-T1` (syntax highlighting in-line)
- `E4-S1-T1` (process group pipeline)

## Regola finale

Non iniziare task di epoche avanzate se esistono blocchi aperti nelle epoche precedenti che cambiano il contratto del parser, dell'expander o del runtime shell. In pratica:

- prima il core
- poi l'ergonomia
- poi il vantaggio distintivo
- infine packaging e release
