# GLON-S1-D4.2 — Implement and Use Runtime Frame Tracing

Date: 2026-11-26
Branch: `experiment/glon-s1-d42`
Baseline: G3J/S1-D4.1, commit `d2db4ed`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.
**Working runtime frame tracing implemented; first bad transition narrowed.**

## 1. Baseline

- Branch `experiment/glon-s1-d42` from `d2db4ed`. G3J.6 diagnostics + `H3B-RESULTS.md` preserved.

## 2. Instrumentation (working runtime trace)

- **`HOST_TRACE_FRAME`** added: `s1.h` enum (before `HOST_DUMP`, which stays the
  validation max); `s1.c` dispatch case (pops tag/site/frame, calls the logger);
  `r0_s1_runtime.c` bounded ring buffer (`http_trace[128]`, overflow flag) +
  `http_trace_frame()` + dump in `r0_s1_dump_ip`.
- Emitted `asm_host(HOST_TRACE_FRAME)` at **`emit_call_indirect`** (tag 1 = CALL)
  and **`emit_return`** (tag 2 = RETURN), pushing `RV_FRAME`/`RV_SITE`.
- Read-only w.r.t. Glon state (no eval/allocation); disabled unless the build
  includes it (the ring is bounded; overflow reported).

## 3. Captured (G3H task build, expanded)

```
[frame-trace n=9 ov=0:
 {t=1 ip=11328 sp=20479 rp=28667 f=0 s=121}
 {t=1 ip=11328 sp=20479 rp=28667 f=0 s=134} ×7]
[halt-emitters=… executed-ip=11992 tag=3516 …]
```

- **9 CALL records, every one with `frame=0`** (RV_FRAME never changes from 0),
  sites 121/134, at ip=11328.
- The RETURN record (tag 2) is **not captured** before the 3516 halt (a
  discrepancy to note: the return trace at `emit_return` start did not appear in
  the ring before the fail-stop).

## 4. First incorrect transition / root cause (narrowed)

- The activation frame is **never established**: every CALL runs with
`RV_FRAME=0`, and the failing RETURN (3516) also sees `RV_FRAME=0`. So the
callee activation prologue is not setting `RV_FRAME` (or the calls are not
creating an activation). This is the first incorrect transition — the frame
stays 0 from the first call.
- The exact reason the prologue doesn't set the frame is the next step (inspect
the closure prologue / `emit_mkclosure` and the `do`/call setup). Not yet
pinpointed; no fix applied (cause not fully demonstrated).

## 5. Correction

**None yet** — the trace shows the frame is never set, but the specific
prologue/emit defect is not isolated. No guard bypass; no speculative change.

## 6. Verification

- Runtime CALL/ACTIVATE/RETURN records — DONE (CALL records captured).
- G3H non-task (expanded): PASS (200, earlier).
- G3H task (expanded): FAILS (3516) — frame never set.
- Default build: PASS.
- Frozen check: fails (s1.c/s1.h/r0_s1_runtime.c modified — permitted).

## 7. Next

Inspect the closure activation prologue (where `RV_FRAME` should be set on a
call) — the trace shows it never changes from 0. That is the direct continuation
(frame-integrity), not a capacity change.

## 8. Commit

Branch `experiment/glon-s1-d42`, one local commit. Not pushed/merged.