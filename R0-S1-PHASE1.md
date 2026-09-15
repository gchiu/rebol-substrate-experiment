# R0-S1 Phase 1 — Implementation Plan

Minimal real evaluator: prove R0 evaluation executes through `s1_run()` on the
seven-primitive S1 machine, not through C.

C is restricted to: parsing/interning/loading, preloading the global
environment, assembling S1 code, providing mundane HOST services, calling
`s1_run`, and inspecting results. All evaluation semantics — data/result stack,
block traversal, lookup, SET, eval-subexpr, native dispatch, sequential block
evaluation — are S1 machine code.

## Memory map

| Region | Purpose |
|---|---|
| `0..3` | S1 registers `IP`/`SP`/`RP`/`HP` (memory-mapped) |
| `256..` | S1 code (the assembled evaluator; grows up) |
| `8192..8212` | R0 runtime variable cells (see below) |
| `16384..` | S1 data stack `SP` (the R0 value/result stack, grows down) |
| `24576..` | S1 return stack `RP` (continuations for `CALL`/`EXIT`, grows down) |
| `40000..47000` | R0 loader heap: blocks, contexts (C allocates at load time) |

R0 values are tagged cells (low 4 bits) living on the S1 data stack and in the
heap — the same tag scheme as the architecture.

## Runtime variable cells (fixed, in M)

| Cell | Meaning |
|---|---|
| `RV_CUR` | current element address (walking a block) |
| `RV_END` | one-past-last element address |
| `RV_CTX` | current context (tagged CONTEXT) |
| `RV_WORD` | set-word target (persistent across RHS eval) |
| `RV_NAT` | native id (persistent across argument eval) |
| `RV_HOSTCALLS` | HOST-call counter (instrumentation) |
| `RV_N`, `RV_T1..T6` | scratch (callee-clobbered) |

Persistent cells are written by a frame and read after nested `CALL`s; scratch
cells are used only by leaf routines that return before any further nesting.

## Entry points

- `EVAL_LOOP` — the top-level S1 entry. C sets `RV_CUR`, `RV_END`, `RV_CTX`,
  resets `SP`/`RP` (`s1_reset`), then `s1_run(EVAL_LOOP)`. It walks the block
  element-by-element, discarding non-final result sets, and `HALT`s leaving the
  final result set `[r1..rN, tagged-N]` on the data stack.
- `EVAL_SUBEXPR` — evaluates one sub-expression at `RV_CUR`, advancing it.
- `APPLY_NATIVE` — evaluates a native's arguments and dispatches on the HOST id.
- `REDUCE` — `[r1..rN, tagged-N] -> r1` (or NONE if N=0).
- `DISCARD` — drop a result set.
- `LOOKUP` — `word -> value` (or `-1` if unbound).
- `SET` — nearest-binding update of a word to a value in `RV_CTX`.

## S1 calling convention

All subroutines use `CALL`/`EXIT` (continuations on `RP`). Arguments and
results pass on the data stack (`SP`). `RV_CUR`/`RV_END`/`RV_CTX` are the
evaluator's global position/context; recursion is a linear left-to-right walk,
so the position is never saved/restored across a `CALL`.

## How REG_SP / REG_RP are used

- `REG_SP` is the R0 value/result stack. Tagged values and the tagged-INT
  arity marker are pushed/popped here. The result protocol `[r1..rN, N]` is
  `r1..rN` pushed then `mk_int(N)` pushed (top = arity).
- `REG_RP` is the evaluator's recursion/return stack: every `CALL` pushes a
  continuation, `EXIT` pops it. Recursion in `EVAL_SUBEXPR`/`APPLY_NATIVE` is
  genuine machine call/return, not C recursion.

## Current-context storage

`RV_CTX` holds the current context as a tagged `CONTEXT` value. Phase 1 has a
single global context (no `FUNC`), so it never changes during a run, but
`LOOKUP`/`SET` walk the parent chain anyway (reusable for phase 2).

## Evaluator loop (S1 pseudocode)

```
EVAL_LOOP:
  if RV_CUR >= RV_END: push [NONE,1]; HALT
  loop:
    CALL EVAL_SUBEXPR            ; [result, N]
    if RV_CUR >= RV_END: HALT    ; last element: keep result
    CALL DISCARD                 ; drop result, continue

EVAL_SUBEXPR:
  elem = M[RV_CUR]; RV_CUR++
  tag = elem % 16
  if tag in {INT,NONE,BLOCK,CONTEXT,CLOSURE,NATIVE,RAW}: push [elem, 1]
  if tag == WORD:  v = LOOKUP(elem); if NATIVE: APPLY_NATIVE else push [v,1]
  if tag == GET:   v = LOOKUP(elem-2); push [v, 1]
  if tag == LIT:   push [elem-3, 1]
  if tag == SET:   RV_WORD = elem-1; eval RHS; REDUCE; SET; push [v, 1]
```

## HOST ABI

HOST arithmetic pops raw cells and pushes a raw result (`a op b`). The S1
native trampoline untags R0 INT arguments (`/16`), calls the appropriate HOST
opcode, tags the result (`*16`), and pushes the tagged-1 arity marker. `PRINT`
untags and calls `HOST_PRINT`, then pushes tagged-0 (`[0]`). The trampoline
also increments `RV_HOSTCALLS` (instrumentation). Native ids equal HOST ids:
`+ - * /` = 0 1 2 3, `= < > <= >=` = 6 8 9 10 11, `print` = 14.

## Deferred (not in phase 1)

`FUNC`/closures, `BREAK`/`RETURN`/`THROW`/`CATCH`, `RAW`/trapdoor, `VALUES`,
and the control stack. `VALUES` is explicitly deferred per the phase brief.

## What runs where

- **C (loader/toolchain):** parser, interning, heap allocator, global-context
  preload, assembler/emitter, `s1_run` driver, result inspection.
- **S1 (runtime semantics):** everything in "Evaluator loop" above — traversal,
  dispatch, lookup, SET, argument evaluation, the result protocol, the native
  trampoline.
