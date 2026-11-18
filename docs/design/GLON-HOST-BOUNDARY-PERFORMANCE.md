# IO-4 — Host-boundary performance investigation

**Status:** profiling/investigation only. No production host ABI, S1, HOST,
parser, evaluator, GC, M1, IO-3 or MASM-1 baseline change.
**Branch:** `experiment/io4-host-boundary`
**Base:** `fac9b5769a2a20007b97d78d937e9e01bb11bf7b` (MASM-1)
**Reference:** IO-3 `72ff1ff`; HOST investigation `43600bb`.

The question is where the remaining end-to-end file-streaming time goes after
MASM removed most of the substring-search cost. The short answer is:

> **The dominant remaining cost is the Glon callback/state-machine turn per
> 400-byte request, not textual reply parsing or copy generation.** For a
> MASM-accelerated 1 MiB no-match stream, about 89% of profiled wall time is
> inside the reply callback run; host wait/poll is second at about 9%.

Direct value transfer would remove a small source-generation/parse/loader cost,
but would not remove the dominant per-reply evaluator work. It would only
become a large win if it also enabled materially larger chunks. The smallest
justified host improvement is a separate, narrow experiment: replace the
worker's 1 ms polling wait with an event/condition completion, then test larger
payload chunks through a documented managed-value transfer.

---

## 1. Complete data path

One 400-byte file chunk currently follows this path:

```text
Glon callback decides to read
  -> io/read builds structured request [id op fields]
  -> native driver io/take-request returns the block
  -> C take_request encodes the v1 request frame
  -> glon_io_cli_submit copies the frame into host driver storage
  -> worker thread runs POSIX read
  -> POSIX bytes are copied into service.reply_data
  -> glon_io_encode_reply copies them into cli.reply
  -> glon_io_cli_wait copies cli.reply into the driver's rep buffer
  -> C run_io_reply builds a decimal Glon source expression
  -> r0_s1_parse builds a transient loader block
  -> the callback's mk-string copies block bytes into a managed STRING!
  -> io/reply dispatches the callback
  -> streaming state machine combines carry/chunk, searches, slices output
  -> io/write builds a structured request
  -> take_request copies write bytes into a request frame
  -> submit copies the frame into driver storage
  -> worker performs POSIX write
```

Sources:

| Stage | Source | How many byte copies / representation changes |
|---|---|---|
| POSIX read | `glon_io_file_posix.c` read helper | kernel -> `service.reply_data` (1) |
| Worker completion | `glon_io_file_cli.c:41-67` | no byte copy; service handles |
| Reply encode | `glon_io_file.c:336-363` | reply bytes -> `cli.reply` (1) |
| Driver wait | `glon_io_file_cli.c:123-148` | reply -> `rep` (1); 1 ms polling loop |
| Reply source build | `glon.c:366-407` | bytes -> decimal ASCII source (expansion, no direct copy) |
| Parse | `r0_s1_runtime.c` parser/loader (block form limit 512) | source -> transient loader block (1 copy into cells) |
| Managed string | `jupyter/prelude.glon:26-40` `mk-string` | loader byte block -> managed STRING! (1) |
| Reply callback | `modules/file.glon:83-118`; `modules/file-stream.glon:126` | evaluator calls; string values |
| Stream combine/slice | `modules/file-stream.glon:126-164` | `s/+` and `s/slice` copies; no match retains m-1 |
| Request extract | `glon.c:261-330` | structured fields -> C request frame (1) |
| Submit | `glon_io_file_cli.c:99-121` | request frame -> driver request buffer (1) |
| POSIX write | `glon_io_file_posix.c` write helper | request bytes -> kernel (1) |

### Copy/representation count

For a normal no-match read/write chunk, the dominant path contains roughly:

1. kernel read -> service buffer;
2. service buffer -> reply frame;
3. reply frame -> driver reply buffer;
4. reply bytes -> decimal source text;
5. source text -> parser loader block;
6. parser block -> managed STRING!;
7. carry + chunk -> combined managed STRING!;
8. combined -> safe-prefix and carry `s/slice` strings;
9. managed write bytes -> C request frame;
10. request frame -> driver request buffer;
11. request buffer -> kernel write.

The match path replaces the safe-prefix/carry pair with prefix, replacement and
suffix slices and may issue several write requests. The host protocol itself
does not copy the bytes at the worker's decode boundary: request/reply fields
point into the driver buffers for the duration of the call.

The current host protocol uses **length-framed binary fields**; the
bytes-to-text conversion happens only in the *Glon reply injection* step, not
in the wire protocol. `io/take-request` is structured, so there is no Glon-built
request frame string in IO-3.

---

## 2. Instrumentation and method

`experiments/io4/profile_glon.c` is a copy of `glon.c` with `clock_gettime`
timers around:

- `io/take-request` parse/run and extraction;
- worker submit and wait;
- reply source construction;
- parser and evaluator run;
- loader cells consumed before reclamation;
- request/reply bytes, source bytes, host calls, GC counters.

It is a profiling binary, not a replacement CLI. Timers add overhead; totals
are compared with uninstrumented runs and stage **shares** are used for the
analysis.

`experiments/io4/c_copy.c` provides:
- `native` POSIX copy;
- `service` direct `glon_io_service` request/reply copy;
- `transport` `glon_io_cli` worker submit/wait copy with no Glon evaluator.

`experiments/io4/stream_copy.glon` is the streaming state machine with no
search. `experiments/io4/stream_copy_check.py` verifies exact bytes.

`experiments/io4/loader_lifetime.py` exercises saved blocks, closures, SIN!
values, late global bindings and managed blocks across 50 asynchronous replies.

---

## 3. Measured breakdown

### 3.1 Complete 1 MiB MASM no-match stream

Profile run (uninstrumented total was 60.75 s; instrumented total 64.03 s):

| Stage | Seconds | Share |
|---|---:|---:|
| Reply callback run (`run_io_reply` run phase) | 56.906 | ~89% |
| Worker wait/join (includes 1 ms polling) | 6.020 | ~9.4% |
| Worker submit/thread creation | 0.842 | ~1.3% |
| Request extraction (`io/take-request`) | 0.152 | ~0.24% |
| Reply source construction | 0.103 | ~0.16% |
| Parser | 0.113 | ~0.18% |
| Loader cells consumed/reclaimed | 1,303,194 cells total | not time |
| Requests | 5,250 | |
| Request bytes | 1,179,810 | |
| Reply bytes | 1,116,830 | |
| Generated source bytes | 4,361,156 | ~3.9× reply bytes |
| Host calls | 233,502 | |

The source-construction + parser costs are about **0.2 s of 64 s**. Textual
transfer is not the bottleneck.

### 3.2 64 KiB baseline vs MASM

Profile, no-match:

| Stage | Ordinary reference | MASM |
|---|---:|---:|
| total profiled | 10.32 s | 3.51 s |
| reply run | 9.372 s | 3.064 s |
| wait/join | 0.370 s | 0.369 s |
| submit | 0.049 s | 0.053 s |
| parse | 0.0066 s | 0.0062 s |
| source build | 0.0058 s | 0.0054 s |
| loader cells | 81,702 | 81,728 |
| host calls | 349,620 | 14,740 |
| requests | 334 | 334 |

The search substitution removes most of the extra `run` time; host wait,
source/parse and loader handling are essentially unchanged.

### 3.2b Matched size table

Representative measured times (WSL2 `-O0` Glon, `-O2` C; loads varied; `—`
means not measured in this pass):

| Size | Glon copy | Ordinary no-match | MASM no-match | C native | C service | C transport |
|---|---:|---:|---:|---:|---:|---:|
| 4 KiB | 0.303 s | 1.440 s | 0.642 s | ~1.0 ms | ~0.55 ms | ~38.6 ms |
| 64 KiB | 2.864 s | 15.825 s | 7.516 s | ~1.4 ms | ~1.3 ms | ~454 ms |
| 1 MiB | 38.65–58.64 s | 194.99 s | 60.75 s | ~29 ms | ~54 ms | ~6.84 s |
| 4 MiB | — | — | 283.82 s | ~42 ms | ~34 ms | ~27.2 s |

The C native/service figures are sensitive to page-cache and host load; the
transport figures are stable and show the polling/worker cost. C `c_copy` was
built at `-O0`, `-O2` and `-Os`; the 1 MiB transport times were about 6.67,
6.84 and 6.60 s respectively, while native/service stayed in the
10–54 ms range and were noisy. Glon host profiling was `-O0`; the standalone
find benchmark in `GLON-MASM-VOCABULARY-ACCELERATION.md` covers `-O0`/`-O2`/
`-Os`. Glon 64 KiB values are medians of three runs; 1 MiB copy has a measured
range under load. The 4 KiB MASM run is within startup noise of the ordinary
implementation; the search saving appears as the text grows.

### 3.3 Streaming control without search

`stream_copy.glon` (400-byte read/write, no search):

```text
1 MiB copy: 38.65 s (one low-load run); 58.64 s under a later loaded run
64 KiB: 3.192 / 2.864 / 2.841 s -> median 2.864 s
```

This isolates host transfer + evaluator state from search. The 1 MiB result
varies with host load; the 64 KiB repeated median is the more stable control.

### 3.4 C/POSIX references

`c_copy` on 1 MiB, `-O2` (representative; native/service vary with cache/page
warmth, transport is stable):

| Mode | Time |
|---|---:|
| native POSIX copy | ~29 ms |
| direct `glon_io_service` copy | ~54 ms (range ~10–54 ms across builds) |
| `glon_io_cli` worker request/reply | **~6.8 s** |

Transport is roughly 1000× slower than native copy at 400-byte request
granularity. The 1 ms polling wait dominates that difference:

```c
while (!atomic_load_explicit(&cli->done, memory_order_acquire))
    sleep_ms(1);
```

`glon_io_file_cli.c:130-131`. The profile's 6.02 s wait over 5,250 requests is
consistent: about 1.15 ms per request, mostly the 1 ms sleep plus join.

### 3.5 Other baselines

- Empty Glon closure call: **~38.4 µs/call** (100-call slope). A 1 MiB stream
  has 5,250 requests; trivial callback dispatch alone would be ~0.2 s, so the
  ~57 s reply-run cost is not merely a function-call baseline.
- The 400-byte reply source is parsed/constructed in ~0.2 s per 1 MiB; the
  same reply is passed through five host/loader/managed representations before
  the callback.

---

## 4. Allocation and copy accounting

Measured / inferred for one 1 MiB no-match stream:

- 5,250 requests (roughly 2,621 read + 2,621 write + opens/closes).
- Reply bytes: 1,116,830; generated source bytes: 4,361,156.
- Loader cells consumed per reply before reclamation: ~248 cells on average;
  total ~1.30 million cells, all transient and reclaimed.
- Managed live after collection: ~2,624–3,456 cells; heap high 40,000.
- GC collections: 2,622 (1 MiB); 10,487 (4 MiB no-match).
- Host source parsing: 0.113 s/1 MiB.
- Source construction: 0.103 s/1 MiB.
- Request extraction: 0.152 s/1 MiB.

The exact per-chunk copy count is listed in §1. A direct managed-value transfer
could remove stages 4–6 (text expansion, parser block, `mk-string` copy) and one
host frame copy, but would still need at least one host-to-managed byte copy
and the same callback/state-machine turn.

**Why textual parsing is not the main target:** it is <1% of the profiled
1 MiB time. Removing all of it would not materially change the end-to-end
result. The repeated per-request callback/state machine is the dominant cost.

---

## 5. Loader-heap reclamation audit

### 5.1 Current invariant

`run_expression` saves `GC_LOADER_HP`, parses the host-generated source, runs
it, then restores the saved frontier (`glon.c:243-258`). The invariant required
for safety is:

> **No persistent Glon object may retain a reference to a loader object
> allocated above the saved `GC_LOADER_HP`.**

The generated reply/take source currently satisfies this by construction:

- it contains no `func` or block literal that is stored persistently;
- its `mk-string [ ... ]` argument block is consumed immediately and copied to
  a managed STRING!;
- `io/reply` does not retain the transient parser block;
- callbacks, pending closures, global blocks and SIN! values come from the
  application/library loader region below the saved frontier, or from the
  managed heap.

### 5.2 Focused test

`experiments/io4/loader_lifetime.glon` creates before any reply:

- a saved block `[11 22 33]`;
- a saved closure computed from that block;
- a saved SIN! with id `oops` and arg `7`.

A callback then creates a late global integer and a managed block, and the next
callback checks all of them. The source performs **50 asynchronous file/read
replies** and prints `LIFETIME_OK`.

Result:

```text
PASS loader lifetime
```

The same property is exercised at scale by the 4 MiB MASM stream (10,487 GC
collections and thousands of reply turns), which completed with exact output.

### 5.3 Boundary

The current implementation is safe only while every transient expression obeys
the invariant above. A future direct-value transfer or a host-generated source
that defines/stores a closure or loader block above the save point would be a
**loader-lifetime blocker** unless it copies/roots that object before
reclamation. The test covers current callbacks; it does not prove safety for an
arbitrary future source constructor. This is an integration condition, not a
current observed failure.

Task state was not modified or tested; the host does not enter M1 task
suspension. A future task-aware host must include task-local loader roots in
the invariant.

---

## 6. Representation alternatives

### A. Existing reply mechanism — reference

Current `glon.c:366-407` constructs decimal source, parses a transient loader
block, then `mk-string` builds the managed string. This is safe and already
bounded, but uses text expansion, parser/loader work and an extra managed copy.

Measured cost: source build + parse ≈ **0.2 s/1 MiB**; not the bottleneck.

### B. Direct managed STRING! construction

**Existing support:** no public/document host API constructs a managed STRING!
from arbitrary host bytes. `mk-string` is ordinary Glon; `alloc`/GC state are
runtime internals. `r0_s1_session_run_block` still requires a parsed block.
Writing M[] from C would be an undocumented memory-write shortcut, which this
investigation does not implement.

**Required changes:** a documented host value-transfer entry, explicit tag /
payload ownership, GC rooting and lifetime rules; or a C helper that invokes
the managed allocator and copies bytes under a safe GC state.

**Expected benefit:** removes text source + parser + loader block and possibly
one host copy. Estimated <1% of the current 1 MiB run unless paired with larger
chunks. Not justified alone.

### C. Existing structured host interfaces

`r0_s1_g1a_live_event_bytes` and `host_call` already provide an asynchronous
byte/event boundary, but they still construct `mk-string` source chunks
(`r0_s1_g1a_live.c:129-177`) and parse them. WASM `glon_event_bytes` has a
larger data cap but the same source construction model. Desktop `fs-read`
uses the same event path. They are useful protocol precedents, not a faster
native local transport.

### D. Bounded host-owned byte buffer

A documented fixed host buffer could be filled by C and copied into Glon as a
managed string, avoiding textual expansion. It would need:

- a stable, documented buffer location/ownership (no arbitrary pointer into
  Glon);
- a length bound and stale-data rules;
- a Glon-side copy into an immutable STRING! (or an explicit immutable
  constructor);
- GC/rooting rules while the buffer is copied;
- a versioned host contract and capability semantics.

Without those it is an undocumented side channel. If implemented, it could
enable 800–1,024-byte chunks and remove the parser/loader step. It should be a
separate ABI experiment, not an incidental change to IO-3.

---

## 7. Chunk-size and granularity trade-off

Current limits:

- `GLON_IO_MAX_CHUNK = 400` (`glon_io_file.h:22`).
- reply source cap 32,768 bytes (`glon.c:181`).
- frame cap 8,192 bytes (`glon_io_file.h:21`).
- parser block/form limit: 512 forms; measured 511 bytes succeeds, 512
  decimal byte forms fails (`parse_block` / `parse_string_literal`,
  `r0_s1_runtime.c:645-657,764-780`).
- 1,024-byte whole-file convenience cap in `modules/file.glon`; streaming
  host per-handle cap 8 MiB.

At 400-byte chunks, 1 MiB requires ~2,621 read + ~2,621 write replies. The
per-reply reply-run cost is ~10.8 ms, so reducing reply count is attractive.
However:

- 800-byte decimal reply source would require ~800 block forms, above the
  512-form parser limit;
- simply increasing `GLON_IO_MAX_CHUNK` would hit that parser limit and change
  the v1 protocol;
- a direct managed-value/buffer transfer could permit 800–1,024 bytes, but it
  introduces the ABI/ownership questions in §6;
- larger chunks increase per-string memory only modestly (about 1–2 KiB), but
  must remain bounded and must preserve short-read/write loops.

The 400-byte size is therefore a **representation/parser compromise**, not a
managed-heap necessity. It should not be changed in the IO-3 baseline without a
separate experiment.

---

## 8. Recommended improvement

### Smallest immediate, low-risk improvement

Replace the 1 ms polling wait in `glon_io_cli_wait` with an explicit
completion event/condition variable (or equivalent worker-done notification),
while preserving join/cancellation and one-outstanding-request semantics.

- No wire protocol change.
- No change to S1/HOST/parser/evaluator/GC/M1.
- Expected saving: the measured 1 MiB wait is 6.02 s; nearly all is polling.
  A C-native transport copy is ~6.7 s versus ~10–54 ms for direct service
  calls. This is ~9% of the MASM 1 MiB time and a larger fraction of MASM’s
  copy-only control.
- Must be tested for cancellation, worker failure, stale reply and join.

### Next focused experiment, only if more throughput is required

Test **larger chunks plus a documented managed-value transfer** (or bounded
host buffer), isolated from IO-3:

- 400 versus 800/1,024-byte payloads;
- direct managed STRING! copy vs text source;
- parser/loader removal;
- 4 KiB/64 KiB/1 MiB/4 MiB;
- peak live/managed/loader memory and GC;
- exact output and source preservation.

The expected gain comes primarily from fewer callback/state-machine turns, not
from removing textual parsing. Direct value transfer alone is not justified.

Do not implement either change in this investigation without architectural
review.

---

## 9. Verification

Performed on the experiment branch:

- `./check-frozen-s1.sh`: PASS.
- `make io-file-test`: 30/30.
- `make io-file-stream-test`: 24/24.
- `make glon-smoke` and `make glon-smoke MODULES=file`: PASS.
- MASM-1 differential tests: 221/221 (reference, MASM, C).
- MASM streaming integration: 1 KiB, 64 KiB, 1 MiB, 4 MiB exact output PASS.
- `loader_lifetime.py`: PASS across 50 asynchronous replies.
- `make wasm-test`: PASS.
- Non-simulation native runner: 948 checks, all tests passed.
- Full `./s1` still stalls in the documented long simulation tests; no
  simulation/runtime change was made.

The C and Glon benchmark outputs were verified against expected bytes; the
benchmark is not treated as proof of correctness.

---

## 10. Limitations

- WSL2 host load varied significantly: repeated 64 KiB Glon runs ranged from
  2.8 to 3.2 s (copy), 15.1 to 19.0 s (reference), and 7.4 to 8.0 s (MASM);
  1 MiB copy ranged 38.65–58.64 s. Stage shares are more useful than single
  absolute timings.
- `profile_glon` timers add overhead; 1 MiB profile total was 64.03 s versus
  60.75 s uninstrumented.
- The callback/state run is not split into evaluator, `s/+`, slicing and
  callback subcosts; source construction/parse/loader are split.
- POSIX read/write times are measured via C copy, not inside every Glon request.
- `c_copy` native/service timings are cache/load sensitive; transport timing is
  stable.
- No direct-value ABI was built; its copy/parse estimates are labelled
  estimates.
- The 1 ms polling cost is measured via transport and profile wait; removing it
  is a recommendation, not an implemented result.
- The loader invariant is tested for current callbacks, not formally enforced
  for arbitrary future transient sources.

---

## 11. Bottom line

The current host-to-Glon transfer is **safe but text/loader-heavy**; however,
at 400-byte chunks, the dominant cost is **per-reply Glon callback/state
execution**, not the text representation. The smallest justified immediate
change is to remove the 1 ms worker wait polling. A meaningful next experiment
is larger chunks with a documented direct managed-value/buffer transfer; that
must be an isolated ABI experiment, not a silent IO-3 change.

**Recommendation:** keep IO-3/MASM-1 as the working baseline; implement only
the event/condition completion experiment next. Do not redesign the HOST
contract or introduce an undocumented buffer write in this phase.
