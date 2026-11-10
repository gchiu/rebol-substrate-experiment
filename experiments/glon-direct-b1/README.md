# GLON Experiment B1 — direct function execution

**Status:** isolated experiment, 2026-11-10, branch `glon-shop-g1a`.
No production runtime, frozen-S1, memory-layout, host-ABI or application
change. Nothing is deployed.

## Question

Can compile-once execution substantially reduce Glon's evaluator overhead
without adding an S1 primitive?

This experiment answers the narrow, testable version of that question:

- compile a **small documented Glon subset** to direct S1 code;
- run a recursive Fibonacci program in that code on the unchanged frozen S1;
- compare it with the existing Glon evaluator and the existing hand-written
  Forth-style masm Fibonacci;
- measure instructions per activation, generated code size, compiler size and
  extra runtime state.

It is not a redesign of Glon and does not claim the full language is compiled.

## Result

For naive `fib 18` (8,361 activations, result 2584):

| Case | Executed S1 instructions | Per activation | Generated code | Extra runtime state |
|---|---:|---:|---:|---|
| Existing Glon evaluator | 30,909,921 | 3,696.9 | shared 12,861-cell runtime + 80 loader cells | production runtime |
| Hand-written Forth-style masm | 334,942 | 40.0 | 170 masm cells (+ shared runtime) | production runtime |
| B1 compiled Glon | 497,479 | 59.5 | 153 cells | **0 runtime-state cells** |

- Compiled vs Glon evaluator: **62.1× fewer S1 instructions**.
- Compiled vs hand-written masm: **1.49× more** instructions, but **153 vs 170
  generated cells**.
- Compiled code uses only S1 code and the frozen data/return stacks. It allocates
  no heap, loader, GC or `RV_*` state.

**The hypothesis is supported for this subset:** direct compilation removes
most evaluator overhead without a new primitive. It is not evidence that the
full language (closures, dynamic binding, escape laws, GC'ed values) can be
compiled with the same result.

## Supported subset

The compiler accepts this grammar (comments start with `;`):

```
program   := definition* expression-sequence
definition:= WORD ":" "func" "[" parameter* "]" "[" expression-sequence "]"
expr      := INTEGER
           | parameter
           | "either" expr "[" expression-sequence "]" "[" expression-sequence "]"
           | ("+" | "-" | "*" | "<" | "<=" | "=" | ">" | ">=") expr expr
           | function-name argument*
```

- Top-level function definitions only.
- Integer literals, including negatives.
- Parameters and function calls, including recursion and forward references.
- `+ - *` and the comparisons `< <= = > >=`.
- `either` with both branches.
- Multiple expressions in a sequence: all but the last are evaluated and
  discarded.
- Limits: 32 functions, 8 parameters, 8 arguments per call, 128 expressions in
  a sequence, 2,048 AST nodes.

**Not implemented:** closures, dynamic binding, escape laws, blocks as values,
set-words/locals, strings, nested functions, `if` without an else, `/` and
other natives, non-integer values, NONE/boolean values as distinct types,
type checks, exceptions, tail calls, GC, heap or loader integration.

## Calling convention

- The caller pushes arguments left to right; the callee receives them on the
  data stack with the last argument on top.
- The callee's prologue moves each argument to the return stack with the frozen
  `>R` macro. Parameter `i` is read from `RP + i`.
- The body leaves one result on the data stack.
- The epilogue discards the saved arguments with `R> DROP` and returns with the
  frozen `EXIT` macro.
- Calls use the frozen CALL expansion shape: continuation literal, `>R`,
  callee literal, `LIT REG_IP !`. Forward references are patched after all
  functions are emitted.
- The top-level entry is compiled to code, then `HALT`.

No opcode outside the frozen seven-primitive machine is emitted. The source
uses the frozen assembler's existing derived macros (`>R`, `R>`, `CALL`,
`EXIT`, `BRANCH`); those macros are already part of `s1.c`.

## Tests

`make test` builds against the **frozen `s1.c`** and runs 31 checks:

- positive: identity, two arguments, subtraction, tagged multiplication,
  comparisons, `either`, forward reference, negative literals, recursive fib
  5/10/18, a two-parameter recursive sum, and fib 0..18 against a reference;
- negative: unknown word, missing argument, nested `func`, `if` without else,
  `/`, set-word local, block value, string, empty body, duplicate function,
  unterminated body, too many parameters.

All 31 checks pass, including an independent AddressSanitizer/UBSan run.

## Measurement method

- `s1_count.c` is a **copy of frozen `s1.c` with counters only**. Semantics are
  unchanged; the frozen file itself is not modified.
- The baseline links the repository's unchanged production runtime
  (`r0_s1_runtime.c`, `r0_s1_session.c`, `r0_s1_show.c`) with `s1_count.c`.
- The Glon and masm definitions are loaded once, then the counters are reset
  before the timed `fib 18` call, so definition and parse overhead are excluded.
- masm per activation is computed as
  `(instructions(fib 18) - (instructions(fib 1) - 15 base body)) / 8,361`;
  this subtracts the top-level/wrapper while keeping one base activation in the
  denominator. The result is 40.00.
- Compiled per activation is `497,479 / 8,361 = 59.5`; the top-level entry is
  15 instructions (one `LIT`, a 13-instruction CALL expansion, and `HALT`) and
  is included. Removing it changes 59.5 only in the second decimal place.
- Instruction counts are deterministic. No wall-time claim is made. Compile
  time is not part of any S1 instruction count; the compiler is host C code.

## Code and compiler size

- Compiled fib: **153 S1 cells**, including the function and top-level entry.
- Hand-written masm fib: **170 S1 cells**.
- Glon evaluator: no per-function code; the shared emitted runtime is 12,861
  cells and the fib source consumes 80 loader-heap cells.
- Compiler: **503 lines** of `b1_compiler.c` plus a 20-line header; `-O2`
  object text **7,927 bytes**, `data` 0, `bss` 256. The test/measurement
  harness is separate (`b1_main.c`).
- Compiled runtime state: **0** heap/loader/GC/`RV_*` cells. Measured stack
  high-water: data 13 cells, return 36 cells. For comparison, Glon evaluator:
  data 16, return 1,736; masm: data 21, return 22 (both include their
  production runtime).

## Equivalence

All three cases run the same naive recursion and return 2584 for `fib 18`.
The Glon and compiled versions operate on tagged Glon integers and use the
same `+`, `-`, `<`, `=` semantics; the masm comparison works on untagged
integers internally and tags only at the boundary, which is the natural
Forth-style implementation. The compiled version is therefore not faster by
changing the algorithm; it removes lookup, dispatch, contexts and GC from a
closed program. The fib 0..18 sweep checks exact outputs.

## What would be required to extend it

1. **Local variables and set-words** need frame slots and a clear distinction
   between bindings and parameters.
2. **Closures and captured contexts** need allocation, context capture,
   stack-local contexts and the binding/escape laws; this is the boundary
   between this experiment and the real language.
3. **Blocks/strings and GC** need managed representations and collector
   integration. The compiled code currently assumes integer-only values.
4. **Errors and `judge`** need the SIN! representation and a non-local exit
   protocol.
5. **More forms and natives** (`if`, `/`, `do`, `reduce`, `print`, type
   predicates) need a larger parser and semantics, not just more code
   generators.
6. **A real loader/compiler boundary** would need source identification, code
   relocation, caching, and a decision about whether compilation is offline or
   at load time.
7. **Safety:** compiled code currently trusts its source and has no stack
   check or K1 protection. A production compiler needs resource bounds and the
   same fail-stop discipline as the evaluator.
8. **Tail calls and optimisation** could close the 1.49× gap to hand-written
   masm, but are not required to support the hypothesis.

## Reproduce

From the repository root:

```
make -C experiments/glon-direct-b1 test
make -C experiments/glon-direct-b1 measure
make -C experiments/glon-direct-b1 size
```

The raw recorded output is in [`results.txt`](results.txt).

## Files

| File | Purpose |
|---|---|
| `b1_compiler.c`, `b1_compiler.h` | parser and direct S1 code generator |
| `b1_main.c` | positive/negative tests and compiled-fib measurement |
| `baseline.c` | production-runtime Glon and masm fib measurements |
| `s1_count.c`, `s1_count.h` | measurement copy of frozen `s1.c` with counters only |
| `Makefile` | test, measure and size targets |
| `results.txt` | recorded test, measurement and size output |
