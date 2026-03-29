# Changelog

All notable changes to arksh are documented in this file.

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), versioning follows [Semantic Versioning](https://semver.org/).

---

## [Unreleased]

### Added

#### sudo as an object (E15-S3)
- `sudo()` value resolver: returns a typed `sudo_context` map; `sudo("cmd")` stores the target command
- Member-access chain `sudo("service") -> start()` dispatches as `sudo service start`
- `with sudo do ... endwith` compound block: sets `ctx_sudo` flag so all external commands inside are prefixed with `sudo`; multi-line form supported in both source files and REPL
- Windows: `sudo` member calls return an unsupported warning; `with sudo do` is a no-op
- 3 new CTests (335–337): resolver context, resolver with cmd, with-sudo block

---

## [0.1.0] — 2026-03-29

First public pre-release. All core subsystems are functional on Linux, macOS and Windows (x64).

### Added

#### Language and runtime (E1–E3)
- Full POSIX-compatible shell execution: pipelines, redirections, here-docs, globbing, brace expansion, tilde expansion
- Typed object model: `string`, `number`, `bool`, `list`, `dict`, `path`, `file`, `directory`, `process`, `null`
- Member-access chains via `->` (e.g. `ls -> [0] -> name`)
- Object pipeline `|>` with typed stage operators: `map`, `filter`, `reduce`, `sort`, `take`, `skip`, `flatten`, `zip`, `uniq`, `count`, `sum`, `min`, `max`, `first`, `last`, `reverse`, `chunk`, `join`, `keys`, `values`, `entries`, `to_list`, `to_dict`, `to_json`, `to_string`, `each`, `tap`, `group_by`
- Block literals `{ ... }` bindable with `let`
- `if`/`elif`/`else`/`fi`, `while`/`do`/`done`, `for`/`in`/`do`/`done`, ternary `? :`, `switch`/`case`/`endswitch`
- `function name() do ... endfunction` syntax

#### Parameter expansion and variables (E2)
- Full `${...}` expansion: `${var:-default}`, `${var:+alt}`, `${var:?err}`, `${var:=assign}`, `${#var}`, `${var%pat}`, `${var%%pat}`, `${var#pat}`, `${var##pat}`, `${var/pat/rep}`, `${var//pat/rep}`, `${var^}`, `${var^^}`, `${var,}`, `${var,,}`
- Arrays: `arr=(a b c)`, `${arr[i]}`, `${arr[@]}`, `${arr[*]}`, `${#arr[@]}`
- Special variables: `$?`, `$$`, `$!`, `$0`–`$9`, `$@`, `$*`, `$#`, `$PPID`, `$BASHPID`, `$LINENO`, `$RANDOM`, `$SECONDS`, `$OLDPWD`
- `nameref` via `declare -n` / `local -n`; write-through and `unset -n`

#### Built-ins (E3)
- 55 built-in commands including: `cd`, `pwd`, `echo`, `printf`, `read`, `source`, `eval`, `exec`, `exit`, `return`, `break`, `continue`, `true`, `false`, `test`/`[`, `[[`
- `declare`/`local`/`export`/`unset`/`readonly` with flags `-a`, `-i`, `-r`, `-x`, `-n`
- `trap` for signals and `ERR`/`EXIT` pseudo-signals
- `wait`, `jobs`, `fg`, `bg`, `disown`
- `getopts`, `type`, `hash`, `command`, `compgen`/`complete` (basic)
- `alias`/`unalias`, `history`, `fc`
- `time`, `ulimit`, `umask`
- `mapfile`/`readarray`
- `printf` with full format-string support

#### Job control and TTY (E4, E13)
- Full job control: `SIGTSTP`/`SIGCONT`, foreground/background, `fg`/`bg`/`jobs`/`disown`
- TTY state save/restore on foreground/background transitions
- `stty` passthrough from parent shell
- `SIGWINCH` handling and terminal resize propagation

#### Interactive UX (E5)
- Readline-based line editing with history search (`Ctrl-R`)
- Tab completion: commands, files, variables, built-ins
- Configurable prompt: `$ARKSH_PROMPT` and `$ARKSH_PROMPT2`
- Multi-line command editing

#### Object model extensions (E6)
- `map` / `Dict` / `Matrix` types
- `json parse`, `json stringify`, `json query` (E7)
- Plugin-typed values exposed to scripts

#### JSON and structured data (E7)
- `json parse <string>` → typed object tree
- `json stringify <obj>` → compact or pretty JSON
- `json query <obj> <path>` → jq-like path access

#### Quality and CI (E8)
- Full CTest suite (333 tests as of E15-S2)
- Address Sanitizer and Undefined Behaviour Sanitizer clean
- Valgrind-clean on Linux
- GitHub Actions matrix (Linux + macOS + Windows)

#### Performance (E12)
- Startup wall time ≤ 50 ms (non-interactive, E15-S2 guard)
- RSS footprint < 8 MB at startup

#### POSIX system shell (E11)
- `/etc/profile`, `~/.profile` startup sequence
- `ENV` file for non-interactive shells
- POSIX `set` options: `-e`, `-u`, `-x`, `-o pipefail`, etc.

#### Signal and TTY robustness (E13)
- Full signal disposition management for interactive and non-interactive modes
- Correct TTY restore on `exec` and subshell exit

#### `sh` compatibility mode (E14)
- `arksh --sh` runs as a strict POSIX sh interpreter
- Disables object extensions; passes POSIX test suite subset

#### Packaging and distribution (E9)
- CMake `install` target with standard directories (`bin`, `lib/arksh/plugins`, `share/man`, `share/arksh`)
- CPack support: `.deb`, `.rpm`, `.tar.gz` (Linux)
- Homebrew formula (`Formula/arksh.rb`) — install via `brew install --HEAD`
- winget manifest skeleton (`packaging/winget/`) ready for `microsoft/winget-pkgs`
- Man page `arksh(1)` installed to `man1`

#### Plugin system (E9-S3)
- ABI versioning: `ARKSH_PLUGIN_ABI_MAJOR 5` / `ARKSH_PLUGIN_ABI_MINOR 0`
- `arksh_plugin_query()` entry point; capability flags in `ArkshPluginInfo`
- `plugin load` validates major/minor before calling `arksh_plugin_init`
- `plugin list` and `plugin info` show ABI version and capabilities

#### Documentation
- User manual (`docs/user-manual.md`, `docs/manuale-utente.md`)
- Installation guide (`docs/guide-installation.md`)
- Scripting guide (`docs/guide-scripting.md`)
- Plugin author guide (`docs/guide-plugin-author.md`)
- Comprehensive language reference (`docs/reference.md`)
- Troubleshooting guide (`docs/troubleshooting.md`)
- Benchmarks baseline (`docs/benchmarks-baseline.md`)
- Man page `arksh(1)`

### Changed
- History loading is now guarded by `interactive_shell` flag — non-interactive shells no longer touch `~/.arksh_history`

### Fixed
- `$BASHPID` and `$PPID` now resolve correctly via platform-specific APIs (sysctl / procfs / Toolhelp32)
- TTY state restored correctly after background job returns to foreground
- Plugin loader rejects plugins with incompatible ABI major version

---

[Unreleased]: https://github.com/nicolorisitano82/arksh/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/nicolorisitano82/arksh/releases/tag/v0.1.0
