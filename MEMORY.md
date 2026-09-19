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

## 4. Current benchmark baseline (P5)

P5 (`fib-opt-p5`) is the current optimisation baseline. Naive recursive
Fibonacci (`fib: func [n] [ either <= n 1 [ n ] [ + fib - n 1 fib - n 2 ] ]`):

| metric | P3 | P4 | P5 |
|---|---|---|---|
| fib 25 result / calls | 75025 / 242785 | 75025 / 242785 | 75025 / 242785 |
| logical lookups | 1 699 493 | 1 092 531 | 1 092 531 |
| direct lexical-slot accesses | — | 606 962 | 606 962 |
| slots examined (linear) | 11 653 672 | 11 046 710 | **0** |
| hash probes | — | — | 2 185 061 |
| parent hops | 1 092 530 | 1 092 530 | 1 092 530 |
| managed allocs / GC cycles | 1 / 0 | 1 / 0 | 1 / 0 |
| fib 25 wall-clock (median) | ~10.2 s | ~10.0 s | ~8.0 s |
| fib 20 wall-clock (median) | ~0.91 s | ~0.82 s | ~0.71 s |

P4 removed static lexical scanning (`T_BOUND` depth/slot). P5 removed dynamic
name scanning: profiling showed every remaining `fib 25` dynamic lookup was a
global (`either`, `<=`, `+`, `-`, `fib`), and the ~11M slot examinations were
entirely linear scans of the global context plus the child-context miss. Each
context now carries a fixed hash index over the interned word id (word id →
slot; open addressing, linear probing, no tombstones); `r_lookup` probes it when
`count < cap` and falls back to the ordered scan when the index is full. The
ordered `(word,value)` pairs remain authoritative for storage, rebinding,
introspection, the debugger and RAW; the hash is only an index. Dynamic
rebinding, closures, promotion, multitasking, RAW and the debugger are all
unchanged. 326 regressions pass and the frozen-S1 guard passes; P5 is retained.
Name resolution is no longer the dominant cost — the leading candidates are now
evaluator dispatch, argument/subexpression evaluation and native/HOST
arithmetic.

### Rebol3 comparison (measured, same PC)

On the same PC, using the same naïve recursive Fibonacci workload, local
**Rebol3** (Oldes' `Rebol/Bulk 3.22.1`, Windows-native) measured `fib 20`
~0.0089 s and `fib 25` ~0.099 s, while **Glon P5** measured ~0.806 s and ~9.01 s
— about **~90× faster** on fib 20/25. This is one workload/one PC/one timing
method, not a general claim. Full method and caveats (cross-environment
Windows-vs-WSL2, ms timer resolution): `R3-FIB-COMPARISON.md`.

### P6 diagnostic conclusion (where the ~9 s goes)

P6 (`fib-profile-p6`) counted the interpreter's work for `fib 25`: **1.1 billion
S1 opcode dispatches** — ~4560 per fib call, and **169 million HOST calls**
(~698 per call, ~279× the fib's own 606 961 arithmetic natives). The opcode mix
is dominated by the memory-mapped register-access idiom: LIT 43.8%, `@` 19.3%,
`!` 12.8% (76% total), HOST 15.3%, `0BRANCH` 4.6%. At the `-O0` baseline that is
~8.8 s ≈ ~24 CPU cycles per opcode.

Conclusion: the cost is **instruction amplification** (the Glon evaluator
expressed in S1 expands each fib call into thousands of LIT/`@`/`!`/HOST cells,
because IP/SP/RP/HP are memory-mapped), compounded by an **unoptimised `switch`
dispatch** (`-O0`). Hash lookup is now immaterial (0.2% of opcodes). Leading P7
candidates, in order: (1) reduce emitted S1 instruction count; (2) cheaper
dispatch (`-O2` / computed goto) — a separate experiment. Full detail:
`FIB-OPT-P6-PROFILE.md`.

### P6B compiler control (how much is the unoptimised C build?)

P6B (`fib-profile-p6b-compiler`) changed only the C optimisation level, not the
S1/Glon source. `fib 25` (1.1071B opcodes fixed): `-O0` 8.54 s → `-O1` 4.99 s
(1.71×) → `-O2` 4.00 s (2.13×) → `-O3` 4.05 s (2.11×, tied with `-O2`). The
opcode switch was already a jump table even at `-O0`; the `-O2` win is register
promotion + HOST inlining, not a dispatch change. Normal compilation removes
~half the time, cutting the R3 gap from ~86× to ~40× — a modest speedup
(Outcome B). Instruction amplification remains dominant; P7 stays justified,
now measured from a ~4.0 s `-O2` baseline. Full detail:
`FIB-OPT-P6B-COMPILER.md`.

### P7 evaluator density (say the same thing in fewer S1 words)

P7 (`fib-opt-p7-density`) is the first emitted-evaluator-code density pass: no
S1 change, no opcode change, no HOST semantics. It found the hot path dominated
by call/return overhead + `reduce` round-trips (the `subexpr` value-dispatch
tail, `reduce` after every argument) and removed them: tail-call elimination
(`CALL X; EXIT` → `BRANCH X`), a dense `reduce` with an `N == 1` fast path,
`discard` inlined into `block_eval`, and `BOUND`/arithmetic-native dispatch
reorders. Result: `fib 25` S1 instructions **1,107,100,190 → 1,023,582,152
(−7.54%)**; amplification 4560 → 4216 ops/call, 414.6 → 383.3 ops/subexpr,
698 → 656 HOST/call; every evaluator event counter unchanged (same work, denser).
Wall-clock `-O2` ≈ 4.9 s → 3.6 s (~1.37×), cutting the R3 gap to ~36×. 326/326
tests pass; frozen-S1 guard passes. Next target is the activation/binding path
(`invoke_closure` + `mkctx` + `append` ≈ 28%). Full detail:
`FIB-OPT-P7-DENSITY.md`, `FIB-OPT-P7-RESULTS.md`.

### P8 lazy context hash (activations don't need a full hash index)

P8 (`fib-opt-p8-lazy-ctx`) targets the activation path. It found that every one
of fib's 242,785 arity-1 activations eagerly zeroed a 16-slot P5 hash index and
`hash_insert`ed its parameter, yet the child-context hash was **never hit** —
all ~1.09M probes were misses for global words, and parameters resolve via P4
`T_BOUND`/`load_lex`. P8 makes the hash threshold-lazy (`HASH_MIN = 4`): `mkctx`
no longer zeroes it, `append`/`set` materialise it (via a new `build_hash`) only
when count reaches `HASH_MIN`, and `lookup` uses the linear scan below that.
Result: `fib 25` **1,023,582,152 → 925,982,568 (−9.5%)**; amplification
4216 → 3814 ops/call, 383.3 → 346.7 ops/subexpr, 656 → 588.5 HOST/call;
hash-probes halved (2.19M → 1.09M, all global hits). Wall-clock `-O2` ≈ 3.7 s
→ 3.2 s (~1.16×), R3 gap ~32×. 326/326 tests pass; frozen-S1 guard passes.
Remaining activation cost is the 9-field frame save/restore + parameter `append`.
Full detail: `FIB-OPT-P8-LAZY-HASH.md`, `FIB-OPT-P8-RESULTS.md`.

### P9 dense frame (allocate once, store at fixed offsets)

P9 (`fib-opt-p9-frames`) tested whether the activation frame could be densified
by reserving the frame base once and storing the 9 fields at fixed symbolic
`FRAME_*` offsets instead of nine derived `>R` pushes (each re-reads/rewrites the
memory-mapped RP). Result: **essentially a negative result** — `fib 25`
instructions fell only **−0.94%** (925,982,568 → 917,242,308; 3814 → 3778
ops/call), and wall-clock is a wash (~3.2 s), because the `>R` RP traffic is
cheap LIT/`@`/`!` while the fixed-offset stores add `HOST ADD` address
arithmetic. Layout, RAW ABI, alignment and semantics are unchanged; 326/326
tests pass; frozen-S1 guard passes. P9 is retained (small, correct) but the
finding is that **no further interpreter-density pass is likely to change the
performance class** — the remaining ~917M ops are fundamental interpretation
overhead (tagged dispatch, memory-mapped registers, call convention). Next
experiment should **compile the frozen S1 stream** instead of interpreting it.
Full detail: `FIB-OPT-P9-FRAMES.md`, `FIB-OPT-P9-RESULTS.md`.

### P10A compiled-S1 baseline (what does interpretation itself cost?)

P10A (`fib-opt-p10a-compiled-s1`) answered the controlled question by
mechanically compiling the frozen S1 stream: a driver emits a literal
computed-goto C transcription of `M[256..asm_here())` (one label per
instruction, operands as constants, a verbatim copy of the frozen `host()`
switch, `goto *table[IP-256]` dispatch), compiles it `-O2 -shared -fPIC`, and
runs it through a new `r0_s1_run_compiled` (same setup, different executor).
`IP/SP/RP/HP` stay memory-mapped; HOST unchanged. **Result: the interpreted
layer (opcode fetch + decode + switch dispatch) costs ~2.4×.** `fib 25` fell
from ~4.3 s (interpreted, same session) to ~1.8 s; compiled is ~18× slower than
R3 (was ~32×). Correctness: fib/tree/raw all MATCH; 326/326 interpreted tests
pass; frozen-S1 guard passes. The remaining ~18× is the memory-mapped-register
access + HOST dispatch + the ~3.8k-op/activation S1 stream itself — the P10B
targets. Next experiment: **P10B register promotion / HOST intrinsic expansion**.
Full detail: `FIB-OPT-P10A-COMPILED-S1.md`, `FIB-OPT-P10A-RESULTS.md`.

### P10B register promotion (what does memory-mapped register traffic cost?)

P10B (`fib-opt-p10b-registers`) held `IP`/`SP`/`RP` in C locals during
compiled-S1 execution (HP stays memory-mapped), lowering the frozen `LIT 0/1/2
@`/`!` idiom to direct local access and passing `sp` by value through HOST, with
explicit synchronisation only at entry/`HALT`/`HOST_DUMP`. **Result: ~5× faster
than P10A.** `fib 25` ≈ 0.43 s (was ~2.0 s P10A, ~4.3 s interpreted); the R3 gap
falls to **~4.3×**. Static register-cell accesses drop from ~17,000 to ~160.
HOST calls are unchanged (142.9M) and now dominate. Correctness: 5 workloads
(fib/tree/raw/non-local-return/closure) MATCH; 326/326 interpreted tests pass;
frozen-S1 guard passes. Next target: **P10C HOST intrinsic expansion**.
Full detail: `FIB-OPT-P10B-REGISTERS.md`, `FIB-OPT-P10B-RESULTS.md`.

### P10C HOST intrinsic expansion (is HOST dispatch the remaining cost?)

P10C (`fib-opt-p10c-host`) lowered the 12 pure arithmetic/comparison HOST
operations inline into the compiled-S1 path (the S1 stream still says `HOST`;
`ALLOC`/`PUTCHAR`/`PRINT`/`DUMP` stay generic). Result: **no speedup (~1.0×)** —
`fib 25` ≈ 0.38 s (P10B ≈ 0.38 s), ~3.8× R3. The generic HOST dispatcher was not
the bottleneck: the remaining cost is the data/return-stack cell memory traffic
(`M[sp]`/`M[rp]`) and the per-instruction computed-goto, not the HOST switch.
Correctness: 7 workloads (incl. arithmetic edge cases) MATCH; 326/326 tests
pass; frozen-S1 guard passes. Next target: **P10D data/return-stack cell traffic
or computed-goto elimination**.
Full detail: `FIB-OPT-P10C-HOST.md`, `FIB-OPT-P10C-RESULTS.md`.

### P10D single TOS cache (is the top stack cell traffic the cost?)

P10D (`fib-opt-p10d-tos`) cached the top data-stack element in a C local `tos`
(`sp` shifts to point at the second-from-top; flush `M[--sp]=tos`, reload
`tos=M[sp++]`; the SP register idiom is virtualised). Result: **modest positive
~1.32×** over P10C — `fib 25` ≈ 0.31 s, ≈ **3.1× R3**; ~28% fewer `M[sp]`
references. The remaining cost is the second-from-top cell, the return stack,
and the per-instruction computed-goto (~917M indirect jumps). Correctness: 8
workloads MATCH; 326/326 tests pass; frozen-S1 guard passes. Next candidates:
**P10E two-cell TOS/NOS caching** and/or **computed-goto elimination**.
Full detail: `FIB-OPT-P10D-TOS.md`, `FIB-OPT-P10D-RESULTS.md`.

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
| P5 hash lookup | `fib-opt-p5` | `6a41848` |
| P6 profile | `fib-profile-p6` | `3219e42` |
| P6B compiler control | `fib-p6b-compiler-results` | `81b7d6d` |
| P7 density | `fib-p7-density-results` | `79a1be6` |
| P8 lazy hash | `fib-p8-lazy-hash-results` | `d40e6c8` |
| P9 dense frame | `fib-p9-frame-results` | `1282f0b` |
| P10A compiled S1 | `fib-p10a-compiled-s1-results` | `bf1edeb` |
| P10B registers | `fib-p10b-register-results` | `0ac3c46` |
| P10C HOST intrinsics | `fib-p10c-host-results` | `d1d0921` |
| P10D TOS cache | `fib-p10d-tos-results` | *(this phase)* |

## 6. Lessons learned

- The frozen substrate keeps proving sufficient: every milestone (debugger,
  tasks, GC, datatypes, four optimisation phases) landed with **zero** new S1
  primitives.
- Allocation/GC was removable from Fibonacci first (P2/P3); name resolution was
  next — lexical (P4, `T_BOUND`) and dynamic (P5, per-context hash index) are now
  both gone from the hot path.
- Fib is a microscope, not a specification: tiny one-binding contexts hide the
  win from lexical-slot access; a fixed cap-sized hash index is O(1) for the
  global scan but costs 33–43% more context memory.
- Stack sizing is real infrastructure: P3's context-on-return-stack forced the
  M1 task arena to be enlarged (see `FIB-OPT-P3-CONTEXTS.md`); P5's 64-cell
  context still fits the 30-deep multitasking regression.

## 7. Roadmap

```text
P5 complete
    ↓
profile current runtime
    ↓
choose next optimisation from evidence
    ↓
broader benchmark suite
    ↓
minimal browser/runtime surface
    ↓
Glon Shop
    ↓
programming-model experiments
       ├─ objects
       ├─ reactive/dataflow
       ├─ FBP
       ├─ actors
       └─ tuple-space / distributed Glon
```

- P4 and P5 are complete and retained; `fib-opt-p5` is the current optimisation
  baseline. P6 (`fib-profile-p6`) is a completed diagnostic phase; P6B
  (`fib-profile-p6b-compiler`) is a completed compiler-control phase; P7
  (`fib-opt-p7-density`) and P8 (`fib-opt-p8-lazy-ctx`) are completed evaluator-
  density phases.
- P6 profiled the remaining cost: 1.1B S1 opcode dispatches / 4560 per fib call,
  dominated by the memory-mapped register-access idiom (LIT/`@`/`!` = 76%) and
  HOST arithmetic (15%). P6B then measured the C-compiler factor alone: normal
  `-O2` removes ~2.1× (8.54 s → 4.00 s) — the opcode switch was already a jump
  table at `-O0`, so the win is register promotion + HOST inlining. P7 then
  densified the evaluator itself (tail-calls, dense `reduce`, dispatch
  reordering): −7.5% instructions (1.107B → 1.024B, 4560 → 4216 ops/call) and
  ~1.37× faster at `-O2`, with no S1 change. P8 then made the activation hash
  index threshold-lazy (`HASH_MIN = 4`): −9.5% instructions (1.024B → 926M,
  4216 → 3814 ops/call) by eliminating per-activation hash zeroing/insert for
  small contexts. P9 then densified the frame save/restore (fixed-offset field
  stores + one RP adjustment): −0.94% instructions (926M → 917M, 3814 → 3778
  ops/call) with no timing change — a negative result showing the `>R` RP
  traffic was not the bottleneck. **Conclusion: further interpreter-density
  passes will not change the performance class; the next experiment should
  compile the frozen S1 stream** (eliminate the `s1_run` switch dispatch and
  lower the memory-mapped registers to native code).
- P10A (`fib-opt-p10a-compiled-s1`) then measured that exact cost: a mechanical
  computed-goto compilation of the frozen S1 stream (memory-mapped registers
  and HOST untouched) is **~2.4× faster** — the interpreted layer (opcode fetch
  + decode + switch) costs ~2.4×. Compiled `fib 25` ≈ 1.8 s ≈ 18× R3. The
  remaining ~18× is the memory-mapped-register access + HOST dispatch +
  evaluator amplification, so the next experiment is **P10B register promotion
  + HOST intrinsic expansion** on top of the compiled-S1 baseline.
- P10B (`fib-opt-p10b-registers`) then promoted `IP`/`SP`/`RP` to C locals in
  the compiled path: **~5× faster than P10A**, `fib 25` ≈ 0.43 s ≈ **4.3× R3**.
  The memory-mapped register traffic was the dominant remaining cost after
  dispatch removal.   HOST (142.9M dispatches, unchanged) now dominates — the
  next experiment is **P10C HOST intrinsic expansion**.
- P10C (`fib-opt-p10c-host`) then lowered the pure arithmetic HOST ops inline:
  **no speedup (~1.0×)** — HOST dispatch was not the bottleneck; the remaining
  ~3.8× R3 gap is the data/return-stack cell memory traffic and the
  per-instruction computed-goto. P10D (`fib-opt-p10d-tos`) then cached the top
  data-stack cell: **~1.32×** (0.31 s, ~3.1× R3), a modest win. Next experiment:
  **P10E two-cell TOS/NOS caching** and/or **computed-goto elimination**.
- Continue the rule: **one architectural performance hypothesis per phase.**
- The broader benchmark suite is: Fibonacci (recursion/call overhead), tight
  loop (evaluator/branch overhead), counter closure (captured mutation),
  block/list traversal (managed data), parser workload
  (parsing/backtracking/lookup), task ping-pong (context-switch cost).
- Distributed Glon is an architectural/research target, not a replacement for
  the nearer Glon Shop work.

## 8. Architectural headroom — multiple computational models

Glon need not commit to a single programming model. Different models may suit
different classes of problem and should be constructible **above the same frozen
substrate**.

Candidate models: ordinary procedural/functional Glon; objects/prototypes/
structured data; reactive programming; dataflow; flow-based programming (FBP);
actors/message passing; CSP/channel-style systems; tuple-space/blackboard
coordination; distributed agent systems; domain-specific dialects. These are not
mutually exclusive and may coexist in one application.

Likely fits: ordinary Glon for algorithms/glue; objects for domain state;
reactive/dataflow for GUI state; FBP for pipelines; green tasks for local
asynchronous work; the tuple space for distributed coordination; contracts for
subsystem boundaries; HOST for external machinery.

Principle: **choose the computational model to fit the problem, not one model
imposed by the language designer.** An open research goal is to test how many
such models express cleanly without modifying S1.

Related principles (see `docs/distributed-glon-agents.md`):

- **A shared space without contracts permits coordination but not reliable
  governance. Contracts define what may be exchanged, who may act, what
  completion means, and what authority is granted.**
- **Distributed Glon separates computational semantics from transport topology:
  a Glon program expresses the work and its contract; HOST determines whether
  the participant is local, remote, CPU, GPU, Jetson or another machine.**
- **The BBS was the naturally evolved prototype; structured tuple-space
  coordination is the deliberate design.**

## 9. Distributed Glon (design direction)

The same philosophy that permits several computational models above one frozen
local substrate can permit many Glon instances to cooperate above an abstract
distributed coordination space. The minimal shared-space operations are
`put`/`take`/`read`/`watch` (publish, atomically claim, observe, react), with
claim/lease semantics for failure recovery, capability-limited federation rather
than unrestricted trust, and contracts (data/behavioural/security) as the
mandatory boundary between otherwise-independent computational models. Not yet
implemented — see `docs/distributed-glon-agents.md` for the full design.

## 10. Glon Shop application target

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

## 11. Detailed documents

- `README.md` — project intro.
- `R0-S1-PHASE1.md`, `R0-S1-RESULTS.md`, `R0-ARCHITECTURE.md` — R0-on-S1.
- `M1-MULTITASKING-RESULTS.md`, `M2-GC-DESIGN.md`, `M3-DATATYPE-DESIGN.md`,
  `M3D-MANAGED-BLOCK-DESIGN.md`, `DEBUGGER-D1-*.md`, `W2-STANDALONE-WASM-RESULTS.md`
  — milestone design/results.
- `FIB-PROFILE-P1.md`, `FIB-OPT-P2-FRAMES.md`, `FIB-OPT-P3-CONTEXTS.md` —
  optimisation phases.
- `FIB-OPT-P4-LEX.md`, `FIB-OPT-P4-RESULTS.md` — P4.
- `FIB-OPT-P5-HASH-LOOKUP.md`, `FIB-OPT-P5-RESULTS.md` — P5 (current baseline).
- `FIB-OPT-P6-PROFILE.md` — P6 diagnostic profile (where the ~9 s goes).
- `FIB-OPT-P6B-COMPILER.md` — P6B compiler-optimisation control (the `-O` factor).
- `FIB-OPT-P7-DENSITY.md`, `FIB-OPT-P7-RESULTS.md` — P7 evaluator-density pass.
- `FIB-OPT-P8-LAZY-HASH.md`, `FIB-OPT-P8-RESULTS.md` — P8 lazy activation hash.
- `FIB-OPT-P9-FRAMES.md`, `FIB-OPT-P9-RESULTS.md` — P9 dense activation frame.
- `FIB-OPT-P10A-COMPILED-S1.md`, `FIB-OPT-P10A-RESULTS.md` — P10A compiled-S1 baseline.
- `FIB-OPT-P10B-REGISTERS.md`, `FIB-OPT-P10B-RESULTS.md` — P10B register promotion.
- `FIB-OPT-P10C-HOST.md`, `FIB-OPT-P10C-RESULTS.md` — P10C HOST intrinsic expansion.
- `FIB-OPT-P10D-TOS.md`, `FIB-OPT-P10D-RESULTS.md` — P10D single TOS cache.
- `R3-FIB-COMPARISON.md` — local Rebol3 vs Glon P5 Fibonacci benchmark.
- `docs/glon-shop-product-spec.md` — Glon Shop (browser/shop application target).
- `docs/distributed-glon-agents.md` — distributed/federated agent architecture.
