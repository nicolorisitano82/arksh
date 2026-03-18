# Parser Dispatch Rules

_oosh_ analizza ogni riga in input attraverso una sequenza ordinata di test.
Il primo ramo che riconosce la riga vince; se nessun ramo corrisponde la riga viene segnalata come errore di sintassi.

---

## 1. `oosh_parse_line` — albero di dispatch

```
riga input
│
├── commento (#...)          → OOSH_AST_NOP (ignorata)
│
├── istruzione composta      → OOSH_AST_COMPOUND_*
│   (while / for / if / until / case / select / function / class / {...} / (...))
│
├── lista di comandi         → OOSH_AST_COMMAND_LIST
│   (operatori && / || / ; che separano sotto-comandi semplici)
│
├── pipeline oggetto         → OOSH_AST_OBJECT_PIPELINE
│   (contiene |> — vedi §2)
│
├── espressione oggetto      → OOSH_AST_OBJECT_EXPRESSION
│   (contiene -> ma non |> — vedi §3)
│
├── espressione valore       → OOSH_AST_VALUE_EXPRESSION
│   (token singolo: numero / stringa quotata / booleano / chiamata resolver)
│
└── pipeline shell           → OOSH_AST_SHELL_PIPELINE / OOSH_AST_SIMPLE_COMMAND
    (comandi esterni e built-in shell, con | e reindirizzamenti)
```

---

## 2. Pipeline oggetto (`|>`)

Una riga che contiene `|>` viene spezzata in:

```
<sorgente>  |>  <stage1>  |>  <stage2>  ...
```

**Parsing della sorgente** — `parse_value_source_text_ex(allow_binding_ref=1)`:

| Priorità | Forma riconosciuta | Tipo AST |
|---|---|---|
| 1 | `[:x \| ...]` (blocco lambda) | `BLOCK_LITERAL` |
| 2 | `true` / `false` | `BOOLEAN_LITERAL` |
| 3 | identificatore semplice (`foo`) | `BINDING` (riferimento a variabile) |
| 4 | `cond ? a : b` | `TERNARY` |
| 5 | `a + b`, `a - b`, `a * b`, `a / b` | `BINARY_OP` |
| 6 | `expr -> field` (espressione oggetto) | `OBJECT_EXPRESSION` |
| 7 | `resolver(...)`, `capture(...)`, `capture_lines(...)` | `RESOLVER_CALL` / `CAPTURE_*` |
| 8 | token singolo non riconosciuto | → fallback E3-S3 |
| 9 | qualsiasi altra stringa non vuota | `CAPTURE_SHELL` (E3-S3 bridge) |

**Nota E3-S3**: se nessun parser riconosce la sorgente ma la stringa non è vuota, viene impostato `OOSH_VALUE_SOURCE_CAPTURE_SHELL`: il testo viene eseguito verbatim come riga shell e la stdout diventa un valore text nella pipeline.
Esempio: `ls examples/scripts |> lines |> count`.

---

## 3. Espressione oggetto (`->`)

Una riga che contiene `->` (ma non `|>`) viene interpretata come:

```
<sorgente>  ->  <campo>
```

La sorgente segue le stesse regole di §2 (stesso parser).
Il campo è un identificatore semplice o una chiamata `method(args)`.

**Booleani**: `true -> type` → `"bool"`, `true -> value` → `"true"`.
Prima della fix E3-S4, `true` veniva parsato come `BINDING` → fallback a path object (`type=path, exists=false`).

---

## 4. Parsing dei token valore — `parse_non_object_value_source_tokens`

Usato nei contesti dove `allow_binding_ref=0` (per es. argomenti di espressioni).

| Forma | Tipo AST |
|---|---|
| stringa quotata (`"..."` / `'...'`) | `STRING_LITERAL` |
| `true` / `false` | `BOOLEAN_LITERAL` |
| numero (`42`, `3.14`) | `NUMBER_LITERAL` |

---

## 5. Tipi valore sorgente — `OoshValueSourceKind`

| Costante | Descrizione |
|---|---|
| `STRING_LITERAL` | stringa quotata, testo estratto |
| `NUMBER_LITERAL` | numero intero o floating-point |
| `BOOLEAN_LITERAL` | `true` o `false` |
| `BINDING` | riferimento a variabile per nome |
| `RESOLVER_CALL` | chiamata a una funzione resolver registrata |
| `CAPTURE_TEXT` | `capture("cmd")` — esegue `cmd`, cattura stdout |
| `CAPTURE_LINES` | `capture_lines("cmd")` — come sopra, split per righe |
| `CAPTURE_SHELL` | sorgente multi-parola eseguita come riga shell (E3-S3) |
| `TERNARY` | `cond ? a : b` |
| `BINARY_OP` | operazione binaria aritmetica |
| `BLOCK_LITERAL` | blocco lambda `[:x \| ...]` |
| `OBJECT_EXPRESSION` | catena `src -> field -> ...` |

---

## 6. Ambiguità risolte

### `true` / `false`
Sono parole chiave booleane, non identificatori di variabile.
Riconosciute **prima** del check `BINDING` in `parse_value_source_text_ex` (riga ~1275 di `parser.c`) e in `parse_non_object_value_source_tokens`.
Questo garantisce che `while true`, `if false`, e `true -> type` si comportino correttamente senza dipendere dalla presenza di una variabile di ambiente `true`.

### Comandi shell single-word vs binding
Un token solitario come `pwd` in sorgente pipeline viene parsato come `BINDING` (referenza a variabile).
Per eseguirlo come comando shell usare `capture("pwd")`.
I comandi **multi-parola** (`ls -la`) non sono identificatori validi e finiscono nel fallback `CAPTURE_SHELL`.

### Built-in PURE vs MUTANT in pipeline shell
Un built-in al primo stage di una pipeline shell (`cmd1 | cmd2`) è permesso solo se è `OOSH_BUILTIN_PURE` (non modifica lo stato della shell).
Built-in `MUTANT` o `MIXED` in posizione di stage-0 producono un errore esplicito.
