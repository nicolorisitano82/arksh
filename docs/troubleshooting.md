# ARKsh Troubleshooting Guide

---

## Diagnostic Commands

Before diving into specific problems, these commands give you a quick picture
of the current shell state:

```text
help                     # list all built-ins, resolvers, stages and types
help sort                # show help for a specific command or stage
type ls                  # check if a name is a built-in, function or alias
plugin list              # show loaded plugins with ABI and capabilities
plugin info <name>       # full metadata for one plugin
echo $ARKSH_CONFIG_DIR   # resolved config directory
echo $ARKSH_PLUGIN_DIR   # resolved plugin directory
set                      # dump all shell variables
```

---

## Startup and Initialization

### Shell exits immediately or shows no prompt

- Check that `~/.config/arksh/arkshrc` (or `$ARKSH_RC`) is valid shell syntax.
  A fatal error in the RC file aborts startup.
- Run `arksh -c 'echo ok'` to verify the binary works without the RC.
- Temporarily bypass the RC: `ARKSH_RC=/dev/null arksh`

### RC file is not loaded

- The file must be named `arkshrc` (no dot, no extension) under `~/.config/arksh/`.
- Confirm with: `echo $ARKSH_CONFIG_DIR` — it should point to `~/.config/arksh`.
- Check that `$ARKSH_RC` is not set to a non-existent path.

### Login profile is not sourced

- `--login` or `-l` must be passed, or the binary name must start with `-`.
- Check which file is being looked for: profile is loaded from
  `~/.config/arksh/profile` or `~/.arksh_profile`.
- Set `ARKSH_LOGIN_PROFILE=/path/to/file` to override.

### `ENV` file is not loaded in sh mode

- `arksh --sh` only reads `$ENV` — not `$ARKSH_RC`.
- Confirm: `ENV=~/.shrc arksh --sh -c 'echo $ENV'`

---

## Prompt and Configuration

### Prompt shows `> ` instead of configured segments

- The prompt config file was not found or could not be parsed.
- Confirm the path: `ARKSH_CONFIG` > `arksh.conf` > `~/.config/arksh/prompt.conf`.
- Load explicitly: `prompt load ~/.config/arksh/prompt.conf`
- Check for syntax errors in the `.conf` file (key=value, one per line).

### Prompt does not show git branch

- The `git-prompt-plugin` must be loaded.
- Add `plugin=git-prompt-plugin` to `prompt.conf`, **or** load it manually:
  `plugin load build/arksh_git_prompt_plugin.dylib`
- Add `git` to the `left=` or `right=` segment list.
- Confirm git is on `$PATH`: `type git`

### Colors do not appear

- Set `use_color=1` in `prompt.conf`.
- Verify your terminal supports ANSI colors (`echo -e "\033[32mgreen\033[0m"`).
- On some terminals `TERM=dumb` disables color; check `echo $TERM`.

---

## Object Model and Pipelines

### `unknown property: <name>`

The object does not expose that property.

- Check what properties are available: `"." -> type` then `help <type>`
- Properties are case-sensitive.
- A plugin might provide the property but is not loaded. Run `plugin list`.

### `unknown method: <name>`

Same as above, for methods.

- `type <name>` shows whether the name resolves to a built-in or function.
- Confirm the plugin that provides the method is loaded.

### `... expects a list` or wrong type in pipeline

A pipeline stage received a value of an unexpected type.

- Print the intermediate value before the failing stage: remove stages one at a time.
- Use `|> render()` to inspect what the pipeline holds at any point.
- Bridge a shell command output with `capture_lines("cmd")` to get a proper list.

### Pipeline stage produces no output

- `|> count()` on an empty list returns `0`, not nothing.
- `|> where(...)` may filter everything out. Try without the filter first.
- Check that the source resolver returned a value: `resolver_name() -> type`

### `->` chain returns empty or unexpected value

- `->` is a member access, not shell substitution. Use `$( expr )` to get the
  text value in a shell context: `echo $( README.md -> size )`
- Chained calls require each intermediate value to support the next member.

---

## Shell Execution

### Variable is empty when it should not be

- Check for typos: shell variables are case-sensitive (`$HOME` ≠ `$home`).
- `set -u` causes the shell to exit on unset variables — useful for catching this early.
- Print the value before use: `echo "value=[$var]"` to see whitespace issues.

### Command not found

- `type <cmd>` tells you whether it is a built-in, function, alias or external.
- For external commands, check `$PATH`: `echo $PATH`
- Verify the binary is executable: `[ -x /path/to/cmd ] && echo ok`

### Script exits immediately

- If `set -e` is active, any command that returns non-zero causes exit.
- Wrap commands that may fail: `cmd || true`
- Check the exit code of the previous command: `echo $?`

### Redirection does not work as expected

- Output redirect `>file` truncates; use `>>file` to append.
- `2>&1` must come after the output redirect: `cmd > out.txt 2>&1`
- To discard all output: `cmd > /dev/null 2>&1`

### Job is not found

- `fg %1` requires a job with ID 1. Run `jobs` to see what is running.
- Background jobs are tied to the interactive shell; they do not persist across
  non-interactive invocations.

### Heredoc hangs

- The closing delimiter must appear on a line by itself with no leading spaces
  (unless using `<<-` and tabs).
- The delimiter is case-sensitive: `<<EOF` must close with `EOF`.

---

## Plugins

### `plugin load` fails: ABI mismatch

```
plugin: ABI mismatch: plugin requires major X, host is Y
```

Recompile the plugin against the current `include/arksh/plugin.h`.
Check: `grep ARKSH_PLUGIN_ABI_MAJOR include/arksh/plugin.h`

### `plugin load` fails: missing capability

```
plugin: host does not provide required capability: ...
```

The host binary is older than the plugin expects. Update arksh.

### `plugin load` fails: cannot open library

- Check the path is correct and the file exists.
- On macOS: `otool -L libmy_plugin.dylib` to check dependencies.
- On Linux: `ldd libmy_plugin.so` to check dependencies.
- Missing symbols: ensure the plugin was compiled with `-undefined dynamic_lookup`
  on macOS or that all `arksh_*` symbols resolve at runtime.

### Plugin command is available but returns nothing

- The callback wrote nothing to `out` and returned 0 — the shell interprets
  this as a successful empty result. Add output to the callback.

### Plugin autoload does not work

- Check the autoload file: `plugin autoload list`
- The path in the autoload file must be absolute or a bare plugin name resolvable
  in `$ARKSH_PLUGIN_DIR`.
- Verify the autoload file is at `~/.config/arksh/plugin-autoload.conf` or
  the path pointed to by `$ARKSH_CONFIG_DIR/plugin-autoload.conf`.

---

## sh Mode

### Script uses arksh extensions and fails with "not supported in sh mode"

- The script has `->`, `|>`, `let`, `extend`, `class`, `switch`, `[[ ]]`,
  `<<<`, `<(...)`, `>(...)`, or block literals `[:...]`.
- Either remove those constructs, or run without `--sh`.
- Run `arksh --sh -c 'source script.sh'` for a full error listing.

### `[[` works in normal mode but is rejected in sh mode

By design: `[[` is a bash/ksh extension not available in POSIX sh.
Replace with `[` and POSIX test operators.

---

## Performance

### Shell starts slowly

- Disable plugins temporarily: `ARKSH_CONFIG=/dev/null ARKSH_RC=/dev/null arksh -c true`
- Profile with `ARKSH_PERF=1 arksh -c true` to see internal timing counters.
- Reduce the number of autoloaded plugins.

### Pipeline is slow on large datasets

- `|> where(...)` before `|> sort(...)` reduces the set early.
- `|> take(n)` short-circuits large lists.
- `capture_lines("cmd")` is faster than `capture("cmd") |> lines()` for large output.

---

## Common Error Messages

| Message | Likely cause |
|---------|-------------|
| `parse error` | Syntax error in the input line |
| `unterminated if command` | Missing `fi` |
| `unterminated while command` | Missing `done` |
| `unterminated switch command` | Missing `endswitch` |
| `unable to open source file: <path>` | File does not exist or no read permission |
| `unable to allocate parser state` | Out of memory |
| `unknown property: <name>` | Receiver does not expose that property |
| `unknown method: <name>` | Receiver does not implement that method |
| `... expects a list` | Pipeline stage received wrong type |
| `job not found` | No active job with that ID |
| `not supported in sh mode` | Non-POSIX syntax used with `--sh` |
| `alias expansion error` | Alias expands to invalid syntax |
| `unable to fork` | Process limit reached or system error |
| `command not found: <name>` | Name not a built-in, function, alias, or PATH binary |

---

## Getting More Information

```text
help                        # full built-in list
help <command>              # usage for one command
inspect <expr>              # dump internal value representation
plugin info <name>          # plugin ABI, version, capabilities
set                         # all current shell variables
```

Report issues at the project repository.
