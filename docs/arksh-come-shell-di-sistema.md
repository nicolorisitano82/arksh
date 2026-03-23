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
| `SIGWINCH` e resize del terminale | Non gestito |
| `setsid` / gestione corretta del process group come login shell | Non implementato |
| Mode `--login` | Non implementato |
| `stty` e raw mode ripristino affidabile al crash | Non garantito |

---

### 1.3 Script di compatibilità

Quasi tutti gli script di sistema e gli strumenti (Docker entrypoint, systemd service, initrd, CI runners) assumono `sh` o `bash`. arksh oggi copre una parte molto più ampia della sintassi POSIX, ma non è ancora un sostituto drop-in per questi script perché:

- La sintassi object-pipeline (`|>`, `->`) non è POSIX e nessuno strumento la conosce.
- Manca ancora una modalità di compatibilità `sh` che disabiliti esplicitamente le estensioni non-POSIX.
- Il parsing di shebang multi-riga o di script complessi non è stato testato su corpora reali.
- Restano scoperte diverse aree tipiche da shell di sistema: login shell, segnali completi, mode `sh`, array indicizzati e alcune variabili/feature bash aggiuntive (`$PPID`, `$BASHPID`, `nameref`).

---

### 1.4 Performance di avvio

Le shell di sistema vengono avviate migliaia di volte al giorno (ogni `$(cmd)`, ogni pipeline, ogni script di build). Il tempo di startup deve essere nell'ordine dei millisecondi. arksh ora ha benchmark dedicati e un percorso esplicito di ottimizzazione (`docs/benchmarks-baseline.md` e `docs/studio-cpu-memoria.md`), ma non ha ancora un audit finale mirato allo scenario `/bin/sh` o login shell.

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
2. Modalità `--login` — comportamento identico a `bash --login`: legge `/etc/profile`, `~/.bash_profile` o equivalente arksh.
3. `setsid` e process group — la shell di login diventa session leader; i job in foreground ricevono il TTY.
4. `SIGWINCH` handler — aggiorna dimensioni del terminale, notifica il line editor.
5. Ripristino del terminale al crash — installare un handler di ultimo resort che chiama `tcsetattr` con i settings originali.
6. `stty` built-in o passthrough — necessario per script di configurazione terminale.

---

### Fase C — Compatibilità script esistenti

**Obiettivo:** arksh può eseguire script bash di media complessità trovati in progetti reali.

1. Array indicizzati `a=(v1 v2)` e accesso `${a[0]}`, `${#a[@]}`, iterazione.
2. Array associativi `declare -A` — completati e mappati sui `Dict`.
3. Espansioni di parametro complete: `${var#pattern}`, `${var##pattern}`, `${var%pattern}`, `${var%%pattern}`, `${var/pat/rep}`, `${var//pat/rep}`, `${var^}`, `${var^^}`, `${var,}`, `${var,,}`, `${#var}`, `${var:offset:len}`.
4. `$LINENO`, `$FUNCNAME`, `$BASH_SOURCE`, `$PPID`, `$BASHPID` — i primi tre sono implementati; restano `$PPID` e `$BASHPID`.
5. `declare`, `typeset`, `readonly`, `nameref` — `declare/typeset -A` sono implementati; restano `nameref` e semantica bash completa.
6. `printf` completo con tutti i formati POSIX e estensioni bash comuni.
7. `mapfile` / `readarray`.
8. Coroutine / coprocess `coproc`.
9. Modalità di compatibilità `sh` — flag `--sh` o shebang `#!/bin/sh` che disabilita sintassi non-POSIX (object pipeline, tipi, block literal).

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
| Scripting su sistemi POSIX | Si, con limiti | Il core POSIX del progetto e chiuso; restano fuori soprattutto modalita `sh`, segnali completi, `exec` con redirection e alcune variabili/feature stile bash |
| Shell di sistema (`/bin/sh` replacement) | No | Mancano conformità POSIX, signal handling completo, modalità login |
| Shell in container / initrd | No | Mancano robustezza, startup performance verificata e un audit finale POSIX/login |
| Default shell utente (`chsh`) | Parziale | Possibile su macOS/Linux per chi conosce le limitazioni; sconsigliato per uso generale |
