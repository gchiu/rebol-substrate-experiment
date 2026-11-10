# Glon execution architecture: candidate simplifications and primitives

2026-11-10, branch `glon-shop-g1a`.

**Status: [PROPOSAL]s only. None is authorised, and none is implemented.**
This file is deliberately separate from
[`GLON-EXECUTION-ARCHITECTURE.md`](GLON-EXECUTION-ARCHITECTURE.md), which
records findings and measurements. The evidence labels [M], [R], [D] and [E]
are defined there.

**Objective reminder.** The target is reliable execution for LLM-generated
software: predictable semantics, bounded resources, detectable failures and
an execution model that can be inspected and tested. Preserving seven
primitives and optimising human-facing syntax are not objectives.

## Order of consideration

The order below follows expected impact on reliability and cost, not ease of
implementation.

| # | Candidate | Kind | Evidence in the report | Main risk |
|---|---|---|---|---|
| 1 | Compile-once representation of function bodies | evaluator | Dynamic lookup is 21.7–29.1% of execution; evaluator is 45.0% of runtime code; fib is 3,697 vs 40 instructions per activation | Changes the evaluation model and loader; must preserve binding and escape laws |
| 2 | Pre-resolved bindings and a compact activation frame | evaluator / memory | fib spends 20.7% in closure call/return; activations are ~97 cells | Interacts with closures, stack-local contexts and the escape laws |
| 3 | Stack maps, or a separate activation stack | memory / GC | Conservative scan is 16.9–43.7% of GC-heavy execution; GC is 27.5% of runtime code | Larger evaluator change; context promotion and GC roots must stay exact |
| 4 | Object-start bitmap for the collector | memory / GC | `mark_closure_inline` walks O(objects) per candidate | Bitmap must stay consistent with allocation, free and coalescing |
| 5 | Checked stack limit in S1 (K1) | S1 primitive | masm can push into code between evaluation steps; K1 is the remaining expected failure | Frozen-S1 change; every stack switcher must set the limit |
| 6 | A B-arm S1 with CALL/EXIT/BRANCH/`>R`/`R>` | S1 primitives | 38.0% of runtime code and 23.3–36.2% of execution in macros; compiled fib 40 → ~10 [D] | At most ~1.3–1.6× overall, so it must be justified by safety and code size, not alone by speed |
| 7 | Smaller activations (contexts sized to bindings) | evaluator / memory | 64-cell context per activation, ~97 cells per activation | Changes when `CONTEXT_FULL` fires; touches context, hash and promotion |
| 8 | Compact call through a per-block stub | assembler only | CALL is 21 cells and EXIT 17; a stub is about 14 cells [E] | Non-frozen assembler change; must keep relocation and labels correct |
| 9 | Load-time cross-block linking | assembler only | masm cannot name another block's code | Trust and identity of shared helpers; no authentication today |
| 10 | Stack-shuffle primitives (SWAP/OVER/ROT) | S1 primitives | Runtime-state variable traffic is 23.2–41.7% of execution; SWAP is derived through scratch cells | Adds machine surface and a frozen change; measure against the memory-variable cost first |
| 11 | Bulk byte/string host or primitive operations | host / primitive | One byte per cell; string operations dominate some workloads | Object-model and safety implications; may be better as checked libraries |
| 12 | Structured `glon_load` outcome | host ABI | A halted run's garbage top-of-stack can be read as success or failure | Host-ABI change; WASM hosts and tests must move together |
| 13 | Unwrapped/wrapped `func` equivalence | parser / semantics | Top-level `func` gets different site/scope treatment in unwrapped sources | Language-semantics change for many files; needs a behaviour comparison |

## Notes on the highest-impact candidates

### 1. Compile once, not every evaluation

The strongest finding is that Glon re-interprets source blocks on every
evaluation. The direct comparison is a Glon fib activation at 3,697
instructions against a masm activation at 40 [M]. This is not a primitive-count
effect.

A compile-once representation could keep the same language and the same
binding/escape laws while replacing repeated lookup and dispatch with a
prepared body. The design must say:

- what is compiled (function bodies, argument forms, or whole source blocks);
- when binding is resolved, and how the Guard of Binding and escape laws are
  preserved;
- how closures capture contexts without making the compiled form
  activation-specific;
- what the loader heap holds after compilation.

This is an evaluator and loader change, not a request for new S1 opcodes. It
should be evaluated before a new machine is designed.

### 2. Make activations precise

Stack-local contexts on the return stack are the reason the collector must
scan conservatively. Stack maps or a separate activation representation would
allow exact roots and remove the O(objects) walk. That improves both GC cost
(27.5% of runtime code; 25.8–93.0% of execution in GC-heavy workloads) and
the reliability argument for generated code: an exact root set is easier to
reason about than a conservative scan.

`GLON-ARCHITECTURE-MEMORY.md` §9 already contains a smaller-activation
proposal. It should be considered together with activation placement, not as
an isolated capacity tweak.

### 3. Checked stacks before more primitives

K1 is a correctness gap: a masm word can push into code between evaluation
steps. The only complete fix is a bound in S1's `push`, or a redesigned stack
representation. Any additional primitive set should not be attempted before
this question is settled, because a new CALL/RETURN macro still runs on the
same unchecked stack.

### 4. The B arm is a bounded experiment

The five-opcode CALL/EXIT/BRANCH/`>R`/`R>` arm is worth building only as an
experiment. The measured macro share bounds the overall instruction saving at
23.3–36.2% before subtracting primitive dispatches; the report therefore
predicts at most about 1.3–1.6× overall, not 2×. Code would fall by about
33.6% of the runtime [D]. The low-level fib case (40 → ~10 [D]) is not a
workload-level prediction.

If the B arm is built, it must:

- be a separate experimental machine/build, leaving frozen S1 and the main
  build untouched;
- run the same workloads and language tests;
- preserve the binding and escape laws, K1's behaviour, and the host ABI;
- report code cells, executed instructions and safety behaviour separately.

### 5. Keep masm a validated library tool, not the generated-code target

K6 and K7 now make malformed masm a parse error. That is the right boundary
for trusted libraries. For LLM-generated software, the safer target is Glon
code plus checked libraries; generated code should not need the unrestricted
masm trapdoor. If generated masm is ever supported, it needs an explicit
capability and validation policy, not just better error messages.

## What is not proposed

- No change to frozen S1, the memory map, the host ABI or applications is made
  by this document.
- No capacity is raised.
- No syntax or human-facing convenience feature is proposed.
- No primitive is added.
- No existing [DECISION] is reopened. Any proposal that touches one must cite
  the entry and wait for approval.

## Next evidence needed

1. Build the B arm in an experimental worktree and measure the same six
   workloads.
2. Prototype a compile-once body representation with the existing binding and
   escape-law tests, and measure fib and one application workload.
3. Measure the exact-root cost of stack maps or a separate activation stack
   against the conservative scan.
4. Resolve K1 with a checked-push design before committing to any new control
   primitive.
