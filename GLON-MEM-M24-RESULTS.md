# GLON-MEM-M2.4 — Isolate Full-Application Startup Failure

Date: 2026-11-26
Branch: `experiment/glon-mem-profile-m24`
Baseline: G3J/MEM-M2.3, commit `af95628`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.
**Diagnosis only — no runtime/app change.**

## 1. Baseline

- Branch from `af95628`; G3J.6 diagnostics + `H3B-RESULTS.md` preserved.
- Expanded profile: `MEM_CODE_CAP=20480`, `GLOBAL_CTX_CAP=512`,
  `CODE_RESERVE=128`, M1 3×3400, `MEM_LOADER_CELLS=500000`.

## 2. Bounded incremental loading (expanded profile)

| Step | Result |
|---|---|
| `[ s/print … ]` (prelude+strings) | **hi=5** (works) |
| + `web.glon` + `json.glon` | **hi=5** (works) |
| nested installers + web + json | **nested-ok** (works) |
| G3H app (`server.glon`) + web/json, **non-task** build | **HTTP 200, listening** (works) |
| G3H app + web/json, **task** build (`--http-tasks` + `task.glon`) | **machine halt** at `app/install` |

## 3. First failing component

**Task mode** is the first failing transition. The identical G3H application +
libraries load and serve correctly under the expanded profile in **non-task**
mode (200), but halt at `app/install` when `task.glon` is loaded and
`--http-tasks` is used. So the layout, libraries and application are **not** the
cause; the failure is introduced by the **M1 task path** under the expanded
profile.

Failure (task mode):
```
[dump] IP=11971 SP=20480 RP=28670 HP=42944 top=0
glon: .../server.glon: ** halted (reason 0) [word=app/install ctx=…]
```
(SP=20480=DS_INIT is normal — non-task and small programs run with it.)

## 4. Comparison with default profile

- Non-task expanded: G3H → 200 (works).
- Task expanded: G3H → halt.
- Default (unshifted) task build + G3H: fails on the pre-existing global-context
  `context_full` (G3J.5), a separate issue.
- The task-mode failure is **introduced by the expanded profile** (non-task works;
task halts), pointing at the M1 arena/task-table addresses under the shift.

## 5. Evidence / next (proposal only)

- `M1_TASK_TABLE` (65000) and `M1_ARENA_BASE` (54800) are `-D` overridable
  (m1_layout.h `#ifndef`); the expanded build passes shifted values. The task
  record/arena layout and the RAW task-creation code must be checked for a stale
  address under the shift (the first failing operation is the task spawn/setup in
  `mnew-task`/`task-run`).
- **Proposed smallest correction (not implemented):** verify/repair the M1
  arena/task-table address used by the task path under the expanded profile so it
  matches the generated layout.

## 6. Regression

- `./check-frozen-s1.sh` — PASS.
- Default build — PASS.
- No other tests run.

## 7. Status

- Incremental loading — DONE (first failing transition = task mode).
- Root cause — isolated to the M1 task path under the expanded profile (evidence:
non-task 200 vs task halt).
- Correction — **not implemented** (proposed above).
- Frozen S1 / default — PASS.

## 8. Commit

Branch `experiment/glon-mem-profile-m24`, one local commit. Not pushed/merged.