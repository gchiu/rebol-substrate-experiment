# H2S — Source-level fault isolation and HTTP continuation repair

Date: 2026-11-25
Branch: `experiment/glon-http-h2s`
Baseline: H2R, commit `c690098`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Exact failing reference

At the fail-stop, `r0_s1_dump_ip` reports `word=36 name=a`. Enumerating `a`
definitions in the loaded HTTP application and dependencies, the only local `a`
is **`wp/ci-loop` in `modules/web.glon` line 71**:

```
wp/ci-loop: func [s prefix i] [
    ...
    a: s/at s i
    ...
]
```

`a` is a per-iteration local of `wp/ci-loop`. It is not a request value and
should never be read after `wp/ci-loop` returns. The continuation that reads `a`
is therefore referencing a helper local outside its lifetime — the helper
activation is popped, so `r_lookup` correctly reports `a` unbound.

## 2. Call sequence

`http/task-run → handler → (helpers, incl. ci-style scans) → SQLite →
task-wait → reply → response`. The `a` is created and consumed **inside**
`wp/ci-loop`; it is not captured and must not survive the helper. The failing
continuation reads it after the helper returned — an ownership error at the call
site, not a runtime defect.

## 5/6. Correction and status

The narrowest correction is at the call site: the value the continuation needs
must be returned/captured (closure or explicit argument), not read from the
helper's popped local. I did **not** apply a speculative runtime change, and the
specific app call site that reads `a` after return is in the module helper
invocation path (the HTTP entry/adapter), which needs to capture the value.

**Status:** the exact helper (`wp/ci-loop`) and local (`a`) are identified. The
single-request HTTP path is **not** yet corrected (the call site that reads `a`
after the helper returns must capture/return it); the two-request interleaving is
**not** demonstrated. No frozen-S1, vocabulary or default-mode change.

## 7. Tests

- Frozen S1: PASS.
- H2Q ownership tests: PASS (closures capture; uncaptured rejected).
- Failing HTTP repro: halts on `a` (wp/ci-loop local).
- Default G3F/H2A untouched; task build + M1/M2 pass.

## 8. Next exact step

At the HTTP entry/adapter call site that invokes the ci-style helper, return or
capture the value the continuation needs (closure capture / explicit argument)
instead of reading the helper's local after return; then re-run the single- and
two-request tests.

## Reproduce

```
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
# curl -k https://127.0.0.1:8443/messages  -> halts on unbound local (a)
```