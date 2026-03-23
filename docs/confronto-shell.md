# Confronto tra arksh e le principali shell Unix

Questo documento confronta `arksh` con le shell Unix più diffuse lungo le dimensioni che ne definiscono il posizionamento: modello dei dati, ergonomia interattiva, scripting e portabilità.

Stato del confronto: repository aggiornato al `2026-03-23`.

Le shell analizzate sono: **bash**, **zsh**, **fish**, **nushell**, **dash** e **arksh**.

---

## 1. Scheda di identità

| Caratteristica         | bash 5.x          | zsh 5.x            | fish 3.x           | nushell 0.9x       | dash 0.5.x         | arksh               |
|------------------------|-------------------|--------------------|--------------------|--------------------|--------------------|--------------------|
| Anno di prima release  | 1989              | 1990               | 2005               | 2019               | 1997               | 2026               |
| Linguaggio principale  | C                 | C                  | C++                | Rust               | C                  | C (C11)            |
| Standard di riferimento| POSIX + GNU ext.  | POSIX + Zsh ext.   | Proprio            | Proprio            | POSIX stretto      | Proprio + core POSIX ampio |
| Licenza                | GPL 3             | MIT-like (vari)    | GPL 2              | MIT                | BSD                | MIT                |
| Portabilità OS         | Linux, macOS, Win | Linux, macOS       | Linux, macOS, Win  | Linux, macOS, Win  | Linux, macOS, Unix | Linux, macOS, Win  |
| Shell di login default | Linux (comune)    | macOS (da 10.15)   | Opzionale          | Opzionale          | Debian `/bin/sh`   | No                 |

---

## 2. Modello dei dati

La dimensione più importante per capire il posizionamento di arksh.

| Caratteristica                    | bash   | zsh    | fish   | nushell | dash   | arksh   |
|-----------------------------------|--------|--------|--------|---------|--------|--------|
| Tipo di dato nativo               | Stringa| Stringa| Stringa| Strutturato | Stringa | Object-aware |
| Interi nativi                     | Si (`$((...))`) | Si | No | Si | No | Si (`$((...))` + `Integer`, `Float`, `Double`, `Imaginary`; aritmetica type-aware con promozione) |
| Liste native                      | Array indicizzati | Array + hash | Liste | Liste tipizzate | No | `list(...)` object-aware |
| Dizionari / mappe native          | Array associativi (bash 4+) | Hash | No | Record strutturati | No | `map(...)` typed-map; `Dict()` immutabile con API chiara (`set`, `get`, `has`, `delete`, `keys`, `values`, bridge JSON); `Matrix(col...)` tabella bidimensionale con `add_row`, `select`, `where`, `to_csv`, `to_json` (E6-S8) |
| Booleani come tipo                | No (0/1 o stringhe) | No | No | Si | No | Si (`true`, `false`, `bool(...)`) |
| Oggetti filesystem come tipo      | No     | No     | No     | Si (LS restituisce tabella) | No | Si (file, directory, device, mount) |
| Namespace di sistema built-in     | No     | No     | No     | Parziale | No | Si (`fs()`, `user()`, `sys()`, `time()`, `env()`, `proc()`, `shell()`) |
| Proprietà su oggetti (`-> size`)  | No     | No     | No     | Colonne nella tabella | No | Si, con sintassi `path -> property` |
| Metodi su oggetti (`-> children()`)| No    | No     | No     | Comandi | No | Si, con sintassi `path -> method()` |
| Block / closure come valori       | No     | No     | No     | Block (limitati) | No | Si, Smalltalk-style `[:x \| body]` |
| JSON nativo                       | No (jq esterno) | No | No | Si | No | Si (`to_json`, `from_json`, `read_json`, `write_json`, query `get_path`/`set_path`/`pluck`) |
| Classi definibili dall'utente     | No     | No     | No     | No      | No     | Si (`class ... endclass`) |
| Ereditarietà                      | No     | No     | No     | No      | No     | Si, multipla a precedenza sinistra |
| Tipi custom da plugin             | No     | No     | No     | Parziale | No    | Si (typed-map con `register_type_descriptor`, ABI v4 usata nel core plugin) |
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
| Encoding (`base64_encode`, `base64_decode`) | No (openssl ext.) | No | No | No | No | Si (`base64_encode`, `base64_decode`; RFC 4648, C puro, nessuna dipendenza) |
| Bridge output esterno → pipeline oggetti| No   | No     | No     | Parziale| No     | Si (`cmd \|> stage`) |
| Stage definibili da plugin             | No     | No     | No     | Si (custom commands) | No | Si (con descrizione; visibili in `help stages`) |

---

## 4. Ergonomia interattiva

| Caratteristica                         | bash   | zsh    | fish   | nushell | dash   | arksh   |
|----------------------------------------|--------|--------|--------|---------|--------|--------|
| Syntax highlighting in REPL            | No (plugin esterno) | Parziale (zsh-syntax-highlighting) | Si (built-in) | Si | No | Si (built-in) |
| Autosuggestion da history              | No     | Parziale (zsh-autosuggestions) | Si (built-in) | Si | No | Si (built-in) |
| Tab completion contestuale             | Si     | Si (avanzata) | Si (avanzata) | Si | Minimale | Si (contestuale) |
| Completion per tipo (fn, alias, var, stage) | Parziale | Parziale | Parziale | Si | No | Si (indicatori `(fn)`, `(@)`, `(let)`; stage da metadati registrati) |
| Completion dopo `->` (metodi oggetto)  | No     | No     | No     | No      | No     | Si |
| Navigazione history (frecce)           | Si     | Si     | Si     | Si      | Minimale | Si |
| Editing in-line (Ctrl-A, Ctrl-E, Ctrl-K) | Si (readline) | Si | Si | Si | No | Si |
| Blocchi multilinea in REPL             | Si     | Si     | Si     | Si      | No     | Si |
| Indicatore visivo tipo variabile       | No     | No     | Parziale | Si    | No     | Si (indicatori completion) |
| Prompt configurabile                   | Si (PS1) | Si (prompt theme engine) | Si | Si | Minimale | Si (theme engine con segmenti) |

---

## 5. Scripting

| Caratteristica                         | bash   | zsh    | fish   | nushell | dash   | arksh   |
|----------------------------------------|--------|--------|--------|---------|--------|--------|
| Sintassi compatibile POSIX             | Largamente | Largamente | No  | No     | Si (stretto) | Ampia (core POSIX chiuso) |
| `VAR=value` standalone assignment      | Si     | Si     | Si     | Si      | Si     | Si     |
| `VAR=val cmd` env-prefix               | Si     | Si     | Si     | Si      | Si     | Si     |
| `if` / `while` / `for`                 | Si     | Si     | Si     | Si      | Si     | Si     |
| `case ... esac` (pattern matching)     | Si     | Si     | `switch` | `match` | Si   | Si + `switch/case` proprio |
| `case` pattern `word)` e `(word)`      | Si     | Si     | —      | —       | Si     | Si     |
| Funzioni con sintassi POSIX `f() {}`   | Si     | Si     | No     | No      | Si     | Si     |
| Funzioni con parametri nominali        | No     | No     | No     | Si      | No     | Si (`function f(a b) do ... endfunction`) |
| Scope locale per variabili (`local`)   | Parziale | Si   | Si     | Si      | Parziale | Si (funzioni shell + block typed) |
| `shift [n]` / `set -- args`           | Si     | Si     | No     | No      | Si     | Si     |
| Funzioni come valori                   | No     | No     | No     | Si      | No     | Parziale (block sono valori) |
| Override di built-in con funzioni      | Si     | Si     | Si     | Si      | Si     | Si + `builtin` per escape |
| `trap` / segnali                       | Si (completo) | Si | Parziale | Parziale | Si | Si (POSIX + `ERR`, `trap -p`) |
| `eval`                                 | Si     | Si     | Si (limitato) | Si | Si | Si |
| `exec`                                 | Si     | Si     | Si     | Parziale | Si | Si |
| Heredoc (`<<`, `<<-`)                  | Si     | Si     | Si     | Si      | Si     | Si |
| `set -e` / `-u` / `-x` / `-o`         | Si     | Si     | No     | No      | Si     | Si (`errexit`, `nounset`, `xtrace`, `pipefail`) |
| `read` built-in                        | Si     | Si     | Si     | Si      | Si     | Si (`-r -p -t -n`, IFS splitting) |
| `printf` built-in                      | Si     | Si     | Si     | Si      | Si     | Si (spec POSIX §2.2.3, padding, escape) |
| `echo -e` / `-n`                       | Si     | Si     | Si     | Si      | Parziale | Si |
| `readonly`                             | Si     | Si     | No     | Si      | Si     | Si |
| `getopts` built-in                     | Si     | Si     | No     | No      | Si     | Si (POSIX base, `OPTIND`/`OPTARG`) |
| `ulimit` / `umask`                     | Si     | Si     | No     | No      | Si     | Si (POSIX su Unix, stub espliciti su Windows) |
| `test` / `[` / `[[ ]]`                 | Si     | Si     | Si     | Si      | Si     | Si (`[ ]` con primari POSIX principali, `[[ ]]` con glob/regex) |
| `$((...))` arithmetic expansion        | Si     | Si     | `math` | Nativi  | Si     | Si |
| Operatore ternario                     | No     | No     | No     | `if` inline | No | Si (`? :`) |
| Classi / tipi custom                   | No     | No     | No     | No      | No     | Si |

---

## 6. Job control e processi

| Caratteristica                         | bash   | zsh    | fish   | nushell | dash   | arksh   |
|----------------------------------------|--------|--------|--------|---------|--------|--------|
| Background job (`&`)                   | Si     | Si     | Si     | Si      | Si     | Si     |
| `jobs`, `fg`, `bg`                     | Si     | Si     | Si     | Si      | Parziale | Si   |
| Process group per pipeline foreground  | Si     | Si     | Si     | Si      | Si     | Si     |
| `Ctrl-Z` → stop job foreground         | Si     | Si     | Si     | Si      | Si     | Si (POSIX) |
| Aggiunta automatica del job stoppato   | Si     | Si     | Si     | Si      | Parziale | Si  |
| `tcsetpgrp` / cessione terminale       | Si     | Si     | Si     | Si      | Parziale | Si |
| `wait`                                 | Si     | Si     | Si     | Si      | Si     | Si     |
| Subshell `$(...)` / `(...)`            | Si     | Si     | Si     | Si      | Si     | Si (`$(...)`, `$(< file)`) |

---

## 7. Estensibilità e plugin

| Caratteristica                         | bash   | zsh    | fish   | nushell | dash   | arksh   |
|----------------------------------------|--------|--------|--------|---------|--------|--------|
| Plugin / moduli di terze parti         | No (solo script) | Si (zplug, zinit, ecc.) | Si (fisher, oh-my-fish) | Si (moduli) | No | Si (ABI C già usata; versioning formale ancora in backlog) |
| Aggiungere comandi da plugin           | Script | Script | Script | Script/plugin | No | Si (libreria dinamica) |
| Aggiungere tipi / resolver da plugin   | No     | No     | No     | Parziale | No | Si (typed-map con `register_type_descriptor`, ABI v4) |
| Aggiungere stage pipeline da plugin    | No     | No     | No     | Parziale | No | Si (con descrizione; visibili in `help stages`) |
| Aggiungere proprietà/metodi oggetto    | No     | No     | No     | No      | No     | Si (su tipi built-in e su tipi custom del plugin) |
| Introspezione metadati a runtime       | No     | No     | No     | Parziale | No     | Si (`help commands\|resolvers\|stages\|types`, `help <name>`) |
| Framework di configurazione community  | Oh My Bash | Oh My Zsh, Prezto | Oh My Fish | Parziale | No | No (early stage) |
| Caricamento RC all'avvio               | `~/.bashrc` / `~/.bash_profile` | `~/.zshrc` | `~/.config/fish/config.fish` | `~/.config/nushell/config.nu` | `~/.profile` | `ARKSH_RC`, poi config dir standard, poi fallback `~/.arkshrc` |
| Autoload plugin all'avvio              | No (manuale in RC) | Si (via plugin manager) | Si (via fisher) | Si (moduli) | No | Si (config dir standard + `plugin autoload set/unset/list`) |

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
| Adatto come shell di sistema   | Si     | Si     | No     | No      | Si (minimalismo) | Non ancora (mancano login/signals/release) |
| Adatto come shell interattiva  | Si     | Si (ottima) | Si (ottima) | Si (ottima) | No (minima) | Si (gia usabile, ancora in evoluzione) |

---

## 9. Sintesi del posizionamento

```
Asse 1: POSIX / compatibilità classica    ←——————————————————→ Modello dati innovativo
         dash ——— bash ——— zsh        fish        nushell —— arksh

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
| arksh   | Object model + pipeline tipizzate + namespace di sistema + tipi numerici espliciti + JSON nativo + Matrix/Dict + plugin ABI in C + UX interattiva + core POSIX chiuso | Ecosistema piccolo, mancano ancora login shell robusta, segnali completi e packaging/release |

---

## 10. Confronto con nushell (il concorrente più simile)

Nushell è la shell che si avvicina di più al posizionamento di arksh. Le differenze principali:

| Dimensione                          | nushell                                  | arksh                                           |
|-------------------------------------|------------------------------------------|------------------------------------------------|
| Modello dati                        | Tabelle e record strutturati             | Oggetti filesystem + tipi scalari + classi     |
| Sintassi                            | Propria (rompe completamente con POSIX)  | Shell-compatibile su `;`, `&&`, `\|`, redirection |
| Compatibilità script POSIX          | Nessuna                                  | Ampia per script di media complessità (`VAR=val`, `f() {}`, `case`, `shift`, `local`, `trap`, `getopts`, `$((...))`, `[[ ]]`, here-string, process substitution, …) |
| Funzioni con parametri nominali     | Si                                       | Si (`function f(a b) do ... endfunction`)      |
| Funzioni POSIX `f() { ... }`        | No                                       | Si                                             |
| Classi definibili dall'utente       | No                                       | Si                                             |
| Estensioni di tipo runtime          | No                                       | Si (`extend`)                                  |
| Linguaggio implementazione          | Rust                                     | C11 (portabile, embeddable)                    |
| Plugin in linguaggio nativo         | Si (Rust o via protocollo)               | Si (C, ABI v4 già usata; formalizzazione release ancora aperta) |
| Integrazione filesystem come oggetti| Parziale (LS come tabella)               | Completa (ogni path è un oggetto interrogabile)|
| JSON                                | Si (nativo)                              | Si (nativo)                                    |
| Ereditarietà / OOP                  | No                                       | Si (classi con ereditarietà multipla)          |
| `group_by`, `sum`, `min`, `max`     | Si (built-in)                            | Si (built-in)                                  |
| Namespace di sistema                | Parziale (`$env`, `$nu`)                 | Si (`fs()`, `user()`, `sys()`, `time()`)       |
| Introspezione comandi / stage       | `man`, `--help` per comando              | Si (`help commands\|resolvers\|stages\|types`, `help <name>`) |
| Sintassi pipeline oggetti           | `\|` (stessa della shell)                | `\|>` (distinta dalla shell `\|`)              |
| Disponibilità su Windows            | Si (nativo)                              | Si (nativo, stesso codice C)                   |
