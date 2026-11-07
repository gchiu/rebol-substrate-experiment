# Two independent link extractors in one Glon machine

> **Status (2026-11-07): experiment, not committed.** It changes nothing in
> frozen S1, the runtime, iterator v001, DOM v001 or Pratt v004. It adds no
> module system, scheduler or namespace mechanism.

**Question.** Do Glon's existing closures already isolate state well enough for
two instances of one program to run side by side?

**Answer: yes (verified).** Two link extractors are made by the same `mx/new`,
run the same code, and use the same shared vocabulary:
- `lk/link?`, `lk/attr` and `lk/` constants from `../glon-dom-links/links.glon`;
- `hw/` from `../glon-dom-host/hostwalk.glon`;
- `it/`, `lb/`, `s/`.

Each instance keeps its own state in the parameters of its own closure, as the
`it/` iterators do. The program walks two real HTML documents through
`glon-web`, alternating batches (A, B, A, B, ...), and each extractor produces
exactly the links of its own document, in document order.

## What changed relative to the original extractor

- **`lk/event` is not used.** It keeps its state in globals (`lk/found`,
  `lk/anchor`, `lk/text`, ...). `mx/new` returns an instance (a block holding a
  closure) whose parameters hold that state.
  - The event logic is the same as `lk/event`'s.
  - The original `links.glon` and the native demo are unchanged and still pass
    (15/15).
- **No driver flag.** The balanced driver (`mx/steps`, `mx/pump`) needs no global
  stop flag: each step checks its own walker and extractor.
- **Jobs.** A job is `[label url handle walker extractor]`.
  - The only app-level variable is `mj/current`, the job the one outstanding host
    request belongs to.
  - A finished job is replaced by a stub, so its walker and results become
    garbage before the other job finishes.
- **One host change**, in `../glon-links-native/glon-web.c`: after `dom-release`
  the host now replies `dom-released`, so a program can go on to its next
  document.
  - Programs that ignore that event (`links.glon`) end exactly as before.
  - This file is committed; the change is not yet committed.

## Tests (`make test`; all 5 pass, offline)

Each job's links are compared, byte for byte and in order, with the single
extractor's output for that document. That output is itself `--check`ed against
libxml2.

| Test | Links A / B | Alternations | Collections |
|---|---|---|---|
| 1. kept results: small page and doc-b | 3 / 6 | 5 | 93 |
| 2. kept results, reversed: doc-a and small page | 4 / 3 | 6 | 89 |
| 3. two long documents, streamed | 4 / 6 | 77 | 170 |
| 4. the **same** document as A and B, streamed: each finds its links once | 6 / 6 | 81 | 179 |
| 5. test page (entities, Unicode, nested markup) and doc-a, streamed | 11 / 4 | 25 | 112 |

In every run:
- both documents are released, and nothing leaks;
- the loader heap does not grow;
- collections run in the middle of both traversals.

## Measurements (native, 64K machine)

| | Single extractor (`links.glon`) | Two extractors (`multi.glon`) |
|---|---|---|
| Global bindings | 149 (program file: 34, 1,931 loader cells) | 157 (program file: 42, 3,137 loader cells) |
| Live baseline before any page | 2,992 | 3,216 (+224) |
| Return stack peak | 1,345–1,605 cells | 1,425 cells: **the same**; interleaving adds no depth |
| Managed peak, one long walk | +960 (doc-b, streaming) | +2,208 to +2,368 for two walks in progress |
| Per instance | — | about 100 cells of closure state, plus its walk (about 1,100 cells at peak: batch, cached event, open-element views at depth 8) and its kept links (about 120 cells each) |

## Limits found (verified)

- **Two collecting extractions of long documents do not fit together.** With
  both walks in progress and both keeping links, 6 + 10 links over two
  400-node documents exhausts the heap. It is not a leak: live cells after each
  run return to about 3,700.

  Streaming (`stream.glon`: each instance prints its links as found, labelled
  `~A`/`~B`) removes the result cost, and the same pair then passes (test 3).
- **`lb/to-block` is unchecked.** During development, a streaming instance asked
  for a block of 4 from an empty chain, and the machine fail-stopped. The fix is
  in `mx/result`. This is the masm worker documented in glon-dom-links, and it
  must be called with the chain's exact length.

## Stage 2: M1 cooperative multitasking (investigated, not demonstrated)

Combining the host-backed request/reply mechanism with M1 is **not possible
without an architectural change**:

1. **No waiting state.** An M1 task is `TASK_RUNNABLE`, `TASK_FINISHED` or
   `TASK_EMPTY` (`m1_layout.h`). `run-tasks` round-robins runnable tasks and
   returns to its caller only when **none** is runnable (`M1-MULTITASKING-RESULTS.md`;
   `run-tasks` in `experiments/tuple-space/tuple-space-v003.glon`).
2. **Host replies arrive only between runs.** A reply comes as a new dispatch
   (`glon_event_bytes`), after the current run has ended. A task waiting for its
   next batch can only `yield` and stay runnable, so `run-tasks` would never
   return, the dispatch would never end, and the host could never reply.
3. **One request per dispatch.** The host-call request is the dispatch's single
   result, so two tasks waiting at once could not both ask.

Making this work would need:
- a waiting task state;
- a `run-tasks` that returns to the host while tasks remain suspended;
- the scheduler resumed by the next dispatch.

That is a scheduler change, out of scope here and not attempted. The manual
interleaving above provides the same isolation, with the dispatch loop acting as
the scheduler.

## Files

- `multi.glon`: `mx/` instances and `mj/` jobs.
- `stream.glon`: streaming mode.
- `gen_docs.py`, which writes `doc-a.html` and `doc-b.html`.
- `check.py`, `test.sh`, `Makefile`.
