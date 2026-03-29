# Security Backlog

Risultato della scansione di sicurezza manuale del codebase arksh eseguita il 2026-03-30.
Strumento: analisi statica manuale su tutti i file `src/` e `include/`.

---

## Legenda severità

| Livello | Significato |
|---------|-------------|
| **High** | Sfruttabile in scenari realistici; rischio di esecuzione arbitraria o escalation |
| **Medium** | Sfruttabile in condizioni specifiche (locale, race, input controllato) |
| **Low** | Rischio basso o by-design nelle shell; da documentare/mitigare comunque |

---

## ~~SEC-1~~ — Filename injection in `source_cmd` — **RISOLTO**

**Severità:** ~~High~~ → Fixed
**CWE:** CWE-78 (OS Command Injection)
**File:** `src/main.c`

### Problema

Il path del file script veniva interpolato in un comando shell con semplice double-quoting:

```c
snprintf(source_cmd, sizeof(source_cmd), "source \"%s\"", argv[arg_index]);
```

Un filename contenente `"` chiudeva le virgolette e permetteva injection.
Esempio: `arksh 'foo"$(rm -rf ~)"bar.arksh'` → esecuzione arbitraria.

### Fix applicato

Rimosso l'`snprintf` e la chiamata a `execute_line`. Ora si chiama direttamente:

```c
arksh_shell_source_file(shell, argv[arg_index],
    positional_count, argv + arg_index + 1,
    output, sizeof(output));
```

Il path arriva come dato grezzo senza mai essere interpolato in una stringa di comando.

---

## SEC-2 — Plugin `dlopen` senza autenticazione del .so

**Severità:** High
**CWE:** CWE-426 (Untrusted Search Path) / CWE-427 (Uncontrolled Search Path)
**File:** `src/plugin.c` ~riga 322, `src/platform.c` ~riga 2388

### Problema

I plugin vengono caricati via `dlopen(path, RTLD_NOW)` dopo una semplice risoluzione
del path. Non esiste alcuna verifica di integrità (hash, firma). Un file `.so` malevolo
nella plugin directory ottiene esecuzione di codice con i privilegi della shell.

La query ABI verifica solo `major/minor`, non autentica il plugin.

### Azione correttiva

1. Calcolare SHA-256 del `.so` prima del `dlopen` e verificarlo contro un manifest firmato (anche opzionale/opt-in).
2. Aggiungere flag `--trust-plugin <path>` o config `trusted_plugins = [...]`.
3. Emettere warning se la plugin directory è world-writable (`stat()` → `S_IWOTH`).
4. Documentare chiaramente che i plugin eseguono codice nativo non sandboxato.

---

## ~~SEC-3~~ — `with sudo do` non limita i comandi eseguibili — **RISOLTO (parziale)**

**Severità:** ~~High~~ → Mitigato
**CWE:** CWE-78 (OS Command Injection)
**File:** `src/executor.c`, `src/main.c`, `include/arksh/shell.h`

### Problema

Dentro un blocco `with sudo do ... endwith`, **qualsiasi** comando esterno veniva
automaticamente prefisso con `sudo` senza logging o conferma.

### Fix applicato

1. **Log su stderr** prima di ogni escalation (automatica e da member-access):
   ```
   [arksh:sudo] /usr/sbin/nginx -s reload
   ```
2. **Flag `--no-sudo-escalation`**: disabilita il prepend automatico nel blocco;
   il comando viene comunque eseguito ma senza `sudo`, con avviso su stderr:
   ```
   [arksh:sudo] escalation disabled (--no-sudo-escalation): running 'nginx' without sudo
   ```
3. **Help text** aggiornata con esempi sudo e nota sul flag.

### Aperto

- Allowlist configurabile dei comandi ammessi nel blocco (valutare per v1.0).

---

## SEC-4 — FIFO process substitution con path prevedibile

**Severità:** Medium
**CWE:** CWE-377 (Insecure Temporary File) / CWE-362 (Race Condition)
**File:** `src/shell.c` ~riga 607

### Problema

Il path del FIFO per process substitution è composto da PID + contatore sequenziale:

```c
snprintf(out_path, out_path_size, "%s/arksh-procsubst-%lld-%llu.fifo",
         root, shell->shell_pid, id);
```

Entrambi i componenti sono prevedibili. Un attaccante locale può:
- Pre-creare un symlink verso un file target per intercettare dati
- Eseguire un attacco TOCTOU tra `mkfifo` e la prima `open`

Inoltre `$TMPDIR` può essere sovrascritta da script arksh (riga ~576), permettendo
a codice untrusted di redirigere FIFO verso directory controllate dall'attaccante.

### Azione correttiva

1. Aggiungere componente random al nome del FIFO (es. `getrandom(8)` → hex string).
2. Leggere la directory temporanea dall'**ambiente del processo** (`getenv("TMPDIR")`),
   non dalla variabile di shell — prima dell'init della variabile shell.
3. Verificare che la tmp directory non sia world-writable (o usare `$XDG_RUNTIME_DIR`).

---

## SEC-5 — Integer overflow in `grow_heap_array` su piattaforme 32-bit

**Severità:** Medium
**CWE:** CWE-190 (Integer Overflow)
**File:** `src/executor.c` ~riga 101, `src/shell.c` ~riga 249, `src/plugin.c` ~riga 80

### Problema

La moltiplicazione per `realloc` non è protetta da overflow check:

```c
grown = realloc(*items, next_capacity * item_size);
```

Su piattaforme 32-bit con `item_size` grande, `next_capacity * item_size` può
wrappare a un valore piccolo → allocazione insufficiente → buffer overflow alla
successiva scrittura.

Il check su `doubled <= next_capacity` protegge solo il raddoppio della capacity,
non il prodotto con `item_size`.

### Azione correttiva

```c
/* Before the realloc call */
if (item_size > 0 && next_capacity > SIZE_MAX / item_size) {
    /* overflow: fallback or error */
    return 1;
}
grown = realloc(*items, next_capacity * item_size);
```

---

## SEC-6 — Config file TOCTOU con tmp path prevedibile

**Severità:** Medium
**CWE:** CWE-367 (TOCTOU Race Condition)
**File:** `src/shell.c` ~riga 9150

### Problema

La riscrittura del file di config del plugin autoload usa un file temporaneo
con nome prevedibile `conf_path + ".tmp"`:

```c
snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", conf_path);
tmp_fp = fopen(tmp_path, "w");
// ...
rename(tmp_path, conf_path);
```

Un attaccante locale può pre-creare un symlink `conf_path.tmp` → `/etc/crontab`
(o altro file sensibile) per causare sovrascrittura arbitraria.

### Azione correttiva

Usare `mkstemp` nella stessa directory del file target (per garantire atomicità del `rename`):

```c
snprintf(tmp_path, sizeof(tmp_path), "%s.XXXXXX", conf_path);
int fd = mkstemp(tmp_path);
if (fd < 0) { /* error */ }
tmp_fp = fdopen(fd, "w");
```

---

## SEC-7 — Shallow copy di puntatori nel clone del subshell

**Severità:** Medium
**CWE:** CWE-416 (Use After Free)
**File:** `src/shell.c` ~riga 3503

### Problema

La creazione di un subshell usa una copia struct diretta:

```c
*clone = *source;  /* shallow copy dell'intera ArkshShell */
```

Questo copia tutti i puntatori interni. C'è una finestra temporale in cui sia
`source` che `clone` puntano agli stessi dati dinamici. Se la distruzione del
subshell avviene prima che tutti i puntatori siano stati correttamente clonati
o azzerati, si può ottenere un double-free o use-after-free sui dati del parent.

### Azione correttiva

1. Aggiungere un test di regressione che verifica che dopo la creazione del subshell
   nessun puntatore interno sia condiviso tra parent e clone.
2. Sostituire la shallow copy con un'inizializzazione esplicita campo per campo,
   o azzerare immediatamente tutti i puntatori dopo `*clone = *source`.

---

## SEC-8 — History file senza file locking

**Severità:** Low
**CWE:** CWE-367 (TOCTOU)
**File:** `src/shell.c` ~riga 7751

### Problema

La lettura e scrittura della history usano `fopen("rb")` / `fopen("wb")`
senza `flock(LOCK_EX)`. Con due istanze arksh parallele, la history può
essere corrotta (una istanza tronca con `"wb"` mentre l'altra sta leggendo).

### Azione correttiva

Acquisire un advisory lock prima della lettura e rilasciarlo dopo la scrittura:

```c
int fd = fileno(fp);
flock(fd, LOCK_EX);
/* read/write */
flock(fd, LOCK_UN);
```

Su Windows usare `LockFileEx`.

---

## SEC-9 — Stato globale non thread-safe in platform.c

**Severità:** Low
**CWE:** CWE-362 (Race Condition)
**File:** `src/platform.c` ~riga 37

### Problema

```c
static char g_last_error[256];
```

Il buffer di errore globale è scritto senza mutex. Attualmente la shell è
single-threaded, ma se in futuro venisse usata una thread pool per job control
o async I/O, ci sarebbe data corruption.

### Azione correttiva

Rendere il buffer thread-local (`_Thread_local` in C11) o passarlo come parametro
a tutte le funzioni che lo scrivono.

---

## SEC-10 — Redirection senza `O_NOFOLLOW`

**Severità:** Low
**CWE:** CWE-61 (UNIX Symbolic Link Following)
**File:** `src/platform.c` ~riga 1850

### Problema

```c
opened_fd = open(redirect->path, flags, 0666);
```

Le redirection seguono symlink. In script che usano variabili per i path di
output, un symlink creato da codice concorrente può redirigere output verso
file sensibili.

### Azione correttiva

Valutare l'aggiunta di `O_NOFOLLOW` per le redirection in modalità scrittura
verso percorsi che l'utente non ha esplicitamente richiesto di seguire.
Aggiungere opzione `set -o noclobber` già presente o equivalente `--no-follow-redirects`.

---

## SEC-11 — Permessi file redirection: 0666 senza umask forzata

**Severità:** Low
**CWE:** CWE-732 (Incorrect Permission Assignment)
**File:** `src/platform.c` ~riga 1850

### Problema

```c
opened_fd = open(redirect->path, flags, 0666);
```

Se il processo gira con `umask 0000` (possibile in container/CI), i file
di redirection sono world-readable e world-writable.

### Azione correttiva

Usare `0600` come mode di default, o documentare che la responsabilità
del umask corretto è dell'utente/sysadmin.

---

## Riepilogo azioni

| ID | Severità | Area | Effort stimato |
|----|----------|------|----------------|
| ~~SEC-1~~ | ~~High~~ | ~~`src/main.c` — source injection~~ | ~~S~~ | **FIXED** |
| SEC-2 | High | `src/plugin.c` — plugin auth | L |
| ~~SEC-3~~ | ~~High~~ | ~~`src/executor.c` — sudo logging~~ | ~~M~~ | **FIXED** |
| SEC-4 | Medium | `src/shell.c` — FIFO path | S |
| SEC-5 | Medium | `src/executor.c` — overflow check | S |
| SEC-6 | Medium | `src/shell.c` — mkstemp config | S |
| SEC-7 | Medium | `src/shell.c` — subshell clone | M |
| SEC-8 | Low | `src/shell.c` — history lock | S |
| SEC-9 | Low | `src/platform.c` — thread-local error | S |
| SEC-10 | Low | `src/platform.c` — O_NOFOLLOW | S |
| SEC-11 | Low | `src/platform.c` — file mode 0600 | S |

**Effort:** S = < 1 ora, M = 2–4 ore, L = 1+ giorni

---

## Punti positivi rilevati

- Nessun uso di `gets()`, `sprintf()` non bounded, `strcpy()`, `tmpnam()`, `mktemp()`, `system()`, `popen()`
- Uso sistematico di `snprintf(buf, sizeof(buf), ...)` in tutto il codebase
- Controllo sistematico dei ritorni di `malloc`/`calloc`/`realloc`
- Validazione NULL in ingresso a quasi tutte le funzioni pubbliche
- Array fissi dimensionati con costanti simboliche e boundary checking
- ASan/UBSan abilitati nel CI — rilevano la maggior parte dei buffer overflow a runtime
