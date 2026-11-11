# B11 results — native call/return in the existing evaluator

## Decision

A **working experimental evaluator** was built. Native `CALL`, `RET`, `>R`,
`R>` and `R@` in the runtime-generated evaluator reduce the complete DEFLATE
decoder's executed operations by **15.8–16.1%**, cut emitted code from 14,549
to 9,933 cells, and improve end-to-end median decode time by **1.14–1.20×** on
the fixed, dynamic and streaming workloads. All 480 conformance runs and all 44
unit tests pass. This is a verified whole-decoder experimental result, not a
kernel result.

## Phase 1 — feasibility and calling conventions

The runtime emitter (`r0_s1_runtime.c`) uses generated S1 sequences in four
places:

1. **Static evaluator-routine calls.** GC mark/scan/trace helpers, allocation,
   lookup, error helpers and compiler-runtime routines call one another with
   `asm_call`/`asm_exit`. These are statically known labels and have the same
   continuation/return-stack convention as B9’s native CALL/RET.
2. **Dynamic Glon function invocation.** `emit_invoke_closure` uses
   `asm_call`/`asm_exit` for closure activations. Native CALL/RET preserves the
   exact return address and RP layout, so closure invocation is unchanged.
3. **Closure/environment handling.** Context/frame allocation and lexical
   lookup remain in the existing runtime code. B11 does not change context,
   frame, capture or GC layout; only the call/return instructions used by that
   code change.
4. **Host callbacks and continuations.** HOST arithmetic, allocation, I/O and
   `r0_s1_g1a_live` dispatch are unchanged. The `asm_call`/`asm_exit` pairs
   around them still leave the same return stack entries.

B10 showed that the evaluator's call/return and return-stack macros account for
a large share of the 284M–2.10B operations and that call-heavy code benefits
strongly (B9 kernel: 1.97× fewer instructions). B11 therefore targeted the
bounded change: replace the emitted macro sequences with B9's already tested
native instructions. The original seven-primitive `s1.c` and production runtime
remain untouched.

The masm parser also emits `CALL`/`EXIT` and `>R`/`R>`/`R@` forms. Their native
encodings are shorter, so the experimental runtime copy adjusts the parser's
exact-size accounting to 2 cells for CALL, 1 for RET/EXIT and 1 for the
return-stack forms. This is an experimental runtime change only.

## Implementation

All changes are under `experiments/glon-deflate-b11/`:

- `b11_machine.c`/`b11_machine.h` — a copy of the tested B3/B9 experimental
  machine. `asm_call` emits native CALL (2 cells), `asm_exit` emits native RET
  (1 cell), and `asm_toR`/`asm_fromR`/`asm_fetchR` emit the tested native
  return-stack operations. The original frozen seven-op path is the control.
- `b11_runtime.c` — a copy of `r0_s1_runtime.c` with only:
  - the b11 header include;
  - native `emit_call_fwd`;
  - raw masm size accounting for the shorter CALL/EXIT/>R/R> forms.
  No Glon, DEFLATE, library, heap, GC or error semantics changed.
- `b11_host.c` — a copy of the existing `dzhost.c` with optional counters and
  B11 `CALL`/`RET`/return-stack counters. The normal build has no counters.
- `build/glon-b11` — a CLI linked with the experimental runtime for unit tests.
- `bench_b11.py`, `baseline/b11-results.json` — preserved measurements.
- `fixtures/` — the B9 fixed/dynamic/streaming fixtures.

## Correctness

Using the final all-five native profile:

```
python3 -I tests/run_tests.py --host ../glon-deflate-b11/build/dzhost-b11
  480/480 runs pass (251 s final all-five run; 277 s earlier run)
  164 fixtures; 222 malformed, 230 ok, 24 truncated, 4 window-limit
  all four original schedules plus eager/c2 classes pass
```

The B11 CLI run of the existing unit suite:

```
glon-b11 ... tests/unit-tests.glon
  unit tests: 44 passed, 0 failed
```

The outputs are compared byte-for-byte with the original seven-primitive
`dzhost`/zlib reference by the conformance runner. The three benchmark outputs
also match Python zlib. `./check-frozen-s1.sh` passes. This covers ordinary
calls, nested calls/recursion, closures and capture via the runtime tests, dynamic
invocation, error propagation, host interactions, suspension/streaming state and
the complete 480-case decoder corpus.

## Performance methodology

Both profiles were built with `cc -O2`, the same source tree and the same
fixtures. The control is a fresh **seven-primitive `dzhost` O2 build** in
`build/dzhost-seven-o2`; the experimental binary is `build/dzhost-b11` built
with the same flags. B9’s preserved baseline is retained in
`baseline/b9-baseline.json`, but its O1 timing is not mixed into this O2 table.
Five repetitions per fixture; medians and spreads are in
`baseline/b11-results.json`.

| Workload | Seven time | B11 time | Speedup | Seven ops | B11 ops | Op reduction | Seven code | B11 code |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| fixed | 933.8 ms | 820.1 ms | 1.139× | 284,665,507 | 239,378,089 | 15.9% | 14,549 | 9,933 |
| dynamic | 3,933.1 ms | 3,431.7 ms | 1.146× | 1,237,258,721 | 1,038,022,077 | 16.1% | 14,549 | 9,933 |
| streaming | 6,406.2 ms | 5,322.5 ms | 1.204× | 2,098,178,800 | 1,765,902,992 | 15.8% | 14,549 | 9,933 |

Timing spreads were 0.8–4.9% for the O2 seven/B11 pairs (individual values in
JSON). Decode-only read+feed time tracked whole run time; initialization
(library load/open) is separate and was not included in the comparisons. Peak
live cells and `max_rs` were unchanged: 4,288/2,865 (fixed), 4,464/3,617
(dynamic), 4,832/3,548 (streaming). The B11 host executable is about the same
size as the seven host (96,376 vs 96,312 text bytes); the gain is in emitted
code, not host binary size.

Native operation counts on the three workloads:

| Op | fixed | dynamic | streaming |
|---|---:|---:|---:|
| CALL | 1,497,200 | 6,286,399 | 9,589,466 |
| RET | 1,517,392 | 6,386,724 | 9,733,785 |
| >R | 764,165 | 3,776,579 | 7,538,275 |
| R> | 743,939 | 3,675,688 | 7,393,086 |
| R@ | 41,133 | 157,240 | 206,739 |

The native profile is emitted and executed; these are not just handlers added
to the machine. The complete B10 primitive mix is retained as the baseline
context: 44–45% literal/stack, 32–34% memory, 16–17% host, 6.2–6.5% branch.
B11 removes part of the evaluator's call/return/return-stack macro work; it does
not change literal/memory decoding sequences.

## Limitations

- This is an experimental runtime *copy*; it is not production and no deployed
  page or production ABI was changed.
- B11 native return-stack operations are used by the evaluator and masm forms;
  all 480/44 tests pass, but the experimental machine has no WASM port.
- The B11 runtime copy adjusts masm size accounting; this is necessary because
  native encodings replace the frozen macro lengths.
- No per-op CPU-cycle attribution; instruction counts are deterministic, time
  is end-to-end and modestly variable.
- The 1.14–1.20× result is for this complete decoder and these workloads, not a
  universal Glon speedup. It must not be extrapolated to arbitrary programs.

## Recommendation

**Retain native evaluator call/return as a verified experimental direction.**
The next bounded step is to make the B11 runtime emitter a selectable
experimental execution profile with the seven-primitive path as the default
control, then re-run the full conformance/unit suite and the same three
workloads on the supported native deployment target. Do not expand the Glon
compiler in this phase; the measured gain comes from evaluator code generation,
not from compiling DEFLATE.
