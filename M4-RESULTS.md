# M4 — Reusable tasks and reliable lifecycle management: results

Date: 2026-11-25
Branch: `experiment/glon-m4`
Baseline: M3, commit `9060662`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Audit: why slots were not reused

- `mnew-task` (in `modules/task.glon`) only reused a slot whose state was
  `TASK_EMPTY`. `task-finish` and `task-cancel` set `TASK_FINISHED`, which was
  never reused, so after `M1_MAX_TASKS` spawns the table stayed full forever.
- Handles were `record * 16` with **no generation**, so a recycled slot could
  not be distinguished from the task that previously owned it.
- Resources: a task owns its **record slot** (16 cells: saved IP/SP/RP, evaluator
  state, state, blocked, generation) and its **private DS/RS arena** region; its
  **wrapper block** (at `record + M1_WRAPPER_DELTA`) holds the body. Its **I/O
  requests** are separate records in the file/SQLite module queues, holding the
  success/failure closures; those closures capture the task's handle and context.
- A slot can be reused once the task is no longer runnable (finished/failed/
  cancelled) and its saved state is no longer needed. Pending I/O closures may
  outlive the task, so they must be made safe by a generation check, not by
  waiting for them.

## 2. Implementation

Minimal changes; no new scheduler, no OS threads, no frozen-S1 change.

- **`m1_layout.h`**: added `TREC_GEN` (record cell 10) — a per-slot reuse
  generation.
- **`r0_s1_runtime.c`**: exposed `TREC_GEN` to the assembler and seeded it to 0
  for every slot at init (names/data only; no scheduling semantics).
- **`glon.c`**: seed `TREC_GEN = 0` alongside `TREC_STATE = EMPTY`.
- **`modules/task.glon`**:
  - `mnew-task` now reuses a slot that is **EMPTY or FINISHED**, bumps its
    generation, and returns a **generation-tagged handle**
    `((gen * 16) + slot) * 16` (a tagged Glon integer).
  - `self` builds the same tagged handle from the current record.
  - `task-wake`, `task-cancel`, `task-state` decode `(slot, gen)` and **only act
    if the record's current generation matches**; a stale handle is ignored
    (`task-state` returns -1). A late I/O reply to a retired task therefore
    cannot resume or modify the new task that inherited the slot.
  - A finished/failed/cancelled task's slot becomes reusable immediately; the
    generation protects against stale references.
- `examples/m2/cancel.glon`: updated to read the cancelled task's state **before**
  a peer recycles its slot (the old handle is now correctly stale after reuse).

Task creation is now bounded by the number of **simultaneously active** slots
(`M1_MAX_TASKS`), not by the total number of tasks ever created.

## 3. Demonstration (examples/m4, tests/m4)

- `recycle.glon`: a driver task spawns its own replacement and finishes; 50 tasks
  are created/completed through one recycled slot.
- `generation.glon`: a stale handle cannot wake the task that inherited the slot.
- `endurance.glon`: 10,000 task creations/completions with one active slot.
- Acceptance also covers: 5 batches of 10 tasks (50 total) reusing slots; a late
  file reply to a retired task discarded safely; failure isolation across
  recycling; and peak RSS.

## 4. Measurements

Configuration: `MODULES=file,task`, `MEM_PROFILE=large`,
`MEM_LOADER_CELLS=500000`, `M1_MAX_TASKS=10`.

- **10,000 task creations/completions in 4.29 s** (single active slot).
- Peak resident memory (M4 suite): **6,244 KiB**; the endurance run alone peaked
  at **4,188 KiB** and stayed flat (the collector reclaims finished tasks).
- Executable size: **259,072 bytes** (~253 KiB).
- Maximum simultaneously active slots tested: 10 (the build's `M1_MAX_TASKS`);
  the endurance chain keeps 1 active.

## 5. Tests executed

Passed:
- `./check-frozen-s1.sh` (S1 unchanged).
- `make m1-smoke` (M1 A–K, 1,000-switch stress).
- `make m2-test` (all M2 groups, with the updated cancel example).
- `make m3-test` (all M3 groups).
- `make io-file-test` (39/39).
- `make sqlite-test`, `tests/sqlite-g3d/acceptance.py`,
  `tests/sqlite-g3d/http_sqlite_test.py`.
- `tests/https-g1/acceptance.py`, `tests/https-g3e/acceptance.py`.
- `tests/m4/acceptance.py` (all six groups, including 10,000 tasks).

Not run: the full `make test` monolith (the relevant M1/M2/M3/file/SQLite/HTTPS
suites were run individually under the 1 GB budget).

## 6. Implemented vs. proposed, and limitations

Implemented:
- Slot recycling for finished/failed/cancelled tasks; generation-tagged handles;
  stale-wake suppression; safe cancellation with pending file I/O; bounded active
  capacity with unlimited total creations; bounded memory (GC reclaims finished
  tasks).

Limitations / notes:
- **Host I/O is still serialised** (M3); recycling is about task slots, not
  parallel I/O.
- **Per-task stacks remain ~1,000 cells** (ten tasks share the arena), so task
  recursion is shallow.
- **Retired tasks' saved stacks are not explicitly scrubbed**; they are
  overwritten when the slot is reused and reclaimed by the collector. A
  generation bump makes any stale reference harmless.
- **Cancellation is cooperative**: a cancelled task stops at its next scheduling
  point; it cannot be preempted mid-statement. Pending I/O for a cancelled task
  still runs to completion, but its reply is discarded by the generation check.

## Reproduce

```
make m4-test
```

Builds the large-profile `file,task` CLI and runs the bounded recycling/endurance
suite. The frozen 64K conformance build is unaffected (it does not select
`MODULES=task`).