# arksh — Archetype Shell

**arksh** is an interactive shell and scripting language for Linux, macOS and Windows.

It provides classic POSIX-compatible shell execution while adding a typed, object-aware runtime:
filesystem resources, processes, numbers, strings, lists, dictionaries and command output
are first-class typed values queryable through member-access chains (`->`) and object pipelines (`|>`).

---

## Quick look

```sh
# List the 3 largest files in /usr/bin, sorted by size
ls /usr/bin |> sort_by size |> reverse |> take 3 |> map { |f| echo "${f->name}  ${f->size}" }

# Parse JSON and query a field
cat config.json |> json_parse |> { |o| echo ${o->version} }

# Use $PPID / $BASHPID as typed numbers
echo "parent PID: $PPID"
echo "this shell: $BASHPID"

# Nameref
declare -n alias_var=real_var
alias_var="hello"
echo $real_var   # → hello
```

---

## Installation

=== "Homebrew (macOS / Linux)"

    ```bash
    brew install --HEAD nicolorisitano82/arksh/arksh
    ```

=== "Build from source"

    ```bash
    git clone https://github.com/nicolorisitano82/arksh.git
    cd arksh
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
    cmake --build build
    sudo cmake --install build
    ```

=== "Windows (winget)"

    ```powershell
    # Once accepted into winget-pkgs:
    winget install Arksh.Arksh
    ```

---

## Core concepts

| Concept | Description |
|---------|-------------|
| **Typed values** | Every value has a runtime type: `string`, `number`, `bool`, `list`, `dict`, `path`, `file`, `directory`, `process`, `null` |
| **`->` member access** | Navigate object properties: `ls -> [0] -> name`, `proc($$) -> ppid` |
| **`\|>` object pipeline** | Chain typed stage operators without quoting: `map`, `filter`, `reduce`, `sort`, `take`, … |
| **Block literals** | `{ |x| x -> size > 1024 }` — closures passed to pipeline stages |
| **POSIX compat** | All POSIX sh syntax works; use `arksh --sh` for strict sh mode |
| **Plugin ABI v5** | Load typed extensions via `plugin load path/to/plugin.so` |

---

## Documentation

- [Installation guide](guide-installation.md) — build from source, packages, system shell setup
- [Syntax reference](sintassi-arksh.md) — language overview
- [Full reference](reference.md) — all 55 built-ins, 28 pipeline stages, special variables
- [Scripting guide](guide-scripting.md) — practical examples
- [sh mode](sh-mode.md) — POSIX-strict mode
- [Plugin author guide](guide-plugin-author.md) — write and distribute plugins
- [Troubleshooting](troubleshooting.md)
- [Changelog](../CHANGELOG.md)

---

## License

MIT — see [LICENSE](https://github.com/nicolorisitano82/arksh/blob/main/LICENSE).
