# R3-FIB-COMPARISON.md — local Rebol3 vs Glon P5 (naive Fibonacci)

Same-PC measurement of the locally installed Rebol3 against the Glon P5
Fibonacci baseline. Benchmark/documentation only; neither runtime was modified
or tuned for Fibonacci.

## Executable

- Path: `/mnt/d/repos/tja-membership/dist/helper-test/r3.exe`
- Version: `Rebol/Bulk 3.22.1 (2026-05-27 19:58:00 UTC)` — Oldes' Rebol3
  (`https://github.com/Oldes/Rebol3`), not Ren-C/Red/Rebol2.
- Architecture: PE32+ x86-64, native **Windows** console executable (run from
  WSL2 via interop).

## Exact benchmark source (`benchmarks/r3-fib.reb`)

```rebol
fib: func [n] [
    either n <= 1 [
        n
    ][
        (fib (n - 1)) + (fib (n - 2))
    ]
]
```

Plain naïve recursion, no memoisation/iteration/native/caching. Parentheses
around `(n - 1)`/`(n - 2)` guarantee `fib(n-1)+fib(n-2)` under R3's left-to-right
argument evaluation (the task's `fib n - 1` would parse as `(fib n) - 1`).

Correctness verified: `fib 10 = 55`, `fib 15 = 610`, `fib 20 = 6765`,
`fib 25 = 75025`.

## Timing mechanism

R3 `now/precise` (millisecond resolution) around evaluation only:

```rebol
t0: now/precise
fib n
elapsed: to decimal! difference now/precise t0
```

Batch precision: `loop reps [fib n]` timed as one region, divided by `reps`.

## Environment

- OS: WSL2 (Linux 6.18.33.2-microsoft-standard-WSL2) on Windows; same physical
  PC for both sides.
- CPU: AMD Ryzen 5 7535U with Radeon Graphics (x86-64).
- AC power: online.
- **Cross-environment caveat**: R3 is a Windows-native process (via WSL interop);
  Glon runs as a native Linux/WSL2 process. Both on the same physical machine,
  but not the same OS process environment.

## Warmup / protocol

Warmup (`fib 25` twice) before timing. `fib 20`: 5 batches of 1000 calls.
`fib 25`: 7 individual timed calls, plus 5 batches of 100 calls.

## Results

### fib 20

R3 individual batches (per call): 0.006526, 0.008878, 0.008907, 0.009043, 0.008919 s
— **median 0.008907 s**, min 0.006526, max 0.009043.

### fib 25

R3 individual runs (7): 0.095, 0.099, 0.092, 0.090, 0.117, 0.103, 0.099 s
— **median 0.099 s**, min 0.090, max 0.117.

R3 batch per-call (5 × 100): 0.08709, 0.10514, 0.10487, 0.10198, 0.10338 s
— median 0.10338, min 0.08709, max 0.10514.

### Glon P5 (matched rerun, same session/machine)

| fib n | Glon P5 median |
|---|---|
| 10 | 0.006298 s |
| 15 | 0.068852 s |
| 20 | 0.805915 s |
| 25 | 9.013601 s |

## Comparison

| workload | Glon P5 | Rebol3 | Glon / R3 |
|---|---|---|---|
| fib 20 | ~0.806 s | ~0.0089 s | ~90× |
| fib 25 | ~9.01 s | ~0.099 s | ~91× |

(R3's fib 10/15 are sub-millisecond and dominated by ms-timer resolution, so the
headline is fib 20/25, where the ratio is consistent at ~90×.)

## Interpretation

On this PC, on this naïve recursive Fibonacci workload, Rebol3 (C-implemented
evaluator) executes roughly **90× faster** than Glon P5 (evaluator expressed in
emitted S1 running on the interpreted S1 stack machine). This is not a general
claim about either language: it is one workload, one PC, one timing method.

Caveats: R3 is Windows-native while Glon is WSL2-native; R3 timings sit at
millisecond resolution (a single `fib 25` is ~0.1 s, so ~1% timer noise); and
sustained batches are a few percent slower than isolated calls (R3 GC/memory
effects). The ~90× figure is therefore approximate but unambiguous in magnitude.
