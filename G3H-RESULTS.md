# G3H — SQLite User Registry for the Native Glon Worlds Server

Date: 2026-11-26
Branch: `experiment/glon-http-g3h`
Baseline: G3G, commit `8cf66c4`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.

## 1. Baseline

- G3G committed at `8cf66c4`; working tree clean except the preserved untracked
  `H3B-RESULTS.md`.
- `examples/glon-worlds-g3b/server.glon` is a **Glon** app (not Python). Its
  message store is **NDJSON** (`messages.ndjson` under `--io-root`); it had **no
  SQLite** and no users table. (The "existing SQLite schema" in the task brief
  does not exist in this Glon server; the only SQLite is the optional runtime
  module `modules/sqlite.glon`.)
- Frozen S1 unchanged (`./check-frozen-s1.sh` passes).

## 2. Schema and migration

New database `users.db` under the `--io-root`, created at startup:

```sql
CREATE TABLE IF NOT EXISTS users (
  id           INTEGER PRIMARY KEY AUTOINCREMENT,
  username     TEXT NOT NULL UNIQUE COLLATE NOCASE,
  display_name TEXT NOT NULL,
  role         TEXT NOT NULL DEFAULT 'user',
  status       TEXT NOT NULL DEFAULT 'active',
  created_at   TEXT NOT NULL
);
```

- **Migration:** `CREATE TABLE IF NOT EXISTS` is idempotent — it creates the
  table on a fresh database and is a no-op on an existing one, so an existing
  `users.db` (and its rows) is preserved. There is no destructive change.
- **Messages/groups preserved:** the NDJSON message store and the single-group
  model are untouched; `users.db` is separate.
- No passwords, tokens, sessions or credentials are stored.

## 3. Endpoints (loopback-only administration)

All `/api/users*` requests require a **loopback peer** (`127.0.0.1`, `::1`,
`::ffff:127.0.0.1`); any other peer gets `403`. The listener binds loopback by
default. Mutations are never exposed to the public network.

- `GET /api/users` — list users (`id` ascending).
- `GET /api/users?id=N` — one user by stable id, or `404`.
- `POST /api/users` — body `{username, display_name, role?}`; `role` defaults to
  `user` and must be `user` or `admin`; `201` with the created record, `409` on
  a duplicate username (case-insensitive), `400` on invalid input.
- `POST /api/users/disable` — body `{id}`; `200` with `status:"disabled"` (the
  row is kept, never deleted), or `404`.

Usernames and ids are never trusted as proof of identity (there is no auth in
this milestone); the loopback restriction is the only access control.

## 4. Implementation notes

- Uses the existing async SQLite vocabulary (`sqlite/open`, `sqlite/exec`,
  `sqlite/prepare`, `sqlite/bind-text`, `sqlite/bind-int`, `sqlite/step`,
  `sqlite/finalize`) and the existing HTTP vocabulary; no new host code.
- **Global-context constraint:** the Glon global context is capped at 256
  bindings and is shared with the loaded libraries. With `http`+`json`+`sqlite`
  loaded it sits at ~230, leaving ~26. The original flat server used 28 globals,
  which no longer fits once `sqlite.glon` (+25) is loaded. The server was
  therefore restructured into nested installer contexts (each ≤ 16 bindings,
  the `R0S1_CTX_CAP`), so the global context now holds only `gw/users-db`,
  `handle` and `app/install` (233/256). All message logic is preserved.
- **Startup order:** the file (io pump) read runs first, then the sqlite
  (pre pump) open/migration, then `http/listen` — matching the host's
  drain order (io before pre). The message store is still read before listen.

## 5. Verification (`tests/glon-worlds-g3h/smoke.py`, `make g3h-test`)

Actual output:

```
PASS: created two users (alice=admin, bob=user)
PASS: duplicate username (case variant) rejected with 409
PASS: retrieved both users by stable id
PASS: disabled user 2 (record kept, status=disabled)
PASS: existing message endpoints still work
PASS: G3G logging works without exposing user-management bodies
PASS: user records persist across restart
PASS: non-loopback mutation rejected (403); loopback still allowed
G3H SMOKE PASS
```

All 8 acceptance criteria pass. The non-loopback test binds `0.0.0.0` and
connects via the container's non-loopback address (peer != loopback → 403),
while loopback on the same listener still succeeds (201).

## 6. Regression / safety

- `./check-frozen-s1.sh` — frozen S1 unchanged — PASS.
- Message endpoints (`GET /api/groups`, `POST/GET /api/messages`) still work —
  PASS (in the smoke).
- G3G logging still works and does not record user-management bodies — PASS.
- Build: `make g3h-test` (MODULES=file,http,sqlite) — PASS.

## 7. Limitations

- The Glon Worlds demo now requires `MODULES=file,http,sqlite` (it uses the
  SQLite vocabulary). The message behaviour is unchanged, but the build must
  include sqlite.
- No authentication: the loopback restriction is the only guard; a local user
  can still create/disable users. Auth is explicitly out of scope.
- `created_at` is a fixed synthetic timestamp (Glon has no clock vocabulary).
- On a statement error other than a UNIQUE violation, create returns 409
  (the dominant error is a duplicate username).

## 8. Reproduce

```sh
make g3h-test
# or:
make glon MODULES=file,http,sqlite MEM_PROFILE=large MEM_LOADER_CELLS=500000
tests/https-g1/make-cert.sh
python3 tests/glon-worlds-g3h/smoke.py
```

## 9. Commit

Branch `experiment/glon-http-h3h`, one local commit (see `git log`). Not pushed,
merged or deployed.