# H2M — Persistent interpreter session and HTTP task context isolation

Date: 2026-11-25
Branch: `experiment/glon-http-h2m`
Baseline: H2L, commit `1a31fbd`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Session-boundary experiment

I tested whether re-entering the interpreter (`http_run_expression` /
`r0_s1_session_run_block`) between spawn and run is what breaks the task's
activation chain. Minimal CLI reproducer (no CivetWeb, no HTTP):

- `s1.glon`: `f7` (7 args, `task-wait`) + `spawn` t1 + `spawn` t2 (waker).
- `s2.glon`: `run-tasks`.
- Run: `./glon s1.glon s2.glon` — the task is spawned in **one session** (file 1)
  and first run in a **different session** (file 2).

**Result: PASS** — `S=1`. The nested local survives the cross-session
spawn/run. So **a persistent session is not required**: the session boundary
does not invalidate the task's saved `RV_CTX`/activation chain.

## 2. Conclusion on persistent ownership

Persistent interpreter ownership is **ineffective** for this defect: the same
cross-session spawn/run works in the CLI reproducer. The failure is specific to
the **host-spawned HTTP task path** (CivetWeb dispatch → `http_bridge_spawn_task`
→ owner pump), not to session re-entry in general.

This is consistent with H2L: the standalone SQLite `task-run`+handler+respond
mock (spawned from Glon, one session) PASSES, while the host-spawned HTTP task
loses the helper local after the async SQLite resume. The activation-lifetime
question therefore remains, but the **session boundary is ruled out**.

## 3. Comparison

| Property | Existing host entry | Persistent session |
|---|---|---|
| Nested locals survive (CLI cross-session) | PASS | not needed |
| SQLite suspension/resumption (mock) | PASS | not needed |
| Correct HTTP responses | FAIL (unbound local) | not tested (session ruled out) |
| Two-task interleaving | not demonstrated | not demonstrated |

## 4. Next exact obstacle

The host-spawned HTTP task (`http_bridge_spawn_task` building
`[ spawn [ http/task-run <mk-string/reduce args> ] ]` and running it via
`http_run_expression`) loses the activation that owns the helper local after the
SQLite resume, while the equivalent Glon-spawned mock and the CLI cross-session
spawn/run do not. The next step is to compare the **context/activation state of
the C-spawned task** (the exact `RV_CTX`/`RV_FRAME`/parent links and the
wrapper/body block lifetime) against the Glon-spawned mock — the difference is in
the host-spawn path, not session re-entry.

## 5. Tests

- Frozen S1 unchanged (`check-frozen-s1.sh` PASS).
- CLI cross-session spawn/run: PASS (S=1).
- Standalone SQLite mock (H2L): PASS.
- HTTP H2M two-task interleaving **not demonstrated** — not claimed.
- Default G3F / H2A untouched; task build + M1/M2 pass.

## Reproduce

```
./glon /tmp/s1.glon /tmp/s2.glon      # cross-session spawn/run -> S=1 (PASS)
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
# curl -k https://127.0.0.1:8443/messages  -> halts on unbound local
```