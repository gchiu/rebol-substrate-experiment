# GLON-WORLDS-W4 — Authentication

Date: 2026-11-27
Branch: `integration/glon-worlds-w4`
Base: `integration/glon-worlds-w3.1` at `486264a`
Frozen S1: `f90496c26dc45c7a387d8fc8639cd2781507d3d2`
Status: accepted locally; committed after the tests below. Not pushed, merged,
rebased or deployed.

## 1. G3H/G3J recovery audit

### G3H — SQLite user registry (implemented, passing)
`56b7cc2` added the loopback-only `users` table and `/api/users*` routes. It
deliberately stored **no passwords, tokens or sessions**. Its 8 smoke checks
passed. Its schema and routes are preserved in W4; W4 adds credential and
session tables rather than replacing existing user rows.

### G3I — authentication design (design only)
`06b0e03` selected scrypt (already exposed by `modules/sec.glon`), per-user salt,
generic login failure, session cookies with `HttpOnly/Secure/SameSite=Lax`,
server-side session rows, logout revocation and an additive schema. It added no
code. W4 follows that design.

### G3J — password authentication (source written, blocked, then advanced)
The G3J graph contains a complete auth implementation but no successful full
acceptance run. The blockers were distinct:

- `c50f85b` G3J: the fixed 512-symbol table was exhausted. This is a capacity
  limit, not an auth-design limit.
- `d2016b4` G3J.1: raised `SYM_CAP` to 1024. The current `server-https` profile
  already declares `SYM_CAP=1024`; C2 carries this capacity.
- `1fefe80` G3J.2: the installer tail call omitted `setup-d`; fixed.
- `f30d61d` G3J.3: a 3-statement `sqlite3_exec` corrupted reply delivery.
  W4 uses only single-statement `exec` calls, as G3H already did.
- `596d2d1`/`1fa7539` G3J.4/5: SQLite BLOB values are not `STRING!`, so
  `sec/scrypt` rejected them. W4 stores salt/hash as hexadecimal ASCII `TEXT`.
- `bf87d11`–`bb728bf` G3J.7–9: app-to-app word sharing and nested callback
  contexts failed in the legacy runtime. W4 keeps all auth in one app file and
  avoids passing continuation closures through async SQLite helpers.
- `94f9a01`–`b804352` G3J.10–12: task-mode loading hit the old 16,128-cell code
  limit. The current `server-https` profile has `CODE_CAP=20480`, so
  `sec + task + json + sqlite + web + the app` loads without a memory-map
  change.

### Reusable pieces and current C2 workarounds
Reused: G3H `users` schema/routes; G3I scrypt/session/cookie design; G3J
hex-encoded credential storage and session/revocation model.

Three application-level workarounds were required on the current C2 runtime;
none changes S1, HOST, the memory map or a profile:

1. Calling `sec/hex` with a continuation **inside another async callback**
   failed. W4 computes `sec/hex` synchronously (`salt: gw/auth-hex salt-raw`).
2. The recursive `wp/find-byte` cookie scan halted on the 64-character session
   token. W4 parses its own fixed-length `gw_session=` cookie directly.
3. SQLite row blocks were not carried across many later callbacks. W4 copies
   needed fields into managed blocks before finalizing a statement and avoids
   passing row blocks through async helper chains.

### Does W3.1 expose authentication?
No. The W3.1 client had no registration, login, logout or session UI and sent
messages without a cookie.

## 2. W4 implementation

- **Registration:** `POST /api/auth/register` (loopback-only), unique
  case-insensitive username, 3–64 byte username, 8–1,024 byte password,
  display name optional. Existing user rows are preserved.
- **Passwords:** 16-byte `sec/random` salt; `sec/scrypt` with `N=16384, r=8,
  p=1, dklen=32`; salt and derived key stored hex-encoded as `TEXT` in
  `user_passwords`. No plaintext password is stored.
- **Login:** `POST /api/auth/login`; lookup + scrypt + `sec/ct-eq`; generic 401
  for wrong password, unknown user, missing password verifier or disabled user;
  bounded in-memory failure counter (5) returns 429.
- **Sessions:** 32 random bytes, hex token; `auth_sessions` row with SQLite-time
  `created_at`/`expires_at`; 3,600-second TTL. Cookie:
  `gw_session=<token>; HttpOnly; Secure; SameSite=Lax; Path=/; Max-Age=3600`.
- **Session checks:** `GET /api/auth/session` returns the current user or
  `{user:null}`. Expiry and revocation are checked in SQL (`strftime`), not in
  Glon clock code.
- **Logout:** `POST /api/auth/logout` marks `revoked_at` and emits an expired
  cookie. Replaying the old cookie is rejected.
- **Protected messaging:** both `GET /api/messages` and `POST /api/messages`
  require a valid session. Message `author` and `user_id` come from the session
  user; client-supplied `author`/`user_id` are ignored.
- **Browser:** minimal Sign in / Register / Log out UI. The chat, history and
  W3.1 polling start only after session authentication; polling stops on
  logout/401 and resumes after login. The existing incremental polling, manual
  Refresh, textContent-only rendering and bounded backoff are preserved.
- **Persistence:** existing `users`, NDJSON message records and the G3H admin
  routes are retained. New tables are additive and idempotent. W4 does not
  modify an existing `users.db` or `messages.ndjson` in the source tree; tests
  always use a temporary I/O root.

## 3. Test results

All commands were run locally on the W4 branch.

| Test | Result |
|---|---|
| `make w4-test` | **W4_AUTH PASS** |
| `make w3-test` | **W3_BROWSER PASS** |
| `make w2-test` | **W2_HISTORY PASS** |
| `make w1-test` | **W1_MESSAGING PASS** |
| `make w0-routes-test` | **W0_ROUTES PASS** |
| `make g3g-test` | **G3G SMOKE PASS** |
| `make g3h-test` | **G3H SMOKE PASS** |
| `make json-j2-g3h-test` | **JSON_J2_G3H PASS** |
| `make test` | **exit 0** (S1/core suite, global-context hash, primer check) |
| `make profile-check` | **PASS (8 profiles)** |
| `./check-frozen-s1.sh` | **PASS** at `f90496c…` |

`tests/glon-worlds-w4/auth.py` covers: public metadata; unauthenticated message
401s; registration; duplicate usernames; wrong password/unknown user; database
inspection for salted hashes and absence of plaintext; hardened session cookies;
two authenticated clients with server-derived message identity; authenticated
incremental polling; logout/replay; server-side expiry; malformed inputs and
oversized bodies; restart persistence; no machine halt.

No W4 test failed. The older G3G/G3H, JSON-J2 G3H and W0–W3 tests were updated
only to authenticate before protected message operations; their other
assertions remain. Older message reads/writes now intentionally return 401 when
unauthenticated.

## 4. Remaining security limitations

- **Session tokens are stored as bearer tokens in SQLite** (`auth_sessions.token`).
  A database disclosure would expose live sessions. Hashing tokens at rest is
  the next hardening step; doing it per request safely needs a session cache or
  wider sec concurrency, so it was not added in this milestone.
- CSRF protection is only `SameSite=Lax`; there is no synchronizer CSRF token or
  Origin/Referer enforcement on message writes.
- Registration is loopback-only. There is no email verification, password
  recovery, password change or account deletion UI.
- The login failure counter is global and in memory, not per-IP/per-user, and
  resets on restart. There is no durable lockout.
- Passwords use scrypt, not Argon2id. Parameters are fixed at N=16384/r=8/p=1.
- The session cookie parser expects `gw_session=` at the start of the Cookie
  header and a fixed 64-character token; this is specific to this application.
- Session TTL is a fixed 3,600 seconds; there is no sliding renewal.
- Roles are stored and displayed, but W4 only uses authentication; there is no
  role-based authorization for messaging.
- The Activity log records method/path/status only; it does not record bodies or
  cookies, but it is not an audit trail for auth events.
- Real-browser timer/visibility/login UI behaviour was not executed in a real
  browser engine; the asset source and browser-equivalent HTTPS flows were
  tested.
- The service remains a loopback development server. It must not be exposed
  publicly through Caddy; TLS hardening, rate limiting at the edge, recovery and
  production session-token-at-rest protection remain future work.

## 5. Constraints preserved

Seven frozen S1 primitives unchanged; frozen files unchanged; compile-time
memory profiles unchanged; no WebSocket; no Caddy/public exposure; no push,
merge or deploy. Only Glon application code, browser asset, test code and the
W4 Makefile target changed.
