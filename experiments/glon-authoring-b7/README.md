# GLON B7 — program completion contract

**Status:** isolated experiment, 2026-11-10, branch `glon-shop-g1a`.
B1–B6, frozen S1, production Glon, memory layout, host ABI and applications are
unchanged. No parser, validator, VM, primitive set, compiler or language
semantics changed.

## Question

B6 showed structured diagnosis reduced total effort but did not improve
first-attempt correctness. Its dominant first-attempt failure was
`missing_top_level` (15 of 19 failures in the structured arm). B7 tests whether
that is a documentation/benchmark-contract problem rather than a deficiency of
textual Glon.

## Semantics finding

**Definitions-only Glon is a valid loaded module.** A source containing only
`f: func [x] [ x ]` runs through production `./glon` with exit status 0 and
returns `#[func]`. Production modules such as `jupyter/prelude.glon` and
`demo/shop/strings.glon` are definition modules with no top-level invocation.

**The benchmark requires an invocation to obtain a result.** The B5/B6
benchmark compiler (`b3_compile`) is a standalone-entry compiler: it requires a
top-level expression, emits it as the entry and `HALT`s. It has no module mode.
The tasks specify a top-level call and expected value; without it the harness
has no result to test.

So `missing_top_level` was a **benchmark submission/execution contract**
failure, not a Glon language requirement. B7 does not change language
semantics to accommodate the benchmark.

## Conditions

- **A — existing documentation.** B6 textual prompt and the same structured
  validator/repair feedback as B6 D. *No changes.*
- **E — clarified completion contract.** Byte-identical to A except for the
  paragraph below. Same validator, verifier and execution semantics.
- **B** — B5 corrected tagged representation, historical reference only.
- **C** — B5 result, not rerun.

Exact A→E addition:

> The benchmark submission must include the specified top-level invocation
> after the function definitions. Definitions alone are a valid Glon module,
> but this benchmark executes the final top-level expression to obtain its
> result; a definitions-only artifact does not produce a test result.

No syntax, semantic role, example or reference implementation was added. The
exact diff is in `generation/prompts/DIFF.md`.

## Method

- Same 12 B5/B6 tasks.
- OpenCode 1.18.35, `deepseek/deepseek-flash`, default variant and provider
  temperature, 900 s adapter timeout.
- Fresh temporary project/session per model call; all tools disabled; no
  repository/reference access.
- One initial generation and up to three repairs.
- Same structured validator for A and E; the initial prompt is the only
  condition difference.
- Seeds **20261114** and **20261115**; **24 A + 24 E** matched trials.
- No adapter timeouts; all trials terminated below 900 s.

## Results

| Measure | A | E |
|---|---:|---:|
| First-attempt success | 10/24 | **17/24** |
| Final success | 24/24 | 24/24 |
| Attempts / repairs | 41 / 17 | **36 / 12** |
| Missing-top-level first failures | 8 | **0** |
| Input / output / reasoning tokens | 14,636 / 1,948 / 64,609 | 14,913 / 1,908 / 103,428 |
| Total model tokens | **81,193** | 120,249 |
| Provider cost | **$0.042130** | $0.065439 |
| Model-step time | 416.48 s | 649.83 s |
| Process time | 597.20 s | 792.88 s |
| Verification time | 3.02 s | 2.80 s |
| Validation time | 0.0050 s | 0.0049 s |
| Cost / attempted task | $0.001755 | $0.002727 |
| Cost / successful task | $0.001755 | $0.002727 |
| Tokens / successful task | 3,383 | 5,010 |

Per seed:

| Seed | Condition | First | Final | Attempts | Tokens | Cost |
|---|---|---:|---:|---:|---:|---:|
| 20261114 | A | 4/12 | 12/12 | 20 | 37,471 | $0.019369 |
| 20261114 | E | 10/12 | 12/12 | 16 | 38,781 | $0.020400 |
| 20261115 | A | 6/12 | 12/12 | 21 | 43,722 | $0.022760 |
| 20261115 | E | 7/12 | 12/12 | 20 | 81,468 | $0.045039 |

Seed 2's E aggregate is dominated by one `choose` trial: 4 attempts, 48,460
tokens, $0.028068. Seed 1 A/E total costs were nearly equal.

## First-attempt failures

| Category | A | E |
|---|---:|---:|
| missing_top_level | 8 | 0 |
| invalid_control_flow | 4 | 6 |
| expected_expression | 1 | 0 |
| invalid_function_reference | 1 | 1 |
| pass | 10 | 17 |

The clarification eliminates missing-top-level failures entirely. E's
remaining first-attempt errors are control-flow structure and one invalid
reference. No final failure, timeout, or new severe mode appeared; both
conditions finished 24/24.

## Analysis answers

1. **Missing top-level:** reduced **8 → 0**.
2. **First-attempt correctness:** improved **10/24 → 17/24** (seed 1 4→10,
   seed 2 6→7).
3. **Total tokens/cost:** **worse overall**: 81,193/0.042130 → 120,249/0.065439.
   Seed 1 was essentially neutral; seed 2 was much worse because of one
   reasoning-heavy `choose` trial.
4. **New failure modes:** no new final/timeout failure; E shifted remaining
   first failures toward malformed control flow (6 vs 4).
5. **Consistency:** first-attempt improvement appears in both seeds; total-cost
   effect is not consistent and is dominated by one task in seed 2.
6. **Language vs benchmark:** the completion rule is a **benchmark submission
   contract**, not a Glon language contract. Definitions-only is a valid module.

## Hypothesis verdict

**Partially supported.**

- The clarification is an effective fix for the specific
  `missing_top_level` failure, and it raises first-attempt success in both
  seeds.
- It does **not** reduce total token/cost in the two-seed aggregate; E spent
  more reasoning tokens and had one large outlier despite fewer attempts.
- Final correctness was unchanged at 24/24; E needed fewer attempts and
  repairs (36/12 vs 41/17), but at higher aggregate cost.
- B5's corrected B remains the historical first-attempt reference (11/12), but
  B was not rerun in B7. B5's own documentation-only correction showed that
  representation/documentation can move first-attempt reliability from 0/12 to
  11/12; B7 confirms that completion documentation specifically removes
  missing-entry failures, but does not by itself make text equal to B's
  historical rate or lower total cost.

No winner is selected: the effects are mixed. A larger/more stable sample would
be needed to determine whether the seed-2 outlier is typical or a variance
event.

## Historical references (not controlled)

- B5 A original: first 3/12, final 12/12, 76,685 tokens, $0.042250.
- B5 B original: first 0/12, final 12/12, 234,217 tokens, $0.133951.
- B5 B corrected: first 11/12, final 12/12, 161,245 tokens, $0.092908.
- B6 D: first 5/24, final 24/24, 142,889 tokens, $0.077084.
- B7 A/E are fresh matched seeds; historical rows use different seeds/settings.

## Limitations

- Two seeds, 24 trials per condition, one model/variant, 12 small tasks.
- Total-cost result is mixed and not statistically established.
- The completion rule is benchmark-specific; no language semantics changed.
- A/E share validator feedback; only the initial prompt differs.
- B/C were not rerun; B historical.
- Validator covers the documented subset; alternate valid syntax could be
  rejected.
- No reference implementation or prior successful artifact was exposed.

## Reproduce

From the repository root:

```
make -C experiments/glon-authoring-b7 test
make -C experiments/glon-authoring-b7 analyze
make -C experiments/glon-authoring-b7 sanitize
```

Raw trials are in `trials/AE-seed-20261114/` and `trials/AE-seed-20261115/`;
aggregate analysis is in `trials/analysis.md`/`analysis.json`.

## Files

| File | Purpose |
|---|---|
| `SEMANTICS.md` | completion-semantics investigation |
| `b7_verify.py` | structured validator + B5 verifier for A/E |
| `b7_selftest.py` | validator positive/negative tests |
| `analyze_b7.py` | two-seed A/E aggregation |
| `generation/` | prompts, adapter and matched trial suite |
| `tasks/` | 12 task specifications only |
| `trials/` | raw events, artifacts, diagnostics, usage/cost and analysis |
| `results.txt` | recorded aggregate/per-seed results |
