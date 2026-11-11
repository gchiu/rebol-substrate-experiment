# B5 OpenCode trial evidence

Model: `deepseek/deepseek-flash`
Adapter: `generation/opencode_adapter.sh`
Agent: `b5-solver`, all tools disabled (`tools: {"*": false}`,
`permission: {"*": "deny"}`), fresh temporary project per call.
Seed: `20261110` (full suite random order recorded in
`opencode-seed-20261110/run-config.json`).

## Directories

- `verification-sign_neg-20261110/` — pilot on `sign_neg` for A/B/C.
- `opencode-seed-20261110/` — full 12-task × A/B/C suite.
  - `run-config.json` — model, seed, order, representation list.
  - `NN-task-rep/trial.log` — run_trial stdout/stderr.
  - `NN-task-rep/artifact.attemptN*` — raw model artifacts, converted binary
    candidates for B/C, and `.test` diagnostics.
  - `NN-task-rep/meta.jsonl` — session IDs, tokens, provider cost, model-step
    time, process time and raw JSON paths for every adapter call.
  - `NN-task-rep/raw/raw-*.json` — raw OpenCode JSON events.
  - `NN-task-rep/verify.jsonl` — timed `b5 --verify` results per attempt.
  - `NN-task-rep/complete.json` — final summary record.
  - `summary.jsonl` — one row per completed trial.
  - `analysis.json`, `analysis.md` — aggregate analysis and caveats.

## Adapter defects found

1. **Working-directory defect.** The first suite invocation called
   `generation/run_trial.sh` from the B5 root, but that script resolves
   `../tasks` and `prompts` relative to `generation/`. It produced no model
   calls. `run_opencode_suite.py` now runs it from `generation/`.
2. **Timeout defect.** The adapter used a 180 s per-call timeout. Many C
   attempts hit the cap with zero tokens and 170–180 s process time. Those are
   adapter/infrastructure interruptions, not model-generation outcomes. The
   committed adapter default is now `B5_OPENCODE_TIMEOUT=900`. The completed
   logs used 180 s; **C must be rerun before it is interpreted as a model
   result.**

## Results caveat

A: 12/12 final success, 3/12 first-attempt success, 24 attempts.
B: 12/12 final success, 0/12 first-attempt success, 27 attempts; one ack trial
   had one timeout-affected call but still succeeded.
C: 0/12 final success under the 180 s cap; 11/12 trials were timeout-affected,
   so C is **not evaluable** from this run.

No reference implementation or fixture generator was exposed to the model.
Prompts were only the task specification and representation documentation.
