# GLON-S1-D4.1 — Complete Activation-Frame Tracing (blocked: no runtime trace primitive)

Date: 2026-11-26
Branch: `experiment/glon-s1-d41`
Baseline: G3J/S1-D4, commit `3c763cc`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.
**Investigation checkpoint — runtime frame trace not implementable in scope.**

## 1. Baseline

- Branch `experiment/glon-s1-d41` from `3c763cc`. G3J.6 diagnostics + `H3B-RESULTS.md` preserved.

## 2. Why runtime transition records cannot be generated (concrete)

- Frame transitions happen in **generated S1 code**: `emit_call_indirect`
(pushes the return IP, jumps to the callee), the closure prologue (sets
`RV_FRAME`), and `emit_return` (walks/restores `RV_FRAME`). To record them at
runtime, a **new S1 HOST operation** must be added: a new id in the host enum, a
case in the VM host dispatch, a C handler writing a bounded ring buffer, and
`asm_host(...)` emitted at each transition.
- The host-op set is validated `v >= 0 && v <= HOST_DUMP` (r0_s1_runtime.c:535);
`HOST_DUMP` is the maximum. Adding a new op is a multi-file change (enum + VM +
masm + C handler) — not a small edit.
- The existing `HOST_DUMP` callback (`r0_s1_dump_ip`) fires only at guard sites
and the final halt, **not** per call/return, so it cannot produce the transition
trace.

This is a concrete technical explanation: the trace requires a new runtime HOST
primitive that does not exist, and adding it is a substantial change beyond this
investigation's remaining scope. I did not add speculative instrumentation.

## 3. Established (from D2/D3, unchanged)

- Failing halt: `emit_return` "no matching activation" (tag 3516),
`RV_FRAME == 0`, at `app/install`, task build only; non-task → 200.
- The first incorrect transition is not isolated (needs the trace above).

## 4. Status

- Runtime frame-transition records — **NOT GENERATED** (no HOST primitive; concrete
reason above).
- Root cause / correction — not established.
- Default build — PASS; frozen check — fails (s1.c/r0_s1_runtime.c modified,
permitted).

## 5. Next (requires a new HOST primitive)

Add a `HOST_TRACE_FRAME` op (enum + VM dispatch + C ring-buffer handler) and emit
it in `emit_call_indirect`/closure-prologue and `emit_return`; then compare task
vs non-task. That is the direct continuation and is a runtime change, not a
capacity/guard change.

## 6. Commit

Branch `experiment/glon-s1-d41`, one local commit (checkpoint). Not pushed/merged.