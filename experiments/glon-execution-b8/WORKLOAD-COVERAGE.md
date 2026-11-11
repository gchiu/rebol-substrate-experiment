# WORKLOAD-COVERAGE.md — applications and B3 compiler compatibility

## What was actually executable

The B3 compiler accepts only the documented integer/text subset:

- top-level `name: func [params] [body]` definitions;
- integers, parameters, prefix calls, recursion and forward references;
- `+ - *` and `< <= = > >=`;
- `either`;
- multiple expressions in a sequence.

No existing full application or DEFLATE source compiles as-is. B8 therefore
used ten existing **subset programs** and changed only their top-level call:

| Workload | Source | Input | Expected | Kind |
|---|---|---|---:|---|
| fib_10 | B4 `tasks/fib.glon` | fib 10 | 55 | recursion/calls |
| fib_15 | same | fib 15 | 610 | recursion/calls |
| fib_18 | same | fib 18 | 2584 | recursion/calls |
| ack_22 | B7 textual ack artifact | ack 2 2 | 7 | genuine Ackermann recursion |
| ack_23 | same | ack 2 3 | 9 | genuine Ackermann recursion |
| ack_33 | same | ack 3 3 | 61 | genuine Ackermann recursion |
| sum_200 | B4 `tasks/sum.glon` | sum 200 0 | 20100 | arithmetic recursion |
| sum_500 | same | sum 500 0 | 125250 | arithmetic recursion |
| choose_6_2 | B7 textual choose artifact | choose 6 2 | 15 | binomial recursion |
| choose_10_5 | same | choose 10 5 | 252 | binomial recursion |

The B7 ack source was checked by decoding the program structure and by the
measured multi-input results; it is not the B5 constant-answer shortcut.

## Compile checks against existing applications

`tools/check_coverage.py` invokes the unchanged B3 compiler on the original
files. All fail at the first unsupported construct:

| File | First B3 error | Why it cannot be benchmarked as-is |
|---|---|---|
| `jupyter/prelude.glon` | `expected an expression, found '['` | module block wrapper; loader bootstrap definitions |
| `demo/shop/strings.glon` | `expected an expression, found '['` | module wrapper; masm, managed strings |
| `experiments/glon-deflate/inflate.glon` | `expected 'name: func...'` | top-level block; set-words/globals; `/`; blocks; strings; closures; `do`; `judge`; `invoke`; `block-at`; `s/`; masm |
| `experiments/glon-deflate/app.glon` | same | same plus host/app structure |
| `demo/shop/demos/traffic.glon` | `expected an expression, found '['` | top-level block; set-words/global state; `/`; blocks; strings; `invoke`; `block-at`; masm; host events |
| `demos/kaka/kaka-lib.glon` | `expected an expression, found '['` | top-level block; masm; strings; DOM/Canvas emitters |
| `demos/kaka/kaka.glon` | `expected an expression, found '['` | top-level block; globals/set-words; blocks; strings; host/canvas |
| `demos/kaka/kaka-fighter.glon` | `expected 'name: func...'` | top-level block, same host/game vocabulary |

These are **compiler coverage** limitations. They are not evidence that the S1
machine cannot express the operations. The errors occur before execution; no
window into later opcode selection is obtained.

## Requested workload classes

### Function calls and recursion

Executed: fib 10/15/18 and Ackermann 2 2/2 3/3 3. Multiple inputs verify that
the generated programs compute results, not constants. Ackermann 3 3 returns
61 and exercises deep recursion.

### Iteration and arithmetic

There is no loop syntax in Glon itself; existing loop-like behavior is
recursion or host-driven ticks. DEFLATE's `dz/loop` is balanced recursion over
closures/blocks and is not compilable by B3. Traffic's iteration is host-driven
(`traffic-advance` events, `advance 5` per host tick), not a standalone
compilable loop. B8 therefore measured arithmetic recursion (`sum 200/500`) and
the binomial recurrence; it did **not** substitute an invented simulated loop.

### Memory-intensive computation (DEFLATE)

DEFLATE represents the intended memory-heavy category: bit extraction, Huffman
tables, table access and LZ77 back-reference copying. It could not be executed
on B3:

- bit extraction uses tagged integers and `/`/`MOD`/byte access helpers;
- Huffman tables use blocks of counts/codes and STRING! bytes;
- LZ77 copying uses string slicing/concatenation and managed window segments;
- the decoder uses set-words/locals, closures, `do`, `judge`, `invoke`,
  `block-at`, `s/` and SIN! errors.

No DEFLATE benchmark was fabricated by rewriting or slicing the program. The
memory-heavy workload is **N/A** for B8.

### Existing applications (traffic and Kākā)

- **Traffic:** deterministic IDM ring-road simulator; the host supplies time
  and browser UI. Its code uses global set-words, fixed-point `/`, blocks,
  strings, `invoke`, `block-at`, masm and host events. It cannot be isolated
  without changing behavior or implementing compiler/runtime features.
- **Attack of the Mutant Kākā:** game logic plus Canvas/HTML host emitters and
  masm support; top-level blocks, globals, strings, blocks, closures and host
  calls. No computational kernel can be compiled unchanged by B3.

Neither application benefits immediately from the B8 primitive set because
they cannot reach the B8 compiler.

## Compiler coverage classification

| Feature | Status |
|---|---|
| Top-level functions, parameters, recursion, forward refs, integer/arithmetic/comparison, `either` | **Already supported** |
| Native-vs-macro call/return/stack/branch lowering | **Existing lowering**, fully supported |
| Set-words and local bindings | **Requires compiler work** (frame slots and binding semantics) |
| Blocks, managed strings, byte access | **Requires compiler work + runtime integration** (heap/GC objects) |
| Closures, `do`, `invoke`, `block-at` | **Requires compiler work** (capture and invocation semantics) |
| `judge`, SIN!, error propagation | **Requires compiler/runtime integration** |
| DOM, Canvas, `glon_event`, `glon_load`, browser host | **Requires host/runtime integration** |
| Source/module wrappers and loader layout | **Requires compiler/loader work** |
| `/`, `MOD`, byte operations | **Requires compiler work**; some could use existing HOST ops rather than new S1 primitives |
| Bit/Huffman/LZ77 workloads | **Requires compiler coverage first**; whether byte primitives are needed is undetermined |
| Checked stack bounds (K1) | **Requires a frozen-S1 or machine change**; not a coverage issue |

No new VM primitive is demonstrated as necessary by this audit. The primary
gap is compiler/runtime coverage, not the primitive count. A specific byte
primitive may be considered later, but B8 has no measurement to justify one.

## Immediate beneficiaries

The subset programs above are the only workloads that can use the candidate
native operations today. Full DEFLATE, traffic and Kākā cannot benefit until
the compiler accepts their language/runtime features; measuring them by
rewriting would violate B8's constraints and would not answer the application
question.
