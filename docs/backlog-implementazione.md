# Backlog incrementale per completare `arksh`

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

Stato epoca: `[~]`

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

### E1-S6. Compatibilità POSIX — gap residui

Stato story: `[x]`

Colma le lacune emerse dal confronto con le shell POSIX. Ogni task è indipendente e
può essere implementato isolatamente. L'ordine suggerito riflette l'impatto pratico
sugli script reali.

- `[x]` `E1-S6-T1` `(posix)` `trap` completo — estendere la tabella trap a tutti i segnali standard POSIX (`HUP INT QUIT ILL ABRT FPE SEGV PIPE ALRM TERM USR1 USR2 CHLD TSTP TTIN TTOU`); handler `ERR` (eseguito dopo ogni comando con exit code ≠ 0); `trap -p` per listare i trap attivi; `trap - SIG` per ripristinare il default
- `[x]` `E1-S6-T2` `(posix)` `set -e` / `-u` / `-x` / `-o` — implementare le opzioni di shell più usate negli script: `errexit` (esce al primo errore), `nounset` (errore su variabile non definita), `xtrace` (stampa ogni comando espanso con `+ `), `pipefail` (propaga il codice di uscita non-zero in pipeline); `set -o` per listare lo stato di tutte le opzioni; `set +e` per disabilitare
- `[x]` `E1-S6-T3` `(posix)` built-in `read` — `read [-r] [-p prompt] [-t timeout] [-n nchars] var...`: legge una riga da stdin, applica `IFS` splitting sui token, assegna i campi alle variabili; `-r` disabilita l'escape backslash; `-p` scrive il prompt senza newline; `-t` timeout con exit 1 se scade; `-n` legge al massimo N caratteri
- `[x]` `E1-S6-T4` `(posix)` built-in `printf` completo — `printf format [args...]` con format string POSIX: `%s %d %i %u %o %x %X %f %e %g %c %%`; escape `\n \t \r \\ \0NNN \xNN`; padding e precisione (`%-10s`, `%.2f`); comportamento coerente con `/usr/bin/printf`
- `[x]` `E1-S6-T5` `(posix)` `${var/pattern/replacement}` e `${var//pattern/replacement}` — parameter substitution: prima occorrenza vs tutte; `${var/#pat/repl}` e `${var/%pat/repl}` per ancorare al prefisso o suffisso; pattern segue glob POSIX; `//` con replacement vuoto = cancellazione
- `[x]` `E1-S6-T6` `(posix)` built-in `getopts` — `getopts optstring name [args]`: parsing opzioni stile POSIX; aggiorna `OPTIND` e `OPTARG`; termina su `--` o primo argomento non-opzione; gestisce opzioni con argomento obbligatorio (`:`) e silent error mode (optstring inizia con `:`); necessario per script portabili con `while getopts ...`
- `[x]` `E1-S6-T7` `(posix)` `test` / `[` — completare gli operatori mancanti: test su file (`-e -f -d -r -w -x -s -L -p -b -c -S -g -u -k`); confronto stringa (`-z -n = != < >`); aritmetica intera (`-eq -ne -lt -le -gt -ge`); operatori compositi (`-a -o !`); verifica che `[ "$var" = "val" ]` e `test -f "$path"` producano il codice di uscita corretto
- `[x]` `E1-S6-T8` `(posix)` `$( )` command substitution — completare i casi edge: sostituzione annidata `$(cmd $(inner))`; sostituzione in assegnazione e in argomento di funzione; preservazione del trailing newline nell'interprete (rimozione solo nell'espansione); `$(< file)` come alternativa efficiente a `$(cat file)`

### E1-S7. Compatibilità POSIX — sintassi di base degli script

Stato story: `[x]`

Gap emersi dal test sistematico di script POSIX reali su arksh. Ogni task è indipendente
e può essere implementato isolatamente. L'ordine riflette l'impatto: i primi tre coprono
la stragrande maggioranza degli script esistenti.

- `[x]` `E1-S7-T1` `(posix-script)` `VAR=value` — assegnazione shell in stile POSIX: riconoscere `NOME=valore` in posizione di comando come assegnazione; supporto assegnazioni multiple consecutive (`A=1 B=2`), assegnazione vuota (`X=`), valore espanso (`X=$HOME`); rilevamento in `execute_simple_command` con `split_posix_assignment` senza modifiche al lexer
- `[x]` `E1-S7-T2` `(posix-script)` `VAR=val cmd` — env-prefix inline: assegnazioni prefisso passate come variabili esportate temporaneamente al comando, ripristinate dopo l'esecuzione; funziona per built-in, funzioni e comandi esterni
- `[ ]` `E1-S7-T3` `(posix-script)` `$((...))` — arithmetic expansion: funziona nell'object pipeline; non ancora supportata come espansione di argomenti shell standard (`echo $((3+4))`)
- `[x]` `E1-S7-T4` `(posix-script)` `f() { ... }` — sintassi funzione POSIX: `parse_posix_function_command_tokens` nel parser; `param_count = -1` come sentinella POSIX (nessun controllo arity, argomenti come `$1`/`$2`/…); accetta qualsiasi numero di argomenti posizionali
- `[x]` `E1-S7-T5` `(posix-script)` `case` — fix del pattern matching: `word)` senza spazio funziona; `(word)` funziona; `evaluate_switch_operand` usa `expand_single_word(COMMAND_NAME)` per bare words invece di `evaluate_expression_atom`
- `[x]` `E1-S7-T6` `(posix-script)` `shift` e `set --` — `shift [n]` e `set -- arg...` implementati in `shell.c`; `$#` aggiornato
- `[x]` `E1-S7-T7` `(posix-script)` `local` — built-in `local` implementato; dichiara variabile nel frame funzione corrente
- `[x]` `E1-S7-T8` `(posix-script)` fix minori: (a) `printf` — fix POSIX §2.2.3: backslash dentro doppi apici espanso solo per `$`, `` ` ``, `"`, `\`, newline letterale; (b) `readonly` — built-in implementato; (c) `echo -e/-n/-E` — built-in implementato con escape processing

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
- `[x]` `E2-S5-T2` aggiungere nodo AST `ARKSH_VALUE_SOURCE_BINARY_OP` con campi `left`, `op`, `right`
- `[x]` `E2-S5-T3` implementare parser di espressioni binarie nel contesto value con precedenza corretta (`*`/`/` > `+`/`-`) e guard per evitare false detection su path e comandi shell
- `[x]` `E2-S5-T4` implementare dispatch nell'executor: nativo per NUMBER, fallback su extension method (`__add__`, `__sub__`, `__mul__`, `__div__`) per altri tipi
- `[x]` `E2-S5-T5` aggiungere test su numeri (add, sub, mul, div, prec, assoc) — 6 test aggiunti

---

## E3. Built-in nelle pipeline e integrazione shell/object

Stato epoca: `[x]`

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

- `[x]` `E3-S3-T1` conversione canonica: `text()` / `string()` gia supportati; `capture()` / `capture_lines()` sono gli idiomi espliciti; aggiunto `ARKSH_VALUE_SOURCE_CAPTURE_SHELL` per la sintassi diretta
- `[x]` `E3-S3-T2` sintassi diretta: `<comando shell> |> <stage>` — se la sorgente non e riconosciuta come value expression, viene eseguita come comando shell e stdout diventa text value (es. `ls -la |> lines |> count`)
- `[x]` `E3-S3-T3` test: `arksh_shell_obj_bridge_count`, `arksh_shell_obj_bridge_words`, `arksh_shell_obj_bridge_trim`; esempio `10-shell-object-bridge.arksh`

### E3-S4. Confine chiaro tra comando, value expression e object expression

Stato story: `[x]`

- `[x]` `E3-S4-T1` formalizzare le regole di dispatch in documentazione tecnica (`docs/parser-dispatch.md`)
- `[x]` `E3-S4-T2` ridurre i casi ambigui nel parser — riconoscimento `true`/`false` come `BOOLEAN_LITERAL` prima del check `BINDING` in `parse_value_source_text_ex` e `parse_non_object_value_source_tokens`; fix: `true -> value` → `"true"`, `true -> type` → `"bool"`, `while true` / `if true` ancora funzionanti
- `[x]` `E3-S4-T3` test di regressione: `arksh_bool_lit_true_value`, `arksh_bool_lit_false_type`, `arksh_bool_lit_true_type`, `arksh_bool_lit_if_true`, `arksh_bool_lit_if_false` (126/126 pass)

### E3-S5. Overloading e hook dei comandi

Stato story: `[x]`

Permette di ridefinire qualsiasi comando built-in tramite una funzione shell con lo stesso
nome, e di chiamare comunque l'implementazione originale tramite `builtin <nome>`.
Pattern idiomatico (POSIX): `function cd(dir) do ... ; builtin cd $dir ; endfunction`.

- `[x]` `E3-S5-T1` implementare il built-in `builtin` (kind MUTANT): `command_builtin` in `shell.c` itera `shell->commands` e chiama il built-in direttamente; `execute_simple_command` in `executor.c` ora controlla `function_def` prima di `command_def` (funzioni override i built-in)
- `[x]` `E3-S5-T2` documentare il pattern di override — `examples/scripts/11-command-override.arksh` con esempi per `cd`, `pwd` e `builtin` diretto
- `[x]` `E3-S5-T3` test: `arksh_builtin_cd_override` (directory cambia), `arksh_builtin_pwd_override` (funzione prende priorità), `arksh_builtin_escape` (`builtin` bypassa la funzione); 132/132 pass

---

## E4. Job control e TTY robusti

Stato epoca: `[x]`

### E4-S1. Process group completi per pipeline foreground

Stato story: `[x]`

- `[x]` `E4-S1-T1` assegnare process group coerenti a pipeline composte (`setpgid` in figlio e genitore, `tcsetpgrp` prima/dopo attesa)
- `[x]` `E4-S1-T2` aggiornare `fg` per pipeline e non solo per processi semplici (pgid-based: `kill(-pgid, SIGCONT)` già presente; `out_stopped` aggiunge job alla tabella con `pid=last`, `pgid=leader`)
- `[x]` `E4-S1-T3` testare stop/resume su pipeline reali (verificato manualmente: `Ctrl-Z` su pipeline, `jobs`, `fg` riprende correttamente)

### E4-S2. `wait` e reporting robusto degli status

Stato story: `[x]`

- `[x]` `E4-S2-T1` introdurre built-in `wait` (già presente in `command_wait`; attende uno o più job per ID `%n`)
- `[x]` `E4-S2-T2` tracciare status finali e segnali: aggiunto campo `termination_signal` in `ArkshJob`; `arksh_shell_refresh_jobs` e `wait_for_job_at` lo valorizzano a `exit_code - 128` se `exit_code > 128`; `command_jobs` mostra `exit=N` / `signal=NAME`; helper `signal_name()` restituisce nome POSIX breve (HUP, INT, KILL, TERM, TSTP …); marcatori `+`/`-` per job corrente/precedente; `wait_for_job_at` produce messaggio `[n] done ...` con dettaglio exit/segnale
- `[x]` `E4-S2-T3` aggiunti 2 test CTest: `arksh_wait_done_message` (controlla che `wait` produca "done"), `arksh_jobs_current_marker` (controlla che `jobs` mostri `+` per il job corrente); 134 test tutti verdi

### E4-S3. TTY e segnali affidabili

Stato story: `[x]`

- `[x]` `E4-S3-T1` consolidare il restore del terminale su errori e segnali:
  - `setpgid` spostato PRIMA del reset dei segnali nel figlio della pipeline (`platform.c`) — chiude la race condition dove SIGINT poteva uccidere il figlio prima che si spostasse nel proprio pgid
  - aggiunto `signal(SIGPIPE, SIG_DFL)` esplicito nel figlio — garantisce che SIGPIPE non sia ereditata come SIG_IGN dalla shell
  - `waitpid` della pipeline foreground ora usa loop EINTR (`do { wpid = waitpid(...); } while (wpid < 0 && errno == EINTR)`) — un segnale spurio (es. SIGCHLD di un job background) non salta più silenziosamente l'attesa di un processo figlio
- `[x]` `E4-S3-T2` verificare `Ctrl-C` e `Ctrl-Z` in scenari annidati: testato manualmente — Ctrl-C su pipeline foreground termina i processi senza toccare la shell; Ctrl-Z ferma la pipeline e la aggiunge a `jobs` come `stopped`; le correzioni T1 chiudono le race condition teoriche
- `[x]` `E4-S3-T3` aggiunti 2 smoke test CTest: `arksh_pipeline_three_stage` (emit | echo | count → "3") e `arksh_pipeline_sigpipe_safe` (emit | count → "3") — verificano che pipeline multi-stadio completino senza crash; 136 test tutti verdi

### E4-S4. Comportamento equivalente su Windows

Stato story: `[x]`

- `[x]` `E4-S4-T1` documentare i limiti POSIX-non-portabili — blocco commento in `platform.c` prima della sezione Windows di `arksh_platform_run_process_pipeline`; elenca setpgid/tcsetpgrp/WUNTRACED/SIGTSTP come non portabili e descrive le alternative Windows disponibili (`_isatty`, `CREATE_NEW_PROCESS_GROUP` per background, `pgid_leader` come equivalente informativo)
- `[x]` `E4-S4-T2` implementare il miglior equivalente possibile per Windows — (a) `interactive = _isatty(0)` mirrors POSIX `isatty(STDIN_FILENO)`; (b) `should_capture_output` ora condizionato a `(!interactive || force_capture)` come su POSIX, così in modalità interattiva l'output va direttamente alla console; (c) `pgid_leader` traccia il PID del primo processo per coerenza con il job-table
- `[x]` `E4-S4-T3` aggiungere test su runner Windows reale — aggiunti `arksh_capture_text_first` e `arksh_capture_text_count` in CMakeLists.txt; esercitano il path `force_capture=1` (usato da `capture()` e `capture_lines()`) su tutte le piattaforme; 138 test tutti verdi

---

## E5. UX interattiva di livello quotidiano

Stato epoca: `[x]`

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

Stato story: `[x]`

La completion (in `line_editor.c`) gestisce le seguenti sorgenti:
- `collect_registered_command_matches` — built-in, alias, funzioni (in posizione comando)
- `collect_path_command_matches` — eseguibili nel `$PATH`
- `collect_file_matches` — path di file/directory
- `collect_env_var_matches` — variabili shell (`shell->vars`), attivata da prefisso `$`
- `collect_binding_matches` — binding typed `let` (`shell->bindings`), in contesto non-comando
- `collect_stage_matches` — stage built-in + stage plugin, attivata dopo `|>`

- `[x]` `E5-S4-T1` `collect_function_matches` — già inclusa in `collect_registered_command_matches` che itera `shell->functions`; kind `ARKSH_CMATCH_FN`, mostrato come `(fn)` nell'elenco multi-match
- `[x]` `E5-S4-T2` `collect_alias_matches` — già inclusa; kind `ARKSH_CMATCH_ALIAS`, mostrato come `(@)`
- `[x]` `E5-S4-T3` `collect_env_var_matches` — nuova; itera `shell->vars`, prefisso `$`; attivata quando il token inizia con `$`
- `[x]` `E5-S4-T4` `collect_binding_matches` — nuova; itera `shell->bindings`; attivata in contesto non-comando e non-stage; kind `ARKSH_CMATCH_BINDING`, mostrato come `(let)`
- `[x]` `E5-S4-T5` `collect_stage_matches` — nuova; array statico dei 16 stage built-in + iterazione `shell->pipeline_stages`; attivata da `is_pipeline_stage_position` (token preceduto da `|>`)
- `[x]` `E5-S4-T6` `ArkshCompletionKind` enum + campo `kinds[]` in `ArkshCompletionMatches`; `print_completion_matches` mostra suffisso tipo quando ci sono più match: `(fn)`, `(@)`, `(let)`; file, dir, var, stage e comandi non hanno suffisso (contesto già chiaro)

### E5-S5. Migliorie opzionali di UX

Stato story: `[x]`

Entrambe le funzionalità implementate nel core (in `line_editor.c`), attive solo quando
`arksh_line_editor_is_interactive()` è vero. Nessun plugin necessario.

- `[x]` `E5-S5-T1` **Syntax highlighting** — `highlight_line()`: state machine (S_NORMAL / S_COMMENT / S_SQ / S_DQ / S_VAR) con ANSI codes: keyword bold (`\033[1m`), stringhe verde (`\033[32m`), `$var` cyan (`\033[36m`), commenti grigio (`\033[90m`), operatori (`|>`, `->`, `|`, `&&`, `;`, `>>`) giallo (`\033[33m`). Lista keyword: `if then else elif fi while until do done for in case esac function endfunction return break continue true false`.
- `[x]` `E5-S5-T2` **Autosuggestion** — `find_autosuggestion()`: cerca nella history (dal più recente) la prima entry che inizia con il buffer corrente; mostra il suffisso in grigio (`\033[90m`) dopo il cursore quando `cursor == length`; il cursore viene riposizionato correttamente contando anche i caratteri visibili del ghost text.
- `[x]` `E5-S5-T3` **Decisione architetturale**: nel core — `redraw_line()` ora accetta `ArkshShell *shell`; passa `NULL` nei contesti senza UX (search mode); entrambe le feature disabilitate automaticamente in modalità non-interattiva (pipe/script).

### E5-S6. Tab completion di livello avanzato

Stato story: `[x]`

Porta la completion al livello di zsh / fish: contestuale per tipo di argomento,
filtrata per operatore di redirection, con double-TAB intelligente e fuzzy matching.
Ogni task è indipendente; i task T1–T3 sono prerequisiti naturali per T4–T6.

- `[x]` `E5-S6-T1` `(tab-advance)` completion path-aware dopo redirection — `is_redirection_position()`: se il token prima dell'attuale (saltando spazi) è `>` o `<`, attiva solo `collect_file_matches`
- `[x]` `E5-S6-T2` `(tab-advance)` completion filtrata per tipo di argomento — tabella `s_arg_filter_table` + `ArkshArgFilter` enum; `cd`/`pushd` → solo dir; `source`/`.` → `.arksh`/`.sh`; `plugin` → `.dylib`/`.so`/`.dll`; `collect_file_matches_filtered()` con switch interno
- `[x]` `E5-S6-T3` `(tab-advance)` completion delle opzioni (`--flag`) — tabella `s_flag_table` con opzioni per `ls`, `set`, `export`, `read`, `trap`, `printf`, `cd`; `collect_flag_matches()` attivata quando token inizia con `-` in posizione non-comando
- `[x]` `E5-S6-T4` `(tab-advance)` completion proprietà e metodi dopo `->` — già implementata via `collect_member_completion_matches` + `arksh_shell_collect_member_completions`; filtra per tipo del receiver dal registro estensioni
- `[x]` `E5-S6-T5` `(tab-advance)` double-TAB per listare tutti i match — `last_tab` + `prev_tab` tracciati nel main loop; `handle_completion(... force_list)`; primo TAB estende prefisso comune, secondo TAB mostra sempre la lista completa
- `[x]` `E5-S6-T6` `(tab-advance)` fuzzy / substring matching — `is_fuzzy_mode()` legge `completion_mode` dalla shell; fallback `collect_file_matches_fuzzy()` + `collect_registered_command_matches_fuzzy()` con `strstr`; attivati solo se prefisso non-vuoto e zero risultati con prefix match

---

## E6. Object model avanzato e plugin typed

Stato epoca: `[ ]`

### E6-S1. Namespace object-aware aggiuntivi

Stato story: `[x]`

- `[x]` `E6-S1-T1` progettare `fs()` — `cwd`, `home`, `temp`, `separator`
- `[x]` `E6-S1-T2` progettare `user()` — `name`, `home`, `shell`, `uid`/`gid` (POSIX)
- `[x]` `E6-S1-T3` progettare `sys()` e `time()` — `sys`: os/host/arch/cpu_count; `time`: epoch/year/month/day/hour/minute/second/iso
- `[x]` `E6-S1-T4` decidere se `net()` entra nel core o come plugin ufficiale — **plugin** (operazioni bloccanti, dipendenze socket, non universale)

### E6-S2. Plugin che introducono nuovi tipi completi

Stato story: `[x]`

- `[x]` `E6-S2-T1` estendere ABI plugin con descrizione di nuovi tipi/value kind
- `[x]` `E6-S2-T2` chiarire ownership memoria per valori creati dai plugin
- `[x]` `E6-S2-T3` aggiungere plugin di esempio con tipo custom completo

### E6-S3. Pipeline object-aware piu espressive

Stato story: `[x]`

- `[x]` `E6-S3-T1` aggiungere `map`
- `[x]` `E6-S3-T2` aggiungere `filter` come alias o variante di `where`
- `[x]` `E6-S3-T3` aggiungere `flat_map`
- `[x]` `E6-S3-T4` aggiungere `group_by`
- `[x]` `E6-S3-T5` aggiungere operatori aggregati: `sum`, `min`, `max`

### E6-S4. Introspezione e aiuto typed

Stato story: `[x]`

- `[x]` `E6-S4-T1` esporre metadati su resolver, stage e tipi — `description` aggiunto a `ArkshValueResolverDef` e `ArkshPipelineStageDef`; tutti i resolver e stage built-in registrati con descrizione; `register_builtin_pipeline_stages()` registra i 22 stage built-in al boot; `ARKSH_MAX_PIPELINE_STAGE_HANDLERS` alzato a 64; `ABI v4`
- `[x]` `E6-S4-T2` migliorare `help` e completion con introspezione typed — `help commands|resolvers|stages|types` mostra la sezione corrispondente con descrizioni; `help <name>` ricerca in tutte le categorie e mostra categoria + descrizione; `arksh_shell_print_help` ristrutturato con sezioni dinamiche; completion stage ora guidata da `shell->pipeline_stages[]` (rimosso array statico hardcoded da `line_editor.c`)
- `[x]` `E6-S4-T3` documentare per plugin author — `plugins/skeleton/README.md` aggiornato con guida completa: versione ABI, firma callback, descrizioni, `register_type_descriptor`, introspezione runtime; `sample_plugin.c` e `point_plugin.c` aggiornati con descrizioni

### E6-S5. Tipi numerici espliciti: Integer, Float, Double, Imaginary

Stato story: `[x]`

Aggiunge costruttori di tipo numerico esplicito come resolver o funzioni di conversione,
separando la semantica di intero, floating-point a precisione singola/doppia e numero
immaginario. L'attuale tipo `number` copre solo double implicito; questa story lo rende
esplicito e affiancabile con precisioni diverse.

- `[x]` `E6-S5-T1` aggiungere i value kind `ARKSH_VALUE_INTEGER`, `ARKSH_VALUE_FLOAT`, `ARKSH_VALUE_DOUBLE`, `ARKSH_VALUE_IMAGINARY` all'enum `ArkshValueKind` in `object.h`; aggiornare `arksh_value_render`, `arksh_value_free` e `value_is_truthy` per i nuovi kind
- `[x]` `E6-S5-T2` implementare i resolver `Integer(x)`, `Float(x)`, `Double(x)`, `Imaginary(x)` in `executor.c` (o `object.c`): parsano l'argomento, eseguono la conversione numerica e restituiscono un `ArkshValue` del kind corretto
- `[x]` `E6-S5-T3` esporre su ogni tipo le proprietà `value`, `type`, `bits` e i metodi `-> to_integer`, `-> to_float`, `-> to_double` per conversioni incrociate
- `[x]` `E6-S5-T4` aritmetica mista: definire le regole di promozione quando operandi di kind diversi entrano in un `BINARY_OP` (es. `Integer + Float` → `Float`; qualsiasi operando `Imaginary` → `Imaginary`)
- `[x]` `E6-S5-T5` test: `Integer("42") -> value` → `42`, `Float("3.14") -> type` → `float`, `Imaginary("2") -> value` → `2i`, conversioni incrociate, promozione in espressioni miste

#### Regole aritmetiche per Imaginary()

Un valore `Imaginary(b)` rappresenta il numero puramente immaginario **b·i**, dove `b` è
un `Double` interno. Non esiste un tipo `Complex` separato: la somma di una parte reale e
una immaginaria produce una coppia `(real, imag)` resa come stringa `"a+bi"` oppure,
se si vuole un tipo complex di prima classe, si rimanda a E6-S2 (plugin typed).

**Costruzione**

| Espressione | Risultato | Kind |
|---|---|---|
| `Imaginary(3)` | `3i` | `ARKSH_VALUE_IMAGINARY` |
| `Imaginary(-1)` | `-1i` | `ARKSH_VALUE_IMAGINARY` |
| `Imaginary(0)` | `0i` | `ARKSH_VALUE_IMAGINARY` |

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

### E6-S6. Tipo Dict — array associativo chiave-valore

Stato story: `[x]`

Introduce `Dict()` come tipo di primo livello nell'object model di arksh: un array associativo
con chiavi stringa e valori di qualsiasi `ArkshValue`. Supporta getter, setter, cancellazione di
chiavi e round-trip JSON (import da stringa JSON, export verso stringa JSON).

Il tipo è immutabile per default — ogni operazione di scrittura restituisce una nuova istanza —
coerentemente con la filosofia degli altri tipi arksh. Non esiste aliasing né condivisione di
stato interno tra istanze.

**Interfaccia prevista**

```arksh
let d = Dict()                        # dizionario vuoto
let d2 = d -> set("nome", "alice")    # nuovo dict con "nome"="alice"
let v = d2 -> get("nome")             # "alice"
let d3 = d2 -> delete("nome")         # rimuove la chiave
let ok = d2 -> has("nome")            # true
let ks = d2 -> keys                   # ["nome"]
let vs = d2 -> values                 # ["alice"]
let n = d2 -> count                   # 1
let j = d2 -> to_json                 # '{"nome":"alice"}'
let d4 = Dict() -> from_json(j)       # importa da stringa JSON
```

**Task**

- `[x]` `E6-S6-T1` **Struttura interna** — aggiungere `ARKSH_VALUE_DICT` a `ArkshValueKind` in `object.h`; definire `ArkshDict` come array di `{char key[ARKSH_MAX_TOKEN]; ArkshValue value;}` con un campo `count` e limite `ARKSH_MAX_DICT_ENTRIES` (es. 128); aggiornare `arksh_value_free` (ricorsivo sulle entry), `arksh_value_copy` (deep copy), `arksh_value_render` (formato `{k1: v1, k2: v2}`) e `value_is_truthy` (`count > 0`)
- `[x]` `E6-S6-T2` **Resolver `Dict()`** — registrare il resolver in `executor.c`; senza argomenti restituisce un dict vuoto; con argomenti a coppie `"chiave", valore` (argc pari) costruisce il dict inline; errore se argc dispari o una chiave non è stringa
- `[x]` `E6-S6-T3` **Metodi di scrittura** — implementare come pipeline-method o class-method in `executor.c`/`shell.c`: `set(key, value)` → nuovo dict con entry aggiunta o sovrascritta; `delete(key)` → nuovo dict senza quella chiave (no-op se assente); i metodi non mutano il receiver
- `[x]` `E6-S6-T4` **Metodi e proprietà di lettura** — `get(key)` → valore o stringa vuota se assente; `has(key)` → `true`/`false`; proprietà `keys` → `ArkshValue` lista di stringhe; `values` → `ArkshValue` lista dei valori; `count` → numero intero; `type` → `"dict"`
- `[x]` `E6-S6-T5` **Bridge JSON** — `-> to_json` serializza il dict come oggetto JSON (usando l'infrastruttura esistente in `object.c`/`arksh_value_to_json`); `-> from_json(str)` parsa una stringa JSON-object e costruisce un `ARKSH_VALUE_DICT` (usa il parser JSON esistente); errore chiaro se la stringa non è un oggetto JSON (`"from_json: expected JSON object"`)
- `[x]` `E6-S6-T6` **Test** — `Dict() -> count` → `0`; `Dict() -> set("x", 1) -> get("x")` → `1`; `Dict() -> has("y")` → `false`; `-> keys` e `-> values` su dict con 2 entry; `-> delete` su chiave esistente e inesistente; round-trip `-> to_json | Dict() -> from_json -> get("k")` restituisce il valore originale

---

### E6-S7. Stage di encoding: base64

Stato story: `[ ]`

Due stage simmetrici per codifica e decodifica Base64 RFC 4648, integrati nella pipeline
oggetti (`|>`). L'implementazione è in C puro senza dipendenze esterne: un encoder
standard e un decoder con gestione degli errori.

- `[ ]` `E6-S7-T1` **`base64_encode` stage** — implementare in `shell.c` lo stage `base64_encode`: riceve un valore stringa (o qualsiasi valore renderizzato come stringa), produce la stringa Base64 corrispondente secondo RFC 4648 §4 (alfabeto `A-Za-z0-9+/`, padding con `=`); la funzione C di encoding è pura e non richiede dipendenze esterne; registrare lo stage con `arksh_shell_register_pipeline_stage` con descrizione `"encode a string to Base64 (RFC 4648)"`; lo stage opera su `ARKSH_VALUE_STRING` e restituisce `ARKSH_VALUE_STRING`
- `[ ]` `E6-S7-T2` **`base64_decode` stage** — implementare lo stage `base64_decode`: riceve una stringa Base64 valida, produce la stringa decodificata; gestire correttamente il padding (`=`, `==`) e il caso di input vuoto; in caso di carattere non valido nell'alfabeto Base64 ritornare un errore esplicito (`"base64_decode: invalid character at position N"`); registrare con descrizione `"decode a Base64-encoded string (RFC 4648)"`
- `[ ]` `E6-S7-T3` **Test** — `text("hello") |> base64_encode` → `"aGVsbG8="` ; `text("aGVsbG8=") |> base64_decode` → `"hello"`; round-trip `text("arksh") |> base64_encode |> base64_decode` → `"arksh"`; input vuoto encode → `""`; input vuoto decode → `""`; input non valido decode → errore; verifica che lo stage sia visibile in `help stages`

---

### E6-S8. Tipo Matrix — struttura dati matriciale ispirata ai DataFrame

Stato story: `[ ]`

Introduce `Matrix` come tipo di primo livello nell'object model di arksh: una tabella
bidimensionale con colonne nominate e righe eterogenee, ispirata ai DataFrame di pandas.
Il tipo è immutabile — ogni operazione di trasformazione restituisce una nuova istanza.

Il caso d'uso principale sono script di elaborazione dati tabulari: parsing di CSV, filtraggio
per colonna, selezione di sottoinsiemi, aggregazioni semplici (somma, media, min, max per
colonna), e interoperabilità con il tipo `list` e i map di arksh.

**Interfaccia prevista**

```arksh
# Costruzione
let m = Matrix("name", "age", "score")    # matrice vuota con intestazioni
let m2 = m -> add_row("alice", 30, 95.5)
let m3 = m2 -> add_row("bob", 25, 87.0)

# Accesso strutturale
let r = m3 -> rows                         # numero di righe (Integer)
let c = m3 -> cols                         # numero di colonne (Integer)
let names = m3 -> col_names               # lista di stringhe ["name", "age", "score"]
let row0 = m3 -> row(0)                   # prima riga come mappa {name: "alice", age: 30, ...}
let ages = m3 -> col("age")               # colonna "age" come lista [30, 25]

# Selezione e filtro
let sub = m3 -> select("name", "score")   # proiezione su sottoinsieme di colonne
let flt = m3 -> where("age", ">", 26)     # righe filtrate (confronto stringa/numerico)

# Aggregazioni per colonna
let s = m3 -> col("score") -> sum()        # usa le aggregazioni esistenti sulla lista
let mean_age = m3 -> col("age") -> mean()  # mean() come nuovo stage lista

# Interoperabilità
let maps = m3 -> to_maps                   # lista di mappe (una per riga)
let m4 = Matrix("x", "y") -> from_maps(maps)  # costruisce da lista di mappe
let csv = m3 -> to_csv                     # stringa CSV con header
let m5 = Matrix() -> from_csv(csv)         # importa da CSV (prima riga = intestazioni)

# Rendering e tipo
m3 -> print()                              # tabella formattata con intestazioni
let t = m3 -> type                         # "matrix"
let j = m3 -> to_json                      # array JSON di oggetti
```

**Struttura interna**

Nuovo kind `ARKSH_VALUE_MATRIX` nell'enum `ArkshValueKind`. La struttura `ArkshMatrix`
contiene colonne nominate e dati per righe:

```c
#define ARKSH_MAX_MATRIX_COLS  32
#define ARKSH_MAX_MATRIX_ROWS  1024

typedef struct {
  char col_names[ARKSH_MAX_MATRIX_COLS][ARKSH_MAX_NAME];
  size_t col_count;
  ArkshValueItem data[ARKSH_MAX_MATRIX_ROWS][ARKSH_MAX_MATRIX_COLS];
  size_t row_count;
} ArkshMatrix;
```

`ArkshMatrix` è embedded direttamente in `ArkshValue` (non heap-allocated), coerentemente
con `ArkshValueList` e `ArkshValueMap`. Il limite 32×1024 occupa ~11 MB nel worst case
di item massimali; il 99% degli usi reali resterà ampiamente sotto.

**Task**

- `[ ]` `E6-S8-T1` **Struttura interna** — aggiungere `ARKSH_VALUE_MATRIX` a `ArkshValueKind` e `ArkshMatrix` a `ArkshValue` in `object.h`; aggiornare `arksh_value_init` (zero i campi), `arksh_value_free` (ricorsivo sugli item), `arksh_value_copy` (deep copy riga per riga), `arksh_value_render` (formato tabella con intestazioni), `value_is_truthy` (`row_count > 0`); aggiungere `arksh_value_kind_name` → `"matrix"`
- `[ ]` `E6-S8-T2` **Resolver `Matrix(col...)`** — registrare il resolver in `shell.c`; senza argomenti crea matrice 0×0; con argomenti stringa crea matrice con quelle intestazioni e zero righe; errore se un argomento non è stringa (`"Matrix() expects string column names"`)
- `[ ]` `E6-S8-T3` **Metodi di mutazione** — implementare su `ARKSH_VALUE_OBJECT` di tipo matrix o come property handler: `add_row(v1, v2, ...)` → nuova matrice con riga aggiunta in coda (errore se argc ≠ col_count); `drop_row(n)` → nuova matrice senza la riga n; `rename_col(old, new)` → nuova matrice con colonna rinominata; tutti i metodi restituiscono una nuova istanza, non mutano il receiver
- `[ ]` `E6-S8-T4` **Accesso e selezione** — proprietà `rows`, `cols`, `col_names`; metodi: `row(n)` → mappa chiave-valore della riga n; `col(name)` → `ArkshValue` lista degli item di quella colonna (errore se colonna inesistente); `select(c1, c2, ...)` → nuova matrice con solo le colonne indicate (errore se nome sconosciuto); `where(col, op, val)` → nuova matrice con le righe che soddisfano `item[col] op val` (operatori supportati: `==`, `!=`, `<`, `<=`, `>`, `>=`)
- `[ ]` `E6-S8-T5` **Interoperabilità e serializzazione** — `to_maps` → lista di mappe (usa `ArkshValueMap` esistente, una per riga); `from_maps(list)` → costruisce matrice da lista di mappe omogenee (le chiavi della prima mappa diventano intestazioni, errore se una riga ha chiavi diverse); `to_csv` → stringa CSV RFC 4180 con header; `from_csv(str)` → parsa CSV (prima riga = intestazioni), usa il parser interno; `to_json` → array JSON di oggetti; `type` → `"matrix"`
- `[ ]` `E6-S8-T6` **Stage pipeline** — aggiungere `|> transpose` (scambia righe e colonne, col_count e row_count si scambiano, i nomi diventano `"row_0"`, `"row_1"`, …); `|> fill_na(col, val)` sostituisce item vuoti/stringa-vuota in una colonna con `val`; entrambi gli stage operano su `ARKSH_VALUE_MATRIX` e restituiscono una nuova matrice
- `[ ]` `E6-S8-T7` **Test** — matrice 0×0: `Matrix() -> rows` → `0`; matrice 2×3 dopo due `add_row`: `-> cols` → `3`, `-> col("age")` → lista corretta; `-> select("name")` → matrice 2×1; `-> where("age", ">", 26)` → matrice 1×3; `-> to_maps` → lista di 2 mappe; round-trip `-> to_csv |> Matrix() -> from_csv`: uguale alla matrice originale; `-> to_json` → stringa JSON valida; errore su colonna inesistente; test golden con `source`

---

### E6-S9. Integrazione cestino di sistema

Stato story: `[ ]`

Aggiunge il supporto per il cestino del sistema operativo direttamente dall'object model di
arksh. Non viene implementato alcun cestino proprio: ogni operazione usa l'API nativa della
piattaforma corrente, garantendo piena integrazione con Finder (macOS), Nautilus/Thunar
(Linux via FreeDesktop) e Esplora risorse (Windows).

**Comportamento per piattaforma**

| Piattaforma | Meccanismo nativo |
|---|---|
| macOS | `objc_msgSend` → `NSFileManager trashItemAtURL:resultingItemURL:error:` |
| Linux | FreeDesktop XDG Trash spec (`.local/share/Trash/files/` + `.trashinfo`) oppure delegato a `gio trash` se disponibile |
| Windows | `SHFileOperationW` con `FO_DELETE` + `FOF_ALLOWUNDO + FOF_NOCONFIRMATION` |

**Interfaccia prevista**

```arksh
# Sposta nel cestino — restituisce il path di destinazione nel cestino
let dest = path("file.txt") -> trash()

# Uso in pipeline: cestina tutti i file .tmp nella directory corrente
path(".") -> children() |> where(name ends_with ".tmp") |> each_trash()

# Namespace cestino (supporto varia per piattaforma)
let t = trash()                   # namespace cestino di sistema
let n = t -> count                # numero di elementi nel cestino
let items = t -> items            # lista path degli oggetti nel cestino
t -> empty()                      # svuota il cestino (richiede conferma nel REPL)
t -> restore("file.txt")          # ripristina elemento per nome
```

**Vincoli di progetto**

- `-> trash()` è l'unica operazione garantita su tutte le piattaforme.
- `trash() -> items`, `-> restore()`, `-> empty()` sono implementati solo dove l'API
  nativa lo permette; sulle piattaforme non supportate restituiscono un errore descrittivo
  (`"trash inspection not supported on this platform"`).
- Non viene creata nessuna directory nascosta `.trash` né alcun file di metadati propri:
  tutta la persistenza è affidata al sistema operativo.
- Su macOS, `NSFileManager` viene richiamato tramite il meccanismo Objective-C runtime in C
  (`objc/runtime.h`, `objc/message.h`) senza dipendere da un framework Swift o Cocoa a
  link time.

**Task**

- `[ ]` `E6-S9-T1` **Layer platform** — aggiungere in `platform.h`/`platform.c` la funzione
  `arksh_platform_trash_item(const char *abs_path, char *out_trash_path, size_t out_size, char *error, size_t error_size)`:
  su macOS usa `objc_msgSend` con `NSFileManager defaultManager` e `trashItemAtURL:resultingItemURL:error:`;
  su Linux implementa la XDG Trash spec (crea `~/.local/share/Trash/files/<name>` e il file
  `.trashinfo` in `~/.local/share/Trash/info/<name>.trashinfo`), con fallback a
  `execvp("gio", {"gio","trash","--","path",NULL})` se `gio` è nel `PATH`;
  su Windows chiama `SHFileOperationW` con `FO_DELETE`, `FOF_ALLOWUNDO`, `FOF_NOCONFIRMATION`,
  `FOF_SILENT`; ritorna 0 in caso di successo, 1 altrimenti con messaggio d'errore.

- `[ ]` `E6-S9-T2` **Metodo `-> trash()` su oggetti** — aggiungere il metodo `trash` agli
  oggetti di tipo `file`, `directory` e `path` nell'extension registry; chiama
  `arksh_platform_trash_item` con `object.path`; in caso di successo restituisce un nuovo
  `ArkshObject` che punta al path nel cestino (se restituito dalla piattaforma) oppure un
  valore stringa con il percorso; in caso di errore propaga il messaggio della piattaforma.

- `[ ]` `E6-S9-T3` **Stage pipeline `each_trash`** — registrare lo stage `each_trash` per
  pipeline di liste di oggetti; per ogni item chiama `-> trash()` e raccoglie i risultati
  in una lista; gli errori su singoli item non interrompono lo stage ma vengono accumulati
  e riportati tutti alla fine con contatore `N items failed`.

- `[ ]` `E6-S9-T4` **Namespace `trash()`** — aggiungere il resolver `trash` che restituisce
  un typed-map `trash_namespace` con le proprietà e i metodi seguenti, delegati alle API
  native: `count` (numero di elementi), `items` (lista di path come stringhe), `empty()`
  (svuota il cestino chiamando `NSEmptyTrash`/`rm -rf ~/.local/share/Trash/{files,info}/*`/
  `SHEmptyRecycleBinW`), `restore(name)` (ripristina un elemento per nome con la semantica
  nativa); le operazioni non supportate sulla piattaforma corrente restituiscono un errore
  descrittivo senza crashare.

- `[ ]` `E6-S9-T5` **Test** — almeno un test compilabile su tutte e tre le piattaforme:
  crea un file temporaneo in `$TMPDIR`, lo cestina con `-> trash()`, verifica che il file
  non esista più nella posizione originale; verifica che l'errore su path inesistente sia
  descrittivo; test condizionale `SKIP_IF_MACOS`/`SKIP_IF_LINUX`/`SKIP_IF_WINDOWS` per le
  funzionalità specifiche di piattaforma (`trash() -> items`, `-> restore`, `-> empty`).

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

Stato epoca: `[x]`

### E8-S1. Test unitari mirati

Stato story: `[x]`

- `[x]` `E8-S1-T1` aggiungere test unitari per lexer
- `[x]` `E8-S1-T2` aggiungere test unitari per parser
- `[x]` `E8-S1-T3` aggiungere test unitari per executor — `tests/unit_executor.c`: shell heap-allocata, 24 test su `execute_line`/`evaluate_line_value`/`execute_block`, control flow, POSIX functions, case, variabili
- `[x]` `E8-S1-T4` aggiungere test unitari per object model — `tests/unit_object.c`: 30 test su init/setter/copy/list/map/render/JSON round-trip/object properties, senza dipendenza dalla shell

### E8-S2. Test golden e PTY

Stato story: `[x]`

- `[x]` `E8-S2-T1` creare golden test per script `.arksh` — 5 fixture in `tests/fixtures/golden/`: `pipeline-chain`, `reduce-and-block`, `string-transform`, `if-sequence`, `mixed-shell-and-objects`; registrate in CMakeLists con `PASS_REGULAR_EXPRESSION` su output caratteristico
- `[x]` `E8-S2-T2` aggiungere PTY test per REPL e line editor — `tests/pty_repl.c`: usa `forkpty()` (POSIX only, `if(NOT WIN32)`); 5 test: prompt presente, `echo` in sessione interattiva, continuation prompt su input incompleto, tab completion del prefisso `histor`, Ctrl-D per uscita pulita con exit code 0
- `[x]` `E8-S2-T3` aggiungere job control smoke test ripetibili — 4 ctest: `arksh_jobctrl_two_jobs` (2 job + `jobs`), `arksh_jobctrl_wait_all` (`wait` senza args), `arksh_jobctrl_done_then_cmd` (done message + echo dopo wait), `arksh_jobctrl_current_marker2` (marker `+`)

### E8-S3. Sanitizers e fuzzing

Stato story: `[x]`

- `[x]` `E8-S3-T1` aggiungere target ASan/UBSan
- `[x]` `E8-S3-T2` integrare ASan/UBSan in CI — `.github/workflows/ci.yml`: 3 job (Linux ASan+UBSan, macOS UBSan, Linux Release); trigger su push/PR su `main`
- `[x]` `E8-S3-T3` aggiungere fuzzing su lexer/parser/expander — `tests/fuzz_input.c`: libFuzzer entry point, esercita `arksh_lex_line`/`arksh_parse_line`/`arksh_parse_value_line`; build con `cmake -DARKSH_FUZZ=ON -DCMAKE_C_COMPILER=clang`

### E8-S4. CI multipiattaforma

Stato story: `[x]`

- `[x]` `E8-S4-T1` creare workflow CI Linux
- `[x]` `E8-S4-T2` creare workflow CI macOS
- `[x]` `E8-S4-T3` creare workflow CI Windows
- `[x]` `E8-S4-T4` pubblicare matrice minima di supporto

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

## E10. Plugin ufficiali HTTP

Integrazione HTTP/HTTPS come plugin opzionale (`ARKSH_HTTP=ON`), basato su libcurl.
Permette di costruire richieste tipizzate, eseguirle e collegare la risposta alla pipeline
(Dict se JSON, testo se testuale, file se salvato su disco).

### E10-S1. Oggetto HTTP e pipeline di rete

Stato story: `[ ]`

- `[ ]` `E10-S1-T1` **Scaffold plugin** — opzione CMake `ARKSH_HTTP=ON`, rilevamento libcurl
  con `find_package(CURL)`, file sorgente `src/plugins/http.c`, header pubblico
  `include/arksh/plugins/http.h`. Il plugin registra il resolver `http()` e i stage
  `body_as_json`, `body_as_text`, `save_to` nella tabella dei resolver al momento del
  `arksh_shell_init` (solo se compilato con `ARKSH_HTTP=ON`).

- `[ ]` `E10-S1-T2` **Resolver `http()` e request builder** — `http()` restituisce un
  typed-map `http_request` con campi: `method` (default `"GET"`), `url`, `headers`
  (Dict, opzionale), `body` (stringa, opzionale), `content_type` (stringa, opzionale),
  `timeout` (numero, default 30). Metodi di costruzione della richiesta accessibili via
  `->`: `get(url)`, `post(url, body)`, `put(url, body)`, `delete(url)`,
  `set_header(name, value)`, `set_timeout(seconds)`.

- `[ ]` `E10-S1-T3` **Metodo `send()` e typed-map risposta** — `-> send()` esegue la
  richiesta in modo bloccante tramite libcurl e restituisce un typed-map `http_response`
  con: `status_code` (numero intero), `ok` (bool, vero se 2xx), `body` (stringa raw),
  `content_type` (stringa, valore dell'header `Content-Type`),
  `headers` (Dict chiave→valore per tutti gli header di risposta).

- `[ ]` `E10-S1-T4` **Metodi sulla risposta** — sulla `http_response`: `-> body_as_json()`
  converte `body` in un valore `Dict` usando il parser JSON di arksh;
  `-> body_as_text()` restituisce `body` come stringa;
  `-> raise_on_error()` fallisce con errore descrittivo se `ok == false`.

- `[ ]` `E10-S1-T5` **Stage pipeline** — tre stage registrati nel pipeline resolver:
  `body_as_json` (equivale a `-> body_as_json()` su una `http_response` in pipeline),
  `body_as_text` (idem per testo),
  `save_to(path)` (scrive `body` raw su file e restituisce il percorso come stringa).
  Esempio d'uso: `http() -> get("https://api.example.com/data") -> send() | body_as_json | ...`

- `[ ]` `E10-S1-T6` **Gestione errori, timeout, redirect e proxy** — errori di rete
  (connessione rifiutata, timeout, TLS) vengono riportati come errore arksh con messaggio
  descrittivo; redirect seguiti automaticamente (max 10); variabili d'ambiente
  `HTTP_PROXY` / `HTTPS_PROXY` / `NO_PROXY` rispettate tramite opzione libcurl
  `CURLOPT_FOLLOWLOCATION` / `CURLOPT_PROXYTYPE`.

- `[ ]` `E10-S1-T7` **Test** — helper di test `tests/http_mock.h` che avvia un server
  HTTP minimale in-process (o usa `nc`/`socat` su localhost porta libera); test che
  coprono: GET con status 200, POST con body, status 4xx/5xx + `raise_on_error`, timeout,
  `body_as_json` su risposta JSON valida, `body_as_json` su risposta non-JSON (errore
  atteso), `save_to` scrive file; test condizionali su disponibilità rete marcati
  `SKIP_IF_OFFLINE`.

---

## Prossimi punti consigliati

**Epoche completate:** E1 `[x]`, E2 `[x]`, E3 `[x]`, E4 `[x]`, E5 `[x]`, E8 `[x]`
**In corso:** E6 (S1–S6 `[x]`, S7–S8 aperte)
**Aperte:** E6 (S7–S9), E7 (JSON), E9 (release), E10 (HTTP plugin)

---

### Percorso A — qualità e CI (E8)

~~Completato.~~ ~~`E8-S1`~~ `[x]`  ~~`E8-S2`~~ `[x]`  ~~`E8-S3`~~ `[x]`  ~~`E8-S4`~~ `[x]`

### Percorso B — compatibilità POSIX (E1-S6, E1-S7)

~~Completato.~~

### Percorso C — tab completion avanzata (E5-S6)

~~Completato.~~

### Percorso D — pipeline object più ricca (E6-S3)

~~Completato.~~

### Percorso E — tipi numerici espliciti (E6-S5)

~~Completato.~~ ~~`E6-S5-T1`~~ `[x]`  ~~`E6-S5-T2`~~ `[x]`  ~~`E6-S5-T3`~~ `[x]`  ~~`E6-S5-T4`~~ `[x]`  ~~`E6-S5-T5`~~ `[x]`

### Percorso F — tipo Dict (E6-S6)

~~Completato.~~ ~~`E6-S6-T1`~~ `[x]`  ~~`E6-S6-T2`~~ `[x]`  ~~`E6-S6-T3`~~ `[x]`  ~~`E6-S6-T4`~~ `[x]`  ~~`E6-S6-T5`~~ `[x]`  ~~`E6-S6-T6`~~ `[x]`

Abilita strutture dati chiave-valore native; prerequisito naturale per E7 (JSON avanzato).

1. `E6-S6-T1` (struttura interna `ARKSH_VALUE_DICT`)
2. `E6-S6-T2` (resolver `Dict()`)
3. `E6-S6-T3` (metodi di scrittura: `set`, `delete`)
4. `E6-S6-T4` (proprietà e metodi di lettura: `get`, `has`, `keys`, `values`, `count`)
5. `E6-S6-T5` (bridge JSON: `to_json` / `from_json`)
6. `E6-S6-T6` (test)

### Percorso G — JSON robusto (E7)

Prerequisito naturale per script di automazione e integrazione con API esterne.

1. `E7-S1-T1` (migliorare diagnostica parser con posizione/offset dell'errore)
2. `E7-S1-T2` (casi edge del parser — escape, unicode, numeri float)
3. `E7-S1-T3` (casi edge del serializer — pretty-print opzionale)
4. `E7-S2-T1` (strutture annidate oltre `ARKSH_MAX_COLLECTION_ITEMS`)
5. `E7-S3-T1` (stage `jq`-like o `select` per query su valori JSON)

### Percorso H — plugin HTTP (E10)

Aggiunge chiamate HTTP/HTTPS native ad arksh. Richiede libcurl; non blocca nessun'altra
epoca. Sblocca integrazioni con API esterne direttamente dagli script.

1. `E10-S1-T1` (scaffold plugin + CMake + rilevamento libcurl)
2. `E10-S1-T2` (resolver `http()` e request builder)
3. `E10-S1-T3` (metodo `send()` + typed-map risposta)
4. `E10-S1-T4` (metodi `body_as_json`, `body_as_text`, `raise_on_error`)
5. `E10-S1-T5` (stage pipeline: `body_as_json`, `body_as_text`, `save_to`)
6. `E10-S1-T6` (errori, timeout, redirect, proxy)
7. `E10-S1-T7` (test con mock server)

### Percorso I — stage di encoding (E6-S7)

Basso costo, nessuna dipendenza esterna, utile per automazione e integrazione API.

1. `E6-S7-T1` (stage `base64_encode`)
2. `E6-S7-T2` (stage `base64_decode`)
3. `E6-S7-T3` (test)

### Percorso L — tipo Matrix (E6-S8)

Struttura dati matriciale con colonne nominate ispirata ai DataFrame di pandas.
Prerequisito suggerito: E6-S6 (Dict) per interoperabilità `to_maps`/`from_maps`.

1. `E6-S8-T1` (struttura interna `ARKSH_VALUE_MATRIX` + `ArkshMatrix` + ciclo vita)
2. `E6-S8-T2` (resolver `Matrix(col...)` — costruzione con intestazioni)
3. `E6-S8-T3` (metodi di mutazione: `add_row`, `drop_row`, `rename_col`)
4. `E6-S8-T4` (accesso e selezione: `row(n)`, `col(name)`, `select(...)`, `where(col, op, val)`)
5. `E6-S8-T5` (interoperabilità: `to_maps`, `from_maps`, `to_csv`, `from_csv`, `to_json`)
6. `E6-S8-T6` (stage pipeline: `transpose`, `fill_na`)
7. `E6-S8-T7` (test golden + unit)

### Percorso M — cestino di sistema (E6-S9)

Integrazione con il cestino nativo del sistema operativo (Trash macOS, XDG Linux, Recycle Bin Windows).
Nessuna implementazione propria: ogni operazione delega all'API nativa della piattaforma corrente.

1. `E6-S9-T1` (layer platform: `arksh_platform_trash_item` — macOS via `NSFileManager`, Linux via XDG spec + fallback `gio`, Windows via `SHFileOperationW`)
2. `E6-S9-T2` (metodo `-> trash()` su oggetti `file`, `directory`, `path`)
3. `E6-S9-T3` (stage pipeline `each_trash` per cestinare una lista di oggetti)
4. `E6-S9-T4` (namespace `trash()` con `count`, `items`, `empty()`, `restore(name)`)
5. `E6-S9-T5` (test cross-platform su file temporaneo)

## Regola finale

Non iniziare task di epoche avanzate se esistono blocchi aperti nelle epoche precedenti che cambiano il contratto del parser, dell'expander o del runtime shell. In pratica:

- prima il core
- poi l'ergonomia
- poi il vantaggio distintivo
- infine packaging e release
