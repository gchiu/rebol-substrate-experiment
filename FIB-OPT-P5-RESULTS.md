# FIB-OPT-P5-RESULTS.md — hash-indexed dynamic lookup

Phase P5 of the Fibonacci optimisation experiment: replace linear dynamic name
scanning with a per-context hash index mapping symbol → slot.

## Classification of remaining P4 lookups (done first)

Every remaining dynamic lookup in `fib 25` was a **global** reference — `either`,
`<=`, `+`, `-`, `fib` — all in the single global context (14 bindings, cap 48).
The ~11M slot examinations were entirely the linear scan of that context (plus
the 1-binding child-context miss). No dynamic-local, reserved-keyword or
forward-reference lookups exist in the workload.

## Architecture

- Context layout extended: `[parent, count, cap, (word,value)*cap, hash[cap]]`.
- Hash key = the interned **word id** (`mk_word(id)`); no re-hashing of spelling.
- `hash[i] = word_id*256 + slot` (occupied) or `-1` (empty); `h(id) = id % cap`.
- Open addressing, linear probing; no tombstones (Glon never removes bindings).
- `r_lookup` probes the hash when `count < cap`; otherwise it falls back to the
  ordered linear scan (correct for a full index / pre-existing global overflow).
- `r_append` and `r_set`'s append path insert into the index; promotion copies
  it; `(word,value)` pairs remain authoritative for storage, rebinding, the
  debugger and RAW.

## Symbol representation

Interner symbols (`syms[]`/`nsyms`, ids `0..255`). The hash uses the word id
directly — the smallest clean approach, no symbol-system rewrite.

## Collision strategy

Linear probing from `id % cap`. Consecutive interner ids are distinct mod `cap`
for `count <= cap`, so the benchmark sees no collisions until a global's word id
collides with a parameter's bucket in the child context (e.g. `g11` id 27 vs
`n` id 43, both `27%16 == 43%16 == 11`); those are handled correctly.

## Context memory overhead

| context | before | after | delta |
|---|---|---|---|
| child (cap 16, H 16) | 48 cells | 64 cells | +16 (+33%) |
| global (cap 48, H 48) | ~112 cells | 160 cells | +48 (+43%) |

Hash load factor on fib: child 1/16 ≈ 0.06; global 14/48 ≈ 0.29.

## P4 vs P5 profiling (fib 25)

| metric | P4 | P5 |
|---|---|---|
| result / calls | 75025 / 242785 | 75025 / 242785 |
| logical lookups | 1092531 | 1092531 |
| direct lexical accesses | 606962 | 606962 |
| slots examined (linear) | 11046710 | **0** |
| hash probes | — | 2185061 |
| hash hits / misses | — | 1092531 / 1092530 |
| collision probes | — | 0 |
| fallback linear scans | — | 0 |
| parent hops | 1092530 | 1092530 |
| allocs / GC cycles | 1 / 0 | 1 / 0 |

## P4 vs P5 timing (baseline, same machine)

| fib n | P4 median | P5 median |
|---|---|---|
| 20 | ~0.82 s | ~0.71 s |
| 25 | ~10.0 s | ~8.0 s |

## Synthetic many-binding benchmark (40-global global context, lookups)

| globals | lookups | linear slots | hash probes | collisions | fallback |
|---|---|---|---|---|---|
| 10 | 664 | 0 | 1326 | 0 | 0 |
| 18 | 664 | 0 | 1326 | 0 | 0 |
| 26 | 664 | 0 | 1386 | 60 | 0 |
| 34 (fills cap 48) | 664 | 8695 | 662 | 0 | 664 |

Up to `count < cap` the lookup is O(1) (≈2 probes per lookup, zero linear slots);
a full index (`count == cap`) falls back to the ordered scan, as designed.

## Remaining dominant cost after P5

Linear slot scanning is gone for `fib` (11M → 0). The remaining cost is the
~2.2M hash probes themselves plus the unchanged evaluator dispatch, argument
evaluation and native arithmetic — dispatch/arithmetic is now the leading
candidate, not name resolution.

## Concerns

- A fixed `cap`-sized index is 33% (child) / 43% (global) more context memory;
  an evidence-based threshold (linear for tiny contexts) would trim this if it
  ever matters, but was not needed here.
- The index is skipped once `count == cap` (the pre-existing global overflow
  case), so 48+ globals degrade to the ordered scan.

## Retain P5?

Yes: dynamic lookup is no longer O(bindings) on average, the architecture is
clean, and fib 25 improved ~20% while all 326 regressions and the frozen-S1
guard pass.
