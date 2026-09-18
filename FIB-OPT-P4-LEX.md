# FIB-OPT-P4-LEX.md — design: pre-resolved lexical references

Phase P4 of the Fibonacci optimisation experiment. P3 removed allocation/GC
from the benchmark (1 allocation, 0 GC cycles); the remaining cost is name
resolution: `fib 25` performs 1,699,493 logical lookups examining 11,653,672
binding slots (avg 6.86 slots/lookup) and 1,092,530 parent hops.

P4 tests one hypothesis only:

> Can statically resolvable lexical names be converted to lexical depth/slot
> references so runtime execution avoids repeated context-name scanning?

---

## Part A — What causes the measured lookup cost

A plain word reference is an interned symbol `mk_word(id)`. At runtime the
evaluator dispatches it to `r_lookup` (`emit_lookup`), which:

1. starts at `RV_CTX` (the current context),
2. for each context in the chain, linearly scans its `(word,value)` pairs
   comparing `word == id` (`PF_LK_SLOTS` per slot examined),
3. on a miss walks `CTX_PARENT` to the parent context (`PF_LK_PARENT` per hop),
4. repeats until a match or `R0_NONE`.

In `fib` the references are `either, <=, +, -` (global natives), `fib` (a
global closure) and `n` (the parameter). Every lookup re-scans the current
context's single `n` slot and then most of the global context (the 6.86-slot
average is dominated by the global natives, which live near the end of the
global context). Even the `n` lookup, which is found at slot 0, pays the full
loop setup, count fetch and comparison.

## Part B — Which references can be bound statically

The lexical structure is knowable at load time:

* a **parameter** is bound at closure entry, in spec order, to a fixed slot of
  the invocation's own (child) context — depth 0, slot = parameter index;
* a parameter of an **enclosing** function is bound at that function's entry
  and reachable by walking `CTX_PARENT` a fixed number of hops — depth `d`,
  slot = that parameter's index;
* a **local** (set-word introduced in a body) is *not* statically addressable
  in general: `r_set` assigns local slots in execution order, so a set-word
  guarded by `either` or a loop may or may not have run, and two mutually
  exclusive branches may claim the same slot. Static slot numbers would be
  unsound for locals.
* a **global** (including the arithmetic natives and any top-level word) may be
  redefined or shadowed at runtime, so it must keep dynamic lookup.

Therefore P4 resolves **parameters only**, at any lexical depth. Everything
else (locals, globals, forward references, dynamically introduced bindings)
retains `r_lookup`. Correctness outranks benchmark performance: parameters are
the one class whose slot is *always* statically correct.

## Part C — Representation

A new R0 tag above the frozen S1 layer (the tag space belongs to R0, not S1):

```
T_BOUND = 13   (tags 0..12 are taken; 13/14/15 were free)

bound word = mk_bound(depth, slot) = ((depth * 16 + slot) * 16 + T_BOUND)
depth = number of CTX_PARENT hops from the current context
slot  = binding index in the context's data area (value at p + 4 + 2*slot)
```

The context layout (`[parent, count, cap, (word,value)...]`) is **unchanged**:
the `(word,value)` pairs remain, so the debugger's name-based context walk and
dynamic `r_lookup` continue to see named variables. The bound word is only an
execution fast-path; it carries `(depth, slot)`, and the source symbol stays in
the context for introspection.

## Part D — Loader binding pass

`parse_block` already special-cases `func` and tracks a `site_stack`. P4 adds a
parallel `scope_stack`: on `func`, the spec's set-words are collected as the new
scope's parameters (in spec order); the body is parsed under that scope; the
scope is popped afterwards. `parse_word` resolves a plain word by searching the
scope stack innermost-first and emits `mk_bound(depth, slot)` on a parameter
match, otherwise the ordinary `mk_word(id)`. The reserved keywords (`func`,
`return`, `raw`, word ids 0/1/2) are never resolved, so keyword semantics are
unchanged.

## Part E — Runtime

`emit_load_lex` is a leaf subroutine `( bound -- value )`: decode `(depth,slot)`,
walk `depth` parents from `RV_CTX`, and read the value cell at the slot offset —
no name comparison. `emit_subexpr` gains a `T_BOUND` case that calls
`r_load_lex` and reuses the existing value dispatch (native / closure / raw /
self-evaluating), sharing it with the word case via a forward branch.

## Part F — Why closures / promotion / debugger remain correct

* **Closures**: the captured context is the enclosing activation's context; the
  child context's `CTX_PARENT` chain reproduces the lexical scope stack, so a
  bound `(depth,slot)` indexes the same cell a name scan would find.
* **Promotion** (P3): a promoted context keeps the identical `[parent,count,
  cap,(word,value)...]` layout, so `(depth,slot)` still addresses it after it
  moves from the task stack to the managed heap.
* **Debugger**: it inspects the context's `(word,value)` pairs by name via
  `ctx-lookup`; the bound word is only in the code, never in the inspected
  context, so locals remain named rather than numeric.
* **Multitasking**: bound words are pure code metadata; they carry no per-task
  cached state, so suspended activations of the same function cannot collide.

## Part G — Profiler

Adds `PF_LEX_DIRECT`, `PF_LEX_LOCAL` (depth 0) and `PF_LEX_PARENT` (depth > 0)
counters, and relabels the P3 allocation wart `ctx=N` to `ctx-created=N
heap-ctx=P` (P = promotion count). `lookups` / `slots-examined` / `parent-hops`
now measure only the remaining dynamic lookup work.

---

## Part H — Results (fib 25, unchanged benchmark)

| metric | P3 | P4 |
|---|---|---|
| result / calls | 75025 / 242785 | 75025 / 242785 (ok) |
| logical lookups | 1699493 | 1092531 |
| direct lexical-slot accesses | — | 606962 (all local, depth 0) |
| dynamic lookups remaining | 1699493 | 1092531 |
| slots examined | 11653672 | 11046710 |
| parent hops | 1092530 | 1092530 |
| allocs / cells | 1 / 32 | 1 / 32 |
| ctx-created / heap-ctx | 242785 / 0 | 242785 / 0 |
| GC cycles | 0 | 0 |

The 606,962 `n` references were converted to direct slot accesses: lookups fell
~36% and the lexical slot scanning (the 606,962 one-slot scans) was eliminated
entirely. The remaining 11,046,710 slots examined are all **global** scans
(`either`, `<=`, `+`, `-`, `fib`), which are deliberately dynamic.

### Timing (baseline build, same machine)

| fib n | P3 median | P4 median |
|---|---|---|
| 20 | ~0.91 s | ~0.82 s |
| 25 | ~10.2 s | ~10.0 s |

Timing is roughly neutral (within noise): the direct access removes the name
scan but adds a fixed decode (one `DIV`, one `MOD`, one parent-walk test) that
is *more* work than a one-slot scan for `fib`'s single-binding contexts. The
win is architectural — the O(bindings) scan is gone for lexical names — not a
fib-specific speedup, exactly as a tiny-context benchmark would predict.
