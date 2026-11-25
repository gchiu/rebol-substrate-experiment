# M2 — Complete Glon's existing multitasking: results

Date: 2026-11-25
Branch: `experiment/glon-m2`
Baseline: G3F, commit `52058067`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Audit: what actually existed

### Task vocabulary and scheduler
- The M1 words `spawn`, `yield`, `run-tasks`, `task-finish` and the mechanism
  `mnew-task` existed **only** inside the C test driver `r0_s1_m1_tests.c`
  (as the `M1_LIB` string) and in `examples/multitasking.r0`. The `.r0` listing
  is a stale canonical copy with hard-coded addresses from an older layout.
  **None of it was available to a normal Glon application.**
- A task is a 16-cell record in a fixed table (`M1_TASK_TABLE`) holding
  `[resume_ip, SP, RP, RV_CUR, RV_END, RV_CTX, RV_BLK, RV_FRAME, state]`.
  `REG_HP` is global/shared; each task has a private DS/RS in the arena.
- `run-tasks` is a cooperative round-robin loop; `yield` suspends the current
  task. `M1_MAX_TASKS` was **3** and the arena is 10,200 cells.

### Runtime support
- `r0_s1_runtime.c` already treats saved task records/arenas as **GC roots**
  and guards a task's saved SP/RP against its arena bounds. It contains **no**
  scheduling semantics (deliberate). The RAW assembler resolves `M1_*`,
  `TREC_*`, `TASK_*` names through its symbol table.
- File and SQLite use a **one-outstanding-request** request/reply protocol; the
  host drains them between turns.

### What was test-only vs. missing
- Genuinely implemented: the RAW context switch, round-robin loop, GC rooting,
  stack sentry (test-only driver).
- Missing for ordinary use: a loadable vocabulary, more than three tasks,
  voluntary I/O waits, completion/failure/cancellation policy, and a host pump
  that interleaves tasks with async I/O.

## 2. What was implemented

A loadable cooperative-multitasking capability, reusing the M1 mechanism and
adding no OS threads.

- **`modules/task.glon`** — the vocabulary, built from the same RAW fragments:
  `spawn`, `yield`, `run-tasks`, `self`, `task-wait`, `task-wake`,
  `task-cancel`, `task-state`, `tasks/pending?`, `tasks/error`, `repeat`, plus
  the `task-run`/`task-body` entry. Layout constants resolve symbolically, so
  the file is profile-independent.
- **`m1_layout.h`** — `TREC_BLOCKED` (record cell 9): a suspended-on-I/O flag.
  The state stays `TASK_RUNNABLE` so the collector still traces the task's saved
  stacks/closures; the scheduler skips blocked tasks.
- **`r0_s1_runtime.c`** — exposed the numeric `M1_MAX_TASKS`, `TASK_*`,
  `TREC_BLOCKED`, `M1_STRESS_CNT` constants to the assembler (names only; no
  scheduling semantics added).
- **`glon.c`** — `GLON_MODULE_TASK`: loads `task.glon` when a source references
  it, and a host **task pump** (`pump_tasks`) that alternates `run-tasks` with
  the async file/SQLite drains until nothing progresses. `drain_io` now always
  reports progress (previously only when a progress script was supplied).
- **`Makefile`** — `MODULES=task` with configurable, bounded capacity
  (`M1_TASKS=10`, `M1_TASK_CELLS=1000`, `M1_DS_OFF=200`, `M1_RS_OFF=1000`,
  `M1_WRAPPER_DELTA=256`; defaults fit ten tasks in the 10,200-cell arena), and
  an `m2-test` target.

### How waiting on I/O works
A task calls `file/read …` (one outstanding host request), then `task-wait`
(marks itself blocked and yields). `run-tasks` returns when no task is runnable;
the host pump drains the file request, whose reply callback calls `task-wake`
on the task handle; the pump re-enters `run-tasks`, which resumes the task after
`task-wait`. Other runnable tasks make progress meanwhile.

### Failure isolation and cancellation
`spawn` makes the task entry `task-run`, which runs the body under `judge` and
stores the result (or the caught `SIN!`) in `tasks/error`; a raising task
finishes instead of corrupting the scheduler. `task-cancel` marks a task
finished without resuming it.

## 3. Demonstrated behaviour (examples/m2, tests/m2)

- `tasks.glon`: ten tasks, each yields three times → `M2_TASKS_OK count=30`.
- `fairness.glon`: three tasks round-robin → `123123123`.
- `failure.glon`: one task raises; the peer runs and the `SIN!` is captured →
  `M2_FAIL_ISO_OK`.
- `cancel.glon`: a cancelled task never runs; the peer runs → `M2_CANCEL_OK`.
- `io-wait.glon`: a task reads a file asynchronously, suspends, is resumed by
  the reply callback → `M2_IO_WAIT_OK hello-m2`.
- Capacity: an 11th spawn (capacity 10) returns a negative handle →
  `M2_CAP_OK`.
- `stress.glon`: ten tasks × five yields = 50 switches.

All are executed by `tests/m2/acceptance.py` (Python drives the CLI only).

## 4. Measurements

Configuration: `MODULES=file,task`, `MEM_PROFILE=large`,
`MEM_LOADER_CELLS=500000`, ten task slots.

- Peak resident memory (M2 suite): **4,248 KiB**.
- Context switches: 50 in **0.06 s** (cooperative; not simultaneous).
- Executable size: **196,848 bytes** (task-only), **259,032 bytes** (file+task).
- Existing M1 C stress: 1,000 switches, clean stacks (`make m1-smoke`).

## 5. Tests executed

Passed:
- `./check-frozen-s1.sh` (S1 unchanged).
- `make m1-smoke` (all M1 A–K checks, 1,000-switch stress).
- `make io-file-c-test`, `make io-file-test` (39/39; validates the `drain_io`
  change).
- `make sqlite-test`, `tests/sqlite-g3d/acceptance.py`,
  `tests/sqlite-g3d/http_sqlite_test.py`.
- `tests/https-g1`, `tests/https-g2`, `tests/https-g3` acceptance.
- `tests/https-g3e/acceptance.py`, `tests/https-g3f/acceptance.py`.
- `tests/m2/acceptance.py` (all seven groups).

Not run: the full `make test` monolith (the relevant M1/HTTPS/SQLite/file
suites above were run individually under the 1 GB budget).

## 6. Limitations and findings

- **Per-task stack budget.** Ten tasks share the 10,200-cell arena, so each gets
  ~1,000 cells (200 DS + 800 RS). Recursion is shallow (about five `repeat`
  levels). The split is configurable (`M1_TASK_CELLS`/`M1_DS_OFF`/`M1_RS_OFF`),
  but the arena size is fixed by the memory map. The frozen 3-task conformance
  build keeps the original 3,400-cell tasks.
- **One outstanding async request.** The file/SQLite modules allow a single
  pending request, so two tasks reading at once make the second raise `'busy`.
  A task-level wait queue would need a host change; not attempted.
- **`do` on a reduce-built block halts** (a pre-existing runtime limitation
  discovered here: `do` works on literal blocks but fail-stops on `reduce`
  results, reason 0). `task.glon` works around it by storing the body in the
  GC-traced wrapper and entering through `task-run`/`judge` instead of `do`.
- **Shared `tasks/error`.** It holds the last finisher's result; per-task result
  slots would need a record field the collector traces (future work).
- **Cancellation is fire-and-forget.** `task-cancel` prevents a resume but does
  not unwind the task's saved stacks (they are reclaimed when the slot is
  reused).

## 7. What HTTPS-as-tasks would additionally require

Not attempted (out of scope). Representing HTTPS requests as cooperative tasks
would need:
1. A host-side pending-connection table and a **deferred-response** path —
   CivetWeb's `begin_request` is synchronous and has no "pending" return.
2. A Glon-visible bounded queue of request descriptors.
3. A host pump like `pump_tasks` that drives `run-tasks` together with socket
   I/O, plus per-request task handles and cancellation on client disconnect.
4. The G3F evaluator-ownership mutex (already present) to keep the single
   interpreter serialised.

## Reproduce

```
make m2-test
```

Builds the large-profile `file,task` CLI and runs the bounded cooperative-task
suite. The frozen 64K conformance build is unaffected (it does not select
`MODULES=task`).