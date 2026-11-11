# BENCHMARKS.md — measured B8 comparisons

## Correctness

All **130** workload/configuration runs matched the independent expected
result and ended with a balanced data/return stack. No configuration required a
different source or algorithm. Inputs and expected values:

| Workload | Input | Expected |
|---|---|---:|
| fib_10 / fib_15 / fib_18 | fib n | 55 / 610 / 2584 |
| ack_22 / ack_23 / ack_33 | ack m n | 7 / 9 / 61 |
| sum_200 / sum_500 | sum n 0 | 20100 / 125250 |
| choose_6_2 / choose_10_5 | choose n k | 15 / 252 |

The Ackermann source is genuine recursion from the B7 textual artifact, not the
B5 constant-answer shortcut. Multiple inputs guard against constant folding or
dead-code substitution.

## Configuration aggregate

Ten workloads per configuration; totals are sums of per-workload medians and
deterministic instruction counts. `Instr ratio` and `Cell ratio` are
`none/config`; larger is better. Execution ratios are noisy and are secondary
to instruction/cell counts.

| Configuration | Cells | Instructions | Instr ratio | Cell ratio | Aggregate execution ratio | Peak ds / rs |
|---|---:|---:|---:|---:|---:|---:|
| none | 2,077 | 960,072 | 1.000 | 1.000 | 1.000 | 64 / 1,503 |
| call | 1,488 | 789,096 | 1.217 | 1.396 | 1.391 | 63 / 1,503 |
| ret | 1,917 | 817,592 | 1.174 | 1.084 | 1.238 | 64 / 1,503 |
| >r | 1,856 | 816,192 | 1.176 | 1.119 | 1.273 | 64 / 1,503 |
| r> | 1,856 | 816,192 | 1.176 | 1.119 | 1.183 | 64 / 1,503 |
| r@ | 1,963 | 890,394 | 1.078 | 1.058 | 1.126 | 64 / 1,503 |
| branch | 2,026 | 946,448 | 1.014 | 1.025 | 1.077 | 64 / 1,503 |
| >r+r> | 1,635 | 672,312 | 1.428 | 1.270 | 1.512 | 64 / 1,503 |
| call+ret | 1,328 | 646,616 | 1.485 | 1.564 | 1.738 | 63 / 1,503 |
| call+ret+branch | 1,277 | 632,992 | 1.517 | 1.627 | 1.908 | 63 / 1,503 |
| call+ret+>r+r> | 886 | 358,856 | 2.675 | 2.344 | 3.597 | 62 / 1,503 |
| call+ret+>r+r>+r@ | 772 | 289,178 | 3.320 | 2.690 | 4.867 | 62 / 1,503 |
| all six | 721 | 275,554 | 3.484 | 2.881 | 5.304 | 62 / 1,503 |

All six removes **71.3% of executed instructions** and **65.3% of emitted
runtime code** versus the original lowering in this workload set. CALL+RET+RS
already removes 62.6% of instructions and 57.3% of cells. R@ adds 7.3
percentage points of instruction saving and 5.5 points of code saving; BRANCH
adds only 1.4 points of instructions and 2.5 points of cells.

## Per-workload behavior

`none` → `all six`; execution time is one median sample and is not used as a
standalone speed proof.

| Workload | Cells | Instructions | Exec median | Arithmetic nature |
|---|---:|---:|---:|---|
| fib_10 | 153 → 39 | 10,531 → 2,743 | 35.1 → 5.0 µs | recursive calls |
| fib_15 | 153 → 39 | 117,393 → 30,581 | 405.8 → 55.0 µs | recursive calls |
| fib_18 | 153 → 39 | 497,479 → 129,595 | 1,640.6 → 244.1 µs | recursive calls |
| ack_22 | 252 → 90 | 2,364 → 768 | 8.3 → 1.8 µs | double recursion |
| ack_23 | 252 → 90 | 3,853 → 1,253 | 12.6 → 3.0 µs | double recursion |
| ack_33 | 252 → 90 | 212,881 → 69,337 | 732.4 → 188.5 µs | double recursion |
| sum_200 | 171 → 53 | 16,881 → 4,823 | 55.9 → 12.4 µs | arithmetic recursion |
| sum_500 | 171 → 53 | 42,081 → 12,023 | 128.0 → 32.9 µs | arithmetic recursion |
| choose_6_2 | 260 → 114 | 3,049 → 1,301 | 10.6 → 4.0 µs | binomial recursion |
| choose_10_5 | 260 → 114 | 53,560 → 23,130 | 197.3 → 61.7 µs | binomial recursion |

Instruction reductions are consistent across call-heavy workloads. The
smallest ratio is `choose_10_5` (2.32× fewer instructions); the largest is
fib_18 (3.84×). Wall-clock ratios span roughly 3.2×–6.7× in this noisy sample
and are not treated as precise.

## Direct compilation versus primitives

These are different effects:

- **Direct compilation (B1/B3):** fib 18 went from 30,909,921 interpreted
  evaluator instructions to 497,479 direct S1 instructions, 62.1× fewer, with
  no primitive change. B3's same-machine direct lowering vs interpreted
  baseline was also measured separately; direct compilation is the foundational
  gain.
- **Native control primitives (B8, aggregate):** starting from the already
  direct-compiled `none`, all six provides another 3.48× instruction reduction
  and 2.88× code reduction across the ten workloads. That is meaningful but
  smaller than the direct-compilation effect and is not a whole-language speed
  guarantee.
- The B3 records remain the historical wall-clock reference (fib 18: none
  1860 µs, CALL+RET+RS 477 µs, all six 297 µs). B8's instruction counts span
  more inputs and algorithmic families.

## Compilation, code and memory

- Compile/decode medians for the configs are around 47–61 µs per source; B8
  adds no compiler optimization. Raw values and spreads are in
  `results/measurements.json`.
- Peak data stack: 64 cells (`none`, `ret`, `>r`, `r>`, `r@`, `branch`) to 62
  (`call+ret+rs` and up). Peak return stack: 1,503 cells in all configs,
  dominated by recursion depth, especially `sum_500`/`ack_33`.
- The machine keeps the same fixed `M[65,536]` memory; no primitive adds runtime
  state. The experimental B3 machine object is **5,505 text bytes / 524,328
  bss** (the bss is the fixed cell array); B3 compiler object text is 8,553
  bytes. B8's measurement binaries are 19–21 KB text plus the same bss.
- Native executable size of the full production system is unchanged; B8 only
  builds measurement binaries.
- **WASM:** the B3 experimental machine has no WASM port. WASM compatibility is
  **N/A**, not estimated. Production S1's separate WASM parity does not validate
  the B3 machine or these native opcodes.

## Measurement limits

- Wall-clock timing is noisy in WSL; spreads can exceed 20%, and one choose
  configuration exceeded 100%. Deterministic instruction/cell counts are the
  primary evidence.
- B8 uses one selected source per workload family and one process round of 9
  timing batches; it is not a broad language benchmark.
- The memory-heavy DEFLATE and full application workloads are unsupported by
  the B3 compiler and were not replaced by invented workloads.
- No wall-clock claim is made for DEFLATE, traffic or Kākā.
