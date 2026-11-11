# ARCHITECTURE-RECOMMENDATION.md

**This is a recommendation for a candidate architecture. It is not an approved
architectural change, not a production migration plan, and not authorisation to
modify frozen S1.**

## Recommendation summary

### Adopt into the candidate architecture

1. **Direct compile-once execution** rather than per-evaluation interpretation
   for eligible Glon programs. The B1/B3 evidence shows this is the dominant
   gain: fib 18 fell from 30,909,921 evaluator instructions to 497,479 direct S1
   instructions (62.1×) with no primitive change.
2. **Native CALL, RET, >R, R> and R@.** These five additions plus the original
   seven define a **twelve-operation candidate machine**. In the ten B8
   workloads, CALL+RET+>R+R> alone removed 62.6% of executed instructions and
   57.3% of emitted code versus the original lowering; adding R@ removed a
   further 19.4% of instructions and 12.9% of code relative to that core.
3. **Convention A: return-stack parameters.** B3 measured A ahead of the
   data-stack alternative once native call/return/stack operations existed;
   B6/B7 text does not provide a reason to change it.
4. **Textual Glon plus structured validation and a clear completion contract**
   as the authoring strategy. B4 showed representations can converge to the same
   VM code; B5–B7 showed structured diagnostics and completion wording reduce
   repair work. Native tag encodings did not show an execution advantage once
   programs converged.

### Retain experimentally

- **BRANCH.** It is correct and cheap, but its incremental benefit in B8 was only
  about 4.7% of aggregate instructions, 6.6% of code, and its wall-clock effect
  is noise-level on some workloads; B3 found about 1.05× on fib and essentially
  zero on sum. Keep it as an experimental option for branch-heavy programs, not
  part of the minimum candidate.

### Defer / reject

- **Byte-addressing or string primitives** are not justified yet. The only
  memory-heavy candidate (DEFLATE) cannot be compiled, so there is no B8
  evidence for them.
- **The full thirteen-operation machine is not selected.** The extra twelfth
  operation (BRANCH) lacks sufficient independent benefit.
- **Checked stacks / K1 closure** are a correctness requirement, not a
  performance primitive. They require a separate frozen-S1 or machine decision;
  none of the candidate native ops adds bounds checks.

## Why not simply the seven-primitive machine?

The seven-primitive machine's call/return/stack macros are a large, repeated
tax in compiled code. Across the B8 suite, the four call/return/stack operations
reduced executed instructions by 62.6% and code cells by 57.3%. That is not a
Fibonacci-only effect: fib, Ackermann, sum and binomial recursion all benefited.
The implementation cost is a modest fixed machine expansion (six candidate
opcodes, +688 text bytes over frozen S1 in the B3 counter copy), not new
per-program runtime state.

## Why not all six/ thirteen?

R@ earns its place in this ABI; BRANCH does not yet. The B8 branch-sensitive
workloads (`choose`, `either`-heavy tasks) show the branch effect, but `sum`
shows almost none and B3's fib gain was small. The smallest set meeting the
demonstrated requirements is therefore **S1 + CALL + RET + >R + R> + R@**.

The six-op B2 machine remains valuable as an experimental superset, but it is
not the recommended minimum.

## Compiler strategy (separate from primitive set)

- Direct, compile-once textual compilation for supported programs.
- Preserve the B1/B3 argument/return convention A.
- Keep the validator/structured-diagnostics layer; it is an authoring
  reliability tool, not a code generator or rewriting pass.
- Keep the completion contract explicit for benchmark/executable tasks; do not
  change Glon semantics to make the harness easier.
- Do not treat B5's tagged representation as an execution advantage: B4 and the
  generated-program analysis show representation convergence and at least one
  semantic shortcut (`ack` constant) that test passing did not catch.

## Application impact now

No full existing application compiles with B3. Immediate beneficiaries are the
B4/B5/B6/B7 subset programs and similar integer-only kernels. DEFLATE, traffic
and Kākā are blocked at **compiler/runtime coverage**, not at the primitive set:

- set-words/locals, blocks, strings, closures, `do`/`invoke`/`block-at`,
  errors/`judge`, `/`, masm and module wrappers are unsupported;
- traffic/Kākā also require browser host interfaces and host events;
- DEFLATE's bit/Huffman/LZ77 work cannot be benchmarked without rewriting it,
  which B8 forbids.

## Trade-offs assessed

- **Native calls/returns:** substantial instruction and code savings; acceptable
  machine complexity; no new memory; not required for correctness.
- **Native return-stack operations:** substantial in call-heavy code; same
  stacks; no safety change.
- **Native R@:** useful in the current return-stack ABI, but ABI-dependent;
  adopt as a candidate, not as a universal primitive.
- **Native BRANCH:** cheap but weak measured benefit; retain experimentally.
- **Compiler improvements:** coverage of locals, managed values, strings and
  errors would unlock real applications and is a higher priority than adding
  more primitives.
- **Memory/stack:** peak return-stack use is dominated by recursion depth
  (1,503 cells in the B8 suite), not by native control ops; data stack falls
  slightly. The fixed 65,536-cell map remains practical for the measured
  subset.
- **Native/WASM:** no B3 WASM port; native/WASM equivalence for these opcodes
  is **N/A**. Production S1's WASM parity does not automatically validate the
  B3 machine.

## Minimum remaining evidence before freezing a candidate

1. Bring at least one real application kernel (DEFLATE bit/Huffman or a traffic
   update) into a compiler-supported form **without changing its algorithm**,
   or implement the required compiler/runtime feature and then benchmark it.
2. Measure CALL/RET/RS/R@/BRANCH on string/block/GC-heavy programs and on
   branch-heavy code; confirm R@ and BRANCH effects are not artifacts of the
   current ABI/workloads.
3. Port the experimental machine to WASM or prove semantic equivalence with a
   WASM build; B8 has no such result.
4. Define and test stack bounds/K1; current native ops preserve unchecked S1
   semantics.
5. Measure full application traces with host/render time separated from VM
   execution.
6. Establish a larger, multi-seed workload suite and multiple task families;
   B8's ten integer-subset inputs are a pilot.

## Explicit non-actions

B8 did not modify frozen S1, production Glon, applications, the host ABI,
compiler semantics, or B1–B7. No primitive, syntax or optimisation was added.
No migration is authorised. The next step, if any, is a separately approved
experiment addressing the evidence gaps above.
