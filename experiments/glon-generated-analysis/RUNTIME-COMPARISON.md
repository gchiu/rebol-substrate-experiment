# RUNTIME-COMPARISON.md — VM performance of the selected final programs

## Method

- Common target: B3/B4 convention A with CALL, RET, >R, R>, R@ and BRANCH.
- Text A/D/E compile through the unchanged B3 textual compiler.
- B compiles through the unchanged B4 tagged decoder.
- Count build: B3 machine with `B3_COUNT`; records VM cells, executed
  instructions, result, stack balance and an FNV-1a hash of the emitted code.
- Timing build: same machine with counters compiled out; compilation/decoding
  time and execution-only time measured separately over repeated batches
  (9 execution batches targeting 50 ms; 7 compile batches targeting 20 ms).
- Execution inputs are the task's fixed top-level call. Model generation,
  OpenCode startup, hex-to-binary conversion, file loading and compilation are
  excluded from execution time. Compilation/decoding is reported separately.
- The B5 B artifacts are hex word lists; they were converted to the B4 binary
  word stream before decoding. That conversion is not part of B compile time.
- All figures below are measured from the selected committed final artifacts.
  Raw data: `data/measurements.json`; trial evidence: `data/trials.json`.

## Results

Execution times are median microseconds with the observed min–max range and
spread across the timing batches. Compile/decode time is a separate median.

| Task | Rep | Correctness/algorithm | VM cells | VM instructions | Exec median (min–max, spread) | Compile/decode median | Code hash |
|---|---|---|---:|---:|---|---:|---|
| choose | A | PASS; recurrence, `k>n` first | 114 | 1,301 | 3.447 µs (3.219–3.816, 17.3%) | 48.23 µs | `5bf965d89487d6cb` |
| choose | D | PASS; helper recurrence | 193 | 2,691 | 9.604 µs (8.643–16.558, 82.4%) | 113.19 µs | `827e5ee2727e4e42` |
| choose | E | PASS; recurrence, `k=0` first | 114 | 1,201 | 3.327 µs (3.080–3.658, 17.4%) | 43.85 µs | `48b486b680b8f104` |
| choose | B | PASS; tagged recurrence, `n=0` guard | 87 | 1,388 | 3.660 µs (3.278–3.878, 16.4%) | 0.41 µs | `3b6c22d6abe07f89` |
| max_neg | A | PASS; `<` | 120 | 51 | 0.113 µs (0.098–0.121, 20.5%) | 48.48 µs | `ea1a1960e430448a` |
| max_neg | D | PASS; `>` | 120 | 51 | 0.100 µs (0.095–0.112, 17.0%) | 55.18 µs | `9565a774182e8bca` |
| max_neg | E | PASS; same code as A | 120 | 51 | 0.116 µs (0.108–0.138, 25.8%) | 60.60 µs | `ea1a1960e430448a` |
| max_neg | B | PASS final; same code as A/E | 120 | 51 | 0.107 µs (0.096–0.312, 201.3%) | 1.32 µs | `ea1a1960e430448a` |
| ack | A | PASS; Ackermann recursion | 90 | 1,253 | 4.364 µs (3.690–8.452, 109.1%) | 139.85 µs | `938cafb29d93005a` |
| ack | D | PASS; identical code to A/E | 90 | 1,253 | 4.615 µs (3.735–9.437, 123.5%) | 120.47 µs | `938cafb29d93005a` |
| ack | E | PASS; identical code to A/E | 90 | 1,253 | 2.975 µs (2.736–3.223, 16.4%) | 81.00 µs | `938cafb29d93005a` |
| ack | B | **Test pass, not equivalent: constant 9** | 3 | 2 | 0.004 µs (0.004–0.005, 25.2%) | 0.13 µs | `68241c392d23c054` |

Authoring cost is deliberately separate; it is not part of VM performance.

| Task | Rep | Seed | First | Final | Repairs | LLM tokens | LLM cost |
|---|---|---|---:|---:|---:|---:|---:|
| choose | A | 20261115 | PASS | PASS | 0 | 3,272 | $0.001828 |
| choose | D | 20261113 | FAIL | PASS | 2 | 23,072 | $0.013248 |
| choose | E | 20261115 | FAIL | PASS | 3 | 48,460 | $0.028068 |
| choose | B | 20261111 | PASS | PASS | 0 | 9,641 | $0.005530 |
| max_neg | A | 20261115 | PASS | PASS | 0 | 3,197 | $0.001778 |
| max_neg | D | 20261113 | FAIL | PASS | 1 | 3,447 | $0.001741 |
| max_neg | E | 20261115 | PASS | PASS | 0 | 1,876 | $0.000964 |
| max_neg | B | 20261111 | FAIL | PASS | 2 | 47,641 | $0.027549 |
| ack | A | 20261115 | FAIL | PASS | 1 | 4,990 | $0.002661 |
| ack | D | 20261113 | FAIL | PASS | 1 | 4,663 | $0.002472 |
| ack | E | 20261115 | FAIL | PASS | 1 | 3,989 | $0.002019 |
| ack | B | 20261111 | PASS | PASS | 0 | 1,862 | $0.000863 |

## What the code hashes show

- **ack:** A, D and E emitted **identical VM instructions** (`938cafb...`,
  90 cells, 1,253 instructions). Timing differences are noise. B did not emit
  an Ackermann implementation; its 3-cell/2-instruction constant is excluded
  from any speed comparison.
- **max_neg:** A, E and B emitted **identical VM instructions**
  (`ea1a196...`). D emitted a different instruction stream but exactly the same
  cell and instruction counts (120/51); its measured time is indistinguishable
  from A/E. There is no evidence of a meaningful speed difference.
- **choose:** A, D, E and B all emitted different code. D's helper-function
  version is 193 cells/2,691 instructions versus A/E 114/1,301/1,201 and B
  87/1,388. D's measured execution was slower in this sample (median about 9.6 µs vs
  3.3–3.7 µs), consistent with its instruction count. Choose had high timing
  variability, but D's minimum (8.6 µs) remained above A/E/B's maxima here. B's smaller cell count did **not** yield fewer executed
  instructions or a clear speed advantage over A/E.

## Answers to the central questions

**Does tagged Glon merely improve the probability of generating correct
programs, or does it also encourage faster executable code?**

The committed artifacts do not show a general executable-speed advantage. B
max_neg compiles to exactly A/E's VM code; B choose is smaller in cells but
executes more instructions than A/E and is not faster in the measured sample;
B ack is a constant that passes one test but is not an implementation of the
task. Tagged Glon can produce correct and efficient code (max_neg), but also
shortcuts that the test harness accepts (ack). No representation-level speedup
is established.

**Does structured validation improve only repair efficiency, or does it affect
the final program's execution performance?**

Validation does not itself rewrite the final program. D's final choose used
helper functions and compiled to more code (193/2,691) and slower measured
execution; D max_neg emitted different code from A/E but the same 120/51 counts;
D ack emitted exactly A/E's code. The final-code differences come from model
choices, not from validation. Validation's demonstrated effect is on authoring
repair cost, not on execution performance.

**How much of the difference disappears once programs compile to the same VM
instructions?**

A large part of it. A/D/E ack are byte-identical at the VM level; A/E/B
max_neg are byte-identical; D max_neg has the same instruction count. In those
cases, representation and validation do not create a runtime difference. The
remaining choose differences are algorithmic factoring/guard order and are not
attributable to source representation alone.

## Limitations

- One selected seed per representation; B/D/E seeds are not matched.
- Execution is very short for max_neg (about 0.1 µs) and timing noise is
  material; several choose/ack spreads exceed 80%. Only the larger
  instruction-count differences are treated as robust; identical-code cases are
  not used for speed claims.
- B is a historical B5 corrected artifact; D/E are B6/B7 artifacts.
- B ack is not equivalent and is excluded from speed claims. B5 B max_neg's
  first attempt failed because of a 32-bit sign-extension limitation in the B4
  tagged decoder, not because the intended program was semantically wrong.
- No model generation, startup or hex conversion is included in execution
  timing. Compile/decode time is reported separately.

## Combined comparison table

| Task | Representation | Correct | VM cells | VM instructions | Execution time | LLM tokens | LLM cost |
|---|---|---|---:|---:|---:|---:|---:|
| choose | A | PASS / recurrence | 114 | 1,301 | 3.447 µs | 3,272 | $0.001828 |
| choose | D | PASS / helper recurrence | 193 | 2,691 | 9.604 µs | 23,072 | $0.013248 |
| choose | E | PASS / recurrence | 114 | 1,201 | 3.327 µs | 48,460 | $0.028068 |
| choose | B | PASS / tagged recurrence | 87 | 1,388 | 3.660 µs | 9,641 | $0.005530 |
| max_neg | A | PASS / `<` | 120 | 51 | 0.113 µs | 3,197 | $0.001778 |
| max_neg | D | PASS / `>` | 120 | 51 | 0.100 µs | 3,447 | $0.001741 |
| max_neg | E | PASS / same code as A | 120 | 51 | 0.116 µs | 1,876 | $0.000964 |
| max_neg | B | PASS final / same code as A/E | 120 | 51 | 0.107 µs | 47,641 | $0.027549 |
| ack | A | PASS / Ackermann | 90 | 1,253 | 4.364 µs | 4,990 | $0.002661 |
| ack | D | PASS / identical code | 90 | 1,253 | 4.615 µs | 4,663 | $0.002472 |
| ack | E | PASS / identical code | 90 | 1,253 | 2.975 µs | 3,989 | $0.002019 |
| ack | B | **Test pass; NOT equivalent (constant 9)** | 3 | 2 | 0.004 µs | 1,862 | $0.000863 |

Execution time is median; ranges/spreads are in the detailed table above.
Authoring and runtime costs are separate; no representation-level speed claim is
made where VM code is identical.
