# Optional asynchronous file I/O — implementation report

**Status:** isolated proof of concept, implemented and locally verified.
**Branch:** `impl/optional-file-io`.
**Base:** `glon-shop-g1a` at `3bccc6e95f77c01542c4adbe88e604dec58c5d09`.
**Design:** [`GLON-OPTIONAL-IO.md`](GLON-OPTIONAL-IO.md), Approach B
(host-mediated request/reply).
**Scope:** POSIX native `glon` only. This is not a general I/O, plugin, WASM or
Windows framework.

The frozen S1 substrate, the R0 parser/evaluator, the seven primitives and the
existing seven HOST IDs are unchanged. `./check-frozen-s1.sh` passes.

---

## 1. Build

```sh
make glon                    # unchanged minimal CLI
make glon MODULES=file       # optional host module + file.glon
make io-file-test            # builds both and runs the acceptance suite
```

`MODULES` is compile-time only. The minimal build does not link
`glon_io_file*.c`, does not load `glon-lib/file.glon`, and has no `glon_io`
symbols (`nm` count 0). Unknown module names fail at build time.

`make glon MODULES=file` links `glon_io_file.c`,
`glon_io_file_posix.c`, `glon_io_file_cli.c` with `-pthread`, defines
`GLON_MODULE_FILE`, and stages `modules/file.glon` as `glon-lib/file.glon`.
The module build also loads `glon-lib/strings-ext.glon` for `s/slice`; the
minimal build still loads only prelude and strings.

Runtime capability is separate from compilation. The host defaults to deny-all:

```text
--io-root DIR              root for all logical names
--allow-file-read          permit read access under the root
--allow-file-write         permit write access under the root
--io-max-handles N         default 8, hard maximum 16
--io-max-file BYTES        default 2048
--io-delay-ms N            test-only worker delay
--io-short N               test-only cap on one read/write
--io-progress FILE         run an independent Glon job while a request is pending
--io-report                report replies, handles and cleanup
--io-cancel-before-reply   test the cancellation path
```

Names are logical relative names. Absolute paths, `..`, `.`, empty
components, backslashes, drive/UNC-like colons and embedded NUL are rejected.
Existing targets and write parents are canonicalised with `realpath` and must
remain under `--io-root`; `O_NOFOLLOW` rejects a final symlink. This is a
single-process proof-of-concept policy, not a claim that every TOCTOU race is
eliminated against a hostile concurrent process.

---

## 2. Protocol

The driver and host service use bounded, length-framed messages. All integers
are big-endian.

```text
request = magic:u8=0x01, op:u8, request_id:u32, field_count:u8, field*
field   = length:u32, bytes[length]

reply   = magic:u8=0x81, request_id:u32, status:u8, field_count:u8, field*
```

Operations: `open=1`, `read=2`, `write=3`, `close=4`.

Statuses: `ok`, `eof`, `not-found`, `permission`, `invalid-handle`,
`too-large`, `io-error`, `unsupported`, `busy`.

`open` sends `(name, mode)` and returns `(handle value, generation)`.
`read` sends `(handle value, handle generation, max_bytes)` and returns bytes
or `eof`. `write` sends `(handle value, handle generation, bytes)` and returns
the number written; short writes are normal. `close` sends the handle.
Unknown or stale request IDs, bad magic, malformed fields and trailing bytes
are host protocol/job failures, not recoverable `SIN!` values.

### Request construction stays in Glon

`modules/file.glon` builds the actual binary frame with ordinary `mk-string`,
`s/+` and `s/slice`. It has no new primitive. The host's `io/take-request`
result is copied out as a bounded STRING! frame and decoded by C. The host never
sees the words `"web"` or `"rebol"`.

### Reply handling stays in Glon

The host calls:

```text
io/reply <id> '<status> <v1> <v2>
```

For `open`, `v1`/`v2` are handle value/generation. For `read`, `v1` is the
returned byte string. For `write`, `v1` is the byte count. For `close`, both
are ignored. Recoverable statuses are converted by the Glon library into
`SIN! 'io '<status> <arg>` in the failure closure.

The reply bytes are emitted as a decimal `mk-string [ ... ]` block. The parser's
512-form block limit and the session source-size limit make a **400-byte**
chunk the safe bound. Whole-file reads loop over that bound. This is why the
implementation uses `max_chunk = 400`, not the design's provisional 4 KiB.

### One outstanding request

Phase 1 allows one request per job. A second request raises
`SIN! 'io 'busy`. The native CLI treats one source file as one host-dispatched
job and cleans up its handles at the end of that job.

---

## 3. Asynchronous execution

`glon_io_file_cli.c` owns one worker thread per outstanding request. The main
Glon thread submits the frame and never performs the filesystem call. It can
run another independent host-dispatched Glon job while the worker is pending;
only after that does it wait for the reply. This is host-level concurrency — it
is **not** M1 cooperative task suspension, and no blocked task state was added.

Evidence:

```sh
./glon --io-root "$root" --allow-file-read --allow-file-write \
  --io-delay-ms 150 --io-progress tests/io-file/fixtures/progress.glon \
  --io-report modules/recipe-0004.glon
```

Observed output:

```text
PROGRESS_DONE
RECIPE_DONE
glon: io: progress job completed while request 1 was pending
glon: io: reply 1 status 0
...
```

The progress job runs after request 1 is submitted and before reply 1. With a
150 ms per-request delay, the full recipe took about 1.18 s and the file was
`rebol site`.

Cancellation is cooperative: a worker that is already inside a blocking
syscall is not interrupted. The driver discards its result and joins it; the
limitation is explicit.

---

## 4. Glon API

```glon
io/open  name mode success failure
io/read  handle max success failure
io/write handle bytes success failure
io/close handle success failure

file/read  name success failure
file/write name bytes success failure
```

Handles are `[value generation]` blocks; callers normally use the whole-file
helpers. All callbacks take one value. Pass callback **values** with `:success`
and `:failure`; a bare callback word would be invoked by the evaluator.

`judge` cannot span an asynchronous reply. Each reply is a separate run. A
failure raised by a callback is reported by the host as an uncaught SIN!, but
an enclosing `judge` in the original caller has already returned. This is the
documented phase-1 limitation.

---

## 5. Recipe 0004

`modules/recipe-0004.glon` reads `doc.txt`, calls the existing Glon
`replace-first` implementation (same algorithm as the in-memory Challenge 3
listing), and writes the result back. The host only opens/reads/writes/closes.

The acceptance fixture has 34 non-comment Glon source lines, including the
first-occurrence replacement implementation; the file-job orchestration itself
is 13 lines. The host has no replacement code.

Every acceptance case reads the resulting file bytes independently in Python:

- beginning, middle, end;
- multiple matches: first only;
- no match;
- empty file;
- binary bytes including `0x00`, `0x80`, `0xff`;
- exact bytes outside the replacement;
- multi-chunk read/write;
- forced short reads/writes;
- missing file;
- read permission denied;
- write permission denied, with the file unchanged;
- invalid handle;
- maximum handles;
- maximum file;
- path traversal, absolute path and symlink escape;
- malformed request frame (protocol failure, not SIN!);
- job-end cleanup of an open handle;
- cancellation before reply;
- progress while request 1 is pending;
- minimal build has no file vocabulary;
- the six original in-memory replacement cases.

`make io-file-test` result: **26 passed, 0 failed**.

---

## 6. Regression and verification

Measured on 2026-11-18, Linux, default `CFLAGS=-std=c17 -Wall -Wextra -O0 -g`.

| Check | Result |
|---|---|
| `./check-frozen-s1.sh` | PASS |
| `make gc-worklist-overflow-test` + run | PASS |
| `python3 demo/shop/build-primer.py --check` | PASS |
| `make glon-smoke` (minimal) | PASS |
| `make glon-smoke MODULES=file` | PASS |
| `make io-file-test` | 25/25 |
| non-simulation native test runner | 948 checks, `all tests passed` |
| `make wasm-test` | PASS |
| `make wasm-g1a-test` | PASS |

The full `./s1` run did not complete within our test timeouts (up to 10
minutes): the long traffic/Newell/OVM simulation tests print a `[dump]` and
stall at `O0`. The same traffic stall reproduces on the untouched base
`3bccc6e` binary.
A runner omitting only those four simulation suites finishes with all 948
remaining checks passing and zero failures. This is a pre-existing baseline
issue, not caused by the optional module.

WASM checks are baseline checks only; the file module itself is native POSIX
and is not compiled into WASM.

---

## 7. Measurements

Measured, not estimates:

| Artifact | Minimal | `MODULES=file` | Delta |
|---|---:|---:|---:|
| executable bytes | 185,216 | 230,880 | +45,664 |
| `size` text | 93,258 | 115,158 | +21,900 |
| `size` data | 2,208 | 2,352 | +144 |
| `size` bss | 621,992 | 621,992 | 0 |

Peak RSS (`/usr/bin/time -v`):

| Run | Max RSS | Wall |
|---|---:|---:|
| minimal, in-memory fixture | 1,604 KB | 0.02 s |
| module, in-memory fixture | 1,648 KB | 0.06 s |
| module, Recipe 0004 | 1,944 KB | 0.11 s |
| module, recipe + 150 ms/request + progress | ~1,900 KB | 1.18 s |

The executable delta exceeds the design's provisional 12 KiB budget under the
default `-O0 -g` build. The module adds pthread/libc code and debug symbols;
no size optimization was attempted.

Resource bounds:

| Item | Value |
|---|---:|
| open handles | default 8, hard 16 (session/job scope in phase 1) |
| one read/write chunk | 400 bytes |
| whole-file default cap | 2,048 bytes |
| request frame bound | 8,192 bytes |
| reply frame bound | 8,192 bytes |
| service struct (handles + reply scratch) | 5,200 bytes |
| driver request + reply buffers | 16,384 bytes |

The whole-file cap was reduced from the design's provisional 4,096 to 2,048
after measurement: immutable-string accumulation holds the previous and new
strings during a copy, and a 4,096-byte file exhausted the 7,232-cell managed
heap before completion. This is evidence-driven, not a silent semantic change.

Host implementation: 742 non-comment C lines across the module header/source
files, plus the conditional native CLI driver. Glon library: 159 non-comment
lines. Tests: 197-line acceptance harness plus fixtures.

---

## 8. Limitations and non-goals

- POSIX native CLI only. No Windows or WASM file module in this phase.
- One outstanding request per job. No M1 blocked-task state, no scheduler
  extension, and no claim that host concurrency is task suspension.
- `judge` cannot span an asynchronous reply; callback failures are reported at
  the reply turn.
- Cancellation cannot interrupt an in-flight blocking syscall; it discards the
  result after the worker returns.
- Handles are session/job-owned, not task-owned. The default bound is 8, hard
  16; per-task ownership is deferred.
- No seek, truncate, stat, metadata, random access, atomic replace or
  transactional write. A failed short write can leave partial contents.
- Path confinement is canonicalisation plus `O_NOFOLLOW`; it is not an
  openat2/`RESOLVE_BENEATH` hardening claim against concurrent attackers.
- Whole-file reads are bounded by the managed heap and the 400-byte protocol
  chunk. Larger streaming is a phase-2 API, not implemented here.
- No arbitrary C calls, no new S1 HOST IDs, no evaluator/parser changes, no
  dynamic plugin loading.
- The minimal build intentionally has no file vocabulary; the same source fails
  cleanly rather than falling back to a host-side implementation.
