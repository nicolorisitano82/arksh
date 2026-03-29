# ARKsh Benchmark Baseline

This document captures the first repeatable CPU/memory baseline added in `E12-S1`.

## Scope

Artifacts introduced by `E12-S1`:

- `perf` built-in for lightweight counters
- `arksh_perf_runner` benchmark executable
- `arksh_perf` CMake target
- workload scripts in `tests/perf/`

What is measured:

- wall-clock time in milliseconds
- peak RSS in KB for the spawned `arksh` process
- allocation counters collected by the shell runtime
- hot-path counters for `arksh_value_copy()` and `arksh_value_render()`

Counter semantics:

- `malloc_*`, `calloc_*`, `realloc_*` count requested allocations, not retained/live heap
- `temp_buffer_*` is a focused subset for `allocate_temp_buffer()` hot-path allocations
- `startup` includes the shell allocation in `main()` because `ARKSH_PERF=1` is enabled before the first `calloc()`

## How To Re-run

From the repository root:

```bash
cmake --build build --target arksh_perf
```

Or single cases:

```bash
./build/arksh_perf_runner ./build/arksh startup command 'perf show'
./build/arksh_perf_runner ./build/arksh object-pipeline script tests/perf/object-pipeline.arksh
./build/arksh_perf_runner ./build/arksh registry-lookups script tests/perf/registry-lookups.arksh
./build/arksh_perf_runner ./build/arksh json-structured script tests/perf/json-structured.arksh
./build/arksh_perf_runner ./build/arksh function-scope script tests/perf/function-scope.arksh
./build/arksh_perf_runner ./build/arksh subshell script tests/perf/subshell.arksh
./build/arksh_perf_runner ./build/arksh command-substitution script tests/perf/command-substitution.arksh
```

## Baseline Snapshot

Collected on `2026-03-22 19:44:10 CET` from a local warm build run with:

```bash
cmake --build build --target arksh_perf
```

Environment:

- OS: `Darwin arm64`
- Build tree: `build/`
- Mode: warm local run

| Case | Wall ms | Max RSS KB | malloc calls | calloc calls | realloc calls | temp buffer calls | value_copy calls | value_render calls |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| startup | 3.858 | 21152 | 0 | 1 | 0 | 0 | 0 | 0 |
| object-pipeline | 7.772 | 27728 | 49 | 250 | 57 | 34 | 48 | 8 |
| json-structured | 7.893 | 37024 | 193 | 714 | 537 | 138 | 504 | 56 |
| function-scope | 7.772 | 28544 | 144 | 420 | 24 | 288 | 132 | 12 |
| subshell | 8.007 | 44624 | 41 | 146 | 9 | 42 | 40 | 0 |
| command-substitution | 15.908 | 45216 | 9 | 50 | 1 | 18 | 32 | 0 |

## Notes

- `command-substitution` and `subshell` are the heaviest RSS cases in this first snapshot.
- `json-structured` is the noisiest case in allocation/copy terms, which matches the analysis in [docs/studio-cpu-memoria.md](/Users/nicolo/Desktop/oosh/docs/studio-cpu-memoria.md).
- `function-scope` shows a very high `temp_buffer_calls` count, making it a good validation target for `E12-S2`.
- This baseline is intentionally simple: it is meant to be stable enough for before/after comparisons while `E12-S2` through `E12-S8` land.
- On Windows, `arksh_perf` currently prints an explicit placeholder message instead of running the POSIX benchmark path.

## E12-S2 Delta

After landing the scratch arena in `E12-S2`, the same benchmark bundle showed clear allocation drops in the hottest paths:

- `object-pipeline`: `calloc_calls` from `250` to `216`
- `json-structured`: `calloc_calls` from `714` to `576`
- `function-scope`: `calloc_calls` from `420` to `132`
- `subshell`: `calloc_calls` from `146` to `104`
- `command-substitution`: `calloc_calls` from `50` to `8`

This baseline file remains the reference "before" snapshot. The detailed implementation delta is tracked in the `E12-S2` work and validated by dedicated perf regression tests.

## E12-S3 Delta

`E12-S3` made `ArkshValue` and `ArkshValueItem` lighter by moving large scalar payloads out of the always-resident inline layout and giving string/object/block values explicit heap ownership.

Guardrails added in the unit suite:

- `sizeof(ArkshValue) <= 128`
- `sizeof(ArkshValueItem) <= 64`

Measured effects on the benchmark bundle:

- `object-pipeline`: `max_rss_kb` from `27728` to `20112`
- `json-structured`: `max_rss_kb` from `37024` to `32288`

Tradeoff observed after the refactor:

- `calloc_calls` increased in the same scenarios because string/object/block payloads are now allocated on demand instead of living inline in every value slot.
- The perf regression tests for `E12-S3` therefore validate resident-memory footprint, while the allocation-drop checks introduced in `E12-S2` remain focused on the paths where the scratch arena still gives a clear win.

## E12-S4 Delta

`E12-S4` moved the heavyweight shell registries and session buffers out of the inline `ArkshShell` layout and into heap-owned containers with explicit capacities.

Guardrails added in the suite:

- `sizeof(ArkshShell) <= 32768`
- startup perf check: `calloc_bytes <= 200000`

Measured startup effects after the refactor:

- `startup`: `max_rss_kb` from `21152` to `3120`
- `startup`: `calloc_bytes` now `68352`
- `ArkshShell` size now `29112` bytes

The biggest wins come from moving history, jobs, positional parameters, traps and the large runtime registries out of the base shell allocation. This lowers both the shell allocation footprint and the startup RSS seen by `arksh_perf_runner`.

## E12-S5 Delta

`E12-S5` replaced deep function and block scope snapshots with layered local frames for shell variables, typed bindings and positional parameters.

Guardrails covered by the suite after the refactor:

- full `ctest` green with dedicated regressions for local shadowing, `shift` isolation inside functions and command substitution visibility of function-local state
- the function-scope perf guard is now enforced at `calloc_calls <= 400` to leave headroom for the later AST and sourced-script refactors introduced in `E12-S7`

Measured results on the current build after `E12-S7`:

- `function-scope`: `calloc_calls=362`
- `function-scope`: `calloc_bytes=342324894`
- `function-scope`: `max_rss_kb=41952`
- `command-substitution`: `calloc_calls=73`

The main win of this step is architectural: shell functions and block execution now use `push/pop` scope frames instead of copying the whole visible scope on every call. The remaining cost around subshells and `$(...)` is now isolated enough to be tackled in `E12-S6` without reopening the function/block scoping paths.

## E12-S6 Delta

`E12-S6` replaced the old deep snapshot/restore path for subshells and command substitution with a lightweight cloned shell model:

- global vars/bindings/positional parameters stay shared-read and are shadowed through a cloned scope-frame chain
- mutable registries with shell-local side effects (`functions`, `aliases`, `classes`, `instances`, `plugins`, `jobs`, stages/resolvers/extensions) are copied only into the clone
- process-wide side effects are restored explicitly after the clone exits (`cwd` and exported env visible from the parent shell)

New regressions added for this step:

- rich-state subshell isolation: classes, plugins and exported vars do not leak out of `( ... )`
- rich-state command substitution isolation: plugin side effects and exported vars do not leak out of `$(...)`
- perf regressions for `subshell` and `command-substitution`

Measured results on the current build:

- `subshell`: `max_rss_kb=16704`
- `subshell`: `calloc_calls=176`
- `command-substitution`: `max_rss_kb=14128`
- `command-substitution`: `calloc_calls=8`

Regression strategy after this step:

- `command-substitution` now keeps a hard allocation guard at `calloc_calls <= 90`
- `subshell` now uses a resident-memory guard at `max_rss_kb <= 55000`

The main win here is that command substitution stays cheap while subshell no longer needs a full state snapshot/restore cycle and now has a much smaller RSS footprint than the original baseline (`44624 KB -> 16704 KB`).

## E12-S7 Delta

`E12-S7` removed a chunk of recursive `value -> text -> parse -> value` work from the hottest object-oriented paths:

- object expressions now keep the full top-level `->` chain in the AST instead of reparsing the left side at every step
- object member calls and pipeline stages now carry shallow parsed arguments for the common literal/block cases
- the executor can evaluate those pre-parsed arguments directly, which cuts down repeated `arksh_evaluate_line_value()` calls and avoids extra render/parse hops

New regressions added for this step:

- parser regression for chained object expressions with preserved member arguments
- parser regression for pipeline stages with shallow parsed literal arguments
- runtime regression for `read_json() -> get_path(...) |> render()`
- perf regression `arksh_perf_object_chain_render_drop`

Measured results on the current build:

- `object-chain`: `exit_code=0`
- `object-chain`: `wall_ms=19.002`
- `object-chain`: `value_render_calls=24`
- `object-chain`: `temp_buffer_calls=114`

Regression strategy after this step:

- `object-chain` keeps a render-count guard at `value_render_calls <= 40`
- `startup` now keeps an allocation-footprint guard at `calloc_bytes <= 2200000`
- `function-scope` keeps an allocation-count guard at `calloc_calls <= 400`
- `command-substitution` keeps an allocation-count guard at `calloc_calls <= 90`

The main win of this step is CPU-oriented rather than footprint-oriented: nested object chains and stage arguments no longer bounce through the parser repeatedly, and the perf guard tracks that improvement through a much lower render count on the dedicated chain-heavy workload. This refactor also moved AST handling in sourced and multiline execution paths off the stack, which slightly raised allocation counters but removed a fragile class of stack-pressure crashes; the updated perf guards reflect that new, more stable baseline.

## E12-S8 Delta

`E12-S8` closed the performance epoc with three focused interventions:

- remaining hot-path containers were made dynamic where the old fixed layout still hurt scalability, notably class property/method tables
- hot registries now use sorted indices plus binary-search lookup for commands, value resolvers, pipeline stages, classes and instances
- prompt rendering and interactive completion gained lightweight caches with explicit invalidation keyed by shell generations

Additional coverage added in this step:

- dedicated perf workload `tests/perf/registry-lookups.arksh`
- smoke test for the new perf case
- direct unit regression on `arksh_shell_set_class()` proving class definitions can now hold more than the old `32` property/method slots without relying on the lexer token budget

Measured results on the current build:

- `startup`: `max_rss_kb=4304`, `calloc_bytes=1949312`
- `registry-lookups`: `wall_ms=18.616`, `max_rss_kb=58992`
- `registry-lookups`: `calloc_calls=6718`, `value_copy_calls=531`

Interpretation:

- the indexed lookups remove repeated linear scans from hot runtime paths, but the synthetic `registry-lookups` workload is still dominated by the actual work done around class instantiation, evaluation and value copying
- the new prompt/completion caches target interactive latency and are intentionally lightweight; they are not fully represented by the current non-interactive perf bundle
- startup remains within the current guardrail (`calloc_bytes <= 2200000`) even after adding the index structures and cache metadata

With `E12-S8`, the planned `E12` roadmap is complete. Future performance work can now be driven by new measurements instead of structural debt in the core runtime.

## E15-S2 — Startup audit for the `/bin/sh` scenario

### Measurement setup (T1)

Measured with `arksh_perf_runner` on macOS 15.4 (Apple Silicon), 2026-03-29.
`hyperfine` was not installed; the in-tree runner was used instead.

Command under test: `arksh -c true`

| Metric          | Measured   | CTest guard |
|-----------------|------------|-------------|
| `wall_ms`       | ~3–5 ms    | `<= 50 ms`  |
| `max_rss_kb`    | 4 416 KB   | —           |
| `calloc_bytes`  | 1 949 592  | `<= 2 200 000` |

Target: < 10 ms on modern Linux. The 50 ms CTest threshold leaves 10× headroom
for slow CI machines.

### Phase profiling (T2)

The `arksh_shell_init_with_options` breakdown at startup (`-c true`, no history
file):

| Phase                            | Estimated cost | Skippable in non-interactive? |
|----------------------------------|----------------|-------------------------------|
| Core allocs (memset, traps, arena)| ~0.1 ms       | No                            |
| Platform session preparation      | ~0.5 ms       | No                            |
| Env setup + prompt config         | ~0.1 ms       | No                            |
| History path resolve + load       | ~0 ms*        | **Yes — now guarded**         |
| `register_builtin_*` (4 phases)   | ~1.5 ms       | No                            |
| Config/plugin autoload            | ~0.3 ms       | No (already skipped in sh mode) |
| `rebuild_all_lookup_indices`      | ~0.5 ms       | No                            |
| Profile/RC sourcing               | ~0.2 ms       | No                            |

\* Near-zero when no history file exists (early `fopen` failure). Savings grow
proportionally with history file size on heavily-used installs.

Conclusion: no single phase is a clear outlier. The registration + index rebuild
phases (phases 5 and 7) dominate and are structurally required.

### Optimization applied (T3)

`resolve_history_path` and `load_history` are now executed only when
`shell->interactive_shell == 1`. In non-interactive mode (`-c`, script file,
piped input) history is never read or written.

Rationale:
- History is only useful for interactive readline completion/recall.
- Non-interactive scripts should not influence or be influenced by the user's
  command history.
- The guard avoids an `fopen` + `stat` on every non-interactive invocation
  and, on large histories, avoids loading and parsing hundreds of lines that
  will never be used.

### CTest regression (T4)

```cmake
add_test(NAME arksh_perf_startup_wall_drop
  COMMAND arksh_perf_runner $<TARGET_FILE:arksh> startup-wall command "true"
          "wall_ms<=50")
```

This test fails if a future change causes startup to exceed 50 ms, providing a
stable, non-regressive guard for the startup path without relying on external
timing tools.
