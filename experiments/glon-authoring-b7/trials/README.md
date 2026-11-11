# B7 trial evidence

Model: `deepseek/deepseek-flash`; OpenCode 1.18.35; default variant and provider
temperature; adapter timeout 900 s; tool-free fresh temporary project/session
per call. A and E initial prompts differ only by the completion-contract
paragraph in `../generation/prompts/DIFF.md`.

Directories:

- `AE-seed-20261114/`
- `AE-seed-20261115/`

Each contains `run-config.json`, `summary.jsonl`, and per-trial trial logs,
artifacts, validator/compiler diagnostics, raw JSON events, sessions, token/cost
metadata, verification timings and `complete.json`. `analysis.md` and
`analysis.json` aggregate both seeds.
