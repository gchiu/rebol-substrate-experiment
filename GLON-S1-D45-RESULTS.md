# GLON-S1-D4.5 — Trace Saved Return Addresses (checkpoint)

Date: 2026-11-26
Branch: `experiment/glon-s1-d45`
Baseline: G3J/S1-D4.4, commit `6083361`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.
**Saved return IP captured; exact divergence not isolated; no fix applied.**

## 1. Baseline

- Branch `experiment/glon-s1-d45` from `6083361`. G3J.6 diagnostics + `H3B-RESULTS.md` preserved.

## 2. Extended trace (saved return IP)

- `HOST_TRACE_FRAME` now carries a 4th value, **retip** = `[frame + FRAME_IP]`
  (FRAME_IP=4, r0_s1.h:474), read at ACTIVATE and RETURN; CALL/RESTORE pass 0.
- s1.c dispatch pops tag/retip/site/frame; ring buffer + dump print `ret=`.

## 3. Captured (G3H task, expanded)

```
n=17: {t=1 f=0 s=121 ret=0} {t=1 f=0 s=121 ret=0}
 {t=3 f=28576 s=134 ret=0} {t=4 f=0 s=134 ret=0}
 {t=3 f=28576 s=134 ret=12400} {t=4 f=0 s=134 ret=0}
 {t=1 f=0 s=134} ×6
 {t=3 f=28576 s=190 ret=12400} {t=4 f=0 s=190} ×2
… executed-ip=12032 tag=3516 (emit_return, RV_FRAME==0)
```

- Closures ACTIVATE with **ret=12400** (the saved return address) and RESTORE to
  f=0. The failing RETURN (3516) fires at f=0 (top-level `app/install`).
- **Discrepancy:** the RETURN (tag 2) record at `emit_return` start is **not
captured** before the 3516 halt — the trace at 3512 was not reached, so the
failing return's own retip is not recorded.

## 4. Destination / root cause (narrowed)

- The closures' return address is 12400 (valid, in the caller). The failing
  RETURN is the top-level `app/install` return to the global (f=0); the guard
  fires. Non-task runs the same app without hitting it → the divergence is the
  top-level return path in the task build.
- The exact instruction at the restored address and why the task build's top-level
  return reaches f=0 (while non-task doesn't) is **not isolated** — the tag-2
  RETURN record is missing, so the failing return's own saved IP is not captured.

## 5. Correction

**None applied** (the exact divergence is not demonstrated). No guard bypass; no
artificial frames; no redesign.

## 6. Verification

- Saved-return-IP trace — DONE (closures ret=12400).
- Nested calls → 42 (earlier) — PASS.
- G3H non-task — PASS (200, earlier). G3H task — FAILS (3516, top-level return to f=0).
- Default build — PASS. Frozen check — fails (s1.c/r0_s1_runtime.c modified, permitted).

## 7. Remaining uncertainty / next

- Why the tag-2 RETURN trace at `emit_return` (3512) is not reached before the
  3516 halt (the trace reads `[RV_FRAME+FRAME_IP]` = M[4] when RV_FRAME=0 — it
  may fault or be skipped). Capturing the failing return's own retip (e.g., guard
  the trace read for RV_FRAME==0) would name the destination. That is the direct
  continuation, within the tracing scope.

## 8. Commit

Branch `experiment/glon-s1-d45`, one local commit (checkpoint). Not pushed/merged.