# Glon Architecture Decision — November 2026

**Status:** consolidation after B13. This is a recommendation, not an
implementation, promotion, freeze or authorisation to change production.

## 1. Verified current state

Repository state includes `2d34bc0` (memory audit); the B13 profile commit is
`7b0dd1c`. Frozen S1 is unchanged at
`f90496c26dc45c7a387d8fc8639cd2781507d3d2`; `./check-frozen-s1.sh` passes.
DEFLATE remains **480/480** conformance and **44/44** unit tests, with
byte-identical output to zlib/the seven-primitive reference.

| Checkpoint | Commit | Verified content |
|---|---|---|
| B1–B8 | `3f096bc`…`66f3908` | compiler, representation, primitive and integer-workload evidence |
| C1 | `9a1cf63` | unchanged `hf/left` compiled at Level 3; kernel only |
| F1/F2 | `08c5816`, `22c9c95` | DEFLATE fragmentation diagnosis and bounded code-length buffer; 480/480 |
| B9 | `212f0b8` | full DEFLATE baseline; compiled `hf/left` kernel 1.97× fewer instructions |
| B10 | `a35612f` | operation attribution: evaluator/stack/memory dominate |
| B11 | `481278a` | native CALL/RET/>R/R>/R@ in the evaluator; full DEFLATE |
| B12 | `a202645` | S1-7 vs S1-9 vs S1-12; full decoder correctness and performance |
| B13 | `7b0dd1c` | selectable native profiles; FizzBuzz and LaTeX validation |
| ITER/DOM | `ff2d02c`, `369a3f0`, `9ee6ed7`, `fb2b951` | pure-Glon iterator, DOM and host-backed traversal experiments |
| MEM | `8764f4e`, `2d34bc0` | constrained-memory plan and reference-safety/bounded-memory audit |

The root native `s1` suite still has an unresolved traffic-test discrepancy from
the earlier review (test 8 fail-stop in this environment). B13 does not rerun
that suite and does not rely on it.

## 2. Architectural layers

| Layer | Existing/verified | Experimental/measured | Proposed/unimplemented |
|---|---|---|---|
| Source semantics | words, blocks, contexts, closures, SIN!; Alpha laws | iterator and DOM vocabularies in plain Glon; authoring trials | new syntax, implicit loops, iterator primitive |
| Compiler/lowering | no production compiler | B1–B4 subset, C1 kernel, B9 boundary | full-language compiler/runtime |
| Frozen S1 | seven primitives; hash verified | B1/B11/B12 target the unchanged reference | any frozen-machine change |
| Native execution | none in production | B11/B12/B13 S1-9/S1-12 evaluator profile | production/WASM profile |
| Memory | 65,536-cell map; 7,232-cell managed heap; non-moving GC | F2 bounded buffer; iterator heap measurements; B10 allocation counts | 32K profile, zero-allocation decoding, moving GC |
| Host services | native CLI, `GLON_LIVE`, browser/WASM/DOM | links/native host-backed DOM; B13 native-only boundary | caller buffers; WASM profile |
| Application algorithms | DEFLATE, FizzBuzz, LaTeX, traffic, Kākā, shop | B13 native app validation; iterator examples | compiled full applications |

Separations that must remain explicit: HOST is not a primitive; managed
strings/blocks are runtime data, not language requirements; S1-12 is an
evaluator profile, not a compiler; iterator closures are a library protocol,
not a new VM feature.

## 3. Performance evidence

### Integer/compiler evidence (B1–B8, C1)

B1 direct compilation removed 62.1× of evaluator instructions on fib 18. B8's
ten integer workloads showed five native operations removing 69.9% of
instructions and 62.8% of emitted cells versus seven-op lowering; the six-op
variant including BRANCH reached 71.3%/65.3%. C1's compiled `hf/left` kernel
used 157 vs 279 function cells and 8,960 vs 17,644 instructions. These results
are real but bounded: integer/subset/kernel, native-only, no WASM, no complete
application.

### Whole-DEFLATE evidence (B9–B12)

B10 attributed the full decoder's **284M–2.10B** operations:

- 44–45% literal/stack (`LIT`, `DUP`, `DROP`);
- 32–34% memory (`@`, `!`);
- 16–17% HOST, mostly arithmetic/comparison;
- 6.2–6.5% branches;
- negligible allocation/GC by operation volume.

B10 found no reliable function-name map, so it made no ten-function claim; the
reliable conclusion was that generic evaluator/data movement dominates.

B11 replaced the evaluator's emitted call/return/return-stack macros with the
validated native instructions. On the complete unchanged decoder it passed
480/480 and 44/44 and measured:

| Workload | S1-7 | S1-12 | Code cells |
|---|---:|---:|---:|
| fixed | 933.8 ms | 820.1 ms | 14,549 → 9,933 |
| dynamic | 3,933.1 ms | 3,431.7 ms | 14,549 → 9,933 |
| streaming | 6,406.2 ms | 5,322.5 ms | 14,549 → 9,933 |

B12 separated the native sets. S1-9 (CALL/RET; return-stack macros) executed
**0** native >R/R>/R@; S1-12 executed them millions of times. S1-12 also reduced
total operations 4.8–6.4% relative to S1-9 and code cells from 10,683 to 9,933.
The nine→twelve timing differences were small and noisy (1.01–1.09×), but the
deterministic operation/code reductions show that the extra three instructions
are genuinely exercised, not just present in a larger dispatch table.

### Application/profile evidence (B13)

B13 provides build-time native profile executables without changing the default
`glon`:

- FizzBuzz: 100/100 lines; S1-7 and S1-12 outputs identical.
- LaTeX v001/v002: both suites pass under both profiles; outputs identical.
- DEFLATE: B12's full 480/480 and 44/44 results are reused; B13 adds no decoder
  integration change.
- Shop/browser applications: existing tests use the WASM/DOM host; the native
  CLI does not provide that boundary, so they are not claimed as native
  compatibility.

Small-app timings (startup-dominated) were FizzBuzz 31.7→24.0 ms, LaTeX v001
150.9→132.2 ms and v002 291.6→233.3 ms. These are not a general application
benchmark.

## 4. Iterator experiments

GLON-ITER v001 implements an iterator as a one-element block holding a closure
that answers numbered messages; state lives in the constructor's parameters.
It adds no evaluator feature, syntax or loop construct. Results:

- 77/77 iterator tests and 28/28 lexer tests; 1,200 differential token walks
  matched frozen Pratt v004.
- `next`/`peek`/`pos` allocate no managed cells within one fragment; cross-
  fragment `take`/`skip` use balanced recursion and remain bounded.
- Costs: 2,153 loader cells and 27 code cells for `iter.glon`; cross-fragment
  operations allocate and are expensive compared with v004's non-iterator lexer;
  the iterator lexer was about 2× slower per token at `-O0`.
- Not integrated into the parser; WASM parity not verified.

DOM v001 reuses the protocol for hierarchical traversal; it adds an explicit
parent-frame stack, keeps return-stack use constant per event, and bounds depth
by the managed heap. The host-backed links experiments show streaming traversal
over a real DOM with host-owned nodes and two independent extractors. They also
expose a genuine boundary: M1 cooperative waiting plus GLON_LIVE request/reply
needs a scheduler change; it was not attempted.

Architectural reading: Glon's existing closures and data structures are enough
for iterator/traversal protocols. The experiments do not justify an ITERATOR!
datatype, a `foreach` primitive or a new loop form. Their costs are loader/code/
heap and native-only validation, not a primitive deficiency.

## 5. Memory architecture and constrained operation

Memory management is a runtime implementation concern, **not part of the frozen
seven-primitive S1 interface**. The reference interface is unchanged; changing
object representation would nevertheless have runtime, host and application-wide
consequences and therefore requires a separate decision.

Verified current implementation (`r0_s1_runtime.c`, `r0_s1.h`; plan
`GLON-CONSTRAINED-MEMORY-PLAN.md`, commit `8764f4e`; audit
`GLON-MEMORY-AUDIT-2026-11.md`, commit `2d34bc0`):

- the managed heap is 7,232 cells inside the fixed 65,536-cell map;
- collection is **non-moving, stop-the-world exact mark/sweep**;
- allocation is first-fit over implicit free blocks, with a frontier bump,
  one collection retry and fail-stop on real out-of-memory;
- sweep coalesces adjacent free runs; there is no explicit free list;
- there are **no stable handles, pinning API or compaction**. Tagged Glon values
  contain raw payload pointers; `RV_*`/scratch/MASM/interior/host paths can also
  hold raw managed pointers.

Reference safety is therefore not yet relocation-safe. Tagged values, contexts,
closures, blocks and explicit roots are traceable, but the audit identifies
unresolved raw managed-pointer paths in evaluator/scratch cells, MASM/library
scratch and interior references. Compaction cannot be considered safe until
every such pointer is rooted, pinned or redesigned. Handle indirection alone
does not provide tracing or solve fragmentation.

Demonstrated fragmentation is specific, not general:

- F1: a 320-cell dynamic-header request failed with 2,944 free cells and a
  304-cell largest block; coalescing had run. F2 removed that trigger with a
  bounded per-header buffer and the full DEFLATE suite passes.
- DOM iterator v001: at its depth limit, about 600 free cells were present in
  32-cell holes while an 80-cell allocation failed.

Bounded measurements on the existing fixed/dynamic/streaming DEFLATE workloads
(`experiments/glon-memory-audit/`) found peak live cells 4,288/4,464/4,832,
heap high-water 7,216/7,232, end-free cells 592/1,456/1,760, largest free blocks
432/416/384 and GC counts 13/79/185. All completed without allocation failure.
GC pause and per-allocation latency remain unmeasured without runtime timestamp
instrumentation.

**Decision:** retain the existing non-moving collector for the tested workloads.
No handles, compaction, pinning API or replacement GC is authorised. Reconsider
only if a reproducible application still fails after bounded buffers and
non-moving allocation-policy improvements, and only after a complete raw-
pointer/root/pinning audit with native and WASM validation. A moving design is
not required merely because fragmentation has been observed.

## 6. Recommendation

**Keep S1-7 as the universal frozen reference. Retain S1-12 as an optional,
experimental native evaluator profile. Do not promote it to the production
default.**

S1-12 is now justified by whole-DEFLATE evidence, not only by B8's integer
kernels: it runs the unchanged decoder, passes the full and unit suites, reduces
code size substantially, and gives a measured 1.08–1.20× native speedup. S1-9
is the conservative minimal-call variant; S1-12 is preferable only when the
extra native return-stack surface is acceptable and validated.

This is not a recommendation to freeze twelve primitives or to change the
reference machine. S1-12 remains a build-time native profile, with S1-7 as the
control; production `glon`, browser `/inflate/`, and the WASM applications are
unchanged.

## 7. Remaining blockers and limits

**Correctness/integration blockers**

1. **Full compiler coverage is absent.** The complete DEFLATE/traffic/Kākā/shop
   sources still cannot be compiled. B9 confirms the boundary at module
   wrappers, locals, blocks/strings, closures, errors and host services. This is
   a compiler/runtime project; the S1-12 evaluator profile does not fix it.
2. **S1-12 is native-only.** There is no WASM/browser port. B13 validates native
   CLI applications only; shop/browser compatibility, and the deployed pages,
   remain S1-7.
3. **Root traffic test 8 remains an unresolved verification discrepancy.** It is
   separate from the B9–B13 DEFLATE/profile work and needs a targeted
   investigation, not a broad runtime change.
4. **Iterator/traversal is not integrated.** It is native-only, loader/code
   expensive, slower than v004's lexer, and has no parser integration or WASM
   parity. The links experiments also show the M1/GLON_LIVE waiting-state gap.

**Capability limits, not current defects**

- Full 32 KiB DEFLATE history is impossible in the 7,232-cell heap; it needs a
  new memory profile.
- K1/checked stacks remain a safety decision for native/compiled code.
- F2's per-header fixed buffer remains a design risk for future fragmentation
  shapes.
- The shop/browser applications depend on a host boundary that the native
  profile does not implement.

Performance opportunities such as zero-allocation streaming, packed memory,
compiler coverage or a WASM profile are not correctness blockers and require a
defined application requirement.

## 8. Decision rule

1. Define observable behaviour and an independent oracle.
2. State intrinsic resource needs (bytes, stack, heap, window).
3. Check existing Glon/S1 facilities first.
4. Implement the smallest correct representation; preserve semantics.
5. Test against an external oracle, including the full regression corpus.
6. Measure compiler, evaluator, host and application costs separately.
7. Add or promote a primitive/profile only when a reproducible application gap
   and full validation justify it.

Every experiment must name the decision it will settle. An experiment that
cannot change a retain/defer/stop decision should not run.

## 9. Retain, defer, stop

**Retain:** frozen S1-7; production Glon/runtime/host; F2's bounded buffer;
the non-moving mark/sweep collector for tested workloads; the memory audit and
bounded measurements; B11/B12's S1-12 experimental evaluator; B13's build-time
native selection; iterator/DOM v001 as optional libraries/experiments; the
complete B9–B13 evidence.

**Defer:** S1-12 production promotion; a WASM/browser S1-12 profile; full-language
compiler coverage; iterator parser integration and WASM parity; 32K memory
profile; zero-allocation decoding; checked stacks/K1; any handle/compaction
prototype until the raw-reference/pinning audit and a reproducing application
failure justify a separate architecture decision.

**Stop:** adding primitive variants without a failing application requirement;
compiler expansion merely to make an experiment pass; duplicating the B12
runtime or building another profiling framework; treating integer/kernel or
native-only results as general production evidence; promoting S1-12 before the
browser/WASM and host boundaries are addressed.

## 10. Limits of the evidence

Strongest: frozen S1; DEFLATE 480/480 and 44/44; B10 operation attribution;
B11/B12 whole-decoder native results; B13 native FizzBuzz/LaTeX equality and
selection mechanism; iterator/DOM tests; the memory audit's source inventory
and bounded measurements. Weakest: no full compiler, no S1-12 WASM, no shop
native compatibility, unresolved root traffic test, noisy wall timings,
native-only iterator validation, unmeasured GC pause/allocation latency,
incomplete raw-pointer relocation audit, and a B12 runtime copy with its own
maintenance burden.

**Bottom line:** the smallest justified architecture is still the frozen
seven-primitive S1 with the existing language/runtime and host boundary. An
optional S1-12 native evaluator profile is now supported by full DEFLATE
evidence and native application validation, but it remains experimental and
native-only; iterator support belongs in ordinary Glon libraries, not in a new
primitive or syntax.
