# ARKsh Scripting Guide

This guide covers writing scripts with arksh — both arksh-native scripts that
use the full object model, and POSIX-compatible scripts that run under `--sh`.

---

## Shebang Lines

### arksh-native script

```bash
#!/usr/bin/env arksh
```

### POSIX-compatible script (sh mode)

```bash
#!/usr/bin/env -S arksh --sh
# or, if arksh is symlinked as sh:
#!/bin/sh
```

---

## Running Scripts

```bash
arksh my_script.arksh           # run a script
arksh --sh my_script.sh         # run in POSIX sh-mode
arksh -c 'echo hello'           # inline command
source my_script.arksh          # execute in current shell context
```

Positional parameters are available inside sourced scripts as `$1`, `$2`, …, `$#`:

```bash
arksh my_script.arksh arg1 arg2
```

---

## Part 1 — POSIX Shell Scripting

When using `--sh` mode, arksh behaves as a POSIX sh-compatible shell.
All standard shell constructs are available; arksh extensions are disabled.

### Variables and assignment

```bash
name="world"
echo "hello $name"
readonly PI=3.14
unset name
```

### Positional parameters

```bash
echo "script: $0"
echo "first arg: $1"
echo "arg count: $#"
echo "all args: $@"
shift 2          # remove first two positional parameters
```

### Arithmetic expansion

```bash
x=$((2 + 3 * 4))
echo $x            # 14
echo $(( x % 3 ))  # 2
```

### String operations

```bash
path="/usr/local/bin/arksh"
echo "${path##*/}"        # arksh  (basename)
echo "${path%/*}"         # /usr/local/bin  (dirname)
echo "${path/local/opt}"  # /usr/opt/bin/arksh  (first substitution)
echo "${path//\//|}"      # |usr|local|bin|arksh  (all substitutions)
echo "${#path}"           # length
echo "${path:11:3}"       # bin  (substr offset:length)
```

### Command substitution

```bash
today=$(date +%Y-%m-%d)
files=$(ls -1 | wc -l)
content=$(< README.md)    # fast file read
```

### Control flow

```bash
# if / elif / else
if [ "$1" = "help" ]; then
  echo "usage: script <command>"
elif [ "$1" = "run" ]; then
  echo "running"
else
  echo "unknown command: $1"
fi

# while
count=0
while [ $count -lt 5 ]; do
  echo $count
  count=$((count + 1))
done

# until
until [ -f /tmp/done ]; do
  sleep 1
done

# for over list
for item in apple banana cherry; do
  echo "$item"
done

# for over glob
for f in *.txt; do
  echo "file: $f"
done

# case
case "$1" in
  start|run)  echo "starting" ;;
  stop)       echo "stopping" ;;
  *)          echo "unknown: $1"; exit 1 ;;
esac
```

### Functions

```bash
greet() {
  local name="$1"
  echo "hello, $name"
}

greet "world"
```

### Test operators

```bash
[ -f "$path" ]         # file exists and is regular
[ -d "$path" ]         # directory exists
[ -x "$path" ]         # file is executable
[ -z "$var" ]          # string is empty
[ -n "$var" ]          # string is non-empty
[ "$a" = "$b" ]        # string equal
[ "$a" != "$b" ]       # string not equal
[ "$n" -eq 42 ]        # integer equal
[ "$n" -lt 10 ]        # integer less than
```

### Redirection

```bash
cmd > output.txt        # stdout to file (overwrite)
cmd >> output.txt       # stdout to file (append)
cmd 2> errors.txt       # stderr to file
cmd 2>&1                # stderr to stdout
cmd < input.txt         # stdin from file
cmd1 | cmd2             # pipe stdout to stdin
```

### Heredoc

```bash
cat <<EOF
line one
line two with $var expansion
EOF

cat <<'EOF'
no $expansion here
EOF
```

### Error handling

```bash
set -e          # exit on error
set -u          # error on unset variable
set -o pipefail # propagate pipeline failure

trap 'echo "error at line $LINENO"' ERR
trap 'cleanup' EXIT

cleanup() {
  rm -f /tmp/my_tempfile
}
```

### getopts option parsing

```bash
while getopts "hvf:" opt; do
  case "$opt" in
    h) echo "usage"; exit 0 ;;
    v) verbose=1 ;;
    f) file="$OPTARG" ;;
    ?) exit 1 ;;
  esac
done
shift $((OPTIND - 1))
```

---

## Part 2 — ARKsh-Native Scripting

arksh-native scripts use the full typed object model on top of standard shell.

### Typed value bindings

```bash
let greeting = text("hello, world")
let count     = number(42)
let flag      = bool(true)
let items     = list("a", "b", "c")
```

### Member access with `->`

```bash
. -> type                          # "directory"
. -> name                          # current dir name
README.md -> size                  # file size in bytes
README.md -> read_text(64)         # first 64 chars
. -> parent() -> name              # parent directory name
. -> children() |> count()         # number of children
```

### Object pipelines with `|>`

```bash
. -> children() |> where(type == "file") |> sort(size desc)
. -> children() |> where(name ends_with ".md") |> pluck(name)
list(3,1,4,1,5) |> sort(value asc) |> take(3)
```

### Block literals

```bash
let double = [:n | n * 2]
list(1,2,3) |> map([:n | n * n])
list(1,2,3) |> reduce(number(0), [:acc :n | acc + n])
```

### Shell–object bridge

Capture shell command output into the pipeline:

```bash
capture("ls /usr/local/bin") |> lines() |> grep("ark") |> count()
capture_lines("ps aux") |> where(value contains "arksh")
```

### Dict type

```bash
let d = Dict()
let d = d -> set("key", "value")
let d = d -> set("n",   number(42))
echo $( d -> get("key") )
echo $( d -> has("missing") )     # false
let d = d -> delete("key")
echo $( d -> to_json() )
```

### Classes

```bash
class Point do
  property x = number(0)
  property y = number(0)
  method distance() do
    let dx = x * x
    let dy = y * y
    (dx + dy) -> sqrt()
  endmethod
endclass

let p = Point()
let p = p -> set("x", number(3))
let p = p -> set("y", number(4))
echo $( p -> distance() )    # 5
```

### `extend` — adding properties to existing objects

```bash
extend file property age_days = [:f |
  let now  = time() -> unix_epoch
  let then = f -> modified_at_unix
  (now - then) / 86400
]

. -> children() |> where(type == "file") |> sort(age_days desc) |> take(5)
```

### Control flow (arksh additions)

```bash
# switch / endswitch (not available in --sh mode)
switch $status
  case "ok"   ; then text("success") -> print()
  case "fail" ; then text("failure") -> print()
  default     ; then text("unknown") -> print()
endswitch

# ternary
let label = $code == 0 ? "pass" : "fail"
```

### Functions (arksh style)

```bash
function greet(name) do
  text("hello, %s") -> print(name)
endfunction

greet "world"
```

Arksh functions also accept typed arguments when called from value context.
POSIX-style `f() { }` functions are equally supported.

### let inside functions (local typed bindings)

```bash
function sum_list(items) do
  let total = items |> reduce(number(0), [:acc :n | acc + n])
  total -> value
endfunction
```

---

## Part 3 — Best Practices

### Choose the right mode

| Use case | Recommended style |
|----------|------------------|
| System administration script | `--sh` mode, POSIX only |
| Docker entrypoint | `--sh` mode |
| CI runner script | `--sh` mode |
| Personal automation | arksh native |
| Data query / transformation | arksh native with `|>` |
| Plugin script | arksh native |

### Portability checklist for `--sh` scripts

- Replace `->` member access with command substitution equivalents
- Replace `|>` pipelines with classic `|` pipelines
- Replace `let` with standard `VAR=value`
- Replace `[[ ]]` with `[ ]` or `case`
- Replace `<<<` with `echo "value" |`
- Avoid block literals, classes and switch

### Variable quoting

Always quote variables unless word-splitting is intentional:

```bash
# Good
echo "$name"
[ -f "$path" ]

# Dangerous (word-splits on spaces)
echo $name
[ -f $path ]
```

### Temporary files

```bash
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
echo "data" > "$tmp"
```

### Checking command availability

```bash
if ! command -v curl >/dev/null 2>&1; then
  echo "curl is required" >&2
  exit 1
fi
```

### Exit codes

```bash
main() {
  # ... do work ...
  return 0
}

main "$@"
exit $?
```
