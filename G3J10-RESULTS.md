# G3J.10 — Verify Authentication with HTTP Task Mode

Date: 2026-11-26
Branch: `experiment/glon-http-g3j10`
Baseline: G3J.9, commit `bb728bf`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.

## 1. Execution paths

- `tests/g3j/min_auth.glon` (G3J.9) has **no `task-wait`** → it is a **non-task**
  app: the host runs `[ http/dispatch … ]` synchronously, the handler queues the
  async `sec/random → sec/scrypt → sqlite` chain and returns, then the host's
  pre-response pump (`drain_pre`: sec + sqlite) drains it.
- Created `tests/g3j/min_auth_task.glon` — a task-mode variant that responds in
  the callback and calls `task-wake me` + `task-wait` (the cooperative pattern).

## 2. Controlled comparison (fresh isolated DBs)

**A. Non-task** (`MODULES=file,http,sqlite,sec`, `min_auth.glon`):
- `POST /setup` → **500 "no response"**
- `POST /login` (correct) → 401
- (G3J.9: the async chain does not complete before the response is collected.)

**B. Task mode** (`MODULES=file,http,sqlite,sec,task` + M1 config, `--http-tasks`,
`min_auth_task.glon`):
- **Cannot start.** The task build fails while loading `modules/json.glon`:
  `r0_s1: masm code space exhausted: the block needs 31 cells, 14 are free below
  the code limit 16128` → `parse error: too_large`.
- The M1 task configuration (M1_DS_OFF=400, M1_RS_OFF=3400, M1_WRAPPER_DELTA=256)
  lowers the code limit to 16128; `json.glon` does not fit. The non-task build has
  a larger code space and loads `json.glon` fine.

## 3. Diagnosis (execution boundary)

- **Non-task:** the request **ends before its async security/DB operations finish**
  — the handler returns, http/dispatch sets the "no response" fallback, and the
  pre-pump chain does not complete in time → 500 "no response". (G3J.9.)
- **Task mode:** blocked at **build/load time** — the task build's reduced code
  space cannot load `json.glon` (`too_large`). The app never runs, so no request
  is processed.

**Earliest incomplete operation:** in task mode, loading `modules/json.glon`
fails (masm code space exhausted). In non-task mode, the async setup chain does not
complete before response collection.

## 4. Interface difference (task mode)

`min_auth.glon` (no `task-wait`) is incompatible with `--http-tasks`: the task
entry (`http/task-run`) would consume the synchronously-stored response. A task
variant must respond in the async callback and `task-wake` + `task-wait` (as in
`min_auth_task.glon`). That variant is blocked by the task-build code-space limit,
not by the app.

## 5. Status vs criteria

- Execution paths inspected — DONE.
- Non-task comparison — DONE (setup 500 no response).
- Task-mode comparison — **BLOCKED** (task build cannot load json.glon: code space).
- Frozen S1 / runtime / loader / capacity / auth design — unchanged.

## 6. Reproduce

```
# A (non-task)
make glon MODULES=file,http,sqlite,sec MEM_PROFILE=large MEM_LOADER_CELLS=500000
./glon --http-bind 127.0.0.1 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write \
  modules/json.glon tests/g3j/min_auth.glon        # POST /setup -> 500 no response

# B (task) -- fails to load json.glon
make glon MODULES=file,http,sqlite,sec,task MEM_PROFILE=large MEM_LOADER_CELLS=500000 \
     M1_TASKS=3 M1_TASK_CELLS=3400 M1_DS_OFF=400 M1_RS_OFF=3400 M1_WRAPPER_DELTA=256
./glon --http-tasks --http-threads 4 ... modules/json.glon tests/g3j/min_auth_task.glon
# -> r0_s1: masm code space exhausted ... json.glon parse error: too_large
```

## 6. Commit

Branch `experiment/glon-http-g3j10`, one local commit. Not pushed/merged/deployed.