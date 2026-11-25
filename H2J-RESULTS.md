# H2J — Isolate HTTP task-entry activation and context ownership

Date: 2026-11-25
Branch: `experiment/glon-http-h2j`
Baseline: H2I, commit `29641c7`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Controlled test matrix (no CivetWeb)

| Case | Entry | Async | Result |
|---|---|---|---|
| A/B | 0/1 arg, literal | task-wait | PASS |
| C | 7 args, literal | task-wait | **PASS** (`C=1`) |
| D | 7 args, dynamically-built body (`reduce`) | task-wait | **FAIL** (machine halt, reason 0) |
| E | 7 args, dynamic body | async file | FAIL (same as D) |
| F | 7 args, literal | async file | **PASS** (`F=1`) |
| G | 7 args, literal | async SQLite | **PASS** (`G=1`) |
| H | 7 args incl. `mk-string`/`reduce` values | task-wait | **PASS** (`H=1`) |
| Mock | `task-run`+handler+`http/respond`, spawn+run in one session | async file | **PASS** (`MOCK-DONE`) |

So: 7 args, `mk-string`/`reduce` argument values, async file and async SQLite
**all work** in a literal-block task. Two independent triggers fail:

1. **Dynamically-constructed task body** (Case D/E): a `reduce`-built body
   fail-stops (the same `do`/reduce-built-block limitation seen in H2C). The
   H2B body is host-built *source* (a parsed literal), so this is a separate
   limitation, not the H2B cause.
2. **Cross-session spawn/run** (the HTTP path): the mock (spawn + `run-tasks` in
   one session) passes, but H2B spawns in the HTTP dispatch turn and runs in a
   **later** host pump turn (`http_bridge_process_tasks`, a separate
   `http_run_expression`). This is the first divergence from the working model.

## 2. First divergence

The H2B task is created in one host session turn (the CivetWeb dispatch) and
first run in a **different** host session turn (the pre-response/task pump).
Between them the session resets `RV_CTX` to the global. The task's saved
`TREC_CTX` (the handler's activation) and its parent chain must survive that
boundary; the observed unbound local (`result`, then `a`) shows the chain from
the resumed `RV_CTX` does not reach the `http/task-run`/handler activations
created in the dispatch turn. The mock, which spawns and runs in the **same**
turn, does not reproduce it.

## 3. Status

I did **not** apply a runtime fix. The evidence points to the **cross-session
spawn/run boundary** (and, separately, the reduce-built-body limitation), not to
`r_lookup`, the GC sentry (disproven), or the 7-argument count. A safe fix is to
keep the task's activation chain reachable across the session boundary (e.g.,
spawn and first-run the task in the same session turn, or root/preserve the
dispatch-turn contexts), which is a targeted host/task change to design — not a
global-lookup or scheduler change.

## 4. Next exact obstacle

Make the HTTP task's spawn and first run occur in the same interpreter session
turn (or preserve the dispatch-turn activation chain across the pump boundary),
so the task's locals resolve after the async SQLite resume. The reduce-built-body
limitation (Case D) is a separate issue to avoid by passing a literal body.

## 5. Tests

- Frozen S1 unchanged (`check-frozen-s1.sh` PASS).
- Matrix: C/F/G/H/Mock PASS; D/E FAIL (dynamic body).
- `MODULES=task`/`file,task`/`file,sqlite,task` build; M1/M2 pass.
- HTTP H2J two-task interleaving **not demonstrated** — not claimed.
- Default G3F / H2A untouched.

## Reproduce

```
# dynamic body (fails):
./glon /tmp/h2j_d.glon
# mock (passes):
./glon --io-root /tmp/h2jm --allow-file-read --allow-file-write /tmp/h2jm/mock.glon
# HTTP (fails):
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
```