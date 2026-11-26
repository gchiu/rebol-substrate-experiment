# G3J.3 — Diagnose SQLite Callback Startup Failure

Date: 2026-11-26
Branch: `experiment/glon-http-g3j3`
Baseline: G3J.2, commit `1fefe80`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.

## 1. Reproduction

`tests/g3j/server-auth-attempt.glon` (and, minimally, the G3H server with a
3-statement migration) halts during the startup drain:

```
glon: sqlite/reply: ** halted (reason 0: no SIN!: a machine-level fail-stop)
  [word=175 name=http/handler]  [blk=0 count=11972 idx=254]  [top word=2687 name=?]
glon: service pump: sqlite/reply callback failed (status 4)
```

`status 4` = `R0S1_OUT_HALT` (machine fail-stop, not a Glon SIN!). The dump's
`top word` is out of range (> SYM_CAP 1024), i.e. evaluator state corruption.

## 2. Isolation (the decisive experiment)

Using the **G3H server + sec** (known-good) and varying only the migration
`sqlite/exec` statement count:

| Migration `exec` | Result |
|---|---|
| 1 statement (single literal) | ALIVE (listens) |
| 1 statement via 2-part `s/+` | ALIVE |
| 1 statement via nested 3-part `s/+` | ALIVE |
| 2 statements (`users; user_passwords`) | ALIVE |
| **3 statements** (`users; user_passwords; sessions`) | **HALT** |
| **3 statements** (`2 CREATE + SELECT 1`) | **HALT** |

So the trigger is **`sqlite3_exec` with three or more statements**, not the
statement content, not `s/+` concatenation, and not the auth program size
(the G3H server with a 3-statement migration halts identically). `do_exec`
(glib_sqlite.c:493) calls `sqlite3_exec` and returns `OK` + 8-byte changes; the
fail-stop happens later, in the `sqlite/reply` delivery.

This is a **runtime/SQLite defect** (multi-statement `exec` ≥3 corrupts the
evaluator state during reply delivery). It is not a bracket, symbol-capacity,
loader-frontier or authentication-design problem.

## 3. Minimal application-level fix (applied)

Run each migration statement as a **separate single-statement `exec`**, chained
from callbacks (each literal < 512 bytes):

```
exec "CREATE TABLE users(...)"        -> exec "CREATE TABLE user_passwords(...)"
                                       -> exec "CREATE TABLE sessions(...)"
                                       -> http/listen
```

This preserves the migration requirement (all three tables created) and avoids
the ≥3-statement `exec`. No runtime, S1, capacity or auth-design change.

**Result:** the server now starts, listens, and serves:
- `POST /api/users` → 201 (user created).
- `POST /api/auth/setup` → `{"user_id":1}` (scrypt password stored).
- The process stays ALIVE (no fail-stop) across requests.

## 4. Remaining blocker (separate, smaller issue)

`POST /api/auth/login` (correct password) returns **500 "Glon security
operation failed"** — a `sec`-module error, not a halt (server stays alive). The
login chains several `sec` operations (scrypt → ct-eq → random → sha256 → random
→ sha256 → time) from callbacks; one returns an error. This is a distinct
`sec`-chaining problem, unrelated to the migration fix, and it still blocks the
full auth acceptance (session/logout/expiry/disabled/CSRF/restart).

## 5. Status vs criteria

- Startup halt diagnosed (multi-statement `exec` ≥3) — DONE.
- Minimal app-level fix applied (separate execs); migration + listen work — DONE.
- Server stays up; create + setup password work — DONE.
- Login (and therefore session/logout/expiry/disabled/CSRF/restart) — **BLOCKED**
  by the `sec` login error (separate issue).
- Frozen S1 / G3H regression — unaffected (no runtime change).

## 6. Reproduce

```
make glon MODULES=file,http,sqlite,sec MEM_PROFILE=large MEM_LOADER_CELLS=500000
./glon --http-bind 127.0.0.1 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/x --allow-file-read --allow-file-write \
  modules/web.glon modules/json.glon tests/g3j/server-auth-attempt.glon
# server listens; POST /api/users and /api/auth/setup succeed; /api/auth/login -> 500 sec error
```

## 7. Recommendation

Report the `sqlite3_exec` ≥3-statement fail-stop to the runtime owners (it is a
general defect, reproducible with the G3H server). The auth migration is now
safe via separate execs. The remaining `sec` login error needs a focused
investigation of the login callback chain (which `sec` op errors and why).

## 8. Commit

Branch `experiment/glon-http-g3j3`, one local commit. Not pushed/merged/deployed.