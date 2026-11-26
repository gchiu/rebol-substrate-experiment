# G3J.2 — Repair Authentication Application Structure

Date: 2026-11-26
Branch: `experiment/glon-http-g3j2`
Baseline: G3J.1, commit `d2016b4`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.

## 1. The structural defect (found and fixed)

The authentication source built the installer chain
`app/install → setup-a → setup-b → setup-c → setup-d → setup-e → setup-f → gw/start`,
but the **tail call chain omitted the `setup-d` call**. It read:

```
... setup-e   ] setup-c   ] setup-b   ] setup-a ] app/install ]
```

i.e. after `setup-e` it called `setup-c`, `setup-b`, `setup-a` — skipping
`setup-d` (which contains `setup-e`/`setup-f` and `gw/start`). The top-level
therefore returned closures (`#[func]`) and **`gw/start` was never called**, so
the server never listened.

**Fix:** the tail now calls each level in order
`setup-f, setup-e, setup-d, setup-c, setup-b, setup-a, app/install` (added the
missing `setup-d`), and the temporary trailing `]` (from G3J.1) is removed. The
file is bracket-balanced (depth 0) with the correct nesting.

Verified with temporary markers (since removed): `gw/start` now runs →
`file/read` (not-found on a fresh store) → `gw/open-users` → `sqlite/open ok` →
migration `exec ok` → **`http/listen` is reached**. The primary G3J.2 objective
is met.

## 2. New failure revealed (reproducible, blocks the auth tests)

With the structure fixed, the server now reaches `http/listen`, but the
interpreter-owner **service pump halts** immediately afterward:

```
glon: sqlite/reply: ** halted (reason 0: no SIN!: a machine-level fail-stop)
  [word=175 name=http/handler ctx=24199]
  [blk=0 count=11972 idx=254]
  ...
glon: service pump: sqlite/reply callback failed (status 4)
```

- It happens during the startup drain (no client request is sent), right after
  the migration `exec` success callback runs `http/listen`.
- `status 4` = `R0S1_OUT_HALT` (machine fail-stop), not a Glon SIN!.
- The dump shows the current block as `blk=0` (the large program) with an
  out-of-range `top word=2661` (> SYM_CAP 1024), which indicates the fail-stop
  is a low-level state corruption, not an application-level error.
- It is **not** the bracket defect (that is fixed) and **not** the symbol table
  (capacity 1024 is fine). It is a separate runtime interaction between the
  large auth program, the async SQLite service pump and `http/listen` being
  invoked from inside a `sqlite/reply` callback.

This is a reproducible failure. Per the task, I stop here rather than attempt a
runtime redesign. The auth acceptance criteria (login, session, logout, expiry,
disabled users, CSRF, restart) are **NOT yet runnable** because the server halts
during startup.

## 3. Status vs criteria

- Bracket nesting corrected; `gw/start`/migration/`http/listen` reached — DONE.
- Temporary trailing `]` removed — DONE.
- Server stays up and serves — **BLOCKED** by the `sqlite/reply` halt.
- G3J auth acceptance (login/session/logout/expiry/disabled/CSRF/restart) —
  **NOT RUN** (server halts at startup).
- Frozen S1 / G3H regression — unaffected (no runtime change in G3J.2).

## 4. Evidence / reproduce

```
make glon MODULES=file,http,sqlite,sec MEM_PROFILE=large MEM_LOADER_CELLS=500000
./glon --http-bind 127.0.0.1 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write \
  modules/web.glon modules/json.glon tests/g3j/server-auth-attempt.glon
# -> sqlite/reply machine halt during startup drain; process exits non-zero
```

## 5. Recommendation

The structural repair is correct and complete. The remaining `sqlite/reply`
fail-stop needs a focused runtime investigation (why invoking `http/listen`
inside the SQLite `exec` success callback corrupts the evaluator state with the
large program loaded). That is a separate milestone; it is not a bracket,
symbol-capacity or authentication-design problem.

## 6. Commit

Branch `experiment/glon-http-g3j2`, one local commit. Not pushed/merged/deployed.