# sh mode — POSIX-strict execution

arksh ships with a POSIX-strict execution mode activated by the `--sh` flag (or by invoking the
binary as `sh`). In this mode all typed-object extensions are disabled and the shell behaves as
a conforming `/bin/sh` interpreter.

---

## Activating sh mode

### From the command line

```bash
# Run a single command
arksh --sh -c 'echo "hello from sh mode"'

# Run a script
arksh --sh myscript.sh

# Interactive sh session
arksh --sh
```

### As the system sh

```bash
# If arksh is installed at /usr/local/bin/arksh
sudo ln -s /usr/local/bin/arksh /usr/local/bin/sh
```

On systems that use `/bin/sh` for boot scripts, prefer placing the symlink at `/usr/local/bin/sh`
and adjusting `PATH` so user scripts pick it up without replacing the system sh.

### Shebang

```bash
#!/usr/bin/env arksh --sh
# This script runs in strict POSIX sh mode
set -eu
echo "safe script"
```

---

## What is disabled in sh mode

| Feature | arksh full | arksh --sh |
|---------|-----------|-----------|
| Typed values (`string`, `number`, …) | ✓ | disabled |
| Member access `->` | ✓ | disabled |
| Object pipeline `\|>` | ✓ | disabled |
| Block literals `{ \|x\| … }` | ✓ | disabled |
| `plugin load` | ✓ | disabled |
| `function f() do … endfunction` | ✓ | disabled (POSIX `f() { … }` used) |
| POSIX sh syntax | ✓ | ✓ |
| `set -e`, `set -u`, `set -o pipefail` | ✓ | ✓ |
| Here-docs, here-strings | ✓ | ✓ |
| `trap`, `wait`, `exec` | ✓ | ✓ |
| `ENV` file for non-interactive shells | ✓ | ✓ |
| `$PPID`, `$BASHPID` | ✓ | ✓ |

---

## Startup files in sh mode

| Mode | File loaded |
|------|-------------|
| Login interactive | `/etc/profile`, then `~/.profile` |
| Non-login interactive | value of `$ENV` (if set) |
| Non-interactive | value of `$ENV` (if set) |

Example:

```sh
export ENV="$HOME/.shrc"
echo "alias ll='ls -la'" >> ~/.shrc
arksh --sh   # .shrc is sourced automatically
```

---

## POSIX compliance notes

arksh `--sh` passes the following POSIX sh requirement categories:

- variable assignment and export
- positional parameters and `$@`/`$*`
- `${var:-default}` and all standard parameter expansions
- arithmetic `$(( ))` and command substitution `$( )`
- `if`/`while`/`for`/`case` control flow
- `trap` signal handlers including `EXIT` and `ERR`
- `set` options: `-e`, `-u`, `-x`, `-f`, `-n`, `-o pipefail`, `-o nounset`, etc.
- `getopts`, `read`, `printf`, `test`/`[`
- Here-documents

Known non-conformance (tracked):

- `POSIX` job control in `--sh` non-interactive mode: `wait -n` not yet implemented
- `typeset` is accepted as alias for `declare` (non-POSIX extension, harmless)

---

## Example: POSIX-only script

```sh
#!/usr/bin/env arksh --sh
set -eu

usage() {
    printf 'Usage: %s <dir>\n' "$0" >&2
    exit 1
}

[ $# -eq 1 ] || usage
target="$1"

[ -d "$target" ] || { echo "Not a directory: $target" >&2; exit 1; }

find "$target" -name '*.log' -mtime +7 -exec rm -f {} \;
echo "Cleaned old logs in $target"
```

This script runs identically under `arksh --sh`, `bash`, `dash`, and `busybox sh`.
