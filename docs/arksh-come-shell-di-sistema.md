# arksh come shell di sistema: stato attuale e percorso

## 1. Cosa manca oggi

Una shell di sistema deve soddisfare requisiti molto precisi: deve poter sostituire `sh` o `bash` come shell di login, essere invocata dagli script di sistema, gestire segnali e terminale in modo robusto, e funzionare in ambienti ridotti (container, busybox, recovery). arksh oggi ha chiuso il core POSIX del backlog (`E11`), ma non soddisfa ancora tutti i requisiti da shell di sistema. Di seguito sono elencati i gap residui.

---

### 1.1 Conformità POSIX

| Requisito | Stato in arksh |
|-----------|----------------|
| Shebang `#!/usr/bin/env arksh` funzionante end-to-end | Parziale — esiste `-c`, ma il comportamento con shebang non è verificato su tutti i target |
| `set -e` (errexit) | Implementato |
| `set -u` (nounset) | Implementato |
| `set -o pipefail` | Implementato |
| `set -x` (xtrace) | Implementato |
| Aritmetica `$(( ))` | Implementata |
| Sostituzione di processo `<(cmd)` / `>(cmd)` | Implementata su POSIX |
| `getopts` | Implementato (flusso POSIX base, `OPTIND`/`OPTARG`, cluster opzioni) |
| `ulimit` | Implementato su POSIX, stub su Windows |
| `umask` | Implementato su POSIX, stub su Windows |
| `read` con `-r`, `-p`, timeout | Parziale |
| `printf` completo (tutti i formati POSIX) | Parziale |
| Here-string `<<<` | Implementata |
| Doppio bracket `[[ ]]` | Implementato |
| Test `-f`, `-d`, `-x`, `-z`, `-n`, confronti stringa/numerici, primari POSIX principali | Implementato |
| Funzioni con `local` scope | Implementato |
| Subshell esplicite `( cmd )` | Implementate |
| Gruppi di comandi `{ cmd; }` | Implementati |
| `exec` con redirection (`exec >file`, `exec <file`) | Implementato |
| `$LINENO`, `$FUNCNAME`, `$BASH_SOURCE` | Implementati |
| Array associativi POSIX-compatible | Implementati su `Dict` con `declare -A` / `typeset -A` |

---

### 1.2 Segnali e gestione TTY

| Requisito | Stato |
|-----------|-------|
| `trap` su tutti i segnali POSIX (SIGTERM, SIGHUP, SIGQUIT, SIGPIPE, …) | Implementato sui target POSIX supportati |
| Propagazione corretta dei segnali ai child process | Implementata sui path principali (`fork/exec`, pipeline, background job, subshell child) |
| Gestione SIGCHLD per job control robusto | Implementata nel runtime corrente |
| `SIGWINCH` e resize del terminale | Implementato sui target POSIX supportati |
| `setsid` / gestione corretta del process group come login shell | Implementato sui target POSIX supportati |
| Mode `--login` | Implementato |
| `stty` e raw mode ripristino affidabile al crash | Implementato sui target POSIX supportati |

---

### 1.3 Script di compatibilità

Quasi tutti gli script di sistema e gli strumenti (Docker entrypoint, systemd service, initrd, CI runners) assumono `sh` o `bash`. arksh oggi copre una parte molto più ampia della sintassi POSIX. Con il completamento di E14-S1 è ora disponibile una modalità `sh` esplicita:

- `arksh --sh` o l'invocazione tramite un symlink con nome `sh` attivano la modalità di compatibilità.
- La sintassi non-POSIX (`|>`, `->`, `let`, `extend`, `class`, `switch`, `[[ ]]`, `<<<`, `<(...)`, `>(...)`, block literal) viene rifiutata con un errore esplicito.
- Plugin autoload, config arksh-specifica e prompt avanzato vengono saltati; la variabile `ENV` viene letta come startup file (compatibilità POSIX sh).
- Il parsing di shebang multi-riga o di script complessi non è stato testato su corpora reali.
- Restano scoperte alcune aree tipiche da shell di sistema: array indicizzati,
`mapfile`/`readarray`, `coproc`, e la validazione su corpora reali di script di sistema.
Le variabili `$PPID`, `$BASHPID` e `nameref` (`declare/local -n`) sono state implementate in E15-S1.

---

### 1.4 Performance di avvio

Le shell di sistema vengono avviate migliaia di volte al giorno (ogni `$(cmd)`,
ogni pipeline, ogni script di build). Il tempo di startup deve essere nell'ordine
dei millisecondi.

**Stato E15-S2 (completata):** arksh ha un audit dedicato allo scenario di startup
non-interattivo. Baseline misurata: ~3–5 ms su macOS (target: < 10 ms su Linux moderno).
La history non viene piu caricata in modalita non-interattiva (`-c`, script, pipe).
Un test CTest (`arksh_perf_startup_wall_drop`) garantisce la regressione a 50 ms.
Dettagli in `docs/benchmarks-baseline.md` (sezione E15-S2).

---

### 1.5 Maturità e test di regressione

| Area | Stato |
|------|-------|
| Copertura edge case del lexer/parser | Test di golden file su scenari selezionati; mancano fuzzing coverage e corpus POSIX |
| Comportamento sotto memory pressure | Non testato sistematicamente |
| Comportamento in locale UTF-8, multi-byte, locale C | Non verificato |
| Stack overflow su script ricorsivi profondi | Non testato |
| Script di grandi dimensioni (>10K righe) | Non testato |
| Comportamento con file descriptor aperti al limite | Non testato |

---

### 1.6 Ecosistema

- Nessun package manager disponibile (brew, apt, yum, scoop non hanno un package `arksh`).
- Nessun framework di plugin equivalente a Oh My Zsh / Oh My Fish.
- Nessuna documentazione dell'integrazione con strumenti di terze parti (starship, direnv, fzf, zoxide, …).
- Nessuna shell integration per editor (VSCode, neovim, emacs).

---

## 2. Percorso per diventare adatto

Di seguito un percorso ordinato per colmare i gap. Le epoche sono ordinate per impatto/dipendenza.

---

### Fase A — Completamento POSIX core

**Stato:** completata nel backlog `E11`.

**Obiettivo raggiunto:** arksh può eseguire script POSIX di media complessità senza errori nelle aree coperte dal core.

1. Aritmetica `$(( espressione ))` — implementata.
2. `local` nelle funzioni — implementato con scope isolato e shadowing corretto.
3. Refinement finale su subshell, group command e test POSIX end-to-end — completato.

---

### Fase B — Segnali e TTY robusti

**Obiettivo:** arksh si comporta correttamente come shell di login e gestisce segnali in tutti i casi.

1. `trap` su tutti i segnali POSIX — completato; restano da rifinire i casi da login shell dentro la fase TTY/sessione.
2. Modalità `--login` — completata con profili arksh dedicati: `ARKSH_GLOBAL_PROFILE`, `${config_dir}/profile`, `~/.arksh_profile` e override `ARKSH_LOGIN_PROFILE`.
3. `setsid` e process group — completato sui target POSIX supportati: i login shell senza TTY fanno `setsid()` quando serve, le shell interattive si portano nel proprio process group e reclamano il controlling TTY con `tcsetpgrp()`.
4. `SIGWINCH` handler — completato: aggiorna dimensioni del terminale, notifica il line editor e ridisegna il buffer corrente senza perderlo.
5. Ripristino del terminale al crash — completato: il runtime interattivo centralizza lo snapshot TTY pre-raw e lo ripristina prima di terminare su segnali di uscita anomala supportati.
6. `stty` built-in o passthrough — completato come passthrough built-in ben definito sui target POSIX supportati.

---

### Fase C — Compatibilità script esistenti

**Obiettivo:** arksh può eseguire script bash di media complessità trovati in progetti reali.

1. Array indicizzati `a=(v1 v2)` e accesso `${a[0]}`, `${#a[@]}`, iterazione.
2. Array associativi `declare -A` — completati e mappati sui `Dict`.
3. Espansioni di parametro complete: `${var#pattern}`, `${var##pattern}`, `${var%pattern}`, `${var%%pattern}`, `${var/pat/rep}`, `${var//pat/rep}`, `${var^}`, `${var^^}`, `${var,}`, `${var,,}`, `${#var}`, `${var:offset:len}`.
4. `$LINENO`, `$FUNCNAME`, `$BASH_SOURCE`, `$PPID`, `$BASHPID` — tutti implementati (E15-S1).
5. `declare`, `typeset`, `readonly`, `nameref` — `declare/typeset -A` implementati; `declare/local -n` (nameref) implementato in E15-S1; resta la semantica bash completa per `declare -i`, `-x`, `-r` su array.
6. `printf` completo con tutti i formati POSIX e estensioni bash comuni.
7. `mapfile` / `readarray`.
8. Coroutine / coprocess `coproc`.
9. Modalità di compatibilità `sh` — completata: `--sh` e rilevamento automatico da `argv[0]`; disabilita sintassi non-POSIX (object pipeline, tipi, block literal, `let`, `extend`, `class`, `switch`, `[[ ]]`, `<<<`, `<(...)`, `>(...)`); salta config/plugin autoload arksh-specifica; carica `ENV` come startup file.

---

### Fase D — Performance e robustezza

**Obiettivo:** arksh supera i benchmark di startup e i test di stress di script reali.

1. Benchmark startup time — misurare e portare sotto 5ms a freddo su Linux.
2. Corpus POSIX — eseguire la test suite POSIX (pubs.opengroup.org) e correggere i fallimenti.
3. Corpus bash — eseguire script comuni di sistemi reali (Homebrew formulas, Alpine init scripts, Debian maintainer scripts) e misurare compatibilità.
4. Fuzzing — integrare AFL++ o libFuzzer sul lexer/parser per scoprire crash su input malformati.
5. Valgrind / AddressSanitizer full coverage — zero leak e zero UB sulla test suite completa.
6. Limite stack configurabile e protezione da ricorsione infinita.

---

### Fase E — Ecosistema e distribuzione

**Obiettivo:** arksh è installabile, documentato e integrabile.

1. Package per brew, apt, yum, scoop.
2. Shell integration per starship, direnv, fzf, zoxide — verificare compatibilità dei hook.
3. Integrazione VSCode come shell di terminale.
4. Man page `arksh(1)`.
5. Sito di documentazione online.
6. Roadmap pubblica e issue tracker.
7. Versione 1.0 stabile con changelog e politica di deprecazione dell'ABI.

---

## 3. Riepilogo per decisione

| Uso | Adatto oggi? | Note |
|-----|-------------|------|
| Shell interattiva personale | Si (con limitazioni) | Mancano alcune feature avanzate, ma l'uso quotidiano base funziona |
| Shell di sviluppo in progetti arksh | Si | E il caso d'uso primario del repository |
| Scripting su sistemi POSIX | Si, con limiti | Il core POSIX del progetto e chiuso; modalita `sh` implementata; `$PPID`, `$BASHPID`, nameref implementati; restano `mapfile`, `readarray`, `coproc` e alcune estensioni bash avanzate |
| Shell di sistema (`/bin/sh` replacement) | No | Modalità `sh` implementata; packaging disponibile (Homebrew, DEB/RPM, winget); manca validazione su corpora reali |
| Shell in container / initrd | No | Startup audit completato (E15-S2, ≤50ms); packaging disponibile; manca validazione su ambienti minimali |
| Default shell utente (`chsh`) | Parziale | Possibile su macOS/Linux per chi conosce le limitazioni; sconsigliato per uso generale |
