# H2O-R — Debug host-spawned HTTP tasks using Glon's existing debugger

Date: 2026-11-25
Branch: `experiment/glon-http-h2o-debug`
Baseline: H2N, commit `e1f20d7`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. D1 debugger audit

Located `DEBUGGER-D1-RESULTS.md`, `DEBUGGER-D1-FREEZE.md`,
`DEBUGGER-D1-HARDENING.md`, `r0_s1_debug_tests.c`, `examples/debugger-*.r0`.

Verified capabilities:
- `debug-break` (RAW, arity 0) — cooperative breakpoint.
- State capture: SP, RP, HP, `RV_CTX`, `RV_FRAME` (`state-get`).
- `ctx-lookup ctx 'name` — inspect a binding in a context.
- `frame-get`, `frame-depth` — frame-chain inspection.
- Continue / resume; passable state; inspection allocations are isolated.

**Not available:** single-step / instruction stepping, and **task selection**
(D1 captures the *current* world; it cannot attach to an arbitrary suspended
task record). Breakpoints are the only control; there is no "step to next
instruction".

Consequence: D1 can inspect the current context/frame at a breakpoint, but it
**cannot step across the async SQLite resume or select the suspended HTTP task**
to compare its saved vs live context. Using it for this defect would require a
new task-selection/step adapter — out of scope for this milestone.

## 2. C-side diagnostic comparison

I used the existing `r0_s1_dump_ip` (opcode window + `[word=<id> name=<spelling>
ctx=<ctx>]`) at the fail-stop, which is the minimal host diagnostic for the
boundary D1 cannot reach.

- **Passing Glon-spawned SQLite mock** (H2L): runs to completion, no fail-stop;
  nested locals preserved.
- **Failing C-spawned HTTP task** (`http_bridge_spawn_task` + `--http-tasks`):
  halts at `r_lookup` on word 36 = `a`, `ctx=1622167` (a task-arena context),
  after the SQLite reply + `task-wake`.

Both exercise the same `task-run` → handler → SQLite → `task-wait` → reply →
`task-wake` structure; the difference is the **C-spawned entry path**
(`http_run_expression`) versus Glon `spawn`, not SQLite/args/session/loader
(ruled out in H2G/H2M/H2N).

## 3. First invalid transition (best current evidence)

The earliest divergence is at the **async SQLite resume**: the C-spawned task's
restored `RV_CTX` chain does not reach the activation that owns `a`, whereas the
Glon-spawned mock's does. This is consistent across H2K (same-session spawn+run
fixed first run, not resume), H2L (mock passes), H2M (session ruled out), H2N
(loader heap ruled out). The precise instruction that drops/overwrites the link
is not yet isolated because D1 cannot step/select the task; the next step is a
targeted C breakpoint at the `task-wait` save and the resume restore to compare
the saved `TREC_CTX`/`TREC_FRAME` with the live `RV_CTX`/`RV_FRAME`.

## 4. Status

No runtime fix applied (per the milestone: diagnose, do not fix). D1 is
insufficient for cross-task stepping; the C-side `r0_s1_dump_ip` is the minimal
adapter used. Frozen S1 unchanged; default G3F/H2A untouched; task build + M1/M2
pass. The two-task HTTPS interleaving is **not** demonstrated and not claimed.

## 5. Tests

- D1 debugger tests: not re-run (no debugger code changed).
- Frozen S1: PASS.
- Passing SQLite mock (H2L): PASS; failing HTTP repro: halts on `a`.
- Default G3F/H2A untouched; task build + M1/M2 pass.

## Reproduce

```
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
# curl -k https://127.0.0.1:8443/messages  -> halts on unbound local (a)
```

The halt prints `[word=36 name=a ctx=1622167]` via `r0_s1_dump_ip`.