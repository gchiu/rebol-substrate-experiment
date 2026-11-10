# GLON B5 — LLM program generation and repair

**Status:** isolated experiment, 2026-11-10, branch `glon-shop-g1a`.

**No LLM trials were run.** No LLM was available through this development
environment, and B5 does not simulate trials with fixture generators. The
committed work is the task suite, independent tests, prompts and generation/
repair harness. No first-attempt success, repair rate, token, cost, elapsed
development-time or LLM-productivity claim is made.

B1–B4, frozen S1, production Glon, memory layout, host ABI and applications are
unchanged.

## Question

Measure how effectively an LLM can create and repair working programs using
the three B4 representations: A textual Glon, B semantically tagged words, and
C direct machine words, minimising development effort and cost while preserving
correctness.

The objective cannot be answered without genuine model sessions. B5 therefore
separates:

- **compiler/benchmark measurements**, which are reported here;
- **LLM authoring measurements**, which remain unperformed.

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
| `run_trial.sh` | one trial: initial artifact plus up to three repairs |
| `run_suite.sh` | randomises task × representation order and logs trials |
| `trial-log-schema.md` | required fields for tokens, cost, time, failures |
| `results.md` | records that no trial was run |

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
word streams; the hex interface is only the text-only authoring surface. The schema separates
model tokens, prompt/documentation input cost, generation time, compile time and
execution time. Failure categories are: invalid encoding, incorrect semantics,
calling-convention/stack error, compiler/assembler limitation, other.

Example use, if a model command exists:

```
LLM_CMD='model-cli --stdin' generation/run_suite.sh /tmp/b5-trials
```

## Error analysis

**Not performed.** No failures from LLM artifacts exist to classify. The
harness records the categories above so a later trial can answer whether a
representation makes errors easier to identify and repair.

## LLM result assessment

**No representation is selected or favoured by B5.** Sample size is zero, and
no model tokens, costs or repair times were measured. Compiler size and
reference-artifact speed are not evidence of LLM productivity. The B4 finding
that B has the smallest front-end remains a compiler result, not an authoring
result.

If trials are run, report per representation:

- correct programs per model token and per unit cost;
- first-attempt and post-repair correctness;
- repair attempts and success after each attempt;
- input/output tokens and documentation cost;
- generation time separate from compile/execution time;
- error categories and severity;
- final artifact size and execution performance;
- uncertainty and sample size.

Do not infer a winner from a small trial.

## Implementation size

- B5 harness: 370 lines, 9,159 bytes text, 224 data.
- B5 fixture generator: 354 lines.
- Reused front-ends: A 8,553 bytes, B 4,559, C 2,505; common machine 5,505.
- 12 tasks in all three representations.

## Limitations

- No LLM sessions were orchestrated; no productivity measurement exists.
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

Raw reference measurements are in [`results.txt`](results.txt).

## Files

| File | Purpose |
|---|---|
| `b5_main.c` | task tests, references, counts, `--verify` |
| `tools/gen_tasks.py` | deterministic A/B/C task generator |
| `tasks/` | 12 task specs and artifacts |
| `generation/` | unperformed LLM trial harness |
| `results.txt` | reference measurements and test status |
