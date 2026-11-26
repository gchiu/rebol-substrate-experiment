# GLON-MEM-M2.2 — Validate Expanded Memory Profile

Date: 2026-11-26
Branch: `experiment/glon-mem-profile-m22`
Baseline: G3J/MEM-M2.1, commit `524ce6f`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.

## 1. Baseline preserved

- Branch `experiment/glon-mem-profile-m22` from `524ce6f`; `H3B-RESULTS.md` and
  G3J.6 diagnostics preserved. No reset/clean.

## 2. Expanded profile (generated, non-overlapping by construction)

`MEM_CODE_CAP=20480`, `MEM_GLOBAL_CONTEXT_CAP=512`, `CODE_RESERVE=128`,
`M1_TASKS=3 M1_TASK_CELLS=3400 M1_DS_OFF=400 M1_RS_OFF=3400 M1_WRAPPER_DELTA=256`,
`MEM_LOADER_CELLS=500000`. Generated `s1_profile.c`: `DS_INIT 20480`,
`RS_INIT 28672`, `HEAP_BASE 36864`; `MEM_CELLS 1,637,200`. Regions are laid out
sequentially (each base = previous end + size) → no overlaps.

## 3. Startup comparisons (all with `--http-tasks`, expanded profile)

| App | Modules | Result |
|---|---|---|
| A. G3H user registry (`examples/glon-worlds-g3b/server.glon`) | web,json | **machine halt** (reason 0) at `app/install`, SP=20480 (=DS_INIT) |
| B. H3C task app (`examples/h2b/tasks.glon`) | web,json | **machine halt** (reason 0) |
| C. G3J auth (`tests/g3j/min_auth_task.glon`) | web,json | **machine halt** (reason 0) |

Exact A diagnostic:
```
[dump] IP=11971 SP=20480 RP=28670 HP=42944 top=0
glon: .../server.glon: ** halted (reason 0: no SIN!: a machine-level fail-stop)
  [word=435 name=app/install ctx=1125447]
  [blk=1158464 count=9 idx=-2]  [0] gw/users-db (tag 3)
```

All three fail at startup with the same machine fail-stop; SP sits at `DS_INIT`
(20480, the data-stack top).

## 4. Interpretation

**A, B and C all fail** under the expanded profile → per the task, the **generated
memory layout** is the focus. The common signature (machine halt at
`app/install`, SP=DS_INIT) indicates the shift of `DS_INIT`/data-stack boundary is
not consistent with the runtime's data/return-stack handling (the sentry accepts
SP=DS_INIT, but execution then fail-stops). This is a layout defect, not an
authentication problem.

Note: the default (unshifted) task build + G3H also fails (G3J.5 global-context
`context_full`), which is a separate pre-existing capacity issue.

## 5. Regression

- `./check-frozen-s1.sh` — PASS (frozen S1 unchanged).
- Default build (`make glon MODULES= MEM_PROFILE=large`) — PASS.
- No other tests run (not needed to interpret the failure).

## 6. Recommendation (next step)

Investigate the generated layout's data/return-stack boundary: the shift moves
`DS_INIT` but the runtime fail-stops at SP=DS_INIT. The next milestone should
determine the correct stack-boundary handling for a relocated `DS_INIT` (a
layout/runtime-consistency issue), not authentication.

## 7. Status

- Expanded profile built and regions non-overlapping — DONE.
- A/B/C startup comparison — DONE (all fail).
- Root cause — **not established** (layout/runtime boundary; evidence above).
- Frozen S1 / default build — PASS.

## 8. Commit

Branch `experiment/glon-mem-profile-m22`, one local commit. Not pushed/merged.