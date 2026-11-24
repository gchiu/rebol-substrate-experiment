# Glon Worlds compatibility demonstration (G3B)

Date: 2026-11-24

Status: working local demonstration. This is not an authentication, deployment,
or production-readiness claim.

## Objective

Prove that the existing general-purpose Glon HTTPS runtime can serve the
unauthenticated read path and a deliberately local write path of the Glon
Worlds browser client without:

- modifying the Glon Worlds Python server, SQLite database, attachments or
  browser assets;
- adding chat-specific native HTTP code;
- adding a static-file server to Glon;
- exposing an unauthenticated listener beyond loopback.

The demonstration lives in `examples/glon-worlds-g3b/server.glon`; its tests
live in `tests/glon-worlds-g3b/`.

## Phase 1: extracted contract

Source of truth: `/mnt/d/repos/glon-worlds/server/server.py` and
`/mnt/d/repos/glon-worlds/chat/chat.js`.

### Group

`GET /api/groups` -> `200 application/json; charset=utf-8`

```json
{
  "groups": [
    {
      "id": 1,
      "slug": "general",
      "name": "general",
      "description": "",
      "created_at": "..."
    }
  ],
  "can_create": false
}
```

- Groups are ordered by ascending `id`.
- `can_create` is authorization-dependent. Anonymous access is false.
- Source: `list_groups` at `server/server.py:401`, route at
  `server/server.py:1049`, client load at `chat/chat.js:502`.

### Message

`GET /api/messages?group=<slug>&after=<id>` ->
`200 application/json; charset=utf-8`

```json
{
  "messages": [
    {
      "id": 1,
      "author": "Alice",
      "text": "Markdown source",
      "created_at": "2026-01-01T00:00:00+00:00",
      "group": "general",
      "user_id": 7,
      "legacy": false,
      "preview": null,
      "preview_pending": false,
      "youtube": null,
      "html": "<p>server-rendered HTML</p>"
    }
  ]
}
```

- `group` defaults to `general`; `after` defaults to `0`.
- Messages are chronological (`id ASC`) and filtered by group.
- An unknown group returns `200` with `{"messages":[]}`, not an error.
- `html` is server-rendered. The client uses it directly.
- `preview`, `preview_pending` and `youtube` are always present; the demo emits
  `null`/`false` and does not create previews.
- Source: `_msg_dict` at `server/server.py:529`, `list_messages` at
  `server/server.py:593`, route at `server/server.py:1044`, client poll at
  `chat/chat.js:274`.

### Session

`GET /api/session`

- Anonymous: `200 {"user": null}`.
- Authenticated: `{"user":{"id":...,"username":...,"display_name":...},"csrf":"..."}`.
- The client uses this to decide read-only versus composer and to obtain the
  CSRF token.
- Source: route at `server/server.py:1022`, client at `chat/chat.js:590`.

### Write path in the Python server

`POST /api/messages` with JSON `{ "text": "...", "group": "general" }`

- Requires a session cookie and `X-CSRF-Token`; otherwise `401`/`403`.
- Author identity comes from the session, never the body.
- `text` is required after trimming; maximum is `MAX_TEXT = 8000`.
- Unknown group, invalid JSON and invalid text are `400 {"error":"..."}`.
- Success is `201` with the message object, including `html`.
- Markdown/HTML rendering and link/YouTube previews are server-side.
- Source: `_post_message` at `server/server.py:1212`, `MAX_TEXT` at
  `server/server.py:65`.

### Error and header conventions

- JSON responses use `application/json; charset=utf-8`.
- JSON responses include `Cache-Control: no-store`.
- Error bodies are `{"error":"..."}`.
- There is no CORS middleware and no wildcard origin.
- Sessions use `gw_session` cookies (`HttpOnly; SameSite=Lax`) and CSRF; login
  uses a separate one-time login CSRF token.
- Source: `_send_json`/`_json_error` around `server/server.py:948-973`,
  `auth.py` (`SESSION_TTL` at `auth.py:48`, scrypt at `auth.py:69`, sessions at
  `auth.py:295`, CSRF at `auth.py:357`).

### Client origin requirement

`chat/chat.js:23` sets `var API = ""`, so all requests are relative and
same-origin. Cross-origin use would require CORS, credentialed requests,
cookie policy, preflight and CSRF handling that the current server does not
provide.

## Phase 2: implemented demonstration

`examples/glon-worlds-g3b/server.glon` is ordinary Glon over the existing
HTTP, JSON, web and file vocabularies. It implements:

- `GET /api/groups`
- `GET /api/session`
- `GET /api/messages?group=...&after=...`
- `POST /api/messages`

It does not modify the S1 substrate, the native HTTPS host, the JSON module,
the file protocol, or Glon Worlds.

### Deliberate demo deviations

| Contract | Demo behavior |
|---|---|
| Authenticated POST, CSRF, session author | Unauthenticated loopback-only POST; fixed author `Glon Demo` |
| Real `created_at` | Fixed synthetic ISO timestamp; Glon has no clock vocabulary |
| Markdown, previews, YouTube | Plain escaped HTML in `<p>...</p>`; no previews |
| Text limit 8000 | 1024 bytes; request body still bounded by the host |
| Auth/session cookies | Not implemented |
| Python `int(after)` failure | Non-numeric `after` returns `400 invalid after` |
| Unbounded SQLite store | NDJSON file capped at 1024 bytes; `413 demo store full` |
| Static/Public routes | Not implemented |

Unknown groups on `POST` return `400 unknown group`; unknown groups on `GET`
return `200 {"messages":[]}`, matching the Python read behavior.

### Persistence

- Data path: `messages.ndjson` under the CLI `--io-root`.
- The file is read once at startup, before `http/listen` is configured. This is
  required because the host only allows a response for the request currently
  being dispatched; a deferred file-read response is invalid.
- Messages are held in memory and POST replies immediately. The NDJSON append
  is asynchronous and is drained by the host after the response.
- Restart replays the file and restores the next id.
- Limits: whole-file read/write is 1024 bytes; there is no fsync, locking,
  atomic rename, multi-process coordination, or SQLite. This is demonstration
  persistence only.
- Glon Worlds' own `server/data/messages.db` is never opened or modified.

### Loopback

Run with an explicit loopback bind:

```sh
./glon --http-bind 127.0.0.1 --http-cert tests/https-g1/.cert/server.pem \
  --io-root /tmp/glon-worlds-demo --allow-file-read --allow-file-write \
  glon-lib/strings-ext.glon modules/web.glon modules/json.glon \
  examples/glon-worlds-g3b/server.glon
```

The acceptance test verifies the actual `ss` local address is only
`127.0.0.1:8443`; a connection to the container's non-loopback address is
refused. No static file serving is added.

## Phase 4/5: browser and authentication gaps

### Browser compatibility test

`tests/glon-worlds-g3b/browser_check.py` loads the real
`chat/index.html` and `chat/chat.js` from an external test-only static server
and reverse-proxies `/api` to the Glon demo on the same origin. It is an actual
headless Chromium check, not a hand-built client.

Observed result:

```text
PASS existing chat HTML/JS rendered a Glon message
PASS same-origin API groups/messages/session all returned 200
PASS unauthenticated browser is read-only
```

The existing client can read groups, messages and session when served
same-origin. The browser's `GlonBot` WASM asset is absent in this test, so the
client reports the bot as unavailable; that does not affect the read path.

### Authentication is intentionally absent

A production-equivalent login/session implementation is not safe to add in this
demo because the current runtime lacks:

- a generic CSPRNG for salts, session tokens and CSRF tokens;
- SHA-256/scrypt/HMAC or constant-time comparison;
- a wall clock for expiry;
- peer/client-IP access for login throttling;
- SQLite or a transactional, unbounded credential store;
- secure cookie support and a full auth/CSRF flow.

Do not add fixed passwords, fixed session ids, client-supplied authors, or an
authentication bypass to close this gap. Login-token/login/logout,
user/session tables, password hashing, CSRF validation and rate limiting remain
unimplemented. The browser client is read-only because `/api/session` returns
`{"user":null}`.

### Other missing API surface

Not implemented in this demonstration: `GET /api/groups/<slug>`,
`GET/POST /api/preview(s)`, attachments, group creation/description editing,
login-token/login/logout, `/health`, public `/m/<id>`, `/m/<id>.md`,
`/g/<slug>`, static assets, Markdown rendering, link previews, YouTube cards
and image upload. These are not required for the minimal read path and must not
be presented as complete.

## Acceptance

Commands run from `/mnt/d/repos/glon-https`:

```sh
python3 tests/glon-worlds-g3b/acceptance.py
python3 tests/glon-worlds-g3b/browser_check.py
```

Actual acceptance output:

```text
PASS groups/session/read
PASS validation and escaped HTML
PASS ordering, pagination, unknown group
PASS restart persistence and next id
PASS loopback-only listener
PASS Glon Worlds database and client unchanged
```

The test also checks that `server/data/messages.db` and `chat/chat.js` hashes
are unchanged, that a whitespace-only POST is rejected, that HTML is escaped,
that `after` pagination is chronological, that unknown groups match the read
contract, and that restart preserves ids and messages.

Frozen S1 and the existing regression suite remain separate checks:

```sh
./check-frozen-s1.sh
make test
```

No Glon Worlds files, database rows, attachments, static assets, frozen S1
files, or native chat-specific routes were changed.
