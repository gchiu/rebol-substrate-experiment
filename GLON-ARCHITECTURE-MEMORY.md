# GLON-ARCHITECTURE-MEMORY.md — architectural decisions and their rationale

This is the **authoritative index of Glon's architectural decisions and the
reasons for them**. Read it before changing the runtime, the memory map,
resource limits or language semantics (see `AGENTS.md`).

It is **subordinate** to two things:

1. **`GLON-ALPHA-LAWS.md`**, the normative language contract. Where this
   document summarises a law, the laws win.
2. **The implementation**: `s1.c`/`s1.h` (frozen), `r0_s1.h`, `m1_layout.h`,
   `r0_s1_runtime.c`. Numbers quoted here are snapshots. The code is the ground
   truth for addresses and sizes.

`MEMORY.md` remains the **canonical project history** (milestones, benchmarks,
lessons). This document does not repeat it.

Every entry is one of four kinds, and is labelled so:

| Label | Meaning | May an agent change it? |
|---|---|---|
| **[DECISION]** | an established design decision, with its rationale | only by an explicit, approved proposal that cites the entry |
| **[MEASURED]** | a measured fact about the current implementation, dated, with its configuration | re-measure and update; never treat as a decision |
| **[FINDING]** | an experimental result from a scratch or experiment build, not a property of the shipped system | record it; do not act on it without a proposal |
| **[PROPOSAL]** | an idea that has **not** been authorised or implemented | needs explicit approval before any implementation |

Unless stated otherwise, every [MEASURED] figure is for the **64K reference
configuration** (65,536 cells), the only configuration that exists.

---

## 1. Objective

**[DECISION] Constrained computing is a design objective.** Glon investigates
how much useful computation can be accomplished with a **small, stable
substrate and constrained resources**. It builds high-level language machinery
above a tiny frozen machine, rather than by continually enlarging the machine.

- **The 65,536-cell machine is the reference machine.** Its size was
  deliberately inspired in part by the Commodore 64. It is **not** an attempt
  at C64 emulation (there is no 6502, no C64 memory map, no C64
  compatibility). The 64K bound is a chosen constraint under which Glon's
  capabilities are measured.
- **Consequence:** a resource limit met within 64K is first a *finding about
  constrained computing*, not automatically a defect to be fixed by adding
  memory (see §8).

Sources: the user's design direction (recorded 2026-11-06); `MEMORY.md` §1
("rather than by continually enlarging the machine"); the `m1_layout.h` layout
note ("a memory-limit tradeoff: 65536 cells total").

## 2. Layers and boundaries

```text
hosts             browser WASM + JS host, native CLI (glon), Jupyter kernel, desktop
   ↑ host ABI       (GLON-WASM-ABI.md, GLON-APP-MODEL.md, GLON-HOST-CAPABILITIES.md)
vocabularies      s/ strings, case, l/ LaTeX, p/ Pratt, app dialects: loadable Glon
   ↑
Glon language     words, blocks, contexts, closures, SIN!: GLON-ALPHA-LAWS.md
   ↑
R0 runtime        evaluator, GC, loader, tasks: S1 code EMITTED by r0_s1_runtime.c
   ↑ RAW / masm     the generic low-level trapdoor
S1 substrate      7 primitives, flat cell memory, memory-mapped registers: FROZEN
```

- **[DECISION] S1 is frozen.** It has exactly seven primitives (`LIT DUP DROP @
  ! 0BRANCH HOST`) and no eighth; calls, returns, jumps, loops and non-local
  control are derived. The frozen files are `s1.c`, `s1.h`, `tests.c`,
  `adversarial.c` and `claims.c`, at tag `s1-frozen-v1` (`f90496c…`).
  `./check-frozen-s1.sh` must pass, locally and in CI. Sources: `MEMORY.md` §2;
  `R0-ARCHITECTURE.md`; `CLAIMS.md`; `ADVERSARIAL.md`.
- **[DECISION] HOST is mundane.** It covers arithmetic, comparison, allocation,
  I/O and diagnostics, and never evaluator or control semantics. Source:
  `MEMORY.md` §2.
- **[DECISION] Abstractions are permeable (the Substrate Principle).** Every
  high-level layer keeps a defined trapdoor to the one below it (RAW/masm to S1;
  raw HTML/DOM to the browser). The shorthand is "stay in Glon unless you
  genuinely need to descend". Sources: `DESIGN.md`; `substrate-principle.md`.
- **[DECISION] Mechanisms in the kernel, meanings in loadable Glon.** Runtime
  natives provide small, general mechanisms (allocation, byte access, control,
  failure). Meanings (string operations, formatting, domain vocabularies) live
  in loadable Glon libraries. That keeps the emitted runtime small (code space
  is a measured constraint, §6), lets sessions load only what they use, and
  keeps semantics readable.
  - Applied as "candidate D" (commit `6a14fb3`): `s/+ s/= s/length s/print`
    are library code (`demo/shop/strings.glon`) over masm workers. As for any
    function, both arguments are evaluated before the type check.
  - Sources: `GLON-HANDOFF.md` (local, untracked, 2026-09-25) §"Principle" and
    §"Decision: candidate D"; `GLON-ALPHA-LAWS.md` §9a.
- **[DECISION] JavaScript is a host, not the application language.** A browser
  host moves text and bytes and presents output. Application semantics and
  vocabularies stay in Glon. Sources: `substrate-principle.md`;
  `GLON-APP-MODEL.md`; `GLON-ALPHA-LAWS.md` §13.
  - The v003 infix translator (`experiments/glon-latex/web/infix.js`) is an
    explicitly *front-end* notation layer that emits Glon source. It is not an
    exception to this rule.
- **[DECISION] `domain/operation` names are flat words for now.** `s/+` is one
  word, and a bare `/` is division. The spelling is deliberately path-like so
  these words can later become real path or context lookup without renaming.
  No path evaluation or namespaces exist. Sources: `GLON-ALPHA-LAWS.md` §2;
  `GLON-HANDOFF.md` §"Current language surface".

## 3. Iteration

**[DECISION] Recursion is the current iteration mechanism.** Glon has no loop
words (`while`, `repeat`, `foreach`, `each`, `loop`). Iteration is written as
recursion, and bulk work over bytes is done by library masm workers that loop
in S1 (for example `s/raw-cat`, `s/raw-span`).

This is **not a permanent prohibition.** Native looping constructs are
*deferred* pending evidence from real applications: "Post-Alpha language
changes require evidence from real machine-written applications or a
demonstrated correctness defect" (`GLON-ALPHA-LAWS.md` §14).

- **[MEASURED]** The cost of this choice: recursion depth is bounded by the
  return stack, at about 84 nested Glon calls (§6). That bound shapes
  libraries: the Pratt parser (§7) is shaped to stay within it.
- **[PROPOSAL]** Tail-call optimisation is a **separate proposal** (§9), not
  implied by this decision.

## 4. Binding, contexts and escape laws

These are summaries; **`GLON-ALPHA-LAWS.md` is normative.**

- **[DECISION] Words and contexts.**
  - A word is looked up dynamically through the lexical parent chain (§2).
  - `T_BOUND` is a pre-resolved `(depth, slot)` reference, valid only under an
    activation of the block's site: the Guard of Binding (§2–§4).
  - A set-word updates the nearest existing binding, else it creates a local of
    the current call. Library code must therefore choose local names that no
    global uses (the Pratt parser uses a `pt-` prefix).
- **[DECISION] Blocks are not portable executable objects; closures are.** A
  bare block that depends on an activation may not outlive it. The runtime
  refuses at the moment of escape, raising `SIN! 'escape` with the id of the
  boundary that detected it: `'return`, `'argument`, `'store`, `'capture` or
  `'transport` (§3, §4, §6a, §12).
- **[DECISION] Closure-origin binding.** A closure captures its actual lexical
  context. A captured stack-local context is promoted to the managed heap on
  escape (§4).
- **[DECISION] Failures.** A SIN! is a value; `raise` propagates; `judge`
  catches. **Machine-integrity failures** are fail-stops, not SIN!s, and cannot
  be judged: return-stack exhaustion, out of memory, `CONTEXT_FULL` (§6a, §11).
- **[DECISION] Capacities fail safely, and are not raised casually.** A
  resource limit must fail safely (a structured fail-stop or a clean parse
  failure, never silent corruption). **Raising a limit is a separate,
  deliberate milestone.** Sources: `GLON-HANDOFF.md` §"Invariants" (local,
  2026-09-25); `GLON-ALPHA-LAWS.md` §11.

Any change to the evaluator's call or return machinery must preserve these
laws. That includes TCO, stack relocation and segmentation, which interact with
escape law (1), definitional `return`, the Guard of Binding and stack-local
contexts (§9).

## 5. Memory map (64K reference configuration)

[MEASURED] As defined in `s1.c` (frozen), `r0_s1.h` and `m1_layout.h`, checked
2026-11-06. The headers are authoritative.

| Region | Cells | Size | Notes |
|---|---|---|---|
| registers, scratch | 0–255 | 256 | IP/SP/RP/HP memory-mapped (`s1.h`) |
| emitted runtime code | 256–12,593 | 12,337 | grows with every native |
| main data stack | 12,593–16,384 (grows down) | ~3,791 headroom | `DS_INIT` 16,384 |
| **main return stack** | 16,384–24,576 (grows down) | **8,192** | `RS_INIT` 24,576; frames, **stack-local contexts**, continuations |
| runtime state | 24,576–32,768 | 8,192 | datatype meta/builtins, `RV_*` registers (base 24,595), M1 scheduler (25,010…), GC state incl. **`GC_WORKLIST` 25,057–25,312 (256 entries)**, `GC_A1`/`GC_A2` 25,313/4, `G1_OUT` 25,344 (4,096), `G1_VIS` 29,456 (2,560), `SITE_PARENT_BASE` 32,024, `RV_ESC_*` 32,664… |
| **managed heap** | 32,768–40,000 | **7,232** | non-moving mark/sweep; seeded datatype objects at fixed addresses at its base |
| loader heap | 40,000–54,800 | 14,800 | permanent (source literals, symbols); never swept |
| M1 task arenas | 54,800–65,000 | 3 × 3,400 | each task: 400-cell data stack + 3,000-cell return stack |
| task table | 65,000–65,536 | 536 | |

The runtime's logic, not only its constants, assumes this **order**:
- "SP ≤ `DS_INIT` ⇒ main world, otherwise a task" (GC roots, return-stack
  guards, stack sentry, context promotion);
- the main return stack sits directly on top of the main data stack;
- stack-local contexts, managed objects and loader objects are classified by
  **address range**.

Any new layout must preserve the order, or rewrite that logic.

## 6. Resource limits and measured facts (64K)

[MEASURED] unless marked otherwise. Configuration: the native build from the
current sources, and the CI-built WASM, which behave identically. Dates in
brackets.

| Resource | Limit | Behaviour at the limit |
|---|---|---|
| activation size | ≤ 96 cells by construction: 16-cell frame + 64-cell context + padding + return address; **~97 measured** including continuations [2026-11-06] | — |
| nested Glon calls (main) | **~84** plain recursion; 42 with two functions per level; no tail calls [2026-11-06] | fail-stop (return-stack exhaustion, not judgeable) |
| managed heap | 7,232 cells; **one cell per string byte**; ~4.2K free after prelude + strings + the Pratt libraries [2026-11-06] | out-of-memory fail-stop; fragmentation can fail a large allocation with total space free |
| smallest managed object | **32 cells** (16-cell header + ≥16-cell payload, because payloads are 16-aligned) [2026-11-06] | — |
| global bindings | 256 per session | `CONTEXT_FULL` fail-stop before any write (§11) |
| bindings per function | 16 | `CONTEXT_FULL` |
| values per block / per string literal | 512 values (a quoted literal costs 2); 512 bytes per string literal | clean parse failure (`too_large`) |
| loader heap | ~14,800 cells, permanent | `loader_exhausted`. Large test suites must be split across sessions (`experiments/glon-latex`) |
| symbols, function sites | 512, 640 per session [handoff, 2026-09-25] | clean parse failure |
| masm code-space limit | `R0S1_CODE_LIMIT` = 16,128 (data stack keeps 256 cells) [2026-11-09] | a masm block that would end above it is refused before emitting (`too_large`); the stack sentry compares the deepest SP with the current code end |
| emitted code / data-stack headroom | 12,670 cells / ~3,458 cells, after the GC object-start check added 216 cells [2026-11-09] (12,454 / ~3,674 after the GC worklist guard added 117, from 12,337 / ~3,791 [2026-11-06]) | each native costs far more than it looks (a CALL ≈ 21 cells, an EXIT ≈ 17 [handoff]) |
| masm library code | masm workers are assembled into the same code area, which grows towards the data stack: `prelude` 416 cells, `strings` 304, `strings-ext` 504. With `prelude` + `strings` loaded, code ends at 13,473, leaving 2,911 cells below the data-stack top (16,384); the data stack used ≤ 91 cells at maximum recursion depth [2026-11-07, `experiments/glon-iter`] | a third budget beside bindings and the loader heap, shared by every masm library in a session |

**[MEASURED] GC worklist (investigated and guarded 2026-11-06).** The mark
worklist has a fixed 256 entries (`GC_WORKLIST_CAP`, the cells before `GC_A1`).
- **It cannot overflow in the 64K configuration.** Each object is pushed at
  most once per collection (the mark bit is set first), only managed objects
  are pushed, and the smallest object is 32 cells (`GC_MIN_EXTENT`). So at
  most ⌊7,232 / 32⌋ = **226** objects can exist, below 256. The highest value
  observed in valid programs was 217.
- **[DECISION] Two safeguards** (implemented 2026-11-06, approved by the user):
  1. **Build-time:** `r0_s1.h` asserts `GC_WORKLIST_CAP ≥ (GC_HEAP_LIMIT −
     GC_HEAP_BASE) / GC_MIN_EXTENT`, and that the managed heap ends where the
     loader heap begins. A larger managed heap no longer builds until the
     worklist is enlarged.
  2. **Runtime:** every push is bounded. A full worklist is a machine resource
     fail-stop, like `CONTEXT_FULL`, not a SIN! (no `judge` catches it):
     - it first clears every mark bit, so the abandoned collection leaves the
       heap exactly as it was;
     - it then records `R0S1_HALT_GC_WORKLIST_FULL`;
     - the session reports `RESOURCE_ERROR` / `gc_worklist_full`, and a
       persistent session keeps working.
  Unreachable at 64K; it also covers RAW code that allocates payloads under
  16 cells.
- **[FINDING]** The separate enlarged-heap test build
  (`make gc-worklist-overflow-test`, the only build defining
  `R0S1_GC_OVERFLOW_TEST_BUILD`: an 11,232-cell heap with the build-time
  assertion skipped) exercises the runtime bound. Setting the heap boundary
  any other way is a compile error.
  - With the bound, every overflow case ends in a controlled `gc_worklist_full`
    with nothing written past the worklist, and the session recovers.
  - Against the previous runtime, the same test shows `GC_A1`/`GC_A2`
    overwritten, a spurious out-of-memory and a broken session afterwards.
- **Consequence:** a managed heap larger than **8,192 cells** still needs a
  worklist sized by memory profile (§9, item (c)), which is **not authorised
  or implemented**.

**[MEASURED] A local WASM artefact can be stale.** `demo/shop/glon.wasm` is a
*git-ignored local build output*, not a committed file. CI rebuilds it before
every publish. The local copy here (built 2026-10-07) predated the current
runtime and exhausted its heap after 6 operator terms in the Pratt parser,
where native and CI-built WASM handle 25. **Test WASM behaviour against a
build of the current sources.**

## 7. Vocabularies and experiments (status)

| Item | Status | Where |
|---|---|---|
| `s/` strings (`s/+ s/= s/length s/print`) | shipped library | `demo/shop/strings.glon`; laws §9a |
| `s/` string extension (`s/at s/slice s/span s/scan-set s/from-int`) | shipped **optional** library (API approved 2026-11-07); loaded after `strings.glon` only by sessions that need it | `demo/shop/strings-ext.glon`; laws §9a; tests `r0_s1_string_ext_tests.c` |
| `l/` LaTeX vocabulary v001–v002, `/latex/` page, v003 infix front end (JS) | **deployed** (milestone complete 2026-11-07: `3bfabe9`, `6cdae85`, `e5df1a0`); tested in CI on every publish | `experiments/glon-latex/` |
| `p/` Pratt parser v004, written in Glon, plus its own `s/at s/slice s/span s/from-int` (its `s/span` is the byte-set scanner) | **[FINDING] experiment, committed but not deployed; a subset of v003's grammar, not a replacement for it; frozen by checksum (`FROZEN.sha256`)** | `experiments/glon-pratt/GLON-PRATT-V004.md` |
| `it/` iterator vocabulary v001 and `lx/` lexer over it (after Chris Ross-Gill's iterator concept) | **[FINDING] experiment, not deployed, not yet used by a parser** | `experiments/glon-iter/GLON-ITER-V001.md` |
| `dom/walk` DOM-tree event iterator v001 and `it/close` (after Chris Ross-Gill's DOM iterator) | **[FINDING] experiment, not deployed, not used by an application** | `experiments/glon-dom/GLON-DOM-V001.md` |
| `lk/` link extractor over `dom/walk`, and `lb/` chain-to-block helper | **[FINDING] experiment, not deployed** | `experiments/glon-dom-links/README.md` |
| `hw/` host-backed DOM walker; browser host (`glon-live.wasm`) | **[FINDING] experiment, not deployed; one WASM failure unexplained** | `experiments/glon-dom-host/REPORT.md` |
| `glon-web`: a native host (libcurl, libxml2) running a Glon link-extraction program | **[FINDING] experimental milestone, not deployed; a separate executable (the `glon` CLI is unchanged)** | `experiments/glon-links-native/README.md` |
| `dz/` streaming DEFLATE decompressor (RFC 1951) in Glon, after Chris Ross-Gill's `deflate.reb` | **[FINDING] experiment, not deployed; found a collector defect, since repaired** | `experiments/glon-deflate/README.md` |
| Glon/Binary: an optional `bin/` binary-buffer library | **[PROPOSAL] investigated, not implemented** | `GLON-BINARY-FEASIBILITY.md` |
| `mem/` minimal addressable-memory vocabulary (Forth-style: alloc, release, byte, cell, move) over M3 USER objects | **[FINDING] prototype v002, not deployed and not adopted; native and WASM tested** | `experiments/glon-mem/README.md` |
| masm code density and composition (microbenchmarks, stack high-water marks) | **[FINDING] investigation** | `experiments/glon-masm/README.md` |

**[DECISION] The string extension API (approved by the user 2026-11-07).**
Five byte-level operations in a separate optional module,
`strings-ext.glon`. Indices are 0-based bytes; ranges are half-open as in
Python, but with no negative indices and no clamping:
- `s/slice s start n`: start and length;
- `s/span s start end`: start and exclusive end;
- `s/scan-set s i set`: the first index at or after `i` whose byte is not in
  `set`. Unlike extraction, it clamps `i` to [0, length], because scanning at
  or beyond exhaustion is a normal use case;
- `s/at s i` and `s/from-int n`.

The unchecked `s/raw-...` masm workers must be called only through the
validated public wrappers; the regression tests for invalid ranges
(`r0_s1_string_ext_tests.c` B5–B8, E6–E9) must be kept.

The extension stays optional: neither the 64K machine nor `strings.glon` is to
be changed to accommodate it.

v004 used the name `s/span` for the scanner. This API renames it
`s/scan-set` and gives `s/span` to range extraction. Frozen v004 keeps its own
copy under the old names, so the two files must not be loaded into one session.

The module is separate because `strings.glon` is loaded by every interactive
page. Folding the extension into `strings.glon` would need either Kaka to stop
loading `strings.glon` or a capacity change.

[MEASURED] (2026-11-07, 64K configuration)
- The module costs 10 global bindings and 1,212 loader-heap cells.
- Attack of the Mutant Kaka's browser session, which loads `strings.glon`, uses
  252 of 256 global bindings with 242 loader cells free. With the extension it
  fails to load.
- Other sessions with `strings.glon`, global bindings without → with the
  extension: Jupyter/CLI 40 → 50, primer 150 → 160, Patrol 120 → 130, LaTeX
  page 95 → 105.

**[FINDING] Pratt parser v004 (2026-11-06; re-verified 2026-11-07 against
the runtime of `96a0d13`).** A native Glon Pratt parser for a **subset** of the
v003 grammar: names, integers, decimals, parentheses, unary minus and
`+ - * / ^`. It is **not** a replacement for v003, which stays the deployed
translator: v004 lacks functions, relations, lists, implicit multiplication,
`[ ]` and symbols, so it parses none of Chris Ross-Gill's 17 figures. It is
exact against the v003 reference on its subset: 40/40 native fixtures and
1,500/1,500 WASM differential checks.

Names: v004 ships its own frozen `s-extra.glon`, in which `s/span s i set` is
the **byte-set scanner**. In the standard `strings-ext.glon` that operation is
`s/scan-set`, and `s/span s start end` extracts a half-open range. The two
files must not be loaded into one session. Its limits under the 64K configuration:
- 25-term operator chains;
- 15 levels of parentheses;
- 10 levels of nested products;
- a 160-byte input cap.

Two different resources bound it: the **return stack** for nesting, and the
**managed heap** (including fragmentation) for long chains. Three guards turn
both into catchable errors before a fail-stop.

**[FINDING] Iterator vocabulary v001 (2026-11-07, 64K configuration, native).**
An optional library, `iter.glon`, implements iterators in plain Glon:
- **Mechanism:** an iterator is a one-element block holding a closure that
  answers numbered messages. Its state lives in the constructor's parameters.
  It needs no ITERATOR! type, evaluator change or substrate change.
- **API:** `it/bytes frags` reads a block of strings as one byte stream, with
  `it/peek it/peek2 it/next it/pos it/skip it/take it/end it/end?`.
- **Lexer:** `lx/tokens` in `iter-lexer.glon` is itself an iterator. Its
  position-based tokens are identical to frozen v004's `p/token` on 1,200
  walks (400 inputs, each cut into fragments 3 ways).
- **Tests:** 77 + 28 native tests pass.

[MEASURED] costs:
- `iter.glon`: 19 bindings, 2,153 loader cells, 27 code cells;
- `iter-lexer.glon`: 15 bindings, 1,177 loader cells;
- managed heap kept alive: 144 cells per byte iterator, 288–320 per token
  iterator;
- byte `peek` and `next` allocate nothing; a token costs about 80 cells;
- the return stack per advance does not depend on input length; crossing
  fragments costs about log2(fragments) frames.

The bounded cross-fragment `it/take` uses balanced Glon recursion. A masm
scanner (`variants/iter-masm.glon`) was measured and not adopted:
- **Against:** it is equally bounded, but costs 586 code cells;
- **For:** it is 2.4× faster and allocates up to 14× less, but only on streams
  of hundreds of fragments.

Not yet run in WASM. Details are in `experiments/glon-iter/GLON-ITER-V001.md`.

**[FINDING] DOM-tree iterator v001 (2026-11-07, 64K configuration, native).**
The v001 iterator protocol generalises to hierarchical traversal with no
change to `iter.glon`:
- **Mechanism:** `dom.glon` walks a tree held as Glon data (text is a STRING!,
  an element is `[name attrs children]`). It produces `[kind node depth]`
  events, `open close empty text`, ending with `it/end`. The v001 words
  `it/peek it/next it/pos` work on it unchanged. One new word, `it/close`
  (message 6), skips a subtree.
- **State:** the closure's parameters hold the innermost level. Each open level
  is one 32-cell parent frame in managed data; no Glon activation is held
  between events.
- **Tests:** 87 native tests pass (67 + 8 + 7 + 5).

[MEASURED] costs:
- 6 bindings, 1,033 loader cells, 0 code cells;
- 176 managed cells per walker, plus 32 per open level;
- `next` allocates 32 cells (64 for open);
- the return stack per advance is **5 probe frames at depth 0 and at depth
  59**. A plain recursive walk uses about 2 frames per level and fails beyond
  about 42 levels.
- **Depth is bounded by the managed heap:** 150 levels walk; 160 exhaust it
  with the test fixtures loaded. At that limit the free cells are 32-cell holes,
  so an allocation larger than 32 cells (an 80-cell string) fails while ~600
  cells are free.

Known limitations:
- `it/close` on a v001 byte iterator raises a misleading `'type 's/scan-set`.
  v001 is deliberately not changed for this.
- A consumer that drains any iterator one recursive call per event exhausts the
  return stack after about 84 events, because Glon has no loop construct; the
  tests use balanced drivers.
- An explicit immutable-state alternative allocates about 2× per event but can
  be resumed more than once (fork); it was not adopted.

Details are in `experiments/glon-dom/GLON-DOM-V001.md`.

**[FINDING] Host-backed DOM traversal and native `glon-web` (2026-11-07, 64K
configuration).**

What was shown:
- **Real HTML from a native host.** `glon-web` retrieves real HTML with libcurl
  and parses it with libxml2's HTML parser. A Glon program performs the
  traversal and the hyperlink extraction: it decides which elements are links,
  joins their text across nested elements, and builds the results.
- **Exact results on real pages.** The links of iana.org (31), rebol.com (20) and
  Wikipedia "Rebol" (449) equal libxml2's own `[href, text content]` lists, byte
  for byte.
- **The same Glon code in the browser.** It runs in Chromium through
  `glon-live.wasm`, and matches a JavaScript reference on the test page and the
  100- and 1,000-node pages.
- **Glon never holds the document.** It holds:
  - a document handle;
  - the current batch (at most 120 bytes);
  - one view `[name attrs parent]` per open element.

  The host keeps the document and the cursor. `lk/event` (glon-dom-links) is
  reused unchanged.

**[DECISION] The host boundary is asynchronous; no new primitive.**
- Frozen S1 has no host operation that reads. A synchronous Glon → host call
  returning a value would need a new frozen-S1 primitive, so none was added.
- Host-backed traversal uses the existing `GLON_LIVE` asynchronous request/reply
  mechanism. Glon ends a dispatch with a request fragment
  (`dom-next "handle max close-depth attrs bytes"`). The host replies with
  `glon_event_bytes`; the dispatcher reclaims the loader heap its wrapper used.
- Within a batch, `it/peek`, `it/next`, `it/pos` and `it/close` keep their
  meaning. When the batch is used up, the walker answers a third sentinel,
  `hw/wait`, and the consumer resumes after the reply.
- Writing host replies into Glon's memory from inside `host_print` was rejected
  as an undocumented side channel.

**[DECISION] Glon owns the handle's lifetime.** In `glon-web`, a handle is valid
until Glon sends `dom-release`. The host then frees the document and changes the
slot's generation, so a stale or forged handle is refused, never dereferenced.
The browser host still releases at the end of a walk.

[MEASURED] (native, `-O1`):

| Measurement | gen-100 | gen-1,000 | gen-10,000 |
|---|---|---|---|
| Nodes | 144 | 1,048 | 10,048 |
| Glon peak above baseline, printing links as found | +1,728 | +1,344 | +1,424 |
| Return stack | 1,413 cells | 1,526 cells | 1,526 cells |

- **Constant Glon heap.** Glon's heap usage for the 10,048-node document is
  approximately the same as for the smaller pages. The return stack stays at
  1,413–1,526 cells, and loader growth is 0. The baseline before any page is
  2,992 of 7,232 cells.
- **Session cost:** 149 of 256 bindings; loader 11,119 used, 3,681 free; code
  ends at 14,371.
- **Host memory is outside Glon:** libxml2 holds the document (1.4 MB for 10,048
  nodes, 1.9 MB for the Wikipedia page).
- **Printing links as they are found avoids the result-retention limit.**
  Keeping links costs about 120 cells each during the walk (about 100 after), so
  about 50 fit. The Wikipedia page's 449 links print within +1,792 cells.
- **Copying the whole document into Glon** as `[name attrs children]` blocks costs
  about 70 managed and 37 loader cells per node. It fails at about 100 nodes, and
  a 1,250-child element exceeds a block's 512 values.
- **Performance** is approximately 1.1 ms per event (23 s for 10,048 nodes).
  Glon is the bottleneck, at about one collection per batch.
- **Smaller live baseline.** Loading `emit.glon` (four words copied from
  `common.glon`) instead of `common.glon` cuts the live baseline from 5,344 to
  2,368 cells.

**Outstanding limitations:**
- **The WASM sequential-session failure is unexplained.** In the browser, the
  10,000-node page exhausts the managed heap when it follows another page in the
  same session. It passes in a fresh session, and natively in repeated runs,
  with no leak.
- **Deep nesting and non-moving heap fragmentation remain limits.**
  - Each open element costs 64 cells in Glon.
  - A batch string needs one contiguous block: 240-byte batches fail beyond
    about 30 levels, and 120 bytes reaches 40–50.
- **The existing `glon` CLI is unchanged.** It has no host extension point and
  treats every argument as a file. `glon-web` is a separate executable, so
  `glon links.glon URL` would need an approved CLI change.
- **No scripts run.** libxml2 is not an HTML5 browser, and scripts do not run in
  either host, so links created by JavaScript are not seen.
- **Text tidying is shallow.** The readable output's whitespace tidying recurses
  once per word, so displayed text is cut at 80 bytes (`--raw` is exact).
- **Two earlier findings still apply:** Glon has no growable block (the link
  experiment adds a masm chain-to-block worker), and the extractor's state is
  global, so it is not re-entrant.

**[FINDING] Streaming DEFLATE decompressor (2026-11-09, 64K configuration,
native, `experiments/glon-deflate/`).**

What was shown:
- **Plain Glon, no runtime change.** `inflate.glon` decodes raw DEFLATE: stored,
  fixed and dynamic blocks, canonical Huffman codes, overlapping copies,
  malformed and truncated input. It has no masm, no new native, and no
  change to S1, the runtime or the ABI.
- **A resumable iterator.** `dz/feed`, `dz/read`, `dz/state` and `dz/finish`.
  Input arrives through the existing GLON_LIVE byte event, in any split down
  to 1 byte. Each unit runs under `judge`, and the bit reader rolls back to a
  checkpoint when input runs out mid-unit.
- **Conformance.** Of 480 differential runs against C zlib (164 fixtures, 7
  chunk/read schedules), 478 pass on the unchanged runtime and 479 with the
  proposed collector fix. Every malformed (222) and truncated (24) case
  matches zlib's class and its bytes before the error. No run produced a wrong
  byte: the failures are fail-stops.

[MEASURED] limits and costs:
- **No 32 KB window fits.** At one cell per byte, the managed heap cannot hold
  RFC 1951's window. `dz/new w` keeps w bytes, 768 in the host (covering zlib
  `windowBits` ≤ 10), and a longer distance raises `'window`.
- **Input and read sizes.** Input chunks are ≤ 128 bytes per feed; a read
  returns ≤ 256 bytes.
- **Memory.** Peak live is about 4,500–5,000 of 7,232 cells. The session's 64
  closures alone take 2,048 cells (32 per `func`).
- **Fragmentation.** A dynamic header needs strings of about 300 contiguous
  cells, so one run in 480 fail-stops on heap fragmentation (2,944 cells free
  in 19 holes).
- **Speed.** About 2.4 KB/s for text against about 840 MB/s for C zlib (about
  340,000× slower). One suspend/resume round trip costs about 2.8 ms. A
  dynamic header costs 0.5–0.9 s.
- **Accumulators in recursion are retained.** Glon has no tail calls, so an
  accumulator passed down a recursion is kept alive in every pending frame.
  Build by balanced halves, or update one variable in place.

**[FINDING] Collector defect (now repaired): the return-stack closure scan
accepted interior pointers into STRING!s** (found by the DEFLATE experiment,
2026-11-09).
- **The test.** `emit_mark_closure_inline` treats a 16-aligned heap value p
  as a closure if the cell at p − 15 has the closure kind.
- **The hole.** Inside a STRING! (one byte per cell), a byte of 8–11 passes
  that test. A stale integer n in an unbound stack-context slot is the
  aligned cell 16n.
- **The effect.** `mark_push` then writes the mark bit into a byte of a live
  immutable string. Its "fields" may also fail-stop the mark phase
  (out-of-range T_CONTEXT/T_USER/T_STRING/T_BLOCK).
- **Reproduction.** `experiments/glon-deflate/tests/gc-interior-repro.glon`, in
  plain Glon, prints `corrupted at 0 ...` and `11`.
- **Scope.** This is the only conservative identification in the collector.
  The other roots are exact: the global context, `RV_CTX`/`RV_CHILD`, the
  frame chain (`mark_frame`), stack contexts traced up to their bound count,
  and the data stack under the GC-safepoint invariant (tagged values only).
  Real closure payloads are always p − 16 = header, so the check rejects no
  genuine pointer. Stale integers that equal a real closure's payload can
  still retain that closure (conservative retention), which is harmless.

**[DECISION] Repaired (2026-11-09, approved as a correctness repair: "if the
fix is sound, implement it").**
- **The fix.** `emit_mark_closure_inline` accepts p only if a walk of object
  sizes from `GC_HEAP_BASE` up to `REG_HP` lands exactly on p − 16.
  - **Why the walk is exact:** every cell below the frontier belongs to one
    allocated or free block (alloc splits write remainder headers; sweep
    writes coalesced headers), and the mark phase changes only flags.
  - **What it changes:** no S1 file, no memory-map change, no language
    semantics.
- **Regression test:** `r0_s1_gc_interior_tests.c`, in the `s1` suite.
  - **Unchanged runtime:** 4 of 8 checks fail (byte 9 silently becomes 11,
    then a fail-stop).
  - **Fixed runtime:** 8 of 8 pass, including closures referenced only from
    the stack surviving collections (no premature collection).
- **All suites unchanged.** `make test` (985 checks), `glon-live-native-test`,
  `patrol-test`, the seven `kaka-*` tests and `glon-smoke` pass identically on
  both runtimes. Only code addresses in the dumps of deliberate fail-stop
  tests move.
- **[MEASURED] cost:**
  - **code:** +216 emitted cells (12,454 → 12,670), so data-stack headroom
    falls to ~3,458;
  - **collection time:** about +40–60% in a microbenchmark with 183 live
    objects and 10 closure frames (0.9–1.4 ms → 1.3–2.2 ms per collection);
  - **DEFLATE end to end:** 5–30% slower.
- **[PROPOSAL] A cheaper exact check.** An object-start bitmap (452 bits for
  the 64K heap, kept by alloc and sweep), or one sorted pass over all
  candidates per collection. Neither is authorised.

**[FINDING] Minimal memory vocabulary `mem/` (2026-11-09, 64K configuration,
`experiments/glon-mem/`).**
- **The vocabulary.** An optional library: `mem/alloc release released? size
  byte set-byte cell set-cell move`, plus two STRING! bridges.
  - A region is an M3 USER object with `count` 0, so the collector never
    traces its payload.
  - Bytes are packed 3 per cell, identical in 64-bit native and 32-bit WASM.
  - A cell is 4 bytes, little-endian, at any offset, with values limited to
    the portable range [−2^27, 2^27).
- **No change below the library.** No change to S1, the ABI or the runtime.
- **The collector repair (`e988c19`) is a prerequisite.** Packed contents can
  carry closure-kind bits, and the pre-repair runtime fail-stops on the tests.
- **Gap and limit.** What was missing was a *checked, owned, mutable* object
  at the language level (S1 `@ !` already address every cell). The 7,232-cell
  heap is a separate storage limit.
- **Tests:** 66/66 native; 64/64 in `glon.wasm` under node, with 2 cases not
  expressible in 32-bit integers reported as skipped.
- **The M3 datatype vocabulary** (`make`, `type?`) is not loaded in ordinary
  sessions. The library therefore identifies regions by descriptor identity.

[MEASURED] (native, `-O1`):
- **Library cost:** 28 bindings, 1,931 loader cells, 1,098 code cells (code
  then ends at 15,291 of 16,384), 608 managed cells.
- **Density:** a region holds about 3 bytes per cell (1,024 cells for 3,000
  bytes against a STRING!'s 3,024).
- **Speed:**
  - `move` costs 0.5–0.6 µs per byte;
  - checked byte access costs 94–118 µs, against 32 µs for `s/at`; the
    unchecked worker takes 3 µs, so the Glon checks dominate;
  - the string bridges, Glon recursions, cost 110–264 µs per byte.
- **A 32 KiB window does not fit:** one region needs 10,944 cells, more than
  the whole managed heap.

**[FINDING] Memory-library cost investigation, GLON-MEM v002 (2026-11-09,
64K configuration, native and WASM).**
- **Same contract, rebuilt.** The same API, contract and tests, rebuilt
  around three self-validating masm words that answer `none` on any failure.
  The public words raise the precise SIN! only on the failure path.
- **v001 safety defect fixed.** v001's unchecked workers were global words,
  so any program could write outside a region. v002 has no unchecked
  reachable word.

[MEASURED] (v001 → v002, one harness, `-O1`, median of 3):
- **Checked access:** byte read/write 101/129 → 31/26 µs; cell 159/308 →
  34/25 µs.
- **String bridges:** `to-string` 287 → 1.3 µs/byte, allocating 25,824 → 288
  cells per 256 bytes; `from-string` 118 → 0.31 µs/byte.
- **Library footprint:** bindings 28 → 17, managed cells 608 → 448.
- **Worse:**
  - code 1,098 → 1,577 cells, so data-stack room with prelude, strings and
    strings-ext falls 1,093 → 614;
  - large moves 0.58 → 2.1 µs/byte, through temporary strings.
- **Where checked access goes:** a Glon function call costs 4–8 µs, a
  balanced-loop iteration ~42 µs, a validated masm access 6.6 µs (2.3
  unchecked).

[MEASURED] **Constraints on masm libraries:**
- **Block size:** a masm block holds at most 510 tokens (`MASM_TOK_CAP`), and
  masm cannot `CALL` another masm word.
- **Control-flow cost:** S1 derives `BRANCH` (5 cells), `>R`/`R>` (14), `EXIT`
  (17) and `CALL` (21). So subroutines and multiple return points dominate
  code size, and code size is data-stack room.

**[PROPOSAL] (not adopted):** `mem/` is not yet ready for Glon/Core. Code
space or move speed must first be resolved: either a code/data-stack boundary
moved by an approved profile, or a validated masm move (≈400–450 more code
cells, estimated).

**[FINDING] masm code density and composition (2026-11-09, 64K
configuration, native and WASM, `experiments/glon-masm/`).**

[MEASURED] **Costs:**
- **Cell costs.** S1 has no call, return or jump. The frozen assembler derives
  `BRANCH` (5 cells), `>R`/`R>` (14), `EXIT` (17) and `CALL` (21) over the
  memory-mapped IP and RP. A masm word costs at least 19 cells (`ARITY n
  EXIT`).
- **Executed instructions:**

  | Operation | S1 instructions |
  |---|---|
  | a call/return pair | 24 |
  | a call of a masm word from Glon | ~227 |
  | a Glon function wrapper around it | ~1,400 more |
  | passing a get-word argument | ~408 |

- **Break-even.** A local subroutine saves space only when its body exceeds
  (21k + 17)/(k − 1) cells for k call sites: 34 cells at 4 sites, never under
  21.

[MEASURED] **Code space and stacks:**
- **The budget.** The runtime's code ends at 12,926, and every masm library
  shares the 3,458 cells up to the data-stack top (16,384). The assembler's
  cursor is a `static` of frozen `s1.c`, so code cannot be placed elsewhere.
- **Stack use.** The data stack never exceeded 87 cells in any workload
  measured. The return stack reached 7,783 of 8,192 cells at 80 nested calls.
- **Coexistence.** `mem/` v002 (1,577 cells) plus another ~1,200-cell masm
  library does not fit.

[FINDING] **Composition:**
- **What can be shared.** A masm word can call another, separately assembled
  word passed to it as a value: check the RAW record's arity, call its entry
  through a 3-cell trampoline, and drop the result count. It costs ~34
  instructions and 25 cells per site. That is safe only for callees that
  validate their own (tagged) arguments.
- **What cannot.** Internal raw-pointer helpers cannot be shared safely. masm
  cannot name a code address (`LIT label` is refused), and the loader links no
  names across blocks. These are assembler limits (`r0_s1_runtime.c`), as are
  the 510-token, 64-label and 128-reference block caps, not S1.

[FINDING] **Two safety gaps**, since repaired (see below):
1. The masm assembler emitted past `DS_INIT` without error: `mem/` + the
   variants reached 16,932, into the return stack.
2. The stack sentry checked SP against the runtime's own `code_end`, not the
   current end of library code, so a data stack growing over masm library
   code was not detected.

**[DECISION] Code/stack boundary repaired (2026-11-09, approved as a narrowly
scoped repair of these two defects).**
- **Assembler bound.**
  - **The limit.** Code-space limit `R0S1_CODE_LIMIT` = `R0S1_DS_INIT` −
    `R0S1_DS_RESERVE` (256) = 16,128 (`r0_s1.h`). The 256-cell reserve is
    about 3× the deepest data-stack use measured (87). No region moves.
  - **The check.** The parser computes a masm block's exact emitted size
    (`raw_size`, which mirrors the frozen expansions) and refuses, **before
    emitting anything**, a block that would end above the limit. The parse
    fails as `too_large`, with a message on stderr. No new error kind, so no
    ABI change.
  - **Defence.** After assembly, the emitted size must equal the computed
    size, or the runtime aborts as an internal error. The callable's
    loader-heap record is now allocated before assembly, so a full loader
    heap also fails without emitting code.
- **Sentry.** After each run, the final SP **and the deepest SP the run
  recorded** (`RV_SPMIN`, updated at every closure invocation) are compared
  with the current end of emitted code (`asm_here()`), not the runtime's
  `code_end`. There is no per-instruction cost.
- **Regression test:** `r0_s1_masm_bound_tests.c`, in the `s1` suite.
  - **Coverage:** exact fill to the limit, refusals, return stack and code
    untouched, cursor and bindings consistent, earlier words still running,
    detected data-stack growth into code, and the original `mem/` + variants
    sequence.
  - **Result:** 0 failures, against 14 on the runtime before the repair.
- **Native and WASM behave identically** (the same refusal: 126 cells needed,
  79 free).
- [MEASURED] **Cost:** no measurable change (library loading 2.6–2.7 ms per
  session before and after; short runs within build-layout noise).
- **Remaining limitations:**
  - Code emitted by *earlier* masm blocks of a source that is then refused
    stays in place, unreachable and without bindings, because the frozen
    assembler cursor cannot move back.
  - The sentry detects data-stack growth after the run, as before. Growth is
    sampled at closure invocations, so a masm word that pushes deep between
    invocations is seen only through the final SP.
  - The per-block label and reference arrays (64 and 128) are still not
    bounds-checked (out of scope).

**[FINDING] Machine-integrity audit (2026-11-09,
`MACHINE-INTEGRITY-AUDIT.md`; tests only, no runtime change).** New target
`make machine-integrity` (≈ 3 s), plus `make machine-integrity-wasm`. Known
defects are strict XFAILs: an XPASS fails the target until the check is
promoted.

The audit sharpens the limitations listed above:

- **Data-stack excursions also come from plain Glon, not only masm (K1, K2).**
  - **Mechanism.** Each pending native-operator argument holds one data-stack
    cell, and no closure is entered, so `RV_SPMIN` never sees the depth. One
    512-value block reaches 260 cells, which is already past the 256 reserve.
    Nesting through `either` blocks has no depth bound.
  - **Measured.** `+ 1` nested 3,600 deep (36 blocks × 100) overwrote the
    runtime's own code at 12,926 and was reported `OK`. With code full of
    masm libraries, 400 deep overwrote library code.
  - **Consequence.** The 256-cell reserve is a measured allowance for
    observed workloads, not an enforced bound.
- **The label/reference arrays overflow host memory (K5).** With 65 labels or
  129 references in one block, AddressSanitizer reports a
  `global-buffer-overflow` write into `raw_nlabels`/`raw_nrefs`. This is host
  C undefined behaviour reachable from any masm source; ordinary tests never
  execute it.
- **The refused-source code leak (K3) exhausts masm for the session.** Each
  refusal leaves 21 or more cells; 153 refusals exhaust a fresh code space.
  Bindings, the loader heap, the limit and plain Glon are unaffected.
- **masm in an unwrapped source fail-stops (K4).** The keyword is recognised
  only by `parse_block`, not by the unwrapped `parse_program` path that
  `./glon` uses. First recorded in `experiments/glon-latex`.
- [MEASURED] **Integrity that holds:**
  - **Determinism.** Identical sessions give identical 64K memory.
  - **Residue.** `r0_s1_init` is not a memory clear (dead cells keep residue),
    but sessions on poisoned or reused memory behave and end identically.
  - **Code.** Runtime code is never written by any failure kind.
  - **Recovery.** Nine failure kinds, each repeated 10 times, return code,
    globals and the live heap to the same state, and the loader heap grows by
    the same amount every round.
  - **GC.** Binary strings holding every byte value at every granule
    alignment survive 272 collections with interior stale integers, natively
    and in WASM, identically. The pre-repair collector fail-stops on the same
    test.
- **Repair priorities (not authorised):**
  1. K5 bounds;
  2. data-stack depth (K2/K1), by a parse-time nesting bound and/or a
     code-end canary;
  3. size all masm blocks before emitting any (K3);
  4. K4.

**[PROPOSAL]s (not authorised):**
- **(a)** a compact call through a per-block stub, about 14 cells instead of
  38 [E];
- **(b)** load-time cross-block linking of names;
- **(c)** the two bound checks above (since implemented, see the [DECISION]
  above);
- **(d)** larger or streamed assembler buffers.

All are assembler or runtime changes, not S1 changes. A larger memory profile
does not add code space unless it also moves `DS_INIT` (frozen).

## 8. Why more memory is a separate scaling experiment

**[DECISION]** A larger memory configuration is **a separate scaling
experiment, not a correction** to the 64K design.

- It changes the reference machine itself. `MEM_CELLS 65536` is defined in
  the **frozen** `s1.h`, with no override, so any other size is a deliberate
  departure from the frozen baseline, whatever else it preserves.
- The 64K configuration must remain buildable and tested as the
  constrained-computing reference. A larger profile is measured *against* it
  and does not replace it.
- More memory raises limits linearly but does not change the design's costs.
  At about 97 cells per call, depth costs memory: memory alone suits parsing
  depth, not large iteration counts (§9).
- This follows the existing rule that capacities are not raised casually (§4).

## 9. Proposals (not authorised, not implemented)

All of these are **[PROPOSAL]**s from investigations on 2026-11-06. None has
been approved or built, except the two GC worklist safeguards now recorded in
§6.

| Proposal | Summary | Key conditions and risks |
|---|---|---|
| **GC worklist sized by memory profile** | (c) a worklist sized by memory profile (for example 2,048 entries for a 65,536-cell heap), in a region of its own. Items (a), the build-time assertion, and (b), the bounded push, are **implemented** (§6) | must land with, or before, any heap enlargement; the build-time assertion enforces this |
| **256K memory profile** | (re-examined 2026-11-09 in `GLON-MEMORY-PROFILES.md`: still not configuration-only; it would hold a 32 KiB DEFLATE window, about 11K cells as a `mem/` region; it should come with an object-start bitmap, because the repaired return-stack scan walks O(objects) per candidate) a separate worktree or build. Only frozen change: `MEM_CELLS` → 262,144. Order-preserving map: main return stack ~65,536, managed heap ~65,536, loader ~49K, 8 tasks. Estimated ~675-deep recursion and ~15× usable heap | needs the worklist sized by profile first (the build-time assertion will refuse it otherwise); every fixed address in `r0_s1.h`/`m1_layout.h` and the seeded datatype addresses must move together; tests with literal addresses; slower GC sweeps; 4× WASM memory. The 64K reference stays the baseline |
| **Tail-call optimisation** | reuse the caller's frame for calls in tail position, including inside `either` and a final `do`; in an experimental build flag only | must not eliminate a frame an argument depends on (escape law (1), `return`, Guard of Binding, stack-local contexts); runaway loops would no longer halt, so it needs a fuel limit; diagnostics and benchmarks change. About 2× parser nesting; unbounded tail loops in general |
| **Segmented return stack** | switch to a new segment at closure invocation; frames already save absolute RP, so returns restore the old segment | pointers stay stable, but it gives no capacity gain without more total memory |
| **`mem/` as the Glon/Core memory facility** | adopt the `experiments/glon-mem/` vocabulary as the optional standard memory library, the lower-level alternative to a `binary!`-style `bin/` library. Before adoption: masm bulk string bridges, and a decision on an unchecked fast path for trusted libraries | code space (1,098 cells) is shared with every masm library; checked single-byte access is about 3× slower than `s/at` |
| **Glon/Binary (`bin/`)** | an optional binary-buffer library behind an implementation-neutral contract (`GLON-BINARY-FEASIBILITY.md`). Recommended first backing: heap-backed masm over M3 USER objects (packed bytes after `count`, untraced), which needs no ABI or frozen change | capacity bounded by the 7,232-cell heap: ≈ 12–16 KiB at 4 bytes per cell [E]. A 32 KiB DEFLATE window does not fit any in-heap representation on 64K. The routes that fit (the M1 arena region, the 256K profile, a synchronous host `HOST_CAP` op) are each a memory-map, frozen-S1 or [DECISION]-reversing change. Asynchronous host buffers via GLON_LIVE need no change but cannot offer synchronous reads |
| **Smaller activations** | size contexts to their actual bindings instead of 16 slots (64 cells) | changes when `CONTEXT_FULL` fires; touches the context, hash and promotion code |

A dynamically growing *contiguous* return stack was investigated and **not
recommended**. It would relocate frames and stack-local contexts that are
referenced by absolute address, and it would require changing the frozen `s1.c`.

### PARKED — M1 cooperative multitasking and GLON_LIVE integration

**Status:** deferred (recorded 2026-11-08). The working DOM demonstrations
(`experiments/glon-dom-host/`, `glon-links-native/`, `glon-links-multi/`) do
not need it.

**Established capabilities**
- **[FINDING]** M1 cooperative multitasking works: independent task stacks,
  lexical closures and shared garbage-collected memory. It needs no new S1
  primitive (`M1-MULTITASKING-RESULTS.md`).
- **[FINDING]** `GLON_LIVE` supports an asynchronous host request/reply. Glon
  ends a dispatch with a host-call request fragment; the host answers with a new
  dispatch (`glon_event_bytes`). See §7, host-backed DOM traversal.
- **[FINDING]** (2026-11-07, `experiments/glon-links-multi/`, uncommitted when
  this was written.) Two independent DOM link extractors process different
  documents through manually interleaved batches (A, B, A, B, ...).
  - They share vocabulary definitions but keep separate closure state.
  - All five tests give exact results in document order, including the same
    document as both instances, with collection during both walks.

**Current limitation** ([FINDING], from the code, 2026-11-07)
- **M1 has no `WAITING` task state.** A task is `TASK_RUNNABLE`,
  `TASK_FINISHED` or `TASK_EMPTY` (`m1_layout.h`).
- **A task awaiting a host reply can only keep yielding.** It stays runnable,
  and `run-tasks` returns only when no task is runnable. So the scheduler never
  returns control to the host, and the reply, which can arrive only as a new
  dispatch after the run ends, is never delivered.
- **`GLON_LIVE` supports one outstanding host request per run.** The request is
  the dispatch's single result fragment.

**Future investigation** ([PROPOSAL]: not authorised, not implemented)
- Add task suspension and selective resumption.
- Associate host replies with the correct waiting task.
- Allow the scheduler to return control to the host when no runnable tasks
  remain.
- Investigate supporting multiple outstanding host requests.
- Preserve frozen S1 if feasible. The M1 scheduler is assembled from the frozen
  primitives as masm library code, so a waiting state may not need an S1 change.
  This is unverified.

**Acceptance test:** two Glon tasks request different host resources, suspend
independently, and resume correctly when the replies arrive in reverse order.

**Priority:** parked until an application requires independently scheduled
asynchronous operations.

## 10. Before changing the runtime

1. Read this document and the authoritative sources it cites for the area you
   are changing.
2. Classify the change. Does it alter a **[DECISION]**? Then write an explicit
   proposal citing the entry, and wait for approval.
3. Frozen S1 files do not change; `./check-frozen-s1.sh` passes.
4. The binding and escape laws hold (`GLON-ALPHA-LAWS.md`); resource failures
   stay safe, structured fail-stops.
5. The 64K configuration keeps working; any other memory profile is separate.
6. Test natively, and in WASM built from the **current** sources.
7. Record new measurements (dated) and decisions here. Record milestones in
   `MEMORY.md`.

## Sources

**Normative and ground truth**
- `GLON-ALPHA-LAWS.md`
- `s1.c`, `s1.h` (frozen)
- `r0_s1.h`, `m1_layout.h`, `r0_s1_runtime.c`

**History and design**
- `MEMORY.md`
- `DESIGN.md`, `substrate-principle.md`
- `R0-ARCHITECTURE.md`, `architecture-s1.md`, `CLAIMS.md`, `ADVERSARIAL.md`
- `M1-MULTITASKING-RESULTS.md`, `M2-GC-DESIGN.md`, `M3-DATATYPE-DESIGN.md`,
  `M3D-MANAGED-BLOCK-DESIGN.md`
- `GLON-WASM-ABI.md`, `GLON-APP-MODEL.md`, `GLON-HOST-CAPABILITIES.md`

**Experiments**
- `experiments/glon-latex/GLON-LATEX-V001.md`, `GLON-LATEX-V002.md`,
  `GLON-LATEX-V003.md`, `GLON-LATEX-PAGE.md`
- `experiments/glon-pratt/GLON-PRATT-V004.md`

**Not in the repository**
- `GLON-HANDOFF.md`: a local, untracked checkpoint dated 2026-09-25. Its
  still-valid decisions are consolidated above, and it is not needed to read
  this document.
