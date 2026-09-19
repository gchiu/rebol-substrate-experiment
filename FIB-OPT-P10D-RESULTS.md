# FIB-OPT-P10D — Results

## 1. Headline

A single top-of-data-stack cache gives a **modest positive** speedup: **~1.32×**
over P10C. `fib 25` falls from ~0.41 s (P10C) to **~0.31 s** (P10D); the R3 gap
falls from ~3.8× to **~3.1×**.

## 2. Stack-memory access counts (static, generated C)

| metric | P10C | P10D |
|---|---|---|
| `M[sp]` references | 3,524 | 2,143 |
| `M[sp + 1]` references | 0 | 407 |
| `tos =` (local) | 0 | 4,464 |

P10D removes ~28% of the `M[sp]` references (the top value is now a local
`tos`). The remaining `M[sp]` traffic is the second-from-top cell, the return
stack (`M[rp]`), and the computed-goto dispatch.

## 3. Timing (`-O2`, same-session alternating, 8 rounds each)

| round | P10C median | P10D median | speedup |
|---|---|---|---|
| 1 | 0.4159 s | 0.3144 s | 1.32× |
| 2 | 0.4188 s | 0.3260 s | 1.29× |
| 3 | 0.3932 s | 0.2928 s | 1.34× |

Median speedup **≈ 1.32×**; P10D `fib 25` median ≈ **0.31 s**.

## 4. R3 comparison

```
P10D fib 25  ≈ 0.31 s
R3  fib 25    ≈ 0.099 s
ratio         ≈ 3.1×
```

## 5. Correctness

8 workloads (fib, tree, raw, non-local return, closure capture, arithmetic edge
cases incl. negative division, arithmetic negative, and a generic-HOST `print`)
all run identically through interpreted, P10C and P10D. Complete interpreted
suite: **326 ok, 0 fail**; `./check-frozen-s1.sh` passes.

## 6. Interpretation

**Modest positive.** Stack-cell traffic is a real but partial component of the
remaining cost. The top-cell cache removed ~28% of data-stack cell accesses and
~25% of wall-clock, leaving ~3.1× R3. The residual is the second-from-top cell
(`M[sp]`), the return stack (`M[rp]`), the arithmetic, and the per-instruction
computed-goto (`goto *T[ip-256]` — ~917M indirect jumps).

## 7. Recommendation

Since single-cell caching is positive but diminishing, the next isolated
experiment should measure **P10E: two-cell (TOS/NOS) caching** to see whether
removing the second-from-top cell traffic yields a comparable further gain —
**and** separately measure **computed-goto elimination** (direct fall-through
for sequential S1), which is likely a comparable or larger remaining cost given
the ~917M indirect jumps. Both are separate experiments.
