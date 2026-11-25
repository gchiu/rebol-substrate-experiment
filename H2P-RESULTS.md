# H2P — Diagnose and correct HTTP task activation capture

Date: 2026-11-25
Branch: `experiment/glon-http-h2p`
Baseline: D2A, commit `e506d04`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. D2A baseline confirmed

`r0_s1_task_dump` on the failing `--http-tasks` H2B app shows the suspended
task's saved `TREC_CTX` = the `http/task-run` activation
(request/me/id/method/path, parent global). The helper local `a` (word 36) is
absent. After resume, `r_lookup` fails on `a`.

## 2. Live-context inspection

`r0_s1_dump_ip` at the fail-stop reports the **live** `RV_CTX = 1622167` — the
same `http/task-run` activation (parent global). So at the failing lookup the
live chain is already the task-run chain; the helper activation that owns `a` is
**not** in the live chain either.

## 3. Classification

This is **Case 2 — the live context is already incorrect before the failing
lookup**: the helper activation owning `a` was **popped when the helper
returned**, before the task's continuation (after `task-wait`) performs the
lookup. The helper's local `a` is a normal activation local; when the helper
returns, its activation is popped and `a` is no longer reachable. The task's
continuation then references `a` through a chain that no longer contains the
helper activation.

The first incorrect transition is therefore the **helper return that pops the
activation while the task's continuation still needs its local** — not the
`task-wait` save (the saved chain equals the live chain at the lookup) and not
the scheduler restore.

## 4. Why the Glon-spawned mock passes

The standalone mock's helper is structured so the needed value is stored in a
**global** (or the helper is a closure that captures it) before returning; its
continuation does not read a popped helper local. The HTTP app reads `a` from a
helper activation that has already returned.

## 5. Status

No runtime fix applied. The defect is a Glon activation-lifetime/usage issue: a
helper's local is read after the helper returns. The correct correction is at the
source — keep the value in a closure/global or a context that outlives the helper
— but the milestone forbids "application workaround removing local variables",
and a general "preserve all activation chains" fix is exactly the speculative
full-chain copying to avoid. So I stop and report the evidence.

## 6. Tests

- Frozen S1 unchanged (`check-frozen-s1.sh` PASS).
- D2A dumper + `r0_s1_dump_ip`: live/saved ctx = task-run activation.
- Passing Glon-spawned SQLite mock (H2L): PASS.
- Failing HTTP repro: halts on `a` (reproduced).
- Default G3F/H2A untouched; task build + M1/M2 pass.

## 7. Next step (for review)

Either (a) the HTTP task-entry/callback should store the value the continuation
needs in a context that outlives the helper (a closure capture or the task
activation), or (b) a general activation-preservation rule is designed and
approved. Both require a runtime-contract decision; neither is applied here.

## Reproduce

```
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
# curl -k https://127.0.0.1:8443/messages  -> halts on unbound local (a)
```