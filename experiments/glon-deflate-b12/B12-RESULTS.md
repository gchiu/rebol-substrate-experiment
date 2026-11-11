# B12 results — seven vs nine vs twelve primitives on whole DEFLATE

## Profile definitions and enforcement

All three profiles run the complete unchanged Glon DEFLATE decoder and the same
fixtures/build settings (`cc -O2`, native x86-64 WSL2).

| Profile | Native instructions | Return-stack handling |
|---|---|---|
| **S1-7** | none; frozen seven-primitive `s1.c` and original runtime | frozen assembler macros |
| **S1-9** | `CALL`, `RET` | `>R`, `R>`, `R@` remain seven-primitive assembler macros |
| **S1-12** | `CALL`, `RET`, `>R`, `R>`, `R@` | native |

S1-9 compiles the shared B12 machine with `B12_NATIVE_RS=0`; raw `>R`/`R>` use
the 14-cell macro size. S1-12 compiles with `B12_NATIVE_RS=1`; raw forms use the
one-cell native forms. The S1-9 machine rejects native `>R`/`R>`/`R@` opcodes
rather than silently executing them; `build/b12-guard-nine` demonstrates:

```
b12: native >R is not permitted in the nine-primitive profile
nine native >R guard: REJECTED
```

Thus the nine build genuinely restricts its native set; it does not merely omit
a flag from a twelve-capable machine without enforcement.

## Emission and execution evidence

The native instructions are emitted by the B12 runtime emitter and executed by
the machine. On the complete decoder, the count build reports:

| Profile | Total VM ops (fixed) | CALL | RET | >R | R> | R@ |
|---|---:|---:|---:|---:|---:|---:|
| S1-7 | 284,665,507 | 0 | 0 | 0 | 0 | 0 |
| S1-9 | 251,525,187 | 1,497,200 | 1,517,392 | 0 | 0 | 0 |
| S1-12 | 239,378,089 | 1,497,200 | 1,517,392 | 764,165 | 743,939 | 41,133 |

Dynamic and streaming have the same picture:

| Workload | Profile | Total ops | CALL | RET | >R | R> | R@ |
|---|---|---:|---:|---:|---:|---:|---:|
| dynamic | seven | 1,237,258,721 | 0 | 0 | 0 | 0 | 0 |
| dynamic | nine | 1,097,954,693 | 6,286,399 | 6,386,724 | 0 | 0 | 0 |
| dynamic | twelve | 1,038,022,077 | 6,286,399 | 6,386,724 | 3,776,579 | 3,675,688 | 157,240 |
| streaming | seven | 2,098,178,800 | 0 | 0 | 0 | 0 | 0 |
| streaming | nine | 1,885,767,358 | 9,589,466 | 9,733,785 | 0 | 0 | 0 |
| streaming | twelve | 1,765,902,992 | 9,589,466 | 9,733,785 | 7,538,275 | 7,393,086 | 206,739 |

The nine- and twelve-primitive profiles are **not** operationally identical:
S1-12 actually executes the three return-stack operations. B11 was therefore
effectively a twelve-primitive evaluator with all five native operations
enabled. B12 makes the distinction explicit.

The B10 seven-op categories still explain the bulk of the work: about 44–45%
literal/stack, 32–34% memory, 17% HOST and 6% branch. The native additions
remove call/return/return-stack macro work; they do not change the DEFLATE
algorithm.

## Correctness

Full suites were run for both experimental profiles:

| Profile | Conformance | Units | Frozen S1 |
|---|---:|---:|---:|
| S1-7 (B9/F2 baseline) | 480/480 | 44/44 | pass |
| S1-9 | 480/480 | 44/44 | pass |
| S1-12 | 480/480 | 44/44 | pass |

S1-9 and S1-12 outputs were compared byte-for-byte by the existing conformance
runner against Python zlib/the seven-primitive reference; all malformed,
truncated and window-limit classes match. The B12 unit runs exercise ordinary
calls, recursion, closures/dynamic invocation, errors and host interactions.
The streaming fixture exercises suspension/resumption. No tests were weakened
and no decoder source changed.

## Performance

Five repetitions per workload, medians (spread in parentheses), O2 builds:

| Workload | Seven | Nine | Twelve | Seven→Nine | Seven→Twelve | Nine→Twelve |
|---|---:|---:|---:|---:|---:|---:|
| fixed | 978.0 ms (9.5%) | 900.6 ms (9.7%) | 825.8 ms (5.9%) | 1.086× | 1.184× | 1.090× |
| dynamic | 4,226.8 ms (9.4%) | 3,961.0 ms (3.4%) | 3,910.1 ms (12.0%) | 1.067× | 1.081× | 1.013× |
| streaming | 6,308.5 ms (7.1%) | 5,850.6 ms (10.9%) | 5,589.8 ms (3.6%) | 1.078× | 1.129× | 1.047× |

The dynamic nine→twelve timing difference is inside the observed spread and is
not a reliable throughput claim by itself. The deterministic operation counts
show S1-12 executes **4.8% fewer overall VM operations** than S1-9 on fixed
(and 5.4%/6.4% on dynamic/streaming), and S1-12 emits **7% fewer code cells**
(9,933 vs 10,683). Those are genuine differences from actually executing the
return-stack operations, not merely a dispatch-label change.

Code and memory:

| Profile | Code cells | Host text bytes | Peak live cells | max_rs |
|---|---:|---:|---:|---:|
| S1-7 | 14,549 | 96,312 | 4,288 / 4,464 / 4,832 | 2,865 / 3,617 / 3,548 |
| S1-9 | 10,683 | 96,748 | same | same |
| S1-12 | 9,933 | 96,376 | same | same |

The host executable sizes differ by only hundreds of bytes and are not the
source of the gain. The gain is the emitted evaluator code and the operations it
removes. Initialization was separated in the STATS runs (library load/open);
timing includes feed/read decoding, not library startup or compilation.

The B11 O1 baseline is preserved separately; B12's comparison uses fresh O2
builds for all three profiles so optimization and harness overhead are
controlled.

## Nine vs twelve: meaningful?

**Yes.** S1-12 executes all three return-stack operations (millions of times)
and has lower operation count, lower code-cell count, and a consistent, though
noise-limited, timing advantage over S1-9. This is not the B9 kernel result being
extrapolated; it is measured on the full decoder.

S1-9 is still a useful minimal profile: it captures most of the benefit with
only CALL/RET, and is the appropriate fallback if adding return-stack opcodes
is undesirable for a target. But the evidence does not support calling nine and
twelve operationally equivalent.

## Recommendation

Use **S1-12** as the next optional experimental execution profile for the
evaluator, with S1-7 retained as the frozen reference and S1-9 available as the
minimal native-call variant. Validate S1-12 on the supported native deployment
target and keep the full 480/44 suite as the gate. Do not extend the Glon
compiler on this evidence; the measured gain is from evaluator code generation,
not from compiling DEFLATE.

## Limitations

- Experimental runtime/machine copies only; no production, frozen-S1, heap, GC,
  DEFLATE or deployed-page changes.
- No WASM port of the B12 machine.
- Timings are noisy; operation/code counts are deterministic.
- The profile's native return-stack forms change raw-masm size accounting; this
  is isolated to the B12 runtime copy and verified by the complete suite.
