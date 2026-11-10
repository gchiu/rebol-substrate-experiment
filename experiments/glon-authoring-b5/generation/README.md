# B5 generation-and-repair harness

**Status: not run.** No LLM was available through this development environment.
No model-token, elapsed-time, cost or productivity measurement was performed.
The harness and fixtures are provided so the experiment can be run later.

## Tasks and representations

- Task specifications: `../tasks/<name>.md` (12 tasks).
- A textual Glon format: `prompts/A.md`.
- B tagged-word format: `prompts/B.md`.
- C direct-machine format: `prompts/C.md`.

B and C use a text-only authoring interface: the model writes
whitespace-separated 32-bit hex words, and `tools/hex_to_bin.py` converts them
to the little-endian binary artifacts consumed by the unchanged B4 front-ends.
The conceptual representations remain the B4 word streams. Source/artifact
sizes for LLM trials should be recorded both as raw hex text and as binary
words.

Each prompt contains the task specification plus one representation's format
documentation and no reference implementation.

## Controlled trial protocol

For each task and representation:

1. Use a fresh model context/session. Do not include artifacts or repairs from
   another representation or another trial.
2. Prompt = task specification + representation documentation.
3. Allow one initial artifact and at most three repair attempts.
4. Convert B/C hex to binary with `tools/hex_to_bin.py`, then compile and run
   with `../build/b5 --verify TASK A|B|C FILE`; return diagnostics verbatim
   for repair.
5. Record:
   - model, version, configuration, temperature, token budget;
   - prompt and representation documentation supplied (input cost);
   - initial artifact and every repair artifact;
   - compile/runtime failure category;
   - input/output tokens if the API reports them;
   - model generation time separately from compile/execution time;
   - number of repair attempts and final pass/fail;
   - final program bytes/tokens, code cells, instructions and execution time.
6. Use comparable budgets and the same repair allowance for A, B and C.
7. Randomise task and representation order. `run_suite.sh` shuffles the
   task×representation list and invokes a fresh `LLM_CMD` process per trial.

Failure categories to record:

- invalid representation/encoding;
- incorrect program semantics;
- calling-convention or stack error;
- compiler/assembler limitation;
- other.

## Harness

`run_trial.sh` performs one task/representation trial, including the repair
loop with `b5 --verify`:

```
LLM_CMD='model-cli --stdin' ./run_trial.sh tri A /tmp/tri.glon
```

`run_suite.sh` shuffles all task/representation pairs and writes artifacts and
logs under an output directory:

```
LLM_CMD='model-cli --stdin' ./run_suite.sh /tmp/b5-trials
```

`LLM_CMD` is responsible for model configuration and fresh-session behaviour;
the harness invokes it as a new process for each artifact. Token counts are
only available if that command reports them; record them separately.

## What remains unperformed

No generation, repair, token accounting, cost measurement or model-session
comparison has been run. `results.md` records this limitation. No claim is
made that any representation is easier for an LLM.
