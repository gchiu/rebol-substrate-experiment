# B6 trial harness

- `opencode_adapter.sh` — OpenCode 1.18.35 LLM_CMD adapter, fresh temporary
  tool-free project per call.
- `opencode_extract.py` — preserves raw JSON and appends session/token/cost/
  model-step/process metadata.
- `run_trial.sh` — one task/representation trial, up to three repairs, calling
  `../b6_verify.py`.
- `run_opencode_suite.py` — resumable matched A/D scheduler with recorded seed.
- `prompts/A.md`, `prompts/D.md` — byte-identical textual prompts. D differs
  only in post-generation validation diagnostics.

Commands are run from this directory by the suite. A/D initial prompts are
matched; no reference solution is supplied.
