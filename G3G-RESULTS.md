# G3G — Persistent Logging for the Native Glon Worlds Server

Date: 2026-11-26
Branch: `experiment/glon-http-g3g`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.

## 1. What was already there (audit)

- `examples/glon-worlds-g3b/server.glon` is an ordinary Glon application over the
  existing HTTPS, JSON, web and file vocabularies.  Implemented routes:
  `GET /api/groups`, `GET /api/session`, `GET /api/messages?group=&after=`,
  `POST /api/messages`; `404` for unknown paths, `405` for wrong methods,
  `400` for bad input, `413` when the demo store is full.
- **No logging existed.** The server produced no activity record at all.
- The native host (`glon_http.c`) printed only a one-line
  `glon: http: listening on https://...` banner and error messages to **stderr**
  on fatal bridge/dispatch failures.  There was no file, SQLite or journal
  logging, and no request log.
- CivetWeb (third_party) *supports* `access_log_file` / `error_log_file` and a
  `log_access` callback, but the Glon host did not enable any of them.

## 2. What was added (small, opt-in)

Host-side only (`glon_http.c`, `glon_http.h`, `glon.c`); the frozen S1, the
H3C/H3D task architecture and the Glon Worlds application are untouched.

- New CLI flags (all optional; default off, preserving prior behaviour):
  - `--http-log FILE` — enable persistent logging to `FILE` (e.g. `logs/server.log`).
  - `--http-log-max-bytes N` — rotation threshold (default 5 MiB = 5,242,880).
  - `--http-log-backups N` — rotated backups kept (default 3).
- A `log_access` callback (an existing CivetWeb facility) records one sanitised
  line per request: `access method=<M> path=<P> status=<S>`.
  - `path` is CivetWeb's cleaned `local_uri` with any `?query` stripped, so query
    strings are never logged.
  - The status is taken from CivetWeb's formatted access line (no server-side
    status accessor exists).
- `startup bind=<addr> port=<port>` is logged after the listener is up;
  `shutdown completed=<n> errors=<n>` is logged before `mg_stop`.
- Unexpected errors (dispatch/response/spawn/run-tasks/collect failures) are
  logged as `error ...` lines at the existing fatal sites.
- Rotation: when the file would exceed the limit it is renamed to
  `FILE.1` (shifting `FILE.2`→`FILE.3`, …), a fresh `FILE` is opened.  Writes are
  mutex-guarded and flushed per line.  Timestamps are UTC ISO-8601.
- If the log cannot be opened, logging is silently disabled (the server still
  starts); the directory is created (one level, e.g. `logs/`) when possible.

### Sanitisation guarantees

Never logged: request/response bodies, headers, cookies, session/CSRF tokens,
credentials, private message text, and query strings.  Only method, cleaned path,
status, and explicit startup/shutdown/error events are written.

## 3. Verification (`tests/glon-worlds-g3g/smoke.py`, `make g3g-test`)

Actual output:

```
PASS: startup, GET, POST (body not logged), 404 and sanitisation verified
PASS: rotation produced 1 backup file(s)
PASS: logs and messages persist across restart
G3G SMOKE PASS
```

Sample sanitised entries (from a real run, 250-byte test limit):

```
2026-11-26T04:27:04Z startup bind=127.0.0.1 port=8443
2026-11-26T04:27:06Z access method=GET path=/api/groups status=200
2026-11-26T04:27:07Z access method=POST path=/api/messages status=201
2026-11-26T04:27:08Z access method=GET path=/api/messages status=200
2026-11-26T04:27:08Z access method=GET path=/nope status=404
2026-11-26T04:27:08Z shutdown completed=4 errors=0
```

The POST body (`top-secret-body-42`) and the query (`group=`, `after=`) do not
appear anywhere in the logs.

## 4. Regression / safety checks

- `./check-frozen-s1.sh` — frozen S1 unchanged — PASS.
- Default build `make glon MODULES=` (no HTTP) — compiles — PASS.
- Without `--http-log`, no `logs/` directory is created and the server serves
  normally (GET `/api/groups` → 200) — PASS (behaviour preserved).
- The existing `tests/glon-worlds-g3b/acceptance.py` was not run here because it
  references `/mnt/d/repos/glon-worlds` (absent on this VPS); the G3G change is
  additive and disabled by default, so it does not affect that test.

## 5. Limitations

- Logging is opt-in; without `--http-log` there is still no file log (the prior
  stderr banner remains).
- Rotation is size-based only (no time-based rotation); backups are not compressed.
- The access line has no response-duration field (G3G did not require it).
- Timestamps use `gmtime` (second resolution).
- The log is not fsync'd per line beyond `fflush`; a hard crash may lose the last
  buffered line (acceptable for activity logging, not an audit trail).

## 6. Reproduce

```sh
make g3g-test
# or manually:
make glon MODULES=file,http MEM_PROFILE=large MEM_LOADER_CELLS=500000
tests/https-g1/make-cert.sh
python3 tests/glon-worlds-g3g/smoke.py
```