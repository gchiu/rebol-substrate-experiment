# GLON-MEM-M2.6 — Differential Diagnosis of app/install

Date: 2026-11-26
Branch: `experiment/glon-mem-profile-m26`
Baseline: G3J/MEM-M2.5, commit `4b0ab04`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.
**Diagnosis only — no fix, no runtime/app change.**

## 1. Baseline preserved

Branch from `4b0ab04`; G3J.6 diagnostics + `H3B-RESULTS.md` preserved. No
reset/clean/merge/push.

## 2. Controlled comparison (identical source, expanded profile)

Same `examples/glon-worlds-g3b/server.glon`, `MEM_CODE_CAP=20480`,
`CODE_RESERVE=128`, `MEM_LOADER_CELLS=500000`, M1 3×3400. Module order:
`web.glon`, `json.glon` (explicit) + `file/http/sqlite/sec` (auto) + `task`
(auto, task build only).

| Build | Result |
|---|---|
| A. non-task (`MODULES=file,http,sqlite,sec`) | **HTTP 200, listening** |
| B. task (`MODULES=…,task`, `--http-tasks`) | **machine halt** at `app/install` |
| B with `GLOBAL_CTX_CAP=1024` | **machine halt** at `app/install` (unchanged) |

**Build difference:** B adds `task.glon` (mnew-task/spawn/yield/run-tasks/…,
~16 globals + symbols) and the M1 arena. A and B otherwise compile the same app
and libs. Execution path: A runs `[ http/dispatch … ]` synchronously; B spawns a
Glon task per request (`http/task-run`). The halt is at load, in `app/install`
(the top-level installer), **before any task is spawned** and before `http/listen`.

## 3. Debugger observations

- D2A (`r0_s1_dump_ip`/`r0_s1_task_dump`) at the halt:
  `[word=435 name=app/install ctx=1125447] [blk=1158464 count=9 idx=-2]`
  (top-level block: gw/users-db, handle, app/install …). SP=20480 (=DS_INIT),
  RP=28670. No task exists yet (D2A task dump is empty), so it cannot show task
  state.
- H2Z lookup-provenance does **not** fire: this is a machine fail-stop, not a
  lookup/escape SIN!.
- **What the existing debugger cannot observe:** it shows the halt-time word and
  block, but **not the cause** (whether the fail-stop is a context-creation,
  evaluation, capacity or memory-reference event inside `app/install`). There is
  no facility that dumps the failing activation's binding-lookup state at a
  machine halt.

## 4. First divergence

- The only build difference is `task.glon`. A (no task.glon) → 200; B (task.glon)
  → halt. Raising `GLOBAL_CTX_CAP` to 1024 does not help → **not** global-context
  capacity.
- The divergence is introduced by loading `task.glon` (extra symbols/globals and
  the M1 arena) and then running the G3H installer chain; the failure is at
  `app/install`, before any task runs.
- A minimal nested-installer script + `task.glon` runs fine (`nested-ok`), so the
  difference is the **size/depth of the G3H installer chain + libs + task.glon**,
  not task.glon alone.

## 5. Confidence / unresolved

- **Confident:** the divergence is the task build (`task.glon`), not global-context
  capacity, not a task-execution/address defect (tiny tasks and non-task app
  work), and not the raw memory layout (non-task works).
- **Unresolved:** the exact fail-stop cause inside `app/install` (context-creation
  vs evaluation vs a capacity other than global-ctx). The existing debugger shows
  the word/block but not the cause; no lookup-provenance is produced.

## 6. Recommended next action

Add a **narrow** diagnostic at the machine-halt path that reports the failing
activation's context count/cap and the current RV_CTX/RV_FRAME (or run the G3H
installer under a debugger that can single-step the load), to distinguish a
context-creation capacity failure from an evaluation error. Do **not** change
capacities or the app until that is observed.

## 7. Status

- Differential A/B captured — DONE.
- First divergence isolated to `task.glon` at `app/install` — DONE.
- Exact cause — **not captured** by existing tools (documented above).
- Frozen S1 / default build — PASS (M2.5).

## 8. Commit

Branch `experiment/glon-mem-profile-m26`, one local commit. Not pushed/merged.