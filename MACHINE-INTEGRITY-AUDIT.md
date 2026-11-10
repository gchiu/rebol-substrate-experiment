# Machine Integrity Test Coverage Audit

2026-11-09, branch `glon-shop-g1a`, from commit `72dca7a`. Repairs and the
expanded suite: 2026-11-10, see "Repairs (2026-11-10)" below. The sections from
"Scope" to "Priorities" record the audit as it was made.

## Repairs (2026-11-10)

Four of the five original defects are repaired, in priority order. Each
repair is its own commit, with its expected failure promoted to ordinary
regression checks. Two further defects found during the work, K6 and K7, were
also repaired. K1 cannot be prevented without changing frozen S1, so it is not
repaired; the necessary change is described below.

| Defect | Status | Commit | What changed |
|---|---|---|---|
| **K5** label/reference tables overflow host memory | **repaired** | `4580c68` | The parser counts a block's labels and references (`raw_counts`) and refuses a block over 64 or 128 as `too_large`, before emitting anything. `assemble_raw` also checks each index (internal-error abort). |
| **K2** plain Glon nesting overwrites code, reported clean | **repaired** | `9ec4872` | A data-stack guard at `SUBEXPR` entry, which every evaluation step passes through. A masm word's result set is also checked on return. Details below. |
| **K3** refused multi-block sources leak code | **repaired** | `ef5f2a2` | A source's masm blocks are emitted only after the whole source parses. Each callable gets its predicted entry; a failed parse emits nothing. |
| **K4** masm in unwrapped sources fail-stops | **repaired** | `e0b6692` | One `parse_masm` function, shared by `parse_block` and the unwrapped top-level path. |
| **K6** invalid masm accepted with a warning | **repaired** | `360e0a1` | `raw_validate` refuses undefined or duplicate labels, unresolved operands, unknown mnemonics, an instruction without its operand, and HOST ids outside S1's closed set, before emitting anything. |
| **K7** masm blocks can enter or run past their end | **repaired** | `a9f3f8b` | `raw_validate` refuses an empty block, a block with only labels, a block whose last instruction is not EXIT, BRANCH or the indirect jump `LIT REG_IP !`, and a label after the last instruction. A final CALL is refused because its return falls off the end. |
| **K1** a masm word pushes into code between steps | **not repaired: needs a frozen-S1 change** | — | Explained below; still the only strict expected failure. |

Tests followed each repair, then the expansion in `c20c8a5`. Every commit
was verified with:
- `make test`;
- `make machine-integrity` and `make machine-integrity-wasm`;
- `kaka-test`, `patrol-test`, `glon-smoke` and `glon-live-native-test`;
- the glon-mem tests (native and WASM) and the glon-masm tests.

The DEFLATE corpus gave 479/480 after K2, unchanged; the one failure is the
known fragmentation fail-stop. Frozen S1 is unchanged throughout.

### K2: the data-stack guard

**Main world.** On entry, `SUBEXPR` fail-stops if SP is below `RV_DSLIM`:
- `RV_DSLIM` is the end of emitted code plus `R0S1_DS_GUARD` = 64.
- It is set at initialisation, after each masm assembly, and at each run
  start.

**M1 task.** The bounds are `base + 64 <= SP <= base + M1_DS_OFF`.

**Any other SP** is an underflow.

**Halt reasons.** Two new reasons: `R0S1_HALT_DS_EXHAUSTED` (3) and
`R0S1_HALT_DS_UNDERFLOW` (4). The session outcome stays HALT / machine, so
the host ABI does not change.

**Why 64 cells.** The guard is the most the stack may grow between two
evaluation steps. It was measured with instrumented scratch copies of S1 and
the runtime, over the whole `./s1` suite, the glon-mem and glon-masm suites
and `binary-strings.glon`:
- no runtime routine and no shipped masm library pushed more than 16 cells
  between two steps;
- the only larger excursions were deliberate masm stack misuse in tests (the
  m2 sentry tests and the debugger fixture's stack switch).

**The deepest nesting that runs**, measured by bisection (integrity S2):

| Configuration | Deepest nesting that runs | One level deeper |
|---|---|---|
| libraries loaded (2,144 cells of stack room) | 2,080 levels | fail-stops |
| code full | 192 levels | fail-stops |

In both cases the lowest cell written stays at least 59 cells above the end
of code.

**The masm result check.** `r_invoke_raw` checks, on return, that a masm
word's result set lies inside the stack it returned on. A word that declares
more results than it left would hand back a return stack's bottom cell, and
`block_eval`'s next `CALL` (whose continuation literal passes through the
data stack) wrote there, one push before the next guard check.
`examples/raw-memory.r0` did exactly this:
- its words are documented `( v -- )` but said `ARITY 1`;
- they now say `ARITY 0`, with the same results.

**Code size.** The guard and the result check add 191 cells to the runtime's
emitted code: 12,861 cells, leaving 3,267 below the data-stack top (was
3,458).

**Cost:** about 3% more S1 instructions on call-heavy code, exact counts with
`s1_prof.c`:

| Workload | Change |
|---|---|
| `fib 20` | +3.1% |
| balanced recursive sum | +3.0% |
| `s/from-int` (masm) | +2.6% |
| string building | +0.9% |

**Also repaired by K2: M1 task stacks.** A task nesting 500 deep in its
400-cell stack used to run "clean" while writing into the top of the loader
heap. It now fail-stops (M1 test K, verified failing on the old runtime).

### K1: why it needs a frozen-S1 change

**The mechanism.** A masm word runs raw S1 instructions between two
evaluation steps. `s1.c` executes `LIT`, `DUP`, `@` and `HOST` pushes with
`M[--SP] = v` and no bound, so:
- only the S1 machine can stop a push into code;
- the guard sees nothing until the word returns;
- a word that pushes 400 cells and pops them again is invisible to both the
  guard and the sentry.

**No non-S1 alternative prevents it:**
- A masm verifier cannot bound masm. masm can store to any address (`!`) and
  write `REG_SP` directly, and the M1 scheduler and the debugger fixture rely
  on exactly that to switch stacks.
- Sampling after the word returns would only detect damage already done,
  which is the workaround the repair brief rules out.

**The necessary change, for approval.** A data-stack limit in S1 itself:
- a memory-mapped limit register (for example `REG_SPLIM` in the low cells);
- `push` fail-stops when `SP - 1 < M[REG_SPLIM]`, with one more compare per
  push in the C interpreter;
- optionally, `pop` checks a top register for underflow.

Its consequences:
- **Frozen files.** This changes `s1.c` and `s1.h` and the S1 machine
  contract.
- **Stack switchers.** The runtime would set the register for the main world.
  Every masm stack switcher (the M1 scheduler library, the debugger fixture)
  must also set it when it switches SP.
- **Performance.** It could replace the runtime guard and its ~3% cost.

Until then, masm is a trusted trapdoor and K1 stays an expected failure.

### New findings during the repairs and the expansion

- **K6. Invalid masm was accepted with only a warning on stderr.** Before
  the repair:
  - **Undefined labels.** In `BRANCH` and `CALL`, the target was patched to
    address 0. Running the word executed the register cells as code: in the
    probe, "bad opcode at 1".
  - **Silently dropped:** unresolved operands (`LIT nosuch`), unknown
    mnemonics, and an instruction at the end without its operand.
  - **Duplicate labels:** the first definition won.
  - **`HOST` ids outside S1's closed set:** S1 printed "bad host id" at run
    time and did nothing.

  **Repaired in `360e0a1`.** `raw_validate` now refuses all of these before
  anything is emitted. Section V, cases 1–16, hold the repair; none of the
  refused words is run.
- **K7. A masm block could enter or run past its own end.** An empty block
  (`masm 0 [ ]`) emitted nothing, and its callable's entry was the end of
  code; calling it ran whatever followed. In the isolated probe this drove SP
  down to 10,656, inside the runtime's code. **Repaired in `a9f3f8b`.**
  `raw_validate` now refuses:
  - an empty block, or a block with only labels;
  - a block whose last instruction is not EXIT, BRANCH, or the indirect jump
    `LIT REG_IP !`;
  - a label after the last instruction;
  - a final CALL: the callee returns to the instruction after the call, which
    is the end of the block (the existing MASM probe was made well formed by
    adding EXIT).

  Section V, cases 17–21, and the valid-form checks hold the repair. In the
  probe the check reads the entry; it never calls it.
- **Unwrapped and wrapped top-level `func`s differ.** In an unwrapped source
  (`./glon` and WASM `glon_load` parse files as given), a top-level `func`
  gets no func site and no lexical scope of its own; the session facade wraps
  every cell, so there it does. No test found a behaviour difference, but the
  binding laws are written for the wrapped form. Making the two forms
  identical is a language-semantics change for 40 unwrapped files in the
  repository, so it needs a proposal; K4 deliberately changed only masm.
- **`glon_load` cannot report a halted run.** `r0_s1_run_persistent` returns
  the top data-stack cell divided by 16, and on a halt that is whatever was
  on top. `glon_load` turns it into 0 or −3, so a WASM host can take a halted
  load for a successful one. The parity tests therefore compare printed
  output. Changing it is a host-ABI change.
- **The masm token maximum is 511**, not 510 (`MASM_TOK_CAP` 511; 512 is
  refused).
- **The masm test 13 fingerprint for `demo/shop/common.glon` recorded the K4
  defect:** before K4, only 147 cells, because its top-level masm definitions
  were never assembled. It now equals its wrapped bundle `bootstrap.glon`
  (802 cells), which independently confirms the repair.

### The machine-integrity suite now

**Targets:**

| Target | What it runs | Time |
|---|---|---|
| `make machine-integrity` | frozen-S1 check; `machine-integrity-test` (201 checks, 1 expected failure: K1); the ASan table probe (8 checks); `integrity/parity.sh native` (10 Glon-level files) | ≈ 3.2 s |
| `make machine-integrity-wasm` | `parity.sh` native and in the standalone WASM build; both must match `integrity/expected/` | ≈ 6 s with the WASM build |

**Sections of `r0_s1_integrity_tests.c`:**

| Section | Covers |
|---|---|
| **R** | the GC-interior and masm-bound regressions |
| **A** | determinism and residue independence |
| **B** | ten failure kinds ×10 return to a fixed state, including data-stack exhaustion |
| **C** | runtime code immutability |
| **S** | stack boundaries: K2's reproduction, exact deepest nesting with libraries loaded and with code full, `values` accumulation, closure arguments, recursion, a collection 150 deep, underflow |
| **L** | failed-load recovery (K3) |
| **U** | unwrapped sources (K4) |
| **M** | assembler limits: tokens 511/512, labels 64/65, references 128/129 (BRANCH and CALL), named CALLs, every form's size against the frozen expansions |
| **K** | known defects |

Also: M1 test K (task stacks).

**Glon-level parity files**, each run in a fresh session, with the
`parity:` lines natively and in WASM compared with `integrity/expected/`:

| File | Checks |
|---|---|
| `binary-strings` | binary `STRING!` preservation |
| `gc-values` | a block, a string and a closure probed with stale integers over 81 collections |
| `stack-ok` | permitted deep stacks |
| `stack-exhaust`, `stack-underflow` | the line before the failure prints, the line after does not |
| `masm-unwrapped` | masm in an unwrapped source |
| `masm-limits` | blocks at the table limits |
| `masm-refused`, `masm-invalid`, `masm-empty` | a refused source prints nothing (K6, K7) |

### Coverage matrix after the repairs

Changed rows only; the others are as in Phase 5.

| Invariant | After the audit (2026-11-09) | Now |
|---|---|---|
| S1 data stack never enters code | XF K1, K2 | ✔ for all Glon evaluation (guard, S1–S8, M1 K, parity); **XF K1** for masm (needs S1) |
| S3 underflow detected | ✔ post-run sentry | ✔ before the next push (S7, parity) |
| M2 refused source emits nothing | XF K3 | ✔ L1–L6, U3, masm_bound 6b |
| M3 host tables in bounds | XF K5 | ✔ M2, ASan probe |
| M5 masm in every source form | XF K4 | ✔ U1–U3, parity |
| M7 *(new)* invalid masm refused at parse | — | ✔ K6 in V1–V16; K7 in V17–V21 and the valid-form checks; `masm-invalid` and `masm-empty` |
| M8 *(new)* emitted size equals the frozen expansions, for every form | ◐ M4 internal abort | ✔ M3 |
| G2/G1 preservation under stale integers | ✔ strings, native + WASM | ✔ strings, blocks, closures and contexts, native + WASM |
| E6 native/WASM parity | ◐ one file | ✔ 10 files incl. failures (printed output; see the `glon_load` finding) |

### Remaining priorities

1. **K1** needs approval of the S1 data-stack limit described above.
2. **Wrapped vs unwrapped `func` semantics**, and **`glon_load`'s return
   code**: each needs a proposal (language semantics; host ABI).

K6 and K7 are repaired (`360e0a1`, `a9f3f8b`); no further priority is open
for them.

## Scope

The audit asks whether Glon's tests protect the machine itself as well as what
programs compute: memory, the stacks, emitted code, the collector, and the
state a session returns to after a failure. The audit:

- adds tests and documentation only;
- does not change frozen S1, the ABI, the memory map, the runtime, the
  assembler or the collector;
- does not repair the defects it found. They are recorded as reproducible
  expected failures, ranked in "Priorities".

**New targets:**

| Target | What it runs | Time (native, this machine) |
|---|---|---|
| `make machine-integrity` | the frozen-S1 check, `machine-integrity-test` (96 checks, 4 XFAIL), `integrity/binary-strings.glon` through `./glon`, and the AddressSanitizer probe (2 checks, 2 XFAIL) | ≈ 2.7 s wall, once built: 0.8 s + 1.8 s + 0.1 s |
| `make machine-integrity-wasm` | `binary-strings.glon` in the standalone WASM build under node; its output must equal the native CLI's | ≈ 3 s including the WASM build |

**New files:**

| File | Purpose |
|---|---|
| `r0_s1_integrity_tests.c` | sections R, A, B, C and K (below) |
| `integrity/binary-strings.glon` | binary `STRING!` preservation under GC churn, at Glon level so that it also runs in WASM |
| `integrity/asan_probe.c`, `integrity/asan-probe.sh` | the masm label/reference table probe: one block per process, AddressSanitizer build |
| `integrity/run-wasm.js` | runs Glon files in one standalone `glon.wasm` session |

**Status:** `make test` still passes. Neither new target is part of `make
test`, which is unchanged.

**XFAIL policy.** A known-defect check states the invariant that should hold.
- While the invariant does not hold, the check reports `xfail` and is not
  counted as a failure.
- If a repair makes it hold, the check reports `XPASS` and the target
  **fails**. This forces the check to be promoted to an ordinary one
  deliberately, rather than passing unnoticed.

**Safety.** Every test runs in its process's own copy of the 64K-cell memory.
- The checks that overwrite code (K1, K2) never execute the overwritten code;
  a fresh initialisation follows each.
- The one host-level overflow (K5) runs only in the AddressSanitizer build,
  which stops at the first out-of-bounds write.

## Phase 1: inventory

The repository has 46 C test files (42 `r0_s1_*_tests.c` files, `tests.c`,
`adversarial.c`, `claims.c` and the worklist-overflow build), plus the demo
and WASM suites. Each is
classified below by what it checks:

- **F (functional):** the computed results.
- **B (boundary):** behaviour at a capacity or limit.
- **I (integrity):** that machine state (memory, stacks, code, heap) stays
  sound.
- **R (recovery):** that the session works after a failure.
- **C (composition):** libraries or features combined.

### Integrity, boundary and recovery tests (before this audit)

| File | Class | What it protects |
|---|---|---|
| `r0_s1_m2_tests.c` (44 checks) | I, B, R | Collection reclaims dead objects, cycles and nested closures; the heap frontier stays bounded; OOM is a clean fail-stop. Return-stack overflow fails cleanly (N3). The stack sentry catches DROP/ADD on an empty stack, pops of an empty return stack, and data/return overflow from masm. Finished M1 task residues are skipped by the collector. |
| `r0_s1_gc_safepoint_tests.c` | I | A closure under construction survives a collection with the heap full. |
| `r0_s1_gc_worklist_tests.c`, `gc_worklist_overflow_test.c` | B, I | The mark worklist bound and its fail-stop, in a separate enlarged-heap build. |
| `r0_s1_gc_interior_tests.c` | I | The conservative return-stack scan accepts only object starts. Uses one byte value (9). |
| `r0_s1_masm_bound_tests.c` | B, I | The code-space limit, refusals that write nothing, and the sentry at the current code end. |
| `r0_s1_parse_hardening_tests.c` | B, R | A full symbol table and 5,000 malformed cells. Each parse failure rolls back the loader heap and symbols, and earlier definitions still run. |
| `r0_s1_context_capacity_tests.c` | B, R | A full global context or function context fail-stops before any write. |
| `r0_s1_session_tests.c` | R | Every outcome kind is classified, and the session survives each. |
| `r0_s1_error_tests.c` | F, R | SIN! values, propagation, and the `judge` boundary. |
| `r0_s1_debug_tests.c` | F, R | The cooperative debugger, including OOM paths. |
| `r0_s1_bound_tests.c`, `r0_s1_escape_law_tests.c`, `r0_s1_lambda_tests.c`, `r0_s1_nested_closure_tests.c` | I (language level) | No escaped stack context is ever dereferenced. Saved-IP corruption is regression-tested. |
| `r0_s1_masm_tests.c` | F, I | masm forms. Check 13: emitted library code is identical to the pre-repair assembler, up to relocation. |
| `tests.c`, `adversarial.c`, `claims.c` | F, I | S1 primitives and their combinations. |
| `r0_s1_g1e_tests.c`, traffic/Newell/OVM/NaSch tests | F, determinism | Identical output for identical input, at the program level. |

### Functional and composition tests

- **Language features:** `r0_s1_tests.c`, `case`, `equality`, `invoke`,
  `reduce`, `parse`, `primer`.
- **Strings:** `m3c`, `string_ops`, `string_ext`.
- **Structured data and M1:** `m3`, `m3b`, `m3d`, `m1`.
- **Browser and UI:** the G1A–G1D tests and the GLON_LIVE tests.
- **Demo suites:** the kaka/patrol/desktop suites, and the WASM suites
  (`wasm-*-test`, `demo/shop/*_test.js`).
- **Experiment suites** (not part of `make test`): `experiments/glon-deflate`,
  `glon-mem`, `glon-masm`.

### What the inventory showed

**Well covered:** the integrity tests that exist are thorough for the defects
that produced them.

**Gaps.** Six classes of invariant had no test:

1. **Excursions between samples.** The data stack can go deep between
   closure invocations and come back before the run ends.
2. **Byte-value sweeps.** Binary data under the conservative scan was tested
   with one byte value only.
3. **Repeated failures.** No test checked that the same failure, repeated,
   returns the session to the *same* state, as opposed to "the next program
   still works".
4. **Initialisation.** No test checked that initialisation does not depend on
   prior memory, and that it re-emits identical code.
5. **Code immutability.** No test checked that the runtime's own code is never
   written.
6. **Host-side tables.** The C side of the assembler, its label and reference
   tables, had no bounds test.

Native/WASM parity existed only for functional programs.

## Phase 2: invariants

| # | Invariant | Area |
|---|---|---|
| G1 | A collection never reclaims a reachable object | memory/GC |
| G2 | A collection never writes into a live object's payload, whatever bytes it holds and whatever stale integers are on the stacks | memory/GC |
| G3 | The heap frontier never passes the loader heap (40,000) | memory/GC |
| G4 | Managed-heap exhaustion is a clean fail-stop, after which the session's live heap is what it was | memory/GC, recovery |
| G5 | The mark worklist is bounded | memory/GC |
| S1 | The data stack never enters emitted code; if it does, the run is not reported clean | stacks |
| S2 | Return-stack exhaustion is a clean fail-stop, and the session recovers | stacks |
| S3 | Stack underflow is detected | stacks |
| S4 | Every run starts from clean stack registers | stacks |
| M1 | Emitted code never passes `R0S1_CODE_LIMIT` | assembler |
| M2 | A refused source emits no code | assembler |
| M3 | The assembler's host tables are never indexed out of bounds | assembler |
| M4 | Emitted size equals computed size | assembler |
| M5 | masm is accepted in every source form the loaders accept | assembler |
| M6 | The runtime's own code is never written after initialisation | assembler, execution |
| E1 | A parse failure rolls back the loader heap, symbols and sites | execution |
| E2 | Every failure kind is classified | execution |
| E3 | A failure, repeated, returns the session to the same state (code, globals, live heap; loader heap by a constant amount) | execution, recovery |
| E4 | Identical sessions produce identical memory | execution |
| E5 | A session's behaviour does not depend on memory left by earlier sessions | execution |
| E6 | Native and WASM builds behave identically | execution |

## Phase 3: the three known limitations, examined

All probes ran in isolated scratchpad harnesses against the unchanged
runtime.

### 1. Data-stack excursions between samples: **confirmed, and wider than recorded**

**How the sentry samples.** `RV_SPMIN` is updated only at closure invocation
(`r0_s1_runtime.c`, the closure entry sequence). The post-run sentry compares
it, and the final SP, with `asm_here()`. A run that goes deep and comes back
without entering a closure is therefore invisible.

**(a) masm (K1).** A word pushes 400 values in a loop and pops them again.
- With masm libraries filling code to `R0S1_CODE_LIMIT`, the pushes overwrite
  roughly the last 145 cells of library code.
- The run is reported **clean**: `RV_SPMIN` is still 65,535.
- Called through a Glon function, `RV_SPMIN` records only the function's
  entry (16,383).
- The last three filler words then fail when called.

masm is the RAW trapdoor and can write any cell with `!`, so for masm this is
a gap in detection, not a new capability.

**(b) Ordinary Glon, no masm (K2). This is the significant finding.**
- **Depth per level.** Each pending argument of a native operator holds one
  data-stack cell, and no closure is entered. A flat `+ 1 + 1 … 1` uses
  depth + 5 cells.
- **Depth of one block.** The 512-value block limit caps one block at 255
  levels, 260 cells. That is already 4 cells more than the 256-cell reserve.
- **Unbounded depth.** Nesting through blocks removes even that bound:
  `+ 1` ×100 `either 1 [` …, repeated in 36 blocks, reaches 3,600 levels.

| Configuration | Data stack used | Result |
|---|---|---|
| Fresh runtime (code ends at 12,926), 34 × 100 levels | 3,407 cells | clean, correct |
| Fresh runtime, 36 × 100 levels | reaches 12,926 | **overwrites the runtime's own code**; reported clean (`OK`, value 3,601) |
| Code full of masm libraries, 4 × 100 levels | reaches 16,128 | overwrites library code; reported clean; the last library word then fails |

- The program is short, ordinary Glon: no masm, no host call, no special
  word.
- Nothing bounds the nesting depth: neither the parser nor the evaluator nor
  the sentry. The block-size limit bounds width, not depth.
- Generated code produces deep right-nested arithmetic easily, which matters
  for an execution target for LLM-generated software.

### 2. Repeated refused multi-block loads: **confirmed; bounded, session-local**

**The probe.** Source `a: masm 0 [ LIT 0 ARITY 1 EXIT ] b: masm 0 [ x: CALL x
×250 … ]`. Block b needs 5,271 cells and can never fit.

**What happens on each attempt:**
- the source is refused (`too_large`);
- it binds nothing;
- the loader heap is unchanged (the parse rollback holds);
- block a's 21 cells stay in code space, unreachable.

**Measured.** On a fresh runtime, 153 refusals use all 3,192 cells up to the
limit. After that:
- every masm block is refused for the rest of the session, including the
  21-cell minimum;
- plain Glon still runs.

**Assessment.** No memory is corrupted and the limit is never passed (K3
checks both). The impact is denial of masm in one session, triggered by
retrying a too-large source, which an LLM-driven loop plausibly does.

### 3. Label and reference tables: **confirmed host-level buffer overflow**

**The tables.** `raw_labels[64]` and `raw_refs[128]` (`r0_s1_runtime.c:310`,
`:313`) are written with no index check, while a block may hold 510 tokens.

**Under AddressSanitizer:**
- 64 labels and 128 references assemble cleanly.
- **65 labels:** `global-buffer-overflow` WRITE of 8 bytes just past
  `raw_labels`. The next global is `raw_nlabels`, the table's own count.
- **129 references:** the same past `raw_refs`, whose next global is
  `raw_nrefs`.

**Why it matters.**
- In an ordinary build this is C undefined behaviour in the host, reachable
  from any Glon source containing a masm block.
- In WASM it corrupts the runtime's C data in linear memory.
- It is the only finding that escapes the 64K-cell machine.

### 4. The 256-cell reserve

`R0S1_DS_RESERVE` is a **measured allowance, not an enforced bound**:

- The measured basis is real: workloads used at most 87 cells, and the
  return stack 7,783 of 8,192 cells at 80 nested calls.
- Nothing stops the data stack using more. The sentry compares SP with
  `asm_here()`, not with `R0S1_CODE_LIMIT`; with code below the limit the
  stack may legitimately use the gap.
- As shown in item 1, a single flat block already needs 260 cells, and nested
  blocks need any amount.

The reserve protects library code against the **measured** workloads only. It
is a configurable constant (`r0_s1.h`), and changing it changes neither the
ABI nor the map.

### 5. Other findings during the audit

- **K4. masm in an unwrapped source fail-stops.**
  - **Source:** first recorded in `experiments/glon-latex/GLON-LATEX-V001.md`
    as a loader/CLI question.
  - **Root cause:**
    - `masm`/`raw` is recognised only in `parse_block`
      (`r0_s1_runtime.c:607`).
    - The unwrapped top-level path, `parse_program` (`:3671`), parses each
      item with `parse_form`, so `k: masm 1 [ … ]` loads as plain words.
    - When run, it fail-stops (`HALT / machine`).
  - **Affected:** the native `./glon` CLI parses files as given. The session
    facade wraps cells in `[ ]`, so cells are not affected.
  - **Severity:** a clean fail-stop, but a trap for every library author. The
    audit's own test file hit it.
- **`r0_s1_init` is not a memory clear.**
  - **Observed:** after re-initialisation, 35 to 2,106 cells differ from a
    first initialisation, depending on the previous workload: stack contents below SP and RP, heap cells above HP,
    loader-heap slack, and the low and RV areas. With poisoned memory, 52,532
    cells differ.
  - **Checked:** sessions on zeroed, poisoned, pointer-like or previously used
    memory give identical results and end in an identical state (A3). Nothing
    reads the residue.
  - **Consequence:** for a host that reuses one process for unrelated
    sessions, a masm word can read the previous session's data. This is
    information, not a defect.
- **Loader heap per cell.** Every successfully parsed cell stays in the loader
  heap: at least 16 cells for a short cell, 32–112 cells for the B programs.
  This is documented (`loader_exhausted`, §6 of the architecture memory). B
  checks that a failure costs exactly the same each time, which means no
  hidden retention.

## Phase 4: the `machine-integrity` target

### `machine-integrity-test` (`r0_s1_integrity_tests.c`)

| Section | Checks | Invariants |
|---|---|---|
| **R** | re-runs `gc_interior` (9) and `masm_bound` (29) | G2, M1, S1 |
| **A1** | two initialisations give identical memory (all 64K cells) | E4 |
| **A2** | the same libraries and workload (closures, strings, a collection, a caught SIN!) in two sessions give identical memory | E4 |
| **A3** | sessions on zeroed, poisoned, pointer-like, and previous-session memory give identical results and end state | E5 |
| **A4** | re-initialisation re-emits identical runtime code | M6 |
| **B1–B9** | each failure kind, 10 times in a fresh session after a warm-up. After every round: (a) the classification is as expected; (b) the next program is correct; (c) code end, code, globals and the live heap after a full collection equal the baseline; (d) loader-heap growth is the same every round; (e) the heap frontier is below 40,000, with no sentry report | E2, E3, G3, G4, S2, S4 |
| **C1–C2** | the runtime's code is unchanged by all of B, and equals freshly emitted code | M6 |
| **K1** XFAIL | masm 400-deep excursion into full library code is reported clean | S1 |
| **K2** XFAIL | plain Glon 3,600-deep nesting overwrites runtime code and is reported clean. Control: 200 deep is correct | S1 |
| **K3** XFAIL | 200 refused two-block sources leak code. Ordinary checks: they bind nothing, never pass the limit, and Glon still runs | M2 (M1 holds) |
| **K4** XFAIL | an unwrapped `k: masm …` fail-stops. Control: the wrapped one works | M5 |

The B failure kinds:
- parse error (syntax), parse error (too large);
- uncaught SIN!;
- unset word (HALT / machine, as specified in `r0_s1_session_tests.c` S10);
- managed-heap exhaustion (repeated string doubling);
- return-stack exhaustion (unbounded recursion);
- `CONTEXT_FULL` (17 bindings);
- two controls: a judge-caught SIN!, and closure churn.

**Verified sensitive.** In a scratch copy, a mutant kind that grows a global
string each round fails B(c).

### `integrity/binary-strings.glon` (G2, E6)

- **Strings.** For 16 rotations r, a 256-byte string with byte i = (i + r) mod
  256. Every byte value 0–255 thus sits at every position within a 16-cell
  granule.
- **Stale integers.** For each of the string's 17 granules, an integer equal
  to that interior address is live in a stack context.
- **Churn.** Garbage strings and a closure are allocated, and a full
  collection runs.
- **Check.** Every byte is compared with its pattern.

Result: 272 collections, 0 changed bytes, native and WASM identical.

**Verified sensitive.** The runtime from before the GC object-start repair
(`e988c19^`) fail-stops on this file.

### AddressSanitizer probe (K5, M3)

- 64 labels and 128 references: no out-of-bounds access (ordinary checks).
- 65 labels, or 129 references: the XFAIL expects ASan's
  `global-buffer-overflow`.
- A clean refusal would be an XPASS, which fails the target.

## Phase 5: coverage matrix

Key: ✔ covered · ◐ partly · ✘ not covered · XF known defect, held by a strict
XFAIL.

| Invariant | Before this audit | After |
|---|---|---|
| G1 reachable objects survive | ✔ m2 A/E/G/L, safepoint, gc_interior 4–5 | ✔ (+B controls) |
| G2 no write into live payloads | ◐ one byte value (gc_interior) | ✔ all 256 values × 16 alignments × 17 granules, native + WASM |
| G3 heap frontier bounded | ✔ m2 M/O/P | ✔ (+B, every round) |
| G4 OOM clean, heap restored | ◐ halts cleanly (m2 P) | ✔ B5: live heap equals baseline after 10 OOMs |
| G5 worklist bounded | ✔ worklist tests + overflow build | ✔ |
| S1 data stack never enters code undetected | ◐ final SP + closure-entry samples (masm_bound 7, m2) | **XF K1, K2** (gap made explicit) |
| S2 return-stack exhaustion recovers | ◐ m2 N3 | ✔ B6 |
| S3 underflow detected | ✔ m2 sentry cases | ✔ |
| S4 clean registers each run | ◐ implicit | ✔ B (next run correct after every kind) |
| M1 code ≤ limit | ✔ masm_bound | ✔ (+K3 under repeated refusal) |
| M2 refused source emits nothing | ◐ documented in masm_bound 6b | **XF K3** |
| M3 host tables in bounds | ✘ | **XF K5** (ASan) |
| M4 emitted = computed size | ✔ internal abort + masm 13 | ✔ |
| M5 masm in every source form | ✘ (documented in glon-latex) | **XF K4** |
| M6 runtime code immutable | ✘ | ✔ A4, C1, C2 |
| E1 parse rollback | ✔ parse_hardening | ✔ |
| E2 failure classification | ✔ session tests | ✔ (+B) |
| E3 repeated failure → same state | ✘ | ✔ B1–B9 |
| E4 identical sessions → identical memory | ◐ program-level determinism | ✔ A1, A2 (whole memory) |
| E5 independence from residue | ✘ | ✔ A3 |
| E6 native/WASM parity | ◐ functional programs | ◐ functional + binary-string GC churn |

### Not covered, still

These are recorded so that they are not mistaken for covered:

- **USER payloads after `count`.** They are untraced by design (`trace_user`).
  No test shows a pointer stored there being reclaimed. It is a contract of
  the M3 type, relied on by the Glon/Binary proposal.
- **Long M1 multi-task runs** under failure (beyond the m2 X task-residue
  checks).
- **The GLON_LIVE asynchronous path** under failure.
- **WASM parity of the C sections (A, B, K).** These use the C API, which the
  standalone WASM module does not export.

## Priorities for future repairs (as audited; see "Repairs (2026-11-10)")

1. **K5. Label/reference bounds.** This is host undefined behaviour,
   reachable from any masm source. The repair is small and local: refuse a
   block at 65 labels or 129 references, as `too_large`, before writing. No
   ABI or map change.
2. **K2/K1. Data-stack depth.** Plain Glon can overwrite the runtime's code
   and be reported clean. Options to evaluate, each a runtime change and
   none an S1 change:
   - **(a)** A parse-time bound on pending-argument depth across nested
     blocks. Cheap, and covers K2 but not masm.
   - **(b)** A guard check of SP against `asm_here()` in the evaluator's
     argument path. This has a per-call cost, to be measured.
   - **(c)** Canary cells at the code end, checked by the post-run sentry.
     Cheap and covers both, but it detects after the fact and cannot prevent
     the overwrite.

   Possibly (a) plus (c).
3. **K3. Refused-source code leak.** Size every masm block of a source before
   emitting any. The parser already computes `raw_size`. Alternatively, defer
   emission until the whole parse succeeds.
4. **K4. masm in unwrapped sources.** Recognise `masm`/`raw` in
   `parse_program`, or have `./glon` wrap files as the session facade does.

When a repair lands, its K check reports XPASS and `make machine-integrity`
fails until the check is promoted to an ordinary one, in the same commit as
the repair.
