# GLON / S1 — Project Memory

Canonical transfer-memory document. Compact orientation for a new session; the
detailed documents remain authoritative for their own subjects and are linked
at the bottom. Do not treat this file as a substitute for the source or the
detailed reports.

## 1. What this project is

GLON is a tiny embeddable REBOL-family runtime built as an experiment in how
much high-level language machinery can be constructed above a very small,
stable execution substrate, rather than by continually enlarging the machine.

```text
hardware / WASM / host
        ↓
frozen S1 machine
        ↓
R0 evaluator + RAW trapdoor
        ↓
GLON
        ↓
libraries / applications / dialects
```

Central principle: **Forth optimises transparency of execution; Glon optimises
transparency of intention.** RAW remains the generic low-level trapdoor; the
shorthand is "stay in GLON unless you genuinely need to descend".

## 2. Frozen invariants

- **S1 has exactly seven irreducible primitives** and no eighth:
  `LIT DUP DROP @ ! 0BRANCH HOST`. IP/SP/RP/HP are memory-mapped; calls,
  returns, jumps, loops and non-local control are all derived.
- **S1 is frozen** at tag `s1-frozen-v1` (`f90496c…`); `./check-frozen-s1.sh`
  must keep passing. There is a WASM target (`glon-w2-standalone-v1`).
- **HOST is mundane**: arithmetic, comparison, allocation, I/O, diagnostics.
  It must never acquire evaluator or control semantics.
- **RAW** is the generic first-class low-level trapdoor, with a symbolic ABI;
  the loader/preprocessor is legitimate tooling (there is no pretence of "no
  preprocessing").

## 3. Already demonstrated (not future work)

Above the frozen substrate, without adding an S1 primitive, the project has
already built and tested:

- a genuine R0 evaluator running as S1 machine code;
- words, blocks, contexts, functions and lexical closures;
- lexical capture and captured mutation;
- recursion;
- zero/one/multiple result arity;
- definitional non-local `RETURN` through unaware callers;
- user-defined first-class escape;
- first-class `RAW` with a symbolic ABI;
- a user-space cooperative debugger (suspend/inspect/resume);
- cooperative green threads with suspend/resume and task-local execution state;
- a shared-heap exact, non-moving, cycle-safe mark/sweep GC;
- user-defined structured datatypes (`datatype!`, `person!`, `graph-node!`, …);
- a managed immutable `STRING!`;
- a managed runtime `BLOCK!`;
- task return-stack overflow protection;
- a standalone WebAssembly build with a tiny handwritten JS host.

## 4. Current benchmark baseline (P4)

P4 (`fib-opt-p4` → `d8161b0`) is the current optimisation baseline. Naive
recursive Fibonacci (`fib: func [n] [ either <= n 1 [ n ] [ + fib - n 1 fib - n 2 ] ]`):

| metric | P3 | P4 |
|---|---|---|
| fib 25 result / calls | 75025 / 242785 | 75025 / 242785 |
| logical lookups | 1 699 493 | 1 092 531 |
| direct lexical-slot accesses | — | 606 962 |
| slots examined | 11 653 672 | 11 046 710 |
| parent hops | 1 092 530 | 1 092 530 |
| managed allocs / GC cycles | 1 / 0 | 1 / 0 |
| fib 25 wall-clock (median) | ~10.2 s | ~10.0 s |
| fib 20 wall-clock (median) | ~0.91 s | ~0.82 s |

P4 moved statically-resolvable lexical references (parameters) to a new
`T_BOUND` (13) value that encodes `(depth, slot)` and is resolved at load time,
so execution reads the slot directly with no name scan. Performance is
essentially neutral (see §7 lesson): the direct access removes the scan but adds
fixed decode work that is not cheaper than a one-binding scan.

The remaining dynamic/global lookup path still accounts for roughly **11
million slot examinations** and now dominates lookup cost.

## 5. Milestones (branch / tag → commit)

In order:

| milestone | tag | commit |
|---|---|---|
| S1 substrate | `s1-frozen-v1` | `f90496c` |
| R0 evaluator + trapdoor | `r0-trapdoor-v2` | `9ce3d82` |
| debugger | `debugger-d1-frozen-v1` | `6d6c50c` |
| standalone WASM | `glon-w2-standalone-v1` | `0329df2` |
| M1 multitasking | `glon-m1-multitasking-v1` | `7e81134` |
| M2 GC | `glon-m2-gc-v1` | `f58870b` |
| M3A datatypes | `glon-m3a-datatypes-v1` | `48c7d81` |
| M3B genealogy | `glon-m3b-genealogy-v1` | `a04822f` |
| M3C STRING! | `glon-m3c-string-v1` | `393c0e2` |
| M3D BLOCK! | *(untagged)* | `f221b84` |
| P1 profiling | `fib-profile-p1` | `cb8f8c0` |
| P2 frames | `fib-opt-p2` | `6861194` |
| P3 contexts | `fib-opt-p3` | `67bb997` |
| P4 lexical | `fib-opt-p4` | `d8161b0` |

P4 commits: `a58c48a` (implementation), `d8161b0` (results documentation).

## 6. Lessons learned

- The frozen substrate keeps proving sufficient: every milestone (debugger,
  tasks, GC, datatypes, three optimisation phases) landed with **zero** new S1
  primitives.
- Allocation/GC was removable from Fibonacci first (P2/P3); name resolution is
  the remaining cost and is harder to remove (P4 was architecturally successful
  but timing-neutral).
- Fib is a microscope, not a specification: tiny one-binding contexts hide the
  win from lexical-slot access; the global scan now dominates.
- Stack sizing is real infrastructure: P3's context-on-return-stack forced the
  M1 task arena to be enlarged (see `FIB-OPT-P3-CONTEXTS.md`).

## 7. Roadmap

1. P4 is complete and retained; `fib-opt-p4` is the current optimisation
   baseline.
2. Consolidate profiling after P4.
3. Select P5 from measured remaining costs.
4. The remaining dynamic/global name scan is the leading candidate (≈11M slot
   examinations remain).
5. Do **not** assume P5 is evaluator dispatch unless profiling supports it.
6. Continue the rule: **one architectural performance hypothesis per phase.**
7. After the optimisation sequence, establish the broader benchmark suite:
   Fibonacci (recursion/call overhead), tight loop (evaluator/branch overhead),
   counter closure (captured mutation), block/list traversal (managed data),
   parser workload (parsing/backtracking/lookup), task ping-pong
   (context-switch cost).
8. Then begin the minimal browser/runtime work needed for Glon Shop (§9).
9. Later, use FBP/dataflow/etc. as experiments in programming-model
   extensibility (§8).

## 8. Architectural headroom — multiple computational models

Glon need not commit to a single programming model. Different models may suit
different classes of problem and should be constructible **above the same frozen
substrate**.

Candidate models: ordinary procedural/functional Glon; object/prototype systems;
flow-based programming (FBP); dataflow; reactive programming;
actors/message-passing; CSP/channel-style concurrency; domain-specific
dialects. These may coexist in one application.

Likely fits: ordinary Glon for algorithms/glue; structured objects/datatypes
for domain state; reactive/dataflow for GUI dependencies; FBP for pipelines and
component networks; green tasks/actors for independent asynchronous activities;
parsing dialects for grammars/protocols.

Principle: **choose the computational model to fit the problem, not one model
imposed by the language designer.** An open research goal is to test how many
such models express cleanly without modifying S1.

## 9. Glon Shop application target

Post-optimisation application target: **Glon Shop**, a small browser shop
demonstrating a persistent Glon/S1 WASM interpreter.

```text
browser
    ↓
persistent Glon/S1 WASM
    ↓
Glon shop logic / small web dialect
    ↓
thin HOST/JavaScript bridge
    ↓
DOM / fetch / history / browser events
    ↓
small HTTP API
    ↓
optional DynamoDB / backend database
```

Primary demonstrations: instantiate Glon/S1 WASM once; fetch/display a product
catalogue; product detail; cart; cart persists in Glon state while navigating;
partial-page navigation; browser events invoke Glon behaviour; async fetch can
suspend/resume Glon execution; simple order submission; JavaScript restricted
to thin HOST bridging.

Browser machinery stays in the browser — do not implement in Glon unless
justified: DOM engine, HTML parser, virtual DOM, HTTP stack, TLS, a JavaScript
clone, or a database engine. Likely missing application-facing facilities:
richer UTF-8/string ops, binary values later, collection conveniences, JSON,
URL handling, a fetch bridge, a small DOM bridge, an event bridge,
history/navigation.

Principle: **let the shop reveal what Glon actually needs rather than adding
language facilities speculatively.** Full specification: `docs/glon-shop-product-spec.md`.

## 10. Detailed documents

- `README.md` — project intro.
- `R0-S1-PHASE1.md`, `R0-S1-RESULTS.md`, `R0-ARCHITECTURE.md` — R0-on-S1.
- `M1-MULTITASKING-RESULTS.md`, `M2-GC-DESIGN.md`, `M3-DATATYPE-DESIGN.md`,
  `M3D-MANAGED-BLOCK-DESIGN.md`, `DEBUGGER-D1-*.md`, `W2-STANDALONE-WASM-RESULTS.md`
  — milestone design/results.
- `FIB-PROFILE-P1.md`, `FIB-OPT-P2-FRAMES.md`, `FIB-OPT-P3-CONTEXTS.md` —
  optimisation phases.
- `FIB-OPT-P4-LEX.md`, `FIB-OPT-P4-RESULTS.md` — P4 (current baseline).
- `docs/glon-shop-product-spec.md` — Glon Shop specification.
