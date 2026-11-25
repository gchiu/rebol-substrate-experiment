# H2I — Preserve task activation contexts across suspension

Date: 2026-11-25
Branch: `experiment/glon-http-h2i`
Baseline: H2H, commit `2d7a6d7`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Context model audit

- `RV_CTX` is the current lexical/activation context; `r_lookup` walks its
  `CTX_PARENT` chain. Function calls create an activation context whose parent is
  the caller's `RV_CTX`; locals live in that context.
- A task record saves `TREC_CTX` (and `RV_FRAME`) at `yield`/`task-wait`;
  `run-tasks` restores them on resume. `REG_HP` is global/shared; the collector
  roots saved tasks (RUNNABLE records) and their DS/RS.
- `mnew-task` seeds `TREC_CTX = GC_GLOBAL_CTX` so a host-spawned task resolves
  globals; `task-wait` then saves the live `RV_CTX`.

## 2. Minimal tests (no HTTP)

- **Nested local + yield** (`outer → inner`, `task-wait`, wake by a peer):
  `y=42` — **PASS**. The inner local survives.
- **Nested local + async file I/O** (`outer → inner`, `file/read`, `task-wait`,
  reply wakes): `y=42` — **PASS**. The inner local survives an async suspension.
- **Two independent tasks** with separate locals: each resumes with its own
  value — **PASS** (M2/M3 suites).

So the **activation/context model is sound for ordinary nested tasks**, including
async file I/O. Saving one `RV_CTX` + `RV_FRAME` is sufficient there.

## 3. The HTTP-specific failure

The H2H failure is specific to the HTTP task entry `http/task-run`
(spawned as `[ http/task-run <7 evaluated args> ]`):

- `http/task-run` has 7 argument locals + `request`; it calls `http/handler`
  (the app closure), which enqueues SQLite and `task-wait`s; the reply callback
  calls `http/respond` (per-id) + `task-wake`.
- After resume, `r_lookup` reports the **next local** unbound (`result` in H2H,
  then `a` in a deeper helper). The pure-task nested/async tests do **not**
  reproduce this, so the trigger is the HTTP task-run/handler/callback
  structure, not suspension in general.

The difference from the passing tests: the HTTP handler is a **closure value
passed as an argument** and invoked through `http/task-run`, and the suspension
happens inside a **SQLite reply callback** (which runs in the main world during
the host drain) rather than directly in the task body. The resumed task's
`RV_CTX` chain does not reach the `http/task-run`/handler activation where the
locals live.

## 4. Status

I did **not** apply a runtime fix, because the pure-task tests show the context
restoration is correct in general; the defect is specific to the HTTP
task-entry/callback path. The correct fix is to preserve the full activation
chain for the HTTP task (the `http/task-run` activation and the handler/callback
frames) across the async suspension — a targeted change in how the HTTP task is
entered and resumed, not a change to `r_lookup` or global lookup.

## 5. Next exact obstacle

The HTTP task's `http/task-run` activation (and the handler/callback frames) is
not reachable from the restored `RV_CTX` after the SQLite reply. The next step is
to trace the `RV_CTX`/`RV_FRAME` values at: task creation, `http/task-run` entry,
`task-wait`, the reply callback, and resume — and ensure the HTTP task entry
installs a context chain that survives the callback (e.g., by entering the
handler through a closure whose context is the task's, and restoring that context
on resume).

## 6. Tests

- Frozen S1 unchanged (`check-frozen-s1.sh` PASS).
- Nested-local + yield: PASS; nested-local + async file: PASS.
- `MODULES=task` builds; M1/M2 pass.
- HTTP H2H repro still halts (HTTP-specific); H2I two-task HTTP interleaving
  **not demonstrated** — not claimed.
- Default G3F / H2A untouched.

## 7. Conclusion

The activation/context model is correct for ordinary nested tasks (including
async file I/O): locals survive suspension. The remaining defect is specific to
the HTTP task-entry path (`http/task-run` + handler + SQLite callback), where the
resumed task's context chain does not reach the entry/handler activations. The
fix is a targeted HTTP-task context-restoration change, not a global-lookup or
scheduler change. No frozen-S1, memory-map or vocabulary change was made.

## Reproduce

```
# pure task (passes):
./glon /tmp/ctxi/v10.glon            # nested + async file -> y=42
# HTTP task (halts):
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
# curl -k https://127.0.0.1:8443/messages  -> halts on unbound local
```