# M3 — (M2F) Correct running-task GC sentry and complete cooperative HTTP

Date: 2026-11-25
Branch: `experiment/glon-http-h2f`
Baseline: H2E, commit `c44fbbc`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Confirmed failing condition

Reproduced the H2E halt (`IP 11953/11980, reason 0, SP=arena base, RP in
bounds, HP low`). `emit_collect`'s "every task record" loop checks each
RUNNABLE task's **saved** `TREC_SP`/`TREC_RP` against the arena bounds. For the
**currently-running** task those saved values are from its last `task-wait`
yield and are **stale** while it runs after a resume. The sentry therefore
fail-stops a healthy task (reason 0, `HOST_DUMP`+`HALT`), before the response
is collected. This is a **stale saved-register check**, not stack corruption
(RP is in bounds, HP is far below the limit, the DS is empty).

## 2. Correction (implemented, then reverted for code space)

I changed the sentry to use the **live `REG_SP`/`REG_RP`** when the record is
the running task (`record == M1_CUR_TASK`), and saved `TREC_SP`/`TREC_RP` for
suspended tasks, keeping the bounds check and the saved-range scan:

```
is_running = (record == M1_CUR_TASK)
SP = is_running ? REG_SP : record.TREC_SP
RP = is_running ? REG_RP : record.TREC_RP
... existing bounds checks on SP/RP ...
scan record.TREC_SP..base+DS_OFF, record.TREC_RP..base+RS_OFF
```

**Effect:** with this change the first H2D request returned the correct body
(`GET /messages` → `0`) — the diagnosis is confirmed. The stack-corruption
sentry is preserved (bounds are still enforced; only the source of the running
task's SP/RP changes).

## 3. Blocker: fixed code-space ceiling

The added instructions (~30 emitted cells) overflow the **fixed 16,128-cell
code region**: after the change, loading `task.glon` fails with
`masm code space exhausted: the block needs 34 cells, 29 are free`. The code
limit is part of the frozen memory map (the emitted evaluator/collector live
below it), so raising it is a memory-map change that this milestone forbids
without separate approval. I therefore **reverted** the sentry change to keep
the build working; the H2F success criterion is **not** met.

## 4. Minimal proposed resolution (for approval)

- Increase the code region (e.g., `R0S1_HEAP_LIMIT`/code-limit constant) by a
  small, measured margin (~64 cells) so the sentry fix fits — a separate,
  explicitly approved memory-map change; **or**
- Reduce emitted code elsewhere (e.g., fold the `is_running` check into the
  existing world-identification result, reusing `GC_C3`/the current-slot value
  already computed) so the fix fits the current 16,128-cell budget.

Either is small and targeted; neither changes the scheduler or frozen S1.

## 5. Regression status

- Frozen S1 unchanged (`check-frozen-s1.sh` passes).
- `make m1-smoke` (M1 A–K, 1,000-switch stress) passes.
- `MODULES=task` builds and runs ten cooperative tasks.
- Default G3F / H2A modes are untouched (the sentry change is reverted).
- H2E reproduction still halts (fix not loaded); H2F two-task HTTP interleaving
  **not demonstrated** — not claimed.

## 6. Conclusion

The H2E diagnosis is **confirmed**: the collector validates the running task's
**stale saved** SP/RP. The minimal correction (use live `REG_SP`/`REG_RP` for
the running task) makes the first request succeed, but it exceeds the fixed
16,128-cell code space. The remaining work is a small, approved code-limit
increase (or an equivalent code-size reduction) to load the fix; then the H2B
two-task SQLite interleaving can be re-run.

## Reproduce

```
make glon MODULES=file,http,sqlite,task MEM_PROFILE=large MEM_LOADER_CELLS=500000
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
# curl -k https://127.0.0.1:8443/messages  -> halts (fix not loaded)
```

The `r0_s1_dump_ip` diagnostic prints the opcode window at the faulting IP.