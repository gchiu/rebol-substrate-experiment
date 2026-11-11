# PRIMITIVE-AUDIT.md — the six candidate native operations

All figures below are measured on the B3 machine with the same textual sources,
algorithms, inputs and compiler settings. Aggregate means the ten B8 workloads
(fib 10/15/18, Ackermann 2 2/2 3/3 3, sum 200/500, choose 6 2/10 5).
`none` is 960,072 instructions, 2,077 cells, 49.2 µs median compile time.
Timing ratios are secondary because execution timing is noisy; instruction and
cell counts are deterministic.

| Primitive | Form | Data stack | Return stack | IP | Cells | Instructions |
|---|---|---|---|---|---|---|
| CALL | `CALL a` | unchanged | push return address | `a` | 2 | 1 |
| RET | `RET` | unchanged | pop return address | popped address | 1 | 1 |
| >R | `>R` | pop one | push that value | next | 1 | 1 |
| R> | `R>` | push one | pop one | next | 1 | 1 |
| R@ | `R@` | push one | unchanged (peek) | next | 1 | 1 |
| BRANCH | `BRANCH a` | unchanged | unchanged | `a` | 2 | 1 |

No new memory or register is introduced. These are performance operations with
the same checked/unchecked stack model as S1. They do not close K1.

## Existing lowerings

| Form | Frozen assembler lowering | Cells | Instructions |
|---|---|---:|---:|
| CALL | `LIT cont; >R; LIT callee; LIT REG_IP; !` | 21 | 13 |
| EXIT (assembler convention) | `R@; RP+1; IP=return` | 17 | 11 |
| `>R` | `LIT REG_RP @; LIT 1; HOST SUB; LIT REG_RP !; LIT REG_RP @ !` | 14 | 9 |
| `R>` | `LIT REG_RP @ @; LIT REG_RP @; LIT 1; HOST ADD; LIT REG_RP !` | 14 | 9 |
| `R@` | `LIT REG_RP @ @` | 4 | 3 |
| BRANCH | `LIT target; LIT REG_IP; !` | 5 | 3 |

The native `RET` is the observable return-address pop. The higher-level
assembler `EXIT` convention also includes the return-stack read and the
compiler's result/arity handling; B3's compiled calling convention uses native
RET only where the B1 EXIT macro's stack effect is preserved. Native RET is not
a general replacement for arbitrary masm EXIT semantics with runtime checks.

## CALL

- **Expected benefit:** one per function activation; proportional to call
  density; removes 12 instructions and 19 cells per statically emitted call.
- **Measured individual:** aggregate `call` vs `none`: **−170,976
  instructions (−17.8%)**, **−589 cells (−28.4%)**, instruction ratio 1.217,
  cell ratio 1.396. Timing ratio 1.39× (noisy).
- **Code/stack:** 21→2 cells, 13→1 instruction; data stack unchanged; return
  stack gets one return address; return-stack peak unchanged.
- **Correctness:** not required; the macro is equivalent. Performance-only.

## RET

- **Expected benefit:** one per activation return; removes 10 instructions and
  16 cells per return site.
- **Measured individual:** aggregate `ret` vs `none`: **−142,480 instructions
  (−14.8%)**, **−160 cells (−7.7%)**, ratio 1.174 instructions, 1.084 cells.
  Timing ratio 1.24× (noisy).
- **Code/stack:** 17→1 cells, 11→1 instruction; data stack unchanged; return
  stack pops one address.
- **Correctness:** not required; macro equivalent. Native RET is distinct from
  higher-level EXIT semantics; use only where return-stack/IP behaviour matches.

## >R

- **Expected benefit:** prologue parameter saves; one per parameter per
  activation; removes 8 instructions and 13 cells per static use.
- **Measured individual:** aggregate `>r` vs `none`: **−143,880 instructions
  (−15.0%)**, **−221 cells (−10.6%)**, ratio 1.176 instructions, 1.119 cells.
  Timing ratio 1.27× (noisy).
- **Code/stack:** 14→1 cells, 9→1 instruction; data stack −1; return stack +1;
  peak determined by recursion depth.
- **Correctness:** not required; macro equivalent. Performance-only.

## R>

- **Expected benefit:** epilogue parameter restores; same static expansion as
  >R.
- **Measured individual:** aggregate `r>` vs `none`: same instruction/cell
  count as `>r`: **−143,880 instructions**, **−221 cells**; timing ratio 1.18×
  (noise-dominated).
- **Code/stack:** 14→1 cells, 9→1 instruction; data stack +1; return stack −1.
- **Correctness:** not required; macro equivalent. Performance-only.

## R@

- **Expected benefit:** only the first parameter read at offset 0 uses R@;
  removes 2 instructions and 3 cells per read. It does not replace offset>0
  parameter reads, which still need address arithmetic.
- **Measured individual:** aggregate `r@` vs `none`: **−69,678 instructions
  (−7.3%)**, **−114 cells (−5.5%)**, ratio 1.078 instructions, 1.058 cells;
  timing ratio 1.13× (noisy).
- **Combined:** core→core+R@ reduces aggregate instructions by another 69,678
  (2.675→3.320 ratio vs none) and cells 886→772.
- **Code/stack:** 4→1 cells, 3→1 instruction; return stack unchanged; data
  stack +1. It is ABI-dependent: convention B does not use it.
- **Correctness:** not required; macro equivalent. Performance-only.

## BRANCH

- **Expected benefit:** only the unconditional branch around an `either` else
  block; conditional branching already uses ZBRANCH. One static branch saves 3
  cells and 2 instructions.
- **Measured individual:** aggregate `branch` vs `none`: **−13,624
  instructions (−1.4%)**, **−51 cells (−2.5%)**, ratio 1.014 instructions,
  1.025 cells; timing ratio 1.08× is noise-level.
- **Combined:** core+R@→all six reduces aggregate instructions by 13,624
  (3.320→3.484 ratio) and cells 772→721. Most of that effect is the
  branch-heavy `choose` workloads; it is near-zero on `sum`.
- **Code/stack:** 5→2 cells, 3→1 instruction; no stack change.
- **Correctness:** not required; macro equivalent. Performance-only.

## Combined configurations

| Configuration | Cells | Instructions | Instr ratio vs none | Cell ratio | Exec ratio vs none |
|---|---:|---:|---:|---:|---:|
| none | 2,077 | 960,072 | 1.000 | 1.000 | 1.000 |
| call | 1,488 | 789,096 | 1.217 | 1.396 | 1.391 |
| ret | 1,917 | 817,592 | 1.174 | 1.084 | 1.238 |
| >r | 1,856 | 816,192 | 1.176 | 1.119 | 1.273 |
| r> | 1,856 | 816,192 | 1.176 | 1.119 | 1.183 |
| r@ | 1,963 | 890,394 | 1.078 | 1.058 | 1.126 |
| branch | 2,026 | 946,448 | 1.014 | 1.025 | 1.077 |
| >r+r> | 1,635 | 672,312 | 1.428 | 1.270 | 1.512 |
| call+ret | 1,328 | 646,616 | 1.485 | 1.564 | 1.738 |
| call+ret+branch | 1,277 | 632,992 | 1.517 | 1.627 | 1.908 |
| call+ret+>r+r> | 886 | 358,856 | 2.675 | 2.344 | 3.597 |
| call+ret+>r+r>+r@ | 772 | 289,178 | 3.320 | 2.690 | 4.867 |
| all six | 721 | 275,554 | 3.484 | 2.881 | 5.304 |

All 130 workload/config runs produced the expected result and a balanced stack.
The timing ratios are aggregate medians and are not a claim of identical
instruction cost; code hashes are in `results/measurements.json`.

## Implementation cost and correctness

- B3 machine adds six opcodes: 245 `.c` + 31 `.h` lines; object text 5,505
  bytes versus B1 frozen S1's 4,817 bytes (+688, +14.3%). It uses the same
  `M[65536]` memory, data/return stacks and registers.
- Peak stack in the measured suite: none ds/rs **64 / 1,503**; all six **62 /
  1,503**. Native control operations reduce data-stack temporaries slightly;
  peak return-stack depth is dominated by recursion, not by the primitive set.
- No primitive is required for correctness: every native form has an existing
  macro lowering. They are performance and code-density choices only.
- They do not add bounds checks; all machine copies retain unchecked S1 stack
  semantics. K1 remains open.
