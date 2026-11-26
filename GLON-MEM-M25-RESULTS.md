# GLON-MEM-M2.5 — Diagnose Task-Mode Memory Relocation

Date: 2026-11-26
Branch: `experiment/glon-mem-profile-m25`
Baseline: G3J/MEM-M2.4, commit `91ffb61`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.
**Diagnosis only — no runtime/app change.**

## 1. Configured vs runtime layout (expanded profile)

From the build flags (`make -n`): `R0S1_DS_INIT=20480`, `R0S1_RS_INIT=28672`,
`M1_ARENA_BASE=1625440`, `M1_TASK_TABLE=1635640`, `M1_TASK_REC_SIZE=16`. The
generated `s1_profile.c` (`DS_INIT 20480/RS_INIT 28672/HEAP_BASE 36864`) and
`r0_s1.h` (`-D`) agree. The M1 arena/table/task-rec sizes are `-D` overridable
(m1_layout.h `#ifndef`). No inconsistency found in the addresses.

## 2. Task execution isolation (expanded profile)

| Program | Expanded | Default |
|---|---|---|
| `spawn [ s/print "task-ok" ]` + wake + run-tasks | **task-ok** (works) | task-ok |
| `spawn [ print "before" task-wait print "after" ]` + wake | **before / after** (suspend+resume works) | — |

So the **M1 task path and its addresses work** under the expanded profile
(spawn, suspend, resume all correct).

## 3. Earliest divergence

- The previously failing G3H **task-mode server** halts at **`app/install`** —
  the **top-level installer**, which runs at load **before any task is spawned**
  (and before `http/listen`). It is **not** inside a task.
- The same app + libs run correctly in **non-task** mode under the expanded
  profile (HTTP 200, G3J.2.4).
- So the divergence is the **full application + all modules** starting under the
  task build, at the installer — not a task-execution, address or scheduler
  defect (tiny tasks and the non-task app both work).

## 4. Correction

No specific address or initialisation defect was demonstrated: the M1 addresses
are consistent and tiny tasks run. The blocker is the full app + libs (http+json+
sqlite+sec+task+web) at `app/install` under the task build — most plausibly the
fixed **symbol table (512)** / **global context (512)** / loader footprint with
all modules loaded, not the task path. Per the task, I stop with the reproducer
rather than change a capacity or the app.

## 5. Regression

- `./check-frozen-s1.sh` — PASS.
- Default build — PASS.
- Tiny task spawn + suspend/resume under expanded — PASS.

## 6. Status / recommendation

- Task path/addresses verified consistent (tiny tasks work) — DONE.
- First divergence isolated to the full app + libs at `app/install` (not a task) —
  DONE.
- Root cause — **uncertain** (not a task/address defect); minimal reproducer:
  G3H task build halts at app/install while tiny tasks and the non-task app work.
- Recommendation: investigate the full-application startup with all modules
  (symbol table 512 / global context 512 / loader) in a separate milestone; do
  not change task semantics or capacities here.

## 7. Commit

Branch `experiment/glon-mem-profile-m25`, one local commit. Not pushed/merged.