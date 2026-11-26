# GLON-HTTP-H3E — Task-mode HTTP response lifecycle

Date: 2026-11-26
Branch: `experiment/glon-http-h3e` (from `experiment/glon-s1-claude-d1`, `73fcc44`)
Host: Debian 13 VPS, 967 MiB RAM, 1 GiB swap. Builds and tests run one at a time.
Expanded task build: `MODULES=file,http,sqlite[,sec],task MEM_PROFILE=large
MEM_LOADER_CELLS=500000 MEM_CODE_CAP=20480 MEM_GLOBAL_CONTEXT_CAP=512
CODE_RESERVE=128 M1_TASKS=3 M1_TASK_CELLS=3400 M1_DS_OFF=400 M1_RS_OFF=3400
M1_WRAPPER_DELTA=256` (the existing M1 configuration).

**Result: immediate and asynchronous task-mode responses are both delivered.**
The new acceptance test `make h3e-test` passes, and it fails on the previous
code. G3H's routes all succeed in task mode once tasks have enough return
stack. With the existing 3×3400 configuration, one route still exhausts a
task's return stack (§6.1).

## 1. Reproduction (before the fix)

`tests/h3e/lifecycle.glon` has one route per case. Server started with
`--http-tasks --http-timeout-ms 3000`:

| route | handler | result |
|---|---|---|
| `/now` | `http/respond` directly | **503 "bridge request cancelled" after the 3 s deadline** |
| `/echo?a=1` | direct | **503** after the deadline |
| `/wait` | SQLite callback responds; task `task-wait`s | 200 |
| `/nowait` | SQLite callback responds; handler returns (G3H style) | 200, plus an orphaned 500 left in `http/responses` |
| `/fail` | raises | 500 "internal error" |
| `/none` | no response | 500 "no response" (from Glon's fallback) |

## 2. Root cause

There are four defects. Each one was confirmed in the source.

1. **`http/task-run` consumed its own response** (`modules/http.glon`). After the
   handler returned, it ran `either http/take-response-for id [none] [respond
   500 "no response"]`. `take-response-for` *removes* the response, so a response
   stored by an immediate handler was taken out and thrown away before the
   host's collector (step 4 of `http_bridge_process_tasks`) could see it.
   Async responses survived only because their callbacks run later, in the
   drain step, after this check had already found nothing.
   - The same check's fallback stored a 500 "no response" for every async
     handler that returned before its callback. These orphans built up in
     `http/responses`, and one could have been delivered instead of the real
     reply if the callback had arrived in a later turn.
2. **`http/take-response-for` examined only the head** of `http/responses`.
   Any other request's entry (or an orphan) at the head made it return NONE,
   which can block a request until its deadline.
3. **The host's "task finished without a response" check never fired**
   (`glon_http.c`). It passed the raw tagged handle cell to `task-state`. That
   gives Glon the handle ×16, which decodes to a different slot and generation,
   so it reads as stale (-1). It then divided the result by 16 a second time.
   So a lost response could never be detected, and the request hung until its
   deadline, which is the 503 in §1.
4. **Task mode never pumped file I/O while serving.** Bridge/task mode drained
   only the SQLite/security queues; the file pump ran at shutdown. A task-mode
   handler that waits on file I/O could not complete.

Two further problems, found while testing overlap and capacity:

5. Completions used `pthread_cond_signal` on a condition variable that all
   bridge workers share, so they woke one arbitrary waiter rather than the
   request that had completed.
6. When all M1 task slots were in use, `mnew-task`'s `-1` result was treated
   as a task handle. The request then got "no response" instead of waiting.

## 3. Corrected lifecycle and ownership rule

> Once a handler produces a response, it stays in `http/responses` until the
> host collects it or the request ends. A response is stored only for an
> **open** request id that has no response yet; the first one wins.

- **Open:** `http/task-run` records the id in `http/open-ids` and runs the
  handler under `judge`. It never touches the response. If the handler raises,
  it responds 500 "internal error", which is dropped if a response already
  exists.
- **Respond (task mode):** `http/respond-with` stores `[id status ctype headers
  body]` only if the id is open and has no stored response. Otherwise it drops
  the response silently. Raising would kill the server when the respond happens
  inside a SQLite/file callback.
- **Collect:** each owner turn runs (1) spawn, (2) `run-tasks`, (3) the file
  and pre-response drains repeated until idle, (4) collection. In step 4,
  `http/take-response-for id` finds the id's response *anywhere* in the list,
  removes it and closes the id. Later responses for that id are then dropped.
- **No response:** after step 3 every queued async operation has run its
  callback, because the drains are synchronous until their queues are empty.
  So a task that is no longer runnable and has no stored response can never be
  answered. The host sends **500 "no response"** at once, with no deadline
  wait. A task blocked in `task-wait` stays runnable, and its request waits
  for a callback or the deadline.
- **Close:** a worker that has finished with its slot (reply sent, timeout or
  disconnect) marks it `BRIDGE_CLOSING`. Workers still never enter the
  evaluator. In the next turn the owner runs `http/close-request id tk`, which
  closes the id and drops any uncollected response, then frees the slot. The
  task is cancelled **only** if the request was *abandoned* (ended without a
  delivered response), so a wait whose client has gone doesn't hold a task
  slot. A task whose response was delivered runs to its end. An early version
  cancelled it, and H3C's resume check caught that.
- **Capacity:** if no task slot is free, the request stays `QUEUED` and is
  spawned in a later turn when a slot frees, or it gets 503 at its deadline.
- **Wake-ups:** all bridge completions broadcast.

## 4. Source changes

- `modules/http.glon` (copied to `glon-lib/` by make):
  - added `http/open-ids`, `http/id-member?`, `http/id-remove` and
    `http/close-request`;
  - `respond-with` (task branch) now stores only for an open id with no
    response yet;
  - `take-response-for` now does a whole-list find, remove and close;
  - `response-find`/`response-remove` were rewritten without set-words;
  - `task-run` no longer consumes the response or stores a fallback.
  - The non-task branches are unchanged.
- `glon_http.c`:
  - new states `BRIDGE_NO_RESPONSE` and `BRIDGE_CLOSING`, and an `abandoned`
    flag;
  - the owner's close step (step 0);
  - the file pump runs in the drain loop;
  - corrected `task-state` query and "no response" decision;
  - requeue when all task slots are busy;
  - completions broadcast.
  - The direct (non-bridge) and H2A bridge paths are unchanged apart from
    `broadcast`.
- `Makefile`: new `h3e-test` target.
- `tests/h3e/lifecycle.glon` and `tests/h3e/acceptance.py`: the regression test.

## 5. Tests actually run

| test | result |
|---|---|
| **`make h3e-test`** (new): immediate, async ×2, 24 sequential mixed, 5 overlapping pairs + 3 at once (correlated), raise→500, raise-after-respond keeps 200, double respond→first, no-response→500 in ~70 ms, all 3 slots held→new request waits ~1 s and gets 200, 7 disconnected requests release their slots, answered tasks resume (5/5), no leftover responses or open ids, no host error | **PASS** |
| `tests/h3e/acceptance.py` on the **pre-fix** `glon_http.c` + `http.glon` | FAIL at the first check (`/now -> 503 'bridge request cancelled'`) |
| Same test with the close step cancelling every live task | FAIL (`0 of 5 answered tasks resumed`) |
| `tests/h3c/acceptance.py` (method echo, SQLite effect, 3-task suspend/resume interleave) | **PASS** |
| `tests/h2b/smoke.py` | PASS (partial by design) |
| `make h2a-test` (non-task bridge: isolation, disconnect, queue overflow, shutdown, 50 requests) | **PASS** |
| `make g3h-test` (G3H, direct mode) | **PASS** |
| G3H **task mode**, existing M1 3×3400 | 7/9 routes correct, including all user-registry routes (GET/POST users) and POST message, which persisted to disk *while the server ran*. `GET /api/messages` with a message present: **task return-stack fail-stop, server stops** (§6.1) |
| G3H task mode, diagnostic build `M1_TASK_CELLS=10000 M1_DS_OFF=1000 M1_RS_OFF=10000 MEM_M1_CELLS=30000` | **9/9 routes correct**, same output as non-task mode |
| Default constrained `make glon` | builds; nested closures 42/42; `global-ctx-hash-test` PASS |

The H3E/H3C/H2B results are from the final source. The H2A, G3H-smoke and
G3H task-mode runs came before the last edit, which only touched comments and
how the "no response" return statement is written; behaviour is identical.
Not run: H3D (1,000-request stress, excluded) and WASM targets.

## 6. Remaining limitations

1. **The existing M1 task stacks are too small for G3H's JSON responses.**
   `j/encode-string-loop` recurses once per character, and with a message
   present `GET /api/messages` needs more than the ~3,000 return-stack cells a
   3×3400 task has. The result is a machine fail-stop (not judgeable), and the
   whole server stops. This is the capacity limit H2B-RESULTS already measured,
   not a lifecycle defect: with 10,000-cell tasks every route works. Raising
   the default M1 configuration is a capacity change that needs approval
   (AGENTS.md), so it was not committed. The options are a larger arena
   (`MEM_M1_CELLS`, `M1_TASK_CELLS`; plenty of room in the large profile) or an
   iterative JSON string encoder. No build check ties `M1_TASKS × M1_TASK_CELLS`
   to `MEM_M1_CELLS`.
2. A handler that has already responded but never finishes (for example
   `task-wait` with no wake-up) keeps its task slot. It isn't abandoned, so
   it isn't cancelled.
3. Responses for closed or duplicate ids are dropped silently, with no log
   line or counter.
4. Only the request's own task may answer it in practice. A response produced
   by a different task after the owning task finished could arrive after the
   host has already decided "no response".
5. `make h3c-test`/`h2b-test` as written still use the default code cap and
   fail at load (pre-existing; see GLON-S1-CLAUDE-D1-RESULTS.md §5). `h3e-test`
   uses the expanded cap.
6. A client disconnect is detected only when the request's deadline passes or
   the reply write fails. CivetWeb gives the bridge no earlier signal.
