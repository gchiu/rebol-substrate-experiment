# H2A — Bounded asynchronous HTTP request/response bridge: results

Date: 2026-11-25
Branch: `experiment/glon-http-h2a`
Baseline: H1, commit `91b0762`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Audit (before changes)

- **G3F** runs a bounded CivetWeb worker pool (`--http-threads N`) and an
  evaluator-ownership mutex (`http.lock`) held across dispatch, the pre-response
  service pump, response extraction and the post-response file pump. One thread
  is inside the Glon session at a time.
- **H1** documented that CivetWeb's `begin_request` is synchronous (return 1 =
  handled; no deferral), and that the Glon HTTP state (`http/current`,
  `http/response`) is global.
- **Request/response ownership**: CivetWeb owns each `mg_connection`; the
  response is built in a local `http_response` and written with `mg_write` from
  the worker that owns the connection.
- **Interpreter ownership**: the persistent Glon session is entered only via
  `http_run_expression` (dispatch, pre-pump, take-response, clear-current).
- **Frozen HOST interface**: S1 HOST ids (`HOST_PUTCHAR`…`HOST_DUMP`) are frozen;
  no new host word is added.
- **Timeouts**: `request_timeout_ms` (default 10 s) bounds idle connections.

## 2. Implemented bridge (opt-in `--http-bridge`)

Default mode is unchanged G3F. With `--http-bridge` (and `--http-threads N`):

- **Bounded request table** (`GLON_HTTP_MAX_BRIDGE_REQUESTS = 16`): each slot
  holds `state` (empty/queued/processing/completed/cancelled), a unique
  generation-safe `id` (from the existing monotonic `http.next_id`), the
  connection, **copies** of method/path/query/body/headers/peer (valid after the
  callback starts waiting), the response (`http_response`), and a deadline.
- **Worker (CivetWeb thread)**: `http_begin_request` parses the request, copies
  it into a free slot, marks it queued, signals the owner, then waits on a
  condition variable **without holding `http.lock`** (`pthread_cond_timedwait`
  releases it). On completion it sends the stored response on its own
  connection; on timeout it sends 503/408; on shutdown it sends 503.
- **Interpreter owner (main thread)**: `glon_http_run`'s loop, under `http.lock`,
  runs `http_bridge_process`: for each queued slot it runs `http/dispatch` (from
  the copied slot data), the pre-response service pump (SQLite/file), and
  `http/take-response`, stores the response, marks completed and signals.
  **No CivetWeb worker enters the Glon evaluator in bridge mode.**
- **Bounded capacity / rejection**: 16 slots; a full table returns 503. The
  worker pool is capped at 32 (`GLON_HTTP_MAX_THREADS`); `--http-threads 0` and
  `>32` are rejected.
- **Timeouts / disconnect**: the worker's wait is bounded by
  `request_timeout_ms`; a disconnecting client's slot is cancelled and its
  response discarded. A slow body upload blocks only its own worker (the body
  read is before the lock).
- **Shutdown**: the owner cancels all queued/processing slots and broadcasts, so
  waiting workers wake and exit; `mg_stop` then joins them.

**Evaluator-ownership / thread-safety argument:** exactly one thread is inside
the Glon session because `http.lock` is held across dispatch, the pre-response
pump, the post-response file pump (moved under the lock in G3F), response
extraction and `clear-current`. Workers only copy request bytes and wait; they
never touch the evaluator, the SQLite connection or the file queue. The main
loop reads only `volatile` counters. This is OS-level connection concurrency
with a **single serialised interpreter** — no concurrent evaluator entry, no new
interpreter, no OS thread per Glon task.

## 3. Implemented vs. proposed

Implemented: the bounded bridge (table, states, ids, copies, sync, timeouts,
disconnect, overflow, shutdown) and the opt-in `--http-bridge` mode; G3F remains
the default.

Not in H2A (H2B): running each request as a *cooperative Glon task* that yields
on SQLite while another request's task runs. H2A's owner loop still runs one
request's Glon work to completion per turn; it decouples *receiving* from
*execution* and makes the host safe for H2B to add task scheduling.

## 4. Measurements

Configuration: `MODULES=file,http,sqlite`, `MEM_PROFILE=large`,
`MEM_LOADER_CELLS=500000`, `--http-bridge --http-threads 10`.

- Executable size: **497,888 bytes** (~486 KiB).
- Peak resident memory (H2A suite): **18,232 KiB** (50-request run 17,620 KiB).
- Max simultaneous connections tested: **10** (1 idle keep-alive + 9 submitters).
- Queue capacity: **16**; with 20 workers / 20 simultaneous, **18 admitted, 2
  rejected with 503** (controlled overflow, table intact).
- Latency: 9 overlapping submitters wall **2.31 s**; a fast client during a
  trickling upload completed in **0.06 s**.
- Per-client correctness: each response carried its own author (no
  cross-delivery); unique message ids.

## 5. Tests executed

Passed:
- `./check-frozen-s1.sh` (S1 unchanged).
- `tests/h2a/acceptance.py` — all seven groups (basic, overlap+idle, slow
  client, disconnect, overflow, shutdown, memory).
- `tests/https-g1/acceptance.py`, `tests/https-g3e/acceptance.py`,
  `tests/https-g3f/acceptance.py` (default non-bridge mode).
- `tests/sqlite-g3d/acceptance.py`, `tests/sqlite-g3d/http_sqlite_test.py`,
  `make sqlite-test`.
- `make m4-test` (M2–M4 scheduler/recycle; 10,000 tasks in 3.10 s).

Not run: the full `make test` monolith (relevant suites run individually under
the 1 GB budget). No G1–G3F assertion was weakened.

## 6. Limitations

- H2A does not yet run requests as cooperative Glon tasks (H2B); the owner loop
  processes one request's Glon work per turn.
- The response write happens under the lock (small responses on loopback do not
  block); a very large response to a non-reading client could hold the lock up to
  `request_timeout_ms`.
- The accept backlog is the OS/CivetWeb default; the worker pool (32) and the
  16-slot table are the explicit bounds.
- No fsync/power-failure guarantee; no auth (inherited).

## Reproduce

```
make h2a-test
```

Builds the large-profile `file,http,sqlite` binary, generates the local test
certificate, and runs the bounded bridge suite. Do not expose the experimental
HTTPS server to the public internet.