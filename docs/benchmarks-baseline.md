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
