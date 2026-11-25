# H2B — Cooperative HTTP request scheduling: results

Date: 2026-11-25
Branch: `experiment/glon-http-h2b`
Baseline: H2A, commit `918b94c`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Audit

- **H2A bridge**: a bounded 16-slot request table; CivetWeb workers copy the
  request and wait on a condition variable without the lock; the interpreter
  owner (main thread) runs the Glon work under `http.lock` and signals the
  worker to send the stored response.
- **M2–M4**: `spawn`/`yield`/`run-tasks`/`task-wait`/`task-wake`, cooperative
  round-robin, generation-tagged handles, slot recycling. `M1_MAX_TASKS` is
  configurable; the default task build uses 10.
- **M3**: bounded FIFO of outstanding file/SQLite requests, id-correlated
  replies; the host drains them between turns.
- **Globals**: `http/current` and `http/response` are single-slot and unsafe for
  interleaved request tasks.
- **Frozen S1**: no new HOST id is available; a per-id response must be Glon
  state.

## 2. Implemented (opt-in `--http-tasks`, implies `--http-bridge`)

- Each admitted request is **spawned as a Glon task** (`[ spawn [ http/task-run
  <request> ] ]`); the owner loop runs `[ run-tasks ]` and then drains the async
  file/SQLite queues each turn.
- **Per-request state**: `http/task-mode` makes `http/request-field` trust the
  passed request (no global `http/current`); `http/respond` stores into a
  per-id `http/responses` table; `http/take-response-for id` collects and
  removes it. The owner stores the response in the H2A slot and signals the
  worker. No global `http/response` is used in task mode.
- **Task context**: `mnew-task` now records the **global** context
  (`GC_GLOBAL_CTX`) so a host-spawned task resolves the application vocabulary
  (verified: `spawn`, the app's globals and `http/respond` all resolve inside
  the task).
- H2A and G3F modes are unchanged and remain the default/fallback.

## 3. Blocking obstacle (why H2B is not demonstrated at 10 tasks)

The HTTP response path (`task-run → judge → handler → http/respond →
http/respond-with → reduce`) needs a **large per-task return stack**. Measured:
the default 3-task configuration (3,400 cells/task) served one request but a
subsequent task overflowed its return stack (`RP` reached ~3,731 cells). The
5-task (2,040 cells) and 10-task (1,000 cells) configurations fail-stop inside
the task (machine halt) because the per-task stack is too small.

The M1 arena is **10,200 cells** (constrained and large profiles). Ten tasks at
the ~3,400 cells the HTTP path needs would require ~34,000 cells. This is a
**memory-map limit**, not a code defect: the frozen 64K map and the large
profile's arena cannot give ten tasks enough return stack for this path.

**Minimal proposed resolution (needs approval, not done here):**
- increase the M1 arena (reduce `MEM_LOADER_CELLS` or raise `MEM_CELLS`) so each
  of ten tasks has ~3,400 cells — an explicitly approved scaling change; **or**
- reduce the HTTP respond path's per-task stack footprint below ~1,000 cells
  (inline the per-id response store in `http/task-run`, avoid the `judge`/
  `reduce` frames, and give the handler a shallow calling convention).

Per the milestone instruction, I did **not** ship an unsafe partial
implementation or claim success: the 10-task behaviour is **not** demonstrated.

## 4. Measurements

- Executable: **504,176 bytes** (task build) / **497,888 bytes** (default).
- Default G3E (non-task): PASS, peak RSS 18,032 KiB.
- H2A bridge (non-task): PASS, peak RSS 20,036 KiB.
- H2B task mode: single request served at 3 tasks; 5/10 tasks fail-stop
  (per-task return-stack exhaustion). Not a working 10-task demo.

## 5. Tests executed

Passed:
- `./check-frozen-s1.sh` (S1 unchanged).
- `tests/https-g3e/acceptance.py` (default G3F mode, unchanged).
- `tests/h2a/acceptance.py` (bridge mode, unchanged).

Blocked / not demonstrated:
- `tests/h2b/` 10-task cooperative HTTP interleaving — the per-task return
  stack needed by the HTTP respond path exceeds the 10,200-cell M1 arena for ten
  tasks. A 3-task run served a request but was not stable across tasks.

No existing assertion was weakened; G3F and H2A remain the supported HTTPS paths.

## 6. Remaining work

- The host-side task machinery (spawn per request, per-id response, global
  context, owner `run-tasks` + drain) is in place and opt-in.
- The blocker is capacity: ten tasks need more per-task return stack than the
  arena provides. Resolve via an approved arena increase or a reduced-stack
  response path, then re-run the H2B acceptance (overlap event trace, per-client
  correctness, restart, cancellation, overflow, memory).

## Reproduce (partial)

```
make glon MODULES=file,http,sqlite,task MEM_PROFILE=large MEM_LOADER_CELLS=500000
./glon --http-tasks --http-threads 10 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
```

This exercises the task path; it is not a passing 10-task demonstration (see §3).