# FIB-OPT-P6B — Compiler optimisation control experiment

## 1. Objective

P6 established that naive `fib 25` executes **~1.107 billion S1 opcode dispatches**
(~4560 per fib activation) and ~169.5 million HOST calls, and measured the
current baseline at `-O0 -g` at ~24 cycles/opcode. That baseline was compared
against a release-built Rebol3, so the ~90× gap mixed two factors:

1. Glon's architectural S1 instruction amplification (a property of the
   evaluator code we emit, not the C compiler);
2. an explicitly unoptimised C implementation of S1.

This control experiment measures factor 2 in isolation. **One variable only**:
the C compiler optimisation level (`-O0` / `-O1` / `-O2` / `-O3`). No change to
Glon semantics, S1 semantics, emitted evaluator code, opcodes, HOST behaviour,
dispatch strategy, or Fibonacci. No P7.

**Question answered:** how much of the present execution time is removed solely
by normal compiler optimisation?

## 2. Compiler and environment

| item | value |
|---|---|
| compiler | `cc (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0` (GCC 11.4.0) |
| CPU | AMD Ryzen 5 7535U (WSL2 VM; frequency variable, ~2.89 GHz observed) |
| source | post-P5/P6 runtime, unchanged |
| P6 commit | `3219e423640d40613d19daedea55821fc42ddadf` |
| branch | `fib-profile-p6b-compiler` |
| runtime | ordinary non-counting (`r0_s1_runtime.c` + frozen `s1.c`), **no** `-DR0_S1_PROFILE` |

`-g` was omitted from the performance builds (it only adds debug symbols and is
irrelevant to the one-variable `-O` question); all four levels are otherwise
identical. No `-flto`, `-march=native`, `-ffast-math`, `-fomit-frame-pointer`,
PGO, or any other optimisation variable.

## 3. Exact build commands

Benchmark binary (per level):

```sh
cc -std=c17 -Wall -Wextra -O<level> -o fib-p6b-bench \
    r0_s1_p6b_bench.c r0_s1_runtime.c s1.c
```

Regression binary (per level):

```sh
make CFLAGS="-std=c17 -Wall -Wextra -O<level>" s1
```

Binary sizes (benchmark executable):

| level | size |
|---|---|
| `-O0` | 68856 |
| `-O1` | 59416 |
| `-O2` | 62792 |
| `-O3` | 62680 |

Zero compiler warnings at every level.

## 4. Correctness and regression

All four levels produce identical Glon results:

```
fib 10 = 55 (ok), fib 15 = 610 (ok), fib 20 = 6765 (ok), fib 25 = 75025 (ok)
```

Full regression suite at each level:

| level | result |
|---|---|
| `-O0` | 326 ok, 0 fail — all tests passed |
| `-O1` | 326 ok, 0 fail — all tests passed |
| `-O2` | 326 ok, 0 fail — all tests passed |
| `-O3` | 326 ok, 0 fail — all tests passed |

No optimisation-dependent semantic failures.

## 5. Frozen-S1 guard

`./check-frozen-s1.sh` passes:

```
OK: S1 substrate frozen at f90496c26dc45c7a387d8fc8639cd2781507d3d2; all frozen files unchanged.
```

`fib 25` call count is unchanged by construction (the emitted evaluator code is
untouched; the C compiler cannot change it) — 242785.

## 6. Raw timings (evaluation only, monotonic clock, 1 warmup + 7 reps)

### fib 20

| level | raw (s) | median | min | max |
|---|---|---|---|---|
| `-O0` | 0.807070 0.814604 0.835272 0.793745 0.787292 0.899909 0.905893 | 0.814604 | 0.787292 | 0.905893 |
| `-O1` | 0.452219 0.446213 0.445957 0.439653 0.432429 0.433160 0.439008 | 0.439653 | 0.432429 | 0.452219 |
| `-O2` | 0.350573 0.355018 0.354926 0.354886 0.345231 0.350844 0.359876 | 0.354886 | 0.345231 | 0.359876 |
| `-O3` | 0.358381 0.362834 0.359695 0.366864 0.347409 0.353266 0.349644 | 0.358381 | 0.347409 | 0.366864 |

### fib 25

| level | raw (s) | median | min | max |
|---|---|---|---|---|
| `-O0` | 9.506487 8.426078 8.522230 8.511793 8.650994 8.644384 8.535897 | 8.535897 | 8.426078 | 9.506487 |
| `-O1` | 4.857783 4.908771 5.009146 4.874474 4.985297 5.066018 5.172673 | 4.985297 | 4.857783 | 5.172673 |
| `-O2` | 4.002624 4.297112 4.023354 3.977714 3.976299 4.013498 3.829557 | 4.002624 | 3.829557 | 4.297112 |
| `-O3` | 4.215036 4.084102 4.080436 3.893825 3.866206 3.867194 4.049787 | 4.049787 | 3.866206 | 4.215036 |

Note: this is a WSL2 VM with variable CPU frequency; run-to-run wall-clock
variance of ~10–20% is observed. The relative `-O0`-to-`-O2` comparison is
robust because all levels were timed back-to-back in the same session.

## 7. Headline results

| level | fib 20 median | fib 25 median | speedup vs `-O0` |
|---|---|---|---|
| `-O0` | 0.814604 s | 8.535897 s | 1.00× |
| `-O1` | 0.439653 s | 4.985297 s | 1.71× |
| `-O2` | 0.354886 s | 4.002624 s | 2.13× |
| `-O3` | 0.358381 s | 4.049787 s | 2.11× |

`-O2` and `-O3` are tied within noise (≤1.2%); `-O2` is marginally best. Normal
compiler optimisation alone removes roughly **half** of the `-O0` wall-clock
time for this workload.

## 8. Approximate per-opcode cost

Using the fixed architectural instruction count `1,107,100,190` (P6, unchanged
by `-O` level) and the observed ~2.89 GHz nominal frequency (variable → cycle
estimates are approximate):

| level | ns/opcode | cycles/opcode (~2.89 GHz) |
|---|---|---|
| `-O0` | 7.71 | 22.3 |
| `-O1` | 4.50 | 13.0 |
| `-O2` | 3.62 | 10.5 |
| `-O3` | 3.66 | 10.6 |

This is consistent with P6's independent `rdtsc`-derived estimate of ~24
cycles/opcode at `-O0` (P6 measured total-cycles/opcode including HOST overhead;
the wall-clock estimate here is slightly lower).

## 9. Rebol3 comparison (best build)

Best normal-compiler Glon result: **`-O2` fib 25 median ≈ 4.003 s**.

Local measured R3: **fib 25 ≈ 0.099 s** (same PC, same naïve workload).

```
Glon -O2 / R3  ≈  4.003 / 0.099  ≈  40.4×
```

The old gap of ~86× (`-O0` vs R3) is reduced by normal compilation to ~40×.
Scope of the claim: same-PC naïve recursive Fibonacci workload only; no
generalisation to overall language performance.

## 10. Interpretation

This is **Outcome B** (modest compiler speedup), not the extreme `9.0 s → 2.0 s`
of Outcome A, nor Outcome C.

- Compiler optimisation is real but bounded: ~2.1× (about half the `-O0` time).
- After normal optimisation, the dominant cost is unchanged: the ~1.1 billion
  S1 opcode dispatches (4560 per fib activation) driven by the memory-mapped
  register-access idiom (LIT 43.8% + `@` 19.3% + `!` 12.8% = 76%) and HOST
  arithmetic (15.3%).
- The old ~90× R3 gap did **overstate** the architectural gap — Glon was being
  measured as an unoptimised interpreter — but it remains ~40× even after the
  compiler is allowed to do its normal job. Instruction amplification, not
  debug-mode compilation, is the dominant remaining cost.

## 11. Optional assembly observations

(`objdump` of `s1_run` in the `-O0` and `-O2` benchmark binaries.)

- The opcode `switch` is compiled as an indirect jump (`jmp *%rax`) at **both**
  `-O0` and `-O2` — GCC already emits a jump table for the dense opcode switch,
  so the `-O2` gain is **not** from replacing a linear if/else chain with a
  jump table.
- `s1_run` grows from 745 bytes (`-O0`) to 1412 bytes (`-O2`).
- At `-O0`, `host` is a separate 2218-byte function called per HOST opcode; at
  `-O2`, `host` is inlined into `s1_run` (no separate symbol, a second indirect
  `jmp *%rax` is the inlined HOST-service switch).
- The dominant `-O2` win is register promotion: at `-O0` the S1 machine state
  (current opcode/operands in the `M[]` array) is memory-resident with frequent
  load/store; at `-O2` operands are held in registers.

These observations are recorded only; no source or assembly was edited.

## 12. Recommended next experiment

The compiler removes ~2.1×, leaving ~1.1B opcodes as the primary cost. The
single next hypothesis remains **P7: reduce the emitted S1 instruction count** —
fuse the LIT/`@`/`!` register-access idiom (the 76% of opcodes) so each fib
activation emits far fewer S1 instructions, measured from the new `-O2`
baseline of ~4.0 s. A separate, lower-leverage experiment is cheaper dispatch
(already partly quantified here as ~2.1×); it does not address amplification.

## 13. Files

- `r0_s1_p6b_bench.c` — reproducible timing harness (correctness + warmup + 7
  raw reps; non-counting runtime).
- `Makefile` — `fib-p6b-bench` target (`make CFLAGS="-std=c17 -Wall -Wextra -O2" fib-p6b-bench`).
- Frozen `s1.c` / `s1.h` / `tests.c` / `adversarial.c` / `claims.c` — untouched.
