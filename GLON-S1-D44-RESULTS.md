# GLON-S1-D4.4 — Diagnose and Correct the Unexpected RETURN (checkpoint)

Date: 2026-11-26
Branch: `experiment/glon-s1-d44`
Baseline: G3J/S1-D4.3, commit `8ea7875`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.
**Control-flow path identified; exact divergence not isolated; no fix applied.**

## 1. Baseline

- Branch `experiment/glon-s1-d44` from `8ea7875`. G3J.6 diagnostics + `H3B-RESULTS.md` preserved.

## 2. Intended call/return invariant

- A function CALL pushes a return frame and sets `RV_FRAME` to the new activation;
  the callee body runs; the epilogue RESTOREs `RV_FRAME` to the caller's frame and
  jumps to the saved return IP. A RETURN must have a live activation (`RV_FRAME != 0`).

## 3. Runtime trace (existing HOST_TRACE_FRAME)

- Minimal nested program → 42: `CALL(f=0) → ACTIVATE(f=28576) → RESTORE(f=0)`.
- G3H task (expanded), 17 records: repeated `CALL(f=0) → ACTIVATE(f=28576) →
  RESTORE(f=0)` for the installer closures, then the failing **RETURN (3516) with
  f=0** at `app/install`.
- So the closures activate/restore correctly; the failing RETURN is the
  **top-level `app/install` returning to the global frame (0)** after the last
  RESTORE.

## 4. First incorrect transition (as far as observed)

- The last RESTORE (closure return) sets `RV_FRAME=0` (the global). The
  immediately following `emit_return` (the top-level `app/install` return) sees
  `RV_FRAME==0` and hits the guard.
- The non-task build runs the same `app/install` without hitting the guard, so the
  divergence is in the **top-level return path** of the task build (the app is
  invoked via the task wrapper / a closure call whose return restores to the
  global 0). The current trace does not capture the saved return IP (FRAME_IP), so
  the precise instruction-level divergence (which call/return) is not isolated.

## 5. Correction

**None applied.** The control-flow path (top-level return to global 0) is
identified, but the exact task-vs-non-task divergence is not demonstrated at the
instruction level (the trace lacks the return-address). No guard bypass; no
artificial frames; no redesign.

## 6. Verification

- Runtime CALL/ACTIVATE/RESTORE records — DONE. Nested calls → 42 — PASS.
- G3H non-task — PASS (200, earlier). G3H task — FAILS (3516, top-level return to f=0).
- Default build — PASS. Frozen check — fails (r0_s1_runtime.c modified, permitted).

## 7. Remaining uncertainty / next

- The trace does not capture the **saved return IP** (FRAME_IP) or the return
  address on the return stack, so the exact call whose return targets the global
  (0) is not named. Extending HOST_TRACE_FRAME to also record FRAME_IP / the
  return-stack top would identify the specific call. That is the direct
  continuation (still within the frame-tracing scope), not a redesign.

## 8. Commit

Branch `experiment/glon-s1-d44`, one local commit (checkpoint). Not pushed/merged.