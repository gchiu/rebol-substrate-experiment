# examples/

A readable tour of the R0-on-S1 language as it currently exists, grouped from
high-level to low-level.

## 1. Ordinary R0

| file | shows |
|------|-------|
| `basics.r0` | literals, arithmetic, words, `values`, `print` |
| `lexical-scope.r0` | independent captured environments; nearest-binding mutation |
| `closures.r0` | `func`, lexical capture, higher-order functions, `either` |
| `recursion.r0` | recursion; `return` in the base case |
| `higher-order.r0` | functions as ordinary values (`:word`, closures returned by functions) |
| `multiple-results.r0` | result sets: `values`, a function yielding several values, `return values [...]` |

## 2. Non-local control

| file | shows |
|------|-------|
| `nonlocal-return.r0` | the built-in definitional `return` (an evaluator/runtime experiment) |
| `return-through-unaware.r0` | `return` crossing an unaware helper function |
| `escape.r0` | a user-defined first-class ESCAPE (the library + its use) |
| `nested-escape.r0` | nested `with-escape`: inner targets inner, outer crosses inner |

`return` and `escape` are *different things*:

- **`return`** is built into the evaluator/runtime (Phase 3A). It is an
  experiment in non-local control implemented in the interpreter itself.
- **`escape`** (`with-escape`) was built *later*, in ordinary R0, using the
  generic RAW trapdoor, without changing the evaluator at all (Phase 4B). It
  is proof that new first-class control can be user-defined.

## 3. Basement / RAW

| file | shows |
|------|-------|
| `raw-basics.r0` | the generic RAW trapdoor: assemble S1 fragments as callables |
| `raw-branch.r0` | `0BRANCH` + labels returning different values by argument |
| `raw-memory.r0` | generic `@` / `!` through the `SCRATCH_A`/`SCRATCH_B` cells |
| `raw-first-class.r0` | a RAW callable is an ordinary first-class value |

RAW is **deliberately unsafe, trusted low-level code**: it is a trapdoor to the
S1 substrate. Fragments are assembled from symbolic mnemonics and the symbolic
ABI, and are invoked as ordinary callables.

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
and executed there (the examples are validated by reading these files,
stripping `;;` comments, and running the first `[ ... ]` program). They are
documentation and are not loaded from disk by the runtime today.
