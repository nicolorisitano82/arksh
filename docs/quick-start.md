# Quick Start

This page gets you from zero to a working arksh session in under 5 minutes.

## 1. Install

Follow the [installation guide](guide-installation.md) for your platform.
The fastest path on macOS:

```bash
brew install --HEAD nicolorisitano82/arksh/arksh
```

## 2. Start an interactive session

```bash
arksh
```

You will see the default prompt `> `. Type any POSIX shell command — it works as expected.

## 3. Try the object model

```sh
# Type of the current directory
. -> type          # → directory

# List files and filter by size
ls |> filter { |f| f -> size > 10240 } |> map { |f| echo "${f->name}" }

# Sort processes by PID
proc list |> sort_by pid |> take 5
```

## 4. sh compatibility check

```bash
arksh --sh -c 'echo "POSIX works: $?"'
# → POSIX works: 0
```

## 5. Load a plugin

```sh
plugin load ~/.local/lib/arksh/hello.so
hello.greet "world"
```

## 6. Next steps

| Goal | Read |
|------|------|
| Learn the full syntax | [Syntax reference](sintassi-arksh.md) |
| Write scripts | [Scripting guide](guide-scripting.md) |
| Understand all built-ins | [Full reference](reference.md) |
| Write a plugin | [Plugin author guide](guide-plugin-author.md) |
| Set as system shell | [Installation guide — system shell](guide-installation.md#set-as-default-shell) |
