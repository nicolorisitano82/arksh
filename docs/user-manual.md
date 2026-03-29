# ARKsh User Manual

Reference version: current repository state (March 2026)

Italian version available in [manuale-utente.md](manuale-utente.md).

## 1. What ARKsh Is

ARKsh is an interactive shell and scripting language written in C. It keeps familiar shell behavior for ordinary process execution, while adding a typed, object-aware runtime.

Its main goals are:

- keep classic shell workflows usable
- make filesystem resources queryable as objects
- treat values, lists, maps, blocks, and command output as first-class runtime values
- support extension through classes, `extend`, and native plugins

Typical examples:

```text
. -> type
README.md -> read_text(128)
. -> children() |> where(type == "file") |> sort(size desc)
capture("ls /usr") |> lines() |> grep("lib") |> count()
```

## 2. Build and Run

### Homebrew (macOS / Linux)

```bash
brew install --HEAD nicolorisitano82/arksh/arksh
```

For full installation options (system-wide install, Windows, winget) see
[docs/guide-installation.md](guide-installation.md).

### Build from Source

Build with CMake:

```bash
cmake -S . -B build
cmake --build build
```

Run the interactive shell:

```bash
./build/arksh
```

Run a single command:

```bash
./build/arksh -c '. -> type'
```

Run a script:

```bash
./build/arksh my_script.arksh
```

Execute a file in the current shell context:

```bash
./build/arksh -c 'source examples/scripts/03-shell-session.arksh'
```

Run the test suite:

```bash
ctest --test-dir build --output-on-failure
```

Run the repeatable performance bundle:

```bash
cmake --build build --target arksh_perf
```

## 3. Mental Model

ARKsh has two complementary execution styles.

### 3.1 Classic shell commands

These run as ordinary commands or built-ins:

```bash
ls -la
grep main src/file.c
echo hello
```

Use shell pipes (`|`) when you want text streams.

### 3.2 Typed value expressions

These produce values instead of spawning ordinary commands:

```text
text("hello")
number(42)
bool(true)
list(1, 2, 3)
map("name", "arksh")
```

Use object pipelines (`|>`) when you want structured transformations.

### 3.3 Filesystem objects

A path can be used as a receiver:

```text
. -> type
. -> children()
README.md -> size
README.md -> parent()
```

### 3.4 Lightweight profiling and benchmarks

ARKsh includes a small telemetry surface for CPU, allocation, and memory work.

Available commands:

```text
perf show
perf status
perf on
perf off
perf reset
```

Examples:

```bash
./build/arksh -c 'perf show'
./build/arksh -c 'perf on ; perf reset ; . -> children() |> count() ; perf show'
ARKSH_PERF=1 ./build/arksh -c '. -> children() |> where(type == "file") |> sort(size desc)'
```

Repeatable benchmark workloads live in `tests/perf/`, and the `arksh_perf` CMake target runs the bundle.

The bundle also includes `tests/perf/object-chain.arksh`, which focuses on nested `->` chains and chained object calls.

The initial baseline is documented in [benchmarks-baseline.md](benchmarks-baseline.md).

## 4. Core Syntax

### 4.1 Receiver -> member syntax

General form:

```text
receiver -> property
receiver -> method(arg1, arg2)
```

Examples:

```text
. -> type
README.md -> read_text(64)
README.md -> permissions
README.md -> chmod("640") -> permissions
tests/fixtures/json/nested.json -> read_json() -> get_path("a[2].b")
```

Top-level `->` chains are parsed as one structured expression, so nested calls like `file -> read_json() -> get_path(...)` do not need an intermediate `let` just to stay efficient.

### 4.2 Value constructors

Common constructors:

```text
text("hello")
number(42)
bool(false)
list(1, 2, 3)
map("name", "arksh")
Dict()
Matrix("name", "score")
capture("pwd")
capture_lines("ls -1")
```

### 4.3 Blocks

Blocks are first-class values:

```text
[:it | it -> name]
[:acc :n | acc + n]
```

They can be stored in typed bindings:

```text
let is_file = [:it | it -> type == "file"]
let get_name = [:it | it -> name]
```

### 4.4 Local variables inside blocks

Use `local` to create block-local typed bindings:

```text
[:acc :n | local next = acc + n ; next]
```

### 4.5 Operators

Supported value-level operators:

```text
+  -  *  /
== != < > <= >=
condition ? true_value : false_value
```

### 4.6 Shell metadata and associative arrays

These are also available:

```text
text("%s") -> print("$LINENO")
text("%s") -> print("$FUNCNAME")
text("%s") -> print("$BASH_SOURCE")

declare -A colors
colors[sky]=blue
text("%s") -> print("${colors[sky]}")
text("%s") -> print("${!colors[@]}")
```

`declare -A` and `typeset -A` are backed by `Dict`: `${name[key]}` reads one entry, `${name[@]}` expands values, and `${!name[@]}` expands keys.

Examples:

```text
number(3) + number(4)
number(5) > number(3)
bool(true) ? "yes" : "no"
```

### 4.6 Shell mode flags

ARKsh supports the most common shell execution flags:

```text
set -e
set -u
set -x
set -o pipefail
set +e
set +u
set +x
set +o pipefail
```

Practical notes:

- `set -e` stops execution after a failing non-conditional command
- `set -u` raises an error on unset `$VAR` / `${VAR}` expansions, except forms with defaults such as `${VAR:-fallback}`
- `set -x` prints the expanded command to `stderr`, prefixed by `$PS4`
- `set -o pipefail` makes a shell pipeline fail when any stage fails

Example:

```text
set PS4 "TRACE: "
set -x
set -o pipefail
/usr/bin/true | /usr/bin/false | /usr/bin/true
```

On POSIX runtimes, `stty` is available as a shell builtin passthrough:

```text
stty -a
stty echo
stty -echo
```

The interactive runtime keeps a centralized pre-raw TTY snapshot, so if the REPL is terminated by a supported abnormal signal while the line editor is in raw mode, the terminal is restored before the process exits.

## 5. Pipelines

### 5.1 Object pipelines

Object pipelines use `|>`.

Examples:

```text
. -> children() |> where(type == "file") |> sort(size desc)
list(1, 20, 3) |> sort(value desc)
text(" a, b , c ") |> trim() |> split(",") |> join(" | ")
list(1, 2, 3) |> reduce(number(0), [:acc :n | acc + n])
tests/fixtures/json/nested.json -> read_json() -> get_path("a[2].b") |> render()
```

Common stages:

- `where(...)`
- `filter(...)`
- `sort(...)`
- `take(n)`
- `first()`
- `count()`
- `sum()`
- `min()`
- `max()`
- `lines()`
- `trim()`
- `split(sep)`
- `join(sep)`
- `grep(pattern)`
- `each(...)`
- `map(...)`
- `flat_map(...)`
- `group_by(...)`
- `reduce(init, block)`
- `render()`
- `to_json()`
- `from_json()`

### 5.2 Shell pipelines

Shell pipelines use `|` and operate on text streams:

```bash
ls -1 | wc -l
cat < README.md | wc -l
ls missing 2>&1 | wc -l
./arksh_test_echo_stdin <<< "hello"
read line <<< "$HOME"
wc -l <(printf "a\nb\n")
printf hi > >(wc -c)
```

`<<<` is a here-string: it expands the argument, appends a trailing newline, and feeds the result to the target command's `stdin`. It works with both external commands and built-ins that read from input, such as `read`.

On POSIX runtimes you can also use process substitution:
- `<(cmd)` exposes the output of `cmd` as a readable path
- `>(cmd)` exposes a writable path that feeds `cmd` through `stdin`

For extended shell conditionals you can also use `[[ ... ]]`:

```text
set s demo.txt
[[ "$s" == *.txt ]] && text("match") -> print()
[[ "$s" =~ ^demo\\.[a-z]+$ ]] && text("$BASH_REMATCH") -> print()
```

On interactive POSIX sessions, `arksh` also refreshes terminal metadata when it receives `SIGWINCH`:

```text
shell() -> tty_cols
shell() -> tty_rows
proc() -> tty_cols
proc() -> tty_rows
```

### 5.3 Bridge from commands to typed values

Use:

```text
capture("cmd")
capture_lines("cmd")
```

Examples:

```text
capture("pwd")
capture("ls -1") |> lines() |> first()
capture_lines("ls /usr") |> grep("lib") |> count()
```

## 6. Control Flow

### 6.1 if / elif / else / fi

```text
if . -> exists ; then
  text("yes") -> print()
else
  text("no") -> print()
fi
```

### 6.2 while and until

```text
while false ; do
  text("loop") -> print()
done

until true ; do
  text("retry") -> print()
done
```

### 6.3 for

```text
for n in list(1, 2, 3) ; do
  n -> value
done
```

### 6.4 switch

```text
switch . -> type
case "directory"
then
text("dir") -> print()
default
then
text("other") -> print()
endswitch
```

### 6.5 Shell-style case

```text
case text("demo.txt") in
*.md) text("md") -> print() ;;
*.txt) text("txt") -> print() ;;
esac
```

### 6.6 break, continue, return

These behave as expected inside loops and functions:

```text
break
continue
return text("done")
```

## 7. Functions

Definition:

```text
function greet(name) do
  text("hello %s") -> print(name)
endfunction
```

Shell-style call:

```text
greet nicolo
```

Use `local` inside functions:

```text
function demo(name) do
  local prefix=hello
  text("%s %s") -> print("$prefix", name)
endfunction
```

Outside a shell function, `local` fails with `local: not in a function`.

Use `builtin` to bypass an override:

```text
builtin pwd
```

## 8. Classes and Runtime Extension

### 8.1 Classes

```text
class Named do
  property name = text("unnamed")
endclass

class Document extends Named do
  method init = [:self :name | self -> set("name", name)]
endclass
```

Instantiation:

```text
let doc = Document(text("manual"))
doc -> name
```

### 8.2 Multiple inheritance

ARKsh supports left-to-right multiple inheritance:

```text
class Artifact extends Named, Printable do
  ...
endclass
```

### 8.3 `extend`

You can add properties and methods to built-in receivers:

```text
extend directory property child_count = [:it | it -> children() |> count()]
extend object method label = [:it :prefix | prefix]
```

## 9. Plugins

Load a plugin:

```text
plugin load build/arksh_sample_plugin.dylib
```

Inspect and manage plugins:

```text
plugin list
plugin info sample-plugin
plugin disable sample-plugin
plugin enable sample-plugin
plugin autoload list
```

The sample plugin adds a command, a resolver, a stage, a property, and a method. The base template lives in `plugins/skeleton`.
`plugin info` shows ABI and capability metadata, and `plugin list` includes the ABI plus the capabilities provided by each loaded plugin.

## 10. Prompt and Startup

Standard user directories:

- config: `ARKSH_CONFIG_HOME`, otherwise `XDG_CONFIG_HOME/arksh`, otherwise `~/.config/arksh`
- cache: `ARKSH_CACHE_HOME`, otherwise `XDG_CACHE_HOME/arksh`, otherwise `~/.cache/arksh`
- state: `ARKSH_STATE_HOME`, otherwise `XDG_STATE_HOME/arksh`, otherwise `~/.local/state/arksh`
- plugins: `ARKSH_PLUGIN_HOME`, otherwise `XDG_DATA_HOME/arksh/plugins`, otherwise `~/.local/share/arksh/plugins`

ARKsh loads startup state in this order:

1. `ARKSH_RC`, if defined
2. otherwise `${ARKSH_CONFIG_HOME}/arkshrc` or the resolved standard config dir
3. legacy fallback: `~/.arkshrc`

History:

- `ARKSH_HISTORY`
- otherwise `${ARKSH_STATE_HOME}/history` or the resolved standard state dir
- legacy fallback: `~/.arksh/history`

Prompt config lookup:

1. `ARKSH_CONFIG`
2. local `arksh.conf`
3. `${ARKSH_CONFIG_HOME}/prompt.conf` or the resolved standard config dir
4. legacy fallback: `~/.arksh/prompt.conf`

Default prompt:

```text
user@host | /current/path >
```

Example prompt configuration:

```ini
theme=aurora
left=userhost,cwd,plugins
right=status,os,date,time
separator= ::
use_color=1
```

Available `left` and `right` segments:

- `user` — current user
- `host` — machine hostname
- `userhost` — combined `user@host`
- `cwd` — current working directory
- `status` — last command status, `ok` or `err:N`
- `os` — operating system name
- `plugins` — number of loaded plugins
- `date` — current date in `YYYY-MM-DD`
- `time` — current time in `HH:MM:SS`
- `datetime` — current date and time in `YYYY-MM-DD HH:MM:SS`
- `theme` — active prompt theme name

Practical notes:

- `left` and `right` accept the same comma-separated segment names
- you can also use zero-argument resolvers that render to text, such as `git` from the Git prompt plugin
- if a segment resolves to an empty string, it is simply omitted from the prompt

Git-aware prompt configuration:

```ini
theme=default
left=userhost,cwd,git,plugins
right=status,os,date,time
plugin=git-prompt-plugin
color.git=yellow
```

Git state marks:

- `=` clean branch
- `*` dirty or untracked changes
- `^` ahead of upstream
- `v` behind upstream
- `~` ahead and behind
- `!` conflicts
- `:` detached HEAD

Local installation:

```bash
cmake --install build
```

Load it:

```text
prompt load examples/arksh.conf
prompt load examples/arksh-git.conf
prompt render
```

## 11. Interactive Features

The interactive editor includes:

- persistent history
- syntax highlighting
- autosuggestion from history
- contextual completion
- object member completion after `->`
- stage and resolver completion

Examples:

```text
README.md -> <Tab>
. -> children() |> so<Tab>
plugin <Tab>
```

## 12. Useful Built-ins

Common built-ins:

- `help`
- `pwd`
- `cd`
- `type`
- `history`
- `set`
- `export`
- `unset`
- `alias`
- `unalias`
- `source`
- `eval`
- `exec`
- `trap`
- `jobs`
- `fg`
- `bg`
- `wait`
- `read`
- `printf`
- `test`
- `[[`
- `let`
- `extend`
- `plugin`
- `prompt`

Quick usage:

```text
help
help commands
help resolvers
help stages
help types
```

## 13. JSON and Structured Data

Serialization and parsing:

```text
map("a", list(1, 2, map("b", true))) |> to_json()
text("{\"a\":[1,2,{\"b\":true}]}") |> from_json()
```

File-oriented helpers on path-like receivers:

```text
data.json -> read_json()
data.json -> write_json(payload)
data.json -> read_json() -> get_path("a[2].b")
let data = data.json -> read_json()
data -> set_path("meta.version", number(2))
list(map("profile", map("name", "alpha")), map("profile", map("name", "beta"))) |> pluck("profile.name")
```

Direct chaining is the preferred form when you only need a one-off query:

```text
data.json -> read_json() -> get_path("meta.version")
```

Use an intermediate `let` only when you want to reuse the parsed JSON value multiple times in the same script.

Useful query and transform helpers:

- `get_path("a[2].b")` reads nested paths using `.` segments and `[n]` indexes
- `has_path(...)` checks whether a nested path exists without failing
- `set_path(path, value)` returns an updated copy of the current value
- `pick("k1", "k2")` keeps only selected keys from a `map` or `dict`
- `merge(other)` combines two `map` or `dict` values, with the right side overriding
- `pluck("profile.name")` projects a nested field from each element in a list

## 14. Troubleshooting

Typical issues:

- `unknown property`: the receiver does not expose that property
- `unknown method`: the receiver does not implement that method
- `... expects a list`: a stage was applied to the wrong value type
- `job not found`: there is no active job matching `%n`
- `unable to open source file`: path resolution or permissions failed

Useful diagnostics:

```text
type ls
plugin list
help sort
help Matrix
```

## 15. Cheat Sheet

```text
. -> type
. -> children() |> where(type == "file") |> sort(size desc)
text(" a, b ") |> trim() |> split(",") |> join(" | ")
list(1, 2, 3) |> reduce(number(0), [:acc :n | acc + n])
capture_lines("ls -1") |> grep("md") |> count()

set PROJECT arksh
export PROJECT_ROOT "$PWD"
alias ll="ls -1"

function greet(name) do
  text("hello %s") -> print(name)
endfunction

class Named do
  property name = text("unnamed")
endclass

extend directory property child_count = [:it | it -> children() |> count()]

sleep 5 &
jobs
fg
```

## 16. Related Docs

- [manuale-utente.md](manuale-utente.md)
- [sintassi-arksh.md](sintassi-arksh.md)
- [scelte-implementative.md](scelte-implementative.md)
