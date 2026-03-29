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
9. `E12` Prestazioni e footprint CPU/memoria
10. `E11` POSIX core per uso come shell di sistema
11. `E13` Segnali e gestione TTY come shell di sistema
12. `E14` Modalità compatibilità `sh`
13. `E9` Packaging, release e documentazione finale
14. `E15` Compatibilità bash avanzata e performance di sistema
15. `E10` Plugin HTTP ufficiale

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
- `[x]` `E1-S7-T3` `(posix-script)` `$((...))` — arithmetic expansion ora funziona anche come espansione di argomenti shell standard (`echo $((3+4))`), con nesting `$(( $((2+3)) * 4 ))` e integrazione nelle command line POSIX.
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

Stato epoca: `[x]`

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

- `[x]` `E6-S4-T1` esporre metadati su resolver, stage e tipi — `description` aggiunto a `ArkshValueResolverDef` e `ArkshPipelineStageDef`; tutti i resolver e stage built-in registrati con descrizione; `register_builtin_pipeline_stages()` registra i 22 stage built-in al boot; `ARKSH_MAX_PIPELINE_STAGE_HANDLERS` alzato a 64; base poi formalizzata nell'`ABI v5`
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

Stato story: `[x]`

Due stage simmetrici per codifica e decodifica Base64 RFC 4648, integrati nella pipeline
oggetti (`|>`). Implementazione in C puro in `executor.c` senza dipendenze esterne;
registrazione in `shell.c`.

- `[x]` `E6-S7-T1` **`base64_encode` stage** — `apply_base64_encode_stage` in `executor.c`; alfabeto RFC 4648 `A-Za-z0-9+/` con padding `=`; registrato con descrizione `"encode a string to Base64 (RFC 4648)"`
- `[x]` `E6-S7-T2` **`base64_decode` stage** — `apply_base64_decode_stage` con lookup table `signed char[256]`; gestisce padding `=`/`==`; errore `"base64_decode: invalid character at position N"` su carattere non valido; errore su lunghezza non multipla di 4
- `[x]` `E6-S7-T3` **Test** — golden test `tests/fixtures/golden/base64-stages.arksh` (5 casi: encode hello→`aGVsbG8=`, decode, round-trip arksh, empty encode, empty decode); 208/208 test passano

---

### E6-S8. Tipo Matrix — struttura dati matriciale ispirata ai DataFrame

Stato story: `[x]`

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

- `[x]` `E6-S8-T1` **Struttura interna** — `ARKSH_VALUE_MATRIX` + `ArkshMatrixCell` + `ArkshMatrix` heap-allocated (256 righe × 32 col) in `object.h`; `arksh_value_free`, `arksh_value_copy`, `arksh_value_render` (tabella ASCII), `value_is_truthy` (`row_count > 0`), `arksh_value_kind_name` → `"matrix"`, `arksh_value_set_matrix`
- `[x]` `E6-S8-T2` **Resolver `Matrix(col...)`** — registrato in `shell.c`; senza argomenti crea matrice 0×0; con argomenti stringa crea matrice con quelle intestazioni e zero righe; `parse_extension_target` aggiornato per `"matrix"` → `ARKSH_VALUE_MATRIX`
- `[x]` `E6-S8-T3` **Metodi di mutazione** — `add_row(v1, v2, ...)`, `drop_row(n)`, `rename_col(old, new)`; tutti immutabili (restituiscono nuova istanza)
- `[x]` `E6-S8-T4` **Accesso e selezione** — proprietà `rows`, `cols`, `col_names`, `type`; metodi `row(n)`, `col(name)`, `select(c1, c2, ...)`, `where(col, op, val)` (operatori: `==` `!=` `<` `<=` `>` `>=`)
- `[x]` `E6-S8-T5` **Interoperabilità e serializzazione** — `to_maps()`, `from_maps(list)`, `to_csv()`, `from_csv(str)` (CSV RFC 4180), `to_json()` (array JSON di oggetti)
- `[x]` `E6-S8-T6` **Stage pipeline** — `|> transpose` (nomi colonne → `"row_0"`, `"row_1"`, …); `|> fill_na(col, val)` sostituisce celle vuote in una colonna
- `[x]` `E6-S8-T7` **Test** — golden `tests/fixtures/golden/matrix-types.arksh`; test CTest `arksh_golden_matrix_types`; 212/212 passati

---

### E6-S9. Integrazione cestino di sistema

Stato story: `[x]`

Aggiunge il supporto per il cestino del sistema operativo direttamente dall'object model di
arksh. Implementato come **plugin autonomo** (`plugins/trash/trash_plugin.c`) anziché nel
core: non richiede modifiche a `platform.h`/`platform.c` e può essere caricato on-demand
con `plugin load arksh_trash_plugin`.

**Comportamento per piattaforma**

| Piattaforma | Meccanismo nativo |
|---|---|
| macOS | `objc_msgSend` → `NSFileManager trashItemAtURL:resultingItemURL:error:` (link: `-framework Foundation`) |
| Linux | `gio trash` (se disponibile nel PATH) oppure XDG Trash spec (`~/.local/share/Trash/`) |
| Windows | `SHFileOperationW` con `FO_DELETE + FOF_ALLOWUNDO + FOF_NOCONFIRMATION` (link: `Shell32`) |

**Interfaccia implementata**

```arksh
plugin load arksh_trash_plugin

# Sposta nel cestino — restituisce il path di destinazione nel cestino
let dest = path("file.txt") -> trash()

# Uso in pipeline: cestina tutti i file .tmp nella directory corrente
path(".") -> children() |> where(name ends_with ".tmp") |> each_trash()

# Namespace cestino (supporto varia per piattaforma)
let t = trash()                   # namespace cestino di sistema (typed-map trash_ns)
let n = t -> count                # numero di elementi nel cestino
let items = t -> items            # lista path degli oggetti nel cestino
let ok = t -> empty()             # svuota il cestino
let r = t -> restore("file.txt")  # ripristina elemento per nome (Linux; errore su altri)
```

**Vincoli di progetto**

- `-> trash()` è l'unica operazione garantita su tutte le piattaforme.
- `trash() -> restore()` è supportato solo su Linux (XDG `.trashinfo`); su macOS e Windows
  restituisce un errore descrittivo (`"restore(): not supported on this platform"`).
- `trash() -> items` è completo su macOS e Linux; su Windows restituisce solo il conteggio
  aggregato (per-item enumeration richiederebbe COM/IShellFolder).
- Nessun file di metadati propri: la persistenza è affidata al sistema operativo.
- Su macOS `NSFileManager` viene richiamato tramite ObjC runtime in C (`objc_msgSend`)
  con link a `-framework Foundation`.

**Task**

- `[x]` `E6-S9-T1` **Layer platform nel plugin** — `platform_trash_item()` con ifdefs per
  macOS (`objc_msgSend` + Foundation), Linux (fork/execvp `gio` + fallback XDG rename +
  `.trashinfo`), Windows (`SHFileOperationW`); `platform_trash_list()` e
  `platform_trash_empty()` per il namespace.

- `[x]` `E6-S9-T2` **Metodo `-> trash()` su oggetti** — `object_method_trash` registrato
  come method extension su target `"object"`; chiama `platform_trash_item` con
  `receiver->object.path`; restituisce stringa con il path nel cestino.

- `[x]` `E6-S9-T3` **Stage pipeline `each_trash`** — `stage_each_trash` registrato come
  pipeline stage; itera sulla lista, cestina ogni item (oggetto o stringa), accumula errori;
  sostituisce il valore pipeline con il conteggio degli item cestinati.

- `[x]` `E6-S9-T4` **Namespace `trash()`** — `resolver_trash` crea un typed-map `trash_ns`;
  proprietà `count` e `items` delegate a `platform_trash_list`; metodi `empty()` e
  `restore(name)` delegate a `platform_trash_empty`/`platform_trash_restore`.

- `[x]` `E6-S9-T5` **Test** — 3 test CMake: caricamento plugin + tipo `trash_ns`, lettura
  `-> count` (regex `[0-9]+`), presenza del metodo `trash` in `help methods object`; 208/208
  test passano.

### E6-S10. Plugin autoload — caricamento automatico all'avvio

Stato story: `[x]`

Obiettivo: permettere all'utente di configurare quali plugin vengono caricati automaticamente all'avvio di arksh, e di gestire questa lista con un sottocomando dedicato.

- `[x]` `E6-S10-T1` **File di configurazione** — `~/.arksh/plugins.conf`; formato: un percorso assoluto per riga, commenti `#`, righe vuote ignorate. La directory `~/.arksh/` viene creata automaticamente se assente.
- `[x]` `E6-S10-T2` **Caricamento all'avvio** — `try_load_plugin_autoload()` chiamata da `arksh_shell_init()` dopo `try_load_default_config()`; legge `~/.arksh/plugins.conf` e chiama `arksh_shell_load_plugin()` per ogni riga; errori di caricamento singoli vengono ignorati silenziosamente (plugin non disponibile non blocca il boot).
- `[x]` `E6-S10-T3` **`plugin autoload set <path>`** — risolve il percorso in assoluto, crea `~/.arksh/` se mancante, controlla duplicati, appende una riga al file; output: `plugin added to autoload: <path>` oppure `already configured for autoload: <path>`.
- `[x]` `E6-S10-T4` **`plugin autoload unset <path>`** — legge il file, filtra la riga corrispondente (per percorso letterale o risolto), riscrive il file tramite file temporaneo + `rename`; output: `plugin removed from autoload: <path>` oppure errore se non trovato.
- `[x]` `E6-S10-T5` **`plugin autoload list`** — stampa tutti i percorsi configurati nel file, uno per riga; se il file e assente o vuoto: `no plugins configured for autoload`.
- `[x]` `E6-S10-T6` **Test CMake** — 3 test: `arksh_plugin_autoload_set` (output `added to autoload`), `arksh_plugin_autoload_unset` (output `removed from autoload`), `arksh_plugin_autoload_duplicate` (output `already configured for autoload`).

---

## E7. JSON e dati strutturati a livello prodotto

Stato epoca: `[x]`

### E7-S1. Parser/serializer robusti

Stato story: `[x]`

- `[x]` `E7-S1-T1` **Diagnostica con offset** — `arksh_value_parse_json` calcola `offset = cursor - text` dopo un parse fallito e appende `(at offset N)` al messaggio di errore; stessa info per trailing content
- `[x]` `E7-S1-T2` **Casi edge del parser** — `\uXXXX` unicode escapes implementati con conversione UTF-8 completa e gestione surrogate pair; caratteri di controllo (< 0x20) non quotati rifiutati con errore; leading zeros nei numeri (`01`) rifiutati per conformità RFC 8259; limite di nesting 128 livelli (`JSON_MAX_DEPTH`) per prevenire stack overflow su JSON molto annidato
- `[x]` `E7-S1-T3` **Casi edge del serializer** — `ARKSH_VALUE_MATRIX` serializza come array JSON di oggetti (una entry per riga, col_names come chiavi); caratteri di controllo < 0x20 nelle stringhe serializzati come `\uXXXX` invece di errore; NaN e Infinity serializzati come `null` per conformità RFC 8259 (via `format_number_json`); 8 nuovi test unitari in `unit_object.c`; golden `json-edge-cases.arksh` + test CTest `arksh_golden_json_edge_cases`; 213/213 passati

### E7-S2. Dati grandi e annidati

Stato story: `[x]`

- `[x]` `E7-S2-T1` ridotti i limiti fissi piu stretti sui dati JSON — `ArkshValueList` e `ArkshValueMap` ora crescono dinamicamente con capacity raddoppiata invece di fermarsi a `ARKSH_MAX_COLLECTION_ITEMS`; il parser JSON puo quindi accettare array e oggetti ben oltre 128 elementi, mantenendo il depth limit di `E7-S1`
- `[x]` `E7-S2-T2` aggiunti test su payload grandi — nuovi test unitari in `tests/unit_object.c` coprono array JSON da 384 elementi, oggetti JSON da 320 entry, payload annidati con 192 oggetti e round-trip di liste da 300 elementi
- `[x]` `E7-S2-T3` verificata stabilita memoria e tempi — l’executor libera correttamente la coda scartata nelle stage che compattano liste (`where`, `grep`, `take`, `first`) e la suite completa passa in build pulita: `216/216` test verdi (`build-e7s2`)

### E7-S3. Query e trasformazioni

Stato story: `[x]`

- `[x]` `E7-S3-T1` aggiungere accesso piu comodo a mappe annidate — aggiunti `get_path(path)`, `has_path(path)` e `set_path(path, value)` per `map`/`dict`; supportano segmenti con `.` e indici `[n]` per strutture JSON annidate
- `[x]` `E7-S3-T2` aggiungere helper di trasformazione dati strutturati — aggiunti `pick(k1, ...)`, `merge(other)` e lo stage pipeline `pluck("path")` per proiettare campi annidati da liste di mappe/dict
- `[x]` `E7-S3-T3` testare round-trip file -> value -> transform -> file — golden `tests/fixtures/golden/json-query-transform.arksh`, test runtime `arksh_json_get_path` / `arksh_pipeline_pluck` e unit test object model su `get_path`, `set_path`, `pick`, `merge`

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

Stato epoca: `[~]`

### E9-S1. Installazione standard

Stato story: `[x]`

- `[x]` `E9-S1-T1` aggiungere target `install` CMake
- `[x]` `E9-S1-T2` definire directory standard per config, cache, history e plugin
- `[x]` `E9-S1-T3` allineare il runtime a queste directory

### E9-S2. Packaging per sistemi target

Stato story: `[~]`

- `[ ]` `E9-S2-T1` preparare formula Homebrew o equivalente
- `[x]` `E9-S2-T2` preparare pacchetto Linux iniziale — CPack DEB+RPM+TGZ in `CMakeLists.txt` (solo su Linux, non-Apple); `cpack -G DEB|RPM|TGZ` da `build/`
- `[ ]` `E9-S2-T3` preparare strategia Windows (`winget` o installer equivalente)

### E9-S3. ABI plugin e versioning

Stato story: `[x]`

- `[x]` `E9-S3-T1` versionare formalmente l'ABI plugin — introdotti `ARKSH_PLUGIN_ABI_MAJOR 5` e `ARKSH_PLUGIN_ABI_MINOR 0`; nuovo entry point `arksh_plugin_query(...)`; `plugin load` valida major/minor prima di chiamare `arksh_plugin_init(...)`
- `[x]` `E9-S3-T2` aggiungere capability flags — `ArkshPluginInfo` e `ArkshPluginHost` espongono capability host/plugin (`commands`, `properties`, `methods`, `resolvers`, `stages`, `types`); `plugin list` e `plugin info` mostrano ABI e capability
- `[x]` `E9-S3-T3` aggiungere test di regressione ABI — plugin negativi `bad-abi-plugin` e `need-cap-plugin`; CTest verifica rifiuto loader, `plugin info` e `plugin list`

### E9-S4. Documentazione finale e troubleshooting

Stato story: `[x]`

- `[x]` `E9-S4-T1` scrivere guida installazione → `docs/guide-installation.md`
- `[x]` `E9-S4-T2` scrivere guida scripting → `docs/guide-scripting.md`
- `[x]` `E9-S4-T3` scrivere guida plugin author → `docs/guide-plugin-author.md`
- `[x]` `E9-S4-T4` scrivere troubleshooting operativo → `docs/troubleshooting.md`
- `[x]` `E9-S4-T5` scrivere man page `arksh(1)` → `docs/arksh.1`; installata da CMake in `${CMAKE_INSTALL_MANDIR}/man1`

### E9-S5. Release process

Stato story: `[ ]`

- `[ ]` `E9-S5-T1` introdurre changelog
- `[ ]` `E9-S5-T2` definire checklist release
- `[ ]` `E9-S5-T3` preparare criteri per dichiarare la `1.0`

### E9-S6. Sito di documentazione online (GitHub Pages)

Stato story: `[ ]`

- `[ ]` `E9-S6-T1` creare branch o cartella `docs/` pronta per GitHub Pages (Jekyll o MkDocs static site)
- `[ ]` `E9-S6-T2` struttura minima: home con quick-start, reference sintassi, guida plugin, FAQ e link al changelog
- `[ ]` `E9-S6-T3` collegare CI (GitHub Actions) per rebuild automatico su push a `main`
- `[ ]` `E9-S6-T4` documentare la modalità `sh` con una pagina dedicata: differenze rispetto alla modalità arksh completa, esempi di script POSIX compatibili, variabile `ENV`

### E9-S7. Shell integration per editor

Stato story: `[ ]`

- `[ ]` `E9-S7-T1` verificare che arksh funzioni come shell di terminale in VSCode (`terminal.integrated.shell.*`) e documentare la configurazione
- `[ ]` `E9-S7-T2` verificare compatibilità con neovim `:terminal` e documentare eventuali workaround (es. TERM, COLORTERM)
- `[ ]` `E9-S7-T3` aggiungere snippet di configurazione per starship, direnv, fzf e zoxide nella guida utente
- `[ ]` `E9-S7-T4` aggiungere un test di smoke automatico che avvia arksh come shell non-interattiva in un ambiente senza TTY (CI-safe) e verifica exit 0 su uno script POSIX minimale

---

## E10. Plugin ufficiali HTTP

Stato epoca: `[ ]`

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

### E10-S2. Plugin `claude-nl` — linguaggio naturale via Claude API

Stato story: `[ ]`

Design di riferimento: `docs/plugin-claude-nl.md` (documento esplorativo).

Il plugin traduce comandi in linguaggio naturale in comandi arksh tramite la
Claude API. Scala sulla stessa ABI v5 usata dal plugin HTTP.

- `[ ]` `E10-S2-T1` dipendenza opzionale dall'API Claude: opzione CMake
  `ARKSH_CLAUDE_NL=ON`; richiede libcurl (condiviso con E10-S1) e una chiave
  `ANTHROPIC_API_KEY` nel runtime
- `[ ]` `E10-S2-T2` resolver `nl("linguaggio naturale")` — invia il testo
  al modello, riceve il comando arksh generato, lo esegue nel contesto corrente
- `[ ]` `E10-S2-T3` output trasparente: mostra il comando generato prima di
  eseguirlo (modalita `--dry-run`), permette all'utente di accettare/rifiutare
- `[ ]` `E10-S2-T4` test con mock dell'API (payload fisso) per CI offline

---

## E11. POSIX core — completamento per uso come shell di sistema

Stato epoca: `[x]`

Obiettivo: permettere ad arksh di eseguire script POSIX di media complessità senza errori, rimuovendo i blocchi principali che impediscono l'uso come shell di sistema. Vedi `docs/arksh-come-shell-di-sistema.md` §2 Fase A.

### E11-S1. Flag di modalità shell

Stato story: `[x]`

- `[x]` `E11-S1-T1` **Struttura interna** — flag booleani `errexit`, `nounset`, `pipefail`, `xtrace` presenti in `ArkshShell`; `command_set` riconosce `-e`, `-u`, `-o pipefail`, `-x` e le forme di disabilitazione `+e`, `+u`, `+x`, `+o pipefail`; aggiunta `$PS4` con default `"+ "` e uso come prefisso di `xtrace`.
- `[x]` `E11-S1-T2` **`set -e` (errexit)** — dopo ogni comando non-condizionale in `arksh_shell_execute_line` e nei loop di command list, se `errexit` è attivo e lo status è non-zero, uscire con quello status; esclusioni già coperte nella grammatica attuale: condizione di `if`/`while`/`until`, LHS di `&&`/`||`. Il caso `! cmd` verrà agganciato quando il parser introdurrà il bang command.
- `[x]` `E11-S1-T3` **`set -u` (nounset)** — in `expand.c` durante l'espansione di `$VAR` e `${VAR}`, se la variabile non è definita e `nounset` è attivo, viene restituito un errore descrittivo invece di stringa vuota; le forme con default come `${VAR:-default}` restano escluse.
- `[x]` `E11-S1-T4` **`set -o pipefail`** — in `platform.c` per pipeline shell multi-stage vengono conservati gli exit status dei segmenti; se `pipefail` è attivo, lo status dell'intera pipeline è il più alto status non-zero tra i segmenti (o zero se tutti sono zero).
- `[x]` `E11-S1-T5` **`set -x` (xtrace)** — prima di eseguire ogni simple command viene stampato su stderr `$PS4` seguìto dalla forma già espansa del comando; le expansion sono tracciate dopo la sostituzione, non prima.
- `[x]` `E11-S1-T6` **Test** — aggiunti test mirati per `set -e`, `set -u`, `set -x`, `set -o pipefail` e una fixture golden integrata della story.

### E11-S2. Aritmetica `$(( ))`

Stato story: `[x]`

- `[x]` `E11-S2-T1` **Lexer** — il lexer tratta `$((...))` come parte di una word shell unica anche in command position, con nesting corretto di `$(( ... ))` annidati.
- `[x]` `E11-S2-T2` **Parser** — il parser preserva l'espansione aritmetica come argomento raw/cooked valido nelle simple command POSIX, senza spezzarla in token `(` / `)`.
- `[x]` `E11-S2-T3` **Executor** — l'expander valuta l'espressione intera con precedenza standard, supporto a `**`, nested arithmetic expansion, divisione/modulo per zero come errore e variabili non definite che valgono `0` anche con `nounset`.
- `[x]` `E11-S2-T4` **Test** — aggiunti test shell standard (`echo $((...))`, nesting, `if [ $((n > 0)) -ne 0 ]`, `nounset`) e una fixture golden POSIX dedicata.

### E11-S3. Built-in `[ ]` completo

Stato story: `[x]`

- `[x]` `E11-S3-T1` **Primari file mancanti** — `command_test` ora copre anche `-b`, `-c`, `-g`, `-k`, `-p`, `-S`, `-u`, `-L`/`-h`, `-t fd`, `-O`, `-G`, `-N`, con fallback esplicito sui target non POSIX.
- `[x]` `E11-S3-T2` **Confronti stringa** — copertura completa di `=`, `!=`, `<`, `>` confermata e protetta da CTest dedicati.
- `[x]` `E11-S3-T3` **Confronti numerici** — `-eq`, `-ne`, `-lt`, `-le`, `-gt`, `-ge` validano davvero input interi e producono errore descrittivo su stringhe non numeriche.
- `[x]` `E11-S3-T4` **Operatori logici e raggruppamento** — `!`, `-a`, `-o`, `(`, `)` supportati nel builtin `[ ]` con precedenza `!` > `()` > `-a` > `-o`.
- `[x]` `E11-S3-T5` **Test** — aggiunti CTest mirati e una fixture golden POSIX su file inesistente, symlink, fifo, socket, `-t` e grouping.

### E11-S4. Doppio bracket `[[ ]]`

Stato story: `[x]`

- `[x]` `E11-S4-T1` **Lexer** — il lexer entra in modalita condizionale quando incontra `[[` come command word, mantiene `]]` come chiusura e non tokenizza `&&`, `||`, `!`, `(`, `)` come operatori di command list all'interno; in questo contesto non c'e word splitting ne globbing sugli argomenti.
- `[x]` `E11-S4-T2` **Parser** — `[[ ... ]]` viene preservato come simple command dedicato, cosi puo convivere correttamente con `&&`/`||` esterni nelle command list e con `if`/`while`/`until`.
- `[x]` `E11-S4-T3` **Executor** — valutazione estesa con primari ereditati da `[ ]`, `==`/`=` con pattern glob, `!=`, `=~` via regex POSIX ERE, cattura in `$BASH_REMATCH`, espansione senza word splitting/pathname expansion e operatori `&&` / `||` con cortocircuito.
- `[x]` `E11-S4-T4` **Test** — aggiunti test CTest per `[[ "$s" == *.txt ]]`, `[[ "$s" =~ ^[0-9]+$ ]]`, operatori logici e variabili non quotate; suite completa verde.

### E11-S5. Subshell `( )` e gruppi `{ }`

Stato story: `[x]`

- `[x]` `E11-S5-T1` **Lexer/parser subshell** — `( cmd_list )` è riconosciuto come nodo AST `SUBSHELL` e continua a restare distinto dalle invocation expression e dai resolver.
- `[x]` `E11-S5-T2` **Executor subshell** — su POSIX la subshell viene ora eseguita in un vero processo figlio (`fork`), con raccolta dell'output nel parent; modifiche a variabili, `cd`, alias e funzioni non trapelano al padre.
- `[x]` `E11-S5-T3` **Lexer/parser gruppo** — `{ cmd_list; }` resta un nodo AST `CMD_GROUP`, e il parser richiede esplicitamente `;` o newline prima di `}`.
- `[x]` `E11-S5-T4` **Executor gruppo** — il group command continua a eseguire `cmd_list` nello stesso processo e scope, utile per raggruppare redirection senza fork.
- `[x]` `E11-S5-T5` **Test** — aggiunti CTest e una fixture golden auto-verificante per pid isolato in subshell, `cwd` invariato nel parent, variabili non propagate e redirection dell'intero gruppo.

### E11-S6. `getopts`, `ulimit`, `umask`

Stato story: `[x]`

- `[x]` `E11-S6-T1` **`getopts optstring name [args]`** — implementato il flusso POSIX base con cluster di opzioni (`-abvalue`), `OPTIND`, `OPTARG`, reset con `OPTIND=1`, modalità silenziosa con optstring che inizia con `:` e stato `1` a fine opzioni.
- `[x]` `E11-S6-T2` **`ulimit`** — su POSIX usa `getrlimit`/`setrlimit`; supporta `-a`, `-c`, `-d`, `-f`, `-l`, `-m`, `-n`, `-s`, `-t`, `-u`, `-v` e `-S`/`-H`; su Windows resta stub con messaggio esplicito.
- `[x]` `E11-S6-T3` **`umask [mode]`** — implementato output ottale senza argomenti e set via modalità ottale o simbolica (`u=...`, `g=...`, `o=...`, con clausole separate da virgola); su Windows resta stub.
- `[x]` `E11-S6-T4` **Test** — aggiunti CTest per loop `getopts`, `umask` read/set ottale/simbolico e `ulimit -n`.

### E11-S7. `local` nelle funzioni

Stato story: `[x]`

- `[x]` `E11-S7-T1` **Parser / comando** — `local name[=value]` è accettato come comando valido nel corpo di una funzione shell; fuori funzione restituisce ora l'errore esplicito `local: not in a function`.
- `[x]` `E11-S7-T2` **Frame di scope** — `local` usa i frame di scope già introdotti in `ArkshShell`; all'ingresso nella funzione viene aperto un frame, la lookup scala correttamente verso l'esterno e il frame viene distrutto al ritorno.
- `[x]` `E11-S7-T3` **Shadowing** — una variabile `local` con lo stesso nome di una variabile esterna la oscura all'interno della funzione; al ritorno la variabile esterna riprende il valore originale.
- `[x]` `E11-S7-T4` **Test** — aggiunti test unitari per l'errore fuori funzione e una fixture golden per `local` con inizializzatore, shadowing e non-propagazione fuori dalla funzione.

### E11-S8. Here-string `<<<`

Stato story: `[x]`

- `[x]` `E11-S8-T1` **Lexer** — token `HERE_STRING` (`<<<`); la parola successiva è l'argomento (soggetto a espansione variabili e command substitution).
- `[x]` `E11-S8-T2` **Parser** — nodo di redirection `REDIRECT_HERESTRING` con l'espressione destra; può combinarsi con altri redirect sulla stessa riga.
- `[x]` `E11-S8-T3` **Executor** — creare una pipe anonima; scrivere la stringa espansa seguita da un newline nel write-end; passare il read-end come stdin al comando target; chiudere il write-end dopo la scrittura.
- `[x]` `E11-S8-T4` **Test** — golden script: `cat <<< "hello"` → `hello`; `read line <<< "word"` → `line=word`; espansione variabile `<<< "$HOME"`.

### E11-S9. Sostituzione di processo `<( )` e `>( )`

Stato story: `[x]`

- `[x]` `E11-S9-T1` **Lexer** — token `PROC_SUBST_IN` (`<(`) e `PROC_SUBST_OUT` (`>(`); il contenuto fino alla `)` bilanciata viene conservato come raw token.
- `[x]` `E11-S9-T2` **Parser** — processo di sostituzione accettato come token value-aware valido sia come argomento di comando sia come target di redirection; il bridge a runtime avviene in expansion, senza introdurre un nodo AST separato.
- `[x]` `E11-S9-T3` **Executor POSIX** — implementazione con named FIFO e worker child: `<(...)` espone un path leggibile, `>(...)` un path scrivibile; cleanup e wait eseguiti dal parent a fine comando.
- `[x]` `E11-S9-T4` **Test** — unit test lexer/parser e smoke test POSIX su `diff <(...)`, `wc -l <(...)`, `read ... < <(...)` e `printf > >(wc -c)`.

---

## E12. Prestazioni e footprint CPU/memoria

Stato epoca: `[x]`

Obiettivo: ridurre CPU time, numero di allocazioni, copie profonde e memoria residente del core di `arksh`, seguendo il piano descritto in [docs/studio-cpu-memoria.md](/Users/nicolo/Desktop/oosh/docs/studio-cpu-memoria.md).

### E12-S1. Baseline, benchmark e profiling

Stato story: `[x]`

- `[x]` `E12-S1-T1` creare benchmark ripetibili per startup, pipeline object-aware, JSON strutturato, function scope, subshell e command substitution
- `[x]` `E12-S1-T2` aggiungere contatori opzionali per allocazioni, copie `arksh_value_copy` e render `arksh_value_render`
- `[x]` `E12-S1-T3` documentare la baseline iniziale in un file dedicato con tempo, allocazioni e RSS per i casi principali
- `[x]` `E12-S1-T4` integrare almeno un target `perf` o script di benchmark eseguibile via CMake o shell

### E12-S2. Scratch arena per executor ed espansioni

Stato story: `[x]`

- `[x]` `E12-S2-T1` introdurre un modulo `arena` o `scratch allocator` riutilizzabile nel core
- `[x]` `E12-S2-T2` sostituire `allocate_temp_buffer()` nell'executor con allocazioni da arena per i buffer temporanei del path caldo
- `[x]` `E12-S2-T3` integrare la stessa arena in `expand.c` per command substitution, field splitting e buffer temporanei
- `[x]` `E12-S2-T4` garantire reset a fine comando senza leak e senza cambiare la semantica dei valori che devono sopravvivere oltre il frame
- `[x]` `E12-S2-T5` aggiungere benchmark e regressioni per verificare il calo di allocazioni rispetto alla baseline

### E12-S3. Layout piu leggero per `ArkshValue`

Stato story: `[x]`

- `[x]` `E12-S3-T1` ridisegnare `ArkshValue` per separare il payload per tipo e ridurre i buffer inline sempre residenti
- `[x]` `E12-S3-T2` spostare stringhe grandi e payload complessi su heap con ownership chiara e API di free/copy coerenti
- `[x]` `E12-S3-T3` alleggerire anche `ArkshValueItem` e i contenitori lista/mappa per ridurre il costo delle copie in pipeline
- `[x]` `E12-S3-T4` riallineare `arksh_value_copy`, `arksh_value_free`, `arksh_value_render` e il parser JSON al nuovo layout
- `[x]` `E12-S3-T5` aggiungere test e benchmark per confrontare footprint e costo di copia prima/dopo

### E12-S4. Layout piu leggero per `ArkshShell`

Stato story: `[x]`

- `[x]` `E12-S4-T1` spezzare `ArkshShell` in sottostrutture heap-owned per registry, history, job table e metadata caricati a runtime
- `[x]` `E12-S4-T2` rendere dinamici i contenitori shell ancora fixed-size quando hanno impatto diretto sul footprint base
- `[x]` `E12-S4-T3` aggiornare init/destroy, plugin loader e registrazione di comandi/stage/resolver alla nuova struttura
- `[x]` `E12-S4-T4` ridurre il costo base di una nuova shell e documentare la differenza di memoria residente rispetto alla baseline

### E12-S5. Scope frame locali per funzioni e block

Stato story: `[x]`

- `[x]` `E12-S5-T1` introdurre uno stack di frame locali per `vars`, `bindings` e parametri posizionali
- `[x]` `E12-S5-T2` far usare i frame alle funzioni shell, evitando snapshot profondi dell'intero scope quando non necessari
- `[x]` `E12-S5-T3` far usare i frame anche ai block e a `local`, sostituendo snapshot/restore puntuali dei binding
- `[x]` `E12-S5-T4` aggiungere test di regressione su shadowing, ritorno da funzione e isolamento dei block

### E12-S6. Subshell e command substitution meno costose

Stato story: `[x]`

- `[x]` `E12-S6-T1` sostituire gli snapshot profondi della subshell con un modello overlay o copy-on-write
- `[x]` `E12-S6-T2` alleggerire `clone_subshell()` in `expand.c` per `$(...)`, copiando solo lo stato strettamente necessario
- `[x]` `E12-S6-T3` conservare la semantica di isolamento su cwd, variabili, binding, classi, plugin e job side effect
- `[x]` `E12-S6-T4` aggiungere test e benchmark su `$(...)`, `( ... )` e script con stato shell ricco

### E12-S7. Riduzione di parse/render ricorsivi

Stato story: `[x]`

- `[x]` `E12-S7-T1` estendere l'AST per rappresentare meglio le chain `->` e i selector annidati senza ripassare da stringhe
- `[x]` `E12-S7-T2` far sì che stage e member call ricevano argomenti gia parse-ati quando possibile
- `[x]` `E12-S7-T3` ridurre i passaggi `value -> text -> parse -> value` nei path caldi dell'executor
- `[x]` `E12-S7-T4` aggiungere regressioni specifiche sulle forme annidate e benchmark sul calo di CPU

### E12-S8. Ottimizzazioni mirate e indicizzazione

Stato story: `[x]`

- `[x]` `E12-S8-T1` rendere dinamici i contenitori core ancora statici con impatto reale su memoria e scalabilità
- `[x]` `E12-S8-T2` introdurre lookup piu efficienti per registry consultati spesso: comandi, type resolver, pipeline stage, classi e istanze
- `[x]` `E12-S8-T3` valutare cache leggere per completion e prompt con invalidazione esplicita
- `[x]` `E12-S8-T4` documentare i guadagni finali e aggiornare `docs/studio-cpu-memoria.md` con lo stato di avanzamento reale

---

## Prossimi punti consigliati

**Epoche completate:** E1 `[x]`, E2 `[x]`, E3 `[x]`, E4 `[x]`, E5 `[x]`, E6 `[x]`, E7 `[x]`, E8 `[x]`, E11 `[x]`, E12 `[x]`, E13 `[x]`, E14 `[x]`
**In corso:** E9-S2 `[~]` (T2 fatto, T1 e T3 aperti)
**Aperte:** E9 (release), E10 (HTTP plugin), E15 (bash compat + startup)

**Story completate in E9:** E9-S1 `[x]`, E9-S3 `[x]`, E9-S4 `[x]`

### Priorità 1 — portare il progetto a livello distribuzione (E9)

1. `E9-S2` — completare packaging (`Homebrew` per macOS, strategia Windows); Linux già fatto con CPack
2. `E9-S5` — release process, changelog e criteri `1.0`
3. `E9-S6` — sito documentazione online (GitHub Pages)
4. `E9-S7` — shell integration per editor (VSCode, neovim, starship)

### Priorità 2 — compatibilità bash avanzata e startup (E15)

1. `E15-S1` — `$PPID`, `$BASHPID`, `nameref`
2. `E15-S2` — startup audit per scenario `/bin/sh`

### Priorità 3 — plugin HTTP ufficiale (E10)

`E10-S1` non blocca il core shell. Conviene affrontarla dopo aver completato E9.

### Ordine raccomandato dei prossimi sprint

1. `E9-S2` (T1 Homebrew + T3 Windows)
2. `E9-S5`
3. `E9-S6`
4. `E9-S7`
5. ~~`E15-S1`~~ `[x]` già completato
6. `E15-S2`
7. `E10-S1`
8. `E15-S3` — `sudo` come oggetto / blocchi privilegiati

---

## E13. Segnali e gestione TTY come shell di sistema

Stato epoca: `[x]`

Questa epoca traduce il blocco `1.2 Segnali e gestione TTY` del documento
`docs/arksh-come-shell-di-sistema.md` in story implementabili una alla volta.

### E13-S1. Trap e propagazione segnali POSIX

Stato story: `[x]`

- `[x]` `E13-S1-T1` completare `trap` per tutti i segnali POSIX supportati dal target e allineare `trap -p`, reset, ignore (`trap '' SIG`) e dispatch asincrono
- `[x]` `E13-S1-T2` garantire la propagazione corretta dei segnali ai child process e ai job foreground/background nei path principali (`fork/exec`, pipeline, background job, subshell child)
- `[x]` `E13-S1-T3` consolidare la gestione di `SIGCHLD` per job control robusto, con refresh dei job al boundary dei comandi e prima del prompt interattivo
- `[x]` `E13-S1-T4` aggiungere test PTY e smoke test dedicati per `INT`, `TERM`, `HUP`, `PIPE`, `QUIT`, `TSTP`, `CHLD`

### E13-S2. Login shell, sessione e process group

Stato story: `[x]`

- `[x]` `E13-S2-T1` introdurre `--login` con lettura dei file di startup coerente per shell di login
- `[x]` `E13-S2-T2` implementare `setsid` e il ruolo di session leader sui target POSIX supportati
- `[x]` `E13-S2-T3` rifinire handoff e restore del controlling TTY con `tcsetpgrp` per shell e job foreground
- `[x]` `E13-S2-T4` aggiungere test end-to-end su login mode, sessione e process group

### E13-S3. Resize terminale e line editor

Stato story: `[x]`

- `[x]` `E13-S3-T1` aggiungere handler `SIGWINCH` con refresh delle dimensioni del terminale
- `[x]` `E13-S3-T2` notificare line editor e prompt del resize con redraw coerente del buffer corrente
- `[x]` `E13-S3-T3` aggiungere smoke test PTY per resize e regressioni sul rendering interattivo

### E13-S4. Ripristino TTY e raw mode affidabili

Stato story: `[x]`

- `[x]` `E13-S4-T1` centralizzare snapshot e restore dello stato TTY nel runtime interattivo
- `[x]` `E13-S4-T2` aggiungere un percorso di ripristino affidabile su crash o uscita anomala della shell
- `[x]` `E13-S4-T3` introdurre `stty` built-in oppure passthrough ben definito e documentato
- `[x]` `E13-S4-T4` aggiungere test di regressione sul ripristino del terminale dopo errori, segnali e aborti del line editor

---

## E14. Modalità compatibilità `sh`

Stato epoca: `[x]`

Questa epoca traduce il gap "manca una modalità `sh`" in un blocco
implementabile senza confonderlo con il packaging o con la compatibilità POSIX
già raggiunta nel core.

### E14-S1. Runtime e parser in modalità `sh`

Stato story: `[x]`

- `[x]` `E14-S1-T1` introdurre un flag runtime `sh_mode`, attivabile con `--sh` e anche quando `argv[0]` è `sh`
- `[x]` `E14-S1-T2` far rifiutare al lexer/parser le sintassi non-POSIX in `sh_mode`, almeno: `->`, `|>`, block literal, `let`, `extend`, `class`, `[[ ]]`, `<<<`, `<(...)`, `>(...)`, `switch`
- `[x]` `E14-S1-T3` definire la policy di runtime in `sh_mode`: niente plugin/autoload/config arksh-specifica, prompt minimale e startup compatibile
- `[x]` `E14-S1-T4` supportare startup compatibile `sh` tramite `ENV` e documentare chiaramente le differenze rispetto alla modalità arksh completa
- `[x]` `E14-S1-T5` aggiungere test end-to-end su `arksh --sh`, su invocazione come `sh`, e su errori espliciti quando uno script usa estensioni non permesse

---

## E15. Compatibilità bash avanzata e performance di sistema

Stato epoca: `[~]`

Copre i gap residui emersi dall'analisi di `arksh-come-shell-di-sistema.md` che
non rientravano nelle epoche precedenti: variabili bash mancanti, `nameref`,
e l'audit di startup per lo scenario `/bin/sh`.

### E15-S1. Variabili speciali bash mancanti e `nameref`

Stato story: `[x]`

- `[x]` `E15-S1-T1` implementare `$PPID` — PID del processo padre, calcolato una volta al momento dell'init e esposto come variabile read-only di sola lettura
- `[x]` `E15-S1-T6` esporre `$PPID` nel modello ad oggetti — `proc($PPID)` restituisce un oggetto di tipo `process` con le stesse proprietà di `proc($$)` (pid, pgid, status, ecc.); utile per ispezionare il processo chiamante da script arksh-native
- `[x]` `E15-S1-T2` implementare `$BASHPID` — PID del processo corrente (uguale a `$PPID` nel processo padre, diverso nella subshell); necessario per script che usano `$$` nei contesti di subshell
- `[x]` `E15-S1-T7` esporre `$BASHPID` nel modello ad oggetti — `proc($BASHPID)` restituisce un oggetto `process` che riflette il PID effettivo del processo corrente anche dentro una subshell; si distingue da `proc($$)` (che rimane fisso al PID della shell principale) e permette di ispezionare il contesto di esecuzione corrente via member-access
- `[x]` `E15-S1-T3` implementare `nameref` (`declare -n` / `local -n`) — variabile che è un riferimento indiretto a un'altra; lettura e scrittura trasparenti; `unset -n` per rimuovere il riferimento senza toccare il target; errore su ciclo (nameref che punta a sé stesso)
- `[x]` `E15-S1-T4` aggiungere test di regressione su `$PPID`, `$BASHPID` in subshell e su `nameref` con lettura, scrittura, `unset`, ciclo e passaggio a funzione
- `[x]` `E15-S1-T5` estendere `nameref` al modello ad oggetti — una variabile `declare -n ref=obj` dove `obj` è un valore tipizzato (`ArkshValue`) deve permettere `$ref -> property`, `$ref -> method()` e `$ref |> stage()`; il deref avviene prima della member-access; `ref` rimane un riferimento al nome, non una copia del valore

### E15-S2. Startup audit per scenario `/bin/sh`

Stato story: `[x]`

- `[x]` `E15-S2-T1` misurare il tempo di startup a freddo con `hyperfine` o equivalente per i casi: `arksh -c true`, `arksh --sh -c true`, symlink `sh -c true`; stabilire una baseline e un target (< 10ms su Linux moderno)
- `[x]` `E15-S2-T2` profilare le fasi di init (`register_builtin_*`, `try_load_*`, `rebuild_all_lookup_indices`) per identificare i colli di bottiglia nel path di startup non-interattivo
- `[x]` `E15-S2-T3` ottimizzare il path non-interattivo: evitare allocazioni/registrazioni non necessarie quando non c'è TTY (es. history load, prompt config, completion generation)
- `[x]` `E15-S2-T4` aggiungere un test CTest di benchmark non-regressivo che fallisce se il tempo di startup supera la soglia stabilita in T1; documentare i risultati in `docs/benchmarks-baseline.md`

### E15-S3. `sudo` come oggetto e blocchi privilegiati

Stato story: `[ ]`

Obiettivo: permettere ad arksh di eseguire comandi e blocchi di codice con
privilegi elevati in modo idiomatico e type-safe, integrandosi con il modello
ad oggetti.

Opzioni di design (da scegliere prima dell'implementazione):

**Opzione A — `sudo` come resolver di oggetti**

```
proc(sudo(myapp)) -> kill(9)   # equivale a: sudo kill -9 <pid>
file(sudo("/etc/hosts")) -> write("127.0.0.1 host")
```

Il resolver `sudo()` è un wrapper che esegue il comando successivo (o la
catena member-access) con `sudo`; internamente genera un sottoprocesso
`sudo <cmd>`.

**Opzione B — blocco `with sudo do ... endwith`**

```
with sudo do
  file("/etc/hosts") -> append("127.0.0.1 host")
  proc(nginx) -> restart()
endwith
```

Tutti i comandi all'interno del blocco vengono eseguiti con privilegi
elevati. Il blocco è una costrutto sintattico che imposta un contesto
privilegiato; ogni comando nel blocco viene prefissato con `sudo`.

**Raccomandazione**: implementare entrambe, iniziando da A (resolver `sudo()`
come thin wrapper) poi B (blocco `with sudo do`).

- `[ ]` `E15-S3-T1` aggiungere resolver `sudo(cmd_args...)` — esegue il
  processo dato con `sudo`; restituisce un oggetto `process`; su Windows è
  un no-op con warning
- `[ ]` `E15-S3-T2` integrare `sudo()` nella catena member-access —
  `sudo(service) -> start()` deve tradursi in `sudo service start`; il
  resolver intercetta la chiamata e riscrive l'argv
- `[ ]` `E15-S3-T3` aggiungere sintassi `with sudo do ... endwith` al parser
  — il blocco imposta un flag `ctx_sudo` nel runtime; ogni `exec_command`
  controlla il flag e prepende `sudo` all'argv
- `[ ]` `E15-S3-T4` gestire correttamente TTY/pty per `sudo` interattivo —
  `sudo` richiede un TTY per il prompt password; usare la stessa infrastruttura
  PTY già presente (E13)
- `[ ]` `E15-S3-T5` test di regressione: `sudo echo ok` (mock sudo con script
  no-op), blocco `with sudo do`, catena `sudo(cmd) -> prop`

---

## Regola finale

Non iniziare task di epoche avanzate se esistono blocchi aperti nelle epoche precedenti che cambiano il contratto del parser, dell'expander o del runtime shell. In pratica:

- prima il core
- poi l'ergonomia
- poi il vantaggio distintivo
- infine packaging e release
