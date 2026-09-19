# FIB-OPT-P8 — Lazy context hash (do activations need a full hash index?)

## 1. Hypothesis

P7 left the activation/binding path (`invoke_closure` + `mkctx` + `append`) as
the dominant remaining cost (~28% of execution). P8 tests one architectural
hypothesis:

> Does every function activation need to eagerly construct and initialise a
> fully general dynamic context — specifically the P5 hash index — even when its
> parameters are already statically bound to lexical slots by P4?

For naïve `fib 25` every one of the 242,785 activations builds a 16-slot hash
index (zeroing 16 cells in `mkctx`, then inserting each parameter via
`hash_insert`), yet the child context's hash is **never hit** by a lookup — every
probe of it is a miss for a global word, and the parameters are resolved by
`load_lex` (direct slot access), not by the hash. The hash index in an arity-1
activation is pure overhead.

## 2. Measurement first (context-usage classification)

P7's IP histogram plus the P6/P7 event counters already answer the question:

| activation-context use | `fib 25` |
|---|---|
| parameter slots | 1 (`n`) |
| dynamic locals added | 0 |
| dynamic lookup against the activation | 0 hits (only misses for global words) |
| hash consulted (probes) | 1,092,530 probes, all **misses** |
| hash ever hit | never |
| captured by a closure | no |
| promoted | no |
| RAW inspects it | no |
| debugger enumerates it | no (only in regression tests) |
| task suspension observes it | no (only in M1 tests) |
| GC scans it | yes (parent + value cells; the hash is not scanned) |
| needs full (word,value) enumeration | no |

So **0 of 242,785 activations require their hash index**; the hash is used only
for miss-detection when a global word is looked up from inside the function. The
P7 counters showed `hash-probes 2,185,061` = 1,092,531 global hits + 1,092,530
child-context misses, and `hash-misses 1,092,530`, `hash-hits 1,092,531`.

## 3. Design: threshold-lazy hash

The P5 hash index was built for the *global* context (13+ bindings, looked up a
million times), where linear scan is expensive. For a child context with 1–3
bindings, a linear scan is already O(1) and the hash is pure overhead — and the
miss-detection a linear scan performs is exactly the same one the hash performs.

P8 makes the hash **lazy and threshold-gated**:

- New constant `HASH_MIN = 4`. A context with fewer than `HASH_MIN` bindings
  resolves lookups by the ordered linear scan and does **not** maintain a hash.
- `mkctx` no longer zeroes the hash index (the largest single waste).
- `append` (parameter binding) and `set` (local creation) maintain the hash only
  once the binding count reaches `HASH_MIN`: at exactly `HASH_MIN` a new
  `build_hash` routine materialises it (zero `cap` buckets, then insert each
  existing pair); above `HASH_MIN` they call `hash_insert` as before.
- `lookup` uses the hash only when `HASH_MIN <= count < cap`, otherwise the
  linear scan (which already existed as the "count >= cap" overflow fallback).

For `fib 25`: count stays 1, so no hash is ever zeroed, inserted into, or
probed; the ~1.09M child-context lookups become linear scans of a single slot.

## 4. Why frozen-S1 / observable semantics are preserved

- No S1 primitive added, removed, or reinterpreted; `s1.c`/`s1.h` byte-identical.
- The `(word,value)` slots remain authoritative and unchanged: `load_lex` (P4
  `T_BOUND`), dynamic lookup, `set`, `append`, the debugger, RAW, and GC all read
  those cells exactly as before. Only the *hash index* (a P5 fast-path) is
  deferred.
- The linear scan is the pre-existing overflow path; P8 merely routes small
  contexts to it. Its result is identical to the hash's result (miss → parent,
  hit → value).
- Promotion (`mkclosure`) copies parent/count/cap/data/hash verbatim. For a small
  context the copied hash is garbage but never read (lookups stay linear); if a
  promoted context later grows past `HASH_MIN`, `build_hash` re-materialises it
  from the (word,value) pairs, so identity and shared-mutation semantics are
  intact.
- GC scans only parent + value cells (not the hash), so garbage hash cells are
  invisible to it.
- `build_hash` preserves the caller's registers: the loop counter uses `RV_SITE`
  (written only by `r_return`, never live during binding), and the count is saved
  on the return stack, so the bind-loop counter `RV_T4` and `set`/`subexpr`
  state are untouched.

## 5. Correctness

Full regression: **326 ok, 0 fail** (`-O2`), `./check-frozen-s1.sh` passes. This
includes the P5 "full 16-param context" test, P4 lexical-scope tests, dynamic
locals (multi-local functions crossing `HASH_MIN`), closures/promotion, RAW,
escape/non-local RETURN, debugger enumeration, M1 multitasking, and M2 GC. The
M3C STRING!/M3D BLOCK! libraries (which use 4-parameter functions crossing
`HASH_MIN` and set-word locals) pass.

See `FIB-OPT-P8-RESULTS.md` for before/after instruction counts and timing.
