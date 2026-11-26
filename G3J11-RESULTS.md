# G3J.11 — Restore Task-Mode Loading for Authentication (blocked: code limit)

Date: 2026-11-26
Branch: `experiment/glon-http-g3j11`
Baseline: G3J.10, commit `94f9a01`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.

## 1. Memory-layout comparison

- Code limit: `R0S1_CODE_LIMIT = R0S1_DS_INIT - R0S1_DS_RESERVE` = **16128**
  (r0_s1.h:255) — a fixed memory-map constant, identical for task and non-task
  builds. The S1 code (assembled libs + app) must fit below it.
- M1 task table/arena: `M1_TASK_TABLE=65000`, `M1_ARENA_BASE=54800`,
  `M1_TASK_CELLS=3400`, `M1_DS_OFF=400`, `M1_RS_OFF=3400` (m1_layout.h) — these
  place the M1 arena **above** the code/loader; they do **not** change the code
  limit.
- Loader: grows up to `M1_ARENA_BASE` (54800); `MEM_LOADER_CELLS=500000` is the
  loader heap. Not the constraint here.
- Managed heap: large profile (1,048,576 cells). Not the constraint.

**Why json.glon needs 31 cells with 14 free:** the task build links `task.glon`
(extra assembled S1 code). After the core libs (prelude/strings/strings-ext/
file/file-stream/http/sqlite/**task**) the code frontier is at 16114 (14 free
below 16128). `json.glon` assembles to 31 cells → `masm code space exhausted …
too_large`. The non-task build omits `task.glon`, leaving room for `json.glon`.

## 2. Build-configuration options

- The code limit (16128) is a **memory-map constant** (`R0S1_DS_INIT`/
`R0S1_DS_RESERVE`), not a build parameter. No supported `M1_*`/`MEM_*` option
  raises it; the M1 arena sits above the code region, so shrinking stacks/tasks
  does not add code space.
- Verified: task build **without** `sec` also fails to load `json.glon`
  (14 free, needs 31) — so `sec` is not the tipping point; `task.glon` is.
- Per the task, I did **not** change `R0S1_CODE_LIMIT` (that would be an
  unapproved memory-map/capacity change overlapping the code region).

## 3. Acceptance

- Build: task build succeeds, but **loading `modules/json.glon` fails**
  (`too_large`) → the app never reaches `http/listen`.
- One setup request: **not run** (server does not start).
- Earliest incomplete operation: **parsing/assembling `modules/json.glon`** under
  the task build's code limit.

## 4. Status vs criteria

- Compare memory layouts — DONE.
- Find a build param for more code space — **NONE** (code limit is a memory-map
  constant; task.glon's code is the tipping point).
- Load modules / reach listen / run setup — **BLOCKED** (json.glon too_large).
- Frozen S1 / runtime / loader / capacity / auth design — unchanged.

## 5. Conclusion / recommendation

The existing task configuration **cannot** load the minimal authentication app:
the fixed 16128-cell code limit cannot hold `task.glon` + `json.glon` (14 free,
31 needed). Enabling task mode for this app requires either:
- an **approved** increase of the code region (`R0S1_DS_INIT`/memory map) — a
  scaling experiment, not an incidental fix, or
- reducing the loaded code (e.g., a smaller JSON), which is out of scope.

Per the task, I stop at this established blocker without changing the memory map.

## 6. Reproduce

```
make glon MODULES=file,http,sqlite,task MEM_PROFILE=large MEM_LOADER_CELLS=500000 \
     M1_TASKS=3 M1_TASK_CELLS=3400 M1_DS_OFF=400 M1_RS_OFF=3400 M1_WRAPPER_DELTA=256
./glon --http-tasks ... modules/json.glon app.glon
# -> r0_s1: masm code space exhausted ... code limit 16128 ... json.glon too_large
# (non-task build MODULES=file,http,sqlite,sec loads json.glon fine)
```

## 7. Commit

Branch `experiment/glon-http-g3j11`, one local commit. Not pushed/merged/deployed.