# FIB-OPT-P3-CONTEXTS.md — audit + design: non-escaping lexical contexts

Phase P3 of the Fibonacci optimisation experiment: move ordinary
**non-escaping** lexical activation contexts into task-local storage, promoting
an escaping (closure-captured) context to the managed heap. This document is
the required FIRST step: an audit of the current (P2) context semantics from the
source, followed by the P3 design.

---

## Part A — Audit of current context semantics (P2)

All references are to `r0_s1_runtime.c` / `r0_s1.h` at the P2 commit.

### A.1 Context object layout

```
context value = p + T_CONTEXT   (T_CONTEXT = 7);  p = 16-aligned payload

payload p (managed, allocated by r_alloc as GC_KIND_CTX):
  p+0  CTX_PARENT = parent context value (tagged T_CONTEXT, or R0_NONE)
  p+1  CTX_COUNT  = number of live bindings
  p+2  CTX_CAP    = R0S1_CTX_CAP = 16 (max bindings)
  p+3 ..          (word, value) pairs; word = tagged word, value = tagged R0 value
```

`enum { CTX_PARENT=0, CTX_COUNT=1, CTX_CAP=2, CTX_DATA=3 }`. The allocated
extent is `(CTX_DATA + 2*R0S1_CTX_CAP + 15) & ~15 = 48` cells (16 header +
48 payload... actually `r_alloc` takes `n = 48` payload cells, so extent 64).

### A.2 Parent representation

`p+0` holds the **tagged parent context** (`mk_context(parent)` or `R0_NONE`
for the root). The global context is a *loader* context (allocated in C by
`make_context` at load time, address in `[40000,47000)`); child contexts are
managed.

### A.3 Slot/value representation

`(word, value)` pairs at `p + 3 + 2*i` (word) and `p + 4 + 2*i` (value), both
ordinary tagged R0 values. Binding order is append order.

### A.4 Allocation path

`r_mkctx` (`emit_mkctx`): `( parent -- child )` — `r_alloc(48, GC_KIND_CTX)`,
store parent/count/cap, return `mk_context(payload)`. Called from
`r_invoke_closure` (`e_cell(RV_CLOSURE); +CLOSURE_CTX; fetch` = captured →
`r_mkctx` → `RV_CHILD`).

### A.5 RV_CTX usage

`RV_CTX` is the current (innermost) context. Set to `global_ctx` by
`r0_s1_run`; set to `RV_CHILD` on closure entry; restored from `frame.CTX` on
return/unwind. Read by `r_lookup`, `r_set`, `r_subexpr` (func keyword capture:
`e_cell(RV_CTX)`), and `r_invoke_closure` (frame CTX field).

### A.6 Every place a context pointer is stored

- `RV_CTX` (current context).
- `RV_CHILD` (transient child during invocation).
- `GC_GLOBAL_CTX` (mirror of the global context).
- frame `FRAME_CTX` field (saved caller context).
- closure `CLOSURE_CTX` field (captured context).
- context `CTX_PARENT` field (parent).
- task record `TREC_CTX` (M1 suspended task's context).
- loader `make_context` result (the global context, C-level).

### A.7 Every place a context pointer is traced by GC

- `mark_value` T_CONTEXT case: `p in [GC_HEAP_BASE,GC_HEAP_LIMIT)` → `mark_push`
  (managed); else → `trace_ctx` (loader/global).
- `trace_ctx`: marks `CTX_PARENT` (via `call_mark_value`) and each value cell
  (`call_mark_value`); never traces words/count/cap.
- Roots: `GC_GLOBAL_CTX`, `RV_CTX`, `RV_CHILD` (via `r_mark_value`), and each
  runnable task's `TREC_CTX` (via `r_mark_value`).

### A.8 Closure capture representation

`r_mkclosure` (`emit_mkclosure`): `( spec body captured site-id -- closure )`.
`captured` is popped into `RV_T1` and stored at `CLOSURE_CTX`. The func keyword
in `r_subexpr` supplies `captured = RV_CTX`.

### A.9 Captured mutation mechanism

`r_set` (`emit_set`): `( value -- value )` binds `RV_WORD` nearest-update —
walks `RV_CTX`'s chain; if the word is found, overwrites that slot's value; if
not found in any ancestor, appends to `RV_CTX`'s own context. This makes a
captured variable's mutation visible to all closures sharing that context.

### A.10 Recursive closure handling

Each closure invocation calls `r_mkctx` for a fresh child context, so recursive
activations have distinct contexts (distinct `n`).

### A.11 Nested closure handling

A nested `func` captures the current `RV_CTX` (its lexical parent chain). The
parent chain is walked by `r_lookup`/`r_set` (`e_cell(RV_T2); fetch` = parent).

### A.12 Frame saved_CTX interaction

The frame's `FRAME_CTX` field stores the caller's `RV_CTX` at invocation; the
normal return and non-local `return` restore `RV_CTX` from it.

### A.13 Multitasking save/restore interaction

M1 suspends a task by saving its `RP` (return stack, `TREC_RP`) and `RV_CTX`
(`TREC_CTX`); resume restores both. The context lives on the task's return
stack region in the M1 arena, so it is preserved across switches.

### A.14 RAW ABI references

`RV_CTX`, `RV_CHILD` are symbolic ABI names (`raw_syms`). The `FRAME_SAVED_*`
offsets map to the frame `FRAME_*` offsets (unchanged in P2). No RAW fragment
reads the context layout directly.

---

## Part B — P3 design

### B.1 Stack-local representation

An ordinary (non-escaping) child context is allocated **on the return stack**,
16-aligned, with the **same layout** as a managed context (48 cells: parent,
count, cap, 16 word/value slots, padding). It is the task-local activation
storage; a 16-aligned base gives the tagged `T_CONTEXT` value the correct tag
invariant (this is the P2 lesson generalised).

### B.2 Managed/escaped representation

A promoted (escaping) context is a normal managed `GC_KIND_CTX` object (as in
P2), allocated by `r_alloc`.

### B.3 Promotion / escape strategy

Promotion is **immediate at capture**: when `r_mkclosure` receives a
`captured` context whose payload is in the stack region `[16384,24576)`, it
allocates a managed context, copies `parent/count/cap` and the `count` live
`(word,value)` pairs, and uses the managed context as the captured value (and
updates `RV_CTX`). Copying happens before any further mutation, so the live
activation, the new closure, and any later closures all observe **one** logical
environment. Because the parent chain of any child context is always
managed/loader (a captured context was already promoted), no recursive
promotion is needed; a child `B` whose parent `A` escapes sees the already
managed `A'`.

### B.4 Pointer / tag / alignment rules

| region | classification | tag check |
|---|---|---|
| `[GC_HEAP_BASE,GC_HEAP_LIMIT)` `[32768,40000)` | managed | `mark_push` |
| `[R0S1_HEAP_BASE,R0S1_HEAP_LIMIT)` `[40000,47000)` | loader (global) | `trace_ctx` |
| stack region `[16384,24576)` | task-local | `trace_ctx` (root storage) |
| else | corruption | HALT |

The stack context base is 16-aligned, so `mk_context(p) = p+7` carries tag 7
exactly; stack and managed contexts are distinguished by the payload range,
never by guessing the low bits.

### B.5 Parent-chain handling

Unchanged: `r_lookup`/`r_set` walk `CTX_PARENT`. A stack context's parent is
managed/loader; a promoted context keeps its original parent. No stale stack
pointers remain after promotion (promotion updates `RV_CTX` and the captured
value; child contexts are created after their parent's promotion).

### B.6 Shared captured mutation

`getter`/`setter` both capture the single promoted context; `r_set` mutates it
in place, so they share one environment.

### B.7 Transitive capture

A closure capturing a context whose parent is already promoted/loader sees the
whole chain through `CTX_PARENT`; GC traces each ancestor via `trace_ctx`.

### B.8 Recursion

Each activation gets a fresh stack context (distinct `n`); the return-stack
allocation order (context before frame) is LIFO, so recursive activations never
overwrite each other's live contexts.

### B.9 GC rooting

`mark_value`'s T_CONTEXT case now handles three regions (B.4). A stack context
is traced as root storage (mark parent + value cells, never `mark_push`). The
context's cells are all tagged values (parent `p+7`, words `id*16+2`, values
tagged), so the conservative return-stack closure scan (with the P2 16-alignment
check) skips them; no extra clearing is required for correctness.

### B.10 Multitasking interaction

The stack context lives on the task's return stack and is saved/restored with
`RP`; `TREC_CTX` (which may now be a stack pointer) is traced by `mark_value`
as root storage.

### B.11 RETURN / RAW escape interaction

Unchanged. Unwinding restores `RV_CTX` from `frame.CTX`; a promoted context
(managed) survives the unwind; a non-escaping stack context is discarded with
the return stack — correct because no escaping reference exists.

### B.12 Implementation surface

- `r0_s1.h`: stack-region constant(s).
- `r0_s1_runtime.c`:
  - `emit_mkctx`: allocate on the return stack (16-aligned, 48 cells) instead of
    `r_alloc`.
  - new `emit_promote_ctx`: `( ctx -- ctx' )` promote a stack context to managed.
  - `emit_mkclosure`: call the promotion on the captured value when stack-local.
  - `emit_mark_value` T_CONTEXT: three-region classification.
  - collect roots unchanged (they already route through `mark_value`).
  - profiler: add a `PF_CTX_STACK`/`PF_PROMOTE` counter (diagnostic only).
- `r0_s1_m2_tests.c`: adversarial GC tests A/B/C/D.

---

## Part C — Implementation results + infrastructure correction

### C.1 Measured activation footprint (P3)

Each closure invocation now consumes on the return stack:

```
context (48 cells, 16-aligned) + frame (9 cells, 16-aligned)
  + two alignment paddings (up to 15 each) + 1 return-address cell
  = ~73 cells/invocation measured (fib 20: RP 24576 -> 23123 = 1453 over 20
    calls; data-stack usage is ~10 cells, so the DS stays shallow).
```

### C.2 Why the old M1 task stacks failed

The M1 task RS was 800 cells, sized for the P2 footprint (~16 cells of frame per
invocation). The `m2: N` stress recurses `repeat 20`, i.e. ~1460 RS cells; at
P2 that was fine (frame-only ~320 cells), at P3 the context+frame footprint
overflowed the 800-cell RS starting at depth ~10, silently overwriting a task's
saved IP and surfacing later as `bad opcode`.

### C.3 M1 task-stack resizing (deliberate tradeoff, not a test-fit)

| metric | old | new |
|---|---|---|
| tasks (`M1_MAX_TASKS`) | 8 | 5 |
| cells/task (`M1_TASK_CELLS`) | 1600 | 3600 |
| per-task DS (`M1_DS_OFF`) | 800 | 400 |
| per-task RS (`M1_RS_OFF`-`M1_DS_OFF`) | 800 | 3200 |
| total arena (`M1_ARENA_BASE=47000`) | [47000,59800) | [47000,65000) |
| task table | 60000 | 65000 |
| wrapper delta | 128 | 80 |

Rationale: the fixed 65536-cell memory and the loader-heap boundary at 47000
leave ~18.5k cells above the arena. 3200 RS cells gives >= 2x the observed
repeat-20 requirement (~1460); 400 DS cells gives >= 40x the observed ~10. 8
tasks x 3200 RS would not fit (25.6k cells), so the task count drops from 8 to
5 — still headroom above the <=3 tasks the tests use.

### C.4 Return-stack overflow guard

`emit_invoke_closure` now computes the owning stack's data-stack top (DS_INIT for
the main world, `arena base + DS_OFF` for a running task, detected by
`SP > DS_INIT` — the same probe the collector uses) and fails-stop with
`HOST_DUMP; HALT` when `RP - 96 < top`. A task exhausting its stack now halts
cleanly instead of corrupting saved state. Boundary tests N2/N3/N4/N5 cover:
below-limit recursion succeeds, deliberate overflow fails cleanly (no
`bad opcode`), a suspended neighbour's record + arena stay intact, and
suspend/switch at 30-deep recursion stays correct.

### C.5 P3 results (fib profiler, unchanged benchmark)

| metric | P1 | P2 | P3 |
|---|---|---|---|
| fib 25 wall-clock (baseline) | 24.72 s | 14.71 s | 9.44 s |
| fib 20 wall-clock (baseline) | 2.21 s | 1.23 s | 0.80 s |
| heap allocations (fib 25) | 242786 | 242786 | 1 |
| GC cycles (fib 25) | — | 2551 | 0 |
| closure-invoc | 242785 | 242785 | 242785 |
| lookups / slots / parent-hops | — | — | unchanged |

Context allocations (`ctx=242785`) now hit the return stack, not the managed
heap; the sole remaining heap allocation is the closure, so no collection runs
and lookup counts are unchanged (P4 remains the lookup phase).

### C.6 Bug fixes surfaced during P3

- `emit_mkctx` leaves `RP` at the context base (it moves the CALL return address
  to `base-1` and lets `EXIT` pop it) so the frame is allocated below the
  context, never overlapping it (was: `RP` restored to the caller's top, so the
  frame clobbered the context's count/cap).
- after `r_mkctx`, restore the invocation return address at the caller's RP top
  (the `r_mkctx` CALL had overwritten it); otherwise the normal-return epilogue
  popped a stale address.
- removed a `count > cap` fail-stop added speculatively: it was a false positive
  on the global (loader) context's pre-existing unbounded growth.
