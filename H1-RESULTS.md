# H1 — Native Glon multitasking for HTTP requests: results

Date: 2026-11-25
Branch: `experiment/glon-http-h1`
Baseline: M4, commit `7aa46aa`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Audit of the integration boundary

### CivetWeb's synchronous request callback
`glon_http.c` installs `http_begin_request` as CivetWeb's `begin_request`
callback. The vendored contract (`third_party/civetweb/civetweb.h`) is:
"1-999: callback already processed the request. Civetweb will not send any data
after the callback returned." There is **no pending/deferred return**: the
callback must send the response before it returns, or the connection is done.
A worker therefore cannot return from `begin_request` while its request is
suspended.

### G3F's bounded worker pool and evaluator lock
G3F added `--http-threads N` (CivetWeb worker pool) and an evaluator-ownership
mutex (`http.lock`). The lock is held across dispatch, the pre-response service
pump, response extraction and the post-response file pump, so exactly one thread
is inside the Glon session at a time. With `num_threads=1` requests serialize;
with `num_threads>1` workers overlap at the socket level but serialize on the
lock.

### Why a request never "suspends" today
`http_begin_request` runs, under the lock: `http_dispatch` (the Glon handler),
then the **pre-response pump** (`drain_pre`), which drains **all** pending
SQLite/file requests to completion and invokes their callbacks, then
`http_take_response`, then sends. The handler's async I/O is fully driven to
completion before the lock is released, so request A never yields the
interpreter to request B mid-flight.

### M2–M4 mechanisms (reusable)
- M2: `spawn`/`yield`/`run-tasks`/`task-wait`/`task-wake`, round-robin.
- M3: bounded FIFO of outstanding file/SQLite requests, id-correlated replies.
- M4: slot recycling with generation-tagged handles; stale references are
  discarded.
- `examples/h1/interleave.glon` proves the scheduler-level capability: task A
  suspends on an async file read (`task-wait`) while CPU task B runs to
  completion; `B-done` prints before `A-done` (with `--io-delay-ms 200`).

### Shared vs. per-task state (the second obstacle)
The HTTP vocabulary keeps request/response correlation in **globals**:
`http/current` (the request being dispatched) and `http/response` (the single
response). `http/request-field` and `http/respond` validate against
`http/current`. If two request tasks were interleaved, a task resumed after an
I/O wait could read another task's `http/response`, and `http/respond` would
validate against the wrong `http/current`. These globals are safe today only
because one request is inside the session at a time.

### No new frozen HOST id
The S1 `HOST` operation ids (`HOST_PUTCHAR` … `HOST_DUMP`) are defined in the
frozen `s1.h`; a new host word such as `http/finish` cannot be added as a new
S1 HOST id without modifying frozen S1. A host-side finish must therefore be a
Glon-level per-id table, not a new HOST op.

### Capacity
`M1_MAX_TASKS` is 10 in the task build (M4 recycles slots, so total tasks are
unbounded). Ten concurrent request tasks fit; more would need a larger arena
(a separate, approved scaling change).

## 2. Determination

A **safe** implementation that suspends one HTTP request as a Glon task while
another executes is **not achievable by only reconfiguring the existing
pieces**, because:
1. CivetWeb's `begin_request` is synchronous with no deferral, so a worker must
   stay inside the callback until the response is sent.
2. The Glon HTTP request/response state is global, not per-task, so interleaved
   tasks would corrupt correlation.
3. A host-side `http/finish` cannot be a new frozen HOST id.

Per the milestone's instruction ("If CivetWeb's synchronous callback contract
prevents a safe implementation, stop and document the precise obstacle and a
minimal proposed solution rather than creating unsafe concurrency"), this branch
does **not** ship an unsafe HTTP-task path. It ships the scheduler-level
demonstration and the design below. G3F remains the production path.

## 3. Minimal proposed solution (not implemented)

A bounded host-side request table plus the existing M2 scheduler, keeping one
Glon interpreter:

1. **Request table** (`glon_http.c`): `GLON_HTTP_MAX_REQUESTS` slots, each
   `[used, conn, id, done, status, ctype, body, task]`. `begin_request` parses
   the request, claims a slot, spawns a Glon task `[ spawn [ http/task-run
   <request> ] ]` (the request embedded as source, as `http_dispatch` already
   does), stores the task handle, and waits on a per-slot condition variable.
   The worker stays inside the callback (allowed) but does no Glon work.
5. **Main-thread scheduler**: `glon_http_run`'s loop, under `http.lock`, runs
   `[ run-tasks ]` then `drain_pre` (SQLite/file), then for each slot runs
   `[ http/take-response-for <id> ]`; on a response it stores it in the slot,
   marks `done`, and signals the worker. The worker sends the response on its
   own connection and frees the slot.
6. **Per-id response (Glon)**: add `http/responses` (a bounded cons table) and
   `http/finish id status ctype body`; `http/take-response-for id` finds and
   removes. This replaces the global `http/response` in task mode and avoids the
   cross-task corruption. No new HOST id is needed.
7. **Task mode flag**: `http/task-mode` makes `http/request-field` trust the
   passed request (skip the `http/current` check); default off preserves
   G1–G3F exactly.
8. **App pattern**: the handler enqueues its SQLite op, calls `task-wait`, and
   its reply callback calls `http/finish` (per-id) then `task-wake`. This is the
   M2/M3 pattern already used by `examples/h1/interleave.glon`.

This reuses M2 `run-tasks`, M3 async queues, M4 recycling and G3F's lock; it
adds no scheduler, no OS thread per task, and no frozen-S1 change. The
open work is the C request table, the main-loop scheduler, the per-id Glon
response table, and a task-mode app; each is small but the combination needs
careful concurrency review before it could be trusted.

## 4. Measurements (scheduler-level demonstration)

Configuration: `MODULES=file,task`, `MEM_PROFILE=large`,
`MEM_LOADER_CELLS=500000`, `--io-delay-ms 200`.

- Observed order: `B-start`, `B-done`, `A-done` — B (CPU) completed while A was
  suspended on the file read; A resumed only after its reply.
- The M4 endurance (10,000 tasks, ~4.3 s, peak RSS ~4.2 MiB) exercises the same
  scheduler and recycling that the proposed HTTP path would reuse.

## 5. Tests executed

Passed:
- `./check-frozen-s1.sh` (S1 unchanged; no host/frozen change on this branch).
- `tests/h1/acceptance.py` (scheduler-level interleave).
- (Carried from M4, unchanged by this branch) M1–M4, file, SQLite and HTTPS
  suites; this branch adds no runtime change, so they are unaffected.

Not implemented / blocked:
- HTTP requests as tasks (the synchronous `begin_request` boundary + global
  HTTP state, above). G1–G3F remain the supported HTTPS path.

## 6. Implemented vs. proposed

Implemented:
- A precise audit of the boundary and the two obstacles.
- A safe, observable scheduler-level demonstration that an I/O-bound task can be
  suspended while a CPU task completes (`examples/h1/interleave.glon`).
- A minimal, concrete proposed design (request table + main scheduler + per-id
  Glon response + task mode) that reuses M2–M4 and G3F without a new scheduler.

Proposed (not implemented):
- The C request table, main-loop scheduler, per-id response table and task-mode
  app described in §3.

## Reproduce

```
make h1-test
```

Runs the bounded scheduler-level interleave demonstration. The experimental
HTTPS server must not be exposed to the public internet.