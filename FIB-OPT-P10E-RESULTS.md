# FIB-OPT-P10E — Results

## 1. Headline

Two-cell (TOS + NOS) caching gives a **small additional gain**: **~1.08×** over
P10D. `fib 25` falls from ~0.31 s (P10D) to **~0.29 s** (P10E); the R3 gap
falls from ~3.1× to **~2.9×**.

## 2. Stack-memory traffic

| metric | P10D | P10E |
|---|---|---|
| `M[sp]` references | 2,143 | 2,160 |
| `M[sp + 1]` references | 407 | 407 |
| `tos =` (local) | 4,464 | 4,464 |
| `nos =` (local) | 0 | 3,824 |

The number of memory references is essentially unchanged; NOS caching moves the
second operand (`a`) from `M[sp]` (memory) into the `nos` local and shifts the
memory read to the *refill* (`nos = M[sp++]`), which is off the arithmetic's
critical path. The gain is therefore **memory-latency hiding**, not a reduction
in access count.

## 3. Timing (`-O2`, same-session alternating, 8 rounds each)

| round | P10D median | P10E median | speedup |
|---|---|---|---|
| 1 | 0.3293 s | 0.2953 s | 1.12× |
| 2 | 0.3572 s | 0.3297 s | 1.08× |
| 3 | 0.3132 s | 0.2905 s | 1.08× |

Median speedup **≈ 1.08×**; P10E `fib 25` median ≈ **0.29 s**.

## 4. R3 comparison

```
P10E fib 25  ≈ 0.29 s
R3  fib 25    ≈ 0.099 s
ratio         ≈ 2.9×
```

## 5. Correctness

8 workloads (fib, tree, raw, non-local return, closure capture, arithmetic edge
cases incl. negative division, arithmetic negative, generic-HOST print) all run
identically through interpreted, P10D and P10E. Complete interpreted suite:
**326 ok, 0 fail**; `./check-frozen-s1.sh` passes.

## 6. Interpretation

**Small gain.** TOS caching captured most of the useful stack locality (1.32×);
NOS adds only ~8%, so deeper stack caching is not worth pursuing. The remaining
~2.9× R3 gap is dominated by the per-instruction **computed-goto dispatch**
(`goto *T[ip-256]`, ~917M indirect jumps) and the remaining `M[sp]`/`M[rp]`
memory traffic plus the arithmetic.

## 7. Recommendation

Do **not** proceed to three-cell caching. The next isolated experiment should
measure **computed-goto dispatch elimination** — e.g. direct fall-through for
sequential S1 instructions, so only actual branch/call/return sites pay the
indirect jump — as this is the most likely dominant remaining cost. This is a
separate experiment.
