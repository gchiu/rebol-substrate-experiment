# GLON-S1-D4.6 — Diagnose the Invalid Top-Level RETURN (checkpoint)

Date: 2026-11-26
Branch: `experiment/glon-s1-d46`
Baseline: G3J/S1-D4.5, commit `75a4dd7`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.
**RETURN trace made safe; failing edge not captured (jumps past the trace).**

## 1. Baseline

- Branch `experiment/glon-s1-d46` from `75a4dd7`. G3J.6 diagnostics + `H3B-RESULTS.md` preserved.

## 2. RETURN tracing made safe

- Confirmed: the RETURN trace read `[RV_FRAME+FRAME_IP]` = M[4] when
  `RV_FRAME==0` (a frame-0 dereference). **Fixed:** the RETURN trace (tag 2) now
  pushes retip=0 (no deref); ACTIVATE still reads the valid frame's IP. A RETURN
  is recorded before the no-matching-activation guard, with ip/frame/site/rp.

## 3. Captured (G3H task, expanded)

```
n=17: {t=1 f=0 …} {t=3 f=28576 ret=12394} {t=4 f=0} … (no t=2)
… executed-ip=12026 tag=3516 (emit_return guard, RV_FRAME==0)
```

- Closures: CALL(f=0) → ACTIVATE(f=28576, ret=12394) → RESTORE(f=0) — correct.
- **No tag-2 (RETURN) record** appears before the 3516 halt: the failing return
  reaches the guard **without executing the instrumented `emit_return` start**
(3512). So it does not enter `emit_return` through its first instruction.

## 4. Root cause (narrowed) / remaining blocker

- The failing RETURN jumps **into the guard region (3516) without passing the
  `emit_return` entry trace (3512)** — the incoming control-flow edge targets the
  guard, not the function start. The closures' saved return IP is 12394 (valid);
  the failing return's own saved IP is not captured (no t=2 record).
- This is a control-flow/emit discrepancy (a jump into the middle of `emit_return`
or a duplicated/inlined guard), not a frame-0 capacity issue. The exact incoming
edge is not isolated with the current trace (the entry trace is bypassed).

## 5. Correction

**None applied** (the incoming edge is not demonstrated). Guard not bypassed; no
artificial frames; no capacity change.

## 6. Verification

- Safe RETURN trace (no frame-0 deref) — DONE.
- Nested calls → 42 (earlier) — PASS.
- G3H non-task — PASS (200). G3H task — FAILS (3516, top-level return to f=0).
- Default build — PASS. Frozen check — fails (r0_s1_runtime.c modified, permitted).

## 7. Next

Capture a bounded instruction history (the last N executed IPs) to see the jump
that lands at 3516 without 3512 — the direct continuation within the tracing
scope. The safe trace is in place; the missing piece is the predecessor edge.

## 8. Commit

Branch `experiment/glon-s1-d46`, one local commit (checkpoint). Not pushed/merged.