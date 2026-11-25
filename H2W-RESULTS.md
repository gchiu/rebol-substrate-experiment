# H2W — Validate evaluator cursor and locate the failing Glon expression

Date: 2026-11-25
Branch: `experiment/glon-http-h2w`
Baseline: H2V, commit `6b5889d`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Cursor representation

Block layout is `[count, site, elem0, elem1, ...]` (`BLK_COUNT=0`,
`BLK_SITE=1`, `BLK_DATA=2`). At block entry the evaluator sets
`RV_CUR = blk + BLK_DATA` (first element) and `RV_END = blk + BLK_DATA + count`.
So a valid element index is `RV_CUR - blk - BLK_DATA` in `[0, count)`.

## 2. H2V's idx=-2

H2V reported `blk=1130000 count=8 idx=-2`, i.e. `RV_CUR = blk + 0` (the
**count/header** field), not a data element. This is the **block-entry state**
(RV_CUR at the header before the first element is consumed), not a normal
intra-block index. So the negative index is a **valid entry representation**, not
corruption and not a debugger arithmetic error — but it does not identify a data
element. The block identity (1130000, an 8-element `either` control block) is
valid.

## 5. Application to the HTTPS failure

At the fail-stop the active block is the `either` control block (1130000,
count=8) and `RV_CUR` is at its header (entry). The failing lookup of `a`
(word 36) occurs while this block is active, but the cursor is at the header, so
the element index is not a data position. The block identity is the reliable
correlation key; the exact data element needs a snapshot taken *after* the
evaluator advances `RV_CUR` into the block (a debug-only pre-lookup snapshot).

## 6. Status

- Primary: the cursor representation is documented; `idx=-2` is the block-entry
  header state (valid), and the block identity is valid. The debugger should
  report "header/entry" for `idx<0` rather than a source index.
- Secondary: the exact data element is not identified from the header-state
  cursor; a pre-lookup snapshot (after `RV_CUR` advances) is the missing piece.
- No runtime fix; frozen S1 unchanged; default G3F/H2A untouched; task build +
  M1/M2 pass.

## 7. Next step

Add a debug-only snapshot at `r_lookup` entry (before the fail-stop) to capture
`RV_CUR`/`RV_BLK` when the evaluator is actually positioned on the element, then
correlate the block + index with the loaded program to name the expression.

## Reproduce

```
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
# curl -k https://127.0.0.1:8443/messages  -> halt dump [blk=.. idx=..]
```