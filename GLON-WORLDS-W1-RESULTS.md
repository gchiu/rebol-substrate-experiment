# GLON-WORLDS-W1 — Reliable Messaging

Date: 2026-11-27
Branch: `integration/glon-worlds-w1`
Worktree: `/home/developer/projects/glon-worlds-w1`
Base: W0 `3f35863ec7c2bb51d2fd764b46d91390926b1c60`
Unified core: C2 (`4dcae2a`)
Frozen S1: `f90496c26dc45c7a387d8fc8639cd2781507d3d2`
Status: accepted locally; committed after the tests below. Not pushed, merged,
rebased or deployed. W2 was not started.

**Result:** ordinary G3H chat messages are reliable in HTTPS task mode. A
1,024-byte message — including a 1,024-escaped-byte worst case — is accepted,
retrieved with exact text and unchanged HTML escaping, and survives restart.
The old per-character recursion is gone, the HTTP bridge no longer turns a
request that is too large for parsed source into a server-wide fail-stop, and
the G3H NDJSON store uses the streaming file API rather than the 1,024-byte
whole-file convenience cap.

## 1. Changes

| file | change |
|---|---|
| `modules/web.glon` | Replaced recursive `wp/find-html-special` + `wp/escape-html-loop` with a logarithmic `do`-driver over `s/scan-set`. A 251-byte safe-byte set finds the next HTML-special byte. Output is accumulated in a 512-byte buffer before `s/+`, avoiding quadratic copying. Escapes are unchanged: `& < > " '` and all other bytes, including UTF-8, pass through exactly as before. |
| `glon_http.c` | Long request fields are emitted as right-associative `s/+` chains of `mk-string` blocks of at most 200 bytes, so the generated spawn/dispatch source never contains a >511-element block. A spawn source that still cannot be expressed is a controlled per-request 413/503, not `http.fatal`. No parser, loader, source-buffer or profile capacity was changed. |
| `examples/glon-worlds-g3b/server.glon` | Store read/write now uses the existing streaming `io/open`, `io/read`, `stream/write-all` and `io/close` API, preserving the same NDJSON format. This avoids the 1,024-byte `file/read`/`file/write` convenience cap. `gw/max-store` is now 16,384 bytes. |
| `tests/glon-worlds-w1/messaging.py` | New HTTPS task-mode acceptance test. |
| `Makefile` | New `make w1-test` target using `server-https` (3 × 3,400 task stacks). |

No frozen S1, C2 profile, language, task-stack or memory-map change was made.
The escaping worker is not re-entrant, but HTTP handlers never yield inside it,
so task interleaving cannot occur.

## 2. Measured limits

- Maximum accepted message: **1,024 bytes**. `1,025` bytes returns `400
  "text too long"`.
- A 1,024-byte message made entirely of `<` is accepted and escaped to 4,096
  bytes without a return-stack fail-stop.
- The test store held nine messages (including the maximum and escaped
  maximum) in **9,199 bytes**, and all nine survived restart.
- The app limit is bytes (`s/length`), not Unicode code points.
- The whole-file convenience cap remains 1,024 bytes for other applications;
  G3H no longer uses it.

## 3. Acceptance evidence

| Test | Result |
|---|---|
| `make w1-test` | **W1_MESSAGING PASS**: plain, HTML metacharacters, Unicode, 1,024-byte maximum, 1,024 escaped bytes, overlapping requests, malformed/over-limit/oversized controlled errors, store settled before restart, restart persistence, no machine halt |
| `make w0-routes-test` | **W0_ROUTES PASS**: all G3H routes, SQLite/NDJSON persistence, task mode |
| `make g3h-test` | **G3H SMOKE PASS** (direct mode) |
| `make h3e-test` | **H3E ACCEPTANCE PASS** |
| `make h3c-test` | **H3C ACCEPTANCE PASS** |
| `make json-test` | **JSON_TESTS PASS** |
| `make json-j1-test` | **JSON_J1_TESTS PASS (73 checks)** |
| `make json-j2-test` | **JSON_J2_TESTS PASS (96 checks)** |
| `make json-j2-g3h-test` | **JSON_J2_G3H PASS** (task mode, malformed/escape-heavy bodies) |
| `make sqlite-test` | All SQLite service tests passed |
| `make sec-test` | All security primitive tests passed |
| `make https-g3a-test` | **HTTPS_G3A_ACCEPTANCE PASS** |
| `./check-frozen-s1.sh` | **PASS** at `f90496c…` |
| `make arch-check` / `./arch-check` | **PASS** |
| `make profile-check` | **PASS (8 profiles)** |
| `make profile-reject-check` | **PASS (15 cases)** |
| `make profile-common-test` | **PASS (8 profiles)** |
| `make build-deps-check` | **PASS (30 checks)** |
| `make global-ctx-hash-test` | **PASS on all 8 profiles** |
| `make m1-large-test MEM_PROFILE=large` | **PASS**, `M1_LARGE failures=0` |

The W1 HTTPS test uses the existing G3H app, `server-https`, three 3,400-cell
task stacks, a temporary `--io-root`, and a 30-second request deadline. The
older recursive escaper would exhaust the task stack on ordinary 1,024-byte
messages; the current worker does not.

## 4. Remaining defects and W2 candidate

- **Historical escaping copies.** `examples/https-g3/webparse.glon` and
  `demo/shop/https-examples/g3a/web.glon` still contain the old recursive
  implementation. They are not loaded by the maintained G3H app, which uses
  `modules/web.glon`.
- **Large-body bridge architecture.** The bridge now safely supports the
  application's existing limits, but arbitrary large JSON documents still
  need a non-source-embedded transport. The current chunking avoids a parser
  capacity increase; it is not a general streaming request-body design.
- **Store and response pagination.** G3H still returns one message list and is
  bounded by `json/max-items` (32) per response and `gw/max-store` (16,384
  bytes). A larger store needs pagination rather than a larger response.
- **JSON J2 limitations remain:** heavily escaped JSON is slow; encode/decode
  nesting limits remain asymmetric; fatal task fail-stops still stop the whole
  server.
- **No auth/session/CSRF/public deployment work** was started.

**Proposed next application milestone (W2):** paginated message retrieval and a
bounded streaming request-body transport, without changing S1, C2 profiles or
task-stack capacities.
