# H3 — (M3) Fit collector correction and verify cooperative HTTPS

Date: 2026-11-25
Branch: `experiment/glon-http-h2g`
Baseline: H2F, commit `ca6b969`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Code-capacity audit

- The code region is `[256, R0S1_CODE_LIMIT)` with
  `R0S1_CODE_LIMIT = R0S1_DS_INIT - R0S1_DS_RESERVE = 16384 - 256 = 16128`.
  `R0S1_DS_INIT`/`R0S1_DS_RESERVE` are in `r0_s1.h` (not frozen S1); the
  region sits below the main data stack and above the emitted code.
- I raised `R0S1_DS_INIT` to 16,640 (code limit 16,384, +256 cells; the main
  DS/RS shrink slightly, loader/arena/managed heap unchanged) so the H2F sentry
  correction fits. It built and loaded `task.glon` cleanly. **This part works.**

## 2. The H2E/H2F diagnosis was WRONG

With the sentry correction applied and the code region enlarged, the halt
**persisted** (reason 0). I tagged every GC `HOST_DUMP`+`HALT` site with a
distinct reason and re-ran: the halt is **reason 429**, which is the
**`r_lookup` unbound-word fail-stop** (not the world-ID or task sentry). The
diagnostic prints:

```
[ip=12156 ops 2 6 15 7 0 12429]
[word=3490 ctx=1622167]
```

- **word = 3490** is unbound in the task's context (`ctx` is a task-arena
  context). The resumed task's response collection references a word that is not
  resolvable through the task's context chain.
- This is a **Glon-level unbound-word fail-stop**, not a collector/stack-sentry
  defect and not stack/heap corruption. H2E/H2F's GC-sentry theory is disproven.

## 3. Correction status

I reverted the speculative sentry change and the `R0S1_DS_INIT` bump so the
tree is back to the working H2F state (frozen S1 unchanged, `MODULES=task`
builds, M1/M2 pass). The code-region increase is a viable, measured +256-cell
change (main stacks shrink slightly; no overlap), but it does **not** fix the
halt because the cause is the unbound word.

## 4. Precise next obstacle

The remaining defect is: **word id 3490 is unbound in the resumed task's
context during response collection**. The next step is to resolve word 3490 to
its name (symbol table) and determine why it is not visible in the task context
— most likely a vocabulary word (task/http/sqlite helper) that the task's
context chain does not reach after resume, or a word referenced by the response
path that is defined in a sibling/child context rather than the global.

This is a Glon-level name-resolution issue, not a runtime memory-safety defect;
it should be fixable in the vocabulary/context setup without changing frozen S1
or the scheduler.

## 5. Tests

- `check-frozen-s1.sh`: PASS (unchanged).
- `MODULES=task` builds and runs ten cooperative tasks (M1/M2 pass).
- H2E reproduction still halts (unbound word 3490); H2G two-task HTTP
  interleaving **not demonstrated** — not claimed.
- Default G3F / H2A modes untouched.

## 6. Conclusion

The code-capacity obstacle is solvable (measured +256-cell `R0S1_DS_INIT`
increase, no overlap), but it is **not** the cause of the halt. The actual
cause is an **unbound word (id 3490)** in the resumed task's context during
response collection. The GC-sentry correction is therefore not the fix; the next
milestone should resolve word 3490 and repair the task's context/name
resolution. No frozen-S1, scheduler or memory-semantics change was made.

## Reproduce

```
make glon MODULES=file,http,sqlite,task MEM_PROFILE=large MEM_LOADER_CELLS=500000
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
# curl -k https://127.0.0.1:8443/messages  -> halts (unbound word 3490)
```

To see the word/ctx diagnostic, apply the H2G `r0_s1_dump_ip` word/ctx print
(reverted here to keep the tree clean).