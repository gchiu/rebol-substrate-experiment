# B5 generation-and-repair harness

**Status: completed, including follow-up.** A original and B original are the
reference runs. The original C run was interrupted by the 180 s adapter cap.
The follow-up corrected B's identifier documentation and reran B/C at 900 s:
B corrected 11/12 first, 12/12 final; C 0/12 final with zero timeouts.
See `results.md` and `../trials/`.

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
   - final artifact bytes/words and compile+verify time (code-cell counts were
     not captured by the adapter; the task-level execution result is captured).
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

`opencode_adapter.sh` implements the LLM_CMD contract with OpenCode 1.18.35:
it creates a fresh temporary project per invocation with a tool-free agent,
runs `opencode run --format json --pure` with the selected model, preserves raw
JSON events, and records session/token/cost/model-step/process metadata.

`run_trial.sh` performs one task/representation trial, including the repair
loop with `b5 --verify`:

```
LLM_CMD='model-cli --stdin' ./run_trial.sh tri A /tmp/tri.glon
```

`run_suite.sh` is the simple shell scheduler; `run_opencode_suite.py` is the
recording scheduler used for the reported suite (resumable, timed verification,
random seed and retained raw logs):

```
python3 run_opencode_suite.py --seed 20261110 --out ../trials/opencode-seed-20261110
```

The adapter invokes a new OpenCode process for every artifact; no
`--continue` or `--session` is used, so each initial generation and repair is a
fresh session. Token and cost data come from OpenCode `step_finish` events.
Model-step time is computed from event timestamps and is reported separately
from process wall time and verification time.

## Remaining work

C must be rerun with the corrected 900 s adapter timeout before it is compared
with A/B. A/B results are complete but come from one 12-task pilot. No broad
superiority claim is made.
