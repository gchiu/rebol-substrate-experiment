# GLON B5 — LLM program generation and repair

**Status:** isolated experiment, 2026-11-10, branch `glon-shop-g1a`.

**Trials executed, including a follow-up.** The original A run and B run
completed all 12 tasks. The original C run was invalidated by 180 s adapter
timeouts. The follow-up corrected B's identifier documentation and reran B and
C with a 900 s timeout; follow-up C had zero timeouts but remained at 0/12 final
successes. Original and follow-up evidence is preserved separately under
`trials/`.

B1–B4, frozen S1, production Glon, memory layout, host ABI and applications are
unchanged.

## Question

Measure how effectively an LLM can create and repair working programs using
the three B4 representations: A textual Glon, B semantically tagged words, and
C direct machine words, minimising development effort and cost while preserving
correctness.

B5 separates compiler/benchmark measurements from genuine model-session
measurements. The latter were run with OpenCode 1.18.35 and
`deepseek/deepseek-flash`; C was interrupted by an adapter timeout, so its
results are not interpreted as model failure or success.

## Task suite

Twelve new tasks, all expressible in all three representations. They are not
the B4 benchmark programs.

| Task | Exercises | Expected |
|---|---|---:|
| `tri` | recursion, arithmetic, n<=0 base | 55 |
| `pow2` | recursion, multiplication | 1024 |
| `min3` | multiple arguments, nested branching | 4 |
| `clamp` | multiple arguments, high-bound edge | 10 |
| `sign_neg` | negative input, branching | -1 |
| `range_sum` | two-argument recursion, accumulator | 42 |
| `choose` | two-way recursion, binomial base cases | 15 |
| `is_even` | recursion by subtraction, parity edge | 1 |
| `ack` | deep recursion, multiple arguments | 9 |
| `quad` | nested calls, forward reference | 20 |
| `median3` | multiple arguments, nested comparisons | 7 |
| `max_neg` | three negative inputs, comparisons | -4 |

Each task has:

- a specification in `tasks/<name>.md`;
- A/B/C artifacts (`<name>.glon`, `.b4b`, `.b4c`);
- an independent reference implementation in `b5_main.c`;
- generated-code identity, stack-balance and determinism checks.

Reference implementations are not used by `tools/gen_tasks.py` and are never
included in generation prompts. The generator only emits task artifacts and
specifications.

Negative inputs are represented as subtraction from zero in the fixtures, so
all three representations lower to identical code and no representation is
silently simplified. Edge cases include a non-positive recursion base, high
clamp bound, negative sign, binomial base cases and negative maxima.

## Representations

B5 uses the B4 formats and B4 front-ends unchanged:

- **A**: textual Glon, B3 compiler.
- **B**: semantically tagged 32-bit words, B4 decoder/emitter.
- **C**: direct machine-oriented 32-bit words, B4 assembler with labels and
  relocation.

All target B3 convention A with CALL, RET, >R, R>, R@ and BRANCH. Format
documentation supplied to a model is in `generation/prompts/`.

## Correctness and reference measurements

All 12 A/B/C artifacts generate identical code cells and instruction counts:

| Task | Cells | Instructions | Result | Data/return stack |
|---|---:|---:|---:|---:|
| tri | 34 | 175 | 55 | 12 / 22 |
| pow2 | 47 | 235 | 1024 | 12 / 22 |
| min3 | 108 | 45 | 4 | 3 / 4 |
| clamp | 74 | 41 | 10 | 3 / 4 |
| sign_neg | 49 | 19 | -1 | 2 / 2 |
| range_sum | 53 | 219 | 42 | 10 / 24 |
| choose | 93 | 1,011 | 15 | 7 / 18 |
| is_even | 46 | 115 | 1 | 2 / 12 |
| ack | 90 | 1,253 | 9 | 10 / 30 |
| quad | 22 | 24 | 20 | 2 / 4 |
| median3 | 162 | 55 | 7 | 3 / 4 |
| max_neg | 120 | 51 | -4 | 4 / 4 |

Reference artifact source sizes:

| Measure | A text | B tagged | C direct |
|---|---:|---:|---:|
| Bytes total / median | 1,193 / 94.5 | 1,592 / 122 | 2,604 / 192 |
| Tokens total / median | 425 / 32 | 386 / 29.5 | 639 / 47 |

Token count: A lexical tokens; B/C 32-bit payload words excluding the magic.
These are **not** model tokens.

Reference source-processing medians from one benchmark round: A 53.32 µs,
B 0.38 µs, C 0.36 µs. Execution is identical because the code is identical;
small timing differences are noise. These figures describe the reference
fixtures, not LLM-generated programs.

## Tests and verification

- B1: 31 checks.
- B2: 32 checks.
- B3: 33 checks.
- B4: 21 checks.
- B5: 26 checks, covering 12 task results, A/B/C code identity,
  deterministic recompilation, stack balance, and malformed A/B/C inputs.
- ASan/UBSan passes for B1–B5.
- Frozen-S1 check passes.
- B5 task fixtures regenerate deterministically.

## Generation and repair harness

`generation/` contains:

| File | Purpose |
|---|---|
| `README.md` | controlled trial protocol |
| `prompts/A.md`, `B.md`, `C.md` | equivalent representation documentation |
| `opencode_adapter.sh` | OpenCode LLM_CMD adapter; fresh tool-free session per call |
| `opencode_extract.py` | extracts artifacts and token/cost/session metadata |
| `run_trial.sh` | one trial: initial artifact plus up to three repairs |
| `run_suite.sh` | simple shuffled shell scheduler |
| `run_opencode_suite.py` | resumable recording scheduler used for results |
| `analyze_opencode_trials.py` | aggregates trials and failure categories |
| `trial-log-schema.md` | required fields for tokens, cost, time, failures |
| `results.md` | recorded trial results and interruption |

The prompt is the task specification plus one representation's format. It
contains no reference code, no other representation's artifact and no other
trial's repair history. `run_suite.sh` shuffles the order and invokes a fresh
`LLM_CMD` process for each artifact; the model command is responsible for
fresh-session configuration.

For B/C, the model writes whitespace-separated 32-bit hex words and
`tools/hex_to_bin.py` converts them to the little-endian binary artifacts
consumed by the unchanged B4 front-ends. `run_trial.sh` then calls
`b5 --verify TASK A|B|C FILE`, returns compile/runtime diagnostics for repair,
and records the final result. The conceptual B/C representations remain the B4
word streams; the hex interface is only the text-only authoring surface. The adapter uses a fresh temporary project for every call, with all tools
disabled and wildcard-deny permissions. No repository path, reference
implementation or other representation artifact is exposed. It records session
IDs, input/output/reasoning/cache tokens, provider cost and model-step time;
process wall time and `b5 --verify` time are recorded separately. Failure
categories are: invalid encoding, incorrect semantics,
calling-convention/stack error, compiler/assembler limitation, other.

Reported suite command:

```
python3 generation/run_opencode_suite.py --seed 20261110 \
  --out trials/opencode-seed-20261110
```

## Error analysis

A first-attempt failures were textual/syntax or incomplete programs:
`no top-level expression`, malformed `either` blocks, unexpected `]`. All were
repaired. Three of 12 tasks passed first; nine needed one repair; `quad` needed
three.

B first attempts all failed at token 1 with `bad function id`. The B prompt
encodes `FUNC` as `id<<8|arity` but did not say function ids start at 1; the
model repeatedly used id 0. This is a representation-documentation ambiguity,
not purely semantic difficulty. Repairs fixed it in every task. Across all B
attempts there was one incorrect-semantics and one stack-category failure.

C attempts were dominated by invalid encoding/assembly and wrong results:
38 encoding failures and 10 semantic failures over 48 attempts. However
11/12 C trials were timeout-affected, so these counts are not a fair model
comparison. The one C trial without timeouts (`quad`) produced a wrong result.
C diagnostics are mechanical (label id, unknown operation, bad file size) and
did not consistently guide repair to the ABI/stack cause.

## LLM result assessment

Full suite, 12 tasks per representation, model `deepseek/deepseek-flash`,
seed `20261110`:

| Measure | A text | B tagged | C direct |
|---|---:|---:|---:|
| First-attempt success | 3/12 | 0/12 | 0/12* |
| Final success | 12/12 | 12/12 | 0/12* |
| Attempts / repairs | 24 / 12 | 27 / 15 | 48 / 36* |
| Model tokens | 76,685 | 234,217 | 508,700* |
| Provider cost | $0.042250 | $0.133951 | $0.296129* |
| Cost/successful task | $0.003521 | $0.011163 | no valid success* |
| Model-step time | 381.58 s | 1243.01 s | 2867.38 s* |
| Process time | 523.75 s | 1601.24 s | 6332.99 s* |

\*C is **not evaluable**: 11/12 C trials had at least one zero-token adapter
timeout at the 180 s cap. Its 0/12 is an infrastructure-interrupted result, not
a model verdict. One B trial (`ack`) also had one timeout call but still
succeeded; B has 11 fully clean trials and one timeout-affected success.

Among the valid A/B data, A used about one-third of B's tokens and cost, and B
required more repairs. B's all-first-attempt failures are confounded by the
function-id documentation ambiguity. The sample is 12 tasks per representation;
this is a pilot, not proof of general superiority. C was rerun with the
900 s adapter in the follow-up; see the next section.

Final artifact sizes: A 1367 bytes total (median 103.5); B 3093 raw hex bytes /
345 binary words (median 24 words); C has no valid final artifacts.
Verification (compile+run) time was about 0.35/0.33/0.44 s total per
representation. Model-step time is reported separately from total process
time and is not total development time.

## Follow-up trials (seed 20261111)

The follow-up used the same model (`deepseek/deepseek-flash`), default variant
and provider temperature. B used corrected identifier documentation; C used
the 900 s adapter timeout. A was not rerun and remains the original reference.

| Measure | A original | B original | B corrected | C follow-up |
|---|---:|---:|---:|---:|
| First-attempt success | 3/12 | 0/12 | 11/12 | 0/12 |
| Final success | 12/12 | 12/12 | 12/12 | 0/12 |
| Attempts / repairs | 24 / 12 | 27 / 15 | 14 / 2 | 48 / 36 |
| Tokens | 76,685 | 234,217 | 161,245 | 1,036,806 |
| Provider cost | $0.042250 | $0.133951 | $0.092908 | $0.606714 |
| Cost/successful task | $0.003521 | $0.011163 | $0.007742 | no success |
| Model-step time | 381.58 s | 1,243.01 s | 940.24 s | 6,222.26 s |
| Process time | 523.75 s | 1,601.24 s | 994.47 s | 6,366.90 s |
| Verification time | 0.35 s | 0.33 s | 0.13 s | 0.42 s |
| Final artifact bytes | 1,367 | 3,093 | 3,102 | 2,540* |
| Timeout-affected trials | 0/12 | 1/12 | 0/12 | 0/12 |

\*C's 2,540 bytes are last-attempt artifacts, not valid final programs. B
corrected's 3,102 raw hex bytes are 346 binary words.

B original → B corrected, with only the identifier documentation changed:
first-attempt success **0/12 → 11/12**, repairs **15 → 2**, tokens **−31%**,
cost **−31%**, process time **−38%**, final success unchanged at 12/12. The
only corrected first-attempt failure was `max_neg` (incorrect semantics); all
corrected attempts contained no encoding failures. This is a documentation
effect, not a binary/encoding/decoder change.

A original vs B corrected: B's first-attempt reliability was much higher
(11/12 vs 3/12), but A still used less total effort: 76,685 vs 161,245 tokens,
$0.042250 vs $0.092908, and 523.75 vs 994.47 s.

A original vs C follow-up: C completed without timeouts (12/12 evaluable) but
had **0/12 final successes**, consuming 1,036,806 tokens and $0.606714 —
about 13.5× A's tokens and 14.4× A's cost. C failures were 33 invalid
representation/encoding and 15 incorrect-semantics failures over 48 attempts.
A C smoke test on `tri` succeeded after three repairs, but the full `tri` run
failed; model variability is material.

A and B are completed comparisons; C is a completed but unsuccessful
representation result. The 12-task sample is a pilot, not statistical proof.

## Implementation size

- B5 harness: 370 lines, 9,159 bytes text, 224 data.
- B5 fixture generator: 354 lines.
- Reused front-ends: A 8,553 bytes, B 4,559, C 2,505; common machine 5,505.
- 12 tasks in all three representations.

## Limitations

- Original C is incomplete because of the 180 s adapter timeout; use the
  follow-up C run for C evidence. Follow-up C had zero timeouts but 0/12 final
  successes.
- B original has one timeout-affected successful trial; B corrected has none.
- B original's first-attempt rate is confounded by the function-id prompt
  ambiguity; B corrected removes that confounder.
- The suite is a 12-task pilot, not a general productivity proof.
- B corrected remains about 2.2× A's cost despite its much better first-attempt
  rate; token/reasoning cost is not the same as first-attempt reliability.
- The suite is small and restricted to the integer subset; no closures,
  dynamic binding, escape laws, managed values/GC, errors/`judge` or loader
  integration.
- B/C fixtures were prepared by the B5 generator; authoring effort is not
  included in compiler timings.
- Reference code identity means execution differences between representations
  are not measured; only front-end representation effects.
- Negative inputs are encoded as subtraction from zero; edge coverage is the
  listed tasks.
- All execution uses unchecked stacks; K1 remains open.
- No production change is authorised; B6 was not started.

## Reproduce

From the repository root:

```
make -C experiments/glon-authoring-b5 fixtures
make -C experiments/glon-authoring-b5 test
make -C experiments/glon-authoring-b5 measure
make -C experiments/glon-authoring-b5 bench
make -C experiments/glon-authoring-b5 size
make -C experiments/glon-authoring-b5 sanitize
```

Trial evidence and analysis are in [`trials/`](trials/), with the aggregate at
[`trials/opencode-seed-20261110/analysis.md`](trials/opencode-seed-20261110/analysis.md).
Summary results are in [`results.txt`](results.txt).

## Files

| File | Purpose |
|---|---|
| `b5_main.c` | task tests, references, counts, `--verify` |
| `tools/gen_tasks.py` | deterministic A/B/C task generator |
| `tasks/` | 12 task specs and artifacts |
| `generation/` | OpenCode adapter, trial harness and analysis tools |
| `trials/` | raw JSON, artifacts, diagnostics, sessions, tokens and costs |
| `results.txt` | trial summary, failure patterns and interruption |
