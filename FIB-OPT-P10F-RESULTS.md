# FIB-OPT-P10F — Results

## 1. Headline

Dispatch elimination is a **near-negative result**: the static dispatch sites
collapse ~92% (4,519 → 354), but `fib 25` runtime improves only **~1.05×**
(0.30 s → 0.29 s). The computed goto was already well predicted by the CPU's
branch predictor for sequential flow, so it was not the dominant remaining cost.

## 2. Dispatch counts

| metric | P10E | P10F |
|---|---|---|
| static `goto *T[ip-256]` sites | 4,519 | 354 |
| remaining sites | — | 0BRANCH (142) + `LIT 0 !` branch (211) + 1 entry dispatch |

Dynamic: ~917M indirect jumps (one per executed instruction) → ~70M (only the
`0BRANCH` ≈ 45M and the derived branch/CALL/EXIT ≈ 25M), a **~92% reduction**.

## 3. Timing (`-O2`, same-session alternating, 8 rounds each)

| round | P10E median | P10F median | speedup |
|---|---|---|---|
| 1 | 0.3060 s | 0.3032 s | 1.01× |
| 2 | 0.3032 s | 0.2875 s | 1.05× |
| 3 | 0.2965 s | 0.2822 s | 1.05× |

Median speedup **≈ 1.05×**; P10F `fib 25` median ≈ **0.29 s**.

## 4. R3 comparison

```
P10F fib 25  ≈ 0.29 s
R3  fib 25    ≈ 0.099 s
ratio         ≈ 2.9×
```

(essentially unchanged from P10E).

## 5. Correctness

8 workloads (fib, tree, raw, non-local return, closure capture, arithmetic edge
cases incl. negative division, arithmetic negative, generic-HOST print) all run
identically through interpreted, P10E and P10F. Complete interpreted suite:
**326 ok, 0 fail**; `./check-frozen-s1.sh` passes.

## 6. Interpretation

**Dispatch disappears but speed barely changes** — the important negative result
the task anticipated. A ~92% reduction in indirect dispatches yields ~5%
runtime, so the computed-goto dispatch was *not* a major remaining cost; the
CPU branch predictor was already absorbing it for sequential flow.

This means the remaining ~2.9× R3 gap is now clearly dominated by **the work
inside the operations and the remaining stack-cell memory traffic** — i.e. the
`M[sp]`/`M[rp]` data/return-stack accesses and the arithmetic — rather than
control transfer. The compiled-S1 backend has reached diminishing returns on
each independent micro-optimisation axis.

## 7. Recommendation

Do not continue automatic micro-optimisation. The compiled-S1 path has now
isolated: dispatch (~2.4×, P10A), register traffic (~5×, P10B), HOST dispatch
(~1.0×, P10C), TOS (~1.32×, P10D), NOS (~1.08×, P10E), and dispatch again
(~1.05×, P10F). The residual ~2.9× is spread across the remaining `M[sp]`/
`M[rp]` memory accesses and the arithmetic, with no single dominant lever left.
The next step should be a **measured re-profiling** of the compiled backend
(e.g. `perf`/sampling on the generated code) rather than another hypothesised
micro-optimisation; a fundamentally different approach (native register-based
code generation beyond the memory-mapped S1 model) is the only remaining class
of change, and that is a larger architectural decision.
