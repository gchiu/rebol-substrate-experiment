# GLON B6 — structured validation and repair

**Status:** isolated experiment, 2026-11-10, branch `glon-shop-g1a`.
B1–B5, frozen S1, production Glon, memory layout, host ABI and applications are
unchanged. No new native primitive was added.

## Question

Can textual Glon with immediate structured validation and precise diagnostics
match tagged B's first-attempt reliability while keeping textual Glon's lower
generation cost?

B5 established the starting point:

| Measure | A textual | B tagged |
|---|---:|---:|
| First-attempt success | 3/12 | 11/12 corrected |
| Final success | 12/12 | 12/12 |
| Repairs | 12 | 2 |
| Model tokens | 76,685 | 161,245 |
| Provider cost | $0.042250 | $0.092908 |
| Process time | 523.75 s | 994.47 s |

B5's corrected B result changed only identifier **documentation** from the
original B run, which had 0/12 first attempts. B6 tests whether validation and
repair feedback, rather than a different representation, can improve textual
Glon's total effort and reliability.

## Variants

- **A — textual Glon:** B5's textual compiler and diagnostics. No structured
  validator.
- **B — tagged representation:** B5's corrected tagged reference. Historical
  only; not rerun in B6.
- **D — textual Glon + structured validation:** byte-identical textual syntax,
  semantics and initial prompt as A. After generation a B6 validator reports
  structured diagnostics; if validation passes, the unchanged B5 compiler and
  runner check the program. It never rewrites the artifact.

The A and D initial prompts are byte-identical. This is deliberate: D differs
only in post-generation validation/repair feedback. Raw first-attempt
correctness therefore cannot be improved retroactively by D's validator.

## What B3/B5 already validate

The existing textual compiler already catches unknown words, missing
arguments, malformed `either` blocks, missing top-level expressions,
unbalanced/extra delimiters, duplicate definitions, parameter limits and
unsupported forms. B5 A first-attempt failures were mainly missing top-level
calls and malformed `either`/delimiter structure. B5 diagnostics were useful
messages but did not expose a machine-readable category, location,
expected structure and observed structure.

## D validator

B6 adds a lightweight pre-validator (`b6_verify.py`, 335 Python lines) for the
same subset. It does not introduce a new IR or execution semantics; the B5
compiler remains authoritative for code generation and runtime checks.

It reports:

- **missing_top_level** — definitions but no top-level expression.
- **malformed_block / unbalanced_delimiter / unexpected_token** — missing or
  extra `[`/`]`, unterminated blocks.
- **invalid_control_flow** — malformed `either`, missing then/else blocks,
  unsupported control forms.
- **invalid_function_reference** — unknown function/word.
- **argument_count** — a call lacks required arguments.
- **expected_expression / empty_block** — missing expression or empty block.
- **invalid_definition** — malformed/duplicate functions or parameters.

Diagnostics include:

```
category: missing_top_level
location: line 2, column 1
expected: top-level expression
observed: end of file
message: no top-level expression after function definitions
```

The validator runs before B5 compilation. If it passes, B5 checks
semantics/runtime; if it fails, the model receives the structured diagnostic
for repair. No reference solution is supplied.

## Trial design

- Same 12 B5 tasks and specifications.
- OpenCode 1.18.35, `deepseek/deepseek-flash`, default variant and provider
  temperature, 900 s adapter timeout.
- Fresh temporary project and session per call; all tools disabled; no
  repository/reference access.
- One initial generation and up to three repairs.
- Seeds **20261112** and **20261113**; randomised order in each
  `run-config.json`.
- A and D initial prompts byte-identical.
- B is the B5 corrected historical reference; C was not rerun.

## Results

Matched A/D aggregate over 24 trials each (two seeds):

| Measure | A | D |
|---|---:|---:|
| First-attempt success | 6/24 | 5/24 |
| Final success | 23/24 | **24/24** |
| Attempts / repairs | 50 / 26 | 51 / 27 |
| Input / output / reasoning tokens | 17,881 / 2,509 / 166,950 | 19,221 / 2,487 / 121,181 |
| Total model tokens | 187,340 | **142,889** |
| Provider cost | $0.104358 | **$0.077084** |
| Model-step time | 1,015.90 s | 748.83 s |
| Process time | 1,244.96 s | 984.92 s |
| Verification time | 4.62 s | 4.52 s |
| Validation time | 0 s | **0.0078 s** |
| B5 verify time | 0.750 s | 0.302 s |
| Cost / attempted task | $0.004348 | $0.003212 |
| Cost / successful task | $0.004537 | $0.003212 |
| Tokens / successful task | 8,145 | 5,954 |

Per seed:

- Seed 20261112: A first 3/12, final 12/12; D first 3/12, final 12/12.
- Seed 20261113: A first 3/12, final 11/12 (`choose` failed); D first 2/12,
  final 12/12 (`choose` repaired).

## Diagnostics and error patterns

D first-attempt failures were:

- 15 `missing_top_level`
- 2 `invalid_control_flow`
- 1 `invalid_function_reference`
- 1 `expected_expression`
- 5 passes

All 19 D first-attempt failures were caught before execution by the validator;
none reached a B5 compiler/semantic failure. Across all 51 D attempts, the
categories were pass 24, missing_top_level 15, invalid_function_reference 5,
invalid_control_flow 4, expected_expression 3. No validator false-rejection was
observed: all 24 final D artifacts passed.

A had 18 compiler/semantic first failures and 6 successes. A's final failure
was `choose` in seed 20261113 after four attempts; D's `choose` was repaired
successfully in that seed.

**Diagnostic effect:** D repairs did not use fewer attempts (27 vs 26), but they
used substantially fewer reasoning tokens and cost. Validation overhead itself
was negligible (0.0078 s total); the B5 compile+run time was also lower because
invalid artifacts were rejected before compilation.

## Attribution: why B was reliable first time

B5 B original (function-ID documentation absent) had **0/12** first-attempt
success; B corrected, changing only documentation, had **11/12**. The binary
encoding, decoder and execution semantics did not change. This is strong
evidence that the documented identifier rules—not validation—explain most of
B's first-attempt improvement.

D, with the same textual prompt as A, had 5/24 first successes versus A's 6/24.
Post-generation validation cannot make the initial artifact more likely to be
correct. Any remaining advantage of B's explicit semantic roles over
well-documented text is **not separated** from documentation by B5/B6. Testing
that would require a separate prompt-level text variant with explicit role
rules, not another binary representation. No such variant was implemented.

## Hypothesis verdict

The central hypothesis is **partially supported**:

- **Not supported for first-attempt reliability.** D did not improve raw first
  attempts over A.
- **Supported for total effort and final reliability.** D reached 24/24 final
  successes at 24% fewer tokens, 26% lower cost and 21% less process time than
  matched A, and was much cheaper than historical corrected B. The mechanism is
  cheaper, more actionable repair, not better first generation.

These are pilot results: 24 matched A and D trials across two seeds, one model,
and 12 small tasks. B is historical across a different seed/settings. No
statistically established superiority is claimed.

## Limitations

- B and C were not rerun; B is historical and C is not part of B6.
- Two seeds, one model/variant, 12 small integer-subset tasks.
- The validator accepts the documented subset; valid alternate syntax outside it
  could be rejected.
- Source/target complexity, compile speed and source bytes are not optimisation
  targets here; total LLM effort is.
- Process time includes OpenCode startup; model-step time is separate.
- No reference implementation or fixture generator was exposed to the model.

## Reproduce

From the repository root:

```
make -C experiments/glon-authoring-b6 test
make -C experiments/glon-authoring-b6 analyze
make -C experiments/glon-authoring-b6 sanitize
```

Raw trials are in `trials/AD-seed-20261112/` and `trials/AD-seed-20261113/`;
aggregate analysis is in `trials/analysis.md` and `trials/analysis.json`.
Summary results are in [`results.txt`](results.txt).

## Files

| File | Purpose |
|---|---|
| `b6_verify.py` | D validator and A/D verification wrapper |
| `b6_selftest.py` | validator positive/negative tests |
| `analyze_b6.py` | two-seed A/D aggregation |
| `generation/` | OpenCode adapter, trial runner and suite |
| `tasks/` | 12 B5 task specifications only |
| `trials/` | raw JSON, artifacts, diagnostics, usage/cost and analysis |
| `results.txt` | recorded aggregate results |
