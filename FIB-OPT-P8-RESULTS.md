# FIB-OPT-P8 — Results

## 1. Headline

`fib 25` total S1 instructions fell **9.5%** (1,023,582,152 → 925,982,568),
driven entirely by removing the eager activation hash-index work. Every
evaluator event counter (calls, subexpr, blkeval, natives, allocs, max-depth) is
unchanged; the only difference is that the child contexts now resolve lookups by
linear scan instead of the hash. `./check-frozen-s1.sh` passes and 326/326 tests
pass.

## 2. Instruction count

| opcode | P7 before | P8 after | Δ | Δ% |
|---|---|---|---|---|
| LIT | 449,273,914 | 403,994,503 | −45,279,411 | −10.1% |
| DUP | 31,562,042 | 30,226,727 | −1,335,315 | −4.2% |
| DROP | 11,653,680 | 10,318,365 | −1,335,315 | −11.5% |
| `@` | 194,349,556 | 176,747,640 | −17,601,916 | −9.1% |
| `!` | 132,439,315 | 119,207,531 | −13,231,784 | −10.0% |
| 0BRANCH | 45,036,632 | 42,608,780 | −2,427,852 | −5.4% |
| HOST | 159,267,012 | 142,879,021 | −16,387,991 | −10.3% |
| HALT | 1 | 1 | 0 | — |
| **total** | **1,023,582,152** | **925,982,568** | **−97,599,584** | **−9.5%** |

`LIT + @ + !` fell from ~776M to ~700M (−76M). `HOST` fell 10.3% (the zeroing
loop's address arithmetic is gone).

## 3. Hash activity (the direct evidence)

| counter | P7 before | P8 after |
|---|---|---|
| hash-probes | 2,185,061 | 1,092,531 |
| hash-hits | 1,092,531 | 1,092,531 |
| hash-misses | 1,092,530 | 0 |
| hash fallback (linear scans) | 0 | 1,092,530 |

The ~1.09M child-context hash probes (all misses) became ~1.09M linear scans of
a single slot, and the per-activation hash zeroing + `hash_insert` disappeared.
The global context still uses the hash (1,092,531 hits, unchanged).

## 4. Amplification

| ratio | P7 before | P8 after |
|---|---|---|
| S1 instructions / fib call | 4216 | **3814** |
| S1 instructions / subexpression | 383.3 | **346.7** |
| HOST calls / fib call | 656 | **588.5** |

Evaluator events unchanged (same work, denser):

```
calls 242,785   subexpr 2,670,637   blkeval 485,572   lookups 1,092,531
lex-direct 606,962   natives 849,746   allocs 1   max-depth 25
```

## 5. Wall-clock (`-O2`, non-counting, same-session A/B)

3 alternating rounds, 7 reps each:

| build | fib 20 median | fib 25 median (min–max) |
|---|---|---|
| P7 baseline (`79a1be6`) | 0.312 s | 3.69 s (3.57–4.14) |
| P8 | 0.280 s | 3.17 s (3.08–3.20) |
| speedup | 1.11× | **1.16×** |

As with P7, the timing gain (~16%) exceeds the instruction-count gain (9.5%)
because the removed code was HOST-heavy address arithmetic (`ADD`/`MUL`/`MOD`
for bucket indexing) and 16 stores per activation. Wall-clock is primary; cycle
estimates are approximate (WSL variable frequency).

## 6. Rebol3 comparison (narrow)

Same PC, same naïve recursive Fibonacci workload:

```
P8 -O2 fib 25  ≈ 3.17 s
R3  fib 25      ≈ 0.099 s
ratio           ≈ 32×
```

(was ≈ 86× at `-O0`, ≈ 40× at P6B `-O2`, ≈ 36× at P7 `-O2`). No general
language-performance claim.

## 7. Interpretation and concerns

- The hypothesis is **confirmed and worth retaining**: the eager, fully-general
  hash index is unnecessary for the overwhelmingly common small activation.
  Making it threshold-lazy removes ~97.6M S1 instructions (9.5%) with no
  semantic change.
- The remaining activation cost is now the context allocation itself (parent/
  count/cap writes + 16-alignment + return-address relocation), the 9-field
  frame push/pop, and the parameter `append` (word/value slot writes), none of
  which is the hash. The lazy split proves the "cheap lexical storage + general
  machinery only when needed" idea is viable, but the next win is elsewhere.
- No correctness or observability regression found: 326 tests (debugger, RAW,
  escape, multitasking, GC, closures, M3C/M3D) pass, and the hash is
  re-materialised from the authoritative `(word,value)` pairs if a small context
  is later promoted or grown past `HASH_MIN`.

## 8. Recommended next experiment

The activation path still spends its time on: (a) the 9-field frame save/restore
(push/pop via the frozen 9-op `>R`/`R>` per field) and (b) the per-parameter
`append` slot writes. P9 hypothesis: **reduce the frame save/restore cost** — for
example by computing the frame base once and storing the fields at fixed offsets
(one RP adjustment instead of nine), or by shrinking the saved-state footprint —
measured again by S1 instructions per fib activation. Do not redesign contexts
or function representation.
