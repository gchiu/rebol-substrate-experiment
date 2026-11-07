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
| emitted code / data-stack headroom | 12,454 cells / ~3,674 cells, after the GC worklist guard added 117 cells (from 12,337 / ~3,791) [2026-11-06] | each native costs far more than it looks (a CALL ≈ 21 cells, an EXIT ≈ 17 [handoff]) |
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
| **256K memory profile** | a separate worktree or build. Only frozen change: `MEM_CELLS` → 262,144. Order-preserving map: main return stack ~65,536, managed heap ~65,536, loader ~49K, 8 tasks. Estimated ~675-deep recursion and ~15× usable heap | needs the worklist sized by profile first (the build-time assertion will refuse it otherwise); every fixed address in `r0_s1.h`/`m1_layout.h` and the seeded datatype addresses must move together; tests with literal addresses; slower GC sweeps; 4× WASM memory. The 64K reference stays the baseline |
| **Tail-call optimisation** | reuse the caller's frame for calls in tail position, including inside `either` and a final `do`; in an experimental build flag only | must not eliminate a frame an argument depends on (escape law (1), `return`, Guard of Binding, stack-local contexts); runaway loops would no longer halt, so it needs a fuel limit; diagnostics and benchmarks change. About 2× parser nesting; unbounded tail loops in general |
| **Segmented return stack** | switch to a new segment at closure invocation; frames already save absolute RP, so returns restore the old segment | pointers stay stable, but it gives no capacity gain without more total memory |
| **Smaller activations** | size contexts to their actual bindings instead of 16 slots (64 cells) | changes when `CONTEXT_FULL` fires; touches the context, hash and promotion code |

A dynamically growing *contiguous* return stack was investigated and **not
recommended**. It would relocate frames and stack-local contexts that are
referenced by absolute address, and it would require changing the frozen `s1.c`.

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
