# H2Y — Identify the failing expression by inspecting the Glon block

Date: 2026-11-25
Branch: `experiment/glon-http-h2y`
Baseline: H2X, commit `9d01c26`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Full block dump (1130000)

Added a bounded recursive block dump (`http_dump_block`, depth ≤ 4, ≤ 64 elems)
to the debug-only halt diagnostics. Block 1130000 (count 8) contains:

```
[0] either
[1] http/take-response-for
[2] (block-at
[3] request
[4] func
[5] )
[6] <block>  -> [0] func
[7] <block>  -> [0] http/respond [1] request [2] "" [3] mk-string ...
```

This is the **`http/task-run` body** (the outer `either` dispatch), not a helper
block. It does **not** contain word 36 (`a`) at any level.

## 3. Determination

The reported active block (`RV_BLK` at the fail-stop) is the `http/task-run`
body and does **not** contain the failing word `a`. So the block is **not
sufficient** to identify the failed lookup — the `a` reference is in a different
block that `RV_BLK` no longer points to at the halt. This confirms H2X: the
fail-stop cursor is at the header and the block is the outer body, not the
failing element's block.

## 4. Minimum additional information

A **debug-only pre-lookup snapshot** at `r_lookup` entry (capturing the selected
element address/word and its block) is required. The S1 HOST op is frozen, so
this needs a bounded host-side hook (debug build), validated against known
expressions. No source location is fabricated from the unrelated block.

## 6. Status

- Primary: the reported block does **not** contain `a`; block inspection is
  insufficient (stated explicitly).
- Secondary (exact expression): blocked on the pre-lookup snapshot.
- No runtime fix; frozen S1 unchanged; default G3F/H2A untouched; task build +
  M1/M2 pass. Two-task HTTPS interleaving not demonstrated.

## 7. Next step

Add the debug-only pre-lookup hook to capture the selected element's block/address
at `r_lookup` entry, then correlate with the loaded source to name the expression
that reads `a` after the helper returns.

## Reproduce

```
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
# curl -k https://127.0.0.1:8443/messages  -> halt dump includes [full block]
```