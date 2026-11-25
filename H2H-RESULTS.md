# H2H — Diagnose unbound word and task context restoration

Date: 2026-11-25
Branch: `experiment/glon-http-h2h`
Baseline: H2G, commit `088e9d5`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Word 3490 resolved

I added the symbol name to the halt dump (`syms[word_id(M[RV_WORD])]`). The
H2G "word 3490" was the raw cell; the **word id is 218 = `result`**:

```
[ip=11954 ops 2 6 15 7 0 12227]
[word=218 name=result ctx=1622167]
```

`result` is the **local binding in `http/task-run`**
(`result: judge [ http/handler :request ]`). It is looked up in the task's
context (`ctx=1622167`, a task-arena context) after the task resumes, and is
**not found**.

Removing the `result` local (inlining the `judge` into the `either`) exposes the
next unbound local, **`a`** (word 36) — a local in a deeper helper activation.
So the fault is **not a specific vocabulary word**: it is that the task's
context chain after resume does **not include the intermediate activation
contexts** (task-run's, the handler's, the SQLite-callback's) where those locals
live.

## 2. Root cause

- A task is a suspended world; `mnew-task` seeds `TREC_CTX = GC_GLOBAL_CTX`
  (so a host-spawned task resolves globals). `task-wait` saves the live
  `RV_CTX`/`RV_FRAME` into the record.
- The async SQLite reply runs in the **main world** (the host drain); its
  callback calls `http/respond` (per-id) and `task-wake`.
- On resume, `run-tasks` restores `RV_CTX`/`RV_FRAME` from the record. The
  lookup of a local (`result`, `a`) walks `RV_CTX`'s parent chain — but the
  intermediate activation contexts created by `http/task-run` → `handler` →
  callback are **not reachable** from the restored `RV_CTX`, so the local is
  unbound and `r_lookup` fail-stops (reason 429).

This is a **Glon activation/context lifetime issue across an asynchronous
suspension**, not a vocabulary-definition error, not the GC sentry (disproven in
H2G), and not stack/heap corruption. The `r_lookup` fail-stop is the correct
behaviour for a genuinely unbound word; the defect is that a *live* local is
treated as unbound after resume.

## 3. Minimal reproducer

`examples/h2b/tasks.glon` with `--http-tasks` (the H2B app). The first
`GET /messages` reaches `resume` and then halts on the unbound local. A pure
task that yields (no async) keeps its locals (M2 passes), so the trigger is the
**async suspension + main-world callback + resume**.

## 4. Status / next obstacle

I did **not** apply a runtime fix, because the correct fix is in the task
context/activation restoration (a runtime contract): the resumed task must
restore the **full activation chain** (the `RV_CTX` parent links and `RV_FRAME`
chain) that was live at `task-wait`, not just the single `TREC_CTX`. Changing
that touches the evaluator's context model and must be designed carefully.

Application-level mitigation (partial): avoid locals in `http/task-run` (done:
removed `result`), but the handler/callback chain still has locals (`a`), so
this only moves the failure. A robust application-level fix would require the
task body and all its helpers to avoid locals that must survive a suspension —
not practical for real apps.

## 5. Tests

- Frozen S1 unchanged (`check-frozen-s1.sh` passes).
- `MODULES=task` builds; M1/M2 pass.
- H2E/H2G reproduction still halts (unbound local); H2H two-task HTTP
  interleaving **not demonstrated** — not claimed.
- Default G3F / H2A modes untouched.

## 6. Conclusion

Word 3490 → id 218 = **`result`** (a `http/task-run` local); after removing it,
id 36 = **`a`** (a deeper helper local). The fail-stop is `r_lookup` on a local
that is live at the suspension point but unreachable after resume, because the
task's restored context does not chain to the intermediate activation contexts.
The precise next fix is to preserve/restore the full activation chain across an
async suspension (a runtime context-model change), or to restructure the task
entry so no locals must survive the suspension. No frozen-S1 or scheduler
change was made.

## Reproduce

```
make glon MODULES=file,http,sqlite,task MEM_PROFILE=large MEM_LOADER_CELLS=500000
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
# curl -k https://127.0.0.1:8443/messages  -> halts on unbound local (result/a)
```

The halt dump prints `[word=<id> name=<spelling> ctx=<ctx>]`.