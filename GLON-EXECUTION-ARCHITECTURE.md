# Glon execution architecture: where the complexity lives

2026-11-10, branch `glon-shop-g1a`. Investigation only: nothing in this report
is implemented. It compares Glon's frozen seven-primitive S1 machine with a
conventional minimal Forth-style machine, using the repository's code and
measurements made with the tools in
[`experiments/glon-execution-architecture/`](experiments/glon-execution-architecture/).

## Objective

**The objective is a reliable execution language for LLM-generated software,
not preserving seven primitives and not optimising syntax for human authors.**

The seven-primitive S1 substrate, Rebol-style evaluation and the current
memory map are design choices to be judged by that objective. The questions
are therefore:

- Can generated code be run with predictable semantics and bounded resources?
- Can failures be detected before they corrupt the machine or a session?
- Can the model of execution be explained, inspected and tested?
- Which complexity is essential to those guarantees, and which is historical?

Human-facing syntax is secondary. A primitive is not valuable because it is
minimal; it is valuable if it makes generated software more reliable or cheaper
to execute without weakening the checks. Conversely, a primitive that only
preserves a smaller instruction set at the cost of an uninspectable runtime is
a liability.

**Labels.** [M] measured for this report. [R] an earlier repository measurement,
cited. [D] derived arithmetically from measured counts. [E] an estimate.

## How the measurements were made

A measurement copy of the frozen machine (`measure/s1_arch.c`) counts every
executed S1 instruction per code cell and records which derived macro emitted
each cell. The runtime is compiled with GCC `-finstrument-functions`, so every
emitted cell is attributed to the C emitter that produced it. The frozen
`s1.c` is not modified.

The driver loads `jupyter/prelude.glon` and `demo/shop/strings.glon` first,
then resets the counters, then runs the workload. Workloads that need the
string extension load `demo/shop/strings-ext.glon` as part of the workload.
The exact commands are in the experiment README; recorded output is in
`results.txt`.

| Workload | What runs |
|---|---|
| `+ 1 2` | one expression |
| `fib 18` | naive recursion: `fib: func [n] [ either < n 2 [ n ] [ + fib - n 1 fib - n 2 ] ]` |
| DEFLATE unit tests | 44 tests |
| LaTeX v001 tests | the v001 suite |
| `mem/` tests | 66 tests |
| `binary-strings` | GC-heavy binary-string preservation test |

All ran correctly under instrumentation unless noted.

## The answer

The evidence supports **C, with a substantial but bounded B component**.
**A** holds for the defect list, not for the architecture.

- **C: the memory and evaluation architecture dominates total cost.**
  Glon re-interprets source blocks on every evaluation:
  - it looks every word up dynamically;
  - it builds a 64-cell context per activation on the return stack;
  - it collects garbage with a conservative return-stack scan.

  A Glon `fib` call costs **3,697 S1 instructions** [M]. The same naive
  recursion written Forth-style in masm on the *same* seven primitives costs
  **40** per activation [M]. The gap is about **92×** and is not caused by the
  number of primitives.
- **B: the seven-primitive set is a real constant factor, not the main one.**
  There is no CALL, EXIT, branch, `>R` or `R>` primitive, so each is a macro
  of 5–21 cells.
  - **Code:** these macros are **38.0%** of the runtime's emitted code
    (4,886 of 12,861 cells) [M]. Replacing them with primitive encodings would
    remove about 4,323 cells, leaving about **8,538** [D].
  - **Execution:** the same macros account for **23.3–36.2%** of executed
    instructions across the workloads [M]. That is an upper bound on the
    saving: a primitive still executes once, so replacing them would not
    remove the whole share. Overall the B arm is a factor of at most about
    1.3–1.6×, not 2×. On compiled masm fib the effect is larger, about 4×,
    because that code is almost all call/return/stack macros [D].
  - **Safety:** SP is an ordinary memory cell and `push` is unchecked, so only
    a change to S1 itself can stop masm writing into code between evaluation
    steps (K1).
- **A: most defects found so far were implementation defects** (K2–K7, all
  repaired without touching S1). That does not make the primitives
  economical, but it means the current machine can be made reliable without a
  new primitive set.

## 1. Calls and returns: why CALL+EXIT is 38 cells

S1 has no call instruction. IP and RP are cells `M[0]` and `M[2]`, so the
frozen assembler (`s1.c`) builds calls from the seven primitives:

| Form | Expansion in S1 | Cells | Instructions executed |
|---|---|---|---|
| `>R` | `LIT REG_RP @ LIT 1 HOST SUB LIT REG_RP ! LIT REG_RP @ !` | 14 | 9 |
| CALL | `LIT cont` `>R` `LIT callee LIT REG_IP !` | 21 | 13 |
| EXIT | `LIT REG_RP @ @` (R@) `LIT REG_RP @ LIT 1 ADD LIT REG_RP !` `LIT REG_IP !` | 17 | 11 |
| BRANCH | `LIT target LIT REG_IP !` | 5 | 3 |

So CALL+EXIT is 38 cells and **24 instructions** [M; earlier repository
measurement agrees: `experiments/glon-masm`].

**In a conventional threaded Forth:**
- a call is one cell: the callee's address in the thread;
- EXIT is one cell;
- each is one primitive dispatch (`DOCOL` pushes IP; `EXIT` pops it);
- a subroutine-threaded Forth uses one native call and one return.

**Cost to the runtime [M].** The derived macros take 4,886 of the 12,861 cells
of emitted runtime code:

| Macro | Cells | Expansions |
|---|---:|---:|
| CALL | 1,638 | 78 |
| EXIT | 1,275 | 75 |
| BRANCH | 575 | 115 |
| `>R` | 994 | 71 |
| `R>` | 392 | 28 |
| `R@` | 12 | 3 |
| **total** | **4,886** | |

At two cells for an address-carrying CALL or BRANCH and one cell for
EXIT/`>R`/`R>`/`R@`, the same sites would take 563 cells, so the runtime would
fall to about **8,538 cells**, a 33.6% reduction [D]. This is a code-size
bound, not a measurement of a built machine.

**The masm comparison is algorithmic and equivalent.** `fibm` is the same
naive recursion as the Glon definition above. It works on untagged integers
internally and tags only at the Glon boundary, which is the natural Forth-style
implementation. It costs **40.0 instructions per activation** [M], excluding
527 instructions of top-level Glon/masm invocation and outer-wrapper overhead.
The Glon definition costs 3,697 per activation [M]. This is the measured basis
for the 92× gap.

**What a primitive extension would do to that comparison [D].** Of the 40
instructions per activation, about 33 are macro expansions (CALL including its
internal `>R`, EXIT, explicit `>R`, `R>`); about 3 are primitive dispatches if
those forms became one-cell primitives. The estimate is therefore about
`40 − 33 + 3 = 10` instructions per activation. This is derived, not measured;
the B-arm machine has not been built.

## 2. Stack management: why K1 can overwrite code

**Why it can.**
- S1's `push` is `M[--SP] = v`: no bound.
- SP is a writable memory cell; masm can store to it.
- The data stack grows down towards emitted code in the same address space.
- So a masm word that pushes 400 cells overwrites code, and nothing above S1
  can stop it.

**What the runtime does today.**
- **Data-stack guard (K2):** it checks SP at every evaluation step, costs
  about 3% of instructions [R], and protects all Glon evaluation.
- **masm words:** these run raw S1 instructions between steps and cannot be
  checked from the runtime.

**How Forth systems do it:**
- **Classic Forths** do not check each push; they check depth in the outer
  interpreter (`?STACK`) between words. That is Glon's guard, with the same
  blind spot inside a primitive or code word.
- **A Forth VM written in C** checks bounds in `push`/`pop` at the cost of one
  compare.
- **Hardware stack machines** often use separate stack memories.

So the difference is not "Forth vs Glon". It is where the stacks live and
whether the machine checks them. A checked `push` in S1 is the Forth-VM
answer and needs a frozen-file change. K1 is now the only strict expected
failure in the machine-integrity suite.

## 3. Memory access and the object model

The primitives are not the problem here: S1's `@` and `!` are Forth's `@` and
`!`. The complexity comes from the object model and the evaluator above them.

- **Managed objects.** A string is a tagged, length-prefixed heap object with
  one byte per cell; S1 has no byte addressing. Checks and byte operations are
  implemented in Glon or masm layers.
- **The call path.** Every access from Glon goes through lookup, dispatch and
  argument reduction before the primitive runs.
- **Safety checks.** Range, type and liveness checks are done in software
  because generated code is not trusted.

A Forth `C@` assumes trusted code and a flat address; Glon deliberately does
not. This is **C**. The one-byte-per-cell representation is a chosen trade-off
in the object model, not a consequence of the seven primitives.

**Measurement note.** The earlier draft listed isolated costs for `s/at`,
`mem/byte` and a raw masm fetch. Those figures came from a different session
shape and were not reproducible with the committed workload method here, so
they are not used as evidence in this report. The conclusions above rest on
the aggregate workload measurements, not on those isolated numbers.

## 4. Evaluation: what the evaluator adds

**A Glon function call** (`f 1` with `f: func [x] [ x ]`) costs about 1,464
instructions [R]. `invoke_closure` alone is 684. It allocates a 64-cell context
on the return stack, binds parameters, runs the escape-law checks (`esc_results`
76) and frees the context on return.

**Memory per activation** [R, §6 of the architecture memory]:
- about **97 return-stack cells**, against 1 cell (the return address) for a
  Forth word;
- hence the measured limit of about 84 nested calls.

**Where `fib 18` spends its instructions** [M]:

| Share | What |
|---|---|
| 29.1% | name lookup |
| 26.5% | dispatch |
| 20.7% | closure call/return |
| 15.6% | natives |
| 3.2% | stack guards |
| 3.0% | escape-law checks |

Runtime-state memory-variable traffic (`LIT RV_* @`/`!`) is **29.7%** of the
same run [M]. Across all six workloads it is **23.2–41.7%** [M].

**Where the gap is.** fib costs about 92× more through the evaluator than as
compiled masm on the same machine: 3,697 against 40 per activation [M]. A Rebol
interpreter has the same shape (DO dialect, words bound in contexts, blocks
evaluated at run time). Glon has kept Rebol's evaluation model; a Forth
compiles once, then runs threaded code with no lookup.

Earlier work found the same: compiling the S1 stream to C, which removes all
S1 interpretation, gained only **2.4×** [R, `FIB-OPT-P10A-RESULTS.md`], because
the S1 stream itself is the cost.

## 5. Garbage collection: what comes from managed objects

**Size.** The collector is 3,535 of the runtime's 12,861 cells (27.5%) [M];
the allocator adds 353 (2.7%).

**Share of execution [M]:**

| Workload | GC | Conservative return-stack scan |
|---|---:|---:|
| `binary-strings` | 93.0% | 34.0% |
| `mem/` tests | 68.2% | 43.7% |
| LaTeX | 61.6% | 41.9% |
| DEFLATE | 25.8% | 16.9% |
| `fib 18` | 0.0% | 0.0% |
| `+ 1 2` | 0.0% | 0.0% |

The scan column is `mark_closure_inline` plus `scan_closures`.

**Why the scan dominates.** The largest single GC routine is
`mark_closure_inline`. It is conservative, and since the 2026-11-09 repair each
candidate walks object sizes from the heap base, O(objects). It is
conservative because:
- contexts and frames live on the return stack;
- integers are tagged ×16, so an integer can equal an object address;
- there is no stack map.

None of that follows from having seven primitives. It follows from the object
model (immutable managed strings and blocks, closures with captured contexts)
and from placing activations on the return stack. This is **C**. A Forth with
ALLOT and no GC has none of it, and none of the safety either.

## 6. Code density, sizes, execution cost and memory

**Sizes [M]:**

| Item | Glon (S1) |
|---|---|
| machine (`s1.c` + `s1.h`) | 287 lines of C |
| runtime generator (`r0_s1_runtime.c`, `r0_s1.h`, `m1_layout.h`) | 4,974 lines of C |
| emitted runtime code | 12,861 cells |
| masm library code with prelude + strings | 720 cells |
| collector + allocator | 3,888 cells |

**Runtime code by emitter category [M].** This is a partition: each runtime
cell belongs to exactly one category, and the categories sum to 12,861.

| Category | Cells | Share |
|---|---:|---:|
| evaluator: dispatch | 1,903 | 14.8% |
| evaluator: name lookup | 1,065 | 8.3% |
| evaluator: closure call/return | 1,380 | 10.7% |
| evaluator: closure construction | 558 | 4.3% |
| evaluator: set-word | 627 | 4.9% |
| evaluator: masm call | 252 | 2.0% |
| **evaluator subtotal** | **5,785** | **45.0%** |
| natives | 1,106 | 8.6% |
| GC collector | 3,535 | 27.5% |
| allocator | 353 | 2.7% |
| errors (SIN!) | 1,016 | 7.9% |
| escape-law checks | 772 | 6.0% |
| stack guards | 294 | 2.3% |
| **total** | **12,861** | **100.0%** |

**The derived macros are a subset, not a category.** The 4,886 macro cells are
spread through the rows above. Adding the macro row to the table would
double-count. This correction replaces the draft's overlapping size table.

**Executed instructions by workload [M]:**

| Workload | Executed | In macros | State-variable traffic | GC |
|---|---:|---:|---:|---:|
| `+ 1 2` | 552 | 33.9% | 23.2% | 0.0% |
| `fib 18` | 30,914,744 | 28.1% | 29.7% | 0.0% |
| DEFLATE units | 77,480,081 | 23.3% | 37.5% | 25.8% |
| LaTeX v001 | 40,523,226 | 24.0% | 41.7% | 61.6% |
| `mem/` tests | 3,265,540,942 | 25.0% | 41.1% | 68.2% |
| `binary-strings` | 174,408,246 | 36.2% | 34.7% | 93.0% |

The "in macros" column is an upper bound on the instruction saving from
primitive control forms. The state-variable column counts executed
`LIT addr @`/`!` pairs in the runtime-state range and is a measure of the
cost of keeping evaluator state in memory rather than on the stack. It is
partly **B** (missing stack shuffles) and partly **C** (the evaluator has a lot
of state).

**Glon memory per abstraction [R]:**

| Item | Cells |
|---|---:|
| activation | ~97 (return stack) |
| context | 64 |
| closure | 32 |
| string | 1 per byte + header |
| managed heap | 7,232 |
| loader heap | 14,800, permanent |
| code space for all masm libraries | about 3,000 |

**Forth [E]:** an activation is 1 cell; data is ALLOTted bytes; there is no
heap unless built.

## 7. Compatibility: what could move to another backend

An earlier repository census [R] found that application code is almost
entirely masm-free Glon, including DEFLATE, the LaTeX work and DoorDash's
application files. masm is concentrated in libraries:

- the core strings and block accessors;
- Kākā's and DoorDash's support libraries;
- `mem/`;
- the DOM/G1 output buffers;
- the M1 scheduler and the debugger fixture, which switch stacks by writing
  `REG_SP`, `REG_RP` and `REG_IP`.

The first group (application code) would run unchanged on any backend that
implements Glon's evaluator semantics and the core library words. The masm
libraries depend on S1 itself: expansion sizes, scratch cells, tag values
(`T_*`) and heap layout (`GC_KIND_*`). They would need re-implementation as
natives, or a masm compatibility layer, on another backend.

The census counts are from the earlier investigation and were not re-counted
in this finalisation pass; they are retained as [R].

## The trace: `+ 1 2` in Glon and `1 2 +` in Forth

**Glon [M]**, 552 S1 instructions, grouped by runtime routine:

```
routine        kind     instrs   SP     RP      what happens
main           CALL       13   16384  24576   enter the block evaluator
block_eval                 6
block_eval     CALL       13                  evaluate one expression
ds_guard                  10                  data-stack floor check
subexpr                   25                  fetch the element `+`, tag = WORD
subexpr        CALL       13
lookup                   109   16383  24573   hash lookup of `+` in the global context
lookup         EXIT       11
subexpr        BRANCH...  14                  tag of the value: NATIVE
native                    48                  native dispatch, arity 2
native         >R          9  (x2)            save evaluator state
ds_guard                  10                  argument 1: `1`
subexpr                   34                  INTEGER, self-evaluating
subexpr        EXIT       11
native         CALL       13
reduce                     9                  reduce the result set to one value
reduce         EXIT       11
  ... the same for argument 2 (`2`): 10 + 34 + 11 + 13 + 9 + 11 ...
native                    12                  HOST ADD (the actual work: 1 instruction)
native         R>          9
native        ...         25
native         EXIT       11   16382  24574   result 3
block_eval     EXIT       17
main                        1                  HALT
```

**Where those 552 go [M]:**

| Category | Instructions |
|---|---:|
| call/return/branch/RS macros | 187 |
| evaluator state-variable traffic | 128 |
| name lookup | 120 |
| stack guards | 30 |
| the addition itself | 1 `HOST ADD` |
| remainder (dispatch, natives, setup) | 86 |

**Forth**, compiled `: t 1 2 + ;` in an indirect-threaded VM, 4 primitive
dispatches [E, by construction]:

```
thread:  LIT 1  LIT 2  +  EXIT
LIT      push 1                ( 1 )
LIT      push 2                ( 1 2 )
+        pop 2 pop 1 push 3    ( 3 )
EXIT     IP <- R>
```

On S1 itself, as masm, that is `LIT 1 LIT 2 HOST ADD` = **3 instructions**
[D by construction].

**The fair comparison is with Forth's text interpreter.** Interpreting
`1 2 +` typed at the console does FIND `+` in the dictionary, NUMBER for `1`
and `2`, and EXECUTE. That is the same kind of work as Glon's lookup and
dispatch. Glon pays it every time an expression runs; Forth pays it once, at
compile time. Comparing Glon evaluation with compiled Forth threaded code is
useful for the size of the gap, but it is not an equivalent-operation
comparison.

## Where the complexity resides

| Area | What the evidence shows |
|---|---|
| **Primitive set** | Seven primitives force 38.0% of runtime code into CALL/EXIT/BRANCH/RS macros and 23.3–36.2% of executed instructions. They also make a checked stack impossible without an S1 change (K1). This is a real constant factor and a safety gap, but not the dominant cost. |
| **Evaluator** | 45.0% of runtime code. Dynamic lookup is 21.7–29.1% of execution in non-GC-heavy work; closure call/return is 20.7% on fib. Re-interpreting source blocks at every evaluation is the largest single architectural choice. |
| **Memory management** | GC is 27.5% of runtime code and 25.8–93.0% of execution in GC-bearing workloads. The conservative return-stack scan is 16.9–43.7%. The cause is activations on the return stack plus tagged integers, not the primitive count. |
| **Assembler** | masm is a validated but unrestricted trapdoor. Its expansions are the source of the call/return code cost; its code space competes with the data stack; its limits are 511 tokens, 64 labels and 128 references. K6/K7 now make malformed blocks a parse error. |
| **Host integration** | HOST ops are arithmetic, comparison, allocation, console I/O and diagnostics; they do not manipulate control flow. The host boundary is small. Its reliability risk is not speed but outcome reporting: `glon_load` can report a halted run from its garbage top-of-stack. |

## Essential vs implementation-chosen complexity

| Complexity | Essential to reliable generated software? | Source |
|---|---|---|
| A garbage-collected heap of immutable strings, blocks and closures | essential (safe values for generated code) | the language |
| Lexical closures that escape | essential (the binding laws) | the language |
| Checked stacks, resource fail-stops and parse-time validation | essential for untrusted generated code; their cost is small | runtime |
| Re-interpreting source blocks at every evaluation, with dynamic lookup | **chosen** (Rebol heritage); a compile-once step would remove most of the 92× gap | evaluator |
| Activation contexts on the return stack (64 cells) | **chosen** (fast allocation; it forces the conservative scan) | evaluator / GC |
| Conservative return-stack scan with an O(objects) object-start walk | **chosen** (a consequence of the above and of ×16 integer tags) | GC |
| Call, return, branch, `>R`, `R>` as 5–21-cell macros | **chosen** (seven primitives); 38.0% of code, 23.3–36.2% of instructions | S1 |
| Memory-mapped IP/SP/RP; unchecked `push` | **chosen** (seven primitives); makes K1 unpreventable above S1 | S1 |
| No stack-shuffle primitives | **chosen**; pushes runtime state into memory variables (23.2–41.7% of instructions) | S1 / runtime |
| One byte per cell in strings; no byte addressing | **chosen** (simplicity of S1's cell model) | S1 / object model |
| Code and data stack in one region | **chosen** (memory map) | memory map |
| masm as an unrestricted but parse-validated trapdoor | **chosen** (the escape hatch for libraries) | assembler |
| Unwrapped and wrapped sources interpret top-level `func` differently | **chosen**, but a language-semantics inconsistency to resolve | parser |
| Human-oriented syntax | not a design objective | — |

## What would distinguish the hypotheses next

**One build, two counterfactuals, the same programs**, measured with the
instrumentation used here. Neither needs a new machine or any application
change.

**1. The B arm: an S1 variant with five extra opcodes.** In an experimental
build only (frozen S1 untouched in the main build):
- the opcodes are `CALL a`, `EXIT`, `BRANCH a`, `>R` and `R>`;
- the derived `asm_call` etc. emit them;
- the evaluator, GC and libraries are unchanged.

Predicted result [D], corrected from the draft: the measured macro share says
the instruction saving is **at most** 23.3–36.2%, and a primitive still
dispatches once, so the overall speed-up is at most about 1.3–1.6×. Code would
fall by about 33.6% on the runtime's own code. It is not a 2× effect. The
compiled masm fib case is the exception, because almost all of its body is
macros: about 40 → 10 instructions per activation.

**2. The C arm, with no new machine at all.** Hand-compile two hot Glon
functions to masm on today's S1, as done here for fib, and keep their Glon call
sites. The fraction of each workload's time that moves tells how much is
evaluation architecture. The fib measurement is the first data point: the
evaluator accounts for nearly the whole 92× gap.

Running arm 1 is what would turn the [D] primitive figures into [M]. It
changes no application, no language semantics and no frozen file in the main
build.

## Measurement limits and corrections

- **Instruction counts are deterministic; wall-time is not measured.** The
  report makes no wall-time claim.
- **The code-size table was corrected.** The draft added overlapping
  categories; this version partitions the 12,861 runtime cells exactly and
  reports the 4,886 macro cells as a subset.
- **The B-arm saving was corrected.** The macro share is an upper bound, not a
  direct saving; a primitive still costs one dispatch.
- **The isolated `s/at`, `mem/byte` and raw-fetch figures from the draft are
  not used.** They were not reproducible with the committed workload method
  and are not needed for the conclusions.
- **`emit_call_fwd` is under-attributed.** A masm `CALL label` is not wrapped
  by the assembler as a `CALL` macro for this instrumentation, so its internal
  `>R` is counted but the whole expansion is not. Macro shares are therefore
  conservative for masm-heavy work.
- **The non-overlap categories depend on emitter names** in
  `measure/analyse.py`; a renamed emitter falls into an "other" category
  rather than silently joining another. The table above had no "other"
  category in the runtime.
- **The census is [R].** File and line counts in the compatibility section are
  from the earlier investigation, not re-counted here.
- **No B or C arm was built.** All primitive and compiled-masm figures beyond
  the measured 40 are labelled [D].

## Candidate simplifications and primitives

These are deliberately **not** part of this findings document and are **not
implemented or authorised**. Candidate simplifications, additional primitives
and their risks are listed in
[`GLON-EXECUTION-ARCHITECTURE-PROPOSALS.md`](GLON-EXECUTION-ARCHITECTURE-PROPOSALS.md).

## Sources

- `MACHINE-INTEGRITY-AUDIT.md` — K1–K7, the machine-integrity tests.
- `GLON-ARCHITECTURE-MEMORY.md` — memory map, measured facts, decisions and
  proposals.
- `GLON-ALPHA-LAWS.md` — binding and escape laws.
- `experiments/glon-masm/` — masm code density, call expansions, stack use.
- `experiments/glon-mem/` — memory vocabulary and checked byte access.
- `FIB-OPT-P6-PROFILE.md`, `FIB-OPT-P7-RESULTS.md`,
  `FIB-OPT-P10A-RESULTS.md` — earlier fib profiles and the compiled-S1
  control.
- `experiments/glon-execution-architecture/` — the tools and recorded output
  for this report.
