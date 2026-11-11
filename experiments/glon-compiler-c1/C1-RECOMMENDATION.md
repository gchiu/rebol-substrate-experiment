# C1-RECOMMENDATION.md

## Achievement level

**Level 3 achieved.**

- Level 0 (coverage audit): yes, see `DEFLATE-COVERAGE.md`.
- Level 1 (compiler/runtime design): yes, see `COMPILER-DESIGN.md`.
- Level 2 (existing kernel successfully compiled): yes, `hf/left` is extracted
  unchanged and compiles on the twelve-operation candidate.
- Level 3 (compiled kernel passes independent correctness tests): yes, 10/10
  vectors and runtime invariants, matching production Glon and an independent
  reference.
- Level 4 (equivalent original and compiled execution paths benchmarked): **not
  claimed**. Production Glon was used for correctness, but its CLI/startup/
  evaluator path was not isolated for execution-only timing. Candidate vs
  seven-op compiled paths were benchmarked.

## Findings

1. **A genuine, unchanged DEFLATE function can run on the candidate machine
   without a new VM primitive.** `hf/left` uses recursion, three parameters,
   block access and integer arithmetic; it executes under CALL/RET/>R/R>/R@.
2. **The compiler needed only a small vertical slice:**
   - function-only modules and selecting a named function entry;
   - a `block-at` intrinsic lowered with existing `LIT`, `HOST DIV`,
     `HOST ADD` and `@`.
   No set-words, locals, strings, closures, errors or host features were needed.
3. **The return-stack ABI remains suitable for this kernel.** Peak stack was
   6 data / 64 return cells; recursion depth is bounded and balanced.
4. **The kernel is ~1.97× fewer instructions and 1.78× smaller function code
   under the five candidate operations** than under the seven-operation direct
   lowering. Measured aggregate execution was about 1.65× faster in this noisy
   sample, not a whole-decoder claim.
5. **No new VM primitive was necessary.** The 12-operation candidate suffices
   for this vertical slice.
6. **The full DEFLATE decoder remains unsupported.** It requires compiler and
   runtime coverage for locals/set-words, division, real blocks/strings,
   closures, `do`/`judge`/`invoke`, `s/`, masm, errors and managed memory/GC.
7. **The 479/480 fragmentation fail-stop is unchanged and unrelated to this
   kernel.** No fix was attempted.

## Recommendation for C2

A focused next step should extend the same compiler/runtime path in dependency
order, without changing DEFLATE:

1. **Set-words/locals and division**, then compile and test `hf/walk` (Huffman
   symbol decoding) unchanged. This is the next smallest real kernel and adds
   local binding without strings/closures.
2. **A real block representation / header ABI**, replacing C1's harness-only
   raw handle, and support for block literals or loader-created blocks.
3. **Strings/bytes and managed allocation**, then the bit reader and Huffman
   table construction (`hf/build`, `br/make*`).
4. **Closures, `invoke`, `do`/`judge`, and SIN! errors**, required by the
   decoder state machine and resumption.
5. **LZ77/window copying and GC ownership**, then full-decoder conformance.
6. Keep the twelve-operation candidate and convention A unless a measured
   kernel shows a concrete need for another primitive.
7. Preserve and revisit the 479/480 fragmentation failure as a separate
   runtime/heap issue; do not conflate it with compiler translation.

## Status

This is an experimental feasibility result, not an approved production change.
No frozen S1, production Glon, DEFLATE source, B1–B8, host ABI or application
was modified. Architecture remains unfrozen; explicit approval is required
before C2 or any production migration.
