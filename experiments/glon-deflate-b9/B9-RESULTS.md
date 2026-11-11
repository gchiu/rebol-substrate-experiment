# B9 results — twelve-primitive profile on DEFLATE

## Decision

**The twelve-primitive profile does not execute the complete DEFLATE decoder,
and no whole-decoder throughput claim is made.** The five proposed operations
are emitted and executed on the real DEFLATE kernel `hf/left`; that kernel is
faster and much smaller, but the full decoder remains compiler/runtime-limited.
The profile is not worth adopting for DEFLATE on this evidence alone.

## Phase 1 — seven-primitive baseline

Source: repository commit `efbfed4` (the F2-correction lineage; the F2 commit
`22c9c95`, C1 `9a1cf63` and B3 `08f213f` are ancestors). Environment: native x86_64 WSL2, 12 CPUs, cc 11.4.0,
`-O2`; normal host `experiments/glon-deflate/build/dzhost`; counting host
`experiments/glon-deflate-b9/build/dzhost-b9` linking `s1_prof.c`. Five runs
per fixture; medians and spreads are preserved in `baseline/results.json`.

| Workload | Compressed | Output | Run median | Spread | Decode-only* | Output KB/s | Executed VM ops | Host calls |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| fixed | 537 | 2,000 | 1,373.5 ms | 5.2% | 1,371 ms | 1.42 | 284,665,507 | 48,283,945 |
| dynamic | 1,684 | 9,000 | 6,528.3 ms | 11.8% | 6,518 ms | 1.35 | 1,237,258,721 | 204,257,987 |
| streaming | 1,818 | 9,000 | 9,608.6 ms | 15.9% | 9,600 ms | 0.91 | 2,098,178,800 | 331,915,025 |

\*Read plus feed host time from the STATS line; initialization is separate.
Library load was about 24–27 ms and the open dispatch about 2 ms. Peak live
managed cells were 4,288 / 4,464 / 4,832 of 7,232; `max_rs` was 2,865 / 3,617 /
3,548; emitted `code_end` was 14,549 cells throughout. The counting host is
used only for counters, never for the timing medians.

The **fixed** fixture uses one feed and a large read; **dynamic** uses 128-byte
feeds and 100-byte reads; **streaming** uses the same fragmented schedule on
the multi-memlevel stream. Output bytes match zlib SHA-256 for all runs. The native seven-primitive
baseline was reconfirmed before B9: **480/480** conformance, **44/44** unit
tests, and the frozen-S1 check passes (371 s conformance wall time).

## Phase 2 — the twelve-primitive implementation

B9 reuses B3's non-production machine implementation (frozen seven plus CALL,
RET, >R, R>, R@; the B3 BRANCH handler exists in the reused file but is never
emitted by B9). It is compiled with the C1 compiler, which provides the
existing `block-at` vertical slice. The B9 machine adds per-opcode counters for
the five operations; the timing build has those counters compiled out.

The compiler path is real: the C1 candidate flags select
`CALL|RET|>R|R>|R@`, and the generated code contains the corresponding
opcodes. A seven-op `none` build of the same source is the comparison. No
Fibonacci result is used.

## Phase 3 — application coverage

`./build/b9-coverage experiments/glon-deflate/inflate.glon` reports:

```
UNSUPPORTED: expected 'name: func [params] [body]'
```

The same is true of `app.glon`, prelude and strings. This is the expected
boundary, not a new discovery:

- already supported: functions, parameters, recursion, integer arithmetic and
  comparisons, `either`, calls; C1 adds `block-at`;
- lowerable with existing instructions: `block-at` (C1 does this with
  `LIT`/`HOST DIV`/`HOST ADD`/`@`), and some division via `HOST DIV`;
- retained host/runtime: managed strings and allocation, blocks, closures,
  loader/module wrappers, `do`/`judge`/`invoke`, `s/`, masm, GC and error
  propagation;
- genuine compiler gaps: set-words/local bindings, division in the full
  language surface, block representation/literals, string runtime, closure
  capture/invocation, SIN! non-local control, and module selection.

Building these is a compiler/runtime project, not a reason to rewrite DEFLATE
or add another VM opcode. B9 therefore stops at the verified boundary.

## Phase 4 — equivalence evidence

The only compiled DEFLATE workload is the unchanged `hf/left` kernel from C1.
Both settings run the ten independent vectors in `reference/hf_left.glon`:

- seven-op `none`: 10/10 results match the C reference; function code 279
  cells, total with trampoline 301 cells; stack balances pass;
- five-op candidate: 10/10 results match; function code 157 cells, total 160;
  stack balances pass;
- the production Glon path was checked in C1 against all ten vectors.

This is **kernel-only**. The full 480 conformance and 44 unit tests are
established for the interpreted seven-primitive DEFLATE experiment, not for a
compiled twelve-op decoder (which does not exist here).

## Phase 5 — benchmark

### Kernel (`hf/left`, ten vectors)

| Metric | Seven primitives | Twelve primitives | Ratio |
|---|---:|---:|---:|
| Executed instructions, aggregate | 17,644 | 8,960 | 1.97× fewer |
| Function code cells | 279 | 157 | 1.78× smaller |
| Total code cells incl. trampoline | 301 | 160 | 1.88× smaller |
| Sum of per-vector median exec times | 80.97 µs | 49.77 µs | 1.63× faster |
| Compile median | 70.42 µs | 122.32 µs | slower |
| Peak data / return stack | 6 / 64 | 6 / 64 | same |
| Fixed block storage | 16 cells | 16 cells | same |

The native-op counts across the ten vectors are:

| Operation | Executions |
|---|---:|
| CALL | 115 |
| RET | 115 |
| >R | 345 |
| R> | 345 |
| R@ | 317 |
| BRANCH | 0 |

All five are emitted and executed; BRANCH is not emitted. Native operations are
1,237 of 8,960 total instructions (13.8%), and remove 8,684 instructions
(49.2%) from the seven-op lowering. The timing is noisy (per-vector spreads
3–44% on long paths); the aggregate 1.63× is consistent in direction but is
not a precise constant.

### Full decoder

| Metric | Seven primitives | Twelve primitives |
|---|---:|---:|
| Executed instructions | measured above | **N/A: no complete compiled decoder** |
| Decode-only time | measured above | **N/A** |
| Throughput | 0.91–1.42 KB/s | **N/A** |
| Code cells | 14,549 emitted runtime | **N/A** |
| Runtime binary size | normal native host; B9 S1 profile 4,817 text B | B9 machine 5,505 text B (+688) |
| Peak memory | 4,288–4,832 live cells | kernel fixed 6/64 stacks, 16-cell block; full N/A |
| Host calls | 48M/204M/332M | kernel N/A; full N/A |

The B3 same-build comparison reported the native machine at 5,505 text bytes
versus 4,817 for its S1 copy (about +688); B9 reuses that machine (its fast
object is 5,505 text bytes, and the B9 kernel executable is 21,874 text
bytes including compiler and harness). The C1 compiler is 602 `.c` + 11 `.h`
lines (9,227 text bytes). These are experimental artifacts, not production
costs. Native against native was required first and was the only comparison
made; no browser/WASM timing is mixed in.

## Phase 6 — answers

1. **Does the twelve-primitive runtime execute the complete decoder?** No.
   The existing compiler stops at the complete-source boundary (module
   wrappers, locals, blocks/strings, closures, errors, host/GC).
2. **Which new primitives are emitted?** All five: CALL, RET, >R, R>, R@.
   BRANCH is not emitted.
3. **How frequently?** On `hf/left`, 115 CALL, 115 RET, 345 >R, 345 R>, 317 R@
   across ten vectors (1,237 native ops of 8,960).
4. **How much interpreter work do they eliminate?** 8,684 instructions
   (49.2%) in this kernel.
5. **Measured throughput improvement?** Kernel-only: 1.97× fewer
   instructions, 1.78× smaller function code, 1.63× faster in this noisy
   sample. No complete-decoder throughput improvement was measured.
6. **Additional runtime cost?** About 688 text bytes over the S1 machine copy,
   plus the compiler/vertical-slice infrastructure; not a WASM-qualified or
   production profile.
7. **Worth retaining for DEFLATE?** Not yet. It is promising for compiled
   kernels, but DEFLATE is blocked on compiler/runtime coverage, and B9 has no
   whole-decoder result. Retain as an optional future execution profile only
   after that coverage exists.

## Limits

- Kernel-only; no full 480/44 twelve-op result.
- Timings are native, WSL2, `-O2`, five repeats, and noisy; use the preserved
  JSON for individual runs.
- The reused B3 machine file contains an unused BRANCH handler; B9's emitted
  profile does not use it and no sixth opcode was added.
- No heap, GC, DEFLATE, frozen-S1 or production change.
- The deployed `/inflate/` page was not touched.
