# R0-S1 Phase 1 — Results

Minimal R0 evaluator running **on** the frozen S1 machine. The evaluator is
genuine S1 machine code assembled by a C emitter; C is only the loader
(parse/interning/heap), the code emitter, the `s1_run` driver, and the result
inspector. No R0 semantic work runs in C.

## Build & run

```
make
./s1     # S1 clean-room + adversarial + claims + hosted R0 + R0-S1 phase 1
```

All suites pass: **77 `ok:` checks, 0 failures, exit 0**. The frozen S1 files
(`s1.c`, `s1.h`, `tests.c`, `adversarial.c`, `claims.c`) are untouched.

## What runs where

- **C — loader/toolchain only:** `dup_str`, `intern`, `lalloc`, `make_block`,
  `make_context`, `bind`, `skip_ws`, `parse_form`, `parse_int`, `parse_word`,
  `parse_block`, `r0_s1_parse`, `r0_s1_init`, `r0_s1_run`, `r0_s1_result`,
  instrumentation getters.
- **C — emitter only (emits S1 code, performs no semantics):** `e_cell`,
  `e_setc`, `e_dup`, `e_peek`, `e_pop_to`, `e_untag_ptr`, `emit_call_fwd`,
  `emit_reduce`, `emit_discard`, `emit_lookup`, `emit_set`, `emit_native`,
  `emit_subexpr`, `emit_loop`.
- **S1 (assembled, executes via `s1_run`):** block traversal (`RV_CUR`/
  `RV_END` loop), element dispatch on tag, context lookup, nearest-binding
  `SET` (including append-to-context), `REDUCE`/`DISCARD`, the result protocol
  `[r1..rN, tagged-N]`, and the native trampoline (untag → HOST → tag →
  `[v, tagged-1]` / `[0]`).

## Mandatory + extra tests (all pass)

| Test | Result |
|---|---|
| `[ 42 ]` → one INT 42 | ok |
| `[ none ]` → one NONE | ok |
| `[ + 2 3 ]` → one INT 5 | ok |
| `[ x: 10 + x 5 ]` → one INT 15 | ok |
| `[ print 42 ]` → zero results | ok |
| `[ 10 20 ]` → one result 20 (sequential) | ok |
| `[ x: 10 :x ]` → get-word 10 | ok |
| `[ 'x ]` → lit-word (a WORD) | ok |
| `[ + + 2 3 4 ]` → nested application 9 | ok |
| `[ x: 2 y: 3 * x y ]` → 6 | ok |
| `[ x: 10 x: 20 x ]` → nearest-update 20 | ok |
| `[ - 10 3 ]` → 7 | ok |

`VALUES`, `FUNC`/closures, `BREAK`/`RETURN`/`THROW`, `RAW`/trapdoor, and the
control stack are explicitly deferred to later phases.

## Instrumentation (evidence the machine ran)

For `[ x: 10 + x 5 ]`:

```
code=1337 cells   ip 0->1588   sp 16384->16382   rp 24576->24576   hostcalls=1
results: N=1  hostcalls=1   r0 = 240 (tag 0)
```

`IP` advanced through the emitted evaluator, `SP` (the S1 data stack) moved
16384→16382 (two tagged cells pushed: the value and the tagged arity), `RP`
returned to baseline (all `CALL`/`EXIT` balanced), and the S1 runtime counted
one `HOST` invocation itself. (Dynamic instruction counts would require
instrumenting the frozen `s1_run`; not done.)

## Proof C is not evaluating

A mechanical audit test opens `r0_s1_runtime.c` and `r0_s1.h` and asserts that
none of `ST_BREAK`, `ST_RETURN`, `ST_THROW`, `ds[`, `cstack[`, `eval_subexpr`,
`eval_block`, `apply` appear. It passes. The evaluator lives only in the 1337
cells of emitted S1 code.

## Bugs found and fixed

1. **Inverted `0BRANCH` logic** (three times). `0BRANCH` jumps when the flag is
   *zero*. The block loop's "more elements?" check, and the `LOOKUP`/`SET`
   "ctx == NONE?" and "i >= count?" checks, all had the branch polarity
   backwards. The `ctx==NONE` inversion made every lookup miss (so `+` looked
   unbound); the loop inversion made multi-element blocks evaluate garbage.
2. **Double element fetch.** `e_cell(RV_CUR)` already fetches `M[RV_CUR]`; an
   extra `asm_fetch()` read past the element, so the token read was garbage.
3. **Swapped `!` operands in the append path.** `!` pops the *address* first,
   then the *value*. The `count++` store pushed `(addr, value)` instead of
   `(value, addr)`, writing `M[count+1] = addr` and leaving the context count
   unchanged.

## Measurements

- **New C source lines:** 708 (`r0_s1.h` 90, `r0_s1_runtime.c` 497,
  `r0_s1_tests.c` 121).
- **Emitted S1 code:** 1337 cells.
- **HOST service ids used:** 12 — `ADD`, `SUB`, `MUL`, `DIV`, `MOD`, `EQ`,
  `LT`, `GT`, `LE`, `GE`, `PRINT`, `DUMP` (dump only in the unbound-word
  diagnostic path).
- **S1 primitive changes: 0** (and no reinterpretation).
- **R0 data stack = S1 `REG_SP`; R0 return stack = S1 `REG_RP`; current context
  = `RV_CTX` cell; position = `RV_CUR`/`RV_END` cells.**

## Architectural notes

- **Value/control separation:** not exercised in phase 1 (no control transfer
  yet), but the value side — tagged cells on the real S1 data stack, the
  tagged-INT arity marker, and the native trampoline building `[v, 1]`/`[0]` —
  is exactly as specified.
- **Most difficult S1 routine:** `LOOKUP`/`SET` (context-walking with the
  parent chain and the append path); the recurring bug was `0BRANCH` polarity,
  which a branch-label discipline (emit-then-patch with explicit "if flag==0
  jump") would have caught earlier.
- **Nothing required an eighth primitive.** Everything is `LIT`/`DUP`/`DROP`/
  `@`/`!`/`0BRANCH`/`HOST` plus the already-derived `CALL`/`EXIT`/`BRANCH`.

## Deferred (documented, not skipped silently)

`VALUES`, closures/`FUNC`, `BREAK`/`RETURN`/`THROW`/`CATCH`, `RAW`/trapdoor,
and the control stack — per the phase brief, phase 1 implements only the
minimal evaluator that proves execution runs on S1.

---

# R0-S1 Phase 2 — Results

Functions, closures, lexical capture, captured-binding mutation, recursion, and
`VALUES` — all executing as genuine S1 machine code through `s1_run()`.

## What was added

- **Closure representation** `[spec, body, captured-context, func-site-id]`
  (4 cells) in `M`, allocated at runtime via `HOST_ALLOC`.
- **`func [args] body`** — a reader-recognised keyword (sym id 0) that evaluates
  the spec and body blocks and builds a closure capturing the current context
  (`RV_CTX`).
- **Ordinary function application** entirely on S1 (`INVOKE-CLOSURE`): record
  caller state, evaluate each argument and reduce it to one value, create a
  child context, bind parameters, restore the argument stack, switch to the
  body, evaluate it via `CALL BLOCK-EVAL`, restore caller `RV_CUR`/`RV_END`/
  `RV_CTX`, and leave `[results..., tagged-N]`.
- **Lexical parent-chain lookup / nearest-binding-update SET** (reused from
  phase 1) now walk child → … → global.
- **`either cond then else`** (a native) and **`values [e1 ... en]`** (a native),
  both evaluating their block arguments via `RUN-BLOCK`, which saves/restores
  `RV_CUR`/`RV_END` on `RP` around a nested `CALL BLOCK-EVAL`.
- **Ordinary function completion** — reaching the end of the body is the only
  return mechanism (no `return` word yet).

## Function-call ABI actually used

1. Save `RV_CLOSURE` and `RV_ARITY` on `RP` (they must survive argument eval).
2. Evaluate `arity` arguments (each `CALL SUBEXPR` + `REDUCE`); the argument
   loop index `RV_T4` is saved/restored on `RP` around each `CALL` because a
   nested call clobbers it.
3. Restore `RV_CLOSURE`/`RV_ARITY`; allocate a child context (`HOST_ALLOC`),
   bind parameters by appending to it.
4. Save `RV_CTX`/`RV_CUR`/`RV_END` on `RP`; set `RV_CTX = child`, point
   `RV_CUR`/`RV_END` at the body; `CALL BLOCK-EVAL`.
5. Restore `RV_END`/`RV_CUR`/`RV_CTX` from `RP`; return the result set.

The call chain is real S1 execution state: `REG_RP` holds continuations plus
the saved `RV_*` frames, `REG_SP` holds tagged values and the tagged arity
marker. No C recursion, no C stack, no C status codes.

## Recursion evidence

```
[factorial 5] sp 16384->16382 (min 16379)  rp 24576->24576 (min 24513)  N=1
```

`REG_RP` deepened to 24513 (63 cells) during `fact 5` and returned exactly to
its 24576 baseline; `REG_SP` returned to baseline + result set. This is the
genuine nested S1 activation that Phase 3 will transfer through.

## Tests

All phase-1 tests still pass. New phase-2 tests (all pass):

| Test | Result |
|---|---|
| A. `add: func [a b] [+ a b]  add 2 3` == 5 | ok |
| B. zero-arg `f` == 42 | ok |
| C. lexical capture `outer 3` applied to 4 == 7 | ok |
| D. `make-counter` -> 15, 16 | ok |
| E. `fact 5` == 120 | ok |
| F. f->g->h == 7, RP restored to baseline | ok |
| G. repeated calls, no SP/RP leakage | ok |
| H. argument cleanup (only result set remains) | ok |
| I. nested application `add 1 add 2 3` == 6 | ok |
| J. `values [10 20]` -> two results | ok |
| K. `values []` -> zero results | ok |

Full suite: **84 `ok:` checks, 0 failures, exit 0.**

## Bugs found and fixed

1. **`HOST_ALLOC` is not 16-aligned**, but tagged pointers require 16-alignment.
   Fixed by allocating 16-aligned cell counts for closures and child contexts.
2. **Tagged block used as a raw pointer** (twice): `RV_BODY` (body) and the
   spec (arity + parameter access) were the *tagged* block value; the pointer
   must be untagged before `@`. This made `RV_CUR`/`RV_END`/arity garbage.
3. **Scratch-cell clobbering across nested calls** (three instances): the
   argument-loop index `RV_T4`, the set-word target `RV_WORD`, and the native
   id `RV_NAT` were all overwritten by nested evaluations (function calls use
   `RV_T4`/`RV_WORD` for parameters; nested natives set `RV_NAT`). Each is now
   saved/restored on `RP` around the nested `CALL`.
4. **`either`'s branch evaluation didn't switch `RV_CUR`/`RV_END`** to the
   branch block before `CALL BLOCK-EVAL` — fixed with the shared `RUN-BLOCK`
   helper that saves/restores position around the nested block evaluation.

## Deviations

- `either` (arity 3) is used for the conditional (factorial needs a base case);
  the architecture names `if` for the two-branch form but phase 2 did not
  prescribe a conditional, so `either` was added. It is ordinary control flow
  (conditional evaluation of a block argument), not non-local control.
- `func-site-id` is stored as 0 (unused until the `return` word arrives in
  phase 3).

## Did anything need an eighth primitive?

No. Everything added uses the frozen seven primitives + `HOST` and the derived
`CALL`/`EXIT`/`>R`/`R>` machinery. `HOST_ALLOC` provides runtime allocation.
Zero S1 primitive changes.
