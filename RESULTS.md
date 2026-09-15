# S1 — Results

Neutral substrate bring-up and control-structure experiments. No REBOL
evaluator yet; no optimisation.

## Build & test

```
make
./s1        # runs 9 tests; "all tests passed" on success
```

## Measurements

- **VM implementation code**: `s1.c` is 195 lines; `s1.h` is 92 lines.
  The run loop + host functions + memory/register model is ~100 lines of the
  C in `s1.c`. (Total project, incl. tests: 659 lines.)
- **Irreducible primitive count**: 7 — `LIT`, `DUP`, `DROP`, `@`, `!`,
  `0BRANCH`, `HOST`. (`OP_HALT` is a harness stop, not a control primitive.)
- **Derived-word count**: 7 assembler macros — `SWAP`, `>R`, `R>`, `R@`,
  `CALL`, `EXIT`, `BRANCH`. Tests additionally derive a symmetric
  coroutine switch (`switch_to_gen`/`switch_to_main`), `throw`, and the
  non-local `break`.

The primitive instruction set was **FROZEN** immediately after bring-up
(tests 1–9 were written against it and pass without any VM change).

## What each control example manipulates (and why no new primitive was needed)

1. **Call/return** — `CALL` is derived: it writes the continuation (the
   address just past itself) onto the return stack via `>R`, then stores the
   callee address into the memory-mapped `IP`. `EXIT` pops the return stack
   into `IP`. Only `@`, `!` and the memory-mapped registers are involved.
2. **Conditional** — the single value-inspecting decision is the primitive
   `0BRANCH`; both arms are ordinary code with a derived forward `BRANCH`.
3. **Loop** — a derived backward `BRANCH` plus a `0BRANCH`; the loop index is
   an ordinary memory cell read/written with `@`/`!`.
4. **Static break/continue** — compiled as forward/backward `BRANCH` and
   `0BRANCH` (the compiler knows the loop exit and continue point). No
   non-local mechanism is needed for *static* break/continue.
5. **Dynamic non-local exit** — `break` through two nested `CALL`s works by
   saving the loop's `RP` (a depth marker) and exit `IP` into memory cells,
   then restoring both with `@`/`!`. This unwinds the return stack by writing
   a saved pointer and jumping — ordinary register stores, no primitive.
6. **CATCH/THROW** — `catch` saves `{SP, RP, IP}` into cells; `throw` restores
   them and re-pushes the error value. Three register stores.
7. **Suspend/resume** — the program saves its own `{SP, RP, IP}` to memory and
   halts; resuming restores those three words and re-enters at the saved `IP`.
8. **Generator** — each side has its own stack *region*; yield/resume are a
   symmetric swap of `{SP, RP, IP}` (a pointer swap, not a copy). The
   generator's accumulator lives on its own data stack across yields.
9. **New abstraction (`times`, `do_until`)** — a bounded-repeat loop using the
   return stack as a counter, and a post-test loop. Neither was anticipated by
   the VM; both are pure compositions of `@`/`!`/`0BRANCH`/branches.

In every case the machine state manipulated is just the three memory-mapped
registers plus ordinary memory; no test needed anything the frozen primitive
set does not provide.

## Where S1 is awkward

- **Code blow-up.** Derived `CALL` is 20 cells, `EXIT` 16, `>R`/`R>` 13 each,
  because register access goes through `@`/`!` rather than dedicated
  instructions. Control-heavy substrate code is long and hard to read.
- **Stack bookkeeping is manual.** Non-local exit, CATCH, and the generator all
  require the programmer to save and restore `SP`/`RP`/`IP` by hand. There is
  no type or discipline preventing a mistake.
- **`SWAP` is not reentrant.** It is derived through two fixed scratch cells
  (`SC_A`/`SC_B`), so a nested/interrupted use would corrupt them. A real
  system would reserve scratch per-task or use the return stack.
- **Fixed "variables".** Tests use hard-coded memory cells for locals instead
  of a proper allocation discipline (the heap exists via `HOST_ALLOC` but is
  not wired into a naming scheme). This is a high-level concern, left out.

## Where S1 is unsafe

- Nothing prevents writing to `IP`/`SP`/`RP` arbitrarily; a stray `!` corrupts
  the machine. There is no type system, no protected registers, no bounds
  checking on `@`/`!`.
- Stacks have no overflow/underflow checks (fixed regions).
- A `cont`-style value is just an integer address; nothing stops re-entering a
  stale one (though one-shot use is the norm here).

## Where S1 is expensive

- **Full suspension that must *copy* stacks** (rather than swap regions) is
  `O(stack depth)` per suspend. The generator test avoided this by using
  separate stack regions + pointer swap; the alternative (explicit copying) is
  the known cost the S0 design priced into its continuations.
- **Every call/return/branch is several memory operations** through the
  memory-mapped registers (no direct register fast path), so the common case
  pays for the substrate's transparency.

## Anything that genuinely seems to require a new primitive?

- **No.** All nine tests passed against the frozen 7-primitive set. The only
  near-miss was the aliasing subtlety of reading the memory-mapped `SP` through
  `@` (fixed by reading the cell *before* the push decrements `SP`), which is
  an implementation correctness issue, not a missing capability.
- The strongest *candidate* for promotion, if profiling later demands it, is a
  dedicated `CALL` (one instruction instead of 20 cells), but that is a
  performance convenience, not an expressiveness necessity — exactly the
  distinction the experiment set out to test.
