# Optional file I/O — IO-3 bounded-memory streaming

**Branch:** `impl/optional-file-io-streaming`
**Base:** `97b8d36ff37131b75bdb4ae563f6598874a3657b` (IO-2 stabilised)
**Scope:** streaming first-occurrence file replacement in ordinary Glon over the
existing optional asynchronous file module. No new S1 primitive, HOST id,
parser/evaluator/grammar change, mutable string, or M1 scheduler integration.

---

## 1. Result

`modules/file-stream.glon` implements a reusable Glon file job:

```glon
stream/replace-first source target old new success failure
```

It reads the source in 400-byte chunks, keeps at most `len(old) - 1` bytes of
carry-over, searches for the first occurrence, writes the prefix and
replacement, then copies the rest unchanged. Source and target are distinct
handles; the source is never truncated while it is being read.

`modules/recipe-0004.glon` remains the whole-file 1,024-byte implementation.
Recipe 0004 semantics are preserved: only the first occurrence changes.

Scale results (measured on this WSL2 host, default `-O0 -g`):

| Input | Match | Time | RSS | Max live cells | Max live objects | Collections |
|---|---|---:|---:|---:|---:|---:|
| 1 KiB | start | 0.15 s | 2,136 KB | 2,048 | 59 | 3 |
| 1 KiB | none | 0.36 s | 1,880 KB | 1,728 | 51 | 3 |
| 4 KiB | start | 0.30 s | 2,136 KB | 1,728 | 50 | 9 |
| 4 KiB | none | 1.03 s | 2,256 KB | 2,480 | 61 | 10 |
| 64 KiB | start | 3.80 s | 1,832 KB | 2,944 | 61 | 123 |
| 64 KiB | none | 15.54 s | 2,264 KB | 3,440 | 63 | 163 |
| 1 MiB | start | 55.74 s | 2,008 KB | 3,008 | 62 | 1,967 |
| 1 MiB | none | 246.52 s | 2,136 KB | 3,440 | 63 | 2,621 |
| 4 MiB | start | 225.56 s | 1,880 KB | 3,008 | 62 | 7,865 |

The 4 MiB no-match case was not run: at the measured 1 MiB no-match rate it is
about **980 s (~16 minutes)**, which was judged not practical for this pass.
The 4 MiB start case is copy-dominated after the first chunk; the 1 MiB
no-match case exercises sustained scanning.

Peak **post-collection live** cells stay at roughly **1.7–3.5 k of the 7,232
available**, independent of file size. The managed heap high-water mark reaches
39,984–40,000 because the collector is non-moving and reuses blocks; this is
allocated/free frontier, not live data. Free cells after collection are
typically ~3.7–3.8 k. Collections scale with bytes processed, as expected.

The bounded live set is not an artefact of a fixed host buffer: bytes are
streamed from disk, the host request/reply buffers are fixed at 8 KiB each,
and files up to 4 MiB were processed with the same live-cell range.

---

## 2. Memory investigation

### Strings, reachability and GC

- A Glon STRING! is an immutable managed payload; `s/+` and `s/slice` return
  new strings. Neither retains the input except through ordinary references.
- Between replies, the previous chunk, combined window and emitted slices cease
  to be reachable once the next request is queued. The pending state retains
  only the carry (≤ `len(old)-1`), handles, and callbacks.
- `io/reply` now clears `io/pending-record`, `io/pending-request` and
  `io/pending-arg` before invoking the callback, so the just-consumed request
  and its byte field are not retained for another turn.
- The managed GC reclaims those strings. Measured post-collection live cells
  remain bounded; collection counts grow linearly with the amount streamed.

### Loader-heap issue found and fixed

The host parses a small reply/take expression each turn. Without reclaiming the
loader allocations, a 64 KiB stream exhausted the **loader heap**
(`R0S1_PARSE_LOADER_EXHAUSTED`), despite bounded managed live data. The native
driver now saves/restores `GC_LOADER_HP` around the transient reply/take
expression. The generated source only calls existing library functions and
builds managed strings; it defines no persistent closure. This is host driver
code, not a collector change.

### Request transport

`io/take-request` now returns a structured `[id op fields]` block. The C driver
encodes the v1 length-framed request frame itself. The **wire protocol v1 is
unchanged**; this removes a managed 400-byte frame allocation and repeated
`s/+` work from every request. The focused C protocol test still validates the
v1 frame encoding/decoding.

### Whole-file allocation fragmentation

After the structured request change, the whole-file path could fail to allocate
a 400-byte slice once the 1,003-byte input and 1,005-byte result were live: the
non-moving heap had enough free cells but not a sufficiently large contiguous
block. The whole-file loop now uses 128-byte read/write slices, while the
streaming path keeps the required 400-byte protocol chunk. The 1,024-byte
whole-file cap is unchanged.

---

## 3. Algorithm

Source: `modules/file-stream.glon`.

For nonempty `old` of length `m`:

1. Open source for read and target for write.
2. Read at most 400 bytes.
3. `combined = carry + chunk`.
4. If the first replacement is not yet emitted:
   - search `combined` for `old`, using bounded 64-start segments and unchecked
     `s/raw-at` after bounds checks;
   - if found: write prefix, replacement, suffix, set `done = 1`, `carry = ""`;
   - if not found: write the safe prefix (all but the last `m-1` bytes) and
     retain those `m-1` bytes as carry.
5. If `done = 1`, write every later chunk unchanged.
6. At EOF, write the remaining carry and close both handles.

Carry is at most `m-1` bytes, so a match crossing a chunk boundary is found
without holding the whole file. The recursive matcher's stack is bounded by
segments of 64 candidate starts, not by file size.

Behaviour choices:

- **Empty search pattern:** explicitly rejected with
  `SIN! 'stream 'empty-pattern`. There is no single well-defined first gap.
- **Same source and target:** rejected with `SIN! 'stream 'same-file`; the
  source is not truncated.
- **Replacement length:** may be shorter or longer than `old`; writes are
  split into bounded 400-byte requests.
- **Output failure:** source and target are distinct; a failure after target
  open can leave a partial output file. No atomic replace or rollback is
  provided.
- **Short reads/writes:** both loops continue from the returned byte count.

---

## 4. File-job driver

`stream/replace-first` is the reusable driver:

```glon
stream/replace-first "doc.txt" "out.txt" "web" "rebol"
    func [x] [ s/print "STREAM_DONE" ]
    func [e] [ raise e ]
```

It uses the existing `io/open`, `io/read`, `io/write`, `io/close` operations and
the existing one-outstanding-request protocol. No replacement logic exists in
C or the host. The host only transports bytes and owns descriptors.

Tests use independent Python `bytes.replace(old, new, 1)` results. The Python
reference never performs the Glon transformation.

---

## 5. Failures and classification

- **Algorithmic:** empty pattern and same-file are rejected before opening.
- **Memory:** scale runs complete with bounded live cells; no OOM after the
  loader-heap and whole-file slice corrections.
- **Host I/O:** missing input, read/write permission, short reads/writes,
  cancellation, host cap failure with partial output.
- **Protocol:** stale reply id and malformed structured request are rejected as
  job/protocol failures, not successful results.
- **Test harness:** independent expected bytes and process exit status.

A host cap failure during a 4 KiB stream leaves a partial `out.txt` and leaves
`doc.txt` unchanged; the test asserts both.

---

## 6. Comparison with whole-file implementation

At 1,022-byte input:

| Path | Time | RSS | Max live | Collections |
|---|---:|---:|---:|---:|
| whole-file `file/read` + `replace-first` + `file/write` | 0.30 s | 2,008 KB | 3,104 | 8 |
| streaming `stream/replace-first` | 0.16 s | 1,696 KB | 2,016 | 3 |

These small-file timings are noisy; the important difference is scale. The
whole-file path cannot exceed its 1,024-byte convenience cap, while the
streaming path completed 4 MiB with the same ~3 k live-cell envelope. The
whole-file path is simpler; streaming trades throughput and a second handle for
bounded memory.

---

## 7. Limitations

- POSIX native CLI only; no Windows/WASM/embedded backend.
- Throughput is low: ~246 s for a 1 MiB no-match scan at `-O0`; no-match scales
  with bytes because the search is in Glon.
- One outstanding request per job; no M1 blocked-task suspension.
- Host-level progress only; unrelated host-dispatched Glon jobs can run while a
  worker is pending, but M1 tasks are not suspended/resumed by I/O.
- No seek/stat/truncate/atomic replace; failures can leave partial output.
- Source and target must be distinct logical names.
- Empty search patterns are rejected; patterns longer than a chunk work by the
  carry rule but consume more memory and scan time.
- The host `--io-max-file` is a per-handle cap (8 MiB default, 16 MiB hard);
  the immutable whole-file convenience remains 1,024 bytes.
- The 4 MiB no-match case is not part of the default regression; it is an
  optional extended run.
- `--gc-report` exposes post-collection GC statistics, not an instantaneous
  peak-live counter; true peak-live instrumentation would require collector
  changes and was not made.

---

## 8. Tests and commands

```sh
make io-file-stream-test        # semantic cases, 1 KiB–64 KiB
make io-file-stream-scale       # adds 1 MiB match/no-match with measurements
make io-file-stream-4m          # adds 4 MiB match-at-start (slow)
make io-file-test               # whole-file 1,024-byte suite + C protocol tests
make glon-smoke
make glon-smoke MODULES=file
./check-frozen-s1.sh
```

The default streaming suite has 24 cases; `--scale` adds two 1 MiB cases and `--four-mib` adds the 4 MiB start case. It covers empty input, no match, beginning/middle/end,
multiple matches first-only, binary `00/80/ff`, matches crossing byte 398/399,
short and long replacements, short reads/writes, 1 KiB/4 KiB/64 KiB/1 MiB,
empty pattern, same source/target, missing input, read/write permission,
partial output on host cap, cancellation and cleanup. It compares output and
source bytes independently.

---

## 9. Recommendation

**Streaming is ready for isolated integration testing on POSIX under the
documented limits. It is not ready for production integration.**

The primary acceptance criterion is met: live managed Glon memory remains
approximately bounded as input grows from 1 KiB to 4 MiB, and processed chunks
are reclaimed between asynchronous replies. The remaining blockers for
production are throughput, the absence of Windows/WASM/embedded backends,
partial-output semantics, and the POSIX/pthread host.
