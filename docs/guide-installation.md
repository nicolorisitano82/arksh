# ARKsh Installation Guide

## Requirements

| Platform | Compiler | CMake |
|----------|----------|-------|
| Linux | GCC 11+ or Clang 14+ | 3.20+ |
| macOS | Xcode Clang 15+ | 3.20+ |
| Windows | MSVC 19.38+ (VS 2022) | 3.20+ |

No external libraries are required for the base build.

---

## Build from Source

```bash
# Clone
git clone https://github.com/nicolorisitano82/arksh.git
cd arksh

# Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Verify
./build/arksh -c 'echo "arksh works"'
```

### Optional build flags

| Flag | Default | Description |
|------|---------|-------------|
| `-DBUILD_TESTING=OFF` | ON | Skip building the test suite |
| `-DCMAKE_INSTALL_PREFIX=/usr/local` | system default | Override install prefix |

---

## Install System-Wide

```bash
cmake --install build
```

Default install paths (prefix `/usr/local`):

| File | Path |
|------|------|
| Binary | `$prefix/bin/arksh` |
| Plugins | `$prefix/lib/arksh/plugins/` |
| Examples | `$prefix/share/arksh/examples/` |
| Man page | `$prefix/share/man/man1/arksh.1` |

Custom prefix:

```bash
cmake --install build --prefix ~/.local
```

---

## Set as Login Shell

### macOS / Linux

Add arksh to the list of valid shells, then change the default:

```bash
# Add to /etc/shells
echo "$(which arksh)" | sudo tee -a /etc/shells

# Change default shell for current user
chsh -s "$(which arksh)"
```

### `sh` compatibility symlink

To use arksh as a POSIX `sh` replacement for scripts and shebangs:

```bash
# Create a symlink named sh that activates sh-compatibility mode automatically
ln -s "$(which arksh)" /usr/local/bin/sh
```

When invoked as `sh` (or with `--sh`), arksh disables all non-POSIX extensions.
See [sh-mode compatibility](#sh-mode-compatibility) below.

---

## First-Time Setup

### RC file

On first interactive launch, arksh looks for a startup file in this order:

1. `$ARKSH_RC` (if set)
2. `~/.config/arksh/arkshrc`
3. `~/.arkshrc` (legacy)

Copy the example RC to get started:

```bash
mkdir -p ~/.config/arksh
cp "$(arksh -c 'echo $ARKSH_DATA_DIR')/examples/arkshrc.example" \
   ~/.config/arksh/arkshrc
```

Minimal example `~/.config/arksh/arkshrc`:

```bash
export EDITOR=vim
export PAGER=less
alias ll="ls -lh"
alias la="ls -lha"
```

### Prompt configuration

```bash
cp "$(arksh -c 'echo $ARKSH_DATA_DIR')/examples/prompt.conf.example" \
   ~/.config/arksh/prompt.conf
```

Minimal `~/.config/arksh/prompt.conf`:

```ini
theme=aurora
left=userhost,cwd
right=status,date
use_color=1
```

### Login shell profile

When launched with `--login` or as a login shell, arksh reads:

1. `$ARKSH_GLOBAL_PROFILE` — system-wide profile (e.g. `/etc/arksh/profile`)
2. `~/.config/arksh/profile`
3. `~/.arksh_profile` (legacy)

---

## Directory Layout

All directories follow XDG conventions and can be individually overridden:

| Purpose | Override var | Default (Linux/macOS) |
|---------|-------------|----------------------|
| Config | `ARKSH_CONFIG_HOME` | `~/.config/arksh` |
| Cache | `ARKSH_CACHE_HOME` | `~/.cache/arksh` |
| State / history | `ARKSH_STATE_HOME` | `~/.local/state/arksh` |
| Data / plugins | `ARKSH_DATA_HOME` | `~/.local/share/arksh` |
| Plugin directory | `ARKSH_PLUGIN_HOME` | `~/.local/share/arksh/plugins` |

Inspect resolved paths at runtime:

```text
arksh -c 'echo $ARKSH_CONFIG_DIR'
arksh -c 'echo $ARKSH_PLUGIN_DIR'
```

---

## Plugin Autoload

Place plugin `.dylib`/`.so`/`.dll` files in the plugin directory.
To enable autoload, create `~/.config/arksh/plugin-autoload.conf`:

```ini
# one plugin name or path per line
git-prompt-plugin
```

Check autoload status:

```text
plugin autoload list
```

---

## sh-mode Compatibility

arksh can run as a POSIX-compatible `sh` substitute:

```bash
# Explicit flag
arksh --sh my_script.sh

# Via argv[0] (symlink named sh)
sh my_script.sh
```

In sh-mode:
- Non-POSIX syntax is rejected (`->`, `|>`, `let`, `extend`, `class`, `switch`, `[[ ]]`, `<<<`, `<(...)`, `>(...)`, block literals)
- Plugin autoload and arksh config are skipped
- The `ENV` environment variable is read as startup file (POSIX sh convention)

Startup with `ENV`:

```bash
export ENV=~/.shrc
sh -c 'echo $SHELL'
```

---

## Verify the Installation

```bash
# Version and built-in help
arksh -c 'help'

# Run the test suite (from the build directory)
ctest --test-dir build --output-on-failure -j4

# Quick smoke test
arksh -c '. -> type'                    # should print "directory"
arksh --sh -c 'echo POSIX works'        # sh-mode
```

---

## Uninstall

```bash
# If installed via cmake --install
xargs rm -f < build/install_manifest.txt
```
