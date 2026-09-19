# FIB-OPT-P10C — Results

## 1. Headline

Lowering the 12 pure arithmetic/comparison HOST operations inline into the
compiled-S1 path gives **no speedup (~1.0×)** over P10B. The generic HOST
dispatcher was not the remaining bottleneck — the cost is inside the operations
and the data-stack cell traffic, which are identical before and after.

## 2. HOST distribution and classification

See `FIB-OPT-P10C-HOST.md`. 100% of fib's 142.9M HOST calls are pure
arithmetic/comparison (`ADD` 39.6%, `EQ` 19.6%, `SUB` 13.4%, `MOD` 8.2%,
`DIV` 5.3%, `GE` 3.7%, `MUL` 3.6%, `LT` 3.2%, `GT` 2.1%, `NE` 1.1%, `LE` 0.2%).

## 3. Selection

All 12 class-A operations (arithmetic/comparison) are lowered inline.
`ALLOC` (class B), `PUTCHAR`/`PRINT`/`DUMP` (class C) remain generic HOST.

## 4. Lowering

Verbatim transcription of each frozen `host()` case, using the P10B local `sp`
(`M[sp++]`/`M[--sp]`), at the `HOST id` site. Semantic equivalence is by
construction (same C). The S1 stream still contains `HOST`.

## 5. Correctness

7 workloads run identically through interpreted, P10B, and P10C:

| program | interp | P10B | P10C |
|---|---|---|---|
| fib 25 | 75025 | 75025 | 75025 |
| tree 25 | 75025 | 75025 | 75025 |
| raw add2 | 7 | 7 | 7 |
| non-local return | 42 | 42 | 42 |
| closure capture | 5 | 5 | 5 |
| arith edge cases (incl. `-7 / 2`) | 10 results | 10 | 10 |
| arith negative | 4 results | 4 | 4 |

Complete interpreted suite: **326 ok, 0 fail**; `./check-frozen-s1.sh` passes.

## 6. Timing (`-O2`, same-session alternating, 8 rounds each)

| round | P10B median | P10C median | speedup |
|---|---|---|---|
| 1 | 0.3701 s | 0.3753 s | 0.99× |
| 2 | 0.3702 s | 0.3788 s | 0.98× |
| 3 | 0.4438 s | 0.3818 s | 1.16× |

Median speedup **≈ 1.0×** (within noise). P10B/P10C both ≈ **0.38 s**.

## 7. HOST accounting

| quantity | count |
|---|---|
| logical HOST operations (fib 25) | 142,879,021 |
| intrinsically lowered | 142,879,021 (all pure) |
| generic HOST dispatcher calls | 0 in fib (would be ALLOC/PRINT/DUMP only) |

## 8. R3 comparison

```
P10C fib 25  ≈ 0.38 s
R3  fib 25    ≈ 0.099 s
ratio         ≈ 3.8×
```

(unchanged from P10B ≈ 4.3×; the small difference is frequency noise.)

## 9. Post-P10C profiling / dominant remaining cost

With HOST dispatch shown to be negligible, the remaining ~3.8× is dominated by:

1. **data-stack cell traffic** — every `push`/`pop` is `M[--sp]`/`M[sp++]`
   (the data stack itself is still memory);
2. **return-stack cell traffic** — `M[rp]` in `CALL`/`EXIT`/`>R`/`R>`;
3. **the actual arithmetic** (a+b, a-b, …);
4. **computed-goto dispatch** (`goto *T[ip-256]`, an indirect jump per S1
   instruction);
5. **memory/cache behaviour**.

## 10. Interpretation

**Small win (≈ none).** The generic HOST dispatcher was already cheap (a small
static function + jump table; the measured `host()` is ~577 bytes and not the
hot cost). The remaining cost is the data/return-stack cell memory traffic and
the per-instruction computed-goto, not the HOST switch.

## 11. Recommendation

P10D should target **data-stack (and return-stack) cell traffic** — e.g. keeping
the top data-stack cell in a C local/register and only spilling at genuine
boundaries — or **computed-goto elimination** (direct fall-through for
sequential S1). Both are separate experiments and are not implemented here.
