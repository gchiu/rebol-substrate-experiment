# H3C — HTTP method propagation into cooperative Glon tasks

Date: 2026-11-26
Branch: `experiment/glon-http-h3c`
Baseline: H3B state, commit `660cce7` (the H3A/H3B consolidation; H3B was
committed onto h3a, so `660cce7` is the correct H3C base).
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk.

## 1. Root cause (demonstrated, not assumed)

The method was **not** lost in `reduce`, in the task context, or in CivetWeb.
It was lost because the spawned task's **body block lived in the loader area and
was freed before the task ran**.

Chain of evidence:

1. C bridge `http_bridge_begin` sets `s->method = "POST"` (correct).
2. `http_bridge_request_args` emits `mk-string [ 80 79 83 84 ]` (= "POST"); the
   generated spawn source was dumped and is correct:
   `[ spawn [ http/task-run 2 mk-string [ 80 79 83 84 ] mk-string [ /messages ] ... ] ]`.
3. A debug print at the top of `http/task-run` showed `method` was **already a
   single bad byte `0xE5`** at function entry, for both GET and POST — i.e. the
   argument was corrupted before `reduce` ran. Path, query, body and peer were
   intact and in the correct positions (so it was not a field shift).
4. `mnew-task` (modules/task.glon:58) stores the body block **by reference**
   (`wrapper[2] = body`); it does not copy it.
5. `http_run_expression` (glon_http.c) reset `M[GC_LOADER_HP]` on exit,
   freeing the just-parsed spawn block (and its nested `mk-string` argument
   blocks) **before** the host's `run-tasks` slice ran the task.
6. Disabling only that loader reset made `method` correct (`GET`/`POST`) and
   POST return `ok` (201) — confirming the lifetime, not the value, was wrong.

So the first operation at which the value changed was the loader-heap reset in
`http_run_expression`, which freed a block a live task still referenced.

## 2. Correction (smallest safe change, glon_http.c only)

- `http_run_expression_ex(src, oc, hold)` — a variant that can **hold** the
  loader frontier (`hold=1`) so the just-parsed block survives the call.
  `http_run_expression` is now `http_run_expression_ex(..., 0)` (unchanged
  behaviour for every other caller).
- `http_bridge_spawn_task` uses `hold=1`: the spawn body block stays alive in
  the loader until the task consumes it.
- `http_bridge_process_tasks` checkpoints `M[GC_LOADER_HP]` before spawning and
  restores it **after** `run-tasks` (step 2b). By then every spawned task has
  run its first slice and consumed its body block, so the per-request loader
  space is reclaimed and does not accumulate.

No change to frozen S1, the M1 scheduler, `mnew-task`, `reduce`, lexical
scoping, or the G3F/H2A default paths. `--http-tasks` remains opt-in. The
request record is still built by the same `reduce [ id method ... ]`; no field
is hard-coded and no method is special-cased.

## 3. Field integrity (seven-field request)

With distinct markers for all seven fields, the constructed request preserved
every value and type; the method is `POST`/`GET`/etc. exactly as received. The
method-echo app (`tests/h3c/method.glon`) returns `http/method request`, and
GET/POST/PUT/DELETE round-trip exactly — proving the method that reached Glon,
not inferred from the status.

## 4. HTTP behaviour (verified inside Glon)

- GET `/messages` → `200`, body `0` (GET handler, row count).
- POST `/messages` → `201`, body `ok` (POST handler; the POST branch is taken
  only when `str-eq method "POST"`, so this proves the method inside Glon).
- SQLite effect: count `0 → 1` after one POST (verified by a following GET).
- Unsupported methods are delivered verbatim (PUT→`PUT`, DELETE→`DELETE`), so
  they do **not** silently become GET.

## 5. Cooperative interleaving

Three overlapping mixed-method requests produced this trace (stderr):

```
start 1 / wait 1
start 2 / wait 2
start 3 / wait 3
resume 1 / resume 2 / resume 3
```

Every task suspended (`wait`) before any resumed, and responses were correctly
correlated (GET→`200`, POST→`201 ok`). This is genuine Glon task interleaving
on the shared heap, not parallel Glon evaluation.

## 6. Automated tests

`tests/h3c/acceptance.py` (target `make h3c-test`):

- method echo: GET/POST/PUT/DELETE round-trip exactly — PASS
- sqlite effect: POST inserts (count 0→1) and returns `ok` — PASS
- interleaving: 3 tasks suspend before any resume; responses correlated — PASS

`tests/h3c/method.glon` is the method-echo application.

## 7. Regression inventory (all PASS)

- `./check-frozen-s1.sh` — frozen S1 unchanged — PASS
- `./s1` (full conformance, incl. reduce/debugger tests) — PASS (rc=0)
- `make test` components (s1, gc-worklist-overflow, primer) — PASS
- `m2-test`, `m3-test`, `m4-test` (cooperative tasks) — PASS
- `h2a-test` (bounded async bridge) — PASS
- `h2q-test` (lookup provenance / no global fallback) — PASS
- `h2b-test` (partial task smoke) — PASS
- `https-g3e-test`, `https-g3f-test` (default HTTPS paths) — PASS
- `json-test` — PASS

No assertions were weakened. No test was skipped.

## 8. Resource measurements (H3C build: file,http,sqlite,task, large profile)

- Executable size: 511,992 bytes (~500 KiB).
- Peak RSS under mixed load: ~21,936 KiB (~21 MiB).
- Task capacity: 3 (M1_TASKS=3); bridge slots: 16.
- Loader space: reclaimed each pump (checkpoint/restore), no per-request growth.
- Default constrained profile unchanged.

## 9. Reproduce

```
make h3c-test
# or:
make glon MODULES=file,http,sqlite,task MEM_PROFILE=large MEM_LOADER_CELLS=500000 \
     M1_TASKS=3 M1_TASK_CELLS=3400 M1_DS_OFF=400 M1_RS_OFF=3400 M1_WRAPPER_DELTA=256
tests/https-g1/make-cert.sh
python3 tests/h3c/acceptance.py
```

## 10. Note on the H3B results file

`H3B-RESULTS.md` was produced during H3B and is preserved untracked; H3B was
committed onto the h3a branch (no separate h3b branch existed). H3C is based on
that commit (`660cce7`).