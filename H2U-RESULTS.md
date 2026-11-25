# H2U — Diagnose incorrect continuation or return-frame execution

Date: 2026-11-25
Branch: `experiment/glon-http-h2u`
Baseline: H2T, commit `ffd2882`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Reproduced failure (unchanged)

`--http-tasks` H2B app:
```
[dump] IP=11953 SP=1621544 RP=1622131 HP=38832 top=0
[word=36 name=a ctx=1622167]        ;; r_lookup unbound
[top word=0 name=func]
task[0] state=0 gen=1 ip=11296 sp=1621542 rp=1622019 ctx=37303 frame=1622032
```
- Failing instruction IP **11953** (the `r_lookup` fail-stop).
- Word **36 = `a`**; lookup context **1622167** = the `http/task-run` activation.
- Task saved IP **11296** (resume point), frame 1622032.
- Halt reason 0 (unbound word, `r_lookup`).

## 2. Source mapping is absent

The compiled code is emitted into `[256, 16128)`; functions are **data blocks**
(Glon closures), not compiled code, so there is no instruction→source table.
IP 11953 is inside the evaluator's `r_lookup` fail-stop, not a source statement.
The word `a` is looked up while the evaluator executes a block under the
`http/task-run` context; the `wp/ci-loop` activation is absent.

**Missing diagnostic capability:** a debug-only instruction→function map (which
emitted routine owns IP 11953, and which source block the evaluator is executing).
Without it, the exact source expression cannot be named from the IP alone.

## 3. Return-frame state

At the fail-stop the current frame is 1622032 (the task-run activation); the
saved task frame matches. The `wp/ci-loop` activation is not in the chain, so the
lookup is **after** that helper returned (H2T, Case B). The return stack does not
indicate `wp/ci-loop` is active.

## 7. Classification

The evidence supports: the evaluator is executing a continuation under the
`http/task-run` context that references `a`, a helper local whose activation has
returned. The earliest incorrect transition is the **continuation reading a
helper local after return** (Case B), not an IP/frame-restore defect (saved and
restored IP/frame agree). A definitive instruction→source attribution needs the
missing debug-only mapping above.

## 8. Status

No runtime fix applied (source mapping insufficient to name the exact statement;
no speculative closure change). Frozen S1 unchanged; default G3F/H2A untouched;
task build + M1/M2 pass. Two-task HTTPS interleaving not demonstrated.

## 9. Next step

Add a debug-only instruction→function map (or a call-site trace at the helper
invocation) to name the exact source expression that reads `a` after return, then
apply the narrowest capture/return fix at that call site.

## Reproduce

```
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
# curl -k https://127.0.0.1:8443/messages  -> halts at IP 11953 on word a
```