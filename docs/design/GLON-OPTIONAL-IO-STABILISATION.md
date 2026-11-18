# Optional file I/O — IO-2 stabilisation report

**Branch:** `impl/optional-file-io`
**Starting commit:** `7adf05765aba40c207233cc50ac2de6b1ab5f6bf`
**Base branch:** `glon-shop-g1a` at `3bccc6e95f77c01542c4adbe88e604dec58c5d09`
**IO-3 update:** the host `--io-max-file` is now a streaming per-handle cap
(8 MiB default); the 1,024-byte whole-file cap is enforced by `file.glon`.
See `GLON-OPTIONAL-IO-STREAMING.md`.
**Scope:** audit, harden and measure the existing optional asynchronous file
I/O module. No new S1/HOST operation, parser/evaluator/grammar change, M1
scheduler change, network/seek/stat feature, or production-repository change.

---

## 1. Request lifecycle audit

Sequence audited:

```text
Glon builds frame
  -> io/take-request copies bytes out of managed Glon memory
  -> host decodes into bounded request fields
  -> host copies the whole frame into cli->request
  -> worker thread runs the POSIX operation
  -> worker writes reply frame + done (release)
  -> main observes done (acquire), joins, copies the reply
  -> io/reply rebuilds a Glon source turn and delivers fields
  -> job completion closes handles
```

Findings:

- The worker has no access to `M[]`, `r0_s1_*`, Glon closures, or GC state.
  It sees only the module's host-owned request/reply/service structs.
- `glon_io_cli_submit` copies the complete request frame before
  `pthread_create`. The request lifetime is therefore independent of Glon's
  next turn.
- The worker writes `reply`/`worker_err`, then publishes `done` with release
  semantics. `glon_io_cli_wait` observes `done` with acquire, joins, and only
  then copies the reply. No reply pointer escapes the driver.
- Request/reply IDs are checked twice: the driver carries the submitted ID and
  verifies the worker reply (`run_io_reply`); the Glon library verifies the ID
  against its single pending record and raises `SIN! 'io 'stale-reply`
  otherwise.
- Handles are `(u32 value, u32 generation)`, never pointers or file
  descriptors at the protocol boundary. Closing marks the slot closed; stale
  values fail the generation/value lookup. Reuse increments the generation.
- Cancellation sets an atomic flag, `wait` joins the worker, and any completed
  reply is discarded. Handles opened by the cancelled operation are reclaimed
  by the subsequent cleanup. There is no second reply path.
- `glon_io_cli_destroy` cancels/joins a running worker, then closes all
  remaining handles. Job completion in the CLI cleans up after every source
  file and again on an early error path.
- `glon_io_cli_submit` permits one outstanding request. The frame, chunk,
  field-count, handle and whole-file limits are explicit; malformed requests
  and worker encode failures produce driver errors, not successful replies.

No confirmed use-after-free, double-close, stale-reply acceptance or
worker/Glon-memory race was found in this audit. Passing tests and sanitizers
are evidence, not a proof of race freedom.

---

## 2. Confirmed defects and corrections

### 2.1 Whole-file limit was unsafe — corrected

The previous default `max_file = 2048` was not achievable by the Recipe 0004
path. Measurements:

- 1,100-byte input: completed.
- 1,200-byte input: **machine-level fail-stop** after the write-open reply,
  while the host still reported a normal open; the file was left unchanged.
- 1,600 / 1,800 / 2,000 / 2,048-byte inputs: also failed.

Cause: immutable-string accumulation and replacement hold the input,
replacement, and a write slice together in the 7,232-cell managed heap.
Bounded chunking does not remove that peak. The previous cap allowed a file
into the module and then produced a machine halt instead of a recoverable
`too-large` status.

Correction:

- `GLON_IO_DEFAULT_MAX_FILE = 1024`
- `GLON_IO_SAFE_MAX_FILE = 1024`
- `--io-max-file` values above 1,024 are rejected at service init, not silently
  clamped.
- Tests cover 1,022-byte input → 1,024-byte output, a 1,025-byte input
  returning `too-large`, and rejection of `--io-max-file 2048`.

This is a reduction based on the measured safe bound, not a benchmark-driven
increase. A future streaming/mutable-string design may raise it; that is not
part of this pass.

### 2.2 Host reply validation hardened — corrected

The host previously trusted the read reply field length enough to begin
emitting a decimal `mk-string` source from it. The service itself is bounded,
but a protocol/worker fault could pass a larger field. The host now rejects:

- reply status values outside the v1 status range;
- reply field counts above the protocol maximum;
- read fields above `GLON_IO_MAX_CHUNK` before source construction.

The focused C test covers malformed request frames, oversized fields and a
worker that cannot decode its request.

---

## 3. Protocol v1 contract

Request:

```text
magic:u8=0x01, op:u8, request_id:u32be, field_count:u8, field*
field = length:u32be, bytes[length]
```

Reply:

```text
magic:u8=0x81, request_id:u32be, status:u8, field_count:u8, field*
field = length:u32be, bytes[length]
```

- Operations: `open=1`, `read=2`, `write=3`, `close=4`.
- Status codes: `ok=0`, `eof=1`, `not-found=2`, `permission=3`,
  `invalid-handle=4`, `too-large=5`, `io-error=6`, `unsupported=7`,
  `busy=8`.
- All integers are fixed-width big-endian. All lengths are `u32`. No pointer,
  native `size_t`, OS file descriptor or C structure layout crosses the
  boundary. Handles are two `u32` values; OS descriptors stay host-private.
- Fields are length-delimited, so embedded `0x00` and arbitrary binary bytes
  are preserved. The acceptance suite covers `00 80 ff`.
- Request IDs are non-zero, per-session, and matched. Unknown/stale IDs,
  malformed magic, truncated packets, trailing bytes, oversized fields and
  duplicate/stale Glon replies are rejected.
- Bounds for v1: frame ≤ 8,192 bytes; chunk ≤ 400 bytes; whole-file cap
  ≤ 1,024 bytes; default handles 8, hard maximum 16.
- Version 1 is identified by the two magic bytes. A future incompatible
  protocol must use a new magic; this contract does not silently reinterpret
  v1 fields.

The portable contract is independent of POSIX. `open/read/write/close`
semantics and status meanings are the part a future Windows, WASM or embedded
backend would implement; the current POSIX `realpath`/`open`/`read`/`write`
code is not part of the wire contract.

---

## 4. Sanitizer results

- **AddressSanitizer + UndefinedBehaviorSanitizer:** focused `io-file-c-test`
  passed with default leak detection. The complete 29-case module acceptance suite
  also passed under an ASAN+UBSAN `glon` build with
  `ASAN_OPTIONS=detect_leaks=0` (the interpreter has its own long-lived
  session allocations; leak detection was disabled only for the end-to-end
  run).
- **ThreadSanitizer:** focused C test and the async progress/recipe run both
  produced no data-race report. On this WSL2 kernel TSan aborts before `main`
  with `FATAL: ThreadSanitizer: unexpected memory mapping` unless ASLR is
  disabled; the runs therefore used `setarch $(uname -m) -R`. TSan is reported
  as environment-sensitive, not as a universal gate.
- **Compiler warning:** sanitizer builds emit `-Wformat-truncation` warnings
  for path `snprintf` calls. The paths are explicitly checked against the
  destination bound before or immediately after formatting; no truncation was
  observed. This is a compiler diagnostic, not a sanitizer failure.
- `gcc -fanalyzer` on the three module sources produced no findings.

Sanitizer success is not a claim of race freedom.

---

## 5. Measurements

Measured 2026-11-18 on Linux/WSL2, x86_64, GCC, using the same source and
fixtures for both configurations. `-O0 -g` is the default build;
`-Os` uses `CFLAGS='-std=c17 -Wall -Wextra -Os'`.

| Build | Executable | text | data | bss |
|---|---:|---:|---:|---:|
| `-O0 -g` minimal | 185,216 | 93,258 | 2,208 | 621,992 |
| `-O0 -g` file | 230,936 | 115,389 | 2,352 | 621,992 |
| `-O0 -g` delta | **+45,720** | **+22,131** | **+144** | **0** |
| `-Os` minimal | 95,696 | 71,421 | 2,216 | 621,992 |
| `-Os` file | 110,136 | 85,344 | 2,296 | 621,992 |
| `-Os` delta | **+14,440** | **+13,923** | **+80** | **0** |

The original 12 KiB provisional executable budget is **not met** at `-O0 -g`
(+45,720 bytes) or by the text segment at `-Os` (+13,923 bytes); the `-Os`
total executable delta is +14,440 bytes. No size optimization was attempted.

Peak RSS (`/usr/bin/time -v`; medians/ranges are noisy):

| Run | `-O0 -g` Max RSS | `-Os` Max RSS |
|---|---:|---:|
| minimal, small in-memory probe | 1,796 KB | 1,772 KB |
| module, same probe | 2,072 KB | 1,976 KB |
| module, Recipe 0004 | 2,136 KB (1,944–2,200) | 2,168 KB (2,040–2,296) |

Worker-thread stack:

- Default `pthread_attr_getstacksize`: **8,388,608 bytes** virtual reservation.
- While a 5 s request was pending, `/proc/<pid>/task/*/status` showed **2
  threads**, each with **VmStk ≈ 136 KB** in this WSL2 environment; process RSS
  was about **2,104 KB** during that pending window.
- Request/reply buffers: two 8,192-byte driver buffers (16,384 bytes total);
  service struct 5,200 bytes including handle slots and 400-byte reply scratch.
- Max handles: default 8, hard 16. Max chunk: 400 bytes. Whole-file cap: 1,024
  bytes.

---

## 6. Constrained-device suitability

Reusable without POSIX threads:

- the v1 framing, field lengths, status codes and request-ID matching;
- the Glon `file.glon` state machine and its one-outstanding/job model;
- generation-bearing handle tokens and the capability/root policy shape;
- the host service's bounded buffers and handle table.

Not reusable as-is:

- `pthread_create`/`pthread_join` and the host worker-thread completion model;
- POSIX `realpath`, `open`, `read`, `write`, `close`, `O_NOFOLLOW`, `errno`;
- assumptions about a conventional filesystem and a writable root;
- the CLI's `calloc`ed driver and `glon-lib` discovery;
- the current 1,024-byte whole-file convenience, which assumes a managed heap
  large enough for the input, replacement and write slice.

For an embedded profile, retain the protocol and Glon state machine, replace
the worker with a host-owned poll/event queue or a bounded non-blocking task,
replace the POSIX backend with a static VFS/block backend, make the handle
table size 1 where appropriate, and use static request/reply buffers. No such
backend is implemented here.

For Windows, keep the protocol and replace the POSIX path/open layer with a
Win32 backend plus UTF-8-to-UTF-16 path conversion. For browser/WASM, there is
no ambient filesystem: the host must mediate user-selected files or virtual
names, and the async mechanism must use the host event loop rather than
pthreads. Neither backend is implemented here.

---

## 7. Regression and acceptance

- `make io-file-test`: **C protocol/driver test PASS**, then **30/30**
  cases (29 module cases plus the minimal-build regression).
- `make glon-smoke`: PASS.
- `make glon-smoke MODULES=file`: PASS.
- `./check-frozen-s1.sh`: PASS.
- GC worklist overflow test: PASS.
- Primer check: PASS.
- `make wasm-test`: PASS; `make wasm-g1a-test`: PASS.
- Non-simulation native runner: **948 checks, all tests passed**.
- Full `./s1`: the long traffic/Newell/OVM simulation tests still stall at
  `O0` with a `[dump]`; the same traffic stall reproduces on the untouched
  base worktree. This is unchanged and unrelated to the file module. No
  simulation was modified.

The original Recipe 0004 behaviour remains correct on all acceptance fixtures,
including binary preservation and short read/write loops.

---

## 8. Recommendation

**Ready for isolated integration testing under the documented 1,024-byte
whole-file limit, with no production deployment.** Further stabilisation is
required before production integration for:

- a streamed or mutable-string file path if files above 1 KiB are required;
- Windows/WASM/embedded backends and a threadless async host;
- evidence that the long simulation suites complete in the target CI
  environment;
- a size-budget decision at `-O0 -g` or `-Os`;
- a hardened path policy if concurrent hostile filesystem mutation is in
  scope.

No known critical defect remains within the current POSIX proof-of-concept
boundary, but the module is not claimed to be race-free or production-ready.
