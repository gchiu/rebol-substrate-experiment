# GLON-WORLDS-W3 — Native Glon Browser Messaging Client

Date: 2026-11-27
Branch: `integration/glon-worlds-w3`
Worktree: `/home/developer/projects/glon-worlds-w3`
Base: W2 `1bffec512160de1fb01f12786d9aa48930c8132d`
Unified core: C2 (`4dcae2a`)
Frozen S1: `f90496c26dc45c7a387d8fc8639cd2781507d3d2`
Status: accepted locally; committed after the tests below. Not pushed, merged,
rebased or deployed. W4 was not started.

**Result:** the native Glon Worlds HTTPS server now serves a usable single-page
browser client at `/` and `/index.html`. It displays the current group and
messages, sends messages, pages older history, refreshes for new messages, and
reports loading/success/error states. Message text is rendered with
`textContent` only; the client never assigns untrusted content to `innerHTML`.

## 1. Browser functionality

- Current group shown as `general`.
- Message list in stable ID order with author, `#id`, and readable `created_at`
  timestamp.
- Send box (1,024-byte limit) plus Send button; Ctrl/Cmd+Enter sends.
- **Load older** reveals the previous page from history fetched through W2's
  paginated API.
- **Refresh** fetches pages after the newest known ID; an automatic refresh runs
  every 15 seconds.
- Loading, success, and error states are shown in an `aria-live` status area.
- Controlled 400 and 503 responses are surfaced as readable errors; 503 says
  history is busy and can be retried.
- No JavaScript framework; one inline CSS/JS file.
- Same-origin HTTPS `fetch`; no cross-origin requests.
- Message content uses `textContent`; the asset contains no `innerHTML` use.

## 2. Routes and assets

| Route | Change |
|---|---|
| `GET /` | Serves `web/index.html` from the `--io-root` directory. |
| `GET /index.html` | Same browser client. |
| `GET /api/...` | Unchanged W2 routes. |
| `POST /api/messages` | Unchanged W2 message contract. |

- New asset: `examples/glon-worlds-g3b/web/index.html` (single file, inline
  CSS/JS).
- Server-side asset read uses the existing file vocabulary in bounded 400-byte
  chunks with a 65,536-byte cap; it is not embedded in Glon source and does not
  alter the message store.
- Run with the example directory as the I/O root, for example:

```sh
make glon MODULES=file,http,sqlite,task MEM_PROFILE=server-https
tests/https-g1/make-cert.sh
./glon --http-tasks --http-cert tests/https-g1/.cert/server.pem \
  --io-root examples/glon-worlds-g3b \
  --allow-file-read --allow-file-write \
  modules/web.glon modules/json.glon examples/glon-worlds-g3b/server.glon
# open https://127.0.0.1:8443/
```

The test copies `web/index.html` into a temporary `--io-root` instead of writing
to the source tree.

## 3. Two-client evidence

`tests/glon-worlds-w3/browser_api.py` (run by `make w3-test`) starts the real
native server with `--http-tasks` on `server-https` and uses two independent TLS
HTTP clients. It verifies:

- `/` and `/index.html` return the browser asset with `text/html`; the asset
  contains the UI controls and uses `textContent`, not `innerHTML`.
- Both clients load the same group/history.
- Client A sends an HTML-metacharacter message and a Unicode message; Client B
  retrieves both with exact text.
- Client B replies; Client A retrieves the reply.
- Both clients see identical message IDs and chronological ordering.
- A page walk over W2's API covers every message exactly once.
- Refresh with no new messages is stable.
- The conversation survives a server restart for both clients.
- No machine halt in the task-mode log.

Output: **`W3_BROWSER PASS`**.

### Real browser vs automated evidence

- **Verified:** native server asset serving, safe-rendering source constraints,
  and two independent browser-equivalent HTTPS clients for all nine acceptance
  behaviours.
- **Not verified:** execution in a real browser engine. No browser (Chromium,
  Firefox, WebKit) or browser automation runtime is installed in this
  environment. The UI source is implemented and asset-checked, but actual DOM
  behaviour (layout, events, auto-refresh, Ctrl/Cmd+Enter) still requires a
  real-browser pass.

## 4. Preservation test evidence

| Test | Result |
|---|---|
| `make w3-test` | **W3_BROWSER PASS** |
| `make w2-test` | **W2_HISTORY PASS** |
| `make w1-test` | **W1_MESSAGING PASS** |
| `make w0-routes-test` | **W0_ROUTES PASS** |
| `make g3h-test` | **G3H SMOKE PASS** |
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

No frozen S1, HOST numbering, memory profile, loader/vocabulary semantics or
task-stack capacity changed. The existing NDJSON persistence format is
unchanged.

## 5. Remaining limitations

- **Real-browser execution is unverified** (see §3).
- The browser loads the complete small history through W2 pages on initial
  load; W2's store cap is 16,384 bytes. A larger history needs the W2 page-size
  improvement first.
- W2's page size remains 9 and concurrent history scans return controlled 503s.
- Only one inline asset is served; there is no general static-file tree,
  favicon, caching policy or asset MIME table.
- No authentication, user identity, group administration, file upload,
  WebSocket/live push or message deletion.
- NDJSON remains append-only with no compaction.
- Auto-refresh is polling (15 s), not server push.

**Recommended next user-facing milestone:** run a real-browser two-window
acceptance pass, then expose a bounded latest/older page endpoint (or bounded
page assembly) so the UI can load recent history without fetching all pages.
