# GLON-WORLDS-W5.1 — Browser Acceptance and Deployment Readiness

Date: 2026-11-27
Branch: `integration/glon-worlds-w5.1`
Base: W5 `c8c7f12f12ade62e39f2dbc8de02ba1e5003f3bc`
Frozen S1: `f90496c26dc45c7a387d8fc8639cd2781507d3d2`
Status: implemented and tested locally; committed after the tests below. Not
pushed, merged, deployed or applied to the active Caddy configuration. The
public landing page remains unchanged.

## 1. Invitation transactions

Invitation registration now runs inside one SQLite transaction:

1. compute the invitation hash, salt and scrypt verifier before starting the
   write transaction;
2. `BEGIN IMMEDIATE`;
3. insert the user and password verifier;
4. consume the invitation with
   `UPDATE ... WHERE used_at IS NULL AND expires_at > strftime(...) RETURNING id`;
5. `COMMIT` on success, `ROLLBACK` on duplicate username, password failure,
   invite failure or any intermediate error.

A failed registration therefore does not consume the invitation. The test
registers a duplicate username with a fresh code (409), verifies `used_at` is
still NULL, then successfully registers a different username with the same code.
Local loopback bootstrap registration without an invitation remains a separate
local path.

## 2. Trusted proxy client identification

- The application no longer uses `X-Forwarded-For` for rate-limit identity.
- Rate limiting uses `X-Glon-Client-IP` only when the transport peer is loopback;
  otherwise it uses `http/peer`. A direct non-loopback client cannot inject a
  trusted identity.
- The staged Caddy config overwrites both headers with Caddy's observed peer:

  ```caddy
  header_up X-Glon-Client-IP {remote_host}
  header_up X-Forwarded-For {remote_host}
  ```

- Verified with a temporary Caddy instance on high ports and a local echo
  backend: client-supplied `X-Glon-Client-IP: 7.7.7.7` and
  `X-Forwarded-For: 6.6.6.6` arrived at the backend as `127.0.0.1` for both
  headers. The active Caddy service/config was not touched.
- The legacy `/api/users*` registry requires loopback and the absence of both
  proxy headers; the staged Caddy config also returns 404 for `/api/users*`.

## 3. Web workflow test status

Automated browser-equivalent HTTPS acceptance covers registration, invitation
rejection, login/logout, CSRF, two-user messaging, incremental polling,
session expiry, persistence after restart, unauthenticated rejection,
incorrect invitations and rate limiting.

**Real-browser verification was not performed.** No Chromium, Firefox, WebKit,
Playwright, Selenium or Node/browser automation runtime is installed on this
VPS. The automated HTTPS tests are not a substitute for real-browser DOM,
timer/visibility or cookie behaviour. A manual acceptance procedure is provided
at `tests/glon-worlds-w5.1/MANUAL-BROWSER-ACCEPTANCE.md`; it must be completed
before cutover.

## 4. Deployment readiness review

- `glon-worlds.service` uses the unprivileged `glon-worlds` user, loopback bind
  `127.0.0.1:8443`, `StateDirectory=glon-worlds`, `NoNewPrivileges`,
  `ProtectSystem=strict`, `ProtectHome`, `PrivateTmp`, address-family
  restriction and `UMask=0077`.
- The Caddy snippet keeps Caddy's existing automatic certificate, blocks
  `/api/users*`, and proxies to the loopback Glon service with
  `tls_insecure_skip_verify` for that loopback upstream only. Intended public
  routes are `/`, `/index.html`, `/api/groups`, `/api/session`, `/api/auth/*`
  and `/api/messages`.
- `caddy validate --adapter caddyfile --config deploy/glon-worlds/Caddyfile.worlds`
  → **Valid configuration**.
- `systemd-analyze verify` on a temp copy with `ExecStart=/bin/true` → exit 0
  (only the expected warning about the temporary test user).
- Restart persistence of users, invitations, sessions and messages is covered by
  the W5 and W5.1 tests.
- Deployment and rollback steps are in `deploy/glon-worlds/README.md`.
- Active `/etc/caddy/Caddyfile` was re-read unchanged;
  `https://worlds.tungjung.nz/` still serves the 190-byte landing page;
  `glon-worlds.service` is inactive/not installed.

## 5. Tests

| Test | Result |
|---|---|
| `make w5.1-test` | **W5.1_PILOT PASS** |
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

W5.1 test specifically verifies: failed duplicate registration rolls back and
leaves the invitation usable; successful retry consumes it; forged
`X-Forwarded-For` values do not evade rate limiting; a distinct trusted
`X-Glon-Client-IP` is a distinct rate-limit key; Caddy config overwrites both
forwarding headers; no machine halt.

## 6. Proposed deployment commands (NOT RUN)

These are for review only. Do not run without explicit approval.

```sh
# Build and install application (on the VPS, from the repository checkout)
make glon MODULES=file,http,sqlite,sec,task MEM_PROFILE=server-https
sudo install -d -o root -g root /opt/glon-worlds
sudo install -m 0755 glon /opt/glon-worlds/glon
sudo cp -a glon-lib modules examples /opt/glon-worlds/

# Unprivileged service user and persistent state
sudo useradd --system --home /var/lib/glon-worlds --shell /usr/sbin/nologin glon-worlds || true
sudo install -d -o glon-worlds -g glon-worlds -m 0700 /var/lib/glon-worlds/web
sudo install -m 0644 examples/glon-worlds-g3b/web/index.html /var/lib/glon-worlds/web/index.html

# Loopback-only Glon certificate (does not touch Caddy's certificate)
sudo install -d -o root -g glon-worlds -m 0750 /etc/glon-worlds
sudo ./deploy/glon-worlds/make-local-cert.sh /etc/glon-worlds

# Install and start the loopback service
sudo install -m 0644 deploy/glon-worlds/glon-worlds.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl start glon-worlds
sudo systemctl status glon-worlds --no-pager
curl -k --resolve worlds.tungjung.nz:8443:127.0.0.1 \
  https://worlds.tungjung.nz:8443/api/groups

# Create a pilot invitation
sudo -u glon-worlds python3 /opt/glon-worlds/deploy/glon-worlds/glon-worlds-invite.py \
  --db /var/lib/glon-worlds/users.db --label pilot-1 --days 7

# Caddy cutover (after approval only)
sudo cp -a /etc/caddy/Caddyfile \
  /etc/caddy/Caddyfile.pre-glon-worlds-$(date +%Y%m%d%H%M%S)
sudo install -m 0644 deploy/glon-worlds/Caddyfile.worlds /etc/caddy/Caddyfile
sudo caddy validate --config /etc/caddy/Caddyfile
sudo systemctl reload caddy
sudo systemctl enable glon-worlds
```

## 7. Proposed rollback commands (NOT RUN)

```sh
# Restore the previous landing-page Caddy config
sudo cp -a /etc/caddy/Caddyfile.pre-glon-worlds-<timestamp> /etc/caddy/Caddyfile
sudo caddy validate --config /etc/caddy/Caddyfile
sudo systemctl reload caddy

# Stop and disable the Glon service
sudo systemctl stop glon-worlds
sudo systemctl disable glon-worlds

# Preserve /var/lib/glon-worlds (users.db, messages.ndjson) unless a separate
# data-retention decision says otherwise.
```

## 8. Remaining limitations

- Real-browser acceptance remains outstanding; the manual procedure must be
  completed before cutover.
- No Caddy cutover, systemd installation/start, public deployment or live proxy
  endurance has been performed.
- Login/registration still rely on SameSite/invitation rather than a CSRF token.
- Local bootstrap registration without an invitation remains available on direct
  loopback; do not expose loopback to untrusted users.
- Invitation consumption is transactional, but a successful user creation whose
  response is lost still consumes the code.
- W4 sessions remain invalidated by the W5 token-hash transition.
- Rate limiting trusts Caddy's overwritten `X-Glon-Client-IP`; Caddy must remain
  the only proxy and must not pass client-supplied values through.
- No password recovery/change, Argon2id, role authorization, audit trail,
  monitoring or backup automation.
- The public landing page must not be replaced until explicitly approved.
