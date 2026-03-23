# Git Prompt Plugin

This plugin exposes Git-aware value resolvers that can be used in expressions
and directly inside prompt segments.

Resolvers:

- `git()` -> combined `branch + state` string, for example `main*`
- `git_branch()` -> current branch name
- `git_state()` -> one-character status mark
- `git_info()` -> map with `branch`, `state`, `root`, `git_dir`, `dirty`,
  `ahead`, `behind`, `conflicts`, `detached`, `inside_repo`

State marks:

- `=` clean branch
- `*` dirty or untracked changes
- `^` ahead of upstream
- `v` behind upstream
- `~` ahead and behind
- `!` conflicts
- `:` detached HEAD

Prompt example:

```ini
theme=default
left=userhost,cwd,git
right=status,os,time
plugin=git-prompt-plugin
color.git=yellow
```

Build:

```bash
cmake --build build --target arksh_git_prompt_plugin
```

Usage:

```bash
./build/arksh -c 'plugin load git-prompt-plugin ; git()'
./build/arksh -c 'prompt load examples/arksh-git.conf ; prompt render'
```
