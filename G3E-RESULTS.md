# G3E — Glon HTTPS + SQLite message application: results

Date: 2026-11-25
Branch: `experiment/glon-https-g3e`
Baseline: `b9fb4f8` (`experiment/glon-https-g3b`)
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM, 1 GiB swap, 20 GB disk

## Objective

Demonstrate that one Glon process can receive HTTPS requests, read and write
SQLite records, and serve multiple browser/HTTP clients. This is a
general-purpose capability test, not a Glon Worlds migration.

## Implementation

All application logic is Glon. Python only drives HTTP and inspects the SQLite
file; it implements none of the application.

Files:

- `examples/https-g3e/messages.glon` — the application.
- `tests/https-g3e/acceptance.py` — automated acceptance harness.
- `Makefile` — `https-g3e-test` target.
- `README.md` — build/test instructions.

Endpoints:

| Endpoint | Behaviour |
|---|---|
| `GET /messages` | JSON array of stored messages (`ORDER BY id LIMIT 100`) |
| `GET /messages/<id>` | one message as a JSON object, or 404 |
| `POST /messages` | validate a JSON object and insert a row, or 4xx |

Schema (created on startup, `CREATE TABLE IF NOT EXISTS`):

```sql
CREATE TABLE messages(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  author TEXT NOT NULL,
  text   TEXT NOT NULL,
  ts     INTEGER NOT NULL)
```

Design points:

- **Parameterised SQL only.** The INSERT uses `?` placeholders bound with
  `sqlite/bind-text`; the id lookup uses `sqlite/bind-int`. No request data is
  concatenated into SQL.
- **Single-statement insert.** `INSERT ... RETURNING id,author,text,ts` returns
  the stored row in one prepared statement (SQLite 3.46), so the response
  reflects exactly what was written and no `last_insert_rowid()` round-trip is
  needed.
- **Asynchronous host pump.** The handler enqueues one SQLite request and
  returns; the host pre-response pump runs the statement and the
  success/failure closure eventually calls `http/respond`. This reuses the
  existing G3D mechanism; no new threading model was introduced.
- **Bounded, validated input.** Author ≤ 64 bytes, text ≤ 400 bytes; empty or
  missing fields, non-string fields, malformed JSON, wrong content type and
  non-numeric ids return 400/415/422. The host rejects bodies over
  `--http-max-body` with 413 before dispatch.
- **Small global context.** Helpers live in nested installer contexts (each
  ≤ 16 bindings, the per-context ceiling `R0S1_CTX_CAP`), so the global context
  holds only the DB handle, limits and `handle`.

### Request-body ceiling

The request body reaches the handler as a `mk-string [byte ...]` block, and the
Glon reader caps a block literal at 511 elements (`MASM_TOK_CAP`). A body larger
than ~511 bytes therefore fails to parse at dispatch. The deployment aligns
`--http-max-body 500` with this ceiling so larger bodies are rejected cleanly
with 413 by the host instead of failing dispatch. Author/text limits keep a
maximum-size valid body under that ceiling.

## Vocabulary changes

None. The application reuses the existing `http`, `json`, `web` and `sqlite`
vocabularies plus the shipped prelude/strings/strings-ext. No C host code,
frozen S1 file, memory map or capacity was changed. `./check-frozen-s1.sh`
passes.

## Tests executed and passed

Build (sequential, one binary at a time):

```
make glon MODULES=file,http,sqlite MEM_PROFILE=large MEM_LOADER_CELLS=500000
```

- `./check-frozen-s1.sh` — PASS (S1 substrate unchanged).
- `make sqlite-test` — PASS (all SQLite service C tests).
- `tests/sqlite-g3d/acceptance.py` — PASS (example, restart, transaction,
  sandbox).
- `tests/sqlite-g3d/http_sqlite_test.py` — PASS (SQLite callback → HTTP
  response through the pre-response pump).
- `tests/https-g1/acceptance.py` — PASS (also under `MODULES=file,http`).
- `tests/https-g2/acceptance.py` — PASS (also under `MODULES=file,http`).
- `tests/https-g3/acceptance.py` — PASS (`MODULES=http GLON_HTTP_RECLAIM_M1=1`).
- `tests/https-g3a/acceptance.py --bulk-adds 40` — PASS (large profile,
  `MODULES=file,http`; reduced bulk count to fit the 1 GB RAM budget).
- `tests/https-g3e/acceptance.py` — PASS:

```
PASS: fresh start, POST 201, GET one and GET list agree
PASS: messages survive a full server restart
PASS: 8 concurrent inserts across 4 clients -> unique ids, all retrievable
PASS: malformed/oversized/wrong-type requests rejected with no state change
PASS: SQL-injection payload stored literally; table intact
PASS: 20 repeated requests; peak RSS 15704 KiB
PEAK_RSS_KIB=15704
HTTPS_G3E_ACCEPTANCE PASS
```

The G3E suite covers: fresh start and CRUD JSON shape; restart persistence
(stop, restart in the same directory, data intact); concurrent inserts from
4 keep-alive clients (8 inserts, unique ids, all listed); malformed, oversized,
wrong-type, missing-field, bad-id, 404, 405 and 415 handling with no state
change; a SQL-injection payload stored literally with the table intact; and a
bounded repeated-request run with peak-RSS sampling.

## Measurements

Configuration: `MODULES=file,http,sqlite`, `MEM_PROFILE=large`,
`MEM_LOADER_CELLS=500000`, single Glon evaluator thread (`num_threads=1`).

- Executable size (`glon`): **485,184 bytes** (~474 KiB).
- Peak resident memory (G3E repeated-request run, 20 requests): **15,704 KiB**
  (an earlier run measured 16,464 KiB). RSS stayed flat across the run.
- Peak RSS is sampled from `/proc/<pid>/status` `VmRSS` every 20 ms.

## Skipped or blocked

- **100,000-request endurance** — not run; the documented endurance target is
  uncompleted and the 1 GB RAM / single-thread budget makes it inappropriate
  here.
- **Browser automation** — not used; the acceptance harness drives HTTPS with
  Python `http.client`, which is sufficient for the success criterion.
- **Large-scale concurrency** — the server runs one Glon evaluator thread, so
  clients are serialised. The suite uses 4 clients / 8 concurrent inserts,
  which is enough to show unique ids and multi-client reads/writes.

## Limitations observed

- **Single-threaded server.** `num_threads=1` (one Glon evaluator owner).
  Concurrent clients are served cooperatively and serially; "concurrent"
  inserts are serialised but still receive unique AUTOINCREMENT ids.
- **Per-connection latency growth.** With a fresh connection per request, the
  single worker's latency grows with request count (the documented
  keep-alive/accept starvation). The harness therefore reuses one keep-alive
  connection per client, mirroring the G3A endurance harness.
- **GET /messages cost grows with row count**, and the managed heap is small
  (large profile ≈ 540,000 cells). The list endpoint is bounded with
  `LIMIT 100`; very large collections would need pagination (consistent with
  the JSON collection-size limits). Tests keep the dataset small.
- **Request bodies are capped at ~500 bytes** by the reader's 511-element block
  limit (see above); larger bodies return 413.
- **No fsync/power-failure guarantee** and no authentication/authorisation —
  inherited from the existing SQLite and HTTPS modules.

## Reproduce

```
make https-g3e-test
```

This builds the large-profile `file,http,sqlite` binary, generates the local
test certificate, and runs the bounded acceptance suite. Do not expose the
experimental HTTPS server to the public internet.