# Glon G3C — security foundations for authenticated Glon Worlds

Date: 2026-11-24

Status: implemented security substrate and HTTP integration; authentication
policy is not implemented. This is not a claim of a production authentication
system or of authenticated Glon Worlds messaging.

Baseline worktree: `/mnt/d/repos/glon-https`, branch
`experiment/glon-https-g3b`, starting commit
`239edbfc98ae14ccb5e1c87318b7217c99f2c2f9`. No additional worktree was created.
The G3B demo and its loopback-only, unauthenticated messaging remain in place.

## Phase 1 — verified baseline

At the start of G3C the worktree was clean on the branch above.

| Check | Result |
|---|---|
| G3B acceptance (`tests/glon-worlds-g3b/acceptance.py`) | PASS, 5.86 s, 20,284 KiB peak RSS |
| G3B headless browser (`tests/glon-worlds-g3b/browser_check.py`) | PASS, 3.88 s, 135,352 KiB peak RSS |
| Frozen S1 (`./check-frozen-s1.sh`) | PASS, frozen at `f90496c26dc45c7a387d8fc8639cd2781507d3d2` |
| Relevant HTTPS suites G1/G2/G3 | PASS with the module-enabled binary |
| Full `make test` | Not completed; exceeded five minutes |

### Why `make test` exceeded five minutes

`make test` starts `./s1`, then the GC overflow test and a Python check. The
`./s1` output reaches the **R0-S1 IDM traffic simulator** and then spends most
of its time there. A timed run observed:

```text
 57.08s R0-S1 IDM traffic simulator (fixed-point, ring road)
 57.08s   ok: 7: after 200 ticks the ring is in uniform flow (min-v == max-v)
153.09s   ok: 8: uniform flow has no collisions (min-gap 3500)
```

Test 8 took about 96 seconds; test 9 then performs a 400-step disturbance loop
with several persistent evaluations per step, followed by further 600-step
runs. This is a pre-existing performance limitation in the traffic test/demo
path, not a G3C regression: the G3C changes add optional modules and do not
touch `s1.c`, `s1.h`, `r0_s1_runtime.c`, `r0_s1_traffic_tests.c`, or the traffic
demo. The historical NDJSON membership benchmark was not optimised.

The full suite was not run to completion within this milestone; no claim of a
full-suite pass is made. Focused suites and the frozen check pass.

## Phase 2/3 — implemented vocabulary

A new optional module is selected with:

```sh
make glon MODULES=sec OPENSSL_CFLAGS=... OPENSSL_LIBS=...
```

`sec` is additive with `file` and/or `http`, for example
`MODULES=file,http,sec`. The minimal build (`MODULES=`) compiles and links no
security or OpenSSL code. The Glon library is staged as `glon-lib/sec.glon` and
loaded automatically by a `MODULES=sec` binary.

All names are general-purpose. There are no native names for Glon Worlds,
users, login, sessions or chat.

### Glon API

All operations are asynchronous through the existing host request/reply
pattern. Only one `sec` request may be outstanding. A success callback gets the
result; an operation failure calls the failure closure with
`SIN! 'sec 'error <message>`. A malformed host protocol or a callback failure is
a host pump failure.

| Word | Result | Bounds |
|---|---|---|
| `sec/random n success failure` | `n` secure random bytes | `1 <= n <= 64` |
| `sec/sha256 bytes success failure` | 32-byte SHA-256 | input <= 1024 bytes |
| `sec/scrypt password salt n r p dklen success failure` | raw derived bytes | password <= 1024; salt 1..64; N power of two 1024..65536; r 1..16; p 1..4; dklen 16..64 |
| `sec/ct-eq a b success failure` | integer `1`/`0` | each input <= 1024 bytes |
| `sec/time success failure` | Unix epoch seconds as integer | none |

`sec/scrypt` uses OpenSSL `EVP_PBE_scrypt` and is byte-compatible with Python
`hashlib.scrypt` for the same password, salt, N, r, p and dklen. The tested
vector is:

```text
password = "correct horse battery staple"
salt     = "0123456789abcdef"
N=16384 r=8 p=1 dklen=32
hash     = b632b4ded46f123a8270fc2682d75d324823957ae4f14fdbf6b22f7de04c2827
```

`sec/ct-eq` uses OpenSSL `CRYPTO_memcmp`; unequal lengths return zero without
byte comparison. `sec/random` uses `RAND_bytes`. `sec/sha256` uses
`EVP_Digest(..., EVP_sha256(), ...)`.

### Host boundary

- `glon_sec.c` owns the OpenSSL calls, parameter bounds and fixed buffers.
- The host copies request fields into a fixed `glon_sec_request` (six fields,
  1024 bytes each) and cleanses the request and outputs after use.
- Results are returned as bounded `mk-string` values; the Glon module builds the
  callback and queue state.
- `glon.c` drains the queue after a source file during CLI startup. When HTTP
  is also enabled, a **pre-response pump** runs before `http/take-response`, so
  a handler can use `sec` and still produce its response for the current
  request. The file pump remains post-response as before.
- `http/current` stays valid through the pre-response pump and is cleared by
  the host after the response. This is required for callback response
  generation and preserves stale-request rejection afterward.

### Failure, memory and portability

- Invalid parameters are recoverable operation failures (`'sec 'error`), not
  partial results.
- Host protocol/parse errors and callback failures fail the pump closed; the
  HTTP host sends an error response and stops that request.
- Secrets are not logged. Fixed host buffers are cleansed after use.
  Managed Glon strings still hold password/hash/token bytes; Glon has no
  general secure-wipe operation for managed strings. This is documented rather
  than hidden.
- Scrypt's 64 MiB `maxmem` is host-C memory, not Glon managed-heap capacity.
  The Glon result itself is at most 64 bytes.
- OpenSSL 3 is the only non-standard development dependency, as it already is
  for the optional HTTPS module. The API has no threads and no OS-specific
  calls beyond portable `time()`. Minimal and file-only builds do not compile
  or link it.

### HTTP additions relevant to authentication

These are general HTTP changes, not chat-specific:

- `http/peer request` returns the CivetWeb transport remote address. It does
  **not** trust `X-Forwarded-For`. It is the input needed for per-client login
  throttling, but no throttling policy is implemented.
- Response header names/values containing control bytes (including CR/LF) are
  rejected by the host. This prevents header injection when an application uses
  `http/respond-with` for `Set-Cookie` or other headers.
- The existing `http/respond-with` header block is sufficient to emit a
  `Set-Cookie`; no cookie builder or cookie policy is added.

CORS, cookies, sessions and authentication remain application policy. The
existing browser client is same-origin (`API = ""`), and no CORS is added.

## Implemented versus not implemented

### Implemented and tested

- CSPRNG bytes, SHA-256, scrypt, constant-time equality, wall-clock seconds.
- A documented, bounded, optional host vocabulary (`MODULES=sec`).
- HTTP peer address access.
- HTTP response-header control-byte rejection.
- Pre-response security callbacks for handlers.
- Primitive-level password rejection: a wrong password derives a different
  scrypt key; wrong hashes fail `sec/ct-eq`.
- Random token non-repetition and invalid-length rejection.
- Malformed/oversized security inputs and existing HTTP body limits.
- G3B browser read path against the unchanged Glon Worlds client.
- G1/G2/G3 HTTP regressions and frozen S1.

### Not implemented

The following Python-server security properties are **not** implemented as a
working Glon authentication service:

- admin-controlled account provisioning (`create_user`, password reset,
  status changes);
- a server-side session/token table;
- hashed session-token storage;
- session expiry and revocation;
- login/logout/login-token endpoints and cookies;
- CSRF generation/verification for POST;
- login throttling using `http/peer`;
- server-derived author identity for authenticated messaging.

The G3B message endpoint remains a loopback-only, explicitly unauthenticated
demonstration with its fixed `Glon Demo` author. It must not be exposed as a
public service or presented as authenticated messaging.

A complete login flow was not built because the smallest safe policy requires
account storage/provisioning, a transactional session store, token lifecycle,
CSRF cookie semantics and throttling decisions. The C substrate is now present,
but the application policy and storage are not. This is a deliberate stop at a
tested reusable subset, not a claim that the flow exists.

## Tests and results

Run from `/mnt/d/repos/glon-https`.

```sh
make sec-test OPENSSL_CFLAGS=... OPENSSL_LIBS=...
python3 tests/security-g3c/glon_sec_test.py
python3 tests/security-g3c/http_sec_test.py
python3 tests/glon-worlds-g3b/acceptance.py
python3 tests/glon-worlds-g3b/browser_check.py
./check-frozen-s1.sh
```

Observed focused results:

```text
G3C security primitives
  ok: SHA-256(abc) matches the standard vector
  ok: scrypt matches Python hashlib (N=16384 r=8 p=1)
  ok: wrong password derives a different scrypt key
  ok: ct-eq accepts equal bytes
  ok: ct-eq rejects different bytes
  ok: ct-eq rejects different lengths
  ok: wall clock returns epoch seconds
  ok: random tokens are non-repeating at 16 bytes
  ok: zero-length random is rejected
  ok: oversized random is rejected
  ok: invalid scrypt N is rejected
all security primitive tests passed
PASS Glon sec SHA-256/scrypt/random/ct-eq/time
PASS HTTP sec pre-pump SHA-256/time
PASS http/peer reports the transport loopback address
PASS response header control bytes are rejected
```

G3B and the relevant HTTPS suites remained green with the module-enabled binary:

```text
PASS groups/session/read
PASS validation and escaped HTML
PASS ordering, pagination, unknown group
PASS restart persistence and next id
PASS loopback-only listener
PASS Glon Worlds database and client unchanged
PASS existing chat HTML/JS rendered a Glon message
PASS same-origin API groups/messages/session all returned 200
PASS unauthenticated browser is read-only
```

The focused tests distinguish implemented primitive behaviour from future
authentication requirements. No test claims expiry, revocation, CSRF, author
spoofing protection or throttling is implemented.

## Design decision

The security layer is a **small general host capability**, not a chat/auth
framework:

- C supplies audited primitive operations through OpenSSL.
- Glon supplies sequencing and policy.
- Applications supply accounts, sessions, CSRF, throttling and messaging rules.
- The optional module preserves the minimal build and cross-platform host model.
- No frozen S1 file, memory map, native chat route, Glon Worlds file, SQLite
  database or browser asset is changed.

The next safe milestone is an authentication application that uses these
primitives, with durable account/session storage, expiry/revocation, CSRF,
`http/peer` throttling and tests before any authenticated endpoint is exposed.
