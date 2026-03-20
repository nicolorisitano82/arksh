# Confronto tra arksh e le principali shell Unix

Questo documento confronta `arksh` con le shell Unix più diffuse lungo le dimensioni che ne definiscono il posizionamento: modello dei dati, ergonomia interattiva, scripting e portabilità.

Le shell analizzate sono: **bash**, **zsh**, **fish**, **nushell**, **dash** e **arksh**.

---

## 1. Scheda di identità

| Caratteristica         | bash 5.x          | zsh 5.x            | fish 3.x           | nushell 0.9x       | dash 0.5.x         | arksh               |
|------------------------|-------------------|--------------------|--------------------|--------------------|--------------------|--------------------|
| Anno di prima release  | 1989              | 1990               | 2005               | 2019               | 1997               | 2026               |
| Linguaggio principale  | C                 | C                  | C++                | Rust               | C                  | C (C11)            |
| Standard di riferimento| POSIX + GNU ext.  | POSIX + Zsh ext.   | Proprio            | Proprio            | POSIX stretto      | Proprio            |
| Licenza                | GPL 3             | MIT-like (vari)    | GPL 2              | MIT                | BSD                | MIT                |
| Portabilità OS         | Linux, macOS, Win | Linux, macOS       | Linux, macOS, Win  | Linux, macOS, Win  | Linux, macOS, Unix | Linux, macOS, Win  |
| Shell di login default | Linux (comune)    | macOS (da 10.15)   | Opzionale          | Opzionale          | Debian `/bin/sh`   | No                 |

---

## 2. Modello dei dati

La dimensione più importante per capire il posizionamento di arksh.

| Caratteristica                    | bash   | zsh    | fish   | nushell | dash   | arksh   |
|-----------------------------------|--------|--------|--------|---------|--------|--------|
| Tipo di dato nativo               | Stringa| Stringa| Stringa| Strutturato | Stringa | Object-aware |
| Interi nativi                     | Si (aritmetica `$((...))`) | Si | No (solo stringhe) | Si | No | Parziale (`number`) |
| Liste native                      | Array indicizzati | Array + hash | Liste | Liste tipizzate | No | `list(...)` object-aware |
| Dizionari / mappe native          | Array associativi (bash 4+) | Hash | No | Record strutturati | No | `map(...)` typed-map (in corso: `Dict()`) |
| Booleani come tipo                | No (0/1 o stringhe) | No | No | Si | No | Si (`true`, `false`, `bool(...)`) |
| Oggetti filesystem come tipo      | No     | No     | No     | Si (LS restituisce tabella) | No | Si (file, directory, device, mount) |
| Namespace di sistema built-in     | No     | No     | No     | Parziale | No | Si (`fs()`, `user()`, `sys()`, `time()`) |
| Proprietà su oggetti (`-> size`)  | No     | No     | No     | Colonne nella tabella | No | Si, con sintassi `path -> property` |
| Metodi su oggetti (`-> children()`)| No    | No     | No     | Comandi | No | Si, con sintassi `path -> method()` |
| Block / closure come valori       | No     | No     | No     | Block (limitati) | No | Si, Smalltalk-style `[:x \| body]` |
| JSON nativo                       | No (jq esterno) | No | No | Si | No | Si (`to_json`, `from_json`, `read_json`, `write_json`) |
| Classi definibili dall'utente     | No     | No     | No     | No      | No     | Si (`class ... endclass`) |
| Ereditarietà                      | No     | No     | No     | No      | No     | Si, multipla a precedenza sinistra |
| Tipi custom da plugin             | No     | No     | No     | Parziale | No    | Si (typed-map con proprietà/metodi da plugin) |
| Estensioni di tipo runtime        | No     | No     | No     | No      | No     | Si (`extend target property/method`) |

---

## 3. Pipeline

| Caratteristica                         | bash   | zsh    | fish   | nushell | dash   | arksh   |
|----------------------------------------|--------|--------|--------|---------|--------|--------|
| Pipeline testuale (`\|`)               | Si     | Si     | Si     | Si      | Si     | Si     |
| Pipeline di oggetti strutturati        | No     | No     | No     | Si (tabelle) | No | Si (`\|>`) |
| Operatore pipeline oggetti             | —      | —      | —      | `\|`    | —      | `\|>`  |
| Filtraggio (`where`, `filter`)         | No (grep esterno) | No | No | Si (`where`) | No | Si (`where`/`filter` con prop o block) |
| Ordinamento (`sort`)                   | No (sort esterno) | No | No | Si (`sort-by`) | No | Si (`sort(size desc)`) |
| Proiezione (`each`, `map`, `flat_map`) | No     | No     | No     | Si      | No     | Si (`each(name)`, `map(block)`, `flat_map(block)`) |
| Raggruppamento (`group_by`)            | No     | No     | No     | Si (`group-by`) | No | Si (`group_by(property)` o block) |
| Slice (`take`, `first`)                | No (head) | No  | No     | Si (`first`, `take`) | No | Si |
| Aggregazione (`count`, `reduce`, `sum`, `min`, `max`) | No (wc) | No | No | Si | No | Si |
| Split/join testo                       | No (tr, cut) | No | No  | Si      | No     | Si (`split`, `join`, `trim`, `lines`) |
| Bridge output esterno → pipeline oggetti| No   | No     | No     | Parziale| No     | Si (`cmd \|> stage`) |
| Stage definibili da plugin             | No     | No     | No     | Si (custom commands) | No | Si |

---

## 4. Ergonomia interattiva

| Caratteristica                         | bash   | zsh    | fish   | nushell | dash   | arksh   |
|----------------------------------------|--------|--------|--------|---------|--------|--------|
| Syntax highlighting in REPL            | No (plugin esterno) | Parziale (zsh-syntax-highlighting) | Si (built-in) | Si | No | Si (built-in) |
| Autosuggestion da history              | No     | Parziale (zsh-autosuggestions) | Si (built-in) | Si | No | Si (built-in) |
| Tab completion contestuale             | Si     | Si (avanzata) | Si (avanzata) | Si | Minimale | Si (contestuale) |
| Completion per tipo (fn, alias, var, stage) | Parziale | Parziale | Parziale | Si | No | Si (con indicatori `(fn)`, `(@)`, `(let)`) |
| Completion dopo `->` (metodi oggetto)  | No     | No     | No     | No      | No     | Si |
| Navigazione history (frecce)           | Si     | Si     | Si     | Si      | Minimale | Si |
| Editing in-line (Ctrl-A, Ctrl-E)       | Si (readline) | Si | Si | Si | No | Si |
| Blocchi multilinea in REPL             | Si     | Si     | Si     | Si      | No     | Si |
| Indicatore visivo tipo variabile       | No     | No     | Parziale | Si    | No     | Si (indicatori completion) |
| Prompt configurabile                   | Si (PS1) | Si (prompt theme engine) | Si | Si | Minimale | Si (theme engine con segmenti) |

---

## 5. Scripting

| Caratteristica                         | bash   | zsh    | fish   | nushell | dash   | arksh   |
|----------------------------------------|--------|--------|--------|---------|--------|--------|
| Sintassi compatibile POSIX             | Largamente | Largamente | No  | No     | Si (stretto) | No |
| `if` / `while` / `for`                 | Si     | Si     | Si     | Si      | Si     | Si     |
| `case ... esac` (pattern matching)     | Si     | Si     | `switch` | `match` | Si | Si + `switch/case` proprio |
| Funzioni definibili                    | Si     | Si     | Si     | Si      | Si     | Si (con parametri named) |
| Scope locale per variabili             | Parziale (`local`) | Si (`local`) | Si | Si | Parziale | Si (per funzioni e block) |
| Funzioni come valori                   | No     | No     | No     | Si      | No     | Parziale (block sono valori) |
| Override di built-in con funzioni      | Si     | Si     | Si     | Si      | Si     | Si + `builtin` per escape |
| `trap` / segnali                       | Si (completo) | Si | Parziale | Parziale | Si | Parziale (solo `EXIT`) |
| `eval`                                 | Si     | Si     | Si (limitato) | Si | Si | Si |
| `exec`                                 | Si     | Si     | Si     | Parziale | Si | Si |
| Heredoc (`<<`, `<<-`)                  | Si     | Si     | Si     | Si      | Si     | Si |
| Operatore ternario                     | No     | No     | No     | `if` inline | No | Si (`? :`) |
| Operatori aritmetici in-language       | `$((...))` | `$((...))` | `math` | Nativi | `$((...))` | `+`, `-`, `*`, `/` in value expr |
| Classi / tipi custom                   | No     | No     | No     | No      | No     | Si |

---

## 6. Job control e processi

| Caratteristica                         | bash   | zsh    | fish   | nushell | dash   | arksh   |
|----------------------------------------|--------|--------|--------|---------|--------|--------|
| Background job (`&`)                   | Si     | Si     | Si     | Si      | Si     | Si     |
| `jobs`, `fg`, `bg`                     | Si     | Si     | Si     | Si      | Parziale | Si |
| Process group per pipeline foreground  | Si     | Si     | Si     | Si      | Si     | Si (E4-S1) |
| `Ctrl-Z` → stop job foreground         | Si     | Si     | Si     | Si      | Si     | Si (POSIX) |
| Aggiunta automatica del job stoppato   | Si     | Si     | Si     | Si      | Parziale | Si |
| `tcsetpgrp` / cessione terminale       | Si     | Si     | Si     | Si      | Parziale | Si |
| `wait`                                 | Si     | Si     | Si     | Si      | Si     | Si |
| Subshell `$(...)` / `(...)`            | Si     | Si     | Si     | Si      | Si     | Parziale (`$(...)`) |

---

## 7. Estensibilità e plugin

| Caratteristica                         | bash   | zsh    | fish   | nushell | dash   | arksh   |
|----------------------------------------|--------|--------|--------|---------|--------|--------|
| Plugin / moduli di terze parti         | No (solo script) | Si (zplug, zinit, ecc.) | Si (fisher, oh-my-fish) | Si (moduli) | No | Si (ABI C stabile) |
| Aggiungere comandi da plugin           | Script | Script | Script | Script/plugin | No | Si (libreria dinamica) |
| Aggiungere tipi / resolver da plugin   | No     | No     | No     | Parziale | No | Si (typed-map con `register_type_descriptor`, API v3) |
| Aggiungere stage pipeline da plugin    | No     | No     | No     | Parziale | No | Si |
| Aggiungere proprietà/metodi oggetto    | No     | No     | No     | No      | No     | Si (su tipi built-in e su tipi custom del plugin) |
| Framework di configurazione community  | Oh My Bash | Oh My Zsh, Prezto | Oh My Fish | Parziale | No | No (early stage) |
| Caricamento RC all'avvio               | `~/.bashrc` / `~/.bash_profile` | `~/.zshrc` | `~/.config/fish/config.fish` | `~/.config/nushell/config.nu` | `~/.profile` | `~/.arkshrc` / `ARKSH_RC` |

---

## 8. Portabilità e deployment

| Caratteristica                 | bash   | zsh    | fish   | nushell | dash   | arksh   |
|--------------------------------|--------|--------|--------|---------|--------|--------|
| Disponibile su Linux           | Si     | Si     | Si     | Si      | Si     | Si     |
| Disponibile su macOS           | Si     | Si (default da 10.15) | Si | Si | Si | Si |
| Disponibile su Windows         | WSL / Git Bash | WSL | Si (nativo) | Si (nativo) | WSL | Si (nativo) |
| Binario singolo                | Si     | Si     | No (richiede libs) | Si | Si | Si |
| Dipendenze a runtime           | libc, readline | libc, ncurses | vari | libc | libc | libc |
| Scripting cross-platform nativo| No (POSIX non su Windows) | No | Parziale | Si | No | Si (stesso codice) |
| Adatto come shell di sistema   | Si     | Si     | No     | No      | Si (minimalismo) | No |
| Adatto come shell interattiva  | Si     | Si (ottima) | Si (ottima) | Si (ottima) | No (minima) | Si (in sviluppo) |

---

## 9. Sintesi del posizionamento

```
Asse 1: POSIX / compatibilità classica    ←——————————————————→ Modello dati innovativo
         dash ——— bash ——— zsh        fish        nushell ——— arksh

Asse 2: Scripting puro                    ←——————————————————→ Ergonomia interattiva
         dash ——— bash        zsh ——— fish ——— nushell ——— arksh
```

| Shell   | Punto di forza principale                          | Limite principale rispetto a arksh              |
|---------|----------------------------------------------------|------------------------------------------------|
| bash    | Universale, POSIX, script ovunque                  | Nessun tipo nativo oltre la stringa            |
| zsh     | Ergonomia interattiva matura, POSIX compatibile    | Nessun object model, no JSON nativo            |
| fish    | UX out-of-the-box eccellente, highlighting nativo  | Rompe POSIX, nessun tipo strutturato           |
| nushell | Dati strutturati, pipeline tipizzate               | Rottura totale con shell classica, no classi   |
| dash    | Velocità, POSIX stretto, shell di sistema          | Nessuna funzione interattiva                   |
| arksh    | Object model + pipeline tipizzate + UX interattiva | Ancora in sviluppo, ecosistema piccolo         |

---

## 10. Confronto con nushell (il concorrente più simile)

Nushell è la shell che si avvicina di più al posizionamento di arksh. Le differenze principali:

| Dimensione                          | nushell                                  | arksh                                           |
|-------------------------------------|------------------------------------------|------------------------------------------------|
| Modello dati                        | Tabelle e record strutturati             | Oggetti filesystem + tipi scalari + classi     |
| Sintassi                            | Propria (rompe completamente con POSIX)  | Shell-compatibile su `;`, `&&`, `\|`, redirection |
| Compatibilità script POSIX          | Nessuna                                  | Alta (la parte shell è familiare)              |
| Classi definibili dall'utente       | No                                       | Si                                             |
| Estensioni di tipo runtime          | No                                       | Si (`extend`)                                  |
| Linguaggio implementazione          | Rust                                     | C11 (portabile, embeddable)                    |
| Plugin in linguaggio nativo         | Si (Rust o via protocollo)               | Si (C, ABI stabile)                            |
| Integrazione filesystem come oggetti| Parziale (LS come tabella)               | Completa (ogni path è un oggetto interrogabile)|
| JSON                                | Si (nativo)                              | Si (nativo)                                    |
| Ereditarietà / OOP                  | No                                       | Si (classi con ereditarietà multipla)          |
| `group_by`, `sum`, `min`, `max`     | Si (built-in)                            | Si (built-in, E6-S3)                           |
| Namespace di sistema                | Parziale (`$env`, `$nu`)                 | Si (`fs()`, `user()`, `sys()`, `time()`)       |
| Sintassi pipeline oggetti           | `\|` (stessa della shell)                | `\|>` (distinta dalla shell `\|`)              |
| Disponibilità su Windows            | Si (nativo)                              | Si (nativo, stesso codice C)                   |
