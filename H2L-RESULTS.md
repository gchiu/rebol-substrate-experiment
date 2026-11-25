# H2L — SQLite callback context isolation

Date: 2026-11-25
Branch: `experiment/glon-http-h2l`
Baseline: H2K, commit `c10474b`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Reproduction

`examples/h2b/tasks.glon` with `--http-tasks`: the task starts, enqueues the
SQLite read, prints `wait`, suspends, the reply arrives and `task-wake` runs, then
the resumed task halts on an unbound local (`a`, word 36; earlier `result`,
word 218). The halt dump prints `[word=<id> name=<spelling> ctx=<ctx>]` with
`ctx` a task-arena context.

## 2. Isolation (no CivetWeb)

A standalone mock with the **same `task-run` + handler + `http/respond` +
`http/take-response-for` structure and an async SQLite read**, spawned and run in
one top-level session, **PASSES** (rc=0, no halt). H2J's pure-task SQLite case
(7 literal args, `task-wait`) also PASSED. So **SQLite itself, the 7-argument
entry, and the respond path are not the cause**.

The failing configuration is the **host-spawned HTTP task** (spawned from C in
the dispatch/pump via `http_run_expression`) whose continuation is resumed in a
**later owner-pump session**. The mock and pure-task cases run spawn + first-run
in a single top-level session.

## 3. First invalid transition

Comparing the passing mock with the failing HTTP path, the divergence is at the
**async SQLite resume across the host session boundary**: the resumed task's
restored `RV_CTX` chain does not reach the activation that owns the helper local
(`a`). This is consistent with H2K (same-session spawn+run fixed the first run
but not the post-SQLite-resume lookup) — the remaining defect is the
**activation/context chain across the async suspension**, not SQLite, not the
session boundary alone, and not the GC sentry (disproven in H2G).

Classification: **correctly restored chain invalidated by subsequent execution /
context-lifetime across the async suspension** (the helper activation is not
reachable after resume). The pure-task async-file case (H2J F) shows the chain
can survive an async suspension, so the host-spawned/pump path loses it
specifically.

## 4. Status

No runtime fix applied. The evidence points to the evaluator's activation/context
restoration across an async suspension in the host-spawned HTTP task path. The
correct fix is to preserve/restore the full activation chain (`RV_CTX` parent
links + `RV_FRAME`) that was live at `task-wait`, and keep those contexts
GC-rooted while the task is blocked — a runtime context-model change that must be
designed and reviewed, not patched speculatively. No frozen-S1, vocabulary,
memory-map or scheduler change was made.

## 5. Tests

- Frozen S1 unchanged (`check-frozen-s1.sh` PASS).
- Mock SQLite (one session): PASS; pure-task SQLite (H2J G): PASS.
- HTTP H2L two-task interleaving **not demonstrated** — not claimed.
- Default G3F / H2A untouched; task build + M1/M2 pass.

## 6. Next exact obstacle

Preserve the full activation chain across the async SQLite suspension in the
host-spawned HTTP task (capture the `RV_CTX` parent chain + `RV_FRAME` at
`task-wait`, restore on resume, keep GC-rooted). The mock proves the Glon-level
structure is correct; the host-spawn/pump path is where the chain is lost.

## Reproduce

```
# mock (passes):
./glon --io-root /tmp/m --allow-file-read --allow-file-write /tmp/m/mock.glon
# HTTP (halts on unbound local):
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
# curl -k https://127.0.0.1:8443/messages
```