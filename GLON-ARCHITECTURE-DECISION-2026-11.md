# Glon Architecture Decision — November 2026

**Status:** consolidation and recommendation. Not an implementation, freeze,
migration or authorisation to change the reference machine.

## 1. Verified current state

Repository HEAD is `22c9c95` (“Add bounded DEFLATE code-length buffer and F2
analysis”). The DEFLATE experiment was rebuilt and rerun during this review:

- `make -C experiments/glon-deflate test`: **480/480 conformance runs pass**,
  **44/44 unit tests pass**; every result is compared with Python's zlib.
- `./check-frozen-s1.sh`: **OK**, frozen at
  `f90496c26dc45c7a387d8fc8639cd2781507d3d2`.
- F2's diff from the F1 baseline (`08c5816`) touches only
  `experiments/glon-deflate/`: `inflate.glon`, `dzhost.c`, `f2buf.glon`, the
  Makefile, tests and reports. No frozen-S1 file, production runtime, host ABI,
  primitive or application changed.

Checkpoints inspected in git, not just in summaries:

| Checkpoint | Commit | Verified content |
|---|---|---|
| B1 direct compilation | `3f096bc` | integer-subset compiler; fib 18: 30,909,921 evaluator instructions → 497,479 direct S1 instructions |
| B2 native candidates | `a86f973` | separate 13-op experimental machine; six candidates; no production change |
| B3 execution/calling convention | `08f213f` | instruction and wall-clock comparison; convention A selected |
| B4 source representation | `5dc2633` | text/tagged/direct representations; identical generated code |
| B5–B7 authoring | `358a97e`, `b0fb23e`, `59e2d6b`, `1bb1ffd`, `30553d3`, `9db5000` | LLM trials, structured validation, completion contract; mixed results |
| B8 architecture evaluation | `66f3908` | ten-workload comparison; recommendation only |
| C1 DEFLATE kernel | `9a1cf63` | unchanged `hf/left` compiled; Level 3 only |
| F1 fragmentation | `08c5816` | 479/480; exact fail-stop diagnostics |
| F2 bounded scratch | `22c9c95` | 480/480; no runtime/GC/primitive change |

**One verification discrepancy:** rebuilding the root `s1` test binary
(`make -B s1`) and running `./s1` reached the traffic-simulator section, where
test 8 emitted a machine fail-stop dump (`IP=8438`, `top=94741925`) and the
suite did not complete; `make test` consequently timed out. This is not caused
by F2's diff, which is confined to `experiments/glon-deflate/`, but it means the
full root test suite is **not green in this environment** and should be
investigated separately. The architecture-memory document's old “478/479”
DEFLATE figures are superseded by the verified 480/480 result and are corrected
in that document.

## 2. Architectural layers

| Layer | Existing and verified | Experimental and measured | Proposed but unimplemented |
|---|---|---|---|
| Source semantics | Glon words, blocks, contexts, closures, SIN!; `GLON-ALPHA-LAWS.md`; production evaluator; DEFLATE and application sources | B5–B7 authoring trials; no semantic change | Any new syntax, TCO or loop vocabulary |
| Compiler/lowering | No production compiler | B1–B4 subset compilers; C1 `hf/left`; direct compile-once evidence | Full-language compiler/runtime coverage, module/loader model |
| Frozen S1 | Seven primitives `LIT DUP DROP @ ! 0BRANCH HOST`; frozen hash verified | B1 compiler targets it unchanged | Any eighth primitive |
| Native execution | None in production | B2/B3/B8 separate machine with CALL, RET, >R, R>, R@, optionally BRANCH; no WASM port; unchecked stacks | A standard or optional 12-op execution profile |
| Memory/allocation | 65,536-cell map; 7,232-cell non-moving managed heap; one byte per string cell; production GC | F2 bounded per-header code-length buffer; `mem/` experimental packed-region library | 32 KiB window, packed/zero-allocation decoder, moving GC, heap increase |
| Host services | Native CLI, GLON_LIVE byte events, browser/WASM hosts, DOM/canvas/events | F2 uses only existing alloc/@/! mechanisms; no host change | Caller-provided output/input buffers |
| Application algorithms | DEFLATE interpreter; traffic simulator; Kākā game/demo | F2 DEFLATE; B8 integer kernels | Compiled versions of full applications |

The important separations are: **HOST is not a VM primitive**; managed
strings/blocks are runtime data structures, not language requirements; and
the B2/B3/B8 opcode machine is not production S1.

## 3. What the performance evidence supports

B1 established the largest single effect: for naive fib 18, direct compilation
removed 62.1× of evaluator instructions with no primitive change (30,909,921 →
497,479). B2 then isolated candidate opcodes on an otherwise identical
compiler. B3 added wall-clock and calling-convention comparisons. B8 repeated
the opcode comparison over ten integer workloads: fib 10/15/18, Ackermann
2 2/2 3/3 3, sum 200/500, and choose 6 2/10 5. All 130 workload/configuration
runs were correct and stack-balanced.

B8 configuration aggregate (deterministic counts; execution ratios noisy):

| Configuration | Cells | Instructions | Instr ratio | Cell ratio |
|---|---:|---:|---:|---:|
| seven-op (`none`) | 2,077 | 960,072 | 1.000 | 1.000 |
| CALL+RET+>R+R> | 886 | 358,856 | 2.675 | 2.344 |
| five additions: above + R@ | 772 | 289,178 | 3.320 | 2.690 |
| six additions: above + BRANCH | 721 | 275,554 | 3.484 | 2.881 |

The five-operation extension is **CALL, RET, >R, R>, R@** (S1 plus five =
twelve operations). It removes **69.9% of executed instructions** and **62.8%
of emitted cells** relative to seven-op lowering on this workload set. The
six-operation variant including BRANCH reaches 71.3% and 65.3%; BRANCH itself
adds only 13,624 instructions (1.4 points) and 51 cells (2.5 points), mostly in
branch-heavy `choose`. B3's fib/sum wall-clock medians show the same direction:
seven-op 1,860/145 µs, five-op 313/33.5 µs, six-op 297/33.7 µs. B3's instruction
counts for fib fall 497,479 → 137,957 → 129,595 and code cells 153 → 42 → 39.

The evidence also has firm limits:

- It covers an **integer-only, call-heavy subset**, not the full language.
- B8 has **no WASM port** for the candidate machine; production S1's WASM
  parity does not validate these opcodes.
- Wall-clock measurements are noisy (WSL, unpinned); instruction and cell
  counts are the primary evidence.
- The candidate ops preserve unchecked S1 stack semantics and do not close K1.
- No full application — DEFLATE, traffic or Kākā — was compiled or measured on
  the candidate machine.
- B4 shows representation convergence; compiler quality, VM efficiency,
  application efficiency and host overhead must not be collapsed into one
  number.

Direct compilation is a compiler strategy; native operations are a VM
efficiency choice. Neither establishes a whole-application speedup.

## 4. DEFLATE: requirement versus representation

F1 located the failure precisely. A dynamic Huffman header accumulated its
code-length array with `s/+`; when the array reached 287 bytes and the next
one-byte length arrived, the immutable-string implementation requested a
304-cell payload plus a 16-cell header = **320 cells**. The heap had 2,944 free
cells but the largest contiguous block was 304. The data was a temporary
sequence of code lengths, not output, input, the window or a symbol table.
Coalescing was working; the failure was external fragmentation.

F2 replaced the growing string with one fixed-capacity buffer per dynamic
header, filled in place by a small experimental masm helper (`f2buf.glon`).
`dz/ls-add` rejects an overlong run before it can write past the buffer. This
is a representation change in the decoder, not a new VM primitive. No GC
change, heap increase or host change was required. The result is 480/480
conformance, 44/44 unit tests and byte-identical zlib output on the former
failure.

Measured on `dynamic-text` at c128/r100, decode allocations fell from 4,313 to
4,086; decode cells from 191,056 to 171,568. On `compressible-a` they fell from
4,045/197,184 to 3,989/192,240. On the formerly failing `multi-memlevel1`
schedule the baseline stopped after 4,047 bytes; F2 completes 9,000. Per
produced byte, its decode allocation density is 1.44 versus 1.78 objects and
50.6 versus 75.7 cells. F2 demonstrates a **correctness and allocation-density
fix**, not a performance improvement: passing workloads were roughly 2–5%
slower in the profiling runs, and full-suite wall time is noisy.

The algorithmic requirement is a bounded sequence of Huffman code lengths.
The managed, repeatedly grown, contiguous string was an implementation
choice. Full RFC 1951 32 KiB history remains a separate 64K memory limitation.

## 5. Capability evidence

| Capability | Correctness | Execution | Managed objects | Host services | Performance | Limitations |
|---|---|---|---|---|---|---|
| DEFLATE | **480/480**, zlib match | production Glon on frozen S1 | strings, blocks, closures, GC | feed/read/finish/output frames | measured memory, time, GC, allocations | 768-byte window; 32 KiB unsupported; no compiled decoder |
| Traffic simulator | deployed; headless source/tests exist, but current root traffic test 8 fail-stopped in this review | production Glon/WASM and browser host | yes | DOM, events, canvas, timing | `traffic-bench`, browser evidence | compiler cannot accept source; root-suite discrepancy needs investigation |
| Kākā | deployed; native and WASM test sources exist | production Glon/WASM, browser host | yes | DOM/Canvas/HTML/events | native 200-tick timing in test harness | not rerun here; compiler cannot accept source; no B8 result |

C1's compiled `hf/left` is a genuine but narrow result: unchanged kernel,
10/10 independent vectors, about 1.97× fewer instructions and 1.78× smaller
code than seven-op direct lowering in that kernel. It is not a compiled
decoder and has no Level-4 production-path benchmark. The B5–B7 trials concern
authoring reliability; they do not change the VM evidence.

## 6. The primitive decision

**Recommendation: Option C — keep the seven-primitive S1 as the universal
reference machine and permit optional, explicitly specified execution
profiles, without freezing one now.**

| Criterion | A: seven only | B: adopt five universally | C: reference + optional profiles |
|---|---|---|---|
| Correctness | proven baseline | equivalent macro behaviour, but new machine surface | reference remains; profile must repeat tests |
| Compiler complexity | lowest | five native ops in every target | profile-specific lowering |
| Runtime complexity | lowest | +688 text bytes in B3 counter machine | complexity only in profile |
| Portability | frozen S1 has WASM parity | B3 machine has **no WASM port** | reference portable; profile needs its own port |
| Code size | largest compiled code | 2.69× smaller | depends on profile |
| Performance | baseline | 3.32× fewer instructions in B8 subset | optional measured gain |
| Testability | existing full tests | must test two machine semantics | requires profile conformance suite |
| Maintenance | one machine | frozen machine fork/migration | bounded optional surface |

B is not justified as a universal change: its evidence is integer-subset,
native-only, unchecked, and has no full application. A would discard a measured
3.32× instruction and 2.69× code-density gain for eligible compiled code. C
preserves the frozen reference and permits the five-op profile only where its
own correctness, WASM/portability and stack-safety tests exist. It also leaves
BRANCH out of the minimum; B8's own recommendation selects the five, not the
six.

The C1 result supports the profile for control-heavy kernels, not for the whole
language. No primitive should be frozen on this evidence alone.

## 7. Real blockers and non-blockers

**Correctness blockers / unresolved verification**

1. **Root traffic test 8 fail-stops** in the current rebuilt `s1` suite. The
   requirement is the existing headless traffic suite completing; evidence is
   the `[dump]` at `IP=8438`; the layer is not yet established (traffic,
   runtime or harness). The smallest action is to reproduce test 8 alone and
   identify whether this is a stale/local build or a real regression.
2. **Compile-once execution of full applications is unavailable.** The
   requirement is compiling existing DEFLATE/traffic/Kākā sources unchanged.
   Evidence: B8/C1 coverage failures at module wrappers, locals, blocks,
   strings, closures, errors, host services, and `/`. The responsible layer is
   the compiler/runtime coverage, not S1. The smallest corrective action is the
   next dependency slice (locals/division, then blocks/strings/GC, then
   closures/errors/host), with unchanged algorithms.

**Explicit capability limits, not current defects**

- **32 KiB DEFLATE window:** impossible in the 7,232-cell heap at one byte per
  cell (packed storage still exceeds it); it is a documented 64K resource limit
  requiring a new memory profile.
- **K1/checked stacks:** native and compiled code inherit unchecked S1 stacks.
  This is a safety gap, not a failure of valid existing applications; it needs
  a separate machine decision.
- **F2 buffer allocation:** a future workload could still fail the 336-cell
  per-header allocation; this is a tested design risk, not a current failure.

Performance opportunities (faster compiled code, zero-allocation decoding,
32K profile, packed memory) are **not** correctness blockers and should not be
launched as new projects without a defined application requirement.

## 8. Engineering decision rule

1. State the required observable behaviour and its test oracle.
2. State the algorithm's intrinsic resources (time, bytes, window, stack).
3. Check existing Glon/S1 mechanisms before proposing a primitive.
4. Implement the smallest correct representation; preserve semantics and
   independent tests.
5. Verify against an external oracle, not only against another Glon path.
6. Measure the actual bottleneck, separating compiler, VM, app and host.
7. Consider a new primitive/profile only when the previous steps show a
   concrete, reproducible gap.

Every experiment must name the decision it will settle. An experiment that
cannot change a retain/defer/stop decision is not worth running.

## 9. Retain, defer, stop

**Retain:** frozen seven-primitive S1; production evaluator, managed heap,
host ABI and application vocabularies; F2's bounded-scratch correction;
DEFLATE's 480/480 result; B1–B8/C1 as measured evidence.

**Defer:** optional 12-op profile (five operations, no BRANCH), full-language
compiler coverage, WASM/portability for a profile, 32K memory profile,
zero-allocation decoder, K1/checked stacks. Each needs a separately approved
test plan.

**Stop:** adding primitive variants without an application requirement;
redesigning the GC to hide a representation choice; increasing the heap to make
a test pass; treating a compiled integer kernel or a synthetic benchmark as
whole-application evidence; launching C2 or further architecture experiments
before the compiler/runtime coverage decision.

## 10. Limits of the evidence

The strongest verified facts are: frozen S1; DEFLATE 480/480 and 44/44;
F2's no-runtime-change correction; B1's direct-compilation effect; and B3/B8's
deterministic integer-subset instruction/cell reductions. The weakest are:
full root-suite verification (traffic fail-stop), full-application compilation,
WASM parity of the candidate machine, wall-clock precision, and LLM authoring
generality (single model, limited seeds). The B8 recommendation remains a
candidate recommendation, not an adopted architecture.

**Bottom line:** the smallest Glon architecture already justified by working
software is the frozen seven-primitive S1 plus the existing language/runtime,
managed memory and host boundary. A five-operation execution profile is
promising and measurable, but remains optional and unfrozen. The next real work
is compiler/runtime coverage and resolving the root test discrepancy — not a
new primitive set, heap, GC or application rewrite.
