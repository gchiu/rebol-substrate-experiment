# Glon Worlds pilot deployment (staged, not applied)

Status: W5.1 deployment procedure only. Nothing here has been installed,
enabled or applied on the VPS. The public site still serves the landing page. Do not replace
it until explicitly approved.

Role split: the `developer` account has no sudo. Run the build/stage commands as
`developer`; run installation, service and Caddy commands from a root shell.
Do not prefix root commands with `sudo` from the developer account.

## Tested vs reviewed

Tested non-destructively during the W5.1 audit:

- building the `server-https` binary and staging the file tree under `/tmp`;
- starting the staged binary against a temporary I/O root and certificate;
- generating the loopback certificate and checking its modes/PEM contents;
- `caddy validate` on the staged Caddy file;
- `systemd-analyze verify` on a temp copy (with a temporary executable/user);
- a temporary Caddy instance proving `header_up` overwrites forged forwarding
  headers;
- W5.1, W5, W4–W0, G3G/G3H, JSON-J2-G3H, `make test`, profile checks and frozen
  S1 regression checks.

Reviewed but not executed: actual root installation to `/opt`, creation of the
real `glon-worlds` user, systemd start/enable, real-browser acceptance, Caddy
cutover, live proxy traffic and rollback on the public site.

## Target layout

| Path | Purpose |
|---|---|
| `/opt/glon-worlds` | root-owned read-only application tree: binary, `glon-lib/`, `modules/`, deploy assets, only the Worlds app source/assets |
| `/var/lib/glon-worlds` | service-owned persistent state: `users.db`, `messages.ndjson`, `web/index.html` |
| `/etc/glon-worlds/localhost.pem` | combined self-signed cert+key readable by the service group; used by Glon only |
| `/etc/systemd/system/glon-worlds.service` | installed unit |
| `/etc/caddy/Caddyfile` | Caddy's existing config; backed up before cutover |

`/opt` contains no runtime data. Upgrades replace application files only and
must not touch `/var/lib/glon-worlds`.

## 1. Developer: build and stage the application

Run as `developer` from the repository checkout. This does not install anything.

```sh
cd /home/developer/projects/glon-worlds-w3
git switch integration/glon-worlds-w5.1
git status --short

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

# Stage only files, never local runtime test data.
find "$STAGE" -type f \( -name users.db -o -name messages.ndjson \) -print -quit
chmod -R a+rX "$STAGE"

# Inventory checks
test -x "$STAGE/glon"
test -f "$STAGE/glon-lib/prelude.glon"
test -f "$STAGE/modules/web.glon"
test -f "$STAGE/modules/json.glon"
test -f "$STAGE/examples/glon-worlds-g3b/server.glon"
test -f "$STAGE/examples/glon-worlds-g3b/web/index.html"
test -x "$STAGE/deploy/glon-worlds/glon-worlds-invite.py"
find "$STAGE" -maxdepth 3 -type f | sort
```

Optional staged-binary smoke (temporary I/O root; not the systemd service):

```sh
TD=$(mktemp -d)
install -d "$TD/io/web"
install -m 0644 "$STAGE/examples/glon-worlds-g3b/web/index.html" \
  "$TD/io/web/index.html"
tests/https-g1/make-cert.sh
"$STAGE/glon" --http-tasks --http-bind 127.0.0.1 \
  --http-cert tests/https-g1/.cert/server.pem --io-root "$TD/io" \
  --allow-file-read --allow-file-write --http-timeout-ms 30000 \
  "$STAGE/modules/web.glon" "$STAGE/modules/json.glon" \
  "$STAGE/examples/glon-worlds-g3b/server.glon" &
GLON_PID=$!
sleep 1
curl -k --resolve worlds.tungjung.nz:8443:127.0.0.1 \
  https://worlds.tungjung.nz:8443/api/groups
kill "$GLON_PID"
rm -rf "$TD"
```

## 2. Root: install and start the private loopback service

Run in a root shell. `/tmp/glon-worlds-stage` must exist from step 1.

```sh
set -euo pipefail
STAGE=/tmp/glon-worlds-stage
test -x "$STAGE/glon"

getent group glon-worlds >/dev/null || groupadd --system glon-worlds
getent passwd glon-worlds >/dev/null || useradd --system \
  --gid glon-worlds --home-dir /var/lib/glon-worlds \
  --shell /usr/sbin/nologin glon-worlds

# Application tree: root-owned, no runtime data.
install -d -o root -g root -m 0755 /opt/glon-worlds
cp -a "$STAGE/." /opt/glon-worlds/
chown -R root:root /opt/glon-worlds
find /opt/glon-worlds -type d -exec chmod 0755 {} +
find /opt/glon-worlds -type f -exec chmod 0644 {} +
chmod 0755 /opt/glon-worlds/glon
chmod 0755 /opt/glon-worlds/deploy/glon-worlds/glon-worlds-invite.py
chmod 0755 /opt/glon-worlds/deploy/glon-worlds/make-local-cert.sh
if find /opt/glon-worlds -type f \( -name users.db -o -name messages.ndjson \) | grep -q .; then
  echo "ERROR: runtime data staged under /opt" >&2
  exit 1
fi

# Persistent state: service-owned. Do not copy over existing users.db/messages.
install -d -o glon-worlds -g glon-worlds -m 0700 \
  /var/lib/glon-worlds /var/lib/glon-worlds/web
install -m 0644 -o glon-worlds -g glon-worlds \
  "$STAGE/examples/glon-worlds-g3b/web/index.html" \
  /var/lib/glon-worlds/web/index.html

# Loopback-only certificate; does not touch Caddy's certificate.
install -d -o root -g glon-worlds -m 0750 /etc/glon-worlds
/opt/glon-worlds/deploy/glon-worlds/make-local-cert.sh /etc/glon-worlds

# Unit installation and loopback service start.
install -m 0644 "$STAGE/deploy/glon-worlds/glon-worlds.service" \
  /etc/systemd/system/glon-worlds.service
systemctl daemon-reload
systemd-analyze verify /etc/systemd/system/glon-worlds.service
systemctl start glon-worlds
systemctl status glon-worlds --no-pager
journalctl -u glon-worlds -n 50 --no-pager

# Verify permissions, loopback binding and endpoints.
runuser -u glon-worlds -- test -r /etc/glon-worlds/localhost.pem
ss -ltnp | grep ':8443'   # MUST show 127.0.0.1:8443, not 0.0.0.0 or [::]
curl -k --resolve worlds.tungjung.nz:8443:127.0.0.1 \
  https://worlds.tungjung.nz:8443/api/groups
stat -c '%U:%G %a %n' /var/lib/glon-worlds \
  /var/lib/glon-worlds/users.db /var/lib/glon-worlds/messages.ndjson \
  /var/lib/glon-worlds/web/index.html /etc/glon-worlds/localhost.pem
```

Create a pilot invitation only after the service has created its schema:

```sh
runuser -u glon-worlds -- python3 \
  /opt/glon-worlds/deploy/glon-worlds/glon-worlds-invite.py \
  --db /var/lib/glon-worlds/users.db --label pilot-1 --days 7
```

The raw code is printed once; only its SHA-256 digest is stored. Registration
through a proxy requires a valid, unexpired, single-use invitation. Direct
loopback with no proxy headers is a local bootstrap path; it is not the public
workflow.

## 3. Private browser acceptance (required before cutover)

Automated browser automation is not installed on this VPS. Real-browser testing
is not done by the W5.1 automated tests. Use the full manual checklist in
`tests/glon-worlds-w5.1/MANUAL-BROWSER-ACCEPTANCE.md`.

Direct loopback tunnel for UI/messaging checks:

```sh
# On the developer workstation:
ssh -N -L 8443:127.0.0.1:8443 developer@160.30.240.188
# Browse https://127.0.0.1:8443/ and accept the loopback self-signed warning.
```

This direct path is the local bootstrap path. To test that the public invitation
gate actually applies, run a temporary loopback-only Caddy acceptance proxy (as
root, in a separate shell) and tunnel to port 8444 instead:

```sh
install -d -m 0700 /tmp/glon-worlds-browser
cat > /tmp/glon-worlds-browser/Caddyfile <<'CADDY'
{
	admin off
	auto_https off
	storage file_system /tmp/glon-worlds-browser/caddy-data
}
https://localhost:8444 {
	bind 127.0.0.1
	tls internal
	reverse_proxy https://127.0.0.1:8443 {
		header_up X-Glon-Client-IP {remote_host}
		header_up X-Forwarded-For {remote_host}
		transport http {
			tls_insecure_skip_verify
		}
	}
}
CADDY
caddy validate --adapter caddyfile --config /tmp/glon-worlds-browser/Caddyfile
caddy run --config /tmp/glon-worlds-browser/Caddyfile --adapter caddyfile
```

From the developer workstation:

```sh
ssh -N -L 8444:127.0.0.1:8444 developer@160.30.240.188
# Browse https://localhost:8444/ and accept Caddy's internal-CA warning.
```

Use two browser profiles/windows and the checklist. Stop the temporary Caddy
after acceptance; it is not the active Caddy service.

## 4. Caddy cutover (only after explicit approval)

Run in a root shell. This keeps Caddy's existing certificate and replaces only
the site configuration, after taking a timestamped backup.

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

# Verify public routing, headers and admin block.
curl -fsS https://worlds.tungjung.nz/ | head -20
curl -sS -o /dev/null -w '%{http_code}\n' https://worlds.tungjung.nz/api/groups
curl -sS -o /dev/null -w '%{http_code}\n' https://worlds.tungjung.nz/api/users
# /api/users must be 404; /api/groups should be 200.
ss -ltnp | grep ':8443'   # still loopback only
```

## 5. Rollback (does not delete application data)

Run in a root shell. This restores the latest pre-cutover Caddy backup and stops
the private service. `/var/lib/glon-worlds` is preserved.

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

# Verify the landing page is back.
curl -fsS https://worlds.tungjung.nz/ | head -20

# Preserve /var/lib/glon-worlds (users.db, messages.ndjson) unless a separate,
# explicitly approved data-retention decision says otherwise.
```

## Persistence, upgrades and security notes

- Persistent state is only `/var/lib/glon-worlds`. Back it up before upgrades:
  `tar -C /var/lib -czf /var/backups/glon-worlds-$(date +%Y%m%d%H%M%S).tgz glon-worlds`.
- An application upgrade replaces `/opt/glon-worlds` but must not touch
  `/var/lib/glon-worlds`; never run `rm -rf /var/lib/glon-worlds`.
- `users.db`, `messages.ndjson` and `web/index.html` are owned by
  `glon-worlds:glon-worlds`; the state directory is `0700` and files are
  `0600` under the unit's `UMask=0077`.
- The combined loopback certificate `localhost.pem` is mode `0640`
  `root:glon-worlds`; separate `key.pem`/`cert.pem` stay root-only.
- The Glon process listens only on `127.0.0.1:8443`; Caddy is the only public
  listener. Do not add a public bind or firewall opening for 8443.
- Caddy overwrites `X-Glon-Client-IP` and `X-Forwarded-For` with
  `{remote_host}`. Glon ignores client-supplied `X-Forwarded-For` and trusts
  `X-Glon-Client-IP` only when the transport peer is loopback.
- Caddy blocks `/api/users*`. The app also rejects that route whenever either
  proxy header is present.
- Never store raw invitation codes, passwords or session bearer tokens in logs.
