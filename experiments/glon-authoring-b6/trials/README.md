# B6 trial evidence

Model: `deepseek/deepseek-flash`; OpenCode 1.18.35; default variant and provider
temperature; adapter timeout 900 s; tool-free fresh temporary project/session
per call. A and D initial prompts are byte-identical.

Directories:

- `AD-seed-20261112/`
- `AD-seed-20261113/`

Each has `run-config.json`, `summary.jsonl`, and per-trial `trial.log`,
`artifact.attemptN`, `.test` diagnostics, `meta.jsonl`, raw OpenCode JSON
events, `verify_meta.jsonl`, `timed_verify_meta.jsonl`, `complete.json`.
`analysis.md` and `analysis.json` aggregate both seeds.

B is the B5 historical reference and was not rerun. C is not part of B6.
