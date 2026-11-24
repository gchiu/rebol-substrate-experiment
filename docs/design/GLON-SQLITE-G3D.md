# Glon SQLite module (G3D-SQL)

Date: 2026-11-24

Status: implemented optional SQLite storage substrate and focused acceptance
tests. This is infrastructure, not a Glon Worlds account/session/messaging
implementation.

Worktree: `/mnt/d/repos/glon-https`, branch
`experiment/glon-https-g3b`. No extra worktree was created. No Glon Worlds file,
database or browser asset was modified.

## Phase 1 — architecture review

The existing optional modules establish the pattern used here:

- `file` owns opaque OS handles (slot + generation) and uses an asynchronous
  request/reply boundary. Glon supplies paths, buffers and callbacks.
- `http` owns sockets/TLS/parsing, passes a bounded request block to Glon, and
  accepts only a response for the request currently being dispatched.
- `sec` uses the same one-outstanding-request boundary. A pre-response host
  pump lets a closure finish before HTTP takes its response.
- Native code owns capabilities and resources. Glon owns application policy.

SQLite is therefore an **optional host capability with opaque database and
statement handles**, not a general object system. Handles are 16-character
slot/generation tokens (`8` hex digits for each part), never native pointers.
The SQLite service keeps fixed arrays of at most 8 databases and 32 statements.

## Execution and ownership model

- One SQLite service lives in the native CLI process.
- The host evaluator thread executes SQLite calls synchronously. There are no
  SQLite threads, worker pools, callback re-entry or parallel statements.
- Glon sees asynchronous callbacks because the existing runtime has no generic
  synchronous native call. A callback may queue the next SQLite operation.
- Only one SQLite request may be outstanding. A second request before the reply
  raises `SIN! sqlite busy`.
- For HTTP, the same pre-response pump used by `sec` runs SQLite turns before
  `http/take-response`. A handler can therefore query SQLite and respond from
  the resulting callback. A slow query blocks that request and the single
  evaluator thread; it does not block other SQLite calls concurrently because
  there is no concurrency.
- `sqlite3_busy_timeout(db, 0)` makes a locked database fail as a recoverable
  `busy`/`locked` error instead of blocking indefinitely.
- Shutdown finalizes every outstanding statement before closing every database.
- The service is loaded lazily when a native source refers to `sqlite/`; this
  avoids spending global bindings in applications that build SQLite support but
  do not use it. An application can also arrange to have the library staged and
  loaded by the normal source/library path.

No S1, evaluator, parser, HOST-id, frozen memory-map or unrelated runtime code
was changed.

## Phase 2 — vocabulary

All words use callbacks because Glon's native modules are host turn-based.
Success gets the value below; operation failure calls the failure closure with a
`SIN! 'sqlite <status> <message>`.

```rebol
sqlite/open path success failure
sqlite/prepare db sql success failure
sqlite/bind-int stmt index value success failure
sqlite/bind-text stmt index text success failure
sqlite/bind-null stmt index success failure
sqlite/bind-blob stmt index bytes success failure
sqlite/step stmt success failure
sqlite/reset stmt success failure
sqlite/clear stmt success failure
sqlite/finalize stmt success failure
sqlite/exec db sql success failure
sqlite/close db success failure
```

- `open` creates/opens a database read-write and requires both
  `--allow-file-read` and `--allow-file-write`. It returns an opaque database
  handle.
- `prepare` returns an opaque statement handle. Only one SQL statement may be
  prepared; trailing non-whitespace is rejected.
- `bind-int`, `bind-text`, `bind-null` and `bind-blob` use SQLite's explicit
  parameter API. User data is never interpolated into SQL. Text and blob have
  separate binding words because both use Glon `STRING!` bytes.
- `step` calls the success closure with a **row block** for a row, or `none` at
  end-of-results. Errors call the failure closure.
- `reset` resets the statement (`sqlite3_reset`); `clear` clears bindings
  (`sqlite3_clear_bindings`); `finalize` releases the statement.
- `exec` runs application-supplied trusted SQL, including `BEGIN`, `COMMIT` and
  `ROLLBACK`, and returns the changed-row count. It is not a user-data binding
  path.
- `close` finalizes every statement owned by the database, closes it, and
  invalidates the database and its statement handles.

### Row representation

A row is a flat Glon block in column order. There is no whole-result
materialisation.

| SQLite type | Glon value |
|---|---|
| NULL | `none` (distinct from `""`) |
| INTEGER in native range | integer |
| INTEGER outside native safe range | `[ 'big-int "decimal" ]` |
| REAL | `[ 'real "decimal text" ]` from `%.17g` |
| TEXT | `STRING!` (bytes preserved) |
| BLOB | `[ 'blob STRING! ]` (bytes preserved) |

Column names are not returned; the application knows its SELECT. Column values
are typed by the tags above. Empty text is `""`; SQL NULL is `none`.

### Errors and statuses

Recoverable operation statuses are exposed as `sin-id` words:

`busy`, `constraint`, `malformed`, `invalid-handle`, `stale-handle`,
`too-large`, `permission`, `not-found`, `out-of-memory`, `read-only`, `error`.

Invalid/stale handles are rejected before touching a native pointer. A closed
database invalidates its statements. Malformed SQL, constraint violations,
locked databases, failed opens and oversized values are recoverable. The host
protocol itself and callback failures fail the service pump closed.

## Sandbox and path policy

`sqlite/open` uses the same logical-name rules and canonical root as the file
module:

- `--io-root` (default `.`), `--allow-file-read`, `--allow-file-write`;
- absolute paths, drive/UNC prefixes, backslashes, empty components, `.` and
  `..` components are rejected;
- existing targets and creatable parents are resolved through the file
  backend's `glon_io_backend_resolve_path`;
- the resolved parent must remain under the canonical root.

On POSIX, an existing final symlink is rejected with `lstat`. Windows uses the
file backend's final-path/reparse checks for parent resolution; a native
Windows build was not available in this environment, so Windows is documented,
not claimed as tested. No alternate filesystem route circumvents `--io-root`.

SQLite journal files are created in the same sandbox directory.

## Limits and memory

| Limit | Value |
|---|---|
| Open databases | 8 |
| Prepared statements | 32 |
| SQL bytes | 8,192 |
| Bound text/blob bytes | 4,096 |
| Encoded row bytes | 8,192 |
| Row columns | 32 |
| Database path | 4,096 |
| Busy wait | 0 ms; locked returns `busy` |

The service uses fixed slot arrays and bounded host buffers. A row is encoded
and copied one row at a time; SQLite is never asked to materialise a whole
result. SQLite's own library allocations and database size are outside the Glon
managed heap; the row limit prevents a single oversized result from crossing
the host boundary. A result larger than the row limit raises `too-large`.

The 64 KiB constrained Glon build is not the target for SQLite applications;
the SQLite module is additive and does not alter the minimal build's memory map.

## Portability and build

```sh
make glon MODULES=sqlite \
  SQLITE_CFLAGS=... SQLITE_LIBS=...
```

`SQLITE_CFLAGS`/`SQLITE_LIBS` default to the standard `sqlite3.h` and
`-lsqlite3`. A local header/library can be supplied for a cross environment.
SQLite is dynamically linked in the measured Linux build. The optional module
does not compile or link in `MODULES=` builds. It is additive with
`file,http,sec`.

The SQLite-safe path resolver is shared with the file backend. When the file
module is not selected, the SQLite module compiles only the small POSIX/Windows
backend source, not the file Glon vocabulary or asynchronous file service.

Portability requirements for Windows/macOS/ARM are the SQLite development
header and library, plus the existing platform file backend. Only Linux was
built and tested here; no Windows/macOS/ARM execution claim is made.

## Tests

Focused tests:

```sh
make sqlite-test SQLITE_CFLAGS=... SQLITE_LIBS=...
python3 tests/sqlite-g3d/acceptance.py
python3 tests/sqlite-g3d/http_sqlite_test.py
```

Coverage:

- create/open/close and opaque handles;
- table creation and inserts;
- prepared statements and bound integer/text/NULL/blob;
- ordered iteration and end-of-results;
- injection resistance through binding;
- reset, clear and finalize;
- transaction rollback and commit;
- constraint violation;
- busy/locked database;
- malformed SQL;
- invalid, stale, finalized and closed handles;
- oversized bindings and rows;
- sandbox rejection of `../`;
- persistence across process restart;
- HTTP pre-response callback and response;
- shutdown with outstanding database/statement resources;
- cleanup leaves no open handles.

Observed:

```text
G3D SQLite service
  ...
all SQLite service tests passed
PASS SQLite example/open/close
PASS integer/text/NULL/blob round trips
PASS restart persistence
PASS transaction rollback/commit
PASS sandbox rejects ../ path
PASS SQLite step/finalize during HTTP pre-response pump
PASS SQLite-backed response returned to CivetWeb
```

Timings/RSS on this Linux host:

| Check | Result |
|---|---|
| `sqlite-test` (C) | PASS; about 0.02 s, 3,532 KiB peak RSS |
| SQLite example | PASS; 0.08 s, 5,072 KiB peak RSS |
| SQLite acceptance Python | PASS; 0.38 s, 13,032 KiB peak RSS |
| HTTP SQLite test | PASS; 2.35 s, 19,476 KiB peak RSS |
| Frozen S1 | PASS |
| G3B acceptance/browser | PASS |
| G3C sec tests | PASS |
| G1/G2/G3 HTTPS suites | PASS |
| Minimal `MODULES=` build | PASS (`minimal-ok`) |
| Full `make test` | Not completed; pre-existing IDM traffic test exceeds five minutes |

The `make test` timeout is the previously documented IDM traffic test; this
milestone did not change or optimise it.

## Size measurements

Linux, `-O0 -g`, large profile where HTTP is included:

| Build | Binary bytes |
|---|---:|
| `MODULES=` | 185,248 |
| `MODULES=file,http,sec` | 5,463,456 |
| `MODULES=sqlite` | 249,200 |
| `MODULES=file,http,sec,sqlite` | 5,503,576 |

SQLite adds 40,120 bytes to the combined binary and 63,952 bytes to the
minimal-style SQLite-only binary. The system `libsqlite3.so.0` is 1,358,520
bytes and is dynamically linked, not copied into `glon`. These are local
measurements, not a size guarantee for stripped release builds.

## Remaining limitations

- No ORM, schema migration framework, SQL parser or query builder.
- `sqlite/exec` accepts trusted application SQL; user values must use
  `bind-*`.
- No read-only open mode; `sqlite/open` requires both file read and write
  capabilities and opens read-write/create.
- No column names or metadata in a row; values are positional and typed by
  documented tags.
- Text/blob binding is capped at 4,096 bytes; SQL at 8,192; row at 8,192;
  no streaming/clob API.
- No callback concurrency, connection pool, worker thread or asynchronous
  database machinery.
- SQLite library allocations and database growth are not bounded by the Glon
  managed heap.
- No encryption, WAL tuning, fsync policy or backup API.
- POSIX final-symlink rejection is implemented; Windows was not executed here.
- No Glon Worlds accounts, sessions or authentication; no data migration.
- No static-file server, previews, attachments or chat-specific native code.

## Decision

SQLite is an additive, general-purpose capability. Database and statement
resources are opaque, bounded and explicitly owned; Glon application code owns
SQL, transaction and cleanup policy. This provides the storage foundation for a
later authenticated Glon Worlds milestone without implementing that application
here.
