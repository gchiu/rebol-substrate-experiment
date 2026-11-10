# GLON Experiment B3 — execution speed and calling conventions

**Status:** isolated experiment, 2026-11-10, branch `glon-shop-g1a`.
B1 and B2 are unchanged. No production runtime, frozen-S1, memory-layout,
host-ABI or application change. No architectural migration is proposed.

## Question

Do B2's instruction-count reductions translate into real execution-speed
improvements? Is the return-stack calling convention a good choice?

B3 adds only measurement and a second calling convention. It does not add a
primitive, change the machine opcodes, or change the supported source subset.

## What is compared

Workloads (identical source and results in every case):

- `fib 18`: naive recursion, 8,361 activations, result 2584.
- `sum 500 0`: two-parameter recursion, 501 activations, result 125250.

Primitive configurations:

| Label | Flags |
|---|---|
| `none` | original seven primitives, B1 macro lowering |
| `call+ret+>r+r>` | native CALL, RET, >R, R> |
| `call+ret+>r+r>+r@` | plus native R@ |
| `all six` | plus native CALL, RET, >R, R>, R@, BRANCH |

Calling conventions:

- **A — return-stack parameters (B2 ABI).** The caller pushes arguments left
  to right. The callee moves them to the return stack, reads parameter `i` at
  `RP + i`, leaves the result on the data stack, discards the saved parameters
  and returns.
- **B — data-stack parameters with a separate return mechanism.** The callee
  leaves its arguments on the data stack. Parameter `i` is read at
  `SP + temporaries + (N - 1 - i)`. On return, the result is temporarily moved
  to the return stack, SP is advanced by `N` to discard the arguments, and the
  result is restored. Return addresses remain on the return stack, so the
  return mechanism is separate from the argument storage.

Both conventions use the same parser, subset, argument order, result
convention, recursion and control flow. Convention B adds compile-time data
stack-depth tracking and an SP-adjusting epilogue; it uses no new primitive.

## Measurement method

- **Instruction counts, code cells and stack depth** use a machine build with
  counters. The count run is separate from timing.
- **Wall-clock** uses the same B2/B3 machine semantics with counters compiled
  out (`-DB3_COUNT` absent). B1 timing uses the frozen `s1.c`.
- Each timed batch is auto-calibrated to at least 50 ms per trial; 9 trials per
  process round. Compilation time is measured separately by repeatedly calling
  the compiler without running the code.
- Three unpinned process rounds were run for both the B1 reference and all B3
  configurations. The table below gives the median of the three round medians;
  raw ranges and within-round spreads are in [`results.txt`](results.txt).
- The empty-program/reset overhead is about 0.00–0.01 µs, negligible for these
  workloads.

The environment is WSL on a shared AMD Ryzen 5; wall-clock variability was
large (within-round spreads mostly 3–30%, and larger across some rounds). Only
the large differences should be treated as architectural signals. Instruction
counts are deterministic and separate from timing.

**Reference reproduction.** B1's harness measured 497,479 instructions / 153
cells for fib 18 and 42,081 / 171 for sum 500, matching B1. B3 `A: none` is
identical. B2's `call+ret+>r+r>` and `all six` counts are reproduced by B3
`A: core` and `A: all six` (179,761 / 54 and 129,595 / 39 on fib; 15,027 / 65
and 12,023 / 53 on sum).

## Wall-clock results

Medians from three unpinned rounds (microseconds per complete workload):

| Case | fib 18 | sum 500 | fib compile | sum compile |
|---|---:|---:|---:|---:|
| B1 original S1 | 2103.10 | 175.80 | 54.22 | 57.25 |
| A: none (B1 lowering) | 1860.03 | 145.36 | 67.47 | 54.64 |
| A: call+ret+>r+r> | 476.89 | 49.11 | 50.82 | 42.11 |
| A: call+ret+>r+r>+r@ | 313.37 | 33.52 | 57.48 | 42.59 |
| A: all six | **296.90** | **33.65** | 61.73 | 79.77 |
| B: none (B1 lowering) | 1952.01 | 143.72 | 46.40 | 64.89 |
| B: call+ret+>r+r> | 612.06 | 63.43 | 46.22 | 44.18 |
| B: call+ret+>r+r>+r@ | 574.82 | 64.64 | 54.22 | 50.04 |
| B: all six | 560.16 | 65.71 | 53.81 | 53.45 |

Selected measured ratios:

| Comparison | fib 18 | sum 500 |
|---|---:|---:|
| A none → A core | 3.90× faster | 2.96× faster |
| A core → A core+R@ | 1.52× faster | 1.47× faster |
| A none → A all | 6.26× faster | 4.32× faster |
| B none → B all | 3.48× faster | 2.19× faster |
| A all vs B all | 1.89× faster | 1.95× faster |
| B1 original vs A all | 7.08× faster | 5.22× faster |

**Compilation is separate.** It is ~40–80 µs per compilation here, versus
~0.3–2.1 ms for fib 18 and ~0.03–0.18 ms for sum. For the small `sum` workload,
compile time can exceed execution time; compile-once overhead matters there.

## Instruction counts, code and stacks

Measured on the counter build (fib 18 / sum 500):

| Configuration | Instructions (fib) | Per activation | Code cells | ds / rs (fib) | Instructions (sum) | Code cells | ds / rs (sum) |
|---|---:|---:|---:|---:|---:|---:|---:|
| B1 | 497,479 | 59.50 | 153 | 13 / 36 | 42,081 | 171 | 5 / 1,503 |
| A none | 497,479 | 59.50 | 153 | 13 / 36 | 42,081 | 171 | 5 / 1,503 |
| A core | 179,761 | 21.50 | 54 | 12 / 36 | 15,027 | 65 | 3 / 1,503 |
| A core+R@ | 137,957 | 16.50 | 42 | 12 / 36 | 12,025 | 56 | 3 / 1,503 |
| A all | 129,595 | 15.50 | 39 | 11 / 36 | 12,023 | 53 | 3 / 1,503 |
| B none | 547,644 | 65.50 | 166 | 22 / 19 | 38,067 | 159 | 1,005 / 502 |
| B core | 229,926 | 27.50 | 67 | 22 / 19 | 19,029 | 79 | 1,005 / 502 |
| B core+R@ | 229,926 | 27.50 | 67 | 22 / 19 | 19,029 | 79 | 1,005 / 502 |
| B all | 221,564 | 26.50 | 64 | 21 / 19 | 19,027 | 76 | 1,004 / 502 |

R@ is unused by convention B: `B core` and `B core+R@` are identical in
instruction count and code cells, and their wall-clock difference is noise.

## Calling conventions: what happened

**A is the better choice once native primitives are enabled.**

- With no native primitives (`none`), A and B are close: 1860 vs 1952 µs on
  fib, 145.4 vs 143.7 µs on sum. B's no-prologue epilogue can be slightly
  cheaper for multiple parameters, but its parameter reads and cleanup are
  more expensive in code and instructions.
- With native CALL/RET/RS, A wins clearly: 476.9 vs 612.1 µs (fib) and 49.1 vs
  63.4 µs (sum). With all six: 296.9 vs 560.2 µs and 33.65 vs 65.71 µs.
- B's data-stack retention raises data-stack depth sharply: fib 22 vs 13;
  sum 1,005 vs 5. It lowers return-stack depth: fib 19 vs 36; sum 502 vs
  1,503. It trades one resource for the other.
- B's generated code is larger: fib 166 vs 153 cells at `none`, 67 vs 54 at
  core, 64 vs 39 at all. It also adds compiler state and an SP-adjusting
  epilogue. **B does not simplify the implementation overall; it moves
  complexity from the return stack to the data stack and the compiler.**

Both conventions preserve equivalent results and stack balance in tests. The
source subset is the same. Both inherit unchecked stacks: no native or
compiled operation adds a bounds check, and K1 remains.

## Primitive isolation and speed

Within convention A, using the same machine and convention:

- CALL/RET/>R/R>: 1860 → 477 µs on fib (3.90×), 145 → 49 µs on sum (2.96×).
- R@: 477 → 313 µs (1.52×) and 49 → 33.5 µs (1.47×).
- BRANCH: 313 → 297 µs (1.05×) on fib and no measurable change on sum.

R@ is worth carrying for this compiler's parameter ABI. BRANCH is small and
conventional but does not justify itself on these workloads. That agrees with
B2's instruction-count findings, but the wall-clock R@ gain is larger than its
instruction-count share: the eliminated R@ macro performs more memory work
per instruction.

## Implementation complexity

| Component | B1 | B2 | B3 |
|---|---:|---:|---:|
| Compiler source lines | 523 | 554 | 615 |
| Compiler text | 7,927 B | 8,096 B | 8,553 B |
| Machine source lines | 287 (frozen s1.c/s1.h) | 269 | 276 |
| Total source lines | 810 | 823 | 891 |
| Machine text | 4,817 B | 5,569 B (counter build) | 5,505 B fast / 5,569 B count |
| Compiler+machine text | 12,744 B | 13,665 B | 14,058 B |

B3 adds convention support and depth tracking: +61 compiler lines and 457
bytes of compiler text over B2. The B3 machine is the B2 machine with
conditional counters; it adds no primitive. B3 has no additional runtime
memory cells; the only runtime difference is which stack holds arguments.

## Unexpected findings

1. **Wall-clock speedup exceeds the instruction-count reduction on fib.**
   A none → A all is 3.84× fewer instructions but 6.26× faster in the measured
   runs. Native ops remove memory traffic, not only dispatches. This is not a
   general whole-language result.
2. **B1 and B3 A:none are the same code but different machine binaries.**
   B1 was 2103 µs and A:none 1860 µs on fib; sum 175.8 vs 145.4 µs. The
   difference is machine implementation/code layout, not instruction selection.
   It shows why instruction counts alone are not a speed proof, and it
   confounds any direct B1-vs-B2 absolute timing comparison.
3. **B's convention is roughly tied with A under all macros, but loses badly
   with native primitives.** Without native RS, B's data-stack arguments avoid
   prologue/epilogue macro saves; with native RS, A's return-stack saves are
   one instruction each and A wins decisively.
4. **Compilation can dominate small workloads.** For `sum` with A all, the
   ~80 µs compile time is larger than the ~34 µs execution time.
5. **B's R@ is dead code.** It has no effect on B's generated instruction
   count or code size; only timing noise differs.

## Recommendation

- **Keep convention A (return-stack parameters).** It is faster with the
  native control primitives, produces less code, and needs less compiler
  machinery. B is not adopted.
- **Native CALL, RET, >R, R> are justified by wall-clock as well as instruction
  counts** for this compiler: 3.90×/2.96× on the two workloads.
- **R@ is justified in convention A:** another 1.52×/1.47×.
- **BRANCH is optional:** 1.05× on fib, no measurable gain on sum.
- **B2 "all six"** is the fastest configuration measured, but the marginal
  case for BRANCH is weak.
- No production change is authorised. Any future architecture work still needs
  a decision about the frozen machine, checked stacks and K1.

## Limitations

- Restricted integer subset only. No closures, dynamic binding, escape laws,
  GC/strings, errors/`judge`, locals or loader integration. Do not infer
  whole-language performance.
- WSL wall-clock noise is substantial; medians and ranges are reported. Only
  large differences are reliable.
- The B1 reference uses frozen `s1.c`; B3 uses the B2/B3 machine. Machine
  implementation differs even when instruction selection is identical.
- Both conventions use unchecked stacks. K1 remains; underflow/overflow alias
  memory exactly as in S1.
- Compilation time is host C time; no JIT/caching/load-time model is measured.
- No production runtime, memory layout, host ABI or application was changed.

## Reproduce

From the repository root:

```
make -C experiments/glon-direct-b3 test
make -C experiments/glon-direct-b3 reproduce
make -C experiments/glon-direct-b3 measure
make -C experiments/glon-direct-b3 bench
make -C experiments/glon-direct-b3 bench-rounds BENCH_ROUNDS=3
make -C experiments/glon-direct-b3 size
make -C experiments/glon-direct-b3 sanitize
```

`reproduce` runs the unchanged B1 and B2 measurements. The raw three-round log,
count run and size run are recorded in [`results.txt`](results.txt).

## Files

| File | Purpose |
|---|---|
| `b3_compiler.c`, `b3_compiler.h` | B2 subset compiler with A/B conventions |
| `b3_machine.c`, `b3_machine.h` | B2 machine copy; counters optional |
| `b3_main.c` | tests, count measurement and wall-clock harness |
| `b1_ref_main.c` | B1 reference timing/count harness (frozen S1) |
| `bench_util.h` | auto-calibrated median/spread timing helper |
| `Makefile` | test, measure, bench, reproduce, size, sanitize |
| `results.txt` | raw and summarised measurements |
