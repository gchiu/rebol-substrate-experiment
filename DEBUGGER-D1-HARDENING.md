# DEBUGGER D1.1 — Hardening / Root-Cause Investigation

D1 was functionally correct but fragile: ordinary R0 closures made during a
suspended-debuggee inspection could produce address-dependent corruption
(unbound words, `bad opcode`, infinite loops). D1.1 identifies and fixes the
root cause without touching the frozen evaluator/trapdoor or S1.

**Result: success.** Full suite **156 `ok:` checks, 0 failures, exit 0** (30 of
them D1 debugger checks), **zero** `bad opcode`, **zero** `[dump]` (unbound
word) diagnostics, zero hangs. The frozen runtime/trapdoor and S1 are
byte-for-byte unchanged.

---

## 1. Minimal reproducer

Ordinary R0 closure calls during inspection, with the debuggee suspended:

```
debuggee:  f: func [] [ x: 10 debug-break x: + x 5 x ]  f

body (fragile):   st: continue   ctx: state-get st 6
                  a: f5 ctx 'x 0 16   b: f5 ctx 'x 0 16   continue
```

`f5: func [c w i n] [ either >= i n [ -2 ] [ ctx-val c i ] ]` — a single,
non-recursive closure. One call during inspection worked; **two** calls (or a
recursive `count: func [n] [ either = n 0 [ 0 ] [ count - n 1 ] ]`, or the
ordinary-R0 `find-in`/`lookup` lexical walk) corrupted the debuggee's resume.

The corruption was **address-dependent**: adding or removing one RAW fragment
or one closure from the debugger library shifted the heap addresses and moved
the failure between "unbound word", `bad opcode 8199 at 265`, and infinite
loop.

## 2. Root cause

The debugger and the debuggee share a single `HP` (the S1 heap). Two distinct
clobbering sites follow from that:

1. **Debugger closures vs debuggee data.** The debugger block runs first and
   defines its own ordinary R0 closures (`f5`, `count`, `find-in`, `lookup`,
   `resume-it`) at `HP = 32768 .. X`. The debuggee then runs with the *same*
   initial `HP = 32768`, so its live data (the debuggee's `f` closure, child
   context, activation frame) is allocated at `32768 .. Y`, **overwriting the
   debugger's closures**.

2. **Debugger inspection vs debuggee frames.** When the debugger later calls a
   closure during inspection, that call allocates a child context + frame at
   the current `HP`, which sits on top of the debuggee's still-live frames and
   contexts, clobbering them. When the debuggee resumes, its frame restoration
   reads garbage.

The nine-cell suspended-state record was **not** the problem (see §6): it was
insufficient only in the sense that the debugger's *heap* was not separated
from the debuggee's *heap*.

## 3. Why the original tests passed

The D1 submission moved the lexical walk (`ctx-lookup`) and the frame count
(`frame-depth`) into RAW fragments. RAW fragments are assembled as *code*, not
allocated from `HP`, so they never touched the debuggee's live heap data — they
simply avoided the overlap by construction. The one remaining ordinary R0
closure (`resume-it` in test I) survived only because the (incorrect) D1
"HP-fix" line happened to place its allocation where it did no harm for that
particular test's address layout. The tests passed, but the underlying
heap-sharing hazard was never resolved.

## 4. Why the failure was address-dependent

The corruption writes to whatever cells happen to be at `HP` at that moment.
`HP` advances deterministically with the *number and order* of the debugger's
own closures and RAW fragments (code size) — so every change to the debugger
library shifted the collision point. A collision that landed on the debuggee's
frame produced a `bad opcode`; a collision that landed on the debugger's own
closure table produced an unbound word; a collision that landed on a return
address produced an infinite loop. Hence: same bug, three symptoms.

## 5. Explanation of `bad opcode 3904 at 19988`

`bad opcode X at Y` means the machine reached `IP = Y` and found `M[Y] = X`
(not a valid opcode). Here `19988` is a **return-stack address** (the debuggee
runs on a reserved return stack at `20000` downward), and `3904` is the address
of `r_subexpr`'s `asm_exit` (a *code continuation*, i.e. a return-stack entry's
payload).

So the machine was made to jump to a return-stack *address* (`19988`) instead
of to the *continuation stored there* (`3904`). That is exactly what happens
when the debuggee's clobbered activation frame yields a garbage saved `IP` /
resume token on restore: `continue` copies the corrupted saved state back into
`REG_IP`, which points into the return stack rather than at code.

The variant `bad opcode 8199 at 265` is the same class of event one level
down: the corrupted resume landed in the middle of `r_reduce` (at `265`, the
`LIT RV_T1` operand `8199`) instead of at `r_reduce`'s entry `256`.

## 6. Complete suspended-state audit

The nine captured cells are `resume_ip, SP, RP, HP, RV_CUR, RV_END, RV_CTX,
RV_BLK, RV_FRAME`. Every other mutable evaluator cell was classified:

| cell | classification |
|---|---|
| `RV_T1..T6`, `RV_N` | **safe (scratch)** — recomputed before every use; never live across a sub-expression boundary |
| `RV_WORD`, `RV_NAT` | **safe (saved on RP)** — the set-word / native trampoline pushes them on the return stack around nested evaluation |
| `RV_CLOSURE`, `RV_ARITY` | **safe (saved on RP)** — `invoke_closure` saves/restores them on `RP` |
| `RV_CHILD`, `RV_BODY`, `RV_SITE`, `RV_SIP`, `RV_SRP`, `RV_FNEW` | **safe (recomputed)** — derived within a single `invoke_closure`, never needed after it returns |
| `RV_NVALS` | **safe** — recomputed by `values` |
| `RV_RES_BUF` | **safe** — only used inside `return`/`throw`, never live across a breakpoint (a breakpoint is a sub-expression boundary) |
| `RV_HOSTCALLS`, `RV_RPMIN`, `RV_SPMIN` | **irrelevant** — instrumentation only |
| `REG_IP, REG_SP, REG_RP` | **captured** (as `resume_ip`, `SP`, `RP`) |
| `REG_HP` | **captured** (as `HP`) — and this was the only cell whose *region* was wrong, not whose *capture* was wrong |

**Conclusion:** the nine cells were sufficient. No additional cell needed to be
captured; the fix is purely the heap-region separation in §8.

## 7. Memory-region audit

| region | range | owner |
|---|---|---|
| code (evaluator + RAW fragments) | 256 .. ~5000 | shared, read-only at runtime |
| state buffers A / B | 6000..6008 / 6020..6028 | debugger |
| `RV` cells + scratch | 8192..8261 | shared (register file) |
| debuggee data stack | 12000 downward | debuggee |
| debugger data stack | 16384 downward | debugger |
| debuggee return stack | 20000 downward | debuggee |
| debugger return stack | 24576 downward | debugger |
| S1 heap (`HP`) | 32768 .. 40000 | **debuggee** (was shared — the bug) |
| loader heap (`hp`) | 40000 .. 47000 | toolchain (parse time) |
| **debugger heap** | **50000 upward** | **debugger** (new, fixed) |

Before the fix, "S1 heap" was shared by both worlds, and the debugger's own
closure definitions plus its inspection allocations collided with the
debuggee's live data. After the fix the two heaps are disjoint; observed
maximum use is far below every boundary.

## 8. Fix

Two changes, both in the debugger's own R0/RAW source (no evaluator change):

1. The debugger block begins by moving `HP` to a private region:

   ```
   set-hp: raw [ LIT 50000 LIT REG_HP ! ARITY 0 EXIT ]
   set-hp
   ```

   so the debugger's ordinary closures and its inspection allocations live at
   `50000+`, never over the debuggee's S1 heap.

2. The D1 "HP-fix" line in `continue` (which copied the debugger's HP into the
   debuggee record) is removed; `continue` simply restores the debuggee's own
   captured HP, and `debug-break` restores the debugger's own saved HP. HP is
   monotonic within each world and never moves backward.

## 9. Regression tests

- `test_hll_inspection` (acceptance): recursive `count`, nested `twice :inc 5`,
  and an ordinary-R0 lexical `lookup`/`find-in` are all exercised **during**
  the suspended inspection, and their results are checked.
- `test_stress`: 100 suspend / inspect-with-recursive-closure / resume cycles,
  each checked for the correct result and a clean return stack.
- `check_no_diagnostics`: the debug suite runs with `stderr` redirected to a
  file, then the file is asserted to contain no `bad opcode` and no `[dump]`.

## 10. Stress results

100 suspend / inspect / resume cycles: 100/100 correct results, RP returned to
the debuggee baseline every cycle, no bad opcode, no unbound word, no hang.

## 11. Do HLL inspection closures now work?

**Yes.** The acceptance test proves ordinary R0 closures (recursive, nested,
and the lexical chain-walk `lookup`/`find-in`) run correctly during a suspended
inspection — no longer only the RAW `ctx-lookup`/`frame-depth` workaround.

## 12. Is the RAW workaround still required?

**No.** The RAW `ctx-lookup` and `frame-depth` remain in the library as small
generic mechanism primitives, but the HLL `lookup`/`find-in` (ordinary `func`)
is now exercised and passes. The workaround is no longer masking the bug.

## 13–16. Frozen-state status

- runtime/trapdoor (`r0_s1_runtime.c`, `r0_s1.h`): **unchanged**
  (`git diff --exit-code r0-trapdoor-v2 -- r0_s1_runtime.c r0_s1.h` is empty).
- HOST: **unchanged** (no new service).
- S1: **frozen** (`./check-frozen-s1.sh` passes; `f90496c26dc45c7a387d8fc8639cd2781507d3d2`).
- Eighth primitive: **not required** — the fix is a `LIT 50000` heap-region
  switch inside ordinary RAW, using only the frozen primitives.
