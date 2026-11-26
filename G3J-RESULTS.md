# G3J — Password Authentication and Sessions (BLOCKED)

Date: 2026-11-26
Branch: `experiment/glon-http-g3j`
Baseline: G3I, commit `06b0e03` (design `G3I-AUTH-DESIGN.md`)
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.

## Status: BLOCKED — implementation written, cannot load

The password-auth implementation is complete in source
(`tests/g3j/server-auth-attempt.glon`) but **the server does not load**: the
Glon interpreter's fixed symbol table is exhausted before the auth vocabulary
can be interned. This is a platform capacity limit, not a security-requirement
problem. Per the task, I stop and document the blocker rather than weaken a
requirement or make an unapproved capacity change. The working G3H server is
unchanged (`examples/glon-worlds-g3b/server.glon` is the G3H version).

## 1. What was implemented (source-complete, load-blocked)

Additive, using only existing primitives (`sec`, `sqlite`, `http`, `json`):

- **Migration** (idempotent, one `sqlite3_exec` with three
  `CREATE TABLE IF NOT EXISTS`; the long SQL is split into concatenated
  literals to respect the 512-byte string-literal cap):
  - `user_passwords(user_id PRIMARY KEY, algo, salt BLOB, hash BLOB, n, r, p)` —
    a separate table (instead of `ALTER TABLE`, which is not idempotent) so
    existing users and their stable IDs are preserved; users without a row are
    not authenticated.
  - `sessions(id, user_id, token_hash BLOB UNIQUE, csrf_hash BLOB, created_at,
    expires_at, revoked_at)`.
- **Password setup** (`POST /api/auth/setup`, loopback-only): validate
  username/password (length 8..1024), `sec/random` 16-byte salt,
  `sec/scrypt N=16384 r=8 p=1 dklen=32`, `INSERT OR REPLACE user_passwords`.
- **Login** (`POST /api/auth/login`): lookup user; **dummy scrypt** on unknown
  user / no password (no user enumeration); `sec/scrypt` + `sec/ct-eq`; disabled
  users rejected; on success `sec/random` 32-byte token + 16-byte CSRF,
  `sec/sha256` both, `sec/time` for created/expiry, insert session, set
  `Set-Cookie: gw_session=...; HttpOnly; Secure; SameSite=Lax; Path=/;
  Max-Age=`, return `{user, csrf}`.
- **Session** (`GET /api/auth/session`): parse `Cookie`, `sec/sha256` the token,
  look up session, reject revoked/expired (`sec/time`), reject disabled user,
  return `{user}` or `{user:null}`.
- **Logout** (`POST /api/auth/logout`): `sec/sha256` token, set `revoked_at`,
  emit an expired cookie.
- **Rate limiting**: bounded in-memory failure counter (`auth/fails`,
  `auth/max-fails=5`) → 429 after repeated failures; reset on success.
- **CSRF**: synchronizer token (sha256 of the per-session CSRF secret) available
  to the browser; the session record stores `csrf_hash`.
- Generic 401 for all login failures; no secrets logged.

## 2. The blocker (evidence)

- `make glon MODULES=file,http,sqlite,sec` builds.
- Loading `examples/glon-worlds-g3b/server.glon` (G3H) + `sec` succeeds
  (`glon: http: listening ...`).
- Loading the auth-attempt server fails:
  `glon: ... ** resource error: symbol_table_full`.
- Instrumented `intern()` (temporary, reverted) reported
  `nsyms=512 cap=512 name=b5` — the table (`syms[512]`, `SYM_CAP=512`,
  `r0_s1_runtime.c:22`) is full; the auth vocabulary needs ~30 more symbols than
  remain after the libraries and the G3H server are interned.
- `tests/g3j/blocker_check.py` reproduces this deterministically (baseline
  loads; auth-attempt → `symbol_table_full`).

## 3. Why symbol reduction alone is insufficient

- The libraries dominate the table: `json` (~74 globals + many locals),
  `http` (32), `file` (23), `sqlite` (25), `sec` (16), `web` (15), plus
  prelude/strings/strings-ext/file-stream.
- Dropping `web.glon` and inlining the four used helpers nets only ~6 symbols;
  reusing local names nets ~10. The auth vocabulary needs ~30. So no combination
  of symbol reuse fits within 512.
- `SYM_CAP` is a fixed array (`static const char *syms[512]`), not a build
  option. Raising it is a runtime capacity change, which AGENTS.md says requires
  explicit approval (a separate scaling experiment), not an incidental fix.

## 4. Options to unblock (need a decision)

- **A. Approve raising `SYM_CAP`** (e.g., 512 → 1024) as a scaling experiment.
  Smallest code change; preserves the design and all requirements. Needs sign-off.
- **B. Reduce the loaded vocabulary** (e.g., a smaller JSON, or move message
  rendering out of Glon) to free ~30 symbols. Large refactor, may not suffice.
- **C. Split the application** so auth lives in a second process/library with its
  own symbol table. Architectural change, out of scope.

Recommendation: **A** (approve a bounded `SYM_CAP` increase), then re-run the
auth attempt; the implementation is otherwise ready.

## 5. Verified vs untested

Verified (deterministic):
- `sec` primitives work: `sec/time`, `sec/random`, `sec/scrypt`, `sec/sha256`,
  `sec/ct-eq` (async chaining) — PASS.
- Migration SQL parses (after splitting the >512-byte literal) — PASS.
- Baseline G3H server + `sec` loads and serves — PASS (regression intact).
- Frozen S1 unchanged; G3H smoke still passes — PASS.
- Blocker reproduced (`symbol_table_full`) — PASS (documents the limit).

Untested (blocked by the symbol table; the auth server does not load):
1. Create user with password (loopback) — UNTESTED.
2. Correct password → session — UNTESTED.
3. Wrong password rejected — UNTESTED.
4. Unknown user → same 401 — UNTESTED.
5. Session cookie → correct user — UNTESTED.
6. Token stored only as hash — UNTESTED (code does sha256 before store).
7. Logout invalidates — UNTESTED.
8. Expired session rejected — UNTESTED.
9. Disabled user cannot log in / session invalid — UNTESTED.
10. CSRF/Origin reject — UNTESTED (CSRF token present; Origin not added).
11. Rate limiting after failures — UNTESTED.
12. Restart preserves unexpired / rejects revoked — UNTESTED.
13. Messaging/logging regressions — PASS (G3H smoke).

## 6. Build requirements

`make glon MODULES=file,http,sqlite,sec MEM_PROFILE=large MEM_LOADER_CELLS=500000`
(plus `modules/web.glon modules/json.glon` at load). The blocker is the symbol
table, not memory/loader.

## 7. Deliverables

- `tests/g3j/server-auth-attempt.glon` — the full auth implementation (blocked).
- `tests/g3j/blocker_check.py` + `make g3j-test` — deterministic blocker repro.
- This file.

## 8. Commit

Branch `experiment/glon-http-g3j`, one local commit. Not pushed/merged/deployed.