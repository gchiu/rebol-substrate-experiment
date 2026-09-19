# FIB-OPT-P9 — Results

## 1. Headline

The dense-frame change reduces `fib 25` S1 instructions by only **0.94%**
(925,982,568 → 917,242,308) and produces **no measurable wall-clock
improvement**. This is essentially a **negative result** for the P9
hypothesis: the derived `>R`/`R>` frame pushes are not the bottleneck.

## 2. Instruction count

| opcode | P8 before | P9 after | Δ |
|---|---|---|---|
| LIT | 403,994,503 | 398,896,018 | −5,098,485 |
| DUP | 30,226,727 | 30,226,727 | 0 |
| DROP | 10,318,365 | 10,318,365 | 0 |
| `@` | 176,747,640 | 174,805,360 | −1,942,280 |
| `!` | 119,207,531 | 115,808,541 | −3,398,990 |
| 0BRANCH | 42,608,780 | 42,608,780 | 0 |
| HOST | 142,879,021 | 144,578,516 | **+1,699,495** |
| HALT | 1 | 1 | 0 |
| **total** | **925,982,568** | **917,242,308** | **−8,740,260 (−0.94%)** |

The fixed-offset stores remove ~43 opcodes/activation of LIT/`@`/`!` (the `>R`
RP re-read/re-write), but introduce ~7 HOST `ADD`/activation (address
arithmetic), so HOST *rose* — and at `-O2` a HOST call is comparatively
expensive, which is why wall-clock is a wash.

## 3. Amplification

| ratio | P8 before | P9 after |
|---|---|---|
| S1 instructions / fib call | 3814 | **3778** |
| S1 instructions / subexpression | 346.7 | **343.5** |
| HOST calls / fib call | 588.5 | 595.5 |

Evaluator events unchanged (calls 242,785, subexpr 2,670,637, etc.).

## 4. Frame-heavy benchmark

A second benchmark (pure recursion, `t: func [n] [ either < n 2 [ n ] [ + t - n 1 t - n 2 ] ]`,
`n = 25`, also 242,785 calls) isolates frame cost from fib's `<=` base case:

| benchmark | P8 | P9 | Δ |
|---|---|---|---|
| S1 instructions (fib 25) | 925,982,568 | 917,242,308 | −0.94% |
| S1 instructions (tree 25) | 936,179,538 | 927,439,278 | −0.93% |

The absolute saving is identical (8,740,260) — a fixed ~36 opcodes per
activation, independent of the workload, confirming the change is purely the
frame save/restore.

## 5. Wall-clock (`-O2`, same-session alternating A/B)

| round | P8 fib 25 median | P9 fib 25 median |
|---|---|---|
| 1 | 3.379 s | 3.231 s |
| 2 | 3.074 s | 3.167 s |
| 3 | 3.246 s | 3.318 s |

No clear improvement (within WSL frequency noise). Historical P8 `~3.17 s`
retained; P9 remains ≈ **3.2 s**.

## 6. Regression / frozen S1

- Full regression: **326 ok, 0 fail** (`-O2`).
- `./check-frozen-s1.sh`: passes (frozen files byte-identical).

This covers deep recursion, RETURN through nested frames, recursive same-site
activation, RAW escape, debugger inspection at depth, multitasking (yield +
resume + 1000 switches + task-stack overflow), GC, closures/promotion, frame
16-alignment, and multiple results.

## 7. Why the hypothesis failed

The derived `>R` (14 opcodes) re-reads and re-writes the memory-mapped RP, but
those are cheap LIT/`@`/`!` memory accesses and the RP is already the next
stack address. Replacing nine `>R` pushes with fixed-offset `STORE`s removes the
RP traffic but adds `HOST ADD` address arithmetic — a like-for-like swap that
nets ~0. So the answer to "why move the return-stack pointer nine times?" is:
**because the pointer is memory-mapped and already where it needs to be; moving
it is cheaper than computing nine explicit addresses.**

## 8. Retention decision

P9 is **retained** as a small, correct, semantics-preserving change (instruction
count falls 0.94%, all tests pass, frozen S1 untouched, layout unchanged). But
it is not a meaningful performance win.

## 9. Strategic conclusion (post-P9)

| metric | value |
|---|---|
| remaining S1 instructions (fib 25) | 917,242,308 |
| remaining instructions / activation | 3778 |
| remaining fib 25 time (`-O2`) | ≈ 3.2 s |
| remaining R3 ratio | ≈ 32× |

Cumulative interpreter-density work (P7 −7.5%, P8 −9.5%, P9 −0.9%) has taken the
count from 1,107M (P6) to 917M, with **diminishing returns**. The remaining
~3778 opcodes/activation are dominated by *fundamental interpretation
overhead*: tagged dispatch, memory-mapped register/cell access, and the
subroutine-call convention — not by any single removable idiom.

> Is another interpreter/evaluator density optimisation likely to change the
> performance class?

**No.** Further emitted-code density optimisations will yield single-digit
percent at most, leaving the interpreter still ≈ 30× off R3.

**Recommendation:** the next experiment should be to **compile the unchanged
frozen S1 stream** (ahead-of-time or JIT) rather than interpret each opcode.
This removes the `s1_run` switch dispatch and lets the seven frozen primitives
and the memory-mapped registers be lowered to native code/CPU registers — the
only remaining lever that changes the performance class. Not implemented here.
