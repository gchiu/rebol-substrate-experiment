# GLON-WORLDS-W5 — Invited Pilot Hardening

Date: 2026-11-27
Branch: `integration/glon-worlds-w5`
Base: W4 `d5d93299b36185affa692e5b89b3b763096ddf8d`
Frozen S1: `f90496c26dc45c7a387d8fc8639cd2781507d3d2`
Status: implemented and tested locally; committed after the tests below. Not
pushed, merged or deployed. The public landing page and active Caddy config are
unchanged.

## 1. Invitation registration

- Added `auth_invites(id, code_hash UNIQUE, label, created_at, expires_at,
  used_at, used_by)`.
- Invitation codes are single-use and time-limited. Only the SHA-256 hex digest
  is stored; the raw code is printed once by
  `deploy/glon-worlds/glon-worlds-invite.py`.
- Registration through the proxy (`X-Forwarded-For` present) or from a
  non-loopback peer requires a valid, unexpired, unused invitation. Unknown,
  expired and reused codes return 403.
- Direct loopback registration without `X-Forwarded-For` remains a local
  bootstrap path for local tests/administration. It is not reachable through the
  staged Caddy proxy.
- The legacy `/api/users*` registry now requires both a loopback peer and no
  `X-Forwarded-For`; the staged Caddy config also returns 404 for
  `/api/users*`. This prevents the proxy from turning the old admin routes into
  public routes.

## 2. Session-token and CSRF hardening

- W4's `auth_sessions` table is left intact for record preservation but is no
  longer read. New sessions use `auth_sessions_v2` with `token_hash` and
  `csrf_token`.
- The cookie contains a 32-byte random hex bearer token. The database stores
  only `SHA-256(token)`. W4 sessions are therefore not resumed after this
  change; their rows are preserved.
- A bounded process-local token -> session-id cache reduces hashing on normal
  requests. After a restart, the first request per client hashes the token once.
- Login/session responses return a per-session CSRF token. Message POST and
  logout require `X-CSRF-Token`; missing/invalid tokens return 403. CSRF
  comparison uses the existing constant-time security primitive.
- The browser client stores the CSRF token in memory and sends it on message
  POST/logout. Messages still derive author/user id only from the session.

## 3. Rate limiting

- Added `auth_rate(key PRIMARY KEY, failures, expires_at)`.
- Login failures are counted against both the client key
  (`X-Forwarded-For` when present, otherwise peer) and the username key.
- Five failures in a 300-second window block further attempts with 429; the
  window refreshes on failure. Successful login clears both keys. State is
  stored in SQLite and survives restart.

## 4. Persistent unprivileged service and Caddy staging

Staged under `deploy/glon-worlds/`, none applied:

- `glon-worlds.service`: `User=glon-worlds`, loopback bind `127.0.0.1:8443`,
  `StateDirectory=glon-worlds`, `NoNewPrivileges`, `ProtectSystem=strict`,
  `ProtectHome`, `PrivateTmp`, address-family restriction and `UMask=0077`.
- `make-local-cert.sh`: generates a loopback-only self-signed cert+key for the
  Glon process under `/etc/glon-worlds`; it does not touch Caddy's certificate.
- `Caddyfile.worlds`: blocks `/api/users*`, then reverse-proxies to
  `https://127.0.0.1:8443` with `tls_insecure_skip_verify` for the loopback
  upstream. Caddy continues to own the public `worlds.tungjung.nz` certificate.
- `glon-worlds-invite.py`: creates invitation rows against the live temporary
  or pilot database.
- `README.md`: staged install, cutover, backup and rollback procedure.

Validation performed without changing the active service:

- `caddy validate --adapter caddyfile --config deploy/glon-worlds/Caddyfile.worlds`
  → **Valid configuration**.
- `systemd-analyze verify` on a temp copy with `ExecStart=/bin/true` →
  exit 0 (only the expected warning about using the temporary test user).
- Running `/etc/caddy/Caddyfile` was re-read and is unchanged:
  `worlds.tungjung.nz { root * /var/www/glon-worlds; file_server }`.
- `https://worlds.tungjung.nz/` still returns the 190-byte landing page.
- `glon-worlds.service` is not installed/active; no Glon process is public.

## 5. Tests

| Test | Result |
|---|---|
| `make w5-test` | **W5_PILOT PASS** |
| `make w4-test` | **W4_AUTH PASS** |
| `make w3-test` | **W3_BROWSER PASS** |
| `make w2-test` | **W2_HISTORY PASS** |
| `make w1-test` | **W1_MESSAGING PASS** |
| `make w0-routes-test` | **W0_ROUTES PASS** |
| `make g3g-test` | **G3G SMOKE PASS** |
| `make g3h-test` | **G3H SMOKE PASS** |
| `make json-j2-g3h-test` | **JSON_J2_G3H PASS** |
| `make test` | **exit 0** |
| `make profile-check` | **PASS (8 profiles)** |
| `./check-frozen-s1.sh` | **PASS** at `f90496c…` |

`tests/glon-worlds-w5/pilot.py` verifies: browser asset invitation/CSRF flow;
unauthenticated message rejection; proxied admin rejection; missing, expired and
reused invitations; duplicate usernames; login/session CSRF; SHA-256 token
storage and raw-token absence; legacy `auth_sessions` preservation; CSRF
required/incorrect/correct message writes; server-derived identity; two-client
messaging and incremental polling; logout/replay rejection; per-client/user rate
limiting; unrelated-client non-blocking; restart persistence of users,
invitations, sessions and messages; staged systemd/Caddy asset markers; and no
machine halt.

The older world/JSON tests were updated only to send the now-required CSRF token
after login. Their other assertions remain.

## 6. Verification boundary

- **Automated:** every test above, Caddy config validation, systemd unit
  validation and the runnable local certificate script.
- **Not performed:** real-browser two-window acceptance, actual Caddy cutover,
  systemd service installation/start, public pilot deployment, certificate
  interaction under live proxy traffic, and long-running endurance.

## 7. Remaining limitations / outstanding tasks

- Real-browser registration/login/CSRF/polling must be tested before the pilot.
- Login and registration do not carry a synchronizer CSRF token; they rely on
  SameSite and the invitation gate. Login CSRF/Origin enforcement remains future
  hardening.
- The process-local session cache is bounded but reset on restart; it is only an
  optimisation, never an authorization source.
- W4 sessions are invalidated by the token-hash schema transition; rows are
  preserved but users must log in again.
- An invitation is consumed before user/password creation, so a duplicate or
  failed registration consumes that code. Issue a fresh code for retries.
- Rate-limit keys use the raw username spelling and the full proxy-supplied
  `X-Forwarded-For` string. Caddy must remain the sole writer of that header.
- Local bootstrap registration without an invitation remains available on
  direct loopback; do not expose loopback to untrusted users.
- No password change/recovery, Argon2id, per-role authorization, account
  deletion, audit trail, monitoring or backup automation is added.
- The service/Caddy assets are staged only. Cutover requires explicit approval;
  rollback is documented in `deploy/glon-worlds/README.md`.
- The public landing page must not be replaced until explicitly approved.

## 8. Constraints preserved

Frozen S1 and its seven primitives unchanged; compile-time profiles unchanged;
existing SQLite users/passwords/messages preserved; old session rows preserved;
no WebSocket or new S1/host primitive; no push, merge, Caddy change or
deployment.
