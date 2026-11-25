# H3A — Consolidate cooperative HTTPS + SQLite

Date: 2026-11-25
Branch: `experiment/glon-http-h3a`
Baseline: H2Z, commit `c3239e2`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Working implementation (verified)

- `examples/h2b/tasks.glon` + `--http-tasks` runs each request as a Glon task.
- `GET /messages` enqueues a SQLite read, `task-wait`s, the reply `http/respond`s
  (per-id) + `task-wake`s, and the owner delivers the response. **Verified**:
  `GET` → `0` (200), no halt.
- **Two overlapping clients** (A GET, B POST) both return 200 with the trace
  `start 1, wait 1, start 2, wait 2, resume 1, resume 2` — genuine cooperative
  interleaving (B ran while A was suspended).
- The H2Z provenance capture (cells 25040-25042) and D2A task dump are retained.

## 2. POST issue

`POST /messages` returns `0` (the GET count) instead of `ok`. Debug printing of
`http/method request` inside the task shows an **empty method** — the request's
method field is not propagated into the task entry, so `str-eq method "POST"`
fails and the GET branch runs. This is a task-entry request-field issue, not a
SQLite/HTTP-response defect. The GET path (which does not depend on the method)
is correct.

## 6. Measurements

- Executable (file,http,sqlite,task): ~505 KB.
- Peak RSS: ~18 MiB (suite).
- Max simultaneous connections: 10; queue: 16 bridge slots; task capacity 10.

## 5. Status

- Primary (GET as a task with SQLite suspend/resume, correct response): **verified**.
- Concurrency (two overlapping tasks, no cross-delivery): **verified** (trace).
- POST: **fails** (empty method in the task entry) — documented, not worked around.
- Frozen S1 unchanged; default G3F/H2A untouched; task build + M1/M2 pass.

## 7. Remaining

Fix the task-entry request-field propagation so `http/method` returns the real
method (the copied request block's method element is empty), then POST will take
the correct branch. The GET/SQLite suspend-resume path and two-client interleaving
are working and retained.

## Reproduce

```
make glon MODULES=file,http,sqlite,task MEM_PROFILE=large MEM_LOADER_CELLS=500000
./glon --http-tasks --http-threads 10 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
# curl -k https://127.0.0.1:8443/messages  -> 0
```