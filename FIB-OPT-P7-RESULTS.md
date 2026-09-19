# FIB-OPT-P7 — Results

## 1. Headline

`fib 25` total S1 instructions fell **7.54%** (1,107,100,190 → 1,023,582,152),
with every evaluator event counter unchanged. Wall-clock at `-O2` improved by
~**1.35×** in a same-session A/B, because the removed instructions were
disproportionately branch- and memory-heavy. `./check-frozen-s1.sh` passes and
326/326 tests pass.

## 2. Instruction count

| opcode | P6B before | P7 after | Δ | Δ% |
|---|---|---|---|---|
| LIT | 484,599,132 | 449,273,914 | −35,325,218 | −7.3% |
| DUP | 33,382,927 | 31,562,042 | −1,820,885 | −5.5% |
| DROP | 13,838,746 | 11,653,680 | −2,185,066 | −15.8% |
| `@` | 213,286,788 | 194,349,556 | −18,937,232 | −8.9% |
| `!` | 142,150,715 | 132,439,315 | −9,711,400 | −6.8% |
| 0BRANCH | 50,377,901 | 45,036,632 | −5,341,269 | −10.6% |
| HOST | 169,463,980 | 159,267,012 | −10,196,968 | −6.0% |
| HALT | 1 | 1 | 0 | — |
| **total** | **1,107,100,190** | **1,023,582,152** | **−83,518,038** | **−7.54%** |

`LIT + @ + !` fell from ~840M (76.0%) to ~776M (75.8%) — the three together are
down ~63.9M ops. `0BRANCH` fell 10.6% and `HOST` 6.0%.

## 3. Amplification (the metric that matters)

| ratio | P6B before | P7 after |
|---|---|---|
| S1 instructions / fib call | 4560 | **4216** |
| S1 instructions / subexpression | 414.6 | **383.3** |
| HOST calls / fib call | 698 | **656** |

The evaluator event counts are identical before and after (proof that the same
work is done, just denser):

```
calls 242,785   subexpr 2,670,637   blkeval 485,572   lookups 1,092,531
lex-direct 606,962   hash-probes 2,185,061   natives 849,746
allocs 1   max-depth 25
```

## 4. Wall-clock timing (`-O2`, non-counting runtime)

Same-session A/B (baseline `81b7d6d` vs P7), 7 reps each, 3 alternating rounds:

| build | fib 20 median | fib 25 median (min–max) |
|---|---|---|
| P6B baseline | 0.420 s | 4.97 s (4.69–5.63) |
| P7 | 0.324 s | 3.58 s (3.49–4.16) |
| speedup | 1.30× | **1.37×** |

The timing gain (≈27–37%) exceeds the instruction-count gain (7.5%). This is
expected and not a contradiction: the removed instructions were
disproportionately expensive at `-O2` — the tail-call elimination removed one
indirect `jmp *%rax` (through the `switch` jump table) per value dispatch, and
`0BRANCH` fell 10.6%; the dense `reduce` short-circuits the `N == 1` case and
removes `HOST` `DIV`/`MOD`/`SUB` plus load/store round-trips from the hottest
argument-evaluation path. Cycle estimates are approximate (WSL variable
frequency); wall-clock is primary.

## 5. Rebol3 comparison (narrow)

Same PC, same naïve recursive Fibonacci workload:

```
P7 -O2 fib 25  ≈ 3.58 s
R3  fib 25      ≈ 0.099 s
ratio           ≈ 36×
```

(was ≈ 86× at `-O0`, ≈ 40× at `-O2` before P7). No general language-performance
claim.

## 6. Interpretation

- The hypothesis holds: the evaluator can say the same thing in ~7.5% fewer S1
  words with the frozen substrate untouched, and the instruction amplification
  per fib call fell from 4560 to 4216.
- The reduction is **modest but real** and comes from denser evaluator
  expression (tail-jumps, single-value fast paths, fewer failed tag
  comparisons), not from backend fusion or new opcodes.
- The *dominant* remaining costs are unchanged and structural: `lookup` (~23%),
  `invoke_closure` (~17%), `subexpr` dispatch (~16%), `native` dispatch (~15%),
  `append`+`mkctx` (~11%). These are the tagged-dispatch + memory-mapped-machine
  fundamentals, not the call/reduce overhead P7 removed.

## 7. Should P7 be retained?

Yes. It is a small, safe, semantics-preserving density win with no downside: the
frozen S1 is untouched, every event counter is identical, and 326 tests pass.
The one test change (`9000` → `SCRATCH_A`) fixes a latent address-collision bug
in a non-frozen test, unrelated to the evaluator semantics.

## 8. Recommended next experiment

The evidence now points clearly at the **activation/binding path**, not
dispatch micro-overhead. `invoke_closure` + `append` + `mkctx` together are
~28% of execution, dominated by (a) the per-invocation 16-slot hash-index
zeroing in `mkctx`, (b) the 9-field frame push/pop, and (c) the parameter-bind
`append`+`hash_insert`. Next hypothesis (P8): **make child-context creation and
the parameter-binding fast path cheaper** (e.g. amortise or lazy-init the hash
index, and shrink the frame save/restore), measured again by instruction count
per fib activation — without redesigning contexts or function representation.
