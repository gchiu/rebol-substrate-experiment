# GLON-WORLDS-W3.1 — Automatic Message Updates

Date: 2026-11-27
Branch: `integration/glon-worlds-w3.1`
Base: W3 `9d683a03e31b9bc05c5cface8f10fc5d8af05a9f`
Base branch: `integration/glon-worlds-w3`
Frozen S1: `f90496c26dc45c7a387d8fc8639cd2781507d3d2`
Status: accepted locally; committed after the tests below. Not pushed, merged,
rebased or deployed.

**Result:** the W3 browser client now polls the existing W2 history API about
every 2 seconds and appends messages sent from another browser window without a
manual Refresh. Manual Refresh remains available as a fallback.

## 1. Browser polling behaviour

- `POLL_MS = 2000`; the next poll is scheduled with `setTimeout` only after the
  previous poll finishes, so polls cannot overlap.
- Each poll starts at the newest displayed ID (`newestId()`) and requests
  `GET /api/messages?group=general&after=<newest>&limit=9`, following W2
  `next_after`/`has_more` only when more than one page is new.
- New records are added to the existing `Map` keyed by message ID, so repeated
  responses, an overlapping send response or a partial page never duplicate a
  message. Rendering always iterates IDs in ascending order.
- The full history is fetched only by the initial load and by manual Refresh;
  automatic polls never restart at `after=0`.
- A `pollInFlight` guard plus the `busy` guard prevents automatic polling from
  overlapping another poll or a manual load/send.
- While the page is hidden, the pending timer is cleared and no new poll is
  scheduled. When it becomes visible, polling resumes immediately and catches
  up from the newest displayed ID.
- HTTP 503 (history busy) and transient fetch/network failures are retried with
  exponential backoff (`2 s`, `4 s`, `8 s`, …) capped at
  `MAX_BACKOFF_MS = 30000`. A success resets the backoff. A status message
  reports that live updates are paused and retrying.
- If a user is reading older history, an automatic update does not move the
  visible page. If the view is already at the newest page, the window shifts to
  the latest page so new messages appear.
- Manual **Refresh** remains and now also resets polling backoff. Send also
  resets polling backoff after a successful post.
- Message rendering is still `textContent`-only; no `innerHTML`. No WebSocket,
  S1, memory-profile or server storage change was introduced.

## 2. Verification

`tests/glon-worlds-w3/browser_api.py` (run by `make w3-test`) now also checks:

- the served asset contains the 2-second polling, visibility, in-flight guard,
  503 handling and bounded-backoff logic, and no longer uses `setInterval`;
- two browser-equivalent polling clients load history once, then exchange a
  message and replies using only `after=<latest displayed id>`;
- the receiver obtains the other client's message without calling Refresh or
  walking the full history;
- polling calls never use an `after` value older than the newest ID present
  when polling began;
- a second poll with no new data appends nothing;
- a client that did not poll while several messages arrived catches up in
  ascending ID order in one poll;
- both polling clients agree on message IDs and text.

Output: **`W3_BROWSER PASS`**.

### Real browser vs automated evidence

- **Verified:** native asset serving, safe-rendering source constraints, and two
  independent browser-equivalent HTTPS clients exchanging messages through the
  automatic-poll protocol.
- **Not verified:** execution in a real browser engine. No browser (Chromium,
  Firefox, WebKit) or browser automation runtime is installed in this
  environment. Actual DOM visibility events, timers and layout still require a
  real-browser pass.

## 3. Preservation test evidence

| Test | Result |
|---|---|
| `make w3-test` | **W3_BROWSER PASS** |
| `make w2-test` | **W2_HISTORY PASS** |
| `make w1-test` | **W1_MESSAGING PASS** |
| `make w0-routes-test` | **W0_ROUTES PASS** |
| `make test` | **exit 0** (S1 suite, core checks, `global-ctx-hash-test`, primer check) |
| `./check-frozen-s1.sh` | **PASS** at `f90496c…` |

No frozen S1, HOST numbering, memory profile, loader/vocabulary semantics or
task-stack capacity changed. The W2 NDJSON format and API contract are
unchanged.

## 4. Remaining limitations

- Real-browser execution of timers/visibility is unverified (see §2).
- Polling is request/response, not server push. The interval is a client
  constant; there is no adaptive interval beyond failure backoff.
- The browser still loads the complete small W2 history on initial load.
  W2's store cap remains 16,384 bytes.
- W2's page size remains 9 and concurrent history scans still return controlled
  503s; automatic polling treats those as transient.
- No authentication, user identity, WebSocket/live push or message deletion.
