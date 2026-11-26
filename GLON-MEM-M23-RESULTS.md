# GLON-MEM-M2.3 — Diagnose Relocated Memory Layout

Date: 2026-11-26
Branch: `experiment/glon-mem-profile-m23`
Baseline: G3J/MEM-M2.2, commit `fa9e0cd`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.

## 1. Exact failure (minimal reproducer)

Expanded profile (`MEM_CODE_CAP=20480`, `GLOBAL_CTX_CAP=512`, `CODE_RESERVE=128`,
M1 3×3400, `MEM_LOADER_CELLS=500000`):

| Program | Expanded | Default |
|---|---|---|
| `[ s/print s/+ "hi=" s/from-int + 2 3 ]` | **hi=5** (works) | (needs strings-ext; `MODULES=` halt is unbound `s/from-int`, not layout) |
| nested installers (`app/install → setup-a → setup-b → print`) | **nested-ok** (works) | nested-ok (works) |
| G3H HTTP app (`examples/glon-worlds-g3b/server.glon`) + web/json | **machine halt** at `app/install` | (default task build fails on pre-existing `context_full`) |

G3H expanded halt:
```
[dump] IP=11971 SP=20480 RP=28670 HP=42944 top=0
glon: .../server.glon: ** halted (reason 0: no SIN!: a machine-level fail-stop)
  [word=435 name=app/install ctx=1125447]
  [blk=1158464 count=9 idx=-2]   (top-level block: gw/users-db, handle, app/install ...)
```

**SP=20480 = DS_INIT is normal** (empty data stack) — the tiny and nested
scripts run fine with the same SP, so this is **not** a stack error (per the task).

## 2. Relocated-address audit

- Generated `s1_profile.c`: `DS_INIT 20480`, `RS_INIT 28672`, `HEAP_BASE 36864`
  (sed-rewritten). `r0_s1.h` receives `-DR0S1_DS_INIT=20480`,
  `-DR0S1_RS_INIT=28672`, `-DGC_HEAP_BASE=36864`, `-DR0S1_HEAP_BASE`,
  `-DR0S1_LOAD_BASE`, `-DM1_ARENA_BASE`, `-DM1_TASK_TABLE` (all `#ifndef`
  guarded). The generated profile and C runtime **agree** on every address.
- No fixed numeric stack addresses in RAW/machine code beyond the M1/loader bases
  (already `-D`). The sentry uses `R0S1_DS_INIT`/`R0S1_RS_INIT` (now shifted).
- No stale address found in the constants.

## 3. Correction

No single stale address was demonstrated: small programs (including nested
installers) run correctly under the expanded profile, so the layout is not
inherently broken. The failure is specific to the **full HTTP app with all
modules loaded** halting at `app/install`. No safe one-line correction was
demonstrated, so per the task I stop with the reproducer rather than guess.

## 4. Verification

- Minimal reproducer (tiny + nested) under expanded — PASS.
- G3H HTTP app under expanded — FAILS (machine halt at app/install).
- Frozen S1 — PASS; default build — PASS.

## 5. Interpretation / next

The expanded layout is valid for small programs; the blocker is the full app +
libs halting at `app/install`. The next step is to isolate whether it is the
global-context occupancy (512 with http+json+sqlite+sec+web+app), the loader
footprint, or an app-startup interaction — with a bounded probe, not a redesign.

## 6. Status

- Failure reproduced minimally — DONE.
- Address audit (no stale constant) — DONE.
- Single demonstrated correction — **NOT FOUND** (blocker isolated to full-app
  startup, not the raw layout).
- Frozen S1 / default build — PASS.

## 7. Commit

Branch `experiment/glon-mem-profile-m23`, one local commit. Not pushed/merged.