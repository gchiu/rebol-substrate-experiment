# Glon Worlds pilot deployment (staged, not applied)

Status: W5 deployment assets only. Nothing in this directory has been installed,
enabled or applied on the VPS. The public site still serves the landing page.
Do not replace it until explicitly approved.

These files prepare a small invited-user pilot at `https://worlds.tungjung.nz`
while preserving Caddy's existing certificate and the current static page until
cutover.

## Target layout

| Path | Purpose |
|---|---|
| `/opt/glon-worlds` | read-only application tree: binary, `glon-lib/`, `modules/`, example app |
| `/var/lib/glon-worlds` | service state: `users.db`, `messages.ndjson`, `web/index.html` |
| `/etc/glon-worlds/localhost.pem` | self-signed loopback cert+key used by Glon only |
| `/etc/systemd/system/glon-worlds.service` | installed unit file |
| `/etc/caddy/Caddyfile` | Caddy's existing config (keep a timestamped backup) |

The service user is `glon-worlds`, with no login shell. The Glon process binds
only `127.0.0.1:8443`; Caddy remains the only public listener.

## Staging steps (after approval)

1. Build and install the application without changing the public site:

   ```sh
   make glon MODULES=file,http,sqlite,sec,task MEM_PROFILE=server-https
   sudo install -d -o root -g root /opt/glon-worlds
   sudo install -m 0755 glon /opt/glon-worlds/glon
   sudo cp -a glon-lib modules examples /opt/glon-worlds/
   ```

2. Create the unprivileged user and state directory:

   ```sh
   sudo useradd --system --home /var/lib/glon-worlds --shell /usr/sbin/nologin glon-worlds
   sudo install -d -o glon-worlds -g glon-worlds -m 0700 /var/lib/glon-worlds/web
   sudo install -m 0644 examples/glon-worlds-g3b/web/index.html /var/lib/glon-worlds/web/index.html
   ```

   `StateDirectory=glon-worlds` in the unit also creates the state directory
   when supported; the explicit commands make ownership clear.

3. Generate the loopback-only certificate. This does not touch Caddy's files:

   ```sh
   sudo install -d -o root -g glon-worlds -m 0750 /etc/glon-worlds
   sudo ./deploy/glon-worlds/make-local-cert.sh /etc/glon-worlds
   ```

4. Install the service unit, but do not enable it until cutover is approved:

   ```sh
   sudo install -m 0644 deploy/glon-worlds/glon-worlds.service /etc/systemd/system/
   sudo systemctl daemon-reload
   sudo systemctl start glon-worlds
   sudo journalctl -u glon-worlds -n 40 --no-pager
   curl -k --resolve worlds.tungjung.nz:8443:127.0.0.1 https://worlds.tungjung.nz:8443/api/groups
   ```

5. Create invitations after the service has created the schema:

   ```sh
   sudo -u glon-worlds python3 deploy/glon-worlds/glon-worlds-invite.py \
     --db /var/lib/glon-worlds/users.db --label pilot-1 --days 7
   ```

   The raw code is printed once; only its SHA-256 digest is stored. Registration
   through Caddy (`X-Forwarded-For` present) requires a valid, unexpired,
   single-use invitation. Direct loopback registration without an invitation is
   retained only as a local bootstrap path.

6. Only after explicit approval, back up Caddy's current config and stage the
   reverse proxy:

   ```sh
   sudo cp -a /etc/caddy/Caddyfile /etc/caddy/Caddyfile.pre-glon-worlds-$(date +%Y%m%d%H%M%S)
   sudo install -m 0644 deploy/glon-worlds/Caddyfile.worlds /etc/caddy/Caddyfile
   sudo caddy validate --config /etc/caddy/Caddyfile
   sudo systemctl reload caddy
   ```

   The staged config keeps Caddy's existing automatic HTTPS certificate and
   blocks `/api/users*` before proxying. Glon also rejects those routes when a
   proxy header is present.

   Caddy overwrites `X-Glon-Client-IP` and `X-Forwarded-For` with
   `{remote_host}`. Glon ignores client-supplied `X-Forwarded-For`; it trusts
   `X-Glon-Client-IP` only when the transport peer is loopback. This keeps rate
   limiting tied to the address Caddy observed, not an attacker-supplied header.
   Public application routes intended through Caddy are `/`, `/index.html`,
   `/api/groups`, `/api/session`, `/api/auth/*` and `/api/messages`.

## Rollback

1. Restore the previous Caddyfile and reload Caddy:

   ```sh
   sudo cp -a /etc/caddy/Caddyfile.pre-glon-worlds-<timestamp> /etc/caddy/Caddyfile
   sudo caddy validate --config /etc/caddy/Caddyfile
   sudo systemctl reload caddy
   ```

   This returns the public site to the landing page.

2. Stop and disable the Glon service:

   ```sh
   sudo systemctl stop glon-worlds
   sudo systemctl disable glon-worlds
   ```

3. Preserve `/var/lib/glon-worlds` (`users.db`, `messages.ndjson`) unless a
   separate, explicitly approved data-retention decision says otherwise.

## Operational notes

- The service is unprivileged and loopback-only. Do not bind it publicly.
- Back up `/var/lib/glon-worlds` before upgrades or invite-code maintenance.
- Never store raw invitation codes, passwords or session bearer tokens in logs.
- The current application still stores session tokens hashed at rest, but the
  cookie token is a bearer secret; protect `users.db` file permissions.
- Real-browser acceptance is separate from the automated W5 tests.

## Manual browser acceptance (required before cutover)

Automated browser automation is not installed on this VPS. Before cutover, run
the procedure in
`tests/glon-worlds-w5.1/MANUAL-BROWSER-ACCEPTANCE.md` against the loopback
service (or a staging hostname) with two real browser profiles. Do not treat the
automated HTTPS tests as a substitute for real-browser acceptance.
