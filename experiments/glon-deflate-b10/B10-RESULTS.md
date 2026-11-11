# B10 results — where DEFLATE's VM operations go

## Conclusion

The cost is overwhelmingly **generic evaluator work and data movement**, not
allocation, garbage collection, or the DEFLATE algorithm itself. On the fixed,
dynamic and streaming workloads:

- **44–45%** of all executed S1 operations are `LIT`/`DUP`/`DROP`;
- **32–34%** are `@`/`!` memory accesses;
- **16–17%** are `HOST` escapes, almost entirely arithmetic/comparison;
- **6.2–6.5%** are `0BRANCH`;
- allocation and GC are a very small fraction of operation volume.

The evaluator/lookup path is observable through the runtime's `PF_*` counters,
but there is no reliable symbol map from emitted S1 code to Glon function
names, so B10 does **not** claim a ten-function ranking. The applicable ranking
is operation classes and evaluator paths.

## Reproducible setup

B9 baseline and fixtures are reused. The baseline source is `212f0b8` (B9);
the normal seven-primitive host is
`experiments/glon-deflate/build/dzhost`. B10's own files are isolated in
`experiments/glon-deflate-b10/`.

```
make -C experiments/glon-deflate build/dzhost
cd experiments/glon-deflate-b10
make
make profile          # writes baseline/profile-results.json
make test             # profile, frozen-S1 check, fixed-workload smoke
```

Instrumentation:

- `build/b10-host-ops` links the **unprofiled** current runtime and
  `s1_prof.c`; it produces exact opcode/HOST counts without the runtime PF
  instrumentation. `B10_OPS`/`B10_HOST`/`B9_PROFILE` lines.
- `build/b10-host-pf` compiles the runtime with `-DR0_S1_PROFILE`; it produces
  evaluator, lookup, allocation, lexical and hashing counters. The PF build
  adds instrumentation operations, so its op totals are not used for timings.
- B9's preserved `baseline/b9-baseline.json` is the timing baseline (normal
  host, five repeats). B10 did not mix PF-instrumented timings into it.

Fixture output is checked byte-for-byte against independent Python zlib, using
the B9 fixture SHA-256 values, before `baseline/profile-results.json` is
written.

## Operation-count breakdown

Exact unprofiled counts from `b10-host-ops`:

| Class | Fixed | Dynamic | Streaming |
|---|---:|---:|---:|
| `LIT` + `DUP` + `DROP` | 127,404,071 | 549,856,313 | 916,443,928 |
| `@` + `!` | 91,371,335 | 405,637,085 | 713,985,121 |
| `HOST` | 48,283,945 | 204,257,987 | 331,915,025 |
| `0BRANCH` | 17,606,146 | 77,507,224 | 135,834,610 |
| **Total** | **284,665,507** | **1,237,258,721** | **2,098,178,800** |

Percentages:

| Class | Fixed | Dynamic | Streaming |
|---|---:|---:|---:|
| Literal/stack (`LIT`,`DUP`,`DROP`) | 44.76% | 44.44% | 43.68% |
| Memory (`@`,`!`) | 32.10% | 32.79% | 34.03% |
| Host escape (`HOST`) | 16.96% | 16.51% | 15.82% |
| Branch (`0BRANCH`) | 6.18% | 6.26% | 6.47% |

The `HOST` calls are dominated by arithmetic and comparison, not allocation:
on fixed, `HOST_ADD` alone is 17,977,523 calls; `EQ` 8,276,087; `LT` 4,227,203;
`SUB` 3,767,964; `MUL` 3,610,564; `MOD` 3,050,162. `HOST_ALLOC` is 0 in this
runtime path and `HOST_PUTCHAR` is only 2,061. Dynamic and streaming show the
same shape at larger scale (`ADD` 75,211,186 / 121,604,737; `EQ` 33,616,116 /
50,027,433).

The independent `PF_*` counters (profiled build, not timing) measure the
evaluator paths:

| Counter | Fixed | Dynamic | Streaming |
|---|---:|---:|---:|
| closure activations | 11,272 | 41,620 | 61,435 |
| subexpression dispatches | 332,475 | 1,145,140 | 1,686,112 |
| block-evaluator iterations | 53,984 | 189,910 | 283,808 |
| logical word lookups | 143,713 | 490,564 | 707,397 |
| lookup slots examined | 170,634 | 712,044 | 1,169,613 |
| native invocations | 91,233 | 299,116 | 425,308 |
| allocation calls | 846 | 4,086 | 12,963 |
| allocated cells | 41,088 | 171,568 | 455,104 |
| RAW/masm invocations | 15,848 | 67,611 | 112,664 |
| max closure depth | 31 | 48 | 48 |

These are operation counts, not VM instruction counts; one subexpression or
closure expands to many S1 instructions. Their scale, together with the
opcode breakdown, says the dominant work is in the evaluator's expression/
lookup/stack machinery and its emitted call/return and lexical-access
sequences. The actual DEFLATE-specific decisions are embedded in that work;
this instrumentation cannot separate them from generic evaluation.

## Ten most expensive functions?

Not reliably measurable in this repository setup. The runtime creates S1 code
dynamically; `s1_prof` has a code-location histogram, but there is no symbol
table mapping emitted code addresses to Glon function names. Instrumenting
closure identity or inserting per-function counters would require invasive
runtime changes, which the task excludes. Reliable attribution stops at the
operation classes and `PF_*` paths above.

## Instruction count versus elapsed time

B9's elapsed-time measurements remain the timing evidence: fixed 1,373.5 ms,
dynamic 6,528.3 ms, streaming 9,608.6 ms (medians; 5.2%, 11.8%, 15.9% spread),
with initialization separated (library load ~24–27 ms, open ~2 ms). B10's
opcode counts are deterministic but **do not** imply that a percentage of
instructions is the same percentage of wall time. No per-opcode cycle timing is
available; host overhead and memory effects are not attributed per instruction.

## Three optimisation directions

### A. Accelerate the existing evaluator/runtime

Evidence for: nearly all executed operations are low-level literal, stack,
memory, host-arithmetic and branch work; `PF_SUBEXPR`, lookup and block-eval
paths are active in every workload; allocation/GC is negligible by volume.
A native-call profile or peephole/code-generation improvements in the
runtime's emitted S1 code directly target the measured hot classes without
needing to compile Glon applications. B9's kernel found a 1.97× instruction
reduction for call-heavy code, showing the potential, but that is not a
whole-decoder result.

Risk/cost: medium implementation work in the runtime emitter, but no language
coverage project, no DEFLATE changes, and full conformance can be re-run.

### B. Compile selected hot functions

Evidence for: C1 compiled `hf/left` to 157 vs 279 function cells and 8,960 vs
17,644 instructions (1.97×), and was 1.63× faster in B9's noisy sample. A
compiled hot function could remove evaluator overhead while leaving the rest
interpreted.

Evidence against: B10 cannot reliably identify the ten hottest Glon functions;
the compiler currently lacks strings/blocks, set-words, closures, errors and
GC; a partial compiled/interpreted boundary adds ABI, rooting and error
semantics. C1's kernel is not a decoder and its compiler path is narrow.

### C. Extend the compiler to complete Glon

Evidence against: complete-source coverage would need module wrappers, locals,
block/string representation, closures, non-local control, loader/host
integration and GC. The profiling does not identify a small set of functions
that justify that infrastructure; it shows generic evaluator costs spread
across all workloads. This is the largest project and the weakest immediate
evidence.

## Recommendation

**Next experiment: accelerate the existing evaluator/runtime, not the
compiler.** Specifically, test whether the five validated native operations
can be emitted by the runtime emitter in place of its high-frequency call/
return/return-stack macros (and, separately, whether a small peephole change
removes frequent `LIT`/`@`/`!` sequences). Measure the same fixed, dynamic and
streaming workloads on the full interpreted decoder, with the 480/480 and 44/44
regressions and the same native-only methodology. This targets the measured
79–81% stack/memory/branch and 16–17% host work without a compiler coverage
project.

Selected-function compilation remains a useful later experiment, but only after
function attribution is available and the compiler can preserve the required
runtime semantics. Complete compilation should wait for a concrete application
that cannot be made fast enough on the optimized evaluator.

## Regression and limits

- B9's DEFLATE baseline remains **480/480** conformance, **44/44** unit tests,
  and frozen-S1 passes; B10 changes no runtime/decoder/library file.
- B10's fixed/dynamic/streaming outputs match zlib SHA-256; `make test` passes.
- PF instrumentation inflates operation counts; exact counts use the
  non-PF build.
- No function-name ranking, no per-opcode CPU-time attribution, no whole-app
  native-primitive result, and no browser/WASM timing.
- B10 is measurement only; no optimisation was implemented.
