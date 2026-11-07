# GLON-DOM v001 — a DOM-tree event iterator written in Glon

> **Status (2026-11-07): experiment, not deployed, not used by any application.**
> An optional loadable file on the unchanged substrate:
> - no new datatype, evaluator change, binding rule or loop construct;
> - no change to frozen S1, the runtime or the 64K memory map;
> - iterator v001 (`experiments/glon-iter/`) and Pratt v004 are only read.

**Objective.** To test whether the iterator architecture of GLON-ITER v001
generalises from linear byte and token streams to **hierarchical traversal**.
This is a step before directory traversal. It follows Chris Ross-Gill's DOM
iterator ("Iterators, Part I", gist `rgchris/9770ab0f7b625ed4b5db84a97fa999ba`).
That article describes the iterator's behaviour through examples; its module
source is not in the gist, so this experiment matches the behaviour shown, not
Chris's implementation.

**Result.** It does generalise.
- **One new file, one new word.** A walker over a tree held entirely as Glon
  data produces OPEN, CLOSE, EMPTY and TEXT events. It answers the v001 words
  `it/peek`, `it/next` and `it/pos` **unchanged**. Its only new public word is
  `it/close` (skip a subtree).
- **Bounded return stack.** The traversal stack is held in managed data, as one
  32-cell parent frame per open level. Each advance costs the same few
  return-stack frames at any nesting depth.
- **Deep trees.** A 150-level tree is walked. The depth limit is the managed
  heap, not the return stack. A plain recursive walk fails beyond about 42
  levels.

## Files

| File | Purpose |
|---|---|
| `dom.glon` | the walker: `dom/walk`, the new word `it/close`, and its helpers (adopted) |
| `t-common.glon` | shared test helpers (balanced drivers) and fixtures: Chris's example tree, a 60-level chain |
| `dom-tests.glon` | 67 tests (A–H): Chris's example, peek/close, pause and resume, independence, empties and mixed text, completion, errors, the shared protocol, depth 60 |
| `gc-tests.glon` | 8 tests (I): walks resumed across garbage collections with a 59-frame stack live |
| `deep-tests.glon` | 7 tests (L): a 150-level chain, walked and skipped from its bottom |
| `fn-tests.glon` | 5 tests (K) of the measured alternative |
| `variants/dom-fn.glon` | the measured alternative: explicit immutable state, no closure (not adopted) |
| `variants/rw.glon` | the measurement baseline: a plain recursive walk |
| `measure.sh` | resource measurements (`make measure`), using `../glon-iter/measure.c` (only read) |
| `Makefile` | `make test`, `make measure` |

`dom.glon` requires `prelude.glon`, `strings.glon`, `demo/shop/strings-ext.glon`
and `../glon-iter/iter.glon` (for `it/end`, `it/iter?`, `it/raw-tag` and the
`it/` words).

## Representation

The tree is plain Glon data, in the block shape of Chris's `r3:xml` module. No
HTML is parsed, and nothing depends on a browser.

- **Text** is a STRING!.
- **An element** is a 3-element BLOCK! `[ name attrs children ]`.
  - `name` and `attrs` are not interpreted: attributes are not walked, as in
    Chris's design.
  - `children` is a non-empty BLOCK! of nodes. "No children" (Chris's `_`) can
    be NONE, the word `none`, or `[]`.
- **How a tree is written:**
  - A tree containing text must be built with `reduce`, because an unevaluated
    `"..."` is not a STRING!.
  - An element-only tree can be a literal block, which lives in the loader heap
    and costs no managed heap.

```
tree: reduce [ 'document none reduce [
    reduce [ "a" none reduce [
        reduce [ "b" none none ]
        reduce [ "c" none [] ]
        reduce [ "d" none reduce [ reduce [ "e" none none ] ] ]
        "f"
    ] ]
] ]
```

## Event protocol

`it/next` and `it/peek` return an **event value**, `[ kind node depth ]`, where
`kind` is one of the words below. Chris's iterator returns true and sets its
own properties instead. A value suits the v001 contract, in which `next`
returns the item.

| kind | when | node |
|---|---|---|
| `open` | an element with children, before them | the element |
| `close` | after the element's children, including after `it/close` | the element |
| `empty` | an element without children | the element |
| `text` | a STRING! node, including an empty one | the string |

- **Depth.** The root is depth 0 and gets its own open and close, as Chris's
  `document` does. An element's open and close share a depth; its children are
  one level deeper.
- **Matches Chris's output.** Walking the tree above, with `it/close` after
  `<d>`, gives `<#><a><b/><c/><d></d>f</a></#>`, which is Chris's output
  (test A4).
- **Completion.** The end is v001's `it/end`. It is identity-unique, so no
  event and no node can equal it (tests E1–E6). At the end, `peek` and `next`
  keep returning `it/end`, `it/pos` stops changing, and `it/close` returns
  `none`.

| word | message | result |
|---|---|---|
| `dom/walk root` | — | a walker (a v001-style iterator) over `root` |
| `it/peek w` | 0 | the next event, without advancing; cached until consumed |
| `it/next w` | 1 | the next event, then advance past it |
| `it/pos w` | 2 | the number of events consumed by `it/next` |
| `it/close w` **(new)** | 6 | skip the rest of the innermost **open** element; the next event is its `close`. Returns the element, or `none` when nothing is open. Discards a peeked event |
| `it/peek2`, `it/skip`, `it/take` | 3, 4, 5 | raise SIN! `'type 'dom/message`: a walker has no byte semantics |

- **`it/peek` and `it/next` are reused unchanged.** The v001 words check only
  that their argument is a block holding a closure, then send message 0, 1 or
  2. The walker answers these messages, so they work on it without
  modification.
- **`it/skip` is not overloaded.** Subtree skipping is the separate word
  `it/close`, message 6. It is defined in `dom.glon`, so v001's file is not
  touched.
- **Close semantics.**
  - Right after an `open` event, `it/close` skips that element's children,
    which is Chris's case.
  - Anywhere else, it skips the remaining siblings of the innermost open
    element.
  - Calling it twice does nothing more (test B12). Before the first event
    nothing is open, so it returns `none` (B8).
- **Errors.** A malformed node raises SIN! `'type 'dom/node` (or
  `'dom/children`) **before any state changes**. `it/pos` is unchanged, the
  same call raises again, and `it/close` can skip past the bad node
  (F1–F7).

## Traversal state

- **The closure.** As in v001, the walker is a one-element block holding a
  closure. The constructor's parameters hold the **innermost level**:
  - the children block and the index into it;
  - the open element and the depth;
  - the number of events consumed and the cached peeked event.
- **The parent frames.** Each `open` pushes one managed **parent frame**,
  `[ parent-frame kids index element ]` (32 cells). Each `close` pops one.
- **No suspended activations.** No Glon activation is held between events, so
  a walk can be paused anywhere and resumed later. The collector keeps the
  frames alive (`gc-tests`).
- **Peek and close.**
  - `peek` computes the event without changing the state, and caches it.
  - `next` applies the transition.
  - `close` drops the cache, so a peeked `open` is never half-entered.

## Tests (native, fresh build, 2026-11-07)

`make test` builds a fresh CLI from the unchanged sources and runs four
sessions:

| Session | Tests | Result |
|---|---|---|
| `dom-tests` (A–H) | 67 | pass |
| `gc-tests` (I) | 8 | pass |
| `deep-tests` (L) | 7 | pass |
| `fn-tests` (K, the alternative) | 5 | pass |

The suites are separate sessions because one session's loader heap cannot hold
every literal fixture. Coverage by section:
- **A:** Chris's example tree, end to end and with `it/close`.
- **B:** peek caching; close after a peek, before the first event, and twice.
- **C:** pause and resume; two walkers over one tree; a subtree as a root; the
  tree left unchanged.
- **D:** each kind of empty element; text roots; empty texts; mixed content.
- **E:** completion compared with `""`, an element named `"none"`, and
  `it/end` inside the tree.
- **F:** errors, recovery by close, and argument checks.
- **G:** the shared protocol, including the known `it/close` limitation below.
- **H:** a 60-level chain, with close at the bottom and at depth 30.
- **I:** about 23K cells allocated against a 7,232-cell heap, so collections
  run while one walker is paused mid-walk and another holds 59 frames; both
  resume correctly.
- **K:** the alternative gives the same events, and one saved state is resumed
  twice.
- **L:** a 150-level chain: 299 events, depth sums, close from depth 149.

## Resource measurements (64K reference configuration, 2026-11-07)

These are native figures from `make measure` (`measure.c` from GLON-ITER v001).

**Global bindings, loader heap and code space**

| | bindings | loader cells | code cells |
|---|---|---|---|
| `dom.glon` | 6 | 1,033 | 0 |
| session: prelude + strings + strings-ext + iter + dom | 75 / 256 | 5,924 used, 8,876 free | code ends at 13,961 |
| `variants/dom-fn.glon` (alternative) | 3 | 528 | 0 |

**Managed heap kept alive**

| | cells |
|---|---|
| a walker | 176 |
| a walker with a cached (peeked) event | 208 |
| each open level (a parent frame) | 32: 59 levels deep keep 1,888 |
| the alternative's state, at start / 59 levels deep | 64 / 1,952 |

**Allocation per operation** (40 steps; the loop's overhead of about 1.6 cells
is included)

| operation | walker | explicit-state alternative |
|---|---|---|
| `next` → `empty` or `text` | 33.6 cells (the event) | 65.6 |
| `next` → `open` | 65.6 (event + parent frame) | 97.6 |
| `next` → `close` | 33.6 | 33.6 |
| `peek` of a cached event | 2.4 (about 0) | — |
| `pos`, `close` | 1.6 (about 0) | — |
| `dom/walk root` (construction) | 177.6 | — |

**Return stack per advance**

This is the deepest probe recursion under which the step still completes, out
of 84 with no step. A higher number means less stack used.

| step | max probe | frames used |
|---|---|---|
| `it/next` at depth 0 (an `open`) | 79 | 5 |
| `it/next` at depth 59 (an `open`, 59 frames live) | 79 | 5 |
| `it/next` at the bottom (an `empty`) | 81 | 3 |
| `it/peek` at depth 59 | 79 | 5 |
| `it/close` at depth 59 | 82 | 2 |
| `df/step` (the alternative) | 82 | 2 |

**The cost per advance does not depend on nesting depth.** The plain recursive
walk (`variants/rw.glon`) uses about 2 frames per level of nesting:

| nesting depth | 1 | 5 | 10 | 20 | 30 | 40 | 50 |
|---|---|---|---|---|---|---|---|
| max probe around `rw/node` | 82 | 74 | 64 | 44 | 24 | 4 | fails |

The data stack peaks at 88 cells at the deepest probe, of 2,380 free above the
code.

**Deepest walk** (CLI, test helpers and fixtures loaded):

| depth | result |
|---|---|
| 150 | 299 events |
| 160, 170 | managed heap exhausted (fail-stop) |

## Findings

1. **The architecture generalises.** The v001 protocol carries tree traversal
   unchanged. A block holding a closure answers numbered messages, the state
   lives in the constructor's parameters, and `it/end` marks completion.
   Hierarchy needs one new operation (`it/close`) and an explicit stack, both in
   ordinary Glon.
2. **The return stack per advance is constant.** It is 5 probe frames at depth
   0 and at depth 59. Depth moves to the managed heap, at 32 cells per open
   level.
3. **Depth is bounded by the managed heap.**
   - The limit is about (free managed cells − 176) / 32 levels. With this
     session's 1,216-cell baseline that is about 180 levels in principle; with
     the test helpers and fixtures loaded (2,112 cells), 150 levels work and
     160 do not.
   - **Fragmentation matters at the limit.** At the bottom of the 150-level
     walk about 600 cells are free, as 32-cell holes. A 50-byte string (an
     80-cell object) then fails to allocate, while 32-cell events still fit.
     `deep-tests.glon` keeps its test names short and releases the example tree
     for this reason.
4. **Closure versus explicit state.** The explicit-state alternative
   (`df/step st` → `[kind node depth next-state]`) shows the trade-off:
   - it allocates about twice as much per event, and is not an `it/` iterator
     unless wrapped in a closure;
   - but any saved state can be resumed more than once (fork and backtrack, test
     K3), which a closure cannot offer;
   - its return stack is the same.

   The closure is adopted. The alternative is the model for a consumer that
   needs backtracking.

## Limitations

- **`it/close` on a byte iterator raises a misleading error.**
  - v001's byte iterator does not know message 6, and treats it as a skip with
    a `none` set. The error is SIN! `'type 's/scan-set` with arg `none`, not one
    naming `it/close`.
  - The byte iterator is left unchanged and still works (test G3).
  - **v001 is deliberately not modified for this.** A later iterator version
    could answer unknown messages explicitly.
- **Recursive consumers are a separate limit.** Glon has no `while` or
  `foreach`. A consumer that drains any iterator with a plain
  one-call-per-event recursive loop exhausts the return stack after about 84
  events, whatever the tree's depth. The tests drive walkers with **balanced**
  recursion (`t/walk`, `t/count`: depth about log2 of the event count). A
  shared draining helper does not exist yet.
- **Per-event allocation.** Each event is a new 32-cell block. A cursor-style
  variant (`next` returns only the kind word; the node read by another message)
  would allocate only the parent frames. It was not built.
- **No `path`.** Chris's `path` (the ancestors) could be read from the parent
  frames on request; it was not built.
- **The tree is not checked as a whole.** Nodes are validated as they are
  reached, so a malformed node late in a tree raises only after the earlier
  events have been delivered.
- **WASM parity has not been verified.** All figures are native.

## Next: directory traversal (not started)

The design maps directly onto Chris's folders iterator:
- a folder read at `open` becomes the children block;
- `it/close` skips a folder without reading it;
- Chris's `error` event (a folder whose contents cannot be read) fits as one
  more kind.

That needs a host interface for reading directories, which Glon does not have.
