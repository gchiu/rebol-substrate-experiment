# H2T — Trace wp/ci-loop activation and return

Date: 2026-11-25
Branch: `experiment/glon-http-h2t`
Baseline: H2S, commit `679a773`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Call path

`wp/ci-loop` (modules/web.glon:67) is called only by
`wp/ci-prefix?` (web.glon:59), a case-insensitive prefix test. `wp/ci-loop`
binds `a: s/at s i` per iteration and uses it immediately; it contains no async
operations and creates no closures. The H2B app does not call `wp/ci-prefix?`
directly; the reference reaches it only if a header/content-type check invokes
it during the response path.

## 2. Before/after return

D2A's read-only dump at the fail-stop shows the suspended task's saved/live
`RV_CTX` = the `http/task-run` activation (request/me/id/method/path, parent
global). The `wp/ci-loop` activation is **absent** from the chain. So the failing
lookup of `a` occurs **after `wp/ci-loop` returned** (its activation popped) —
**Case B**: a continuation references a helper local after the helper returned.

This is consistent with H2P (helper popped on return) and H2Q (closures capture;
uncaptured locals are rejected). The `a` is a per-iteration helper local that
should never be read after return; the continuation that reads it has no captured
binding.

## 3. Status

- Primary (before/after return): established — the lookup is **after** the
  helper's return (activation absent at the lookup).
- The exact call site that reads `a` after return is in the module invocation
  path; the narrowest correction is to return/capture the value (closure or
  explicit argument) rather than read the popped helper local. No speculative
  runtime change applied.
- Secondary (two interleaved HTTPS tasks): **not** demonstrated.

Frozen S1 unchanged; default G3F/H2A untouched; task build + M1/M2 pass.

## 4. Next exact step

At the call site that invokes `wp/ci-prefix?`/`wp/ci-loop` and later reads `a`,
return or capture the value; do not read the helper's local after return. Then
re-run the single- and two-request tests.

## Reproduce

```
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
# curl -k https://127.0.0.1:8443/messages  -> halts on unbound local (a)
```