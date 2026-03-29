# arksh Language and API Reference

Version 0.9.0 — updated 2026-03-29

This is the authoritative quick reference for arksh: commands, value resolvers,
pipeline stages, syntax, special variables, and the plugin ABI. For conceptual
explanations and tutorials see the user manual (`docs/user-manual.md`) and the
scripting guide (`docs/guide-scripting.md`).

---

## Table of contents

1. [Command-line flags](#command-line-flags)
2. [Builtin commands](#builtin-commands)
3. [Value resolvers](#value-resolvers)
4. [Pipeline stages](#pipeline-stages)
5. [Object extensions](#object-extensions)
6. [Syntax cheatsheet](#syntax-cheatsheet)
7. [Special variables](#special-variables)
8. [Parameter expansion](#parameter-expansion)
9. [Startup files](#startup-files)
10. [Plugin ABI](#plugin-abi)

---

## Command-line flags

```
arksh [flags] [script [args...]]
arksh [flags] -c 'command'
```

| Flag | Description |
|------|-------------|
| `-c 'cmd'` | Execute a single command string and exit |
| `--sh` | Enable POSIX sh-compatibility mode; disables arksh-specific syntax |
| `--login` | Behave as a login shell (load login profiles) |
| `--help` | Print a brief help message and exit |

When invoked as `sh` (via symlink), `--sh` mode is activated automatically.

---

## Builtin commands

Commands are listed alphabetically. `[PURE]` = never modifies shell state.
`[MUTANT]` = must run in the current shell process.
`[MIXED]` = read-only when listing, mutating when writing.

### alias `[MIXED]`

```
alias [name[=value] ...]
```

With no arguments lists all defined aliases. `alias name=value` defines a new
alias. Aliases are expanded before parsing.

### bg `[MUTANT]`

```
bg [%jobspec]
```

Resume a stopped background job. If no jobspec, resumes the current job.

### break `[MUTANT]`

```
break [n]
```

Exit the current loop. If `n` is given, exit the nth enclosing loop.

### builtin `[MUTANT]`

```
builtin name [args...]
```

Invoke a builtin directly, bypassing any function override with the same name.

### call `[PURE]`

```
call path method [args...]
```

Invoke object method `method` on the object at `path`. Equivalent to
`path -> method(args...)` but in a command form. Useful in scripted contexts.

### cd `[MUTANT]`

```
cd [dir]
```

Change current directory to `dir`. With no argument, change to `$HOME`.
Supports `~` expansion and `cd -` to return to the previous directory.

### class `[PURE]`

```
class [name]
```

With no arguments lists all defined classes. With a name, prints the
definition of that class (properties, methods, base classes).

### classes `[PURE]`

```
classes [name]
```

Alias for `class`. Lists or inspects defined classes.

### continue `[MUTANT]`

```
continue [n]
```

Skip to the next iteration of the current loop. If `n` is given, continue
the nth enclosing loop.

### declare `[MUTANT]`

```
declare -A name          # associative array backed by Dict
declare -n name=target   # nameref (reference to another variable)
```

Declare shell metadata. `-A` creates an associative array. `-n` creates a
nameref: reads and writes to `name` transparently access `target`. Use
`unset -n name` to remove the nameref itself without touching its target.

### echo `[MUTANT]`

```
echo [-n] [-e] [args...]
```

Print arguments to standard output. `-n` suppresses the trailing newline.
`-e` interprets escape sequences (`\n`, `\t`, `\033`, etc.).

### eval `[MUTANT]`

```
eval 'command...'
```

Evaluate the given string in the current shell context. Single-quoted strings
are the most reliable form.

### exec `[MUTANT]`

```
exec [cmd [args...]]
exec >file
exec <file
```

Without a command, applies redirections persistently to the current shell.
With a command, replaces the current shell process with `cmd`.

### exit / quit `[MUTANT]`

```
exit [n]
quit [n]
```

Terminate the shell with exit code `n` (default: last command status).
Runs EXIT trap before exiting.

### export `[MUTANT]`

```
export [name[=value] ...]
```

Mark variables for inheritance by child processes. `export NAME=value` is
equivalent to `set NAME value ; export NAME`.

### extend `[MIXED]`

```
extend                                # list all extensions
extend type property name = block    # define a property extension
extend type method   name = block    # define a method extension
```

Define or list object extensions. `type` can be `any`, a value kind
(`string`, `number`, `bool`, `object`, `block`, `list`), or an object kind
(`path`, `file`, `directory`, `device`, `mount`).

Property block signature: `[:receiver | expression]`
Method block signature: `[:receiver :arg1 :arg2 | expression]`

### false `[PURE]`

```
false
```

Return failure (exit code 1). Always fails.

### fg `[MUTANT]`

```
fg [%jobspec]
```

Bring a background job to the foreground and wait for it. If no jobspec,
uses the current job.

### function / functions `[PURE]`

```
function [name]
functions [name]
```

With no arguments lists all defined shell functions. With a name, prints the
source of that function.

### getopts `[MUTANT]`

```
getopts optstring varname [args...]
```

POSIX option parser. `optstring` is the list of accepted flags; prefix with
`:` to suppress automatic error messages. Sets `OPTIND` and `OPTARG`.
Returns 0 while options remain, non-zero when exhausted.

### get `[PURE]`

```
get path property
```

Read object property `property` on the object at `path`. Command-form
equivalent of `path -> property`.

### help `[PURE]`

```
help [commands | resolvers | stages | types | extensions | where | <name>]
```

Print usage information. Without arguments shows an overview.
`help commands` lists all builtins. `help resolvers` lists value resolvers.
`help stages` lists pipeline stages. `help <name>` shows details for a
specific command, resolver, or stage.

### history `[PURE]`

```
history [n]
```

Print interactive command history. With `n`, shows the last `n` entries.
Only populated in interactive sessions.

### inspect `[PURE]`

```
inspect path
```

Print full object metadata for the object at `path` (type, size, permissions,
timestamps, etc.).

### jobs `[PURE]`

```
jobs
```

List all background jobs with their state (`running`, `stopped`, `done`) and
PID. The current job is marked `+`, the previous one `-`.

### let `[MIXED]`

```
let                          # list typed bindings
let name = value-expression  # create a typed binding
```

Typed binding management. Unlike `set` (which stores text), `let` stores the
full `ArkshValue` object, making the name usable in pipelines and
member-access chains.

### local `[MUTANT]`

```
local [name[=value] ...]
local -n name=target
```

Declare function-local variables. Each `local` variable shadows any
outer variable with the same name for the duration of the function.
`-n` creates a function-local nameref.

### perf `[MIXED]`

```
perf show    # print allocation counters
perf status  # print whether perf is enabled
perf on      # enable counters
perf off     # disable counters
perf reset   # reset counters to zero
```

Lightweight runtime counters. Enable at startup with `ARKSH_PERF=1`.

### plugin `[MIXED]`

```
plugin load path|name         # load a plugin from path or from autoload dir
plugin list                   # list loaded plugins
plugin info name              # print plugin metadata
plugin enable name            # re-enable a disabled plugin
plugin disable name           # disable a plugin without unloading
plugin autoload set path      # add path to autoload config
plugin autoload unset path    # remove path from autoload config
plugin autoload list          # list paths in autoload config
```

Plugin lifecycle management. See `docs/guide-plugin-author.md`.

### printf `[PURE]`

```
printf format [args...]
```

Format and print. Supports POSIX §2.2.3 format specs (`%s`, `%d`, `%f`,
`%x`, `%o`, `%b`, `%%`) plus width, precision, and padding flags.

### prompt `[MIXED]`

```
prompt show           # print current prompt config
prompt load path      # load a prompt config file
prompt render         # render the prompt string to stdout
```

Prompt configuration. Config files use `key=value` format.

### pwd `[PURE]`

```
pwd
```

Print the current working directory.

### read `[MUTANT]`

```
read [-r] [-p prompt] [-t timeout] [-n nchars] [name...]
```

Read a line from stdin into one or more variables. IFS-splitting applies.
`-r` disables backslash continuation. `-p` prints a prompt string.
`-t N` times out after N seconds. `-n N` reads exactly N characters.

### readonly `[MUTANT]`

```
readonly [name[=value] ...]
```

Mark variables as read-only. Subsequent assignments produce an error.

### return `[MUTANT]`

```
return [n | value-expression]
```

Exit the current shell function. If a value expression is given its result
becomes the function return value (accessible via `let` assignment at call
site). If `n` is given it sets the exit status.

### run `[PURE]`

```
run cmd [args...]
```

Execute an external command directly, bypassing shell builtins and function
overrides. Equivalent to `exec` without process replacement.

### set `[MUTANT]`

```
set [name[=value] ...]
set -e | -u | -x | -o pipefail
set +e | +u | +x | +o pipefail
```

With `name=value` or `name value`, assigns shell variables.
Without arguments, lists all shell variables.

| Flag | Effect |
|------|--------|
| `-e` / `+e` | Exit immediately on command failure (`errexit`) |
| `-u` / `+u` | Error on unset variable expansion (`nounset`) |
| `-x` / `+x` | Trace commands to stderr (`xtrace`) |
| `-o pipefail` | Pipeline exit status is the last non-zero stage |

### shift `[MUTANT]`

```
shift [n]
```

Shift positional parameters left by `n` (default 1). `$2` becomes `$1`,
`$3` becomes `$2`, etc.

### source / `.` `[MUTANT]`

```
source path [args...]
. path [args...]
```

Execute commands from `path` in the current shell context. Changes to
variables, functions, and directory persist after return.

### stty `[MUTANT]`

```
stty [-a]
stty setting
stty -setting
```

Show or change terminal line settings. `-a` prints all settings.
Pass-through to the system `stty`; also sets the internal terminal state.

### test / `[` `[PURE]`

```
test expression
[ expression ]
```

Evaluate POSIX conditional expressions. Returns 0 (true) or 1 (false).
Supports file tests (`-f`, `-d`, `-r`, `-w`, `-x`, `-e`, `-s`, `-L`, `-p`),
string tests (`-z`, `-n`, `=`, `!=`), and numeric comparisons
(`-eq`, `-ne`, `-lt`, `-le`, `-gt`, `-ge`).

### `[[` `[PURE]`

```
[[ expression ]]
```

Extended conditional. Adds glob pattern matching (`== pattern`), POSIX ERE
regex (`=~ regex`), compound conditions (`!`, `&&`, `||`), and grouping.
On regex match, `$BASH_REMATCH` holds the full match; the typed binding
`BASH_REMATCH` exposes captures as a list.

### trap `[MUTANT]`

```
trap [command signal...]
trap -p [signal...]
```

Define or list signal handlers. `signal` can be a signal name (`SIGTERM`,
`EXIT`, `ERR`, `DEBUG`, etc.) or number. `trap '' SIGNAL` ignores the signal.
`trap -p` prints current handlers.

### true `[PURE]`

```
true
```

Return success (exit code 0). Always succeeds.

### type `[PURE]`

```
type name [...]
```

Show how each `name` resolves: builtin, function, alias, or external
command (with path).

### typeset `[MUTANT]`

```
typeset -A name
typeset -n name=target
```

Alias for `declare`.

### ulimit `[MUTANT]`

```
ulimit [-a] [-n n] [-s n] [-u n] [-v n]
```

Show or change POSIX resource limits. `-a` prints all limits. On Windows,
reports an explicit stub warning.

### umask `[MUTANT]`

```
umask [mode]
```

Show or set the file mode creation mask. `mode` can be octal (`022`) or
symbolic (`u=rwx,g=rx,o=`).

### unalias `[MUTANT]`

```
unalias name [...]
unalias -a
```

Remove aliases. `-a` removes all.

### unset `[MUTANT]`

```
unset [-n] name [...]
```

Remove shell variables and typed bindings. `-n` removes a nameref itself
without touching its target variable.

### wait `[MUTANT]`

```
wait [%jobspec | pid]
```

Wait for background jobs to complete. Without argument waits for all.

---

## Value resolvers

Resolvers are called as `name(args...)` and return a typed `ArkshValue`.
They are the entry point to the object model.

### env

```
env()                   # map of all environment variables
env("VAR")              # value of a single variable
```

Access environment variables. `env("PATH")` returns the PATH string.
`env() -> keys()` lists all variable names.

### fs

```
fs()
```

Filesystem namespace. Properties: `cwd`, `home`, `temp`, `separator`.

### Integer, Float, Double, Imaginary

```
Integer(n)     # 64-bit signed integer
Float(n)       # 32-bit float
Double(n)      # 64-bit float
Imaginary(n)   # purely imaginary b·i
```

Explicit numeric type constructors. Support arithmetic with type-aware
promotion: `Integer(3) + Float(1.5)` promotes to `Float`.

### Dict

```
Dict()                   # empty immutable dictionary
Dict("k1", v1, "k2", v2, ...)
```

Immutable key-value dictionary with string keys. See
[Object extensions — Dict](#dict-1) for available methods.

### map

```
map("key", value, ...)
```

Create a typed map (key-value pair list). Supports nested maps, JSON
round-trip, and path access.

### Matrix

```
Matrix("col1", "col2", ...)
```

Create a matrix with named columns. Rows are added with `add_row`.
Supports CSV and JSON serialisation, column selection, and row filtering.
See [Object extensions — Matrix](#matrix-1) for full method list.

### path

```
path("string")
```

Create a path value from a string. The returned object exposes file/directory
properties and methods.

### proc

```
proc()           # current process info
proc(pid)        # process info for an arbitrary PID
```

Properties: `pid`, `ppid`, `pgid`, `sid`, `tty_cols`, `tty_rows`, `has_tty`,
`is_session_leader`, `is_process_group_leader`, `args`, `status`.

### shell

```
shell()
```

Arksh runtime introspection. Properties: `cwd`, `pid`, `login_mode`,
`interactive_shell`, `has_tty`, `tty_cols`, `tty_rows`, `history_path`,
`last_status`, `plugin_dir`.
Methods: `vars()`, `functions()`, `keys()`.

### sys

```
sys()
```

System info. Properties: `os`, `host`, `arch`, `cpu_count`.

### time

```
time()
```

Current time. Properties: `epoch`, `year`, `month`, `day`, `hour`,
`minute`, `second`, `iso`.

### user

```
user()
```

Current user info. Properties: `name`, `home`, `uid`, `shell`.

---

## Pipeline stages

Stages are chained after `|>`. Most stages are also callable without `|>` on
a value expression: `value_expr |> stage(args)`.

### Filtering

| Stage | Signature | Description |
|-------|-----------|-------------|
| `where` | `where(prop == val)` / `where(block)` | Keep items matching predicate |
| `filter` | same as `where` | Alias for `where` |
| `grep` | `grep("pattern")` | Keep items/lines matching pattern string |

### Ordering

| Stage | Signature | Description |
|-------|-----------|-------------|
| `sort` | `sort(prop asc\|desc)` / `sort(block)` | Sort list |

### Slicing

| Stage | Signature | Description |
|-------|-----------|-------------|
| `take` | `take(n)` | First N items |
| `first` | `first()` | First item only |

### Aggregation

| Stage | Signature | Description |
|-------|-----------|-------------|
| `count` | `count()` | Count items |
| `sum` | `sum()` / `sum("prop")` | Sum numeric items |
| `min` | `min()` / `min("prop")` | Minimum |
| `max` | `max()` / `max("prop")` | Maximum |
| `reduce` | `reduce(init, block)` / `reduce(block)` | Fold to single value |

### Projection / Transformation

| Stage | Signature | Description |
|-------|-----------|-------------|
| `each` | `each(prop)` / `each(block)` | Project each item |
| `pluck` | `pluck("nested.path")` | Extract nested path from each item |
| `map` | `map(block)` | Transform each item, return new list |
| `flat_map` | `flat_map(block)` | Transform + flatten one level |
| `group_by` | `group_by("prop")` / `group_by(block)` | Group items into a map |

### Text operations

| Stage | Signature | Description |
|-------|-----------|-------------|
| `lines` | `lines()` | Split text into list of lines |
| `trim` | `trim()` | Remove leading/trailing whitespace |
| `split` | `split("sep")` / `split()` | Split at separator |
| `join` | `join("sep")` / `join()` | Join items into text |

### JSON

| Stage | Signature | Description |
|-------|-----------|-------------|
| `to_json` | `to_json()` | Serialize value to JSON string |
| `from_json` | `from_json()` | Parse JSON string into value |

### Encoding

| Stage | Signature | Description |
|-------|-----------|-------------|
| `base64_encode` | `base64_encode()` | Encode to Base64 (RFC 4648, no external deps) |
| `base64_decode` | `base64_decode()` | Decode from Base64 |

### Matrix-specific

| Stage | Signature | Description |
|-------|-----------|-------------|
| `transpose` | `transpose()` | Swap rows and columns |
| `fill_na` | `fill_na("col", value)` | Replace empty cells in a column |

### Misc

| Stage | Signature | Description |
|-------|-----------|-------------|
| `render` | `render()` | Render any value to its text representation |

---

## Object extensions

Built-in extensions available on all typed values.

### any

| Member | Kind | Description |
|--------|------|-------------|
| `print` | method | Print the value to stdout |

### map

| Member | Kind | Signature / Description |
|--------|------|-------------------------|
| `keys` | property | List of all keys |
| `values` | property | List of all values |
| `entries` | property | List of `[key, value]` pairs |
| `get` | method | `get("key")` |
| `has` | method | `has("key")` → bool |
| `get_path` | method | `get_path("a[2].b")` — JSONPath-style nested access |
| `has_path` | method | `has_path("a.b")` → bool |
| `set_path` | method | `set_path("a.b", value)` → new map |
| `pick` | method | `pick("k1", "k2", ...)` → new map with selected keys |
| `merge` | method | `merge(other_map)` → new map |

### file / path

| Member | Kind | Description |
|--------|------|-------------|
| `read_json` | method | Parse the file as JSON → value |
| `write_json` | method | `write_json(binding)` — serialize binding to file |

### Dict

| Member | Kind | Signature / Description |
|--------|------|-------------------------|
| `keys` | property | List of all keys |
| `values` | property | List of all values |
| `get` | method | `get("key")` |
| `has` | method | `has("key")` → bool |
| `set` | method | `set("key", value)` → new Dict |
| `delete` | method | `delete("key")` → new Dict |
| `to_json` | method | Serialize to JSON string |
| `from_json` | method | Parse JSON string into Dict |
| `get_path` | method | `get_path("a[2].b")` |
| `has_path` | method | `has_path("a.b")` → bool |
| `set_path` | method | `set_path("a.b", value)` → new Dict |
| `pick` | method | `pick("k1", "k2", ...)` |
| `merge` | method | `merge(other)` |

### Matrix

| Member | Kind | Description |
|--------|------|-------------|
| `rows` | property | Number of rows |
| `cols` | property | Number of columns |
| `col_names` | property | List of column name strings |
| `type` | property | Type descriptor string |
| `add_row` | method | `add_row(v1, v2, ...)` → new Matrix |
| `drop_row` | method | `drop_row(n)` → new Matrix |
| `rename_col` | method | `rename_col("old", "new")` → new Matrix |
| `row` | method | `row(n)` → map for row n |
| `col` | method | `col("name")` → list of values in column |
| `select` | method | `select("c1", "c2", ...)` → new Matrix with subset |
| `where` | method | `where(block)` → new Matrix filtered by predicate |
| `to_maps` | method | Convert to list of maps |
| `from_maps` | method | `from_maps(list)` → Matrix |
| `to_csv` | method | Serialize to CSV string |
| `from_csv` | method | `from_csv(text)` → Matrix |
| `to_json` | method | Serialize to JSON |

---

## Syntax cheatsheet

### Variable assignment

```sh
set NAME value           # shell variable
NAME=value               # POSIX standalone assignment
NAME=value command       # env prefix (child process only)
export NAME=value        # export to child processes
readonly NAME=value      # read-only variable
local NAME=value         # function-local variable
declare -A AMAP          # associative array (Dict-backed)
declare -n REF=TARGET    # nameref: REF transparently accesses TARGET
local -n LREF=TARGET     # function-local nameref
```

### Typed bindings

```sh
let name = value-expression    # typed binding
let                            # list all bindings
unset name                     # remove variable and binding
unset -n name                  # remove nameref without touching target
```

### Functions

```sh
# arksh-style (named parameters)
function greet(name) do
  text("hello %s") -> print(name)
endfunction

# POSIX style
greet() {
  echo "hello $1"
}

greet world
```

### Control flow

```sh
if condition ; then cmd ; fi
if condition ; then cmd ; else cmd2 ; fi

while condition ; do cmd ; done
until condition ; do cmd ; done

for var in list(1, 2, 3) ; do cmd ; done
for word in a b c       ; do cmd ; done

case $VAR in
  *.md)  echo markdown ;;
  *.txt) echo text     ;;
  *)     echo other    ;;
esac

switch expression
  case "value" then cmd
  default      then cmd2
endswitch

condition ? then_value : else_value    # ternary
```

### Object expressions

```sh
receiver -> property               # property access
receiver -> method(arg1, arg2)     # method call
receiver -> prop1 -> method()      # chained access
```

### Object pipeline

```sh
source |> stage1(args) |> stage2(args)
```

### Blocks (closures)

```sh
[:x | expression]               # one parameter
[:x :y | expression]            # two parameters
[:acc :n | local next = acc + n ; next]   # with local
```

### Classes

```sh
class Animal do
  property name = text("unknown")
  method speak = [:self | text("...") -> print()]
endclass

class Dog extends Animal do
  method speak = [:self | text("woof") -> print()]
endclass

Dog() -> speak()
Dog() -> isa("Animal")    # → true
```

### Redirections

```sh
cmd > file          # stdout to file
cmd >> file         # append stdout
cmd < file          # stdin from file
cmd 2> file         # stderr to file
cmd 2>&1            # merge stderr into stdout
cmd <<< "string"    # here-string
cmd << EOF          # heredoc
  body
EOF
cmd <<- EOF         # heredoc (strip leading tabs)
  body
EOF
cmd <(subcmd)       # process substitution (POSIX only)
cmd >(subcmd)       # process substitution (POSIX only)
n>file  n>>file  n<file  n>&m  n<&m    # arbitrary fd
```

### Background and job control

```sh
cmd &           # run in background
jobs            # list jobs
fg [%n]         # bring job to foreground
bg [%n]         # resume stopped job in background
wait [%n]       # wait for job(s)
```

### Shell option flags

```sh
set -e          # exit on error
set -u          # error on unset variable
set -x          # trace execution
set -o pipefail # pipeline fails on any stage failure
set +e          # disable errexit (and similarly for others)
```

---

## Special variables

| Variable | Description |
|----------|-------------|
| `$?` | Exit status of the last command |
| `$$` | PID of the current shell (fixed at init) |
| `$!` | PID of the last background job |
| `$0` | Name or path of the shell / script |
| `$1`…`$9`, `${10}`, … | Positional parameters |
| `$#` | Number of positional parameters |
| `$@` | All positional parameters (separate words) |
| `$*` | All positional parameters (single word with IFS) |
| `$PPID` | PID of the parent process (fixed at shell init) |
| `$BASHPID` | PID of the current process (dynamic; differs in subshells) |
| `$LINENO` | Current line number in the script |
| `$FUNCNAME` | Name of the current function |
| `$BASH_SOURCE` | Path to the current source file |
| `$BASH_REMATCH` | Full match from the last `=~` test |
| `$OPTIND` | Current index for `getopts` |
| `$OPTARG` | Current option argument for `getopts` |
| `$IFS` | Input field separator (default: space/tab/newline) |
| `$ARKSH_RC` | Override path for the user RC file |
| `$ARKSH_PERF` | Set to `1` to enable performance counters at startup |
| `$ARKSH_HISTORY` | Override path for the history file |

---

## Parameter expansion

```sh
${var}              # basic expansion
${var:-default}     # default if unset or empty
${var:=default}     # assign default if unset or empty
${var:?error}       # error if unset or empty
${var:+value}       # value if set and non-empty

${#var}             # string length

${var#pattern}      # remove shortest prefix matching pattern
${var##pattern}     # remove longest prefix
${var%pattern}      # remove shortest suffix
${var%%pattern}     # remove longest suffix

${var/pat/rep}      # replace first occurrence
${var//pat/rep}     # replace all occurrences

${var:offset}       # substring from offset
${var:offset:len}   # substring of length len

${var^}             # uppercase first character
${var^^}            # uppercase all
${var,}             # lowercase first character
${var,,}            # lowercase all

# Associative arrays (declare -A / Dict-backed)
${amap[key]}        # read value at key
${amap[@]}          # all values
${!amap[@]}         # all keys
```

---

## Startup files

Loaded in order depending on mode:

### arksh (default mode)

| File | Condition |
|------|-----------|
| `$ARKSH_GLOBAL_PROFILE` | Login shell only |
| `${config_dir}/profile` | Login shell only |
| `~/.arksh_profile` | Login shell only |
| `$ARKSH_RC` | Always (if set) |
| `${config_dir}/arkshrc` | If `ARKSH_RC` not set |
| `~/.arkshrc` | Fallback |

`config_dir` follows XDG: `$XDG_CONFIG_HOME/arksh` on Linux,
`~/Library/Application Support/arksh` on macOS.

### sh mode (`--sh` or invoked as `sh`)

| File | Condition |
|------|-----------|
| Login profiles (see above) | `--login` only |
| `$ENV` | Always (if set) |

### Config and plugin autoload

| File | Description |
|------|-------------|
| `${config_dir}/arksh.conf` or `prompt.conf` | Prompt and theme config |
| `~/.arksh/plugins.conf` or `${config_dir}/plugins.conf` | Plugin autoload list (one path per line) |

---

## Plugin ABI

Version: **ABI major 5, minor 0**

A plugin is a shared library (`.so` / `.dylib` / `.dll`) that exports two
entry points:

```c
int arksh_plugin_query(ArkshPluginInfo *out_info);
int arksh_plugin_init(ArkshShell *shell,
                      const ArkshPluginHost *host,
                      ArkshPluginInfo *out_info);
```

`arksh_plugin_query` is called first. It must fill:

```c
out_info->abi_major       = ARKSH_PLUGIN_ABI_MAJOR;  // 5
out_info->abi_minor       = ARKSH_PLUGIN_ABI_MINOR;  // 0
out_info->name            = "myplugin";
out_info->version         = "1.0.0";
out_info->description     = "does something";
out_info->required_caps   = 0;  // ARKSH_CAP_* flags
out_info->provided_caps   = 0;
```

If `query` passes ABI validation, `init` is called with the host API:

```c
host->register_command(shell, name, handler, help, type)
host->register_property_extension(shell, target, name, handler)
host->register_method_extension(shell, target, name, handler)
host->register_value_resolver(shell, name, handler, help)
host->register_pipeline_stage(shell, name, handler, help)
host->register_type_descriptor(shell, name, description)
```

For a full worked example see `examples/plugins/arksh_sample_plugin.c` and
`docs/guide-plugin-author.md`.
