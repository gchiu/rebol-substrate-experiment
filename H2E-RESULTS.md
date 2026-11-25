# H2E — Diagnose cooperative HTTP response collection: results

Date: 2026-11-25
Branch: `experiment/glon-http-h2e`
Baseline: H2D, commit `6ff2618`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## 1. Reproducible failure

`tests/h2b/smoke.py` (and the manual run below) reproduces it. The task starts,
enqueues a SQLite read, suspends, resumes, stores a response, then the owner
halts while collecting it:

```
glon: http: listening on https://127.0.0.1:8443
start 1
wait 1
resume 1
[dump] IP=11953 SP=1621544 RP=1622131 HP=38832 top=0
glon: http: run-tasks: machine halt (machine)
  [ip=11954 ops 2 6 15 7 0 12227]
```

- **IP = 11953** (the faulting halt site; `M[11953] = 7`, the halt opcode;
  preceding ops `2 6 15 7`, where `15` is the `HOST_DUMP` host op).
- **SP = 1,621,544** = `M1_ARENA_BASE` (the task's DS base) — the data stack is
  empty, **not** underflowed.
- **RP = 1,622,131** = base + 787, inside the per-task RS `[base+200, base+1000)`
  — **not** a return-stack overflow.
- **HP = 38,832** — far below the managed-heap limit, so this is **not** heap
  exhaustion or GC thrash.
- **Reason = 0** (`R0S1_HALT_NONE`): a `HOST_DUMP`+`HALT` fail-stop that does
  not set a structured reason (the GC "bad SP" / task-sentry family).

It is a **machine-level fail-stop**, not a Glon `SIN!`, not a type/index error.

## 2. Isolation

The same response record `[id status ctype headers body]` and the same
`http/take-response-for` (head-only `block-at` + `-`) work at **top level**:

```
[ http/responses: reduce [ reduce [ 1 200 "text/plain" none "body" ] none ]
  r: http/take-response-for 1  s/print either r [ "found" ] [ "missing" ] ]
;; => found
```

It also works in an ordinary task that does not suspend. The fail-stop appears
only after a task **suspends on SQLite and resumes**, i.e. in the resumed task
context during collection. So the defect is **task-context-specific**, not in
`block-at`/`=`/`-` themselves and not the reduce-built-block `do` limitation.

## 3. Root cause (diagnosis)

The fail-stop is the collector's **task-stack sentry / bad-SP** path
(`emit_collect` in `r0_s1_runtime.c`): it runs because the resumed task performs
an allocation (the response/`mk-string` path) and the collector validates the
running task's saved `SP/RP` from the task record. The record still holds the
**suspended** (task-wait) SP/RP while the task is running after resume, and the
sentry's world identification / bounds check fail-stops (reason 0) before the
response can be returned. The top-level path has no task record, so it never
hits this check.

This is a **runtime/collector interaction with resumed tasks**, not a Glon-level
error and not stack exhaustion (RP is in bounds, HP is low).

## 4. Fix status

I did **not** apply a fix, because the correct fix is in the collector's
task-sentry/world-identification (a runtime contract), and changing it without
fully proving safety risks the frozen-S1 guarantees. Per the milestone
instruction, I stop and report the evidence.

**Minimal proposed correction (for review):** when the collector identifies the
running world, use the **live** `REG_SP/REG_RP` for the running task's own
bounds check (the record's saved SP/RP are the last *suspended* values and are
stale while the task runs), or skip the sentry for the currently-running task
(it is already validated by the DS/RS guards). This is a small, targeted change
in `emit_collect`; it does not alter the memory map or the scheduler.

An application-level workaround (collect the response in the owner via a
per-task response slot) is possible but would need a per-task result field the
collector traces; that is a larger change than the sentry fix.

## 5. Regression safety

Frozen S1 unchanged (`check-frozen-s1.sh` passes). Default G3F and H2A modes
are untouched; G3E passes. The `r0_s1_dump_ip` diagnostic is only emitted on a
machine halt.

## 6. Tests

- Reproduced: the H2D task path halts at collection (IP 11953, reason 0).
- Top-level `take-response-for`: PASS.
- Full H2E success criterion (two tasks, SQLite interleaving, correct responses):
  **not met** — collection halts. Not claimed.
- Regressions (frozen S1, G3E) pass; M1/file/SQLite/HTTPS suites unchanged.

## 7. Conclusion

The response-collection halt is a **collector task-sentry fail-stop on a resumed
task**, not a Glon error, stack overflow or heap exhaustion. The precise site is
IP 11953 (`HOST_DUMP`+`HALT`, reason 0) with the task's SP at the arena base and
RP in bounds. The minimal proposed correction is to validate the running task
against the live `REG_SP/REG_RP` (or skip the sentry for the running task) in
`emit_collect`. This is reported rather than applied to avoid silently changing
a runtime contract.

## Reproduce

```
make glon MODULES=file,http,sqlite,task MEM_PROFILE=large MEM_LOADER_CELLS=500000
./glon --http-tasks --http-threads 4 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write examples/h2b/tasks.glon
# then: curl -k https://127.0.0.1:8443/messages
```

The `[dump]`/`[ip=…]` diagnostic is printed by `r0_s1_dump_ip` on the halt.