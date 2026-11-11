# PERFORMANCE.md — original versus compiled kernel

## What was measured

- **Production Glon path:** correctness only. The production CLI includes
  startup, prelude loading and dynamic evaluation; C1 did not build an isolated
  production-runtime timing harness. Original execution-only timing is **N/A**,
  not estimated.
- **C1 compiled candidate:** CALL/RET/>R/R>/R@ on the B3 experimental machine.
- **Seven-operation direct lowering:** same source and inputs, flags none.
- Compilation and execution are separate. Execution timing excludes parsing,
  compilation, block setup and model/startup costs. Block setup is included in
  the C harness timing loop? The harness fills the fixed block before each
  call; this is part of the harness run, while the kernel code itself begins at
  the function entry. The reported timings are full C harness call including
  the 3-cell/22-cell trampoline and block fill.
- Nine timing batches per vector; medians/ranges below. Instruction counts are
  deterministic from the B3 count build.

## Code and instruction summary

| Measure | Seven-op `none` | Candidate five ops | Ratio |
|---|---:|---:|---:|
| Function code cells | 279 | 157 | 1.78× smaller |
| Total cells incl. trampoline | 301 | 160 | 1.88× smaller |
| Aggregate instructions, 10 vectors | 17,644 | 8,960 | 1.97× fewer |
| Aggregate execution time | 63.604 µs | 38.661 µs | 1.65× faster |
| Compile median | 37.931 µs | 63.157 µs | noisy; no ranking |
| Peak data / return stack | 6 / 64 | 6 / 64 | same |
| Fixed block storage | 16 cells | 16 cells | same |

The timings are noisy; the instruction/cell reductions are deterministic. The
2× instruction reduction is consistent with the candidate operations; it is
not a whole-decoder speed claim.

## Per-vector instruction and timing

Candidate vs seven-op;

| Vector | Instructions candidate / none | Exec median candidate / none |
|---|---:|---:|
| all_zero | 1,260 / 2,470 | 5.437 / 8.950 µs |
| complete_len1_2 | 1,260 / 2,470 | 5.532 / 9.193 µs |
| incomplete_len1_1 | 1,260 / 2,470 | 5.416 / 8.859 µs |
| over_len1_3 | 55 / 127 | 0.137 / 0.359 µs |
| len2_4 | 1,260 / 2,470 | 5.472 / 8.901 µs |
| len3_8 | 1,260 / 2,470 | 5.484 / 8.998 µs |
| two_codes_len2 | 55 / 127 | 0.140 / 0.364 µs |
| boundary_l16 | 30 / 100 | 0.075 / 0.254 µs |
| zero_left | 1,260 / 2,470 | 5.460 / 8.867 µs |
| partial | 1,260 / 2,470 | 5.508 / 8.859 µs |

Within-run spreads were mostly 3–13% for full-recursion vectors and 6–22% for
the very short paths. Aggregate timing ratio is 1.65×; do not treat it as a
precise constant.

## Memory and implementation costs

- C1 uses the existing B3 machine: fixed `M[65,536]`, 5,505 text bytes; no new
  VM primitive or register.
- Block storage: 16 tagged cells at a fixed harness address; no allocation or
  GC during the kernel.
- C1 compiler: 602 `.c` + 11 `.h` lines; 9,227 text bytes (B3: 573+? and 8,553
  text); +674 text bytes.
- C1 harness: 141 lines; binaries 20.6–21.9 KB text, same fixed bss.
- No heap, loader, GC or runtime-state cells are used by the compiled kernel.
- WASM: **N/A**; the B3 machine has no WASM port in this experiment.

## Comparison limits

- The production Glon path was verified for correctness, not execution-only
  timing. Its CLI startup/loader/evaluator costs are not mixed into the
  compiled numbers.
- The harness trampoline is included; candidate uses native CALL, seven-op uses
  the macro CALL. Function-only cell counts are reported separately.
- This is one kernel and ten count vectors, not DEFLATE throughput.
