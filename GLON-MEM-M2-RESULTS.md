# GLON-MEM-M2 — Generated Application-Specific Memory Profiles

Date: 2026-11-26
Branch: `experiment/glon-mem-profile-m2`
Baseline: G3I/MEM-M1, commit `f5142f5`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.

## 1. Mechanism (build-time, default preserved)

- `Makefile`: added `MEM_CODE_CAP ?= 16384` (the initial data-stack top / code
  ceiling). For the `large` profile it derives `MEM_SHIFT = MEM_CODE_CAP-16384`
  and shifts the fixed bases with checked `expr` arithmetic:
  `RS_INIT=24576+shift`, `HEAP_BASE=32768+shift`, then managed end / load base /
  M1 arena / task table / `MEM_TOTAL` cascade. `GLON_MEMORY_CFLAGS` passes
  `-DR0S1_DS_INIT`, `-DR0S1_RS_INIT`, `-DGC_HEAP_BASE` (plus the existing
  `R0S1_HEAP_BASE`/`R0S1_LOAD_BASE`/`M1_*`).
- `build/profile/s1_profile.c` is generated from frozen `s1.c` by `sed`; the rule
  now also rewrites `DS_INIT`/`RS_INIT`/`HEAP_BASE` to the shifted values. **The
  frozen `s1.c`/`s1.h` are not modified.**
- `r0_s1.h`: `GC_HEAP_BASE`, `R0S1_DS_INIT`, `R0S1_RS_INIT` made `#ifndef`
  overridable (default unchanged); `R0S1_DS_RESERVE` already overridable (G3J.12).
- Default (`MEM_CODE_CAP=16384`, shift 0) reproduces the original layout; the
  default build still compiles.

## 2. Generated layout (verified, MEM_CODE_CAP=20480, CODE_RESERVE=128)

`build/profile/s1_profile.c`: `DS_INIT 20480`, `RS_INIT 28672`, `HEAP_BASE 36864`;
`MEM_CELLS 1637200`. Non-overlapping by construction (each base = previous end +
size; total = task table + 536 + 1024). The code ceiling rose 16128 → 20352.

## 4. Result (G3J auth task app)

- Code/libraries load **without code-space exhaustion** (json.glon's 521 cells
  now fit; the stack-sentry mismatch is resolved by passing `R0S1_RS_INIT`).
- The app reaches the point of loading all modules, but the server does **not**
  reach `http/listen`: it fails with **`resource error: context_full`**.
- One setup request: **not run** (server does not listen).

## 5. New independent blocker

`context_full` — the **global context cap is 256** (`r0_s1_runtime.c:3872`,
`make_context(R0_NONE, 256)`), a fixed constant. With `http`+`json`+`sqlite`+
`sec`+`task`+`file` libs + the app, the global bindings exceed 256. This is a
**separate capacity constant**, not the code-space region. Raising it is a
capacity change requiring approval (per AGENTS.md), so per the task I stop and
report it rather than change it here.

## 6. Regression

- `./check-frozen-s1.sh` — PASS (frozen S1 unchanged).
- Default build (`make glon MODULES= MEM_PROFILE=large`) — PASS.
- Debugger/M1/H3C/G3H — not re-run in this milestone (build/loader change only;
  the default layout is unchanged).

## 9. Status

- Generated app-specific profile mechanism — DONE (Makefile + profile sed + #ifndef).
- Default constrained/large layout preserved — DONE.
- Code-space exhaustion resolved for the auth app — DONE.
- App reaches `http/listen` — **BLOCKED** by `context_full` (global ctx 256).
- Frozen S1 / runtime semantics / auth logic — unchanged.

## 10. Smallest safe next step

Raise the global-context capacity (256 → e.g. 512) as an approved, bounded
change (build-time constant, default preserved), then re-test `min_auth_task`
reaching `http/listen`. That is the single remaining blocker for this milestone.

## 11. Commit

Branch `experiment/glon-mem-profile-m2`, one local commit. Not pushed/merged.