# D2A — Host-side task inspection using the existing D1 debugger: results

Date: 2026-11-25
Branch: `experiment/glon-debugger-d2a`
Baseline: D2, commit `877d09a`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Approach

D2's Glon-side `ctx-lookup` overflowed the fixed 16,128-cell code region, and
raising `R0S1_DS_INIT` broke the main stack sentry. So I added a **debug-only
C-side adapter** (`r0_s1_task_dump`) inside `r0_s1_runtime.c` (where `syms[]`,
`M` and the context layout are accessible). It walks each non-empty task record
and, for a RUNNABLE (suspended) task, its saved `TREC_CTX` parent chain and
bindings — **read-only, no switch into the task, no mutation**. No Glon vocabulary,
no code-region or `R0S1_DS_INIT` change.

## 2. Observed suspended-task state (failing HTTP task)

`--http-tasks` H2B app, at the fail-stop:

```
task[0] state=0 gen=1 ip=11296 sp=1621542 rp=1622019 ctx=37303 frame=1622032
    [0] request = 37030 (tag 6)     ;; block
    [1] me = 256 (tag 0)            ;; int handle
    [2] id = 16 (tag 0)             ;; 1
    [3] method = 36652 (tag 12)     ;; string
    [4] path = 36684 (tag 12)       ;; string
    (parent: global — no `a`)
```

So the suspended task's saved context is the **`http/task-run` activation**
(request/me/id/method/path), whose parent is the global. The helper local `a`
(word 36) that the resumed code looks up is **not present in this chain**.

## 3. First invalid transition

The helper activation that owns `a` is **not linked into the task's saved
context chain** at `task-wait`: the saved `TREC_CTX` is the task-run activation
(parent global), and the deeper helper activation created during the handler's
execution is absent. After resume, `r_lookup` walks the task-run→global chain,
does not find `a`, and fail-stops. This is a **context-chain/activation-lifetime**
defect (the helper activation is not preserved), not a body/lifetime/session/GC
issue (ruled out in H2G/H2M/H2N).

## 4. Status

D2A provides a working read-only task dumper (C-side, debug-only) that inspects a
suspended task's saved registers and context chain without mutating it. It
**does not** fix the HTTP defect (per the milestone: diagnose, do not fix). The
next step is to preserve the helper activation in the saved chain at `task-wait`
(a runtime context-model change) so `r_lookup` can resolve `a` after resume.

## 5. Tests

- Frozen S1 unchanged (`check-frozen-s1.sh` PASS).
- D2A dumper: works (task/context dump).
- Passing SQLite mock (H2L) / cross-session CLI (H2M): PASS (earlier).
- Failing HTTP repro: halts on `a` (dumped).
- Default G3F/H2A untouched; task build + M1/M2 pass.

## 6. Measurements

- Executable (file,http,sqlite,task): ~505 KB (dumper adds a few KB).
- Peak RSS: ~16 MiB (suite).
- Code region: unchanged (no Glon vocabulary added).

## Reproduce

```
make glon MODULES=file,http,sqlite,task MEM_PROFILE=large MEM_LOADER_CELLS=500000
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
# curl -k https://127.0.0.1:8443/messages
# on halt the task/context dump is printed
```