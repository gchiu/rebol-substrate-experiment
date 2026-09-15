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
