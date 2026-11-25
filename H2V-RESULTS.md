# H2V — Expression-level tracing for Glon's debugger

Date: 2026-11-25
Branch: `experiment/glon-http-h2v`
Baseline: H2U, commit `1aef3b2`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Expression-level trace added

Extended the debug-only `r0_s1_dump_ip` (called on a machine halt) to report the
**Glon expression location** from the evaluator registers:
- `RV_BLK` — the current block identity.
- `RV_CUR` — the current element address; `idx = RV_CUR - RV_BLK - BLK_DATA`.
- The block's element count and the elements around `idx` (word id → spelling).

This is read-only, debug-only, and adds no normal-execution overhead. No new
source-location mechanism, no frozen-S1 change.

## 2. Observed at the H2B failure

```
[word=36 name=a ctx=1622167]
[top word=0 name=func]
[blk=1130000 count=8 idx=-2]
  [0] either (tag 2)
task[0] state=0 gen=1 ... ctx=37303 frame=1622032
```

The current block is a Glon block at 1130000 with 8 elements (element 0 is the
word `either`); the failing lookup of `a` occurs while this block is the active
evaluation block. The block identity + index can be correlated with the loaded
program (the `either` control block in the handler/respond path).

## 3. Status

- Primary (expression-level diagnosis): the debugger now reports the block
  identity and element index at the fail-stop, distinguishing occurrences by
  location rather than only word id.
- Secondary (exact HTTPS expression): the block is identified (an `either`
  control block); the precise source statement still needs the block→source
  correlation (the loaded program's `either` blocks), but the location is now
  observable.
- No runtime fix; frozen S1 unchanged; default G3F/H2A untouched; task build +
  M1/M2 pass.

## 4. Next step

Correlate the reported block identity (1130000) with the loaded program's `either`
blocks (the handler/respond path) to name the exact source expression, then apply
the narrowest capture/return fix if one is needed.

## Reproduce

```
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
# curl -k https://127.0.0.1:8443/messages  -> halt dump includes [blk=.. idx=..]
```