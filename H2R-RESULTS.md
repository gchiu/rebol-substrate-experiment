# H2R — Implement closure-safe cooperative HTTPS tasks

Date: 2026-11-25
Branch: `experiment/glon-http-h2r`
Baseline: H2Q, commit `cffcbd3`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Ownership verification

- **Activation locals survive `task-wait` + resume** in a pure task
  (`f: func [id] [ task-wait  ... id ]` → `id=99`). PASS.
- **Closures capture locals** across return and async suspension (H2Q A/B). PASS.
- **Uncaptured locals are rejected** (H2Q E). PASS.

So Glon's existing ownership mechanisms are sufficient: a value that must
survive an async suspension is retained by an activation local (task stack) or a
closure capture.

## 2. Remaining obstacle (single request)

The C-spawned `--http-tasks` H2B app still halts on an unbound helper local
(`a`, word 36; D2A shows the saved/live `RV_CTX` = the `http/task-run`
activation, parent global — the helper activation is absent). The pure-task and
mock cases preserve their locals, so the defect is a **specific uncaptured
helper local in the C-spawned HTTP entry/callback path**, not a general
activation-lifetime problem.

Identifying the exact helper requires stepping the C-spawned task (D1 has no
single-step; D2A's C dumper shows the chain but not the popped helper). The
narrowest correction is to capture that value in a closure / request-owned state
in the HTTP entry adapter — but the specific value is not yet isolated, so no
speculative change was applied.

## 6. Status

- Primary (ownership mechanism): verified (activation locals + closures survive;
  uncaptured rejected).
- Secondary (two interleaved HTTPS tasks): **not** demonstrated — the specific
  uncaptured helper local in the C-spawned HTTP path remains.
- Frozen S1 unchanged; default G3F/H2A untouched; task build + M1/M2 pass.

## 7. Next exact step

Instrument the C-spawned task's helper call (the one defining `a`) to identify
the value, then capture it in a closure or the request record. The pure-task
`id=99` test proves the mechanism; only the C-spawned HTTP entry needs the
capture.

## Reproduce

```
make h2q-test        # ownership tests pass
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
# curl -k https://127.0.0.1:8443/messages  -> halts on unbound local
```