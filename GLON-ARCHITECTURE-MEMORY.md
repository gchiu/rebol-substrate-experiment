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

**[MEASURED] AvP G5y renderer-only room detail (2026-11-15, 64K configuration, WSLg D3D12).**
A visual-only change in `experiments/glon-doom` added a room-specific OpenGL
ceiling layer and cabinet hardware. It does not touch S1, parser semantics,
collision, navigation or the G5x2 door. The final executable is 6,146,536
bytes (+25,080 from G5x2); GL texture memory rises by 16,384 bytes to
9,894,784 for one 64x64 RGBA texture. Loader/heap are unchanged. Interleaved
old/new medians: standard 960x600 object benchmark 52.7 -> 54.2 FPS; room-detail
960x600 209.4 -> 218.5 FPS; room-detail 1920x1080 122.8 -> 120.4 FPS (-1.9%,
within the observed spread). Full report:
`experiments/glon-doom/G5Y-ENGINEERING-DETAIL-REPORT.md`.

**[MEASURED] AvP G5z renderer-only GLON industrial pass (2026-11-15, 64K
configuration, WSLg D3D12).**
A visual-only change in `experiments/glon-doom` rebuilt the fixed corridor
drums/crates and the north-wall engineering control bank. `objects.glon`,
`doom.glon`, collision extents, movement, AI, the G5x2 door and frozen S1 are
unchanged. All labels and instrument panels use one generated 512x256 RGBA
atlas, adding **524,288 bytes** of GL texture memory to **10,419,072 bytes**.
The final executable is **6,172,112 bytes** (+25,576 from clean G5y; +21,120
from the diagnostic baseline). Loader/heap are unchanged at 54,601/33,600 in
the matching corridor captures. Same-session five-run medians: standard
960x600 object benchmark 42.1 -> 45.8 FPS; control-bank pose 116.3 -> 115.8;
container-only pose 115.6 -> 102.2 at 960x600 and 84.5 -> 73.4 at 1920x1080.
The localized container-view cost is textured-surface fill; it stays above
65 FPS. Full report: `experiments/glon-doom/G5Z-GLON-CORPORATION-REPORT.md`;
matched evidence under `experiments/glon-doom/evidence/g5z/`.

**[MEASURED] AvP G6a continuous ambient audio (2026-11-15, 64K
configuration, WSLg D3D12 + PulseAudio).**
A host-only audio change in `experiments/glon-doom` adds two continuous ambient
layers from the supplied `g5y.zip` pack. No Glon, parser, collision, AI, visual
or frozen-S1 semantics changed. The 44.1 kHz mono s16le sources are converted
to S16, crossfaded at the loop boundary (1.0 s ship, 0.5 s ventilation) and
mixed on SDL's audio thread at 44.1 kHz stereo, 1024 frames. Default volume is
60%; `M` mutes and `[`/`]` adjust by 5. Decoded loop memory is 3,042,900 bytes;
the two source WAVs are 3,175,288 bytes on disk. The final executable is
6,181,624 bytes (+9,512). No new direct library dependency was added (SDL2 was
already linked; PulseAudio/ALSA remain transitive). The mixer costs about
0.003-0.005 ms per 23.22 ms block. Ten 360-frame interactive smoke medians were
82.1 FPS with audio and 86.1 FPS without (-4.6%, ranges overlap); the device
test opened PulseAudio and shut down cleanly, and an unavailable driver fell
back gracefully. Playback was not judged by listening. Full report:
`experiments/glon-doom/G6A-AMBIENT-AUDIO-REPORT.md`; evidence under
`experiments/glon-doom/evidence/g6a/`.

**[MEASURED] AvP G6a Freesound 873687 ambience replacement (2026-11-15, 64K
configuration, WSLg D3D12 + PulseAudio).**
The two synthetic G5y ambience loops were removed and replaced by one converted
Freesound recording: Tommaso Motteran, sound 873687, CC BY 4.0. The source is
24-bit stereo 44.1 kHz, 34.000068 s; the runtime asset is 16-bit mono 44.1 kHz,
1,499,403 frames / 2,998,920 bytes. The existing 1.0 s crossfade loop engine
plays 1,455,303 frames (33.000068 s), consuming 2,910,606 bytes. The mixer,
volume/mute, device fallback, Glon semantics and frozen S1 are unchanged. The
final executable is 6,181,632 bytes. Six 360-frame interactive smoke medians
were 110.8 FPS with audio and 124.0 FPS without (-10.6%, ranges overlap); mixer
cost is about 0.004 ms per 23.22 ms block. Real PulseAudio, dummy and unavailable
drivers all opened/continued and shut down as designed. Playback was not judged
by listening. Full report: `experiments/glon-doom/G6A-AMBIENT-AUDIO-REPORT.md`;
evidence under `experiments/glon-doom/evidence/g6a/`.

**[MEASURED] AvP G6b event-driven sound effects (2026-11-15, 64K configuration,
WSLg D3D12 + PulseAudio).**
A host-only audio change in `experiments/glon-doom` retains the G6a 873687
ambient loop and adds six converted event clips to one SDL2 mixer: door
open/close, Alien shriek, rifle shot, wall thud and cargo impact. The mixer has
8 voices; door/Alien voices are exclusive with a 125 ms replacement fade;
door/Alien use distance attenuation; wall/cargo use blocked-position resolution
plus existing object extents, a 350 ms cooldown and no gameplay physics. The
G5r weapon is static, so the rifle effect has an explicitly presentation-only
F/LMB preview and does not change ammo, damage, AI, collision or Glon
semantics.

Assets are 3,821,200 bytes on disk; decoded runtime audio is 3,732,340 bytes
(ambient loop 2,910,606 + SFX 821,734). The executable is 6,190,464 bytes
(+8,832 from G6a). Six 360-frame graphical smoke medians were 172.1 FPS with
audio and 180.9 without (-4.9%, ranges overlap); the ambient-plus-8-voice mixer
cost is about 0.007-0.009 ms per 23.22 ms block. Real PulseAudio, dummy and
unavailable driver paths all behaved; playback was not judged by listening.
Full report: `experiments/glon-doom/G6B-SOUND-EFFECTS-REPORT.md`; evidence under
`experiments/glon-doom/evidence/g6b/`.

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

### G5g AvP execution-cost investigation [FINDING]

[MEASURED 2026-11-14, `experiments/glon-avp-g5g`, native 64K, GCC 11.4.0
`-O2`, WSL2 AMD Ryzen 5 7535U]

- **G5f wall regression is a host-build effect, not a wall/row algorithm
  effect.** The G5f game under a host without the G5f projection diagnostic code
  measured 103.65 ms wall; the G5f host measured 136.62 ms. Wall-stage S1
  operation count was identical: 33,377,988 per textured frame (160 rays,
  1,600 candidates; 208,612 ops/ray, 20,861/candidate). The exact
  microarchitectural cause is undetermined; function/loop alignment flags did
  not help. Host diagnostics must be isolated from production measurements.
- **G5f rows work.** Row-stage operations fell from 16,622,624 (G5e) to
  5,188,130 (G5f), 68.8%; row time fell from ~51.8 ms to ~18.5 ms under the
  same host. Wall is ~84% of the remaining textured render instruction count.
- **Primitive cost is small; macro cost is large.** Assembled S1 loops cost
  roughly 1.6–3.1 ns per additional primitive; normal Glon recursion measured
  22–33 µs per 60-level workload (add 22.6, divide 26.9, branch 30.2, memory
  33.5 µs/level). An isolated 160-ray/10-wall Glon workload measured a median
  112.4 ms versus 0.00126 ms native C (identical checksum, ~89,000×).
- **Loader frontier.** `GC_LOADER_HP` is a permanent bump pointer into
  `[40000,54800)` (14,800 cells), not managed heap or code. After prelude + game
  + host blocks it used 14,162 cells (638 free); the game contains no masm and
  adds 0 code cells (runtime code 12,454). A 128-integer table cost 144 loader
  cells (~1.125/int); 320 integers were refused with 222 cells free. A compact
  per-ray wall table therefore does not fit as ordinary loader data.
- **Stationary view cache.** A reversible last-scene cache (camera + player +
  door + Alien/tracker + render mode signature) had 99.4% hits in a static
  corridor state. Frame production fell from ~168 ms to ~6.7–7.5 ms, but
  input-to-visible-update remained ~168–179 ms because a turn invalidated the
  cache. At 60 Hz CPU use was ~41% (busy loop ~84%). This is an idle
  optimisation, not a responsiveness improvement.
- **Diagnostic safety.** Opcode/HOST/branch counts came from the separate
  `s1_prof.c` build; production still links frozen `s1.c`. Instrumented and
  ordinary fixed-frame hashes matched, and all existing tests and the frozen-S1
  check passed. No primitive, memory-map, resolution, artwork or gameplay
  change was made.
- **G5h direction.** Isolate production and diagnostic host code, keep the
  64K map, and measure a compact prepared-wall representation against the
  638-cell loader margin before attempting another renderer rewrite. The
  current macro cost is evaluator/activation dominated, not primitive
  arithmetic dominated.

### G5h AvP prepared wall execution [MEASURED]

[MEASURED 2026-11-14, `experiments/glon-avp-g5h`, native 64K, GCC 11.4.0
`-O2`, WSL2 AMD Ryzen 5 7535U]

- **Prepared execution works within the frozen substrate.** The existing
  `masm`/RAW lowerer compiles the 10-wall calculation once into S1; no primitive
  or memory-map change. Integrated under `--raw-wall`, the wall stage falls from
  33,377,988 to 4,555,404 S1 operations/frame (86.4%) and from 6,014,444 to
  846,833 HOST calls/frame (85.9%). Per candidate: 20,861 → 2,847 operations;
  3,759 → 529 HOST calls.
- **Measured game effect, same host binary:** textured 6.2 → 19.5–20.3 FPS
  (wall 129.34 → 18.53 ms; total 160.53 → 51.23 ms); simple 6.7 → ~30.4 FPS.
  The raw path passes the complete default self-test, including deterministic
  framebuffer, floor/wall/ceiling alignment, materials, door, Alien, occlusion
  and repeated-session loader stability.
- **Memory:** raw mode adds ~170 loader cells (14,162 → 14,322 used; 478 free)
  and ~1,702 code cells (`asm_here` 13,126 → 14,828; limit 16,128). There is no
  data table or managed allocation in the raw workers; managed frontier is
  33,472 and GC is 0. The raw file is loaded only in the experimental mode.
- **Preparation:** isolated load 0.39 ms; 10/10 edge/camera cases and full
  160-ray checksum 112,340 match native C. The main remaining costs are the
  Glon wrapper/output and texture rows, not wall arithmetic.
- **Reserve primitives:** not justified. The 86% reduction came from removing
  repeated evaluator lookup/traversal, not from a missing instruction.

### G5i AvP Alien sprites and visual diagnosis [MEASURED]

[MEASURED 2026-11-14, `experiments/glon-avp-g5i`, native 64K, GCC 11.4.0
`-O2`, WSL2 AMD Ryzen 5 7535U]

- **Alien assets:** the requested `sprites/sprites.png` is absent; the only
  supplied sheet is the G5d environment contact sheet, which has no Alien art.
  G5i therefore uses five original 64×128 transparent pixel-art frames
  (`idle`, `walk1`, `walk2`, `attack`, `side`) generated by
  `tools/gen_alien_sprites.py`, with no contaminated crops. PNG total 49,111
  bytes; compiled arrays 163,840 bytes, outside Glon memory.
- **Rendering:** native alpha sampling/blending, nearest filtering, distance
  shading and simulation-tick animation. Glon still owns position, state,
  door/LOS occlusion and the projected rectangle. Sprite cost is 0.10–0.11 ms
  when visible and close; no green placeholder/rectangle remains.
- **Performance:** raw-wall textured median 21.8 FPS, wall 15.54 ms, texrow
  20.24 ms, total 45.78 ms; visible close Alien 21.1 FPS, sprite 0.10 ms,
  total 47.30 ms. Wall S1/HOST counts are unchanged (4,555,404/846,833);
  managed frontier 33,472, GC 0; loader 14,322 used / 478 free; code 14,828 of
  16,128. Absolute wall time varies with documented host layout.
- **Wall diagnosis:** a checker texture on the existing geometry/materials
  showed no hidden strip gaps; blockiness is dominated by 320×200 internal
  resolution, 3× nearest scaling, 2-pixel projection columns, repeated 64×64
  tiles and four shading levels. Bilinear final-framebuffer scaling softens
  edges but blurs texels; it is optional (`--filter linear`), not default.
  Environmental resolution/tiling is deferred to G5j; G5d artwork unchanged.

### G5j AvP wall tiling diagnosis [MEASURED]

[MEASURED 2026-11-14, `experiments/glon-avp-g5j`, native 64K, GCC 11.4.0
`-O2`, WSL2 AMD Ryzen 5 7535U]

- **Root cause of the Tetris walls:** the G5d wall textures are non-tileable
  contact-sheet crops. Opposite-edge RGB differences are 160–414 for old wall
  assets versus 0.4–7.7 for two new original seamless 64×64 textures. The UV
  checker found no hidden wall-strip gaps; repeated 64×64 tiling creates hard
  seams. Low 320×200 resolution, 2-pixel rays and four shade levels amplify the
  pattern but are secondary.
- **Configuration A** (320×200, 160 rays) remains the supported practical
  configuration. 320 rays are blocked by the loader cost of four hosted
  ray-block sources (478 cells free); 640×400 requires parallel Glon
  projection/texrow work because the wall U formula, horizon and 200-row
  texrow data are hardcoded. No ray/block duplication was used.
- **New textures:** `panel_v2` and `service_v2`, original seamless assets;
  32,768 compiled bytes, no Glon loader/managed allocation. New textures
  visually restore continuous panels. Median performance: new textures 19.6
  FPS (wall 17.73 ms), smooth shading 21.4 FPS, new+smooth 21.6 FPS vs 21.7
  baseline; all above 15 FPS. Smooth shading is host-only and leaves Glon
  S1/HOST counts unchanged.
- **Preserved:** G5h raw wall (4,555,404 S1 ops, 846,833 HOST/frame), G5i Alien
  sprites, 64K map, loader 14,322/478 free, code 14,828/16,128, managed 33,472,
  GC 0.

### G5k AvP flat surfaces, clutter and cover [MEASURED]

[MEASURED 2026-11-14, `experiments/glon-avp-g5k`, native 64K, GCC 11.4.0
`-O2`, WSL2 AMD Ryzen 5 7535U]

- **Flat surfaces** remove the Glon texrow pass (`0 S1 ops/0 HOST` in the
  opcode profile) and replace it with a native gradient. Raw-wall medians:
  A textured 20.6 FPS / floor 21.4 ms / total 48.6 ms; B flat 33.2 FPS /
  floor 2.35 ms / total 30.1 ms. About 19 ms is removed.
- **Six compact objects** (`objects.glon`, loaded only with `--objects`) add
  shared Glon projection/collision and four original 64×96 prop sprites.
  C flat+objects: 27.5 FPS / floor 5.3 ms / total 36.4 ms. D cover:
  26.7 FPS. Object cost is ~6.3 ms, leaving a net floor saving.
- **Occlusion:** objects and Alien are depth-sorted with alpha compositing;
  object-vs-wall is screen/door-state based, not per-pixel. This limitation is
  explicit. Object collision is shared by Marine and Alien; no pathfinding.
- **Memory:** object sprites 98,304 compiled bytes; object mode loader
  14,210 used / 590 free (raw), managed 33,632, GC 0, code 14,828/16,128.
  Default mode remains compact and does not load `objects.glon`. No frozen
  capacity changed.

### G5l AvP OpenGL renderer [MEASURED]

[MEASURED 2026-11-14, `experiments/glon-avp-g5l`, WSLg, AMD Ryzen 5 7535U,
AMD Radeon integrated, Mesa OpenGL 4.2 compatibility, GCC 11.4.0 `-O2`]

- **Hardware verified:** `D3D12 (AMD Radeon(TM) Graphics)`, vendor Microsoft,
  Mesa 23.2.1-1ubuntu3.1~22.04.4. OpenGL 2.1 compatibility fixed-function
  backend in the SDL host.
- **Benchmarks (120 frames, objects/Alien):** CPU G5k 320×200 27.5 FPS;
  GL 320×200 60.0 FPS; GL 640×400 62.4 FPS; GL 960×600 57.7 FPS. GL sim
  11.9–12.6 ms, submit+glFinish 2.05–2.34 ms. GPU is not resolution-bound at
  these sizes; object/status/Glon work dominates.
- **ABI:** static walls/door/floor/ceiling are a one-time renderer ABI copy;
  dynamic camera, door, object records, Alien status and Glon `light-phase`
  are supplied per frame. No wall/texrow S1 work in GL mode. CPU renderer
  remains the default/fallback.
- **Visual:** illuminated ceiling panels, subtle localized pulse from Glon
  light phase, depth-tested objects/Alien, door occlusion, transparent sprite
  edges. G5h raw wall and G5i Alien remain intact; no primitive/memory-map
  change.
- **Memory:** loader 14,082 used / 718 free; heap 33,600; GC 0; executable
  596,696 bytes. Static geometry should move into a compact Glon scene block
  in G5m; sound remains a proposed event ABI.

### G5m Playable OpenGL AvP and creature replacement [MEASURED]

[MEASURED 2026-11-14, `experiments/glon-avp-g5m`, WSLg, AMD Ryzen 5 7535U,
AMD Radeon, Mesa OpenGL 4.2 compatibility, GCC 11.4.0 `-O2`]

- **Playable GL loop:** normal `./build/doom` now runs the same fixed-timestep
  Glon input/simulation loop through `interactive_gl`, with W/S/A/D, arrows, E
  and Esc. CPU remains `--renderer cpu`; GL failures fall back with a diagnostic.
  Real WSLg key events verified W movement, one-shot E door opening, Right turn
  and D strafe on both GL and CPU. E was double-triggered and GL masked off turn
  bits 16/32; both are fixed and regression-tested via `--input-test`.
- **GL performance** with objects/128x192 Alien: 320x200 60.0 FPS, 640x400
  66.0–67.4 FPS (sim 9.9–10.2 ms, submit+finish 2.4–2.7 ms), 960x600 57.7 FPS;
  CPU reference 27.5 FPS. Larger creature assets and host changes correlate with
  a lower range than G5l's ~60 FPS; still responsive and faster than CPU.
- **Resize fix:** GL windows are resizable; viewport/projection/HUD use the
  actual drawable size every frame, with vertical FOV preserved, full-window
  aspect-fill, HUD circularity and a scale cap. Maximized and non-16:10 windows
  were verified; `--resize-test` covers 640x400–1600x900.
- **HUD fix:** GL initially omitted the CPU motion tracker. The shared
  `tracker_blip()` mapping now drives both CPU and GL from the same Glon
  `detected/tside/tdepth/state/door_blocked` state; a depth-disabled
  screen-space GL overlay restores the ring, bar and blip. `--tracker-test`
  covers ahead/right/left/behind/near/out-of-range.
- **Orientation fix:** GL texture upload now reverses source rows; the previous
  upside-down Alien was the GL texture-origin mismatch. New 128x192 creature
  frames are upright, transparent and depth-tested.
- **Artwork:** original procedural biomechanical creature, 6 states (idle,
  walk1/2, attack, side, death), near-black/eyeless/ribbed/clawed. Death asset
  not wired (no death state). No copyrighted art.
- **Limits:** static wall geometry remains a renderer-side ABI copy. Manual
  physical-keyboard verification remains recommended. Loader 14,185 used/615 free; heap 33,600; code 13,126/16,128;
  executable 924,496 bytes; no frozen-memory or primitive change.

### G5n AvP industrial corridor greybox [MEASURED]

[MEASURED 2026-11-14, `experiments/glon-avp-g5n`, WSLg D3D12 AMD Radeon,
Mesa OpenGL 4.2 compatibility, GCC 11.4.0 `-O2`]

- **Implemented:** real 3D corridor architecture in the existing GL backend:
  bulkhead walls, recessed wall panels/trim, projecting wall/ceiling/floor ribs,
  lower service housings, pipes/conduits, recessed ceiling fixtures, floor
  plates/grates/drains, framed near/far bulkheads and 3D object masses.
- **Artwork gate open:** all materials are temporary flat solids. The approved
  reference at `/mnt/c/users/anon_/downloads/corridor.png` was used only as art
  direction; no crops or new low-quality procedural textures are claimed as
  final. `G5N-ASSETS.md` specifies required final assets. Alien remains pending
  separate artwork.
- **Performance:** 640x400 measured 31–46 FPS (simulation 19–20 ms); 960x600
  36.2 FPS. Immediate-mode geometry adds draw cost; still interactive but below
  G5m. Loader 14,082 used/718 free; heap 33,600; GC 0; executable 1,035,848.
- **Preserved:** GL default and CPU fallback, resize drawable viewport, controls,
  doors/collision/Alien state, tracker/HUD, ceiling pulse, frozen S1/64K.

### G5o AvP corridor artwork pipeline [FINDING]

[FINDING 2026-11-14, `experiments/glon-avp-g5n`, WSLg D3D12 AMD Radeon]

- Supplied prototype art pack installed under `assets/corridor/`; replacement
  manifest installed, original G5n manifest preserved in Git history.
  `tools/gen_corridor_textures.py` generates 512² runtime arrays from the
  1024/512 source PNGs, bounding executable growth.
- Only the first three materials were integrated: recess panel (per panel),
  bulkhead steel (far door, U corrected so A-3 is upright), floor plate (per
  deck section). The other eleven materials remain pending.
- Recess panels and bulkhead materially improve the corridor. Floor material is
  low-contrast, stretched over 36×100 plates and repeats; source edge metrics
  (floor 44.9/104.8, panel 67.6/142.6, bulkhead 61.4/471.0 RGB units) confirm
  the supplied images are not seamless. The visual-quality gate remains open.
- Performance with art: 640×400 37.6–56.4 FPS; 960×600 51.1 FPS. Loader/heap
  unchanged (14,185/615, 33,600); executable 4,181,800 bytes. Frozen S1 and
  all controls/gameplay unchanged.

### G5p AvP corridor composition [FINDING]

[FINDING 2026-11-14, `experiments/glon-avp-g5p`, WSLg D3D12 AMD Radeon]

- Replaced flat ceiling panels with framed recessed fixtures, warm lenses and
  grille geometry; retained the Glon light pulse.
- Replaced the art-mode black floor grid with a world-scaled deck-texture surface
  (128-unit plate UVs), plate seams, central drain and narrow edge grilles.
- Reduced prop records and kept them wall-mounted. Records remain the sole
  visual/collision source; no separate object state.
- Measured 960×600 32.2 FPS / 31.01 ms and 640×400 32.8 FPS. This is below the
  G5o ~51 FPS baseline; immediate-mode geometry submission is the main cost.
- Loader 14,082 used / 718 free; heap 33,600; executable 4,185,600 bytes.
- Floor/ceiling/prop artwork remains prototype/placeholder; final seamless deck,
  light insert and prop materials are explicit dependencies.

### G5p corrective: material coverage and performance [FINDING]

[FINDING 2026-11-14, `experiments/glon-avp-g5p`, WSLg D3D12 AMD Radeon]

- Blank spawn/door surfaces were missing material assignments: base floor,
  ceiling and all `gl_walls` used flat-colour helpers, and the near door had no
  texture. Existing `gl_wall`/`gl_plane` helpers now cover all room/corridor
  walls and floor; both door faces use `GT_DOOR`; lower housings use tinted
  service material. Deliberate dark recesses/ribs/fixtures stay dark.
- Corridor CPU submission is only 0.5–0.9 ms; static geometry rebuild is not the
  bottleneck. A display-list cache was rejected because WSLg D3D12 fell below
  1 FPS. Forced `glFinish` cost 2.3–6 ms and is now opt-in (`GLON_FINISH=1`).
- Matched clean runs: G5o 53.7 FPS at 960×600 and 34.0 FPS at 2080×1300; G5p
  corrective 51–62 FPS and 40.7–44.5 FPS respectively. Remaining frame time is
  Glon tick (~6–9 ms) plus state extraction (~5–6 ms).

### G5q Alien reference conversion [FINDING]

[FINDING 2026-11-14, `experiments/glon-avp-g5p`, WSLg D3D12 AMD Radeon]

- The supplied 1536×1024 concept sheet has a real alpha channel but is not a
  validated fixed-grid atlas: labels, an info box, variable spacing and merged
  silhouettes are present. Five isolated poses were cropped, excluding labels,
  and converted to 192×144 RGBA frames with one common scale, foot baseline and
  foot pivot. No procedural creature was generated.
- The five replacement textures preserve the existing idle/walk1/walk2/attack/
  side frame selection and timing. Billboard width changed 1.0→1.8 to preserve
  the new 4:3 silhouette; AI, collision and detection are unchanged.
- Full idle/walk/turn/hit/death sequences and per-frame pivot metadata remain
  missing artwork. The sheet must not be described as a validated sprite atlas.
- 960×600 measured 51.6 FPS (sim 13.33 ms, submit 0.76 ms); executable
  4,251,192 bytes; loader/heap unchanged. Frozen S1 and all regression tests pass.

### G5r pulse-rifle reference integration [FINDING]

[FINDING 2026-11-14, `experiments/glon-avp-g5r`, WSLg D3D12 AMD Radeon]

- The four supplied weapon references are 1672×941 opaque corridor illustrations,
  not transparent sprites. The pulse rifle/hand was hand-masked to a 768×406 RGBA
  asset; corridor background and the baked display digits were removed. The
  carry-handle opening required a separate silhouette hole.
- Glon now owns `doom/ammo` (initial 24). OpenGL draws the live value with
  seven-segment quads in the blanked display; no firing/reload/consumption
  mechanics were invented.
- The overlay is static, bottom-right, 60% of drawable width, and independently
  resizes. Software fallback has no first-person weapon.
- 960×600 measured 66.0 FPS (sim 7.15 ms, submit 0.77 ms); `make test` and frozen
  S1 pass. Corridor, floor, Alien AI, controls and tracker are unchanged.

### G5s Directional Alien sprites [FINDING]

[FINDING 2026-11-14, `experiments/glon-avp-g5s`, WSLg D3D12 AMD Radeon]

- G5s combines the G5q Alien baseline (`a131440`) with the G5r pulse-rifle work
  (`7183ae6`) and the G5p orientation report (cherry-picked, not merged). The G5r
  rifle and live 24-round display remain visible.
- Seven isolated poses were extracted at native source resolution from two
  1536×1024 reference sheets: frontal idle, two frontal approach frames,
  three-quarter, side, rear and frontal attack. Poses are 188–268 source pixels
  tall; no upscaling or generative detail was used. Component masks prevent
  adjacent tails from entering a crop.
- Selection uses the actual Alien movement for approach/retreat plus the
  camera-relative screen angle for front/three-quarter/side. Frontal approach now
  alternates the two genuine frontal poses. Attack is always frontal. The side
  pose is horizontally mirrored only for the opposite crossing direction.
- Directional acceptance passes in-game: the Alien approaches head-on with a
  frontal silhouette. Close-up resolution fails: a 188–268 px source is displayed
  at roughly 600–740 px near attack range. The supplied art is not sufficient for
  a sharp close-up.
- 960×600 measured 95.3 FPS (sim 4.56 ms, submit 0.63 ms) without object mode and
  54.9 FPS (sim 11.61 ms, submit 0.83 ms) with objects; the same G5r baseline
  measured 93.7/48.5 FPS. Loader 53,337 (54,201 with objects); heap 33,440
  (33,600 with objects); executable 6,074,896 bytes. `make test` and frozen S1
  pass.



### G5t engineering room geometry [FINDING]

[FINDING 2026-11-14, `experiments/glon-avp-g5t`, WSLg D3D12 AMD Radeon]

- The spawn room is converted into one distinct engineering compartment from
  the two supplied concept references. The corridor, deck material, Alien AI,
  rifle/ammo, tracker, doors and controls are unchanged. No map, collision,
  frozen S1, memory-layout or language-semantics change was made.
- **[DECISION]** The room upgrade is renderer-side geometry placed around the
  perimeter; Glon collision remains unchanged. A central door-to-spawn lane
  (roughly `y=256..384`) is kept clear. This preserves the current world and
  mechanics for this pass; adding machinery collision would be a separate
  gameplay/navmesh change.
- Reusable components: `eng_rail_x/y`, `eng_hazard_x`, `eng_pipe_x/z`,
  `eng_tray_x`, `eng_fixture`, `eng_wall_bay_x/y`, `eng_grille_y`,
  `eng_cabinet_x/y`, plus `eng_generator`, `eng_coolant`, `eng_walls` and
  `eng_overhead`. The turbine adds `gl_cyl_y`, `gl_disc_x/y`,
  `gl_cyl_z_lit/y_lit` and `gl_turbine_face`.
- Geometry: front-facing turbine drum/rotor, two coolant vessels with valve
  wheels and manifold, service cabinets, control bank, recessed wall bays,
  ventilation grilles, structural posts/ceiling beams, four overhead pipe runs,
  four cable trays, five recessed ceiling fixtures and restrained indicator
  lights. Existing `GT_SERVICE` panels are tinted for room use; no new bitmap
  material was added. The art floor remains the existing deck material.
- Navigation: the player starts at `(320,320)`; the door lane and spawn are
  clear, and the trace reaches the corridor normally. Large equipment is outside
  the central lane. Equipment has no new collision, so the player can walk
  through it if deliberately walked into; this is recorded as a limitation, not
  a hidden gameplay change.
- 960×600, 180-frame matched medians: no objects 97.1 FPS (sim 4.56 ms, submit
  1.06 ms, total 10.30 ms) versus G5s 117.5 FPS (8.51 ms). With objects: 63.1
  FPS (sim 10.68 ms, submit 1.11 ms, total 15.85 ms) versus G5s 65.7 FPS
  (15.23 ms). That is about 17% lower without objects and 4% lower with objects;
  the room remains above 60 FPS at 960×600. Loader 53,337/54,201; heap
  33,440/33,600; executable 6,092,080 bytes.
- Directional Alien captures and room captures show depth-tested occlusion
  around the new geometry. `make test` and frozen S1 pass.



### G5u Alien obstacle navigation and room accessibility [FINDING]

[FINDING 2026-11-14, `experiments/glon-avp-g5u`, WSLg D3D12 AMD Radeon]

- The G5t engineering room is the actual spawn room (`x=128..768, y=128..640`),
  not a second room. The normal player state is `(320,320)`, Alien
  `(1120,320)`, door closed. The normal GL launch now applies a one-time
  `doom/angle: 240` orientation so the initial view includes both the door and
  engineering machinery; `doom/start-angle` remains `0`, preserving tests,
  trace and scripted-capture expectations.
- Confirmed failure: with the player at `(760,350)`, Alien at `(970,368)` and
  the `(940,370)` barrel present, the Alien attempts x but is rejected by the
  barrel; y reaches the target alignment and stops. It remains at `(968,352)`
  for at least 240 ticks. This is a real AI limitation, not a rendering freeze
  or a player misunderstanding.
- Fix: `doom/update-alien` performs the original direct x/y pursuit first. If
  no position change occurs and the target is beyond attack range, it sets a
  signed 14-tick sidestep on the perpendicular axis. The side is chosen from
  the corridor side (`y>320` or `x>768`), and every step still calls
  `doom/check-all` (walls, doors and objects). The primary axis is retried each
  avoid tick; once it moves, direct pursuit resumes. No teleport, pathfinding,
  new collision primitive or frozen-S1 change was made.
- `doom-host.c` also has `--g5u-nav DIR W H [legacy]` for deterministic
  before/after movement traces and GL screenshots. `legacy` suppresses the
  sidestep. The ordinary interactive executable was launched at 640x400 and
  logged the new spawn orientation and unchanged Alien/rifle state.
- Performance at 960×600, 180-frame matched medians: no-object 86.5 FPS
  (sim 5.51 ms, submit 1.10 ms, total 11.56 ms) versus G5t 104.3 FPS (9.59 ms);
  with objects 51.4 FPS (sim 13.24 ms, submit 1.20 ms, total 19.46 ms) versus
  G5t 60.2 FPS (16.62 ms). The added Glon avoidance checks cost about 17% in
  no-object mode and 15% with objects; the game remains above 50 FPS with
  objects. Loader after host blocks: 174 cells free; table probe largest 128,
  so the compact fix is near the 64K loader limit. Executable 6,100,344 bytes.
- `make test`, G5s directional capture and frozen S1 pass. Room/machinery
  collision remains visual-only from G5t; the player can walk through equipment,
  and the local sidestep is a bounded corridor/obstacle heuristic, not a general
  navmesh.



### G5v engineering-room materials [FINDING]

[FINDING 2026-11-14, `experiments/glon-avp-g5v`, WSLg D3D12 AMD Radeon]

- The corridor's richer look comes from three existing 512×512 art textures
  (`GT_ART_WALL` bulkhead/A-3, `GT_ART_PANEL` recessed vent panel,
  `GT_ART_FLOOR` plate), uploaded with linear filtering and repeat, plus
  per-face tinted immediate-mode shading. There are no GL lights or dynamic
  shadows; brightness is baked into face colours and additive `gl_light_panel`
  quads.
- Room walls/cabinets previously passed material **enums** to
  `gl_tex_quad_tint`, which takes a GL texture name. The resulting numeric
  texture names happened to sample other textures, contributing to the flat
  room palette. G5v uses `gl_tex[...]` consistently and applies the existing
  panel texture to room bays/cabinets.
- Room panel faces use fixed-function `GL_ADD` texture-environment mode to
  re-expose their dark albedo while retaining texture detail. No second texture
  pipeline, new texture asset or per-frame upload was added. Large casing
  surfaces reuse a crop of the bulkhead steel above the A-3 plate; flat-lit
  cylinders (drum/tanks/pipes) avoid the braided look produced by wrapping the
  plate texture around small radii.
- Lighting is baked/emissive: cool overhead rails/lenses and panel washes, warm
  turbine core/floor glow, darker pipe/casing undersides, soot/oil decals and
  restrained indicator lights. The A-3 corridor is pixel-identical before/after.
- Matched 960×600, 180-frame medians: no objects 91.2 FPS (10.97 ms total)
  versus G5u 84.5 FPS (11.83 ms); with objects 60.1 FPS (16.63 ms) versus G5u
  54.9 FPS (18.22 ms). Executable 6,113,024 bytes (G5u 6,100,344); loader
  high-water unchanged at 53,705/54,569 cells; additional texture memory 0.
- `make test`, frozen S1, normal interactive launch, G5u navigation, directional
  Alien captures and rifle/ammo checks pass. Room machinery still has no
  collision (existing G5t limitation), the room remains darker than the
  corridor, and the improvement uses opaque albedo/additive shading rather than
  true local illumination.



### G5w machinery texture mapping [FINDING]

[FINDING 2026-11-14, `experiments/glon-avp-g5w`, WSLg D3D12 AMD Radeon]

- Root cause of the flat machinery: the `gl_cyl_x/y/z`, `gl_disc_x/y`,
  `gl_turbine_face`, `eng_pipe_x` and `eng_pipe_z` helpers all disabled
  `GL_TEXTURE_2D`, called `gl_rgb` and emitted no `glTexCoord`. G5v's textured
  cabinet/panel boxes did not touch those paths. G5v's material-enum vs
  `gl_tex[...]` bug was already fixed for panels; G5w makes the same binding
  rule explicit for machinery.
- G5w adds smooth cylindrical helpers (`gl_cyl_x_tex_smooth`,
  `gl_cyl_y_tex_smooth`, `gl_cyl_z_tex_rot`), annular turbine mapping
  (`gl_ring_y_tex`), planar cap/valve mapping (`gl_disc_*_tex`), and static UVs
  generated per vertex. Each helper binds one existing texture, emits UVs and
  disables texturing afterwards. Shading is smooth per-vertex `cos(angle)`,
  avoiding the faceted/dark-band look.
- Safe crops from the existing 512×512 corridor art avoid the A-3 sign:
  bulkhead top (`v 0.02–0.38`) and bottom (`v 0.64–0.98`) strips, and the
  clean upper floor-plate field (`u 0.18–0.82`, `v 0.05–0.42`). No texture
  uploads, new assets or per-frame image work were added.
- Surfaces now genuinely textured: turbine drum and front ring, both coolant
  vessel bodies and caps, both red valve wheels, overhead and wall pipe bodies,
  plus the G5v cabinet/casing boxes. Small pipe flanges/couplings retain flat
  steel: texturing 16 tiny flange surfaces added roughly 20% to the matched
  frame cost, so that cosmetic gain was rejected.
- Matched 960×600, 180-frame medians: G5v 88.7/59.6 FPS (recorded report
  91.2/60.1) versus G5w 91.1/60.4 FPS; no measurable regression. Executable
  6,117,184 bytes (G5v 6,113,024; +4,160). Loader high-water unchanged at
  53,705/54,569 cells. Texture memory 0 bytes.
- The existing plate/panel art is not a seamless cylinder material. Reusing it
  makes pipes read as fine ribbing/knurling at close range rather than a smooth
  brushed tube; the A-3 wall and floor textures also carry rivet/seam structure
  that had to be cropped around. A dedicated seamless weathered-steel/pipe
  texture would improve close-ups, but no new asset/pipeline was introduced.

### G5x high-resolution sliding blast door [MEASURED]

[MEASURED 2026-11-14, `experiments/glon-avp-g5x`, WSLg D3D12 AMD Radeon]

- The supplied G5x concept sheet was cropped to two native-resolution leaves:
  450×785 (left) and 410×785 (right), about 7 texels per world unit. The old
  OpenGL path used one embedded 64×64 `tex_door` across the whole 128-unit
  face. No upscaling or procedural replacement is used.
- The OpenGL door is now two 64×112 leaf slabs, 8 units thick, inside a
  24-unit-deep casing with side receiving pockets. Its leaf travel is computed
  directly from Glon's existing `door_open` (0..64): 0 closed, 32 clears the
  unchanged collision threshold, 64 parks both leaves in the pockets. There is
  no renderer-side door state machine and no gameplay/VM/frozen-S1 change.
- The leaves are runtime raw-RGB assets (`assets/doors/*.rgb`), loaded once by
  `gl_upload_rgb_file()`. A missing asset fails loudly rather than silently
  reverting to the old 64×64 door. Texture memory adds 2,700,400 bytes.
- Matched 960×600, 180-frame runs: G5w 81.9/54.0 FPS versus G5x 82.1/53.4 FPS
  (no objects/objects). Executable 6,121,456 bytes (G5w 6,117,184); loader
  high-water unchanged at 53,705/54,569 cells; heap 33,440/33,600.
- [FINDING] Embedding the two leaves as C arrays added 2.7 MB to `.rodata`,
  shifted `.bss` by 2.7 MB, and consistently slowed the same benchmark
  (object GL run about 48 FPS versus 57 FPS) with no rendering-code change.
  Runtime asset loading restored the original layout and performance. Large
  static art arrays should therefore be treated as a measurable layout risk,
  not merely as binary size.
- Native-source limitation: 450/410 pixels per leaf is far better than 64×64
  but not the 1024×2048-per-leaf ideal; the extreme 24-unit route close-up
  still shows source softness. Full report:
  `experiments/glon-doom/G5X-BLAST-DOOR-REPORT.md`.

### G5x2 native-resolution blast-door leaves [MEASURED]

[MEASURED 2026-11-15, `experiments/glon-avp-g5x2`,
WSLg D3D12 AMD Radeon(TM) Graphics]

- The supplied G5x2 artwork is two independent 1070×1470 front views. The
  64×112 leaf face cannot use that aspect without stretching, so the converter
  keeps the full height and trims 230 pixels from each **outer** edge: two
  840×1470 leaves (13.125 texels per world unit, versus about 7.0 in G5x).
  The inner seam edges are retained; no upscaling, mirroring or procedural
  reconstruction is used. Source checksums and the crop are enforced by
  `tests/check_g5x2_door.py`.
- Geometry, casing, side pockets, leaf travel, `door_open` thresholds,
  collision, Alien navigation and frozen S1 are unchanged. The executable is
  still 6,121,456 bytes; loader high-water/heap stay 53,705/54,569 and
  33,440/33,600. GPU texture memory rises from 2,700,400 to 9,878,400 bytes
  (+7,178,000).
- Sampling stays on the renderer's `GL_LINEAR` path, with `GL_CLAMP_TO_EDGE`
  for the leaf quads (they span exactly 0..1, so repeat filtering could blend
  the seam edge with the opposite edge).
- [FINDING] Mipmapping these textures is a measurable loss on this driver.
  Both `GL_LINEAR_MIPMAP_LINEAR` and `GL_LINEAR_MIPMAP_NEAREST` raised
  object-mode tick time from about 8.9 to 11.5 ms and reduced median FPS to
  about 55–59. The GL_LINEAR no-mipmap path keeps tick time level (median
  8.86 → 9.04 ms) and object-mode median 67.0 → 65.2 FPS (-2.7%, within run
  spread); no-object mode was 98.2 → 106.9 FPS in the same randomized session.
  Mipmapping large leaf textures should not be re-enabled without a fresh
  driver measurement.
- Native-source limitation: at 1920 the 24-unit close-up still shows some
  source softness because 840 source pixels cover roughly 1500 screen pixels.
  The outer-edge crop also removes the left vent and the right baked control
  strip (the latter is not functional). Full report:
  `experiments/glon-doom/G5X2-DOOR-TEXTURE-REPORT.md`.

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
