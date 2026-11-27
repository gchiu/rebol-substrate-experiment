# GLON-WORLDS-W2 — Message History and Multi-Client Messaging

Date: 2026-11-27
Branch: `integration/glon-worlds-w2`
Worktree: `/home/developer/projects/glon-worlds-w2`
Base: W1 `24fc99c4ec1e601faf8ddbfd90012c89169756ce`
Unified core: C2 (`4dcae2a`)
Frozen S1: `f90496c26dc45c7a387d8fc8639cd2781507d3d2`
Status: accepted locally; committed after the tests below. Not pushed, merged,
rebased or deployed. W3 was not started.

**Result:** Glon Worlds now exposes bounded, paginated message history and
supports two independent HTTPS clients exchanging messages through the native
Glon server. Message IDs are stable, pages are consistent, append-only NDJSON
storage survives restart, and ordinary concurrent posts keep unique IDs and
complete contents. The history is never loaded into one Glon string/list.

## 1. API contract

`GET /api/messages?group=general&after=<id>&limit=<n>`

- `after` is an exclusive cursor; omitted/0 starts at the oldest message.
- `limit` is 1..9; omitted defaults to 9. The current cap is a measured task-mode
  limit (see §5), not a language limit.
- Messages are returned in ascending stable ID order.
- Response:

```json
{
  "messages": [ ... ],
  "next_after": 9,
  "has_more": true
}
```

- Clients page by passing `after=next_after`. A page walk has no omissions or
  duplicates; `has_more` is false on the final page.
- Invalid `after`/`limit` returns 400. An overlapping history scan returns a
  controlled 503 `"message history busy"` rather than mixing scanner state.
- Existing `after`-style clients remain compatible within the page-size limit;
  extra response fields are additive.

`POST /api/messages` is unchanged: JSON `{text, group}` -> 201 with the stored
message; invalid JSON, blank/over-limit text and unknown group remain controlled
400 responses.

## 2. Persistence design

- One append-only NDJSON record per message (`messages.ndjson`), unchanged format.
- IDs are reserved from the monotonic `gw/next-id` before the asynchronous
  append, so overlapping posts cannot receive the same ID.
- Short lines (≤4096 bytes) use one `io/write` append; longer lines use the
  existing bounded `stream/write-all` path. The file API remains serialized by
  the host.
- Startup scans only the leading `{"id":` field and byte count. It does not load
  the history into a string or a list.
- Page retrieval scans the file in bounded chunks, parses each record's leading
  ID, skips records at/below the cursor, and JSON-decodes only candidate page
  records (plus one look-ahead to set `has_more`). At most the page plus one
  record is retained.
- `gw/max-store` remains 16,384 bytes. No compaction or deletion is added.
- The direct HTTP bridge now drains the file pump before collecting a response,
  so streaming history/append works in both direct and task mode. This is a
  host scheduling fix only; no Glon semantics or profile changed.

## 3. Two-client HTTPS evidence

`tests/glon-worlds-w2/history.py` starts the real native G3H server with
`--http-tasks` on `server-https` (3 × 3,400-cell stacks) and uses actual TLS
HTTP clients:

- 18 seeded messages across two pages, all returned in stable ascending order
  with unique IDs.
- Successive page walks cover every message exactly once.
- Page limits 1..9, default page size, and invalid cursor/limit errors verified.
- Client A posts; Client B retrieves it, replies; Client A retrieves the reply.
  Both clients see identical ID/text ordering.
- Four concurrent posts receive unique IDs and all contents survive.
- Overlapping GET scans return complete pages or controlled 503s, never mixed
  pages.
- The NDJSON store has 24 valid records after the run, ordered by unique ID.
- Server restart preserves the full paginated history and ordering.
- No machine halt in the server log.

Output: **`W2_HISTORY PASS`**.

## 4. Browser client

No browser UI was added in W2. The repository's maintained Glon Worlds path is
the native server/API; the earlier Python prototype is not migrated. Building a
browser client (send, recent messages, older-page loading, refresh) is reported
as a separate milestone so it does not expand W2's server/persistence work.

## 5. Preserved behaviour and tests

| Test | Result |
|---|---|
| `make w2-test` | **W2_HISTORY PASS** |
| `make w1-test` | **W1_MESSAGING PASS** (including 1,024-byte and escaped-max messages) |
| `make w0-routes-test` | **W0_ROUTES PASS** |
| `make g3h-test` | **G3H SMOKE PASS** (direct mode) |
| `make h3e-test` | **H3E ACCEPTANCE PASS** |
| `make h3c-test` | **H3C ACCEPTANCE PASS** |
| `make json-test` | **JSON_TESTS PASS** |
| `make json-j1-test` | **JSON_J1_TESTS PASS (73 checks)** |
| `make json-j2-test` | **JSON_J2_TESTS PASS (96 checks)** |
| `make json-j2-g3h-test` | **JSON_J2_G3H PASS** |
| `make sqlite-test` | All SQLite service tests passed |
| `make sec-test` | All security primitive tests passed |
| `make https-g3a-test` | **HTTPS_G3A_ACCEPTANCE PASS** |
| `./check-frozen-s1.sh` | **PASS** at `f90496c…` |
| `make profile-check` | **PASS (8 profiles)** |
| `make profile-reject-check` | **PASS (15 cases)** |
| `make profile-common-test` | **PASS (8 profiles)** |
| `make build-deps-check` | **PASS (30 checks)** |
| `make global-ctx-hash-test` | **PASS on all 8 profiles** |
| `make m1-large-test MEM_PROFILE=large` | **PASS**, `M1_LARGE failures=0` |

No frozen S1, language, profile, memory-map or task-stack capacity changed.

## 6. Remaining blockers / next application milestone

- **Page size is 9.** Page reversal currently uses a recursive list walk; at
  20+ items it exceeds the 3,400-cell task return stack. A bounded reverse or
  direct ascending page builder is the smallest W3 improvement.
- **Concurrent history scans are serialized.** A second overlapping scan gets a
  controlled 503 because the scanner state is global/non-reentrant.
- **Startup validates only the leading ID field**, not every full JSON record.
  Malformed data after the ID is detected when that record is read.
- **NDJSON log has no compaction or deletion.** It is bounded by
  `gw/max-store` (16,384 bytes); a larger history needs storage rotation or a
  different store.
- **Heavily escaped JSON remains slow** (JSON J2 limitation).
- **Browser UI is not implemented.**
- No authentication, sessions, CSRF, rate limiting or public deployment.

**Proposed W3:** bounded page assembly (raising page size within the existing
task stacks), then a minimal browser client over this API.
