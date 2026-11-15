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

Design decision: **Glon abstractions are permeable.** High-level language and
GUI dialects must retain a defined trap door to the underlying host substrate
(raw HTML/CSS/DOM/browser APIs, and the RAW/S1/HOST runtime substrate). This is
formalised as the **Substrate Principle** in `DESIGN.md`.

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

### P10E two-cell (TOS + NOS) caching

P10E (`fib-opt-p10e-nos`) cached the second stack item (`nos`) too, with `sp`
pointing at the third (`sp-2` = top slot). Result: **small gain ~1.08×** over
P10D — `fib 25` ≈ 0.29 s, ≈ **2.9× R3**. Memory-reference count is unchanged;
the win is latency hiding (the refill `nos=M[sp++]` is off the arithmetic's
critical path). Deeper stack caching is not worth pursuing. The remaining ~2.9×
is dominated by the per-instruction **computed-goto dispatch** (~917M indirect
jumps). Correctness: 8 workloads MATCH; 326/326 tests pass; frozen-S1 guard
passes. Next experiment: **computed-goto dispatch elimination** (direct
fall-through for sequential S1).
Full detail: `FIB-OPT-P10E-NOS.md`, `FIB-OPT-P10E-RESULTS.md`.

### P10F sequential fall-through / dispatch elimination

P10F (`fib-opt-p10f-fallthrough`) removed the per-instruction `goto *T[ip-256]`
for sequential S1 flow (fall-through in generated C; only `0BRANCH`, the
`LIT 0 !` branch idiom, `HALT` and error paths dispatch). Result: **near-negative
~1.05×** over P10E — `fib 25` ≈ 0.29 s, ≈ **2.9× R3**. Static dispatch sites
collapse 4,519 → 354 (~92%), but the CPU branch predictor was already absorbing
the indirect jumps for sequential flow, so runtime barely moves. The remaining
~2.9× is the work inside the operations and the remaining `M[sp]`/`M[rp]`
memory traffic — no single dominant lever remains. Correctness: 8 workloads
MATCH; 326/326 tests pass; frozen-S1 guard passes. Next: measured re-profiling
rather than another micro-optimisation.
Full detail: `FIB-OPT-P10F-FALLTHROUGH.md`, `FIB-OPT-P10F-RESULTS.md`.

### Closure-origin binding law + CASE (post-Alpha)

A review for "can CASE be ordinary Glon?" found CASE-as-Glon impossible on the
old runtime: turning a caller's block into a closure hit five general bugs
(shadowed params parsed as outer `T_BOUND`; literal func re-evaluated under
its own live activation mis-captured; closures in escaped closures and
dead-origin blocks read stale slots; factory promotion gave a snapshot copy and
clobbered the factory's `RV_CTX`; a fresh closure was swept by a GC triggered
by its own promotion). Fixed in the func/mkclosure/parser path only (fib hot
path untouched, +224 code cells); the law is in `GLON-ALPHA-LAWS.md` §4.
`case` is now pure Glon in `demo/shop/case.glon` (block conditions; RETURN in
an action returns from the action). It is not in `common.glon`: the shipped
bundles have no headroom (traffic.html is 506/511 forms per block; the G1E
image fills the loader heap). Tests: `r0_s1_case_tests.c`.

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
| P10D TOS cache | `fib-p10d-tos-results` | `c93e5db` |
| P10E TOS+NOS cache | `fib-p10e-nos-results` | `7bd42cb` |
| P10F fall-through | `fib-p10f-fallthrough-results` | *(this phase)* |
| Glon LaTeX v001–v003 (**deployed**: `/latex/` page) | *(untagged)* | `3bfabe9`, `6cdae85`, `e5df1a0` |
| GC worklist bound + fail-stop | *(untagged)* | `a1d0f8a` |
| `strings-ext.glon` optional string API | *(untagged)* | `96a0d13` |
| Pratt parser v004 (**experiment, not deployed**) | *(untagged)* | *(the commit adding `experiments/glon-pratt/`)* |
| Iterator vocabulary v001 (**experiment, not deployed**) | *(untagged)* | *(the commit adding `experiments/glon-iter/`)* |

**Glon LaTeX milestone (2026-11-06/07).** What is deployed and what is
experimental are different things:
- **Deployed** (GitHub Pages `/latex/`): the `l/` LaTeX vocabulary in Glon
  (v001), Chris Ross-Gill's `math.test.reb` figures rendered through KaTeX
  (v002), and the infix-to-Glon translator **written in JavaScript**
  (`web/infix.js`, v003). The page runs Glon in WASM; CI tests it on every
  publish.
- **Experimental, not deployed:** Pratt v004 (`experiments/glon-pratt/`), an
  infix parser written in Glon itself. It handles only a **subset** of v003's
  grammar (names, numbers, parentheses, unary minus, `+ - * / ^`), matches
  `infix.js` exactly on that subset (40 fixtures, 1,500 WASM differential
  checks), and is **not** a replacement for v003: it parses none of Chris's 17
  figures, and its limits under the 64K map are small (25-term chains, 15
  parenthesis levels, a 160-byte input cap). Its files are frozen by
  `FROZEN.sha256`. Its `s/span` is a byte-set scanner, which the standard
  `strings-ext.glon` calls `s/scan-set`; the standard `s/span` extracts a
  range.

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
- Compiled-S1 performance path: interpreted ~4.3 s → P10A ~2.0 s (dispatch
  removal 2.4×) → P10B ~0.43 s (register promotion ~5×) → P10C ~0.41 s
  (HOST inlining ~1.0×, negative) → P10D ~0.31 s (TOS cache 1.32×) → P10E
  ~0.29 s (TOS+NOS 1.08×) → P10F ~0.29 s (fall-through 1.05×, near-negative).
  Remaining ~2.9× R3 is spread across the operations themselves and the
  remaining `M[sp]`/`M[rp]` memory traffic — no single dominant lever; the
  compiled-S1 backend has reached diminishing returns on each isolated axis.
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

## 10a. AvP G5g execution-cost investigation (2026-11-14)

G5g measured the AvP renderer rather than changing it. The G5f wall regression
was traced to host C build layout, not to the wall or row algorithm: the same
wall stage executed 33,377,988 S1 operations/frame at both 103.7 ms and
136.6 ms depending on the host binary composition. G5f's row compiler was
confirmed (16.62M → 5.19M S1 operations/frame).

An isolated benchmark outside the game loader measured S1 primitives at about
1.6–3.1 ns each, Glon recursion at 22–33 µs/level, a 160-ray/10-wall Glon
workload at 112 ms median versus 0.00126 ms native C, and identical checksums.
A reversible stationary-view cache cut idle frame production from ~168 ms to
~7 ms, but input-to-visible update remained a full render. The loader frontier
was measured at 638 free cells with the game contributing no masm code.
All tests, including the new stationary-cache tests and frozen-S1 check,
passed. Full report: `experiments/glon-doom/G5G-INVESTIGATION.md`.

## 10b. AvP G5h prepared wall execution (2026-11-14)

G5h showed that the existing seven-primitive substrate can execute the wall
calculation far more efficiently when the repeated evaluator structure is
prepared with the existing `masm` lowerer. The experimental `--raw-wall` path
reduced wall S1 operations/frame from 33,377,988 to 4,555,404 (86.4%) and HOST
calls/frame from 6,014,444 to 846,833 (85.9%). Textured FPS improved from 6.2
to 19.5–20.3; simple from 6.7 to ~30.4.

The prepared worker adds ~170 loader cells and ~1,702 code cells, leaving 478
loader cells and 1,300 code cells. It uses only frozen S1 primitives and the
existing RAW/CALL macros. The complete default self-test passes through the raw
path. Full report: `experiments/glon-doom/G5H-INVESTIGATION.md`.

## 10c. AvP G5i Alien sprites and visual diagnosis (2026-11-14)

G5i replaced the geometric Alien with five original 64x128 transparent
pixel-art frames (idle, two walk frames, attack, side), rendered with alpha
blending, nearest sampling, distance shading and simulation-tick animation.
Glon still owns the Alien's position, state, door/LOS occlusion and projected
rectangle. Sprite cost is 0.10–0.11 ms when visible; raw-wall textured
performance remains about 21.8 FPS, with unchanged 4,555,404 wall S1 ops and
846,833 HOST calls per frame.

The requested Alien reference sheet was absent; the supplied environment
contact sheet has no Alien art, so all frames are original. A checker diagnostic
found no wall-strip gaps; the remaining blockiness is low internal resolution,
3x nearest scaling, repeated 64x64 tiles and coarse shading. Bilinear final
scaling is optional and does not replace the deferred G5j environmental review.
Full report: `experiments/glon-doom/G5I-INVESTIGATION.md`.

## 10d. AvP G5j wall tiling diagnosis (2026-11-14)

G5j traced the Tetris-like walls to non-tileable 64x64 G5d contact-sheet crops,
not primarily to ray count or resolution. Opposite-edge RGB differences were
160–414 for the old wall textures versus 0.4–7.7 for two new original seamless
textures. The UV checker showed no hidden strip gaps. New textures remove the
repeated hard seams; smooth shading removes some banding.

Configuration A (320x200, 160 rays) remains supported. 320 rays is blocked by
the loader cost of four hosted ray source blocks (478 free); 640x400 requires
parallel Glon projection/texrow work. Textured performance remains 19.6–21.7
FPS across new/smooth variants, above 15 FPS. Raw wall and Alien paths are
preserved. Full report: `experiments/glon-doom/G5J-INVESTIGATION.md`.

## 10e. AvP G5k flat surfaces, clutter and cover (2026-11-14)

G5k replaced the expensive floor/ceiling texrow pass with an optional cheap
depth gradient. Flat mode removed about 19 ms of floor work and raised raw-wall
textured performance from 20.6 to 33.2 FPS. Six compact Glon-defined industrial
props added shared projection/collision and depth-sorted sprites; the
flat+objects mode measured 27.5 FPS, spending roughly 6 ms on objects and
collision while retaining a net saving. The Alien can be hidden behind a crate
and emerge around it; player and Alien object collision are tested.

Object-vs-wall occlusion is screen/door-state based rather than per-pixel; this
limitation is documented. The object module is separate and does not affect
the default loader. Full report: `experiments/glon-doom/G5K-INVESTIGATION.md`.

## 10f. AvP G5l OpenGL renderer (2026-11-14)

G5l added an explicit SDL/OpenGL backend on the verified WSLg D3D12 AMD Radeon
device. Static level geometry/textures are uploaded once; Glon supplies camera,
door, object, Alien and light-phase state each frame. OpenGL rendered 60.0 FPS
at 320x200, 62.4 at 640x400 and 57.7 at 960x600 with objects, versus 27.5 FPS
for the G5k CPU reference. Ceiling illumination, a subtle Glon-controlled pulse,
depth occlusion, door occlusion, transparent Alien/object sprites and CPU
fallback were retained.

Static geometry is currently a renderer-side ABI copy; moving it into a compact
Glon scene block is the next architectural cleanup. Sound remains a proposed
event interface. Full report: `experiments/glon-doom/G5L-INVESTIGATION.md`.

## 10g. AvP G5m playable OpenGL and creature replacement (2026-11-14)

G5m connected the OpenGL scene to the normal keyboard/fixed-timestep loop, made
GL the default with CPU fallback and explicit `--renderer`, fixed GL texture row
orientation, and replaced the cartoon Alien with a 128x192 original
biomechanical six-frame creature set. Real WSLg key events verified W movement, one-shot E door opening, Right turn
and D strafe on both GL and CPU. GL with objects measured 66-67 FPS at 640x400,
57.7 at 960x600, versus CPU 27.5. The E double-trigger and turn-mask bugs are
fixed and covered by `--input-test`; the missing GL motion tracker is restored
by a shared Glon-state blip mapping and depth-disabled HUD overlay, covered by
`--tracker-test`. GL windows now use actual drawable-size viewport/projection
with a capped circular HUD; maximized/non-16:10 resize was verified. The current
creature still fails the final art gate; `ALIEN-ART-SPEC.md` records the
requirement and the need for separately produced artwork. The manual procedure and remaining
static-geometry duplication are documented. Full report:
`experiments/glon-doom/G5M-INVESTIGATION.md`.

## 10h. AvP G5n industrial corridor greybox (2026-11-14)

G5n replaced the flat GL corridor with real 3D architectural geometry:
recessed wall panels, structural ribs, service housings, pipes, ceiling light
fixtures, floor plates/grates/drains, framed bulkheads and 3D props. The
approved corridor reference was used only as art direction; all current
materials are explicitly temporary flat solids and the visual-quality gate
remains open. Final material assets are specified in `G5N-ASSETS.md`.

GL performance measured 31–46 FPS at 640x400 and 36.2 FPS at 960x600. Frozen
S1, the 64K map, controls, doors, collision, tracker/resize behavior and Alien
state are unchanged. Full report: `experiments/glon-doom/G5N-CORRIDOR-REPORT.md`.

## 10i. AvP G5o corridor artwork pipeline (2026-11-14)

The supplied prototype art pack was installed under `assets/corridor/` with a
generated runtime-array pipeline. Recessed-wall and far-bulkhead materials
materially improve the corridor; the floor material is stretched/low-contrast
and all three source images are non-seamless (edge deltas 45–471 RGB units).
Only three of fourteen materials are installed; the artwork gate remains open.
GL measured 37.6–56.4 FPS at 640×400 and 51.1 FPS at 960×600. Full report:
`experiments/glon-doom/G5O-ART-PIPELINE-REPORT.md`.

## 10j. AvP G5p corridor composition (2026-11-14)

G5p added recessed framed/grille ceiling fixtures, world-scaled deck plates with
edge drainage, and smaller wall-mounted props while preserving the centre lane
and all gameplay state. It is visually closer to the references but not final:
the floor/ceiling/prop materials remain prototype or placeholder. Performance
fell to about 32 FPS at 960×600 from G5o's ~51 FPS due to immediate-mode
geometry; batching remains future work. Full report:
`experiments/glon-doom/G5P-CORRIDOR-REPORT.md`.

## 10k. AvP G5p corrective investigation (2026-11-14)

Flat spawn/doorway surfaces were caused by missing material assignments, not
occlusion; existing textured wall/floor helpers now cover room and corridor
base surfaces, both door faces, and lower housings. Corridor CPU submission is
only ~0.5–0.9 ms; a display-list cache was rejected after falling below 1 FPS
on WSLg D3D12. Forced `glFinish` was removed from normal play (opt-in with
`GLON_FINISH=1`). Matched 960×600 performance is 51–62 FPS, approximately G5o;
remaining time is Glon tick and state extraction. Full report:
`experiments/glon-doom/G5P-CORRECTIVE-REPORT.md`.

## 10l. AvP G5q Alien reference sprites (2026-11-14)

Five transparent poses from the G5q reference sheet replace the procedural Alien
textures, with a common scale, foot baseline and foot pivot. Existing AI,
collision, state selection and timing are unchanged. Only idle, two walk phases,
attack and side were converted; full turn/hit/death sequences remain missing
artwork. The concept sheet is not a validated fixed-grid atlas. Full report:
`experiments/glon-doom/G5Q-ALIEN-REPORT.md`.

## 10m. AvP G5r pulse-rifle reference integration (2026-11-14)

The pulse-rifle/hand was manually masked from an opaque corridor reference into
a transparent 768×406 sprite. The baked `60` was removed and Glon now owns
`doom/ammo`, displayed live by OpenGL. This is a static weapon overlay; firing,
reload and the other three weapons are not implemented. Full report:
`experiments/glon-doom/G5R-WEAPON-REPORT.md`.

## 10n. AvP G5s directional Alien integration (2026-11-14)

Seven native-resolution directional Alien poses replace the five side-facing G5q
frames. Head-on approaches now use frontal walk artwork chosen from actual
movement and camera-relative direction; three-quarter, side, rear and attack
paths are wired without changing AI or collision. The G5r pulse rifle and live
ammunition display remain visible. Directional acceptance passes; close-up
resolution still fails because the source poses are only 188–268 px tall. Full
report: `experiments/glon-doom/G5S-ALIEN-REPORT.md`.

## 11. Detailed documents

- `README.md` — project intro.
- `DESIGN.md` — normative design principles (the Substrate Principle; the
  persistent interpreter/application model).
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
- `FIB-OPT-P10E-NOS.md`, `FIB-OPT-P10E-RESULTS.md` — P10E two-cell TOS+NOS cache.
- `FIB-OPT-P10F-FALLTHROUGH.md`, `FIB-OPT-P10F-RESULTS.md` — P10F sequential fall-through.
- `R3-FIB-COMPARISON.md` — local Rebol3 vs Glon P5 Fibonacci benchmark.
- `GLON-ARCHITECTURE-MEMORY.md` — architectural decisions, measurements and
  proposals (the memory map, capacities, vocabularies and experiments).
- `experiments/glon-latex/` — the deployed LaTeX vocabulary and `/latex/` page.
- `experiments/glon-pratt/GLON-PRATT-V004.md` — the Pratt parser experiment.
- `experiments/glon-iter/GLON-ITER-V001.md` — the iterator vocabulary
  experiment (`it/`, and an `lx/` lexer that matches v004's tokens).
- `docs/glon-shop-product-spec.md` — Glon Shop (browser/shop application target).
- `docs/distributed-glon-agents.md` — distributed/federated agent architecture.

## 10o. AvP G5t engineering room (2026-11-14)

One room — the spawn room — is now a distinct engineering compartment. The
renderer adds a front-facing turbine/generator with an orange rotor, yellow
guardrails, two coolant vessels with valves, service cabinets, a control bank,
recessed wall bays, ventilation grilles, overhead pipework/cable trays and
ceiling fixtures. Corridors, deck material, G5s Alien artwork/AI, G5r rifle and
ammo, tracker, doors, controls, Glon collision and frozen S1 are unchanged.
`--gl-rooms DIR W H [objects] [plain]` captures the room and reproduces the
plain G5s room from the same poses. See `experiments/glon-doom/G5T-ROOMS-REPORT.md`.

## 10p. AvP G5u Alien navigation and room accessibility (2026-11-14)

The G5t engineering room is the normal spawn room, not a separate location:
normal player start `(320,320)`, Alien `(1120,320)`, closed door. The original
normal camera faced the closed door and showed only side detail; G5u makes the
ordinary GL launch face `angle=240` so the room and door are both visible.
`doom/start-angle` remains 0 for tests, trace and scripted captures.

The reported barrel incident reproduced deterministically: with the player just
inside the room and the Alien behind the `(940,370)` barrel, x movement was
rejected and y movement became zero once aligned, leaving the Alien stationary
at `(968,352)` for hundreds of ticks. `doom.glon` now adds a compact bounded
sidestep that uses existing collision checks and resumes pursuit after clearing
the obstacle. Glon remains authoritative; no map, collider, AI state enum or frozen
S1 change was made. Full report: `experiments/glon-doom/G5U-NAVIGATION-REPORT.md`.

## 10q. AvP G5v engineering-room materials (2026-11-14)

The engineering room now uses the corridor's existing recessed-panel and
weathered-steel textures through the established texture path, with the room's
panel faces re-exposed additively to recover their dark albedo. Large machinery
uses cropped bulkhead steel; pipes use flat-lit metal with couplings and
hangers; rails carry varied worn paint and dark brackets. The turbine has a
metal bolt ring, stronger warm core/floor glow, and soot/oil stains. Cool
overhead rails and baked panel washes replace the previous uniform flat
lighting. No textures were added, no corridor pixels changed, and no gameplay,
frozen-S1 or VM semantics changed.

Matched 960x600 medians improved from G5u's 84.5/54.9 FPS to 91.2/60.1 FPS
(no objects/objects). Full report: `experiments/glon-doom/G5V-ROOM-MATERIALS-REPORT.md`.

## 10r. AvP G5w machinery textures (2026-11-14)

The engineering machinery now has actual curved-surface texture coverage. The
old `gl_cyl_*`, `gl_disc_*`, `gl_turbine_face` and pipe helpers disabled
texturing, set a flat colour and emitted no texture coordinates. G5w added
smooth cylindrical UV helpers for X/Y/Z cylinders, annular UVs for the turbine
housing, planar caps and valve faces, and static safe crops from the existing
wall/floor art. The turbine drum and ring, both coolant vessels, caps, valves
and overhead/wall pipe bodies are now genuinely textured. The A-3 corridor
remains pixel-identical; no frozen S1, gameplay or VM code changed.

Matched 960x600 medians were 91.1/60.4 FPS (no objects/objects), effectively
level with the verified G5v result. Full report:
`experiments/glon-doom/G5W-MACHINERY-TEXTURES-REPORT.md`.

## 10s. AvP G5x high-resolution blast door (2026-11-14)

The spawn-room door now uses native-resolution leaf crops from the supplied
concept sheet (450×785 and 410×785), replacing the embedded 64×64 texture. It
is an 8-unit-thick two-leaf slab in a deep casing with side pockets; leaf travel
is driven directly by Glon's `door_open`, so partial/opening/fully-open views
agree with the unchanged collision threshold. The leaf art is loaded once from
raw RGB assets; the initial embedded-array version was rejected because the
2.7 MB of static data shifted `.bss` and measurably slowed the benchmark.

Matched 960×600 medians were 82.1/53.4 FPS versus G5w's 81.9/54.0, with loader
and gameplay semantics unchanged. Full report:
`experiments/glon-doom/G5X-BLAST-DOOR-REPORT.md`.

## 10t. AvP G5x2 native-resolution blast-door leaves (2026-11-15)

The G5x leaves are replaced by the two supplied 1070×1470 industrial door
images. The runtime art keeps the 64×112 face aspect by trimming 230 pixels
from each outer edge, yielding two 840×1470 leaves with the inner seam edges
retained; no stretching, mirroring or upscaling is used. Texel density rises
from about 7.0 to 13.125 per world unit. Geometry, leaf travel, collision,
casing, pockets, locker housing, Alien navigation and frozen S1 are unchanged.
The worktree is the permanent `/mnt/d/repos/glon-worktrees/glon-avp-g5x2`.

Sampling remains the renderer's `GL_LINEAR` path, now with `GL_CLAMP_TO_EDGE`
so the seam edges cannot bleed into the opposite edge. Mipmapping was measured
and rejected: it raised object-mode tick time from about 8.9 to 11.5 ms and
dropped median FPS to roughly 55–59 on the WSLg D3D12 driver.

Randomized interleaved 180-frame A/B at 960×600: object mode 67.0 → 65.2 FPS
median, within the observed spread and about 2.7% lower; no-object mode
98.2 → 106.9 FPS in the same session but highly variable. Tick time was
effectively unchanged. The executable remains 6,121,456 bytes; loader/heap
remain 53,705/54,569 and 33,440/33,600. GPU texture memory rises by 7,178,000
bytes to 9,878,400. Full report:
`experiments/glon-doom/G5X2-DOOR-TEXTURE-REPORT.md`; before/after evidence is
under `experiments/glon-doom/evidence/g5x2/`.

## 10u. AvP G5y engineering-room cabinet and ceiling detail (2026-11-15)

The engineering-room cabinet fronts had a full-height near-black strip at 48%
of each south cabinet's width. It was an intentional double-door centre stile
with almost no metallic detail; it also ran through the centred vent of the
reused panel overlay, so it read as unfinished geometry. The cabinets now use a
single-door read with steel jambs, top/bottom rails, three hinges and an edge
pull clear of the vent. The east cabinets' panel overlay was buried inside the
cabinet body at `x+0.1`; it now sits in front at `x-0.1`, and the east pull was
moved to the latch edge. Cabinet positions and collision are unchanged.

The room ceiling now reuses the existing 64x64 `tex_ceiling_panel` as a
room-only 6x4 recessed panel layer at `z=127.35`, with 1.3-unit seams exposing
the original dark plane, a perimeter ledge, two ventilation grilles and a
subtle cool bounce. The base world ceiling, structural beams, pipes, trays and
light housings are unchanged; no floor texture was copied and no collision
changed.

The G5y worktree is the permanent `/mnt/d/repos/glon-worktrees/glon-avp-g5y`,
based on G5x2 `133d5b0`; implementation `5c0ef85`. Full `make test` passes,
including frozen S1 at `f90496c26dc45c7a387d8fc8639cd2781507d3d2`.

The final executable is 6,146,536 bytes (+25,080 from G5x2; +20,648 from the
capture-only diagnostic baseline). GL texture memory rises by exactly 16,384
bytes to 9,894,784 for one 64x64 RGBA ceiling texture. Loader/heap are
unchanged. Interleaved old/new medians: standard 960x600 object benchmark
52.7 -> 54.2 FPS; room-detail 960x600 209.4 -> 218.5 FPS; room-detail
1920x1080 122.8 -> 120.4 FPS (-1.9%, ranges 101.3-134.1 vs 104.5-133.2, so
within run-to-run spread). Full report:
`experiments/glon-doom/G5Y-ENGINEERING-DETAIL-REPORT.md`; evidence under
`experiments/glon-doom/evidence/g5y/`.

## 10v. AvP G5z GLON CORPORATION industrial equipment (2026-11-15)

The corridor storage props are six fixed scenery records in
`experiments/glon-doom/objects.glon`: two red drums, two olive-green crates, a
grey cabinet and a low green container. They are not movable cargo. Object-mode
collision uses their original rectangular extents through `doom/check-objects`;
`doom/objects-on` defaults off and is enabled only for object mode. G5z changes
only the OpenGL renderer.

The drums and crates now use textured painted steel, reinforced corners,
recessed lids/panels, bands, handles, fasteners, rust runs and stencilled GLON
markings. The engineering control bank replaces flat coloured rectangles with
recessed screened displays, bezels, screws, instrument graphics, a retained
six-pair red/green status strip and a GLON manufacturer plate. All labels and
instrument panels come from one generated 512x256 RGBA atlas (524,288 bytes);
the displays are decorative and do not track game state.

The G5z worktree is the permanent `/mnt/d/repos/glon-worktrees/glon-avp-g5z`,
based on G5y `94699eb`; implementation `ff2ea47`. Full `make test` passes,
including frozen S1 at `f90496c26dc45c7a387d8fc8639cd2781507d3d2`.

The final executable is 6,172,112 bytes (+25,576 from G5y; +21,120 from the
diagnostic baseline). GL texture memory rises to 10,419,072 bytes. Matching
same-session medians: standard 960x600 object benchmark 42.1 -> 45.8 FPS;
control-bank pose 116.3 -> 115.8 FPS; container-only pose 115.6 -> 102.2 FPS at
960x600 and 84.5 -> 73.4 FPS at 1920x1080, a localized fill-rate cost from the
new textured surfaces (still above 65 FPS). Full report:
`experiments/glon-doom/G5Z-GLON-CORPORATION-REPORT.md`; matched evidence under
`experiments/glon-doom/evidence/g5z/`.


## 10w. AvP G6a spaceship ambient audio (2026-11-15)

The user-supplied `g5y.zip` pack contains two continuous ambient WAVs:
`ship_ambience_loop.wav` (24 s) and `ventilation_loop.wav` (12 s), both mono
44.1 kHz PCM s16le. G6a copies those files unchanged into
`experiments/glon-doom/assets/audio/` and uses SDL2 audio, already linked by
the host, to play them as two mixed layers. No new audio library was added and
the remaining effects in the pack are intentionally not integrated.

Each source is converted to 44.1 kHz S16 and given a precomputed loop
crossfade (1.0 s ship, 0.5 s ventilation) so the loop wrap is continuous in
the sample stream; the resulting loop lengths are 23.0 s and 11.5 s. The SDL
callback mixes both layers at 44.1 kHz stereo with a 1024-frame buffer. Default
ambient volume is 60%; `M` toggles mute, `[`/`]` adjust by 5, and
`--ambient-volume 0..100` / `--no-audio` set the launch defaults. Audio starts
only in the interactive GL/CPU loops, after session setup, and is closed before
`SDL_Quit`.

The G6a worktree is the permanent `/mnt/d/repos/glon-worktrees/glon-avp-g6a`,
based on G5z `6168609`; implementation `69614a6`. Full `make test` passes,
including the new audio asset check and `--audio-test`, and frozen S1 at
`f90496c26dc45c7a387d8fc8639cd2781507d3d2`.

The final executable is 6,181,624 bytes (+9,512 from G5z). The two WAV assets
occupy 3,175,288 bytes on disk; decoded loop memory is 3,042,900 bytes. The
mixer costs about 0.003-0.005 ms per 1024-frame block (23.22 ms budget). Ten
360-frame interactive smoke runs gave a median of 82.1 FPS with audio versus
86.1 FPS without (-4.6%, overlapping 58.1-96.0 vs 66.8-93.9 ranges); no render
path changed. The audio device test opened the real PulseAudio device and shut
down cleanly, and an unavailable driver fell back gracefully. Playback was
verified programmatically, not by listening. Full report:
`experiments/glon-doom/G6A-AMBIENT-AUDIO-REPORT.md`; evidence under
`experiments/glon-doom/evidence/g6a/`.


## 10x. G6a ambience replaced with Freesound 873687 (2026-11-15)

The initial G6a synthetic ship/ventilation ambience was replaced after the
user confirmed it was still the audible bed. The active asset is now a
converted version of Freesound 873687, "Alien Spaceship Interior Ambience 04
Sci-Fi Engine Hum & Electronics" by Tommaso Motteran, licensed CC BY 4.0
(https://creativecommons.org/licenses/by/4.0/). The source is 24-bit stereo
44.1 kHz, 34.000068 s; it was converted to 16-bit mono 44.1 kHz and is looped
by the existing runtime crossfade engine (1.0 s), giving a 33.000068 s played
loop. The old synthetic ship and ventilation files were deleted, so no old
hiss is layered underneath. Credit and modification details are in
`experiments/glon-doom/assets/audio/SOURCE.txt`, the README and the report.

Replacement implementation `42230b3`. The mixer, volume/mute controls, device
fallback and gameplay are unchanged. The runtime asset is 2,998,920 bytes;
decoded loop memory is 1,455,303 * 2 = 2,910,606 bytes. The executable is
6,181,632 bytes. Six 360-frame interactive smoke medians were 110.8 FPS with
audio versus 124.0 FPS without (-10.6%, overlapping 102.0-134.8 vs
108.5-146.5 ranges); mixer cost remains about 0.004 ms per 23.22 ms block. The
real PulseAudio device opened and closed cleanly; unavailable and dummy drivers
also behaved. Full `make test` passes, frozen S1 unchanged at
`f90496c26dc45c7a387d8fc8639cd2781507d3d2`. Playback was again verified
programmatically, not by listening. Full report:
`experiments/glon-doom/G6A-AMBIENT-AUDIO-REPORT.md`; evidence under
`experiments/glon-doom/evidence/g6a/`.


## 10y. AvP G6b event-driven sound effects (2026-11-15)

G6a's Freesound 873687 ambient loop is retained unchanged. G6b extends the
single SDL2 mixer with six event clips, an 8-voice pool, exclusive door/Alien
voices with a 125 ms replacement fade, master mute/volume and graceful
no-device handling. Sources and licences are recorded in
`experiments/glon-doom/assets/audio/sfx/SOURCE.txt`:

- 799198 kentspublicdomain, lab door open/close, CC0; split into two 2.2 s
  sections.
- 737275 Mastersoundboy2005, alien roar/scream, CC0.
- 850829 Heretic4522, sci-fi LMG shot, CC0; 0.65 s single-shot excerpt.
- 620830 F.M.Audio, low metal thud, CC BY 4.0; 1.2 s excerpt.
- 530486 Rickplayer, metal impact, CC0; 0.9 s excerpt.

All are now 16-bit mono 44.1 kHz WAVs. Door sounds start on actual
`door_open`/`door_target` motion transitions and reverse without stacking.
Alien shrieks use detection/attack transitions, a 2.2 s cooldown and distance
attenuation. Wall/cargo collisions use input plus resolved-position blocking;
cargo is classified with the existing object extents, with a 350 ms cooldown
and cargo priority to avoid duplicate sounds. The G5r weapon has no gameplay
firing, so `F`/left mouse is an explicitly documented presentation-only rifle
preview; no ammo, damage, AI, collision or Glon semantics change.

The G6b worktree is the permanent `/mnt/d/repos/glon-worktrees/glon-avp-g6b`,
based on G6a `f816ab5`; implementation `ea2b670`. Full `make test` passes,
including `check_g6b_audio.py`, `--audio-test`, `--sfx-test`, the new input
check and frozen S1 at `f90496c26dc45c7a387d8fc8639cd2781507d3d2`.

The final executable is 6,190,464 bytes (+8,832 from G6a). Assets are
3,821,200 bytes on disk; runtime decoded audio is 3,732,340 bytes (ambient
played-loop 2,910,606 + SFX 821,734). Six 360-frame graphical smoke runs gave
a median 172.1 FPS with audio versus 180.9 without (-4.9%, overlapping
159.3-178.8 vs 169.1-187.1 ranges); mixer cost with ambient plus 8 voices was
about 0.007-0.009 ms per 23.22 ms block. PulseAudio played all six effects and
the unavailable-driver path continued gracefully. Playback was verified
programmatically, not by listening. Full report:
`experiments/glon-doom/G6B-SOUND-EFFECTS-REPORT.md`; evidence under
`experiments/glon-doom/evidence/g6b/`.

## 10z. AvP G6b.1 interactive sound fixes (2026-11-15)

G6b automated tests passed but the user playtest found a ~1 s rifle delay and
missing door-opening/wall sounds. Diagnosis found three concrete defects:

- `SFX_DOOR_OPEN` was enum value zero and the dispatch used `if (ev)`, so the
  opening request was discarded. The enum now has an explicit `SFX_NONE=0`
  sentinel; opening and closing both reach the mixer.
- Wall/cargo detection required the entire position to be unchanged. The Glon
  movement resolver now reports a per-tick `doom/player-hit` flag when either
  axis is blocked, so diagonal/one-axis impacts are detected. The flag is
  reporting-only; movement, collision extents and AI are unchanged.
- Cargo and Alien clips had 79/64 ms of leading silence and low effect gains.
  They were trimmed to about 14 ms onset; door/Alien/rifle/wall/cargo gains
  were rebalanced upward.

The software input→request→callback path was already on the frame of input and
0.1–19 ms in the trace; the user-observed ~1 s was therefore downstream. G6b.1
reduces the SDL callback buffer from 1024 to 256 frames and requests
`PULSE_LATENCY_MSEC=25` when unset. This is a measured software-path change;
speaker/server latency was not captured reliably and requires a new user
playtest.

Interrupted/reversed door motion still replaces the exclusive door voice with
a 125 ms fade. The rifle remains a presentation-only F/LMB preview because G5r
has no actual firing mechanic.

The G6b.1 worktree is the permanent
`/mnt/d/repos/glon-worktrees/glon-avp-g6b1`, based on G6b `b37f66e`;
implementation `88bdc6f`. Full `make test` passes, including the new
`--audio-e2e` test that exercises F input, E activation, tick-into-wall and
tick-into-cargo with nonzero PCM, plus frozen S1 at
`f90496c26dc45c7a387d8fc8639cd2781507d3d2`.

The executable is 6,198,952 bytes (+8,488 from G6b). Audio assets are
3,811,498 bytes on disk; runtime decoded audio is 3,722,638 bytes. Six
360-frame graphical smoke medians were 132.0 FPS with audio and 139.2 without
(-5.2%, overlapping 111.3–136.1 vs 132.9–161.1). The ambient-plus-8-voice mixer
costs about 0.002 ms per 256-frame block (5.80 ms budget). A graphical
`--sfx-smoke` run with real F/E/W events produced F, door-open, door-close,
wall and cargo requests and callbacks. Playback was not subjectively verified.
Full report: `experiments/glon-doom/G6B1-SOUND-FIX-REPORT.md`; evidence under
`experiments/glon-doom/evidence/g6b1/`.

## 10aa. AvP G6c portal animation and RDPSink latency diagnosis (2026-11-15)

The engineering-room turbine's flat orange inner face is now a procedural
liquid-energy surface drawn inside `gl_turbine_face`: a warm plasma bed,
orange/blue-white ripples, rotating shimmer bands and sparse blue-white/orange
electrical arcs. All geometry stays inside radius 0.52 of the 54-unit rotor
face; the outer drum, spokes, hub, railings and room are unchanged. Animation
uses `sim_tick` time. `--portal-test` verifies 7,018 changed pixels in a
contained 960x600 mask; portal-only 180-frame runs measured 171–216 FPS at
960x600 and 109–134 FPS at 1920x1080.

Audio was re-diagnosed. Source onsets are immediate (rifle 0.023 ms, door open
8.957 ms, door close 1.043 ms after re-cut, wall 0.317 ms, Alien/cargo 14.1
ms), and the game request-to-callback path is under 9 ms. The remaining delay
is the WSLg PulseAudio sink: `pa_sinks` reports **RDPSink** with 1209.40 ms
reported latency (configured 6.26 ms), and the stream probe reports 20.32 ms
buffer + 628.42 ms sink = 648.74 ms. This matches the user's observed ~1 s and
cannot be removed from the application. No arbitrary offset was added. The
door close clip was re-cut from source 3.15–4.35 s to remove 1.7 s of quiet
pre-slam mechanism, leaving the final impact about 0.75 s into the clip.

The G6c worktree is `/mnt/d/repos/glon-worktrees/glon-avp-g6c`, based on G6b.1
`a9e0a05`. Full `make test`, `--audio-e2e`, `--portal-test`, graphical
`--portal-smoke`, PulseAudio and no-device paths pass; frozen S1 unchanged at
`f90496c26dc45c7a387d8fc8639cd2781507d3d2`. Playback was not subjectively
judged. Full report: `experiments/glon-doom/G6C-PORTAL-AUDIO-REPORT.md`;
evidence under `experiments/glon-doom/evidence/g6c/`.

## 10ab. AvP G6d native Windows x86-64 port (2026-11-15)

The G6c game host was cross-compiled to Windows x86-64 with a standalone Zig
0.13.0 toolchain and SDL2 2.30.9 MinGW libraries; no system-wide SDK install
was used. The game source, Glon runtime/programs, assets, portal, rifle
presentation and audio-event behaviour are unchanged. The only new platform
source is `platform_win.h`: Windows `clock_gettime`, `_putenv_s`/`setenv`,
header macro cleanup, conditional POSIX includes, Windows root path, a
`tmpfile`/`_dup` capture backend and 64-bit timestamp variables (MinGW `long`
is 32-bit). A portable distribution is under `dist/windows/` with
`doom.exe`, `SDL2.dll`, runtime assets, licences, attribution and
`run-g6d.bat`.

Toolchain: Zig 0.13.0 under `/home/graham/.cache/glon-g6d`, SDL2 2.30.9
MinGW package under `.deps/` (ignored). The Windows PE is 6,546,432 bytes;
the distribution is 22,075,065 bytes. The Linux build remains 6,203,168
bytes and full `make test` passes, frozen S1 unchanged.

The PE was executed as a native Windows process through WSL Windows interop
(not Wine). Windows OpenGL/AMD and **WASAPI** initialized; native self-test,
input, audio, SFX, E2E, portal test, portal shots and graphical portal smoke
passed. WASAPI reported a 10 ms default period, 3 ms minimum, 1056-frame
buffer (~22 ms at 48 kHz) and 0 ms stream latency; request-to-callback traces
were 0.66-9.29 ms. This removes the WSLg RDPSink path for the experiment;
physical Windows input/acoustic playtest is still unverified. Full report:
`experiments/glon-doom/G6D-WINDOWS-PORT-REPORT.md`; evidence under
`experiments/glon-doom/evidence/g6d-win/`.

## 10ac. AvP G6d A-3 side-corridor entrance fix (2026-11-15)

The A-3 side entrance at the corridor's north turn (`x=1152..1280`,
`y=384`) was visually blocked by a floating south-wall decoration panel.
`gl_corridor_static()` drew its south-wall backing boxes, service quads and
`GT_ART_PANEL` vent quads from `x=780..1260` at `y≈374..383`, without stopping
at the side opening. Panels at `x=1100` and `x=1180` therefore crossed the
opening while the Glon wall/collision remained open; the player walked through
non-collidable decoration. The panel was introduced by G5p (`a3886fc`);
G5o greybox (`98e96ad`) had no such panel. Later material work changed its
appearance, not its extent.

Fix `7121aab` clips/skips only the south-wall decorations (backing, trims,
ribs, service quad, warm strip, pipe and accent boxes) at the side opening;
north-wall panels and all other geometry are unchanged. A new
`--entrance-test` checks the south panel x-spans stay at/left of `x=1152` and
that `(1200,384)` and `(1200,500)` are traversable; it is part of `make test`.
Linux full tests pass, frozen S1 unchanged. The Windows distribution was
rebuilt; native Windows self-test, entrance-test, WASAPI audio-device and
sfx-device tests pass. Before/after evidence:
`experiments/glon-doom/evidence/g6d-entrance/`; report:
`experiments/glon-doom/G6D-A3-ENTRANCE-FIX-REPORT.md`.
