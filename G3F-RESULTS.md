# G3F — Glon-native multitasking for HTTPS + SQLite: results

Date: 2026-11-25
Branch: `experiment/glon-https-g3f`
Baseline: G3E, commit `6df83c9`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## Objective

Investigate and, where feasible, implement concurrent HTTPS request handling
using Glon's existing multitasking and asynchronous I/O machinery, without a
new scheduler/task model, and without weakening tests or silently raising
limits.

## 1. Investigation findings (before any change)

### Task vocabulary and scheduler

- The M1 task vocabulary is **not** in the shipped libraries. It lives in
  `examples/multitasking.r0` (canonical listing) and the runnable copy in
  `r0_s1_m1_tests.c`. The user-facing words are `spawn`, `repeat`, `yield`,
  `run-tasks`; the mechanism is RAW/S1 fragments `mnew-task`, `yield`,
  `task-finish`, `run-tasks` assembled from the frozen seven primitives.
- A task is a 16-cell record in a fixed table (`M1_TASK_TABLE`, default 65000)
  holding `[resume_ip, SP, RP, RV_CUR, RV_END, RV_CTX, RV_BLK, RV_FRAME, state]`.
  `REG_HP` is global and shared; each task has a private DS/RS in the arena.
- `run-tasks` is a **cooperative round-robin** loop over the task table. A task
  runs until it executes `yield` or `task-finish`.
- **`M1_MAX_TASKS` is 3** (`m1_layout.h`). The table and arena live in the
  frozen 64K map (relocated in the large profile). Raising it is a resource-map
  change, which this task forbids.

### Asynchronous I/O

- The file module (`modules/file.glon`) and SQLite module (`modules/sqlite.glon`)
  use the same **one-outstanding-request** request/reply protocol: a Glon
  request is enqueued, the host drains it between turns, and a success/failure
  closure is invoked with the reply. SQLite is never entered concurrently; the
  host owns the `sqlite3*` and the single connection serialises all statements.

### HTTP dispatch and the single worker

- `glon_http.c` bridges a **completed** HTTP request into the persistent Glon
  session: CivetWeb's `begin_request` calls `http_dispatch`
  (`[ http/dispatch ... ]`), then the **pre-response pump** drains SQLite/sec
  callbacks, then `http/take-response` extracts the reply. All of this runs
  synchronously inside `begin_request` before it returns.
- CivetWeb's `begin_request` contract (vendored `civetweb.h`) is: return 1 =
  "callback already processed the request; CivetWeb will not send anything
  after the callback returned." **There is no pending/deferred return**, so a
  request cannot be suspended and resumed in a later scheduler turn.
- The worker pool size was hard-coded to `num_threads=1`
  (`GLON-HTTPS-SERVER-G1.md`: "One CivetWeb worker … HTTP requests are
  serialized"; "`num_threads` must not be raised without an explicit
  evaluator-ownership design"; "No M1 tasks are used").

### Why G3E uses one worker, and where starvation occurs

- With one worker, one request is fully processed at a time. Overlapping
  connections queue behind it. An idle keep-alive connection is reaped at
  `request_timeout_ms` (10 s default) and, more importantly, cannot be
  serviced while the single thread is busy. Measured baseline: **9 submitters
  took 12.10 s wall (10.1–12.1 s each)** and the idle client was closed at the
  10 s timeout.
- The G3E per-connection latency growth is dominated by the small managed heap
  and uncollected garbage, not by a socket leak (fd count stayed flat).

### Shared vs. serialised state

- Shared: the single Glon session (memory `M`, global context, `http/current`,
  `http/response`), the single SQLite connection, and the file I/O queue.
- Must remain serialised: **all** entry into the Glon evaluator. The existing
  `http.lock` already covers dispatch, pre-pump, response extraction and
  `clear-current`; the only gap was the **post-response file pump**, which ran
  *outside* the lock.

### Specific limitation of the M1 scheduler for HTTP

The M1 cooperative scheduler **cannot** multiplex HTTPS requests as Glon tasks
with the current host boundary:

1. `begin_request` is synchronous and must produce the response before it
   returns; CivetWeb offers no request-suspension/deferral API.
2. The M1 scheduler (`run-tasks`) is not connected to the event loop; the HTTP
   path calls `r0_s1_session_run_block` directly.
3. `M1_MAX_TASKS = 3` is below the ten-client target, and raising it is a
   memory-map change this task forbids.

Making M1 drive HTTP would require a new host mechanism (a pending-connection
table plus deferred responses plus scheduler integration) — exactly the "new
task model" the task says to avoid without proof. I did **not** invent one.

## 2. What was implemented

An **explicit evaluator-ownership design** that lets the existing CivetWeb
worker pool service overlapping connections while a single Glon interpreter
stays serialised. No new scheduler, no M1 changes, no frozen-S1 change.

- `glon_http.h`: added `unsigned num_threads` (default 1) and
  `GLON_HTTP_MAX_THREADS 32`; documented the ownership rule.
- `glon_http.c`:
  - `--http-threads` sizes the CivetWeb pool, clamped to `[1, 32]` (default 1,
    so G1–G3E behaviour is unchanged).
  - The post-response **file pump now runs under `http.lock`**, closing the one
    gap so that *every* Glon session entry (dispatch, pre-pump, file pump,
  - response extraction, clear-current) is serialised by the single mutex.
- `glon.c`: added and validated `--http-threads N` (rejects 0 and >32).

**Thread-safety argument (evaluator ownership):** exactly one thread can be
inside the Glon session because `http.lock` is held across dispatch, the
pre-response pump, the post-response file pump, response extraction and
`clear-current`. Worker threads only add connection-level overlap (accept,
read request head/body, write response on their own connection); they never
touch the evaluator, the SQLite connection or the file queue concurrently.
The main loop only reads `volatile` counters. The signal handler only sets a
`sig_atomic_t` flag.

**Cooperative tasks vs. CivetWeb threads:** the delivered concurrency uses
**CivetWeb worker threads + the existing asynchronous SQLite interface**, with a
**single serialised Glon interpreter**. It does **not** use M1 cooperative
tasks (see the limitation above). The "multitasking" reused is the existing
async request/reply machinery, not `spawn`/`yield`.

## 3. Demonstration (tests/https-g3f/acceptance.py)

Application unchanged (`examples/https-g3e/messages.glon`). Python only drives
sockets and inspects SQLite.

1. **Overlap + idle client** — one idle keep-alive socket (A) plus nine
   submitters (B–J), `--http-threads 10`:
   - all 9 → 201, unique ids;
   - **wall 2.06 s < sum of latencies 9.35 s** (genuine overlap, not sequential);
   - A still served (200) after the others ran.
   - Baseline single worker (`--http-threads 1`): same 9 submitters took
     **12.10 s wall** — ~6× slower, confirming the overlap is real.
2. **Restart** — 6 messages, stop, restart with the same pool, all ids correct.
3. **Slow/trickling client** — a client dribbles a POST body over ~2 s; a
   normal client during that window completes in **0.06 s**; both messages are
   stored with unique ids (no corruption, no blocking).
4. **Worker-pool bound** — `--http-threads 0` and `33` are rejected (rc≠0).
5. **Memory** — 20 overlapping rounds (4 concurrent each), peak RSS sampled.

## 4. Measurements

Configuration: `MODULES=file,http,sqlite`, `MEM_PROFILE=large`,
`MEM_LOADER_CELLS=500000`, `--http-threads 10` for the concurrency tests.

- Executable size (`glon`): **485,408 bytes** (~474 KiB).
- Peak resident memory (G3F suite): **22,988 KiB** (an earlier run 21,620 KiB).
- Latency, idle client present: submitters 0.2–2.0 s, wall 2.06 s (vs 12.10 s
  single-worker baseline).
- Latency, slow client: fast client 0.06 s while a peer trickles for ~2 s.
- Maximum simultaneous connections tested: **10** (1 idle + 9 active).
- Queue depth: bounded CivetWeb worker pool of 10 (cap 32); the accept backlog
  is the OS/CivetWeb default. No unbounded Glon-side queue exists.

## 5. Tests executed

Passed:

- `./check-frozen-s1.sh` — S1 unchanged.
- `make sqlite-test` — all SQLite service C tests.
- `tests/sqlite-g3d/acceptance.py`, `tests/sqlite-g3d/http_sqlite_test.py`.
- `tests/https-g1/acceptance.py`, `tests/https-g2/acceptance.py`
  (also under `MODULES=file,http` earlier in the session).
- `tests/https-g3/acceptance.py` (`MODULES=http GLON_HTTP_RECLAIM_M1=1`).
- `tests/https-g3a/acceptance.py --bulk-adds 5` (large profile).
- `tests/https-g3e/acceptance.py` (default single-worker, unchanged behaviour).
- `tests/https-g3f/acceptance.py` — all five groups.

Failed / blocked:

- `tests/https-g3a/acceptance.py --bulk-adds 40` failed at the restart step
  with empty stderr: the second server was OOM-killed (the agent process alone
  held ~637 MB of the 967 MB box). Re-running with `--bulk-adds 5` passed; this
  is an environment memory limit, not a code defect.
- 100,000-request endurance and large-scale (>10) connection counts were not
  run (1 GB budget).

## 6. Remaining constraints and unresolved decisions

- **Not Glon M1 task scheduling.** The delivered model is CivetWeb threads + a
  single serialised interpreter + async SQLite. Wiring M1 to HTTP needs a host
  request-queue/deferred-response design that is out of scope here.
- **Response write holds the lock.** A client that stops reading a very large
  response could hold the lock up to `request_timeout_ms`; responses here are
  small, and request-head/body reads happen before the lock, so a slow/stalled
  uploader cannot monopolise the interpreter (demonstrated).
- **Worker pool is the bound.** More simultaneous connections than
  `--http-threads` wait in the OS accept backlog. 32 is the hard cap.
- **Heap/GC latency growth** under many requests persists (small managed heap);
  the suite is bounded accordingly.
- **Decision to record:** `GLON-HTTPS-SERVER-G1.md` cautioned that raising
  `num_threads` needs an explicit evaluator-ownership design. This branch adds
  that design (single mutex across all session entry, default unchanged at 1)
  and should be reflected in the architecture memory before any default change.

## Reproduce

```
make https-g3f-test
```

Builds the large-profile `file,http,sqlite` binary, generates the local test
certificate, and runs the bounded concurrency suite. Do not expose the
experimental HTTPS server to the public internet.