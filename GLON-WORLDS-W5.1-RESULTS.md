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

## 4a. Deployment-instruction audit corrections

The first W5.1 deployment draft had material path/permission errors. Corrected:

- The stage now includes `deploy/`, so the installed invitation tool exists at
  `/opt/glon-worlds/deploy/glon-worlds/glon-worlds-invite.py`.
- Developer and root commands are separated. `developer` has no sudo and only
  builds/stages; installation, systemd and Caddy commands run in a root shell.
- Staging copies only the required Worlds app source/assets and `glon-lib`,
  `modules`, `deploy`; local test `users.db`/`messages.ndjson` are excluded.
- The loopback certificate helper previously made `localhost.pem` mode 0600
  `root:glon-worlds`, which the service could not read. It is now 0640
  `root:glon-worlds`; separate key/cert files remain root-only 0600.
- Persistent state remains only under `/var/lib/glon-worlds`; `/opt` is
  root-owned application code and contains no runtime data.
- Invitation, Caddy and rollback commands use installed absolute paths.
- Private browser acceptance uses either a direct loopback tunnel (local
  bootstrap path) or a temporary loopback Caddy proxy on 8444 to exercise the
  invitation/combined proxy header path without touching active Caddy.

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

## 6. Corrected deployment commands (NOT RUN)

Run build/stage as `developer`; run installation and Caddy commands in a root
shell. Full procedure: `deploy/glon-worlds/README.md`.

Developer build/stage:

```sh
cd /home/developer/projects/glon-worlds-w3
git switch integration/glon-worlds-w5.1
make glon MODULES=file,http,sqlite,sec,task MEM_PROFILE=server-https
STAGE=/tmp/glon-worlds-stage
rm -rf "$STAGE"
install -d "$STAGE/glon-lib" "$STAGE/modules" "$STAGE/deploy" \
  "$STAGE/examples/glon-worlds-g3b/web"
install -m 0755 glon "$STAGE/glon"
cp -a glon-lib/. "$STAGE/glon-lib/"
cp -a modules/. "$STAGE/modules/"
cp -a deploy/. "$STAGE/deploy/"
install -m 0644 examples/glon-worlds-g3b/server.glon \
  "$STAGE/examples/glon-worlds-g3b/server.glon"
install -m 0644 examples/glon-worlds-g3b/web/index.html \
  "$STAGE/examples/glon-worlds-g3b/web/index.html"
find "$STAGE" -type f \( -name users.db -o -name messages.ndjson \) -print -quit
chmod -R a+rX "$STAGE"
```

Root install/start (root shell, no sudo prefix):

```sh
set -euo pipefail
STAGE=/tmp/glon-worlds-stage
test -x "$STAGE/glon"
getent group glon-worlds >/dev/null || groupadd --system glon-worlds
getent passwd glon-worlds >/dev/null || useradd --system \
  --gid glon-worlds --home-dir /var/lib/glon-worlds \
  --shell /usr/sbin/nologin glon-worlds

install -d -o root -g root -m 0755 /opt/glon-worlds
cp -a "$STAGE/." /opt/glon-worlds/
chown -R root:root /opt/glon-worlds
find /opt/glon-worlds -type d -exec chmod 0755 {} +
find /opt/glon-worlds -type f -exec chmod 0644 {} +
chmod 0755 /opt/glon-worlds/glon
chmod 0755 /opt/glon-worlds/deploy/glon-worlds/glon-worlds-invite.py
chmod 0755 /opt/glon-worlds/deploy/glon-worlds/make-local-cert.sh
if find /opt/glon-worlds -type f \( -name users.db -o -name messages.ndjson \) | grep -q .; then
  echo "ERROR: runtime data staged under /opt" >&2; exit 1
fi

install -d -o glon-worlds -g glon-worlds -m 0700 \
  /var/lib/glon-worlds /var/lib/glon-worlds/web
install -m 0644 -o glon-worlds -g glon-worlds \
  "$STAGE/examples/glon-worlds-g3b/web/index.html" \
  /var/lib/glon-worlds/web/index.html

install -d -o root -g glon-worlds -m 0750 /etc/glon-worlds
/opt/glon-worlds/deploy/glon-worlds/make-local-cert.sh /etc/glon-worlds

install -m 0644 "$STAGE/deploy/glon-worlds/glon-worlds.service" \
  /etc/systemd/system/glon-worlds.service
systemctl daemon-reload
systemd-analyze verify /etc/systemd/system/glon-worlds.service
systemctl start glon-worlds
systemctl status glon-worlds --no-pager
journalctl -u glon-worlds -n 50 --no-pager
runuser -u glon-worlds -- test -r /etc/glon-worlds/localhost.pem
ss -ltnp | grep ':8443'   # must be 127.0.0.1:8443 only
curl -k --resolve worlds.tungjung.nz:8443:127.0.0.1 \
  https://worlds.tungjung.nz:8443/api/groups
stat -c '%U:%G %a %n' /var/lib/glon-worlds \
  /var/lib/glon-worlds/users.db /var/lib/glon-worlds/messages.ndjson \
  /var/lib/glon-worlds/web/index.html /etc/glon-worlds/localhost.pem

runuser -u glon-worlds -- python3 \
  /opt/glon-worlds/deploy/glon-worlds/glon-worlds-invite.py \
  --db /var/lib/glon-worlds/users.db --label pilot-1 --days 7
```

Private browser acceptance: use
`tests/glon-worlds-w5.1/MANUAL-BROWSER-ACCEPTANCE.md`. Direct SSH tunnel
`-L 8443:127.0.0.1:8443` exercises the local bootstrap path; a temporary
loopback Caddy proxy on 8444 (same `header_up` rules, `tls internal`) exercises
the invitation/trusted-proxy path without touching active Caddy. Both are
reviewed, not executed.

Caddy cutover (root shell, only after approval):

```sh
set -euo pipefail
BACKUP=/etc/caddy/Caddyfile.pre-glon-worlds-$(date +%Y%m%d%H%M%S)
cp -a /etc/caddy/Caddyfile "$BACKUP"
echo "Caddy backup: $BACKUP"
install -m 0644 /opt/glon-worlds/deploy/glon-worlds/Caddyfile.worlds \
  /etc/caddy/Caddyfile
caddy validate --config /etc/caddy/Caddyfile
systemctl reload caddy
systemctl enable glon-worlds
curl -fsS https://worlds.tungjung.nz/ | head -20
curl -sS -o /dev/null -w '%{http_code}\n' https://worlds.tungjung.nz/api/groups
curl -sS -o /dev/null -w '%{http_code}\n' https://worlds.tungjung.nz/api/users
ss -ltnp | grep ':8443'
```

## 7. Corrected rollback commands (NOT RUN)

```sh
set -euo pipefail
BACKUP=$(ls -1t /etc/caddy/Caddyfile.pre-glon-worlds-* | head -n 1)
test -n "$BACKUP"
echo "Restoring $BACKUP"
cp -a "$BACKUP" /etc/caddy/Caddyfile
caddy validate --config /etc/caddy/Caddyfile
systemctl reload caddy
systemctl stop glon-worlds
systemctl disable glon-worlds
curl -fsS https://worlds.tungjung.nz/ | head -20
# Preserve /var/lib/glon-worlds unless a separate retention decision says otherwise.
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
