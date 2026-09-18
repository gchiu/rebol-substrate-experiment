# FIB-OPT-P5-HASH-LOOKUP.md — design: hash-indexed dynamic context lookup

Phase P5 of the Fibonacci optimisation experiment. P4 removed the static
lexical scan (parameters become `T_BOUND`); the remaining cost is dynamic
name resolution. P5 tests one hypothesis:

> Can the remaining dynamic/global name-resolution cost be reduced by adding a
> hash index over context bindings, without changing rebinding, context
> representation, introspection, RAW, multitasking, closures or frozen S1?

## Part A — Classification of the remaining dynamic lookups (fib 25)

P4 profile (instrumented, fib 25): 1 092 531 dynamic lookups, 11 046 710 slot
examinations, 1 092 530 parent hops.

Every remaining dynamic lookup is a **global** reference — `either`, `<=`, `+`,
`-` and `fib` — all bound in the single global context (13 natives + `fib`, i.e.
14 bindings, cap 48). The per-lookup scan cost is 2 slots in the current child
context (its single `n` slot is a guaranteed miss) plus the position of the name
in the global context, dominated by the late-position names:

| name | slot | lookups | slots scanned each | total slots |
|---|---|---|---|---|
| `either` | 11 | 242 785 | 14 | 3 399 890 |
| `<=` | 7 | 242 785 | 10 | 2 427 850 |
| `-` | 1 | 242 784 | 4 | 971 136 |
| `+` | 0 | 121 392 | 3 | 364 176 |
| `fib` | 13 | 242 784 | 16 | 3 884 544 |

There are **no** dynamic-local, reserved-keyword or forward-reference lookups in
this workload. The ~11M slot examinations are entirely the linear scan of the
14-binding global context (plus the 1-binding child miss). This confirms the
hypothesis's premise: the cost is linear global-context scanning.

## Part B — Symbol identity

Names are already interned: `intern(name)` returns `mk_word(id)` with a stable
symbol id `0..255` (`syms[]`/`nsyms`). The hash key is therefore the **word id**,
a small integer already present in every word value — no re-hashing of UTF-8
spelling, no symbol-system change.

## Part C — Index architecture

Each context gains a fixed hash index, sized to its cap (`H = cap`), placed
after the authoritative `(word,value)` data:

```text
context = [ parent, count, cap, (word,value)*count …, hash[H] ]
```

- Open addressing, linear probing.
- `hash[i] = word_id * 256 + slot` for an occupied bucket; `-1` = empty.
- No tombstones (Glon contexts never remove bindings).
- `h(word_id) = word_id % H`.

Lookup (per context in the chain):

1. if `count >= cap` (index full, or pre-existing global overflow): **linear
   fallback** (correct, current behaviour);
2. else probe the hash: hit → read the value at that slot; empty → move to the
   parent context.

Insert/rebind (`r_append` and `r_set`'s append path) update the hash while
`count < cap`; once full, the index is skipped and the linear fallback takes
over.

This keeps the `(word,value)` pairs authoritative for storage, rebinding,
debugger display, introspection, enumeration and RAW compatibility. The hash is
only a fast path.

## Part D — What stays as-is

- P4 `T_BOUND(depth, slot)` references keep their direct path (never routed
  through the hash).
- Rebindings still observe the *current* value: the hash maps symbol → slot, and
  the value cell at that slot is re-read, so `foo: 10 x: foo foo: 20 y: foo`
  yields `x=10, y=20`.
- Frozen S1 (seven primitives), `check-frozen-s1.sh`, HOST boundary (the hash is
  built from existing HOST arithmetic; no HOST semantic expansion), RAW ABI,
  debugger/introspection (names and values remain in the data area), closures,
  promotion (the promoted copy carries the same layout), and multitasking (the
  index is per-context, never process-global).

## Part E — Memory cost

| context | before | after | delta |
|---|---|---|---|
| child (cap 16, H 16) | 48 cells | 64 cells | +16 (+33%) |
| global (cap 48, H 48) | ~112 cells | 160 cells | +48 (+43%) |

Hash load factor on fib: child 1/16 = 0.06; global 14/48 ≈ 0.29. The child
context grows 48→64 cells, which raises the per-invocation return-stack
footprint to ~96 cells; the M1 3200-cell task arena still fits the 30-deep
regression (30 × 96 = 2880 < 3200). No arena resize is required.

## Part F — Profiling instrumentation

New counters (all compiled under `-DR0_S1_PROFILE`, zero baseline overhead):
`PF_HASH_PROBES`, `PF_HASH_HITS`, `PF_HASH_MISSES`, `PF_HASH_COLLISIONS`,
`PF_HASH_FALLBACK`, `PF_HASH_FALLBACK_SLOTS`.

## Part G — Collision / growth strategy

Linear probing handles collisions. The index is fixed at `H = cap` and never
grows; once the context is full (`count == cap`) the index is simply not used
and the linear fallback is correct. This is a deliberate simplicity trade-off,
not a claim that a growing hash would not be better.
