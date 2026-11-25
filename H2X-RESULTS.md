# H2X — Identify the exact Glon expression causing the unbound lookup

Date: 2026-11-25
Branch: `experiment/glon-http-h2x`
Baseline: H2W, commit `cf75ccb`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Word-lookup path

`r_lookup` pops the word into `RV_T1` and resolves it through `RV_CTX`. The
evaluator reads a word (advancing `RV_CUR`), then calls `r_lookup`. At the
fail-stop the word is already consumed and `RV_CUR` is at the block header
(H2W: `idx=-2`), so the element index cannot be derived from the halt state.

## 2. Missing capability

A **debug-only pre-lookup snapshot** at `r_lookup` entry (before the word is
consumed, or capturing `RV_T1`/`RV_WORD` and the element address) is required to
name the exact element. The S1 HOST op is frozen (no new HOST id), and the
emitted `r_lookup` cannot call C directly, so this needs a bounded host-side hook
(e.g., a `begin_request`-style callback or a debug build flag) — the smallest
addition that does not change evaluation semantics.

## 3. Status

- Primary (exact element): **blocked** — the fail-stop state has `RV_CUR` at the
  block header, so the element index is not recoverable; a pre-lookup snapshot is
  the missing capability (documented, not fabricated).
- Secondary (source expression): the active block is an 8-element `either`
  control block (1130000); the exact element/source needs the pre-lookup snapshot.
- No runtime fix; frozen S1 unchanged; default G3F/H2A untouched; task build +
  M1/M2 pass.

## 4. Next step

Add the debug-only pre-lookup hook to capture the selected element address/word at
`r_lookup` entry, validate it against known expressions (same word twice, nested
`either`, task resume), then correlate the block + element with the loaded source
to name the expression that reads `a` after the helper returns.

## Reproduce

```
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
# curl -k https://127.0.0.1:8443/messages  -> halt dump [blk=.. idx=-2]
```