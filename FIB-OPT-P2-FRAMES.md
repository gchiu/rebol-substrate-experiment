# FIB-OPT-P2-FRAMES.md — move transient activation frames off the managed heap

Phase P2 of the Fibonacci optimisation experiment: remove the ordinary
transient activation **frame** from the GC-managed heap and store it in
task-local stack storage (the return stack). Nothing else changes: no lexical
lookup optimisation, no context-representation change, no evaluator-dispatch
change, no Fibonacci special-casing.

Baseline (P1) is commit `cb8f8c070792fe3aaabbb3c4a0af2e4d6f074fb1`.

---

## 1. Old frame representation

Each closure activation allocated a **managed heap object** (`GC_KIND_FRAME`,
`r_alloc(16, FRAME)`), a 16-byte header + 16-byte payload (32 cells), filled
with 9 fields linked by `RV_FRAME`:

```
frame (payload) : [ prev, site, SP, RP, IP, CTX, CUR, END, BLK ]
FRAME_PREV=0 FRAME_SITE=1 FRAME_SP=2 FRAME_RP=3 FRAME_IP=4
FRAME_CTX=5 FRAME_CUR=6 FRAME_END=7 FRAME_BLK=8
```

`prev` links to the caller's frame; `site` is the func-site-id for non-local
`return`; `SP/RP/IP` are the caller's machine state; `CTX/CUR/END/BLK` are the
caller's evaluator state. The GC traced frames as ordinary managed objects
(`mark_frame` → `mark_push`, drain → `trace_frame` → mark `prev` + `CTX`).

## 2. New frame representation

The frame is a 9-field record allocated **on the return stack** (task-local),
with the base **16-aligned**. Because the return-stack top is not naturally
16-aligned (continuations are 1 cell), the allocator first pushes
`padding = (RP-9) mod 16` dummy cells, then pushes the 9 fields in reverse so
they land contiguously at a 16-aligned base `frame`:

```
return stack (deepest -> shallowest):
  [ frame+0 .. frame+8 ]  9 fields (prev .. BLK)   frame is 16-aligned
  [ frame+9 .. RP0-1  ]   padding (0..15 dummy cells)
  [ RP0             ]   closure-invocation return address
```

The frame is released by restoring `RP` on return/unwind (pop
`frame.RP - 1 - frame` cells), so its lifetime is exactly the activation's.
`RV_FRAME` still points at the live frame and `frame.prev` still links the
chain; the FRAME_* offsets and field semantics are unchanged.

**Why 16-alignment matters (bug found):** the old heap frame was naturally
16-aligned, so its pointer had R0 tag `T_INT(0)` and `frame-here`'s result was
treated as a plain value. A return-stack pointer such as `24536` has
`24536 % 16 == 8 == T_CLOSURE`, so `target: frame-here` produced a value that
the evaluator tried to *invoke* as a closure — corrupting execution. Aligning
the frame base to 16 restores the `T_INT` tag and the value semantics, without
changing the ESC_LIB or the FRAME_* ABI.

## 3. Why frames cannot escape their activation lifetime

The frame holds only the caller's resume state (`SP/RP/IP/CTX/CUR/END/BLK`), a
`site` id, and the `prev` link. Nothing in the frame is exposed to GLON — no
language value references a frame except transiently through `RV_FRAME`/RAW
`frame-here` during the activation. When the activation returns or is unwound,
`RV_FRAME` becomes `frame.prev` and `RP` is restored, so the frame record is
dead storage. It never needs to outlive its activation (unlike a lexical
context, which closures capture). Hence stack storage is sound.

## 4. Effect on RETURN

Unchanged. `emit_return` still walks the chain via `frame.prev`, matches by
`FRAME_SITE`, and restores `SP/RP/IP/CTX/CUR/END/BLK` from the matched frame's
`FRAME_*` offsets. Restoring `REG_RP = FRAME_RP` discards the intermediate
frames and their padding in one step. Deep nested `return` passes (verified).

## 5. Effect on RAW escape

Unchanged. The ESC_LIB `frame-here`/`restore` fragments (and `examples/*.r0`)
are byte-identical; the `FRAME_SAVED_*` symbolic offsets map to the same
`FRAME_*` numbers, and the frame pointer is a stable address with the same
field layout. The physical location moved (heap → return stack) and the pointer
is now 16-aligned, which is exactly the "layout and pointer semantics remain
valid" case the brief permits.

## 6. Effect on multitasking

None required. The frame lives on the **return stack**, which is already
task-local (the main world uses `[16384,24576)`, each M1 task its own arena
slice). Suspending task A saves its `RP` (and `RV_FRAME` via `TREC_FRAME`) and
resuming restores them, so A's frame chain is intact. Existing M1 tests pass
unchanged; no M1 library edit was needed.

## 7. GC root treatment

`mark_frame` no longer marks the frame record (it is not a heap object) but
instead traces its pointer-bearing contents directly, walking the chain:

- `frame.prev` — recursively (`call_mark_frame`), and
- `frame.CTX` — the saved caller context, via `call_mark_value` (tagged value).

`BLK` is a permanent loader block and `CUR/END` are raw loader addresses, so
they are never traced. The `RV_FRAME` root and each task's `TREC_FRAME` root
still call `mark_frame`; the drain loop's `GC_KIND_FRAME` case and the old
`trace_frame` were removed. The conservative return-stack closure scan
(`emit_mark_closure_inline`) gained a **16-alignment check** so the frame's
tagged `CTX` field (a `p+7` context) is skipped rather than misread as a
closure pointer.

The **adversarial test** (M2 test S) proves this: a closure bound in an outer
function's context, reachable during a forced collection *only* through the
outer frame's `CTX`, survives; after the activation returns and the last
reference drops, it is reclaimed.

## 8. Complete regression results

`./check-frozen-s1.sh` → **OK** (before and after). `s1.c`/`s1.h`/`tests.c`/
`adversarial.c`/`claims.c` byte-identical.

`make clean && make && make test` → **306 assertions, 0 failures** (304 prior
+ 2 new adversarial frame-root tests). Ordinary calls, recursion, nested calls,
lexical closures, captured mutation, non-local `return`, RAW `frame-here`,
user-space with-escape, nested escapes, multiple results, GC with deep live
activations, managed BLOCK!/STRING!/USER reachable only through a live frame,
and multitasking suspension all pass.

## 9. Baseline vs P2 profiler table (fib 25)

| metric | P1 baseline | P2 frame-stack | change |
|---|---|---|---|
| result | 75025 | 75025 | = |
| fib activations | 242 785 | 242 785 | = |
| managed allocations | 485 571 | 242 786 | **-242 785** |
| — contexts | 242 785 | 242 785 | = |
| — frames | 242 785 | **0** | **-242 785** |
| — other (closure) | 1 | 1 | = |
| allocated cells | 23 307 392 | 15 538 272 | **-7 769 120** |
| GC cycles | 4 164 | 2 551 | **-1 613 (-39%)** |
| heap high-water | 40 000 | 39 968 | ≈ |
| lookups | 1 699 493 | 1 699 493 | **= (unchanged)** |
| slots examined | 11 653 672 | 11 653 672 | = |
| parent hops | 1 092 530 | 1 092 530 | = |
| max recursion depth | 25 | 25 | = |
| SP high-water depth | 13 | 13 | = |
| RP high-water depth | 315 | 765 | +450 (frames now on RS) |

`frame allocs` is exactly `calls(n)` going to 0; `allocs` is `calls(n)+1`
(one closure + one context per activation). Lookups/slots/hops are bit-identical,
confirming name resolution is untouched.

## 10. Baseline vs P2 timing table (median, monotonic, define+invoke)

| n | P1 baseline | P2 frame-stack | speedup |
|---|---|---|---|
| 10 | 0.0156 s | 0.0089 s | 1.76× |
| 15 | 0.187 s | 0.106 s | 1.76× |
| 20 | 2.208 s | 1.228 s | 1.80× |
| 25 | 24.717 s | 14.706 s | 1.68× |

fib 10/15/20 = medians of 100/50/20 reps; fib 25 = median of 5 reps.

## 11. Exact allocation reduction

Per activation the frame allocation (1 object, 32 cells) is removed:

- allocations: `2·calls(n)+1` → `calls(n)+1` (fib 25: 485 571 → 242 786).
- cells: `96·calls(n)+32` → `64·calls(n)+32` (fib 25: 23 307 392 → 15 538 272).

The heap high-water no longer reaches the 40 000 ceiling (now 39 968), because
the transient frame churn that forced full sweeps is gone.

## 12. Exact GC reduction

GC cycles: 4 164 → 2 551 (fib 25). Collections dropped ~39% and now reclaim
only dead *contexts*, not dead frames+contexts. Because contexts are half the
per-activation footprint of frames+contexts, the heap turns over roughly half
as often.

## 13. Any unexpected regression

One implementation bug (caught by the escape tests): the unaligned return-stack
frame pointer carried a callable tag (`T_CLOSURE`) and was invoked by mistake;
fixed by 16-aligning the frame base (§2). RP high-water depth grows (315 → 765
cells) because frames now consume return-stack space — the expected and
intended trade for zero heap allocation. No RAW ABI change; no semantics change.

## 14. Recommendation for P3

The remaining per-activation cost is now the **lexical context** (one 64-cell
heap object per call, still `242 785` allocations for fib 25) plus the
**lexical lookup** (7 lookups/activation, ~6.86 slots each, all of it
unchanged overhead). P3 should target either the per-call context allocation
or static lexical-slot resolution. **Not implemented here**, per the P2 brief.
