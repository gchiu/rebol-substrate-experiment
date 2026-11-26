# GLON-S1-D4.3 — Trace Closure Activation and Fix the Frame Defect (checkpoint)

Date: 2026-11-26
Branch: `experiment/glon-s1-d43`
Baseline: G3J/S1-D4.2, commit `934f799`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.
**Full CALL/ACTIVATE/RETURN/RESTORE trace working; root cause narrowed, fix not applied.**

## 1. Baseline

- Branch `experiment/glon-s1-d43` from `934f799`. G3J.6 diagnostics + `H3B-RESULTS.md` preserved.

## 2. Extended tracing

- `HOST_TRACE_FRAME` tags: **CALL=1** (`emit_call_indirect`), **RETURN=2**
(`emit_return` start), **ACTIVATE=3** (closure prologue, after `RV_FRAME` is
set), **RESTORE=4** (closure return, after `RV_FRAME` is restored). Ring buffer
`http_trace[128]` + dump in `r0_s1_dump_ip`.

## 3. Correct behaviour (minimal nested program)

`[ f: func [x] [+ x 1]  g: func [y] [f y]  s/print g 41 ]` → **42**. Trace
lifecycle: **CALL (f=0) → ACTIVATE (f=28576) → RESTORE (f=0)**. The frame is
set on activation and restored on return — correct.

## 4. G3H task failure trace

```
n=17: {t=1 f=0 s=121} {t=1 f=0 s=121}
      {t=3 f=28576 s=134} {t=4 f=0 s=134}   (×2)
      {t=1 f=0 s=134} ×6
      {t=3 f=28576 s=190} {t=4 f=0 s=190}   (×2)
… executed-ip=12012 tag=3516 (emit_return, RV_FRAME==0)
```

- Closures **do** activate (f=28576) and restore (f=0) correctly.
- The final `app/install` RETURN (3516) fires with **f=0** — after the last
RESTORE set the frame back to the global (0).
- So the failing RETURN is the **top-level `app/install` returning to the global
frame (0)**: RESTORE sets `RV_FRAME=0`, then the RETURN guard (`RV_FRAME==0`)
fires.

## 5. Root cause (narrowed) / remaining uncertainty

- The closures are fine (ACTIVATE/RESTORE correct). The defect is the **top-level
function returning to the global frame (id 0)** hitting the `RV_FRAME==0` guard.
- The non-task build runs the same `app/install` without hitting the guard, so
the difference is the **top-level return path** in the task build (the app is
invoked via the task wrapper / a closure call, whose return restores to the
global 0, whereas the non-task top-level return path differs). The exact
call-path difference is the remaining uncertainty.
- No fix applied (the precise top-level return difference is not isolated);
guard not bypassed; no call-convention redesign.

## 6. Verification

- Runtime CALL/ACTIVATE/RETURN/RESTORE records — DONE.
- Nested calls (trace on) → 42 — PASS.
- G3H non-task — PASS (200, earlier). G3H task — FAILS (3516, top-level return to f=0).
- Default build — PASS. Frozen check — fails (r0_s1_runtime.c modified, permitted).

## 7. Next

Trace the top-level `app/install` call path in the task build vs non-task (where
the call is set up and what frame the return targets) to explain why the task
build's return reaches the global (0) guard. That is the direct continuation.

## 8. Commit

Branch `experiment/glon-s1-d43`, one local commit (checkpoint). Not pushed/merged.