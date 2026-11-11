# B9 — twelve-primitive execution-profile benchmark

This is an isolated B9 experiment. It does not modify frozen S1, production
Glon, the deployed `/inflate/` page, DEFLATE, the heap, or the GC.

## What is here

- `baseline/dzhost_b9.c` — a copy of the current native DEFLATE host with a
  counting build. Its only change is linking `s1_prof.c` and printing
  `B9_PROFILE` after the open dispatch. The timing runs use the normal
  `experiments/glon-deflate/build/dzhost`.
- `b9_machine.c` / `b9_machine.h` — the B3 experimental machine (frozen S1
  plus CALL, RET, >R, R>, R@, and the unused B3 BRANCH handler), reused for the
  twelve-primitive profile. The B9 compiler path never emits BRANCH.
- `b9_kernel.c`, `reference/` — C1's unchanged `hf/left` kernel harness and
  vectors, compiled with the C1 compiler at two settings: seven-op `none` and
  the five native operations.
- `bench_baseline.py`, `baseline/results.json`, `baseline/run.log` — preserved
  seven-primitive measurements.
- `baseline/kernel-*.json` — count and timing outputs for the only compiled
  DEFLATE workload (`hf/left`).
- `coverage.c`, `baseline/coverage-*.txt` — compiler-boundary probes.

## Commands

The normal native seven-primitive host must be built first:

```
make -C ../glon-deflate build/dzhost
```

Then, from this directory:

```
make
make baseline          # five repetitions per fixture; preserves JSON/log
make coverage          # try the complete decoder through the C1 compiler
make kernel            # seven vs five-op hf/left counts and timing
```

`B9_REPS` overrides the baseline repetition count (default 5).

The fixtures are copied from the F2 conformance corpus:

| Name | Compressed | Decompressed | Feed/read |
|---|---:|---:|---|
| fixed | 537 | 2,000 | one feed, large read |
| dynamic | 1,684 | 9,000 | 128-byte feeds, 100-byte reads |
| streaming | 1,818 | 9,000 | 128-byte feeds, 100-byte reads |

The streaming fixture is the multi-memlevel1 stream; the fixed and dynamic
fixtures are fixed-text and dynamic-text. The expected bytes are checked with
Python zlib in `bench_baseline.py`.

## Scope

The complete Glon DEFLATE decoder is **not compilable** by the existing
compiler. C1/B3 accept only the integer/function/either subset plus C1's
`block-at`; the decoder needs module wrappers, set-words, division, real
blocks/strings, closures, `do`/`judge`/`invoke`, `s/`, masm, errors and GC.
The B9 twelve-op benchmark is therefore **kernel-only** (`hf/left`). It does
not establish a whole-decoder speedup.

`hf/left` is a real unchanged DEFLATE function (Huffman length validation),
not a synthetic Fibonacci substitute.
