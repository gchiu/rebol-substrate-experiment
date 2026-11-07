# GLON-ITER v001 — an iterator vocabulary written in Glon

> **Status (2026-11-07): experiment, not deployed, not yet used by any parser.**
> An optional loadable vocabulary on the unchanged substrate:
> - no ITERATOR! datatype, evaluator change, new binding rule or loop construct;
> - no change to frozen S1, the runtime or the 64K memory map;
> - frozen Pratt v004 (`experiments/glon-pratt/`) is only read; its checksums
>   still pass.

**Objective.** Chris Ross-Gill's iterator concept, as plain Glon: an iterator
is a value that keeps its own traversal state between calls. A consumer asks
for the next value, rather than threading a source and a position through every
call as Pratt v004 does. The first application is the native Pratt parser, but
the vocabulary is general.

**Result.**
- **Byte iterator:** `it/` reads a block of string fragments as one byte stream.
  Each advance allocates nothing and costs a fixed few return-stack frames,
  however many fragments it crosses.
- **Lexer:** `lx/` is built over it and is itself an iterator. It produces
  **exactly** Pratt v004's tokens (kind, start, end, first byte, and the same
  error), byte for byte, on 400 inputs each cut into fragments 3 ways.
- **Bounded `it/take`:** the run is found with balanced Glon recursion over
  fragment ranges, at a stack depth of about log2(fragments). A masm
  cross-fragment scanner was built and measured too
  (`variants/iter-masm.glon`), and **not adopted**: it costs 586 code cells
  for a gain only on heavily fragmented streams.

## Files

| File | Purpose |
|---|---|
| `iter.glon` | the `it/` byte iterator (adopted version) |
| `iter-lexer.glon` | `lx/`: v004's lexer over an iterator; position-based tokens; a token iterator |
| `iter-tests.glon` | 77 native tests of `it/` |
| `lexer-tests.glon` | 28 native tests of `lx/`, including a 518-token stream over 74 fragments under collection |
| `iter_corpus.mjs`, `dump-v004.glon`, `dump-iter.glon` | the lexer differential against frozen v004 (Node only writes inputs) |
| `variants/iter-masm.glon` | the measured alternative: the same file with masm fragment search and checks |
| `measure.c`, `measure.sh` | resource measurements on the 64K runtime (`make measure`) |
| `Makefile` | `make test`, `make test-lexer-diff NODE=…`, `make measure`, `make all-tests` |

Requires `prelude.glon`, `strings.glon` and `demo/shop/strings-ext.glon` (for
`s/scan-set`, `s/span`, `s/at`). Do not load v004's `s-extra.glon` in the same
session, because its `s/span` is the scanner.

## The API

| Word | Meaning |
|---|---|
| `it/bytes frags` | an iterator over a BLOCK! of STRING!s, read as one byte stream; empty fragments are allowed anywhere |
| `it/peek i` | the current byte (0–255), without advancing; `it/end` at the end |
| `it/peek2 i` | the byte after the current one, or `it/end` |
| `it/next i` | the current byte, then advance; `it/end` at the end, repeatedly |
| `it/pos i` | the 0-based byte offset of the cursor across all fragments |
| `it/skip i set` | advance past the run of bytes in the string `set`; returns the new position; allocates nothing |
| `it/take i set` | the same advance, returning the run as a new string |
| `it/end`, `it/end? v` | the end marker, and its test |
| `lx/tokens b` | a **token iterator** over byte iterator `b`, which answers `it/peek`, `it/next` and `it/pos` |
| `lx/token b` | one token from `b`, or `it/end` |

- **End of stream.** `it/end` is a unique block, and `=` compares blocks by
  identity, so `none`, `0`, byte 0 and byte 255 are never mistaken for it. That
  is tested.
- **Errors.** A wrong argument raises SIN! `'type` with the operation's name.
  This includes a non-iterator given to `it/peek` and the other operations: in
  a first draft that fail-stopped the machine, because `block-at` and `invoke`
  are unchecked.
- **Why `peek2` is included.** v004's lexer looks two bytes ahead to tell `1.5`
  and `.5` from a lone `.`. Without `peek2`, the token stream could not match.
  `skip` is included so that tokens need not allocate text.

## How it works without new language

- **Protocol.** An iterator is a **one-element block holding a closure**
  `func [m x]`, which takes a message number (0 peek, 1 next, 2 pos, 3 peek2,
  4 skip, 5 take) and an argument. A closure is *called* wherever its word is
  evaluated, so it cannot be passed by name; the block can. Any closure that
  answers messages 0–2 is an iterator, which is how the token stream reuses
  `it/peek`, `it/next` and `it/pos`.
- **State in parameters.** The constructor `it/bytes* frags it-f it-i it-n`
  holds the fragment index, the byte index within it and the absolute position.
  The closure updates them in place with set-words. Parameters are always
  local; every other local name is prefixed (`it-`, `lx-`), because a set-word
  updates the nearest existing binding, which can be a global.
- **The bounded return stack** (requirement 6) works at two levels:
  - **Within a fragment:** runs are scanned by `s/scan-set`, a masm loop.
  - **Across fragments:** the next non-empty fragment is found, and lengths are
    totalled, joined and validated, by **balanced recursion over fragment
    ranges**. Each call splits `[a, b)` at the midpoint, so the depth is about
    log2(fragments), not one frame per fragment. When a run ends inside its own
    fragment, no recursion happens at all, which is always the case for the
    parser.
  - **Invariant:** the cursor is always at the end, or inside a **non-empty**
    fragment, so `peek` never searches.

## Bounding `it/take` across many fragments: three implementations measured

The scratch prototype recursed once per non-empty fragment crossed. Measured
(probe frames still available after a take; 84 is the empty-stack baseline):

| Fragments crossed by one take | 21 | 41 | 81 | 201 |
|---|---|---|---|---|
| prototype (linear recursion) | 70 | 60 | 40 | halts (it halted at 100 in the investigation) |
| **adopted (balanced Glon recursion)** | 72 | 71 | 70 | **69** |
| masm cross-fragment scanner | — | — | — | 69 |

Both bounded versions are bounded by log2 of the fragment count. The fragment
count itself is bounded by the heap: every distinct STRING! occupies at least
one collector extent (32 cells). A block may repeat the same string, though,
which is how the tests build 201 fragments.

What the masm machinery would cost, and what it saves:

| | adopted `iter.glon` | `variants/iter-masm.glon` |
|---|---|---|
| masm added | `it/raw-tag` (one instruction sequence) | `it/raw-full` (a nested loop with a 16-cell workspace), `it/raw-frags?`, `it/raw-iter?` |
| global bindings | 19 | 21 |
| loader-heap cells | 2,153 | 1,821 |
| **code cells** (shared with the data stack) | **27** | **586** |
| skip/take inside one fragment | identical | identical |
| skip across 201 fragments: cells allocated | 3,762 | 274 |
| take across 201 fragments: cells allocated | 11,314 | 7,826 |
| take across 201 fragments: time (native `-O0`) | 183 ms | 75 ms |
| `next` across 201 alternating empty fragments: probe frames | 70 | 80 |
| constructing over 201 fragments (validation): probe frames | 72 | 81 |

**Decision taken in this experiment:** the Glon version is adopted. It is
equally bounded, identical for the parser's single-fragment input, and adds no
loop machinery to masm.
- **Cost of the masm version:** 559 more code cells, about a fifth of the
  2,911 free above emitted code once `prelude` and `strings` are loaded. That
  is a budget every masm library in a session shares.
- **Where the masm version wins:** heavily fragmented streams, where it is
  2.4× faster and allocates up to 14× less.
- **Why the Glon version allocates more:** each fragment it checks calls
  `s/scan-set`, which allocates a 16-cell workspace (32 cells with its extent).
  Empty fragments skip that scan.
- **When to revisit:** if a real fragmented-stream use appears, the variant is
  kept, tested and measured.

## The lexer: position-based tokens, identical to v004

`lx/token` reproduces v004's `p/token` with the same token form,
`[kind start end first-byte]`, the same kinds, the same whitespace set
(9–13, 32), and the same `'parse 'syntax` error for a `.` that starts no
number. No token text is allocated: a consumer slices text from its source when
it emits it, as v004's `p/text` does. `lx/tokens` wraps it as an iterator with
one cached token of lookahead, so `it/peek` on a token stream is free after the
first look.

**Differential** (`make test-lexer-diff`):
- **Inputs (400):** every input of v003's translation, error and Chris tables,
  read from `infix_node_test.mjs`, plus 300 seeded random byte strings over
  digits, `.`, `_`, letters, operators, bytes 9–13, 0, 127, 128, 200, 207 and
  255. The empty input and whitespace-only input are included.
- **Three fragmentings each:**
  - one fragment;
  - fine pieces with `""` between every piece: single bytes up to 40 bytes,
    and 40 pieces beyond that, because 130 one-byte strings do not fit the heap
    beside the libraries;
  - random cuts with empty fragments at both ends.
- **Comparison:** the frozen v004 walk over the whole string is printed once
  per fragmenting, and the outputs are compared byte for byte.

**Result: 1,200 token walks, 11,307 tokens, identical**, including 447 walks
ending in the `.` error at the same position.

## Tests

| Run | Result |
|---|---|
| `iter-tests.glon` (A one fragment, B empty input and empty fragments, C bytes 0/255 vs end, D skip, E take, F 201 fragments, including take/skip at call depth 60, G independent iterators, H errors) | 77 / 77 |
| `lexer-tests.glon` (I tokens across fragments, peek idempotence, end position, `_`, lone `.`, empty input; J 518 tokens over 74 fragments under collection, about 42K cells allocated in the 7.2K heap; K byte walk with garbage under collection) | 28 / 28 |
| both files against `variants/iter-masm.glon` | 77 / 77, 28 / 28 |
| lexer differential against frozen v004 | 102 / 102 chunks identical (1,200 walks) |
| frozen v004 `sha256sum -c FROZEN.sha256`; `./check-frozen-s1.sh` | OK; OK |

All runs are native. **Not yet run: WASM parity** (needs a CI-built
`glon.wasm`; see *Before integration*).

## Resource measurements (64K reference configuration, 2026-11-07)

Measured with `measure.c` linked against the unchanged runtime (`-O1`; the
allocation counters use the runtime's existing `-DR0_S1_PROFILE` build flag).

**Bindings, loader heap and code space**

| Library | Bindings | Loader cells | Code cells |
|---|---|---|---|
| `strings-ext.glon` (required) | 10 | 1,212 | 504 |
| `iter.glon` | 19 | 2,153 | 27 |
| `iter-lexer.glon` | 15 | 1,177 | 0 |
| **session: prelude + strings + strings-ext + iter + lexer** | **84 / 256** | **6,068 used, 8,732 free** | code ends at 13,961 |
| for comparison, the v004 session: prelude + strings + s-extra + pratt + grammar | 93 / 256 | 6,906 used, 7,894 free | 13,934 |

The iterator costs more loader heap than v004's whole lexer. v004's lexer
section of `pratt.glon` (`p/token`, `p/token-at`, `p/number`) is 3 bindings
and 562 loader cells; without it, `pratt.glon` is 40 bindings and 3,596 cells.
The difference is the price of the closure protocol, the argument checks and
fragment handling.

**Managed heap kept alive** (7,232 cells; about 5.7K free after this session's libraries)

| Value | Cells |
|---|---|
| a byte iterator (one, or 74, fragments) | 144 |
| a token iterator with its byte iterator | 288 |
| … with one cached token | 320 |

**Allocation per operation.** Profile counters, net of the measuring loop's
fixed cost, which stays 64 cells whether 10 or 60 steps run.

| Operation | Cells (allocations) |
|---|---|
| byte `peek`, `next`, `pos`, `peek2`, in one fragment or across 74 | **0** |
| `it/bytes` (constructing an iterator) | 144 (3) |
| `skip` within a fragment | 64 (2) (the `s/scan-set` workspace) |
| `take` of a 4-byte run within a fragment | 96 (3) |
| token `next` (v004-style token, no text) | about 80–85 (2.5–2.7) |
| token `peek` (cached) | about 2 |
| v004 `p/token`, for comparison (paid again on every look) | 96 (3) |

**Return stack** (probe frames still available after the operation; 84 means
an empty stack):

| Operation | Probe frames |
|---|---|
| byte `next`, one fragment | 81 |
| byte `next` at offset 2,048 | 81 |
| byte `next` across 201 alternating empty fragments | 70 |
| `skip` / `take` across 201 fragments | 69 / 69 |
| `it/bytes` over 201 fragments | 72 |
| token `next` (one fragment or 74) | 75 |
| v004 `p/token` | 79 |

None of these depends on the input's length. The cross-fragment rows depend on
log2 of the fragment count.

**Data stack:** at the deepest probe, a take across 201 fragments used 86 data-stack
cells of the 2,380 free between emitted code and the data-stack top.

**Speed** (native CLI, `-O0`): 518 tokens take about 1.05 s with the iterator
lexer against 0.47 s with v004's `p/token`, including about 0.1 s of library
loading. That is about 2 ms against 0.9 ms per token: 26 closure calls per
token against 11. For the parser's input cap of 160 bytes, that is about 0.1 s.

## Findings

1. **Code space is a third budget.** It sits beside bindings and the loader
   heap. masm workers are assembled into the code area, which grows towards the
   data stack. Code ends at 13,473 with `prelude` + `strings`, leaving 2,911
   cells. `strings-ext.glon` uses 504 of them, and a masm cross-fragment
   scanner would use 586.
2. **Fragments are heap-bounded.** Every distinct STRING! takes at least one
   32-cell extent, so a 130-byte input cut into single-byte strings does not
   fit beside the libraries. Blocks that repeat strings are cheap.
3. **Unchecked prelude words fail-stop.** `block-at` and `invoke` on a wrong
   value halt the machine, so iterator words must check their argument. The
   check costs one masm tag read, `it/raw-tag`.
4. **masm has no `T_CLOSURE` name.** `T_STRING` and `T_BLOCK` are exported, but
   the closure tag is not, so `iter.glon` compares against 8, the value of
   `T_CLOSURE` in `r0_s1.h`. Exporting the name would be a runtime change and
   was not made.
5. **Tests hit the loader heap first.** One test file with all sections plus
   three libraries exhausted the 14,800-cell loader heap, so the tests are two
   files. Iterators kept in globals also add up: each holds 144 cells or more,
   so tests release them.
6. **State in parameters works as intended.** Two iterators over the same
   fragments are independent. Under collection, state survives about 42K cells
   of allocation in the 7.2K heap.

## Limitations

- **The lexer is about 2× slower than v004 per token.** Natively at `-O0`, it
  takes about 2.0 ms per token against 0.9 ms. It makes 26 closure calls per
  token against 11. It allocates less per token, and much less per repeated
  look.
- **WASM parity has not been verified.** Every result here is native (64-bit
  integers). The tests and the lexer differential have not been run in a
  CI-built `glon.wasm` (32-bit integers).
- **Many-fragment crossings are expensive in the adopted version.** They
  allocate about 3.8K cells per skip and 11.3K per take across 201 fragments,
  and take about 183 ms per take. The masm variant is 2.4× faster and
  allocates less, at a cost of 586 code cells.
- **Loader cost.** The iterator and lexer cost 3,330 loader cells, more than
  v004's lexer section (562).
- **Not integrated.** No parser uses the iterator yet.

## How v005 would consume this (not started)

- **Keep from v004:**
  - `math-grammar.glon` and the `p/parse` / `p/nud` / `p/chain` structure;
  - the depth and chain guards;
  - the up-front bad-character scan, run on the raw source;
  - the differential harness and its v004 bars (24 engine tests, 40
    fixtures, 1,500 WASM checks, 25-term chains, 15 parenthesis levels,
    160-byte cap).
- **Change:** the parser holds a token iterator, `lx/tokens it/bytes reduce
  [ src ]`, instead of passing `src pos` through every call and re-lexing on
  every look.
  - `p/chain` peeks, decides, then advances.
  - The parse functions return text only, not `[text pos]` pairs.
  - Token text is sliced from `src` by position when emitted.
- **The order of errors matches.** The token stream raises v004's `.` error at
  the same token request as `p/token`.

**Before integration, still to do:**
1. Run `iter-tests`, `lexer-tests` and the lexer differential in WASM against
   a CI-built `glon.wasm`, for 32-bit integer parity.
2. Measure the v005 parse session with the parser loaded. Estimate from these
   figures: this session plus v004's parser without its lexer (3,596 cells,
   40 bindings) and the grammar (270, 1) comes to about 125 bindings and 9.9K
   of 14.8K loader cells, leaving about 4.9K.
3. Re-measure the parser limits, because each pending parse frame now holds an
   iterator reference and does fewer `[text pos]` allocations.
4. Decide whether the token stream's 2× per-token time matters for the page.
