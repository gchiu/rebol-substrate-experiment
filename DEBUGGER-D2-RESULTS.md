# D2 — Task-aware debugger for Glon: results

Date: 2026-11-25
Branch: `experiment/glon-debugger-d2`
Baseline: H2O-R, commit `09e9116`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. D1 audit (from H2O-R, re-verified)

`DEBUGGER-D1-RESULTS.md`, `DEBUGGER-D1-FREEZE.md`, `DEBUGGER-D1-HARDENING.md`,
`r0_s1_debug_tests.c`, `examples/debugger-*.r0`.

Capabilities: `debug-break`, state capture (SP/RP/HP/`RV_CTX`/`RV_FRAME`),
`ctx-lookup` (lexical-chain walk), `frame-get`/`frame-depth`, continue/resume.
**Not available:** single-step, task selection (D1 captures the current world).

## 2. D2 attempt

I added read-only task inspection to `modules/task.glon`:
- `task-ctx handle` → the saved `TREC_CTX`; `task-frame handle` → `TREC_FRAME`.
- `ctx-lookup` (D1's generic lexical-chain walk, reused verbatim) + `task-inspect
  handle 'name` to resolve a binding in the **saved** chain of a suspended task.

The approach is read-only (no switch into the task; no mutation).

## 3. Blocker: fixed code-space ceiling

`task.glon` is loaded as a Glon block whose RAW fragments are emitted into the
fixed code region `[256, R0S1_CODE_LIMIT)` where
`R0S1_CODE_LIMIT = R0S1_DS_INIT - R0S1_DS_RESERVE = 16384 - 256 = 16128`. The
added `ctx-lookup` walk needs **~244 cells**, but only **52** were free, so the
library fails to load (`masm code space exhausted`).

Raising `R0S1_DS_INIT` to 16640/16768 to enlarge the region was tried: the main
data-stack sentry then fail-stops (`main data-stack overflow (SP < code)`), so it
is not a safe way to gain code space. The code region is part of the frozen
memory map; increasing it safely requires a separate, approved memory-map change
(loader/arena relocation), which this milestone forbids without measured
justification.

## 4. Consequence

D2's task-inspection extension is **blocked by the code-space ceiling**, not by a
runtime-semantics problem. The read-only `task-ctx`/`task-frame` readers are small
enough, but resolving a binding needs the `ctx-lookup` walk, which does not fit.

## 5. Diagnosis carried forward (H2O-R)

The first divergence remains the **async SQLite resume** in the C-spawned HTTP
task path: the Glon-spawned SQLite mock preserves nested locals, the C-spawned
`--http-tasks` task halts on the unbound helper local `a` (word 36) after the
SQLite reply + `task-wake`. The session boundary (H2M), loader-heap lifetime
(H2N) and GC sentry (H2G) are ruled out. The precise instruction that drops the
link is still unisolated because D2 cannot be loaded.

## 6. Status

No runtime fix. Frozen S1 unchanged; default G3F/H2A untouched; task build + M1/M2
pass. D2 task inspection is not available (code-space). The two-task HTTPS
interleaving is not demonstrated and not claimed.

## 7. Next step

Provide a small, approved code-region increase (or reduce emitted code
elsewhere) so the `ctx-lookup` task-inspection extension fits; then use it to
compare the saved `TREC_CTX`/`TREC_FRAME` and the helper activation between the
passing mock and the failing C-spawned task.

## Reproduce

```
make glon MODULES=task MEM_PROFILE=large MEM_LOADER_CELLS=500000   # ok
# with the ctx-lookup task-inspect added, task.glon fails to load:
#   r0_s1: masm code space exhausted ... 52 are free below the code limit 16128
```