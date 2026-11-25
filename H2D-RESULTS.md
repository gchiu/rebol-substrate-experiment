# H2D — Task arena scaling and cooperative HTTPS integration: results

Date: 2026-11-25
Branch: `experiment/glon-http-h2d`
Baseline: H2C, commit `3a896a0`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Memory-layout audit

- The M1 arena size is `MEM_M1_CELLS` (Makefile, default **10,200**). The large
  profile computes `MEM_M1_BASE = MEM_LOAD_BASE + MEM_LOADER_CELLS`,
  `MEM_TASK_TABLE = MEM_M1_BASE + MEM_M1_CELLS`,
  `MEM_TOTAL = MEM_TASK_TABLE + 536 + 1024`, and generates `s1_profile.h` with
  `MEM_CELLS = MEM_TOTAL`. Raising `MEM_M1_CELLS` grows the total map (it does
  **not** overlap the loader, managed heap or GC worklist, which are laid out
  below `MEM_M1_BASE`).
- H2C measured ≈2,500 return-stack cells/task for the simple respond path and
  >3,400 with the SQLite chain. Ten tasks therefore need ≈32,000 cells.
- The 64K **conformance** profile is unchanged (default `MEM_M1_CELLS=10200`).

## 2. Configurable task-memory profiles

`MEM_M1_CELLS` is now tracked in `.glon-config` and can be set per build. With
`M1_TASKS=10` and `M1_TASK_CELLS = MEM_M1_CELLS / 10`:

| Profile | `MEM_M1_CELLS` | `M1_TASK_CELLS` (DS/RS) | 10-task result |
|---|---|---|---|
| compact (default) | 10,200 | 1,020 (200/820) | fail (RS too small) |
| **32k** | 32,000 | 3,200 (400/2,800) | **PASS** (`COUNT=10`) |
| 40k | 40,000 | 4,000 | builds (not run) |
| 48k | 48,000 | 4,800 | builds (not run) |

The 32k profile builds and runs ten cooperative tasks (`examples/m2/tasks.glon`
variant → `COUNT=10`). The other regions are unchanged; only the total map grows
(≈170 KB per +21,800 cells), which fits the 1 GB VPS.

Build example:
```
make glon MODULES=file,http,sqlite,task MEM_PROFILE=large MEM_LOADER_CELLS=500000 \
  MEM_M1_CELLS=32000 M1_TASKS=10 M1_TASK_CELLS=3200 M1_DS_OFF=400 M1_RS_OFF=3200 \
  M1_WRAPPER_DELTA=256
```

## 3. HTTPS + SQLite task integration (partial)

I fixed the H2B request encoding: the host now passes the seven request fields as
**evaluated arguments** to `http/task-run` (previously the request block was
passed as data, so `mk-string` was never evaluated and `http/method` saw a word,
causing a `type` error). With that fix:

- The task starts, enqueues the SQLite read, prints `wait`, and **suspends**
  (`task-wait`).
- The host drains SQLite, the reply callback calls `http/respond` (per-id) and
  `task-wake`; the task **resumes** (`resume`).
- The response is stored in `http/responses` (verified: a 2-element cons whose
  head is a 5-element `[id status ctype headers body]`).

**Remaining blocker:** the owner's collection (`http/take-response-for` →
`block-at`/`=`/`-` on the stored pair) **fail-stops (machine halt, reason 0) in
the task context** after `resume`, so the response is never delivered and the
client gets no reply. This is not a stack overflow (RP is within the per-task
RS); it is an unresolved runtime/Glon issue in the task context. I did not
introduce an unsafe workaround.

## 4. Acceptance status (3 / 5 / 10 tasks)

- **3 / 5 / 10 task slots** with the 32k arena: ten cooperative (non-HTTP)
  tasks complete; the HTTP+SQLite task path suspends/resumes but **does not
  deliver the response** (collection halts).
- The H2D final criterion (two HTTPS tasks, one suspends for SQLite, the other
  progresses, both correct) is **not demonstrated** — the event trace shows
  `start/wait/resume` but no correct response. Per the milestone instruction I
  do **not** claim success.

## 5. Regression safety

Default G3F and H2A modes are unchanged; G3E and the M1/file/SQLite/HTTPS
regressions pass (run during this milestone). Frozen S1 is unchanged
(`check-frozen-s1.sh` passes). No runtime semantics changed; the new
`TREC_RESP` field and `task-resp-*` words are inert unless used.

## 6. Measurements

- Executable: **505,360 bytes** (32k profile) / ~504 KB (default).
- 10-task (non-HTTP) peak RSS: ~4 MiB.
- Suspend/resume observed (`start`, `wait`, `resume`); response stored; delivery
  blocked.

## 7. Recommendation

- **Memory:** the 32k arena (option B) is the smallest measured profile that
  gives ten tasks a safe stack; it fits the existing architecture without
  overlapping other regions. This part is implemented and verified for
  non-HTTP tasks.
- **Integration:** the remaining work is the task-context response-collection
  halt. The next step is to isolate it (a minimal C-side trace of the faulting
  IP / a reduced `take-response-for`) rather than increasing memory further.

## Reproduce

```
make glon MODULES=file,http,sqlite,task MEM_PROFILE=large MEM_LOADER_CELLS=500000 \
  MEM_M1_CELLS=32000 M1_TASKS=10 M1_TASK_CELLS=3200 M1_DS_OFF=400 M1_RS_OFF=3200 \
  M1_WRAPPER_DELTA=256
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
```

The task suspends/resumes; response delivery is the open blocker (see §3).