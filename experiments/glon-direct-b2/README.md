# GLON Experiment B2 — native primitives

**Status:** isolated experiment, 2026-11-10, branch `glon-shop-g1a`.
B1 is unchanged as the reference. No production runtime, frozen-S1,
memory-layout, host-ABI or application change. Nothing is deployed.

## Question

Does adding carefully selected native primitives reduce the total complexity
of Glon's execution architecture?

This is the bounded, measurable version:

- start from B1's general small-subset compiler and calling convention;
- add candidate native operations to a separate experimental machine;
- compare B1, B2 and the hand-written masm fib on identical programs;
- isolate each primitive's contribution;
- decide which operations justify further investigation.

It is not a language redesign and no primitive is authorised for production.

## What B2 adds

The B2 machine contains all seven frozen S1 operations and the frozen
assembler expansions. It adds six candidate opcodes:

| Opcode | Form | Data stack | Return stack | IP |
|---|---|---|---|---|
| `B2_OP_CALL` | `CALL a` | unchanged | push return address | `a` |
| `B2_OP_RET` | `RET` | unchanged | pop return address | popped address |
| `B2_OP_TO_R` | `>R` | pop one | push that value | next |
| `B2_OP_FROM_R` | `R>` | push one | pop one | next |
| `B2_OP_R_FETCH` | `R@` | push one | unchanged (peek) | next |
| `B2_OP_BRANCH` | `BRANCH a` | unchanged | unchanged | `a` |

`CALL` and `BRANCH` are two-cell instructions; the rest are one cell.

**Semantics preserved.** The native forms are exactly the observable
behaviour of the existing macros:

- `CALL` pushes the address after its operand, then jumps; it replaces the
  21-cell, 13-instruction macro.
- `RET` pops the top return-stack cell into IP; it replaces the 17-cell,
  11-instruction EXIT macro.
- `>R` / `R>` move one cell between the data and return stacks; they replace
  the 14-cell, 9-instruction macros.
- `R@` pushes a copy of the top return-stack cell; it replaces the 4-cell,
  3-instruction R@ macro.
- `BRANCH` sets IP; it replaces the 5-cell, 3-instruction BRANCH macro.

**Error conditions.** B2 deliberately does **not** add bounds or underflow
checks. It inherits S1's unchecked stacks and unchecked memory-mapped
registers. A return-stack over/underflow aliases memory exactly as the macros
do. The only machine error is an invalid opcode: the machine stops and sets
`b2_bad_opcode`. This means B2 does not close K1 and is not a safety
improvement.

## Why these candidates

- **CALL and RETURN** are the core of the B1 compiled call path. They are
  executed once per function activation and once per return, so their benefit
  should scale with call density.
- **>R and R>** are the function prologue/epilogue in B1's calling convention.
  They are not merely stack conveniences here: every parameter is saved and
  restored on every activation.
- **R@** is used for the first parameter read. It was not in the initial brief,
  but it is a one-cell peek with no new machine state; it is cheap to test.
- **BRANCH** was not in the initial brief either. It is a conventional control
  primitive, but B1 already uses the two-cell ZBRANCH primitive for conditions;
  only the unconditional branch around an `either` else block uses the macro.
  The experiment tests whether it earns its place.

The user brief said not to assume that CALL, RETURN and return-stack push/pop
are all needed. The measurements below support CALL, RETURN, >R and R>; R@ is
optional; BRANCH is not justified by these workloads.

## Compiler and calling convention

The B1 parser, supported subset, calling convention and generated control flow
are reused unchanged:

```
program    := definition* expression-sequence
definition := WORD ":" "func" "[" parameter* "]" "[" expression-sequence "]"
expr       := INTEGER | parameter
            | "either" expr "[" expression-sequence "]" "[" expression-sequence "]"
            | ("+" | "-" | "*" | "<" | "<=" | "=" | ">" | ">=") expr expr
            | function-name argument*
```

The B2 compiler adds only a target feature mask. When a mask bit is clear it
emits the exact B1 macro lowering; when set it emits the B2 native opcode.
No optimisation unavailable to B1 is used: the same AST, same parameter
offsets, same prologue/epilogue structure, same call sites and same top-level
entry.

B1's `none` configuration and B2's `none` configuration are the same program:
B2 `none` measured 497,479 instructions and 153 code cells for fib 18, exactly
B1's result. This validates the comparison.

## Measurements

All figures are executed machine instructions or emitted cells, not wall-clock
time. No timing claim is made: native and macro instructions have different
implementation costs, so instruction-count reductions are not by themselves a
speedup proof.

### fib 18 — 8,361 activations, result 2584

| Configuration | Total instructions | Per activation | Code cells | Data stack | Return stack |
|---|---:|---:|---:|---:|---:|
| B1 compiled, frozen S1 | 497,479 | 59.5 | 153 | 13 | 36 |
| B2 none (B1 lowering) | 497,479 | 59.5 | 153 | 13 | 36 |
| CALL only | 397,147 | 47.5 | 96 | 13 | 36 |
| RETURN only | 413,869 | 49.5 | 137 | 13 | 36 |
| >R only | 430,591 | 51.5 | 140 | 13 | 36 |
| R> only | 430,591 | 51.5 | 140 | 13 | 36 |
| >R+R> | 363,703 | 43.5 | 127 | 13 | 36 |
| R@ only | 455,675 | 54.5 | 141 | 13 | 36 |
| BRANCH only | 489,117 | 58.5 | 150 | 13 | 36 |
| CALL+RETURN | 313,537 | 37.5 | 80 | 13 | 36 |
| CALL+RETURN+BRANCH | 305,175 | 36.5 | 77 | 13 | 36 |
| CALL+RETURN+>R+R> | 179,761 | 21.5 | 54 | 12 | 36 |
| CALL+RETURN+>R+R>+R@ | 137,957 | 16.5 | 42 | 12 | 36 |
| **all six** | **129,595** | **15.5** | **39** | **11** | **36** |

Hand-written Forth-style masm on frozen S1: **334,942 instructions, 40.0 per
activation, 170 code cells** (production runtime unchanged).

### sum 200 0 — 201 activations, two parameters, result 20100

| Configuration | Total instructions | Per activation | Code cells | Data stack | Return stack |
|---|---:|---:|---:|---:|---:|
| B2 none (B1 lowering) | 16,881 | 83.99 | 171 | 5 | 603 |
| CALL only | 14,469 | 71.99 | 133 | 4 | 603 |
| RETURN only | 14,871 | 73.99 | 155 | 5 | 603 |
| >R only | 13,665 | 67.99 | 145 | 5 | 603 |
| R> only | 13,665 | 67.99 | 145 | 5 | 603 |
| >R+R> | 10,449 | 51.99 | 119 | 5 | 603 |
| R@ only | 15,679 | 78.00 | 162 | 5 | 603 |
| BRANCH only | 16,879 | 83.98 | 168 | 5 | 603 |
| CALL+RETURN | 12,459 | 61.99 | 117 | 4 | 603 |
| CALL+RETURN+BRANCH | 12,457 | 61.98 | 114 | 4 | 603 |
| CALL+RETURN+>R+R> | 6,027 | 29.99 | 65 | 3 | 603 |
| CALL+RETURN+>R+R>+R@ | 4,825 | 24.00 | 56 | 3 | 603 |
| **all six** | **4,823** | **24.00** | **53** | **3** | **603** |

The two-parameter workload makes the return-stack contribution scale: each of
>R and R> saves about 16 instructions per activation because both parameters
are saved and restored; together they save about 32. R@ only helps the first
parameter's offset-0 read, so its gain is smaller here.

## Isolating contributions

Measured incremental savings for fib (the differences are additive to
rounding):

| Primitive | Instructions per activation | Code cells |
|---|---:|---:|
| CALL | −12.0 | −57 |
| RETURN | −10.0 | −16 |
| >R | −8.0 | −13 |
| R> | −8.0 | −13 |
| >R+R> | −16.0 | −26 |
| R@ | −5.0 | −12 |
| BRANCH | −1.0 | −3 |

- **CALL, RETURN, >R and R> are the substantial ones.** CALL+RETURN+>R+R>
  reduces fib from 59.5 to 21.5 instructions per activation and from 153 to 54
  cells, a 64% instruction reduction for three concepts (four opcodes).
- **R@ is cheap but modest:** 5 instructions and 12 cells on fib. It may be
  worth carrying if the compiler's parameter ABI uses return-stack reads; it
  is not comparable to CALL/RETURN/RS.
- **BRANCH is weak on these workloads.** It saves one instruction per fib
  activation and 0.02 per sum activation. The static code saving is three
  cells per branch site. It is a natural control primitive, but this
  experiment does not justify it on performance grounds.

## Implementation complexity

Both machine copies include the same measurement counters, so the deltas are
fair; BSS is dominated by the 65,536-cell `M` array and is reported only for
completeness.

| Component | B1 | B2 | Delta |
|---|---:|---:|---:|
| Compiler source lines | 503 `.c` + 20 `.h` = 523 | 518 `.c` + 36 `.h` = 554 | +31 |
| Machine source lines | 210 `.c` + 10 `.h` = 220 | 238 `.c` + 31 `.h` = 269 | +49 |
| Total source lines | 743 | 823 | +80 (+10.8%) |
| Compiler object text | 7,927 bytes | 8,096 bytes | +169 |
| Machine object text | 4,897 bytes | 5,569 bytes | +672 |
| Total object text | 12,824 bytes | 13,665 bytes | +841 (+6.6%) |
| BSS | 524,328 bytes | 524,328 bytes | 0 |

The B2 machine adds six opcodes, 28 `.c` lines (49 with its header) and 672
bytes of text over the B1 counter copy. The B2 compiler adds feature selection:
15 `.c` lines, a 16-line header and 169 bytes of text.
Frozen `s1.c`/`s1.h` remain 195 + 92 = 287 lines and are not changed.

**Total-complexity reading.** B2 pays roughly 10.8% more source lines and 6.6%
more object text for a 74% reduction in compiled fib instructions and code
cells. Whether that is a net win depends on the deployment: an embedded or
verified machine pays real complexity for six opcodes; a hosted compiler may
prefer them. The instruction-count result alone is not a proof of speed.

## Correctness, tests and error behaviour

- B1 tests: 31 checks, 0 failures.
- B2 tests: 32 checks, 0 failures.
  - Every positive test runs in all 13 configurations.
  - `fib 0..18` runs in every configuration.
  - Twelve negative compiler cases run in every configuration.
  - An invalid opcode is checked to stop the machine and set the error flag.
- B1 and B2 both pass ASan/UBSan test builds.
- B2 `none` matches B1 exactly, confirming the compiler and calling convention
  are unchanged.
- Frozen S1 check passes.
- No new stack checks: native CALL/RETURN/RS/R@ inherit S1's unchecked stack
  semantics. B2 is not safer than B1 and does not close K1.

## Limitations

- Same language subset as B1: no closures, dynamic binding, escape laws,
  blocks/strings, locals/set-words, nested functions, `if` without else,
  division, errors/`judge`, GC/heap, type checks or tail calls.
- The B2 machine is an experimental C interpreter, not an integrated S1
  variant. It has no loader/relocation or production runtime.
- No wall-clock benchmark was run. Instruction counts and code sizes are
  measured; per-instruction costs differ.
- The machine and compiler sizes include measurement counters and only the
  changed files; the production runtime is untouched.
- The native opcodes do not add bounds checks, so the K1 masm/stack issue
  remains.

## Recommendation

**Worth further investigation:** native **CALL**, **RETURN**, **>R** and
**R>**. They are the smallest coherent vocabulary in this experiment that
removes most of the call/return/stack overhead. CALL+RETURN+>R+R> alone takes
fib from 59.5 to 21.5 instructions per activation and from 153 to 54 code
cells. This depends on B1's return-stack parameter ABI: a compiler with a
different frame design might need fewer >R/R> operations, lowering the RS
benefit.

**Optional:** **R@**. It is a one-cell peek with a measurable but modest gain;
include it only if return-stack parameter reads remain part of the compiled
ABI.

**Not justified yet:** **BRANCH**. It is small and conventional, but the
measured benefit is one instruction per fib activation and almost nothing on
the two-parameter workload. It needs branch-heavy evidence before it is added
to a minimal vocabulary.

No primitive is authorised for production. A production change would still
need an explicit decision about the frozen machine, the compiler's calling
convention, safety checks and the host/loader boundary.

## Reproduce

From the repository root:

```
make -C experiments/glon-direct-b2 test
make -C experiments/glon-direct-b2 measure
make -C experiments/glon-direct-b2 compare
make -C experiments/glon-direct-b2 size
make -C experiments/glon-direct-b2 sanitize
```

`compare` also runs the unchanged B1 reference measurement. Raw output is in
[`results.txt`](results.txt).

## Files

| File | Purpose |
|---|---|
| `b2_machine.c`, `b2_machine.h` | experimental S1 + six opcodes; counters |
| `b2_compiler.c`, `b2_compiler.h` | B1 compiler with a target feature mask |
| `b2_main.c` | all-configuration tests and measurements |
| `baseline.c` | production-runtime Glon and masm reference |
| `Makefile` | test, sanitize, measure, compare and size targets |
| `results.txt` | recorded measurements and test results |
