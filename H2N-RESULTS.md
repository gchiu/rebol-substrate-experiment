# H2N — Diagnose C-spawned HTTP task representation

Date: 2026-11-25
Branch: `experiment/glon-http-h2n`
Baseline: H2M, commit `54ac00f`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Hypotheses tested and ruled out

- **Loader-heap lifetime**: `http_run_expression` saves/restores `GC_LOADER_HP`;
  I added a non-restoring variant for the spawn so the C-parsed task body stays
  in the loader heap. **No change** — the task still halts on the unbound helper
  local `a` (word 36). Reverted.
- **Session boundary** (H2M): cross-session CLI spawn/run preserves locals. Not
  the cause.
- **GC sentry** (H2G): disproven (the fail-stop is `r_lookup`, reason 429/0).
- **SQLite/7-args/respond** (H2L): the standalone Glon-spawned mock passes.

## 4. Remaining difference

The only remaining structural difference is the **C source-generation +
`http_run_expression` spawn path** (`http_bridge_spawn_task`) versus an
ordinary Glon `spawn`. The task body is a parsed literal block in both cases
(the C source is parsed, not `reduce`-built), and the loader-heap lifetime is
ruled out. The failure is an unbound **helper local** (`a`) after the async
SQLite resume, i.e. the activation/context chain, not the body representation.

## 5. Status

No fix applied. The evidence rules out the loader-heap lifetime, session
boundary, GC sentry and SQLite/arg-count as causes. The remaining defect is the
activation/context chain across the async SQLite suspension in the C-spawned HTTP
task path, while the Glon-spawned mock (same task-run/handler/respond structure)
preserves it. The next step is a direct **context-identity comparison** of the
C-spawned vs Glon-spawned task at checkpoints A–F (H2L §3), focusing on the
helper activation that owns `a` and whether it is reachable from the restored
`RV_CTX`.

## 6. Tests

- Frozen S1 unchanged (`check-frozen-s1.sh` PASS).
- Loader-heap-keep experiment: no change (reverted).
- Glon-spawned SQLite mock (H2L): PASS; cross-session CLI spawn/run (H2M): PASS.
- HTTP H2N two-task interleaving **not demonstrated** — not claimed.
- Default G3F / H2A untouched; task build + M1/M2 pass.

## Reproduce

```
make glon MODULES=file,http,sqlite,task MEM_PROFILE=large MEM_LOADER_CELLS=500000
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
# curl -k https://127.0.0.1:8443/messages  -> halts on unbound local (a)
```