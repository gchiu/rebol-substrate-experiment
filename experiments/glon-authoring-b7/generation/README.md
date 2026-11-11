# B7 trial harness

Reuses the B6 OpenCode adapter, extractor and structured validator, with B7
condition labels A/E.

- `prompts/A.md` — B6 textual prompt, unchanged.
- `prompts/E.md` — A plus the benchmark completion-contract paragraph.
- `prompts/DIFF.md` — exact diff.
- `run_trial.sh` — one trial, up to three repairs, always using `b7_verify.py`.
- `run_opencode_suite.py` — resumable matched A/E scheduler with recorded seeds.

Both conditions use identical structured validation and B5 verifier/execution.
