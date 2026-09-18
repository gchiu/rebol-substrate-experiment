# FIB-OPT-P4-RESULTS.md — deliverables

## 1. Branch / commit
- branch: `fib-opt-p4`
- commit: `a58c48afd9966176a21f03e8976fb8e5c010d133`
- tag: `fib-opt-p4`
- message: `Resolve lexical references to depth/slot at load time`

## 2. Files changed
- `r0_s1.h` — `T_BOUND` tag (13), `mk_bound`/`bound_*` encoding, `PF_LEX_DIRECT/LOCAL/PARENT` counters, `lex_*` stats fields.
- `r0_s1_runtime.c` — loader binding pass (`lex_*` scope stack), `emit_load_lex`, `T_BOUND` dispatch case + shared value-dispatch refactor.
- `r0_s1_fib_profiler.c` — `lex-direct` output; relabel `ctx=` → `ctx-created=/heap-ctx=`.
- `r0_s1_p4_tests.c` — adversarial lexical tests.
- `FIB-OPT-P4-LEX.md` — design note.
- `Makefile`, `main.c` — wire the new test file.

## 3. Binding architecture
A load-time scope stack collects each function's parameters (in runtime slot
order). A plain word reference resolves innermost-first against the stack; a
parameter match becomes `mk_bound(depth, slot)` (tag 13). At runtime
`emit_load_lex` decodes `(depth,slot)`, walks `depth` `CTX_PARENT` hops from
`RV_CTX` and reads the value cell at `p+4+2*slot` — no name comparison. The
context keeps its `(word,value)` pairs, so the dynamic path and introspection
are unchanged.

## 4. What stays dynamic, and why
- **Locals** — `r_set` assigns local slots in execution order; a set-word under
  `either`/a loop may not run (or two branches may claim one slot), so static
  slot numbers would be unsound.
- **Globals** (arithmetic natives + top-level words) — redefinable/shadowable at
  run time, and their depth depends on lexical nesting.
- **Forward references** — a local referenced before its set-word is not yet
  bound and resolves to the parent at run time.
- **Reserved keywords** (`func`/`return`/`raw`, word ids 0/1/2) — never resolved.

## 5. Tests added
`r0_s1_p4_tests.c`: shadowing, 3-level nested capture, captured mutation,
recursion (slot vs activation identity), promotion, dynamic-global redefinition,
higher-order bound closure.

## 6. Regression
318 ok, `all tests passed` (no failures).

## 7. Frozen-S1 guard
`check-frozen-s1.sh` passes (substrate unchanged).

## 8. P3 vs P4 profiling (fib 25)
| metric | P3 | P4 |
|---|---|---|
| result / calls | 75025 / 242785 | 75025 / 242785 |
| logical lookups | 1699493 | 1092531 |
| direct lexical-slot accesses | — | 606962 (local) |
| dynamic lookups remaining | 1699493 | 1092531 |
| slots examined | 11653672 | 11046710 |
| parent hops | 1092530 | 1092530 |
| allocs / cells | 1 / 32 | 1 / 32 |
| ctx-created / heap-ctx | 242785 / 0 | 242785 / 0 |
| GC cycles | 0 | 0 |

## 9. P3 vs P4 timing (baseline, same machine)
| fib n | P3 median | P4 median |
|---|---|---|
| 20 | ~0.91 s | ~0.82 s |
| 25 | ~10.2 s | ~10.0 s |

Roughly neutral (within noise).

## 10. Concern discovered
The direct access eliminates the O(bindings) name scan but adds a fixed decode
(one `DIV`, one `MOD`, one parent-walk test) plus a subroutine call. For
`fib`'s single-binding contexts that fixed cost is *not* cheaper than a one-slot
scan, so there is no speedup here — only the architectural elimination of
lexical scanning. The win would only materialise for functions with many
bindings, where the scan is long.

## 11. Retain P4?
Yes, as the architectural baseline: lexical references are now pre-resolved and
the lexical scan is gone; timing is neutral. P5+ (global/static-slot, dispatch)
would build on it.
