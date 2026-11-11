# GLON B8 — execution architecture selection

**Status:** isolated evaluation experiment, 2026-11-10, branch
`glon-shop-g1a`. This is a recommendation, not an approved architectural
change. No production Glon, frozen S1, memory layout, host ABI, application or
B1–B7 source was modified.

## Scope

B8 evaluates the candidate VM primitives and direct-compilation strategy using
the committed B1–B7 evidence plus fresh measurements. It does not add a
primitive, compiler optimisation, syntax or application feature.

The decision criteria are:

- executed VM instructions and wall-clock performance;
- code density;
- compiler and machine implementation cost;
- correctness and stack/memory requirements;
- native/WASM portability;
- reliability of LLM-generated programs.

Fibonacci is included but is not treated as sufficient evidence.

## Evidence review

- **B1:** direct compilation reduced a recursive fib from 30,909,921 evaluator
  instructions to 497,479 S1 instructions (62.1×), with no new primitive.
- **B2:** the six candidates lowered fib 18 from 497,479 to 129,595
  instructions and 153 to 39 cells (all six); CALL/RET/RS were the substantial
  contributors, R@ modest, BRANCH weak.
- **B3:** on convention A, CALL+RET+>R+R> reduced fib wall-clock from 1860 to
  477 µs and all six to 297 µs; R@ added another 1.5×; BRANCH added about 1.05×
  on fib and nothing measurable on sum. Convention A beat B.
- **B4:** different authoring representations can compile to identical VM code;
  source representation and executable representation are separable.
- **B5–B7:** textual Glon with structured diagnostics and a clear completion
  contract is the strongest practical authoring route observed so far. B's high
  first-attempt rate after documentation correction was not reproduced by
  post-generation validation alone.
- **Generated-program analysis:** test-passing is not semantic equivalence. B's
  `ack` artifact is a constant 9, not Ackermann; B's first `max_neg` failure was
  a tagged-decoder sign-extension limitation.

## Workloads

`workloads/` contains ten input variants created by changing only the final
top-level call of existing Glon sources:

- fib 10, 15, 18 (from the B4 fib artifact);
- Ackermann 2 2, 2 3, 3 3 (from B7's genuine textual Ackermann artifact);
- recursive sum 200 0, 500 0 (from the B4 sum artifact);
- choose 6 2, choose 10 5 (from B7's textual binomial artifact).

No function body was rewritten. The Ackermann source is genuine recursion, not
the B5 constant shortcut. See `WORKLOAD-COVERAGE.md` for why existing DEFLATE,
traffic and Kākā workloads could not be compiled without changing them.

## Configurations

Thirteen combinations on the B3 experimental machine, all with convention A:

`none`, `call`, `ret`, `>r`, `r>`, `r@`, `branch`, `>r+r>`, `call+ret`,
`call+ret+branch`, `call+ret+>r+r>`, `call+ret+>r+r>+r@`, `all six`.

All use the same textual source, algorithm, inputs and compiler settings.
Flags only select native versus existing macro lowering.

## Method

- Count build: B3 machine with `B3_COUNT`; deterministic instructions, cells,
  result, stack balance and code hash.
- Timing build: same machine with counters compiled out; 9 execution batches
  targeting 50 ms, 7 compile batches targeting 20 ms. Execution excludes
  compilation and model/startup costs.
- Correctness: every result checked against an independent expected value;
  stack balance checked.
- 130 workload/config measurements; all correct.
- No WASM target exists for the B3 experimental machine, so WASM is N/A rather
  than estimated.
- Raw data: `results/measurements.json`, `results/aggregate.json`,
  `results/coverage.json`, `results/run.log`.

## Reproduce

```
make
python3 tools/make_workloads.py
python3 tools/run_b8.py
python3 tools/check_coverage.py
```

`make` builds `build/bench-count` and `build/bench-fast` from the unchanged B3
compiler and machine. `tools/run_b8.py` runs all ten workloads under all
thirteen configurations.

## Documents

- `PRIMITIVE-AUDIT.md` — semantics, lowering, implementation cost and measured
  contribution of each candidate.
- `WORKLOAD-COVERAGE.md` — existing application and compiler coverage.
- `BENCHMARKS.md` — correctness, instructions, code size, timing and memory.
- `ARCHITECTURE-RECOMMENDATION.md` — adopt/retain/defer recommendation and
  remaining evidence required.

No production adoption is authorised by this document.
