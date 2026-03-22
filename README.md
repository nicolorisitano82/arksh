# `ARKsh` is an object-oriented shell written in C for Linux, macOS, and Windows.

`ARKsh` stands for **Archetype Shell**: every resource is an object, and every object has a class.

The core idea is that filesystem entities, values, command output, plugins, and structured data can all be queried and transformed in a consistent way:

```text
. -> type
. -> children()
README.md -> read_text(256)
. -> children() |> where(type == "file") |> sort(size desc)
```

Nested member chains are first-class syntax, so direct forms like `data.json -> read_json() -> get_path("a[2].b")` are parsed once and executed without bouncing back through string reparsing at each `->` step.

## Implemented Features

- Interactive REPL and `-c` command mode
- Full lexer, AST, and executor for the shell language
- Filesystem object model for files, directories, devices, mount points, and abstract paths
- Object/member syntax with `receiver -> property` and `receiver -> method(...)`
- Typed runtime values: strings, numbers, booleans, lists, maps, dictionaries, blocks, classes, instances, and matrices
- First-class block literals in Smalltalk-style syntax: `[:param | body]`
- Object pipelines with `|>`: `where`, `sort`, `take`, `first`, `count`, `sum`, `min`, `max`, `render`, `lines`, `trim`, `split`, `join`, `grep`, `each`, `map`, `flat_map`, `group_by`, `reduce`, `to_json`, `from_json`, `base64_encode`, `base64_decode`, `transpose`, `fill_na`
- Native execution of external commands with shell pipes, redirections, and heredoc support
- Shell/object bridge: external command output can become typed pipeline input
- Control flow: `if`, `elif`, `else`, `while`, `until`, `for`, `break`, `continue`, `return`, ternary `?:`, `switch`, and `case`
- Shell functions with named parameters, local scope, and `builtin` fallback
- Custom classes with instantiation, properties, methods, `init`, and left-to-right multiple inheritance
- Runtime extensions via `extend` and native plugins
- Job control with `jobs`, `fg`, `bg`, `wait`, foreground process groups, and `Ctrl-Z` handling on POSIX systems
- Startup files, aliases, shell variables, exported variables, and persistent history
- Prompt configuration with theme segments
- Interactive line editor with highlighting, autosuggestion, and contextual completion
- Stable plugin ABI for commands, properties, methods, value resolvers, pipeline stages, and typed extensions

## Supported Platforms

| Platform | Compiler | Build Types | CI |
| --- | --- | --- | --- |
| Linux (Ubuntu 22.04+) | gcc >= 11 | Debug + Release | Ubuntu |
| macOS (13 Ventura+) | clang >= 15 (Xcode) | Debug + Release | macOS |
| Windows (10/11) | MSVC >= 19.38 (VS 2022) | Release | Windows |

Minimum requirements: CMake >= 3.20 and a C11 compiler.

## Build

```bash
cmake -S . -B build
cmake --build build
```

Run the repeatable performance bundle:

```bash
cmake --build build --target arksh_perf
```

Verified manual fallback on macOS:

```bash
mkdir -p build
cc -std=c11 -Wall -Wextra -pedantic -Iinclude \
   src/line_editor.c src/main.c src/executor.c src/expand.c \
   src/lexer.c src/object.c src/parser.c src/platform.c \
   src/plugin.c src/prompt.c src/shell.c -o build/arksh
cc -std=c11 -Wall -Wextra -pedantic -Iinclude -dynamiclib -undefined dynamic_lookup \
   plugins/sample/sample_plugin.c -o build/arksh_sample_plugin.dylib
cc -std=c11 -Wall -Wextra -pedantic -Iinclude -dynamiclib -undefined dynamic_lookup \
   plugins/skeleton/skeleton_plugin.c -o build/arksh_skeleton_plugin.dylib
```

On Linux, replace `-dynamiclib -undefined dynamic_lookup` with `-shared -fPIC`. On Windows, use `.dll`.

## Quick Start

```bash
./build/arksh
./build/arksh -c '. -> type'
./build/arksh -c '. -> children() |> where(type == "file") |> sort(size desc)'
./build/arksh -c 'list(1, 20, 3) |> sort(value desc)'
./build/arksh -c 'text(" a, b , c ") |> trim() |> split(",") |> join(" | ")'
./build/arksh -c 'list(1, 2, 3) |> reduce(number(0), [:acc :n | acc + n])'
./build/arksh -c 'text("{\"a\":[1,2,{\"b\":true}]}") |> from_json() |> to_json()'
./build/arksh -c 'env() -> HOME'
./build/arksh -c 'shell() -> plugins |> count()'
./build/arksh -c 'true && text("ok") -> print()'
./build/arksh -c 'sleep 1 & jobs'
./build/arksh -c 'perf on ; perf reset ; . -> children() |> count() ; perf show'
```

## Performance Work

ARKsh now ships a small profiling surface intended for optimization work on CPU time, allocations, and memory footprint.

Useful commands:

```bash
./build/arksh -c 'perf show'
./build/arksh -c 'perf on ; perf reset ; . -> type ; perf show'
ARKSH_PERF=1 ./build/arksh -c '. -> children() |> where(type == "file") |> sort(size desc)'
```

Repeatable benchmark workloads live under `tests/perf/`, and the CMake target below runs the bundle:

```bash
cmake --build build --target arksh_perf
```

The bundle includes an `object-chain` workload dedicated to nested `->` chains and stage-heavy object expressions, which is the main regression target added in `E12-S7`.

The initial measured baseline is documented in [docs/benchmarks-baseline.md](docs/benchmarks-baseline.md).

## Example Scripts

The repository includes example `.arksh` scripts:

```bash
./build/arksh -c 'source examples/scripts/01-filesystem-tour.arksh'
./build/arksh -c 'source examples/scripts/02-values-blocks-and-extensions.arksh'
./build/arksh -c 'source examples/scripts/03-shell-session.arksh'
./build/arksh -c 'source examples/scripts/04-control-flow.arksh'
./build/arksh -c 'source examples/scripts/05-shell-functions.arksh'
./build/arksh -c 'source examples/scripts/06-classes.arksh'
./build/arksh -c 'source examples/scripts/07-case-and-builtins.arksh'
./build/arksh -c 'source examples/scripts/08-redirections-and-heredoc.arksh'
./build/arksh -c 'source examples/scripts/09-binary-operators.arksh'
./build/arksh -c 'source examples/scripts/10-shell-object-bridge.arksh'
./build/arksh -c 'source examples/scripts/11-command-override.arksh'
```

## Language Snapshot

```text
# Object expressions
. -> type
README.md -> size
README.md -> read_text(256)

# Typed values
text("hello")
number(42)
bool(true)
list(1, 2, 3)
map("name", "arksh")
Dict()
Matrix("name", "score")

# Pipelines
. -> children() |> where(type == "file") |> sort(size desc)
tests/fixtures/json/nested.json -> read_json() -> get_path("a[2].b")
list(map("profile", map("name", "alpha")), map("profile", map("name", "beta"))) |> pluck("profile.name") |> join(",")
capture("pwd") |> lines() |> first()
list(1, 2, 3) |> map([:it | it + number(1)])

# Shell state
set PROJECT arksh
export PROJECT_ROOT "$PWD"
alias ll="ls -1"
let files = . -> children()

# Functions and classes
function greet(name) do
  text("hello %s") -> print(name)
endfunction

class Named do
  property name = text("unnamed")
endclass
```

## Documentation

- [docs/user-manual.md](docs/user-manual.md) - English user manual
- [docs/manuale-utente.md](docs/manuale-utente.md) - Italian user manual
- [docs/sintassi-arksh.md](docs/sintassi-arksh.md) - syntax reference
- [docs/scelte-implementative.md](docs/scelte-implementative.md) - implementation notes
- [docs/studio-cpu-memoria.md](docs/studio-cpu-memoria.md) - CPU and memory improvement study
- [docs/benchmarks-baseline.md](docs/benchmarks-baseline.md) - initial performance baseline and benchmark commands
- [docs/backlog-implementazione.md](docs/backlog-implementazione.md) - roadmap backlog and remaining work
- [docs/parser-dispatch.md](docs/parser-dispatch.md) - parser dispatch tree
- [docs/confronto-shell.md](docs/confronto-shell.md) - shell comparison notes

## Startup and Configuration

Standard user directories:

- config: `ARKSH_CONFIG_HOME`, otherwise `XDG_CONFIG_HOME/arksh`, otherwise `~/.config/arksh`
- cache: `ARKSH_CACHE_HOME`, otherwise `XDG_CACHE_HOME/arksh`, otherwise `~/.cache/arksh`
- state: `ARKSH_STATE_HOME`, otherwise `XDG_STATE_HOME/arksh`, otherwise `~/.local/state/arksh`
- plugins: `ARKSH_PLUGIN_HOME`, otherwise `XDG_DATA_HOME/arksh/plugins`, otherwise `~/.local/share/arksh/plugins`

At startup, `arksh` loads:

1. `ARKSH_RC` if set
2. otherwise `${ARKSH_CONFIG_HOME}/arkshrc` or the resolved standard config dir
3. legacy fallback: `~/.arkshrc`

History is stored in:

1. `ARKSH_HISTORY`
2. otherwise `${ARKSH_STATE_HOME}/history` or the resolved standard state dir
3. legacy fallback: `~/.arksh/history`

Prompt configuration is looked up in:

1. `ARKSH_CONFIG`
2. local `arksh.conf`
3. `${ARKSH_CONFIG_HOME}/prompt.conf` or the resolved standard config dir
4. legacy fallback: `~/.arksh/prompt.conf`

Minimal `arkshrc`:

```text
set PROJECT arksh
export PROJECT_ROOT "$PWD"
alias ll="ls -1"
prompt load $ARKSH_CONFIG_DIR/prompt.conf
```

Install locally with:

```bash
cmake --install build
```

## Plugins

After building, the sample plugin is available as a shared library:

- Linux: `build/arksh_sample_plugin.so`
- macOS: `build/arksh_sample_plugin.dylib`
- Windows: `build/arksh_sample_plugin.dll`

Load it:

```bash
./build/arksh -c 'plugin load build/arksh_sample_plugin.dylib ; plugin list'
```

After installation, plugins can also be resolved from the standard plugin directory, for example:

```bash
arksh -c 'plugin load sample-plugin ; plugin info sample-plugin'
```

The sample plugin registers:

- the `hello-plugin` command
- the `sample()` value resolver
- the `sample_wrap()` pipeline stage
- the `sample_tag` property on directory-like receivers
- the `sample_label(...)` method on filesystem objects

Plugin management commands:

```text
plugin list
plugin info sample-plugin
plugin disable sample-plugin
plugin enable sample-plugin
plugin autoload list
```

The plugin template is available in [plugins/skeleton](plugins/skeleton).

## Prompt Example

```ini
theme=aurora
left=userhost,cwd,plugins
right=status,os,date,time
separator= ::
use_color=1
color.userhost=green
color.cwd=cyan
color.status=yellow
color.date=blue
color.time=yellow
```

```bash
./build/arksh -c 'prompt load examples/arksh.conf'
```
