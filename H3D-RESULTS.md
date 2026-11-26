# H3D — HTTP Task Lifetime and Cancellation (investigation checkpoint)

Date: 2026-11-26
Branch: `experiment/glon-http-h3d`
Baseline: H3C, commit `777f244`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk (tmpfs `/tmp`).

**Status: INCOMPLETE.** The loader-reclamation safety question is answered
(Section 2). The 1,000-request stress criterion was **not completed** because
the host is throughput-limited (Section 4). No memory-safety failure was found.
No C change was required; the H3C fix is retained unchanged.

## 1. Baseline verification

- Branch `experiment/glon-http-h3d` created from `777f244` (H3C).
- `./check-frozen-s1.sh`: frozen S1 unchanged — PASS.
- `make h3c-test` (method echo, POST/SQLite effect, interleaving) — PASS.
- `H3B-RESULTS.md` and other untracked files preserved.
- `glon_http.c` has **no H3D modifications** (temporary diagnostics removed).

## 2. Task/loader lifetime invariant (the core question)

**Question:** can the H3C loader reclamation (hold the loader across
spawn+run-tasks, reclaim to the checkpoint afterward) invalidate a task that
remains suspended across pump cycles?

**Answer: No, within the documented execution model.** The body block is read
only during the task's first slice, which runs before the reclaim, and is never
traced or re-read afterward.

Reasoning, with evidence from the source:

1. The spawn body block is parsed into the **loader** arena by
   `http_run_expression` (the spawn call uses `hold=1`, so the loader frontier
   is not reset yet).
2. `mnew-task` (modules/task.glon:58) stores the body block **by reference** in
   the task's wrapper (`wrapper[2]`); the wrapper lives in the M1 arena and is
   reachable as `TREC_BLK`.
3. The task's **first slice** runs inside `run-tasks` (step 2 of
   `http_bridge_process_tasks`), **before** the reclaim (step 2b). In that slice
   `do body` evaluates `http/task-run` and its seven argument expressions (the
   `mk-string` calls that read the nested loader blocks) and calls
   `http/task-run`. **All body-block reads happen here.**
4. Step 2b reclaims the loader (`M[GC_LOADER_HP] = lhp_ckpt`). The body block
   memory is freed.
5. A task that suspends (SQLite/file I/O) resumes inside `http/task-run` — a
   file-loaded function in the **permanent** loader region below the checkpoint —
   not in the spawn body block.

Why the freed body block is never re-read or traced:

- The `do` frame keeps `RV_BLK/RV_CUR/RV_END` as **raw loader addresses** on the
  task return stack. The GC does **not** trace them:
  r0_s1_runtime.c:1249-1250 ("BLK is a permanent loader block and CUR/END are
  raw loader-block addresses, so they are never traced"); a loader block
  encountered by the GC is a fail-stop (r0_s1_runtime.c:1417).
- When `http/task-run` returns, `do` compares `RV_CUR` to `RV_END` (addresses).
  The spawn body is exactly `[ http/task-run <7 args> ]` = 8 elements, all
  consumed by the call, so `RV_CUR == RV_END` and `do` ends **without
  dereferencing** the freed block.
- The GC traces each runnable task's `TREC_CTX` (activation context),
  `TREC_FRAME`, `TREC_RESP` (r0_s1_runtime.c:1579-1581) — the managed-heap
  objects (request block, strings, closures). It does **not** trace `TREC_BLK`
  (the wrapper/body). So the freed loader body is never a GC root.

**Evidence (measured, diagnostics since removed):** during a 500-request run the
owner loop sampled loader HP, `http/responses` length and live task count every
50 pumps. All were **constant** (lhp=1133853, responses=2, live_tasks=3) while
`completed` rose — i.e. no loader growth, no response leak, no task leak. The
server kept answering correctly. The 20-request smoke and the H3C interleaving
trace (start/wait 1,2,3 then resume 1,2,3) confirm suspended tasks resume
correctly.

**Documented invariant:** *a spawned HTTP task's body block is consumed in the
task's first slice, which the owner runs in the same pump as the spawn and before
the loader reclaim; after that slice the body block is neither read nor traced,
so reclaiming the loader is safe even for tasks that stay suspended for many
pumps.* The safe reclamation point is immediately after `run-tasks` returns in
`http_bridge_process_tasks`.

## 3. Cancellation / disconnection (tested)

- **Client disconnects before/while suspended** (`test_disconnect`): 6 abrupt
  mid-request closes; the server still served a correct request (count=0) — PASS.
- **Application error isolation** (`test_isolation`, tests/h3d/error.glon):
  `/boom` raises → 500; a following `/` → 200 "ok" — PASS.
- **Clean shutdown + restart** (`test_restart`): SQLite count persisted across a
  clean SIGTERM restart (1 → 2) — PASS.
- Task/bridge capacity (20-client burst) — **UNTESTED** (not run; the 20-way
  burst is slow on this host and was not attempted after the stress loop was
  stopped).

## 4. Repeated operation (NOT completed)

A 1,000-request mixed GET/POST stress was attempted with several client
strategies (new connections, keep-alive, 1–3 workers). **None completed in
time.** Measured steady-state throughput on this 2-vCPU host is ~2–5 req/s
(≈400 ms/request), so 1,000 requests needs ~7+ minutes; the run was stopped per
instruction.

This is a **throughput limitation, not a correctness or memory-safety defect**:

- No leak observed: file descriptors constant (5), RSS bounded (~20 MiB),
  SQLite file bounded, loader HP constant, `http/responses` constant, live task
  count constant.
- The 20-request smoke (GET=10, POST=10, bad=0, final count=10) — PASS.
- Keep-alive connections starve under sustained load (known limitation,
  AGENTS.md); new-connection clients are reliable but slow.

## 5. Test inventory

| Test | Result |
|---|---|
| frozen S1 (`check-frozen-s1.sh`) | PASS |
| H3C acceptance (method echo, SQLite effect, interleaving) | PASS |
| 20-request mixed smoke (GET/POST, count) | PASS |
| isolation (raise → 500, others 200) | PASS |
| disconnect (abrupt close, server healthy) | PASS |
| restart (count persists) | PASS |
| loader-reclamation safety (invariant + sampling) | PASS (no leak) |
| 1,000-request stress | **NOT COMPLETED** (host throughput) |
| capacity (20-client burst) | **UNTESTED** |

## 6. Deliverables

- `tests/h3d/acceptance.py` — stress, overlap, disconnect, capacity, isolation,
  restart (the 1,000-run is correct but slow on this host).
- `tests/h3d/error.glon` — error-injection app.
- `make h3d-test` target.
- This file.

## 7. Unresolved / next

- The 1,000-request acceptance criterion is **not demonstrated** on this host
  due to throughput. A faster host, a lighter stress app, or a reduced request
  count would be needed; the correctness and memory-safety evidence above shows
  no defect, only slowness.
- Capacity (task/bridge full) automated coverage is not yet run.
- No scheduler redesign is proposed or needed; the H3C loader strategy is
  retained.