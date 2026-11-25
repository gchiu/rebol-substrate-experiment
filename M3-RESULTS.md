# M3 — Multiple outstanding asynchronous I/O operations: results

Date: 2026-11-25
Branch: `experiment/glon-m3`
Baseline: M2, commit `73c1384`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Audit: where the single-outstanding restriction lived

The restriction was a **combination**, not one place:

1. **Glon module state.** `modules/file.glon` kept a single `io/pending`
   record; `io/request` raised `'busy` if one was set. `modules/sqlite.glon`
   kept a single `sqlite/pending` and raised `'busy`.
2. **Host file driver.** `glon_io_file_cli.c` is a one-outstanding-request
   driver: `glon_io_cli_submit` rejects if `cli->running`, spawns a worker
   thread, and `glon_io_cli_wait` blocks until it finishes.
3. **Host SQLite.** `drain_sqlite` executes one statement synchronously on the
   single connection.

The host **drain loops already existed** (`drain_io`, `drain_sqlite` loop until
the Glon queue is empty), so the host could already process many requests in
sequence. The missing piece was that the Glon modules only ever exposed **one**
pending request. The task/GC side (M2) already roots whatever the modules hold.

## 2. What was implemented

A small, general-purpose **bounded FIFO of pending requests**, correlated by
id, added to the existing modules. No new scheduler, no OS threads, no second
event loop, no host redesign.

- **`modules/file.glon`** and **`modules/sqlite.glon`**: replaced the single
  pending slot with:
  - `io/queue` — FIFO of records not yet taken by the host,
  - `io/inflight` — records handed to the host, kept so the collector still
    roots their success/failure closures until the reply arrives,
  - `io/count` and `io/max-pending` (default 16) — bounded capacity.
  - `io/request` enqueues (rejecting with a `'capacity` SIN! when full),
    `io/take-request` pops the head into `inflight`, and `io/reply` finds the
    record **by id**, removes it, decrements the count and delivers to that
    task's own success/failure closure.
- The record shape is `[id op fields success failure arg]` (file) /
  `[id op fields success failure]` (sqlite); the host still receives the same
  `[id op fields]` frame, so the host protocol is unchanged.
- **Host execution stays serialised** (one outstanding host request; one SQLite
  connection). Many Glon tasks can be **pending simultaneously**; the host
  drains them in FIFO order and each reply resumes its originating task. This is
  the same "serialise the shared resource" allowance the milestone makes for
  SQLite.
- `glon.c`: the task pump error now reports the halt reason (diagnostic).
- `tests/io-file/fixtures/bad_frame.glon`: adapted to inject a malformed record
  into `io/queue` (the old fixture poked the removed `io/pending-request`).

The M2 scheduler, `modules/task.glon`, the host task pump and the frozen S1 are
unchanged.

## 3. Demonstration (examples/m3, tests/m3)

- `file-fanout.glon`: ten tasks each `file/read` a distinct file and suspend;
  each resumes with **its own** content (`R0=content-0` … `R9=content-9`).
- `sqlite-fanout.glon`: ten tasks each run a **parameterised** INSERT and
  suspend; the single connection serialises the statements; the table ends with
  ten rows `v0..v9` and unique ids.
- Acceptance (`tests/m3/acceptance.py`) also covers:
  - an I/O error in one task (missing file) is isolated — that task gets an
    error, the other nine get correct results;
  - capacity overflow (`io/max-pending: 4`, ten tasks) admits four and rejects
    the rest cleanly, with the four admitted reads intact;
  - cancellation while I/O is pending is safe and the queue stays usable for a
    follow-up task;
  - pending state reclaimed in the completion callback (`io/count` zero);
  - 30 repeated reads complete with flat memory.

## 4. Measurements

Configuration: `MODULES=file,sqlite,task`, `MEM_PROFILE=large`,
`MEM_LOADER_CELLS=500000`, `M1_TASKS=10` (per-task ~1,000 cells).

- Maximum outstanding requests tested: **10** (ten tasks, each with an
  independent request in flight).
- Independently waiting tasks: **10**.
- Executable size: **299,824 bytes** (~293 KiB).
- Peak resident memory (M3 suite): **7,996 KiB**; the repeated-read run stayed
  flat.
- Queue capacity: default **16** (`io/max-pending`), overflow rejected with a
  `'capacity` SIN! and no corruption.
- Throughput: the host serialises, so the ten reads/inserts complete in FIFO
  order; the per-task result is matched by id, not by completion order.

## 5. Tests executed

Passed:
- `./check-frozen-s1.sh` (S1 unchanged).
- `make m1-smoke` (M1 A–K, 1,000-switch stress).
- `make io-file-test` (39/39, including the adapted `bad_frame` fixture).
- `make sqlite-test`, `tests/sqlite-g3d/acceptance.py`,
  `tests/sqlite-g3d/http_sqlite_test.py`.
- `tests/https-g1`, `tests/https-g2`, `tests/https-g3` acceptance.
- `tests/https-g3e/acceptance.py`, `tests/https-g3f/acceptance.py`.
- `tests/m3/acceptance.py` (all seven groups).

Not run: the full `make test` monolith (the relevant M1/file/SQLite/HTTPS
suites were run individually under the 1 GB budget).

## 6. Implemented vs. proposed, and limitations

Implemented:
- Multiple outstanding file and SQLite requests from different tasks, bounded
  capacity, id-correlated delivery, independent success/failure, cancellation,
  error isolation, reclamation, and a mixed file+SQLite workload (both modules
  are drained by the same host task pump).

Limitations / notes:
- **Host execution is serialised.** The file driver has one slot and SQLite has
  one connection, so requests are processed FIFO; "outstanding" means many
  Glon tasks are pending at once, not that the host runs them in parallel. This
  is the documented allowance for a shared resource; genuine parallel host I/O
  would need a bounded worker pool in `glon_io_file_cli.c` (a proposal, not
  implemented, to avoid OS-thread-per-task and thread-safety risk).
- **Task slots are not reused** (M2 behaviour): a finished/cancelled task keeps
  its slot, so a long-running appliance is bounded to `M1_MAX_TASKS` (10)
  spawns. Reuse with a per-slot generation is a possible follow-up.
- **Per-task stacks are ~1,000 cells** (ten tasks share the arena), so task
  recursion is shallow.
- The top-level Glon block runs **before** the final host drain, so an
  in-app check of `io/count` at the very end sees the last requests still
  queued; reclamation is therefore asserted from a completion callback (after
  `io/reply` removed the record) and by a follow-up task.

## Reproduce

```
make m3-test
```

Builds the large-profile `file,sqlite,task` CLI and runs the bounded
multi-outstanding suite. The frozen 64K conformance build is unaffected (it does
not select `MODULES=task`).