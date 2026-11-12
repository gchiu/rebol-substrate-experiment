# Glon Research Handover — November 2026

**Purpose.** Close the current experimental research phase and give future
coding agents an evidence-based starting point. This is a handover, not a new
architecture proposal. The authoritative decision remains
`GLON-ARCHITECTURE-DECISION-2026-11.md`; this document does not duplicate it.

## 1. What Glon is for

Glon is an experimental compact execution language intended for AI-generated
software: precise semantics, a small stable vocabulary, explicit state,
composability, testability and constrained deployment matter more than
human-friendly syntax. The reference execution core is frozen S1-7:
`LIT DUP DROP @ ! 0BRANCH HOST` on a 65,536-cell machine. Production work keeps
the seven-primitive default; experimental profiles and compilers must be
separately selected and tested.

The project has demonstrated useful applications and bounded evaluator
improvements. It has **not** demonstrated general AI authoring reliability, a
full compiler, browser support for the native profile, or a need to change the
reference machine.

## 2. Evidence register

Status labels: **D** demonstrated, **P** partially demonstrated, **U**
unresolved, **N** not investigated. A passing test is not a general conclusion;
a measured improvement is not automatic system simplification.

| Research area | Status | Evidence and limits |
|---|---|---|
| Minimal machine and semantics | **D** | Frozen S1-7, 65,536-cell map, Alpha laws, extensive runtime/library tests; `check-frozen-s1.sh` passes. |
| Direct compilation (B1–B4) | **D/P** | B1 fib direct compilation removed 62.1× evaluator instructions; B4 showed text/tagged/direct representations can converge to identical VM code. Compiler covers a small subset, not full Glon. |
| Primitive candidates (B2, B3, B8) | **D** | B8's ten integer workloads: five operations removed 69.9% instructions and 62.8% cells; six including BRANCH 71.3%/65.3%. Integer-subset, native-only, no full app, no WASM. |
| AI authoring and repair (B5–B7) | **P** | Text A: 3/12 first, 12/12 final; corrected tagged B: 11/12 first, 12/12 final; B6 validated D: 24/24 final, 24% fewer tokens than A; B7 completion clarification improved first attempts 10→17/24 but total cost worsened. Small pilot, one model, 12 integer tasks; test-passing is not semantic equivalence (B ack constant). |
| DEFLATE implementation | **D** | Unchanged Glon decoder, raw DEFLATE, 164 fixtures/480 runs, 44 units; byte-identical zlib output; F2 fixed code-length fragmentation (480/480). |
| DEFLATE operation attribution (B9–B10) | **D** | Full decoder 284M–2.10B operations: 44–45% literal/stack, 32–34% memory, 16–17% HOST, 6.2–6.5% branch; allocation/GC negligible. No reliable function-name ranking. |
| Native evaluator profile (B11–B13) | **D (native only)** | S1-12 emitted/executed CALL/RET/>R/R>/R@; full DEFLATE 480/480, 44/44; code 14,549→9,933; 1.08–1.20× native speedup. S1-9 uses CALL/RET only; S1-12's extra ops were genuinely executed. No WASM port. |
| Selectable profiles (B13) | **D/P** | Build-time native `glon-s1-7`/`glon-s1-12`; FizzBuzz and both LaTeX suites pass with identical output. DEFLATE is B12. Shop/browser apps require the WASM/DOM host and were not run under S1-12. |
| FizzBuzz | **D** | Existing local worktree source; 100/100 expected lines under S1-7 and S1-12; identical output. |
| LaTeX generator | **D (native CLI)** | Existing v001/v002 suites pass under both profiles; outputs identical. Browser LaTeX page remains S1-7 and deployed. |
| Iterator, lexer, DOM, links | **P** | ITER v001: 77/77 + 28/28 tests, 1,200 differential token walks; DOM: 67/67 + 8/8 + 7/7 + 5/5; native host-backed links extract 449 Wikipedia links with no peak growth by document size. Not parser-integrated, native-only, slower than v004's lexer, loader/code expensive. |
| Native/browser deployment | **D/P** | Public `/latex/` and `/inflate/` pages run S1-7 browser/WASM; `/inflate/` verified in Chrome for samples, errors, binary download and limitations. S1-12 has no browser/WASM support; native-only. |

### Important distinctions

- **Generated-code correctness:** B1–B4/C1 and B8–B12 show kernels and subsets
  compile/execute correctly. **General compiler correctness:** not demonstrated.
- **Test success:** B5–B7 show small-task pilots; a passing test does not prove
  algorithm equivalence (the B5 `ack` shortcut).
- **Measured improvement:** B8/B11/B12 measure instructions, code and time in
  bounded configurations. **General conclusion:** no claim that S1-12 improves
  all Glon applications, browser code or AI authoring.
- **Native success:** B13 native CLI. **Browser success:** existing S1-7 WASM
  pages. They are not interchangeable evidence.
- **Iterator architecture:** a library/protocol result, not a language or VM
  requirement.

## 3. Architectural invariants

These decisions are settled for the current phase:

1. **S1-7 remains the frozen reference and production default.** Do not change
   frozen S1, its semantics or the 65,536-cell map.
2. **S1-12 remains an optional experimental native evaluator profile.** It is
   not promoted; the default `glon` and deployed browser pages remain S1-7.
3. **No additional primitive promotion is justified.** CALL/RET/>R/R>/R@ have
   native evidence, but S1-12 is native-only, experimental and has a runtime
   copy maintenance cost.
4. **No full-language compiler promotion is justified.** Only kernels/subsets
   compile; complete DEFLATE, shop, traffic and Kākā source remains unsupported.
5. **Iterators and traversal remain ordinary Glon library abstractions.** No
   ITERATOR! datatype, `foreach`, loop primitive or evaluator change is needed
   by the evidence.
6. **Browser/WASM compatibility must not be inferred from native results.**
   S1-12 has no WASM port; `/latex/` and `/inflate/` are S1-7.
7. **Known discrepancies stay visible.** The root `s1` traffic test 8 fail-stop
   is unresolved; do not claim the full root suite is green.

## 4. Bounded development backlog

### A. Correctness and reliability

| Item | Why it matters | Evidence | Next action | Completion condition | Architecture? |
|---|---|---|---|---|---|
| Root `s1` traffic test 8 fail-stop | Blocks trusting the full native suite | Earlier rebuild: dump at `IP=8438`, suite timed out; exact layer unknown | Reproduce test 8 alone with current build; isolate harness/runtime/app | `./s1` completes all tests green | No |
| B12/S1-12 runtime-copy maintenance | Profile can drift from production runtime | B12/B13 copy runtime and machine; full tests pass today | Add a diff/check test between copies and production runtime | Automated parity check passes for every intended divergence | No |
| F2 per-header buffer risk | Future fragmentation may break another shape | F2 passes 480/480; one fixed allocation per dynamic header | Add adversarial fragmentation workload; document capacity | New workload passes or documented limitation is explicit | No |
| Iterator/lexer WASM parity | Native-only results do not establish browser behavior | ITER docs: `P`; WASM parity not verified | Run existing iter/lexer/differential suites against CI-built WASM | Same token/output results in WASM | No |
| Shop/browser regression visibility | Existing shop tests are separate from native profile | B13 records host boundary; shop tests are Node/WASM | Keep shop/WASM tests in their own lane; do not claim native | CI lanes documented and green separately | No |

### B. Practical application development

| Item | Why it matters | Evidence | Next action | Completion condition | Architecture? |
|---|---|---|---|---|---|
| Build one real AI-assisted application on existing Glon | Proves value without architecture work | FizzBuzz/LaTeX/DEFLATE run; shop/browser already exists in S1-7 | Choose a small concrete application; use existing libraries/host | Application has executable tests and independent output oracle | No |
| Integrate iterator into a parser only if needed | Measures whether library abstraction pays in practice | ITER is 2× slower than v004 lexer, not integrated; DOM protocol works | Build one parser experiment with token iterator, compare same corpus | Same tokens/errors; acceptable loader/time or documented rejection | No |
| Improve application test ergonomics | AI-generated work needs fast, checkable feedback | B5–B7 structured validation helped repair cost | Formalize existing test/lint/check commands for apps | One documented command reproduces app tests | No |
| Document native profile use | B13 selection is usable but niche | `glon-s1-7`/`glon-s1-12`, FizzBuzz/LaTeX | Keep profile README and examples current | Fresh checkout builds both and runs documented tests | No |

### C. Portability and deployment

| Item | Why it matters | Evidence | Next action | Completion condition | Architecture? |
|---|---|---|---|---|---|
| S1-12 WASM/browser profile | B13 native-only; deployed pages S1-7 | No WASM port; native tests not transferable | Separate approved port; reuse B12 machine/runtime semantics | 480/44 plus browser app tests pass under S1-12 WASM | **Yes** |
| Constrained native deployment | Glon target includes constrained environments | 64K machine, native CLI; no profile portability proof | Measure binary/startup/memory on target; keep S1-7 fallback | Documented target tests and resource envelope | Maybe |
| Browser host boundary for shop apps | Shop is useful but not native | Shop uses DOM/event/WASM host; B13 does not run it natively | Keep S1-7 browser lane; no native substitute | Browser tests remain green; boundary documented | No |
| Deployment repeatability | Existing Pages/demos should remain reproducible | `/latex/`, `/inflate/` published; Pages workflow | Keep CI route checks and recorded URLs | Fresh deployment reproduces public pages | No |

### D. Research questions (non-blocking)

| Question | Why interesting | Evidence | Next step | Completion condition | Architecture? |
|---|---|---|---|---|---|
| Does full compiler coverage pay for real apps? | Could remove evaluator overhead generally | B1/B8/C1/ B9 only subsets/kernels; full sources unsupported | Start only after a concrete app bottleneck; compile one dependency slice | One real app compiled with equivalent tests and measured cost | **Yes** |
| How much does 12 vs 9 matter in production? | B12 shows extra ops matter in DEFLATE | S1-12 4.8–6.4% fewer ops, 7% code vs S1-9; noisy time | Replicate on another real app and target | Reproducible result or profile rejected | **Yes** |
| Reliable LLM authoring for full Glon | Core project purpose | B5–B7 pilots, integer subset, one model | Larger controlled suite across language features | Pre-registered reliability/cost result | **Yes** |
| Iterator performance/loader tradeoff | Existing library alternative | 2× slower; expensive cross-fragment; no parser integration | One parser integration with differential corpus | Acceptable behavior/perf or documented rejection | No |
| 32K/zero-allocation memory profile | Full DEFLATE window or lower heap | 64K map cannot hold 32K byte history | Separate memory-profile proposal; no local tweak | Approved profile with full tests | **Yes** |

## 5. Working discipline for the next phase

1. Begin with a concrete application requirement or reproducible defect.
2. Search existing experiments and implementations before proposing new work.
3. Prefer existing Glon language and library facilities.
4. Preserve frozen S1 and established semantics; do not silently change defaults.
5. Require executable tests and independently checkable outputs.
6. Separate application engineering from architectural research.
7. Propose an architectural experiment only for a specific unresolved question.
8. Give every experiment a hypothesis, baseline, success criterion and stop rule.
9. Do not treat fewer VM instructions as automatically meaning a simpler system.
10. Treat AI-generation reliability and total system complexity as primary design
    objectives.

## 6. Practical starting points

Without reopening architecture questions, an agent can safely:

- build native applications/tests against S1-7 using existing libraries;
- use the B13 build-time S1-12 profile only in native experiments, with the full
  tests as a gate;
- work on the documented correctness and test gaps above;
- use iterator/DOM libraries in native experiments, not as a language change;
- keep browser/WASM work on the production S1-7 path.

Do not start B14, promote S1-12, freeze new primitives, expand the compiler,
change the heap/map or deploy a new profile. The next phase should be driven by
useful applications and demonstrable needs.

## 7. Evidence index

Authoritative repository references: `GLON-ARCHITECTURE-DECISION-2026-11.md`,
`GLON-ARCHITECTURE-MEMORY.md`, `GLON-ALPHA-LAWS.md`, `experiments/glon-deflate/`
(F1/F2 reports), `experiments/glon-deflate-b9` through `-b13`, B1–B8 reports,
`experiments/glon-iter/GLON-ITER-V001.md`, `experiments/glon-dom/GLON-DOM-V001.md`,
`experiments/glon-links-native/README.md`, `experiments/glon-links-multi/README.md`,
`experiments/glon-latex`, the FizzBuzz worktree, and the deployed `/latex/` and
`/inflate/` verification records.
