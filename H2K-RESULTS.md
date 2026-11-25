# H2K — Repair HTTP task entry across interpreter sessions

Date: 2026-11-25
Branch: `experiment/glon-http-h2k`
Baseline: H2J, commit `21381ff`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Attempted fix

H2J isolated the failure to the cross-session boundary (spawn in one host
session, first run in a later pump session). I changed the spawn expression to
`[ task-handle: spawn [ http/task-run <args> ] run-tasks task-handle ]` so the
task is spawned **and first run in the same interpreter session**.

**Result:** the spawn+first-run now succeeds, but the task still halts on the
**unbound local `a`** (word 36) after the async SQLite resume. So the
cross-session boundary was **not** the sole cause; the activation chain is also
broken across the asynchronous resume itself.

## 2. Refined diagnosis

- The pure-task matrix (H2J) showed nested locals survive `task-wait` and async
  **file** I/O (Cases C/F/G/H/Mock PASS).
- The HTTP path uses async **SQLite**; after the SQLite reply (delivered in the
  main world during the host drain) and `task-wake`, the resumed task's `RV_CTX`
  chain does not reach the activation of the helper that owns `a` (and earlier
  `result` in `http/task-run`).
- Same-session spawn+run removes the session-boundary variable but the unbound
  local persists, so the defect is the **activation/context chain across the
  async suspension/resume**, not the session boundary alone.

This is a Glon activation-lifetime issue: the helper's activation context (a
child created during the handler's execution) is not reachable from the restored
`RV_CTX` after the async resume. The correct fix is to preserve/restore the full
activation chain (the `RV_CTX` parent links and `RV_FRAME` chain) that was live
at `task-wait`, and ensure those contexts remain GC-reachable while the task is
suspended — a runtime context-model change.

## 3. Status

I reverted the spawn+run change (it did not fix the failure) to keep the tree at
the working H2J state. No runtime fix was applied, because the required change is
in the evaluator's activation/context restoration across an async suspension — a
runtime contract that must be designed and reviewed, not patched speculatively.

## 4. Next exact obstacle

Preserve the full activation chain across the async suspension: at `task-wait`,
capture not only `RV_CTX` but the chain of activation contexts (and `RV_FRAME`),
and on resume restore that chain so `r_lookup` reaches the helper's locals; keep
those contexts GC-rooted while the task is blocked. The pure-task async-file test
(H2J Case F) shows the mechanism can work for file I/O, so the SQLite/HTTP path
must be losing the chain specifically.

## 5. Tests

- Frozen S1 unchanged (`check-frozen-s1.sh` PASS).
- Pure-task matrix (C/F/G/H/Mock) PASS; task build + M1/M2 pass.
- HTTP H2K two-task interleaving **not demonstrated** — not claimed.
- Default G3F / H2A untouched.

## Reproduce

```
make glon MODULES=file,http,sqlite,task MEM_PROFILE=large MEM_LOADER_CELLS=500000
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
# curl -k https://127.0.0.1:8443/messages  -> halts on unbound local (a)
```

The halt dump prints `[word=<id> name=<spelling> ctx=<ctx>]`.