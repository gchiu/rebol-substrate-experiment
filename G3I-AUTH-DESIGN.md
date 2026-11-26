# G3I — Glon Worlds Authentication Architecture (design only)

Date: 2026-11-26
Branch: `experiment/glon-http-g3i`
Baseline: G3H, commit `56b7cc2`
Status: **design only — no authentication code, schema migration or dependency
added in this milestone.**

## 1. Existing capability audit (verified in source)

### HTTPS / HTTP
- CivetWeb (OpenSSL TLS) with `--http-cert`; loopback bind by default; bounded
  worker pool (`--http-threads`); keep-alive; `--http-max-body` (default 2048).
- Request headers are available to Glon: `http/header request name`
  (case-insensitive), so `Cookie`, `Origin`, `X-CSRF-Token` are readable.
- Response headers: `http/respond-with request status ctype headers body` sets an
  alternating `[name value ...]` block. The host owns only Content-Type,
  Content-Length and Connection, so **`Set-Cookie` and arbitrary headers are
  supported**. `http/peer request` gives the transport remote address.
- The G3G activity log records method/path/status only (no bodies, no cookies).

### SQLite
- Async vocabulary `modules/sqlite.glon`: `open/exec/prepare/bind-int/bind-text/
  bind-null/bind-blob/step/reset/clear/finalize/close`. Parameterised only.
- `users.db` (G3H): `users(id, username UNIQUE COLLATE NOCASE, display_name,
  role, status, created_at)`. NDJSON message store (`messages.ndjson`) is separate.

### Cryptography (the important find)
- `modules/sec.glon` (host `glon_sec.c`, OpenSSL) already exposes:
  - `sec/random n` — CSPRNG bytes (`RAND_bytes`);
  - `sec/sha256 bytes` — 32-byte digest;
  - `sec/scrypt password salt N r p dklen` — scrypt KDF (`EVP_PBE_scrypt`),
    bounds N∈{1024..65536, power of 2}, r≤16, p≤4, dklen 16..cap, 64 MiB host
    ceiling, salt 1..64 bytes;
  - `sec/ct-eq a b` — constant-time compare (`CRYPTO_memcmp`);
  - `sec/time` — Unix epoch seconds (wall clock).
- **Missing:** Argon2id (no libargon2), HMAC (only raw SHA-256), WebAuthn/CBOR/
  COSE (no library), email transport. Only one sec request is outstanding at a
  time (async, chained from callbacks).

### Host vs Glon
- Host provides narrow primitives: TLS, HTTP, SQLite, file, sec (random/hash/
  scrypt/ct-eq/time). **All authentication policy, account, session, CSRF and
  authorisation logic belongs in Glon** (the sec module is explicitly "not an
  authentication policy").
- Current administration is **loopback-only** (`gw/loopback?` in server.glon);
  `/api/users*` mutations require a loopback peer (403 otherwise).

### Global-context constraint (design-relevant)
- The Glon global context is capped at 256 bindings and shared with loaded
  libraries. With `http`+`json`+`sqlite` it is ~233; adding `sec` (+16) reaches
  ~249. New auth words must go into nested installer contexts (the server already
  uses ≤16-binding installers), not the global scope.

## 2. Authentication approach comparison

| Approach | Dependencies | Complexity | Recovery | Security | Verdict |
|---|---|---|---|---|---|
| **Passwords + scrypt** | none (sec/scrypt present) | low–med | admin reset / recovery token | memory-hard KDF, per-user salt, constant-time verify | **recommended** |
| Passwords + Argon2id | libargon2 (absent) | med | same | Argon2id is the current best practice | future (add libargon2) |
| Passwords + TOTP | HMAC-SHA1 (absent; only SHA-256) | med | recovery codes | adds second factor | future (add HMAC to sec) |
| Passkeys (WebAuthn) | WebAuthn lib, CBOR, COSE, ES256 | high | device-bound | phishing-resistant | future (large) |
| Passwordless email links | email transport (absent) | med | inbox | depends on email security | not practical now |

scrypt is the pragmatic choice: it is memory-hard and already available, needs
no new dependency, and Argon2id/TOTP/passkeys can be layered later.

## 3. Recommended architecture

**Trust boundary:** the host stays narrow (TLS, SQLite, sec primitives). Glon
owns registration, login, session lifecycle, CSRF and authorisation policy.

### Registration
1. `POST /api/auth/register {username, display_name, password}` (loopback or an
   explicitly enabled admin mode until public signup is approved).
2. Generate a 16–32 byte salt (`sec/random`), derive `scrypt(password, salt,
   N=16384, r=8, p=1, dklen=32)` (`sec/scrypt`), store `algo, N, r, p, salt,
   hash` in `users`. Never store the password.

### Login
1. `POST /api/auth/login {username, password}`.
2. Look up the user; if absent, still run a **dummy scrypt** (constant-ish time)
   to avoid user enumeration.
3. Derive scrypt with the stored salt/params; compare with `sec/ct-eq`.
4. On success create a session (below); on failure return a generic 401.

### Sessions
- `sessions(id, user_id, token_hash, created_at, expires_at, revoked_at)`.
- Token = 256-bit `sec/random`, base64/hex; **store only `SHA-256(token)`**
  (`sec/sha256`) so a DB leak does not reveal live tokens.
- Cookie `gw_session=<token>; HttpOnly; Secure; SameSite=Lax; Path=/` via
  `http/respond-with` (`Set-Cookie`).
- Expiry checked with `sec/time`; revocation = set `revoked_at` (or delete).
- Logout clears `revoked_at` and emits an expired cookie.

### CSRF
- A per-session random CSRF token (stored in the session row, also returned to
  the browser). Browser mutations must send it in `X-CSRF-Token`; Glon compares
  with `sec/ct-eq`. (Double-submit is possible; a synchronizer token in the
  session is simpler and does not need HMAC.)

### Cookies
- `HttpOnly` (no JS access), `Secure` (TLS only), `SameSite=Lax` (blocks most
  cross-site POSTs). No `Domain`; host-only.

### Rate limiting
- Glon-side bounded counters keyed by peer/username with a window from
  `sec/time`; lock an account after N failures; all in memory (bounded), reset on
  restart. No new host code.

### Recovery
- No email transport, so self-service email reset is out of scope. Provide an
  **admin/loopback recovery token** (random, single-use, hashed at rest) and/or
  an admin password reset. Document that device loss requires an operator.

### Authorisation / public vs private
- Public reads (`GET /api/groups`, public `/api/messages`) need no session.
- Private groups: the handler checks a valid session and a `group_members`
  row before returning messages; writes require a session + membership + CSRF.
- Admin vs user: `users.role`; admin-only endpoints (user disable, etc.) require
  `role='admin'`; until auth lands these stay loopback-only (G3H).

## 4. SQLite schema proposal (for G3J+, not applied here)

```sql
ALTER TABLE users ADD COLUMN password_algo TEXT;      -- 'scrypt'
ALTER TABLE users ADD COLUMN password_salt BLOB;
ALTER TABLE users ADD COLUMN password_hash BLOB;
ALTER TABLE users ADD COLUMN password_n INTEGER;      -- scrypt N
ALTER TABLE users ADD COLUMN password_r INTEGER;
ALTER TABLE users ADD COLUMN password_p INTEGER;

CREATE TABLE IF NOT EXISTS sessions (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL,
  token_hash BLOB NOT NULL UNIQUE,     -- SHA-256(token)
  csrf_token_hash BLOB NOT NULL,
  created_at INTEGER NOT NULL,         -- epoch seconds
  expires_at INTEGER NOT NULL,
  revoked_at INTEGER
);
CREATE TABLE IF NOT EXISTS group_members (
  group_id INTEGER NOT NULL,
  user_id INTEGER NOT NULL,
  role TEXT NOT NULL DEFAULT 'member',
  PRIMARY KEY (group_id, user_id)
);
```
Migration is additive and idempotent; existing rows get NULL password columns
(accounts must register/set a password). No destructive change.

## 5. Security assumptions and limitations

- scrypt params are bounded by the host (64 MiB, N≤65536); pick N=16384, r=8,
  p=1 as a balance for the 1 GB VPS.
- The sec queue is single-outstanding; login/registration must chain scrypt →
  DB writes from callbacks (already the async pattern).
- No email = no self-service recovery; no WebAuthn = no phishing-resistant
  second factor yet.
- Rate limiting is in-memory and resets on restart; it is best-effort.
- The global context is near its 256 cap; auth words must live in installers.
- TLS is host-provided; cookie `Secure` assumes HTTPS (the demo is HTTPS-only).

## 6. Implementation sequence

- **G3J (smallest):** password register + login + session cookie + logout +
  rate-limit, loopback-only, with deterministic tests. Adds `sec` to the build
  and the `sessions`/password columns.
- G3K: CSRF enforcement + `SameSite`/`Secure` hardening + login throttling tests.
- G3L: private groups + `group_members` enforcement.
- G3M (optional): TOTP (add HMAC to sec) or Argon2id (add libargon2).
- G3N (optional): passkeys/WebAuthn (new dependency, large).

## 7. G3J acceptance criteria (proposed)

1. Register a user (loopback) → 201; password is not stored in plaintext
   (only salt+hash columns set).
2. Login with the correct password → 200 + `Set-Cookie: gw_session=...;
   HttpOnly; Secure; SameSite=Lax`; a protected endpoint then succeeds.
3. Login with a wrong password → 401; no session cookie.
4. Duplicate/case-variant username → 409; unknown user → same 401 as wrong
   password (no enumeration).
5. Logout → session revoked; the cookie no longer authorises (401 afterwards).
6. Expired session (short TTL in the test) → 401.
7. Mutations without a valid CSRF token → 403.
8. Existing message endpoints and G3H loopback admin still work; frozen S1
   unchanged; `make g3j-test` passes.

## 8. Non-goals for G3I

No auth code, no schema migration, no new dependency, no runtime change, no
stress test. This document is the deliverable.