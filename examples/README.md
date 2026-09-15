# examples/

A readable progression through R0-on-S1, from ordinary evaluation to a
user-defined first-class control construct.

## Progression

1. `basics.r0` — ordinary R0: literals, arithmetic, words, `values`, `print`.
2. `closures.r0` — `func`, lexical capture, higher-order functions, `either`.
3. `counter.r0` — mutable captured state (a closure over a set-word).
4. `recursion.r0` — recursion and the built-in `return` keyword.
5. `nonlocal-return.r0` — the built-in non-local RETURN experiment (Phase 3A).
6. `raw-basics.r0` — the generic RAW trapdoor (Phase 4A): assemble S1 fragments
   as first-class callables, using the symbolic RAW ABI.
7. `escape.r0` — a user-defined first-class ESCAPE built from ordinary R0 plus
   two generic `raw` fragments (Phase 4B), using only symbolic ABI names.

The narrative arc:

    ordinary R0
        -> closures / recursion
        -> built-in non-local RETURN experiment
        -> generic RAW trapdoor
        -> user-defined first-class ESCAPE

## Symbolic RAW ABI

`raw` fragments address the machine through stable symbolic names rather than
hard-wired numbers (resolved by the assembler — tooling only):

- registers: `REG_IP REG_SP REG_RP REG_HP`
- evaluator cells: `RV_CTX RV_CUR RV_END RV_BLK RV_FRAME`
- activation-frame fields: `FRAME_PREV FRAME_SITE_ID FRAME_SAVED_SP
  FRAME_SAVED_RP FRAME_SAVED_IP FRAME_SAVED_CTX FRAME_SAVED_CUR
  FRAME_SAVED_END FRAME_SAVED_BLK`
- generic scratch cells: `SCRATCH_A SCRATCH_B`

The assembler knows these names and maps them to cells/offsets. It has no
knowledge of ESCAPE, RETURN, BREAK, THROW, CATCH, UPARSE, or any other
high-level construct.

## Loading status

The R0-on-S1 harness currently has **no file loader** — programs are passed to
`r0_s1_parse` as C strings. Every file in this directory is therefore a
**canonical source listing**: the exact text is embedded in `r0_s1_tests.c`
and executed there. They are documentation and are not loaded from disk today.
