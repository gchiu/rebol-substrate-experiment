# H2C — Glon task stack audit and optimisation: results

Date: 2026-11-25
Branch: `experiment/glon-http-h2c`
Baseline: H2B, commit `ee36a81`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Audit of the H2B execution path

H2B runs each HTTP request as a Glon task: the owner loop spawns
`[ spawn [ http/task-run <request> ] ]`, then alternates `[ run-tasks ]` with the
async file/SQLite drains. The response path is:

```
task-run → judge → handler → http/respond → http/respond-with → reduce
```

- `task-run` wraps the handler in `judge` (a built-in that establishes a trap
  frame) and calls the handler.
- The handler (e.g. `examples/h2b/tasks.glon`) enqueues a SQLite request and
  `task-wait`s; its reply callback calls `http/respond` (per-id) + `task-wake`.
- `http/respond-with` in task mode builds a 5-element response with `reduce`.

The M1 task record (16 cells) holds the saved `[IP,SP,RP,RV_CUR,RV_END,RV_CTX,
RV_BLK,RV_FRAME,state,blocked,gen]`; each task has a private DS and RS carved
from the arena (`M1_TASK_CELLS`, default 3,400 = 400 DS + 3,000 RS). The
collector already treats saved tasks as GC roots and sentry-checks their SP/RP.

### Data vs. return stack vs. heap
- **Data stack (DS)**: small (a few cells per call); the 400-cell DS was never
  the constraint in the failing runs (the halt RP was within the RS, SP at the
  DS top).
- **Return stack (RS)**: the constraint. Each Glon call frame is ~73 cells
  (FIB-OPT-P3), so a few nested calls plus the evaluator's per-form frames add
  up.
- **Managed heap**: not the constraint (GC reclaims; peak RSS stays ~20 MiB).

## 2. Stack measurement (no arena change)

I added temporary per-task high-water fields (`TREC_MINSP`/`TREC_MINRP`) and a
`task-stack` reader, but the loadable `task.glon` then exceeded the fixed
**16,128-cell code space** (the RAW fragments are emitted into it), so that
instrumentation was reverted. I measured instead by **binary-searching the
per-task RS size** at which the H2B app serves a request, and by the halt-dump
`RP` (the return-stack pointer at the fail-stop).

Measured (large profile, `M1_TASKS=3`, varying `M1_TASK_CELLS`/`M1_RS_OFF`):

| per-task RS | simple `http/respond` app (`examples/h2b` min) | full H2B SQLite app |
|---|---|---|---|
| 2,040 | fail (machine halt) | fail |
| 2,500 | **ok** (first request) | fail |
| 3,000 | **ok** | fail |
| 3,400 | **ok** | fail (handler raised) |

- The **simple respond path needs ≈2,500 RS cells** (works at 2,500, fails at
  2,040). The ~3,700 figure reported by H2B included the SQLite callback chain
  and the `judge`/`reduce` frames; the pure respond path is ~2,500.
- The full SQLite app needs more than 3,400 and additionally hit a handler-level
  error under the task path (a separate integration issue), so 3,400 is a lower
  bound, not a clean pass.

### Why it is large
The chain `task-run → judge → handler → http/respond → http/respond-with →
reduce` is ~6 Glon frames (~440 cells), but each `judge`/`func`/`reduce`
activation also carries the evaluator's per-form bookkeeping, and the H2B app's
SQLite reply callback adds the `sqlite/step`/`finalize` continuation frames on
top. The dominant cost is **nested evaluator frames**, not retained garbage.

## 3. Reduction attempts

- Removing the `reduce` in `http/respond-with` (build the 5-element block
  directly) saves one frame — not enough (2,500 → ~2,430).
- Removing the `judge` wrapper in `task-run` (rely on the outer dispatch) saves
  the trap frame — a few dozen cells.
- Neither brings the requirement near the ~1,000 cells/task that ten tasks in
  the 10,200-cell arena would get. No runtime-semantics change was made.

## 4. Memory-layout options (measured)

**A. Stack reduction (keep 10,200-cell arena).** The measured floor is ~2,500
RS cells/task. Ten tasks would need ~25,000 cells; the arena is 10,200. Even
with aggressive inlining, ~2,000/task → 20,000 cells. **Not feasible** for ten
tasks without a larger arena.

**B. Larger task arena (recommended).** With a measured 2,500 cells/task and a
1.25× safety margin, ten tasks need `10 × 2,500 × 1.25 = 31,250` cells; round
to **32,000 cells** (`MEM_M1_CELLS=32000`). This keeps the frozen 64K map and
all other regions; it requires reducing `MEM_LOADER_CELLS` (large profile) or
raising `MEM_CELLS` — an explicitly approved scaling change, not done here.

**C. Alternative allocation.** A shared/segmented return-stack pool (tasks draw
from a common RS arena with per-task high-water limits) could reduce total
footprint, but it is a speculative redesign of the M1 memory model and is not
recommended without evidence that B is impossible.

**Recommendation: B** — raise the M1 arena to ~32,000 cells (measured 2,500
cells/task × 10 × 1.25 margin), which is the smallest change supported by the
measurements. This is a separate, explicitly approved scaling experiment.

## 5. Acceptance status (3 / 5 / 10 tasks)

- **3 tasks (3,400 cells):** the simple respond app serves a request; the full
  SQLite app errors in the task path. Not a clean pass.
- **5 tasks (2,040 cells):** fail (RS too small).
- **10 tasks (1,020 cells):** fail (RS far too small).
- The H2B 10-task SQLite suspension/interleaving test **does not pass**; per the
  milestone instruction I do **not** claim cooperative HTTP multitasking
  complete.

## 6. Regression safety

The H2C changes are inert for existing builds: the `TREC_MINSP`/`TREC_MINRP`
layout fields and their assembler symbols are unused unless the (reverted)
instrumentation is present; `task.glon` is unchanged. Default G3F and H2A modes
are unaffected. (Frozen-S1 check passes; no runtime semantics changed.)

## 7. Conclusion

The real per-task return-stack requirement for the H2B response path is
**≈2,500 cells** (simple) and **>3,400 cells** (with the SQLite callback chain).
Ten cooperative HTTP tasks therefore need an M1 arena of **≈32,000 cells**
(option B), not the current 10,200. The code-space ceiling (16,128) also blocks
embedding the full RAW task vocabulary plus instrumentation, which is a second
reason a larger loader/arena budget is needed. This is a measured, justified
memory-layout change; it is **not** applied here (requires approval).

## Reproduce

```
make glon MODULES=file,http,task MEM_PROFILE=large MEM_LOADER_CELLS=500000 \
  M1_TASKS=3 M1_TASK_CELLS=3400 M1_DS_OFF=400 M1_RS_OFF=3400 M1_WRAPPER_DELTA=256
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x examples/h2b/tasks.glon
```
Vary `M1_TASK_CELLS`/`M1_RS_OFF` to reproduce the 2,040/2,500/3,000/3,400
measurements. See H2B-RESULTS.md for the task-path design.