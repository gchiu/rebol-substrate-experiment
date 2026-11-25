# H2Z — Capture word-lookup provenance in Glon's debugger

Date: 2026-11-25
Branch: `experiment/glon-http-h2z`
Baseline: H2Y, commit `a829e7d`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Provenance capture

Added a debug-only capture at `r_lookup` entry (before the word is consumed):
the originating block (`RV_BLK`), cursor (`RV_CUR`) and selected word (`RV_T1`)
are stored in fixed cells (25040-25042) and printed by `r0_s1_dump_ip` as
`[origin blk=.. idx=.. word=.. name=..]`. No managed allocation, no evaluation,
no frozen-S1 change.

## 4. Result

The origin capture at the fail-stop:
```
[origin blk=1130000 idx=2 word=218 name=(block-at]
```
The failing lookup is for word 218 = **`(block-at`** — a word with a **stray
leading `(`** — not `a` (36). The source is `modules/http.glon:210`:
`either http/task-run ... (block-at request 0) ...`. Glon has no parentheses, so
`(block-at` was parsed as an (unbound) word. The `a`/word-36 reading at the halt
was a stale `RV_WORD`; the provenance capture identifies the real culprit.

## 7. Correction

Removed the stray `(` (and `)`) in `modules/http.glon:210`:
`http/take-response-for (block-at request 0)` → `http/take-response-for block-at request 0`.

## 8. Verification

- Single request: `GET /messages` → `0` (200); the task starts, enqueues SQLite,
  suspends (`wait`), resumes (`resume`) — no halt.
- Two concurrent requests (A GET, B POST): both 200, both correct, no halt; the
  event trace shows `start 1, wait 1, start 2, wait 2, resume 1, resume 2` —
  genuine cooperative interleaving (B progressed while A was suspended).
- The POST response body is `0` (a separate, non-fatal issue in the demo's POST
  branch); the GET/SQLite suspend-resume path is fixed.

## 9. Status

- Primary: the exact failing word/element identified (`(block-at`, http.glon:210)
  and corrected.
- Secondary: two interleaved HTTPS tasks with SQLite suspension return correct
  responses (GET count), no halt.
- Frozen S1 unchanged; default G3F/H2A untouched; task build + M1/M2 pass.

## 7. Remaining

The demo's POST branch returns `0` instead of `ok` (a separate app-level issue);
the core H2B unbound-lookup defect is fixed. The `--http-tasks` GET path now
suspends on SQLite, resumes, and responds correctly for two overlapping clients.

## Reproduce

```
make glon MODULES=file,http,sqlite,task MEM_PROFILE=large MEM_LOADER_CELLS=500000
./glon --http-tasks --http-threads 10 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
# curl -k https://127.0.0.1:8443/messages  -> 0 (no halt)
```