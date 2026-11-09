# GLON-DEFLATE v001 — a streaming DEFLATE decompressor written in Glon

> **Status (2026-11-09): experiment, not deployed.** It runs on the unchanged
> substrate:
> - no masm in the decompressor, no new native, no new primitive;
> - no change to frozen S1, the runtime, the ABI or any earlier experiment;
> - the host only moves bytes: it feeds compressed chunks, forwards read
>   requests and prints what Glon writes.
>
> The challenge is Christopher Ross-Gill's pull-streaming DEFLATE decoder
> (`rgchris/Scripts`, `r3-oldes/deflate.reb` 0.5.0).

## The answer

**Can Glon's small, frozen machine express a real streaming compression
algorithm? Yes for the algorithm, no for a full DEFLATE window, and the attempt
found a defect in the runtime's collector.**

1. **The algorithm is expressible.** `inflate.glon` is plain Glon. It does:
   - bit-level input;
   - canonical Huffman decoding of fixed and dynamic codes;
   - the code-length code with its repeat codes 16, 17 and 18;
   - stored blocks;
   - length/distance pairs, including overlapping copies;
   - end-of-stream detection;
   - malformed- and truncated-input handling.

   It is a resumable iterator. A request for *n* bytes decodes only those
   bytes. Input may arrive in any split, down to one byte at a time, and a
   symbol cut by a chunk boundary is decoded again from a checkpoint.
   - **Conformance: not a full pass.** Of 480 differential runs against C
     zlib (164 fixtures, 7 chunk/read schedules):
     - **478 pass on the unchanged runtime.** One run stops in a GC-defect
       halt, one in a heap-fragmentation fail-stop.
     - **479 pass on a scratch build with the proposed collector fix.** The
       fragmentation fail-stop remains.

     No run produced a wrong byte: the failures are safe machine fail-stops.
   - **Error classification:** every malformed (222 runs) and truncated (24)
     case gets the class zlib gives it, and exactly zlib's bytes before the
     error. Every over-window stream (4) stops with `'window`.
2. **A full RFC 1951 window does not fit in the 64K reference machine.**
   - DEFLATE allows back-references up to 32,768 bytes, and a STRING! costs one
     managed cell per byte. The managed heap is 7,232 cells. The libraries'
     closures (about 64 objects, 2,048 cells) and the decoder's state take
     about 2,700 before any data.
   - The window is a parameter, `dz/new w`. The host uses 768 bytes, which
     covers every stream made with zlib `windowBits` ≤ 10 (zlib's largest
     distance there is 762). A longer distance stops with a distinct error,
     `'window`, which marks a resource limit, not malformed input.
   - The same budget limits the input chunk (≤ 128 bytes per feed) and the
     result of one read (≤ 256 bytes, enforced by the decoder).
   - Peak live memory is about 4,500–5,000 of the 7,232 cells. A dynamic block
     header needs two strings of about 300 contiguous cells (the code lengths
     and the literal symbols), and the non-moving heap can be too fragmented to
     place them: 1 run in 480.
3. **Runtime defect: the collector can modify an immutable STRING!.**
   - The return-stack scan takes any 16-aligned cell value in the heap for a
     closure pointer if the cell at `p − 15` has closure "kind" bits.
   - The inside of a string can supply those bits: a byte of 8–11 is enough. A
     stale integer *n* on the stack is the 16-aligned cell `16n`.
   - The collector then traces string bytes as a closure, and writes its mark
     bit into one of them. Depending on the neighbouring bytes, that is a
     fail-stop or a silent change to the data.
   - `tests/gc-interior-repro.glon` shows it in 60 lines of plain Glon.
   - **Since repaired** (2026-11-09, a separate correctness commit): the
     runtime now applies `proposal/gc-object-start-check.patch`, with the
     regression test `r0_s1_gc_interior_tests.c`. The results in `results/`
     are kept as they were recorded before the repair; `results/AFTER-FIX.md`
     gives the rerun.

**Performance.** Compared with C zlib on the same machine:

| Workload | Glon | C zlib | Glon is slower by |
|---|---|---|---|
| dynamic-Huffman text | 2.4 KB/s | 840 MB/s | about 340,000× |
| incompressible data in dynamic blocks | 0.45 KB/s | | about 580,000× |
| stored blocks and long overlapping copies | 12–15 KB/s | | |

See *Benchmarks*.

## Files

| File | Purpose |
|---|---|
| `inflate.glon` | the decoder: bit reader `br/`, Huffman tables `hf/`, decoder and API `dz/` |
| `progress.glon` | ASCII progress line, pure presentation (`pb/`) |
| `app.glon` | the decoder's conversation with a host over the GLON_LIVE byte-event ABI (`do-event`) |
| `dzhost.c` | the C stand-in host: unchanged runtime + GLON_LIVE dispatcher; feeds, reads, frames, statistics |
| `tests/fixtures.py` | the corpus: zlib streams and an RFC 1951 bit writer for hand-made and malformed streams |
| `tests/run_tests.py` | the differential runner against C zlib (via Python) |
| `tests/unit-tests.glon` | 44 unit tests of the iterator contract (CLI) |
| `tests/demo.py`, `tests/bench.py` | the progress demonstration; the benchmarks |
| `tests/gc-interior-repro.glon` | the collector defect, in plain Glon |
| `proposal/gc-object-start-check.patch` | the proposed collector fix (not applied) |
| `results/` | the measured reports of 2026-11-09, kept as recorded: conformance, benchmarks and demo output, on the unchanged runtime and with the proposed fix |

`make` builds `build/glon` (the CLI) and `build/dzhost` from the repository's
unchanged sources. Then:
- `make unit`, `make test` (unit tests, then the corpus);
- `make demo`, `make bench`;
- `make repro`.

The tests need Python 3; they use only its standard library.

## The iterator contract

```text
d: dz/new 768           a decoder keeping a 768-byte window (a multiple of 8)
dz/feed d chunk         compressed bytes, a STRING!, any split, any time
dz/finish d             no more input will come
dz/read d n             up to n bytes (at most dz/max-read = 256 per call)
dz/state d              'ready | 'need-input | 'done | 'error
dz/error d              the SIN! once 'error
dz/in-pos d  dz/out-pos d  dz/rest d    consumed / produced / fed but unused
```

- **Pull, not push.** Nothing is decoded until a read asks. A read of *n*
  decodes until it has *n* bytes or the decoder stops: the end of the data fed
  so far, the end of the stream, or an error. Example: feed 15 bytes, read 5
  (`"hello"`), read 100 (`" hello hello world"`); the state is then `'done`.
- **Three ways to stop, kept apart.**

  | Situation | State |
  |---|---|
  | input exhausted, more may come | `'need-input`: feed and read again |
  | input exhausted after `dz/finish` | `'error` with `'truncated` |
  | malformed data | `'error` with the specific id |

- **Good bytes first.** A read that meets an error returns the bytes decoded
  before it, and the next read raises the SIN!. The bytes before an error are
  exactly zlib's.
- **Error ids** (SIN! type `'inflate`):

  | Kind | Ids |
  |---|---|
  | malformed | `'block-type` `'stored-length` `'counts` `'code-lengths` `'repeat` `'no-end-code` `'bad-code` `'symbol` `'distance-symbol` `'distance` |
  | input | `'truncated` |
  | resource limit | `'window` |

- **Trailing data.** After `'done`, `dz/rest` counts the bytes fed beyond the
  stream's end.
- **Independent decoders.** Each decoder's state is its own (unit test H).

## How it is built without new language

### Mutable state: closure contexts
A decoder is a block holding two closures that share their constructor's
context, as GLON-ITER v001's iterators do:
- a **message closure**: begin, end, feed, finish, state, error, positions;
- a **step closure**, which advances the decoder by one unit.

The bit reader is a separate object of the same kind, with its own state:
- the input string and the position in it;
- a bit accumulator and its bit count;
- a committed checkpoint.

A function holds at most 16 bindings. The decoder's state uses 15 (14
parameters and `dz-stop`), which is one reason the bit reader is a separate
object.

### Bits without bitwise operators
Glon has `+ - * /` and comparisons, and no `and`, `or` or shift.
- **Bytes enter the accumulator** as `bb + byte * 2^bc`.
- **Taking k bits** is `bb − (bb / 2^k) * 2^k`, then `bb / 2^k`. The powers are
  a literal block in the loader heap.
- **The accumulator** never holds more than 23 bits, so it stays within the
  32-bit WASM build's 28-bit integers.

### Resumable units and rollback
Input is consumed in units:
- one block header (for a dynamic block, the whole table description);
- one literal;
- one length/distance pair;
- one run of stored bytes.

Each unit runs under `judge`:
1. Running out of input raises `'need-input`, or `'truncated` after
   `dz/finish`.
2. The decoder **rolls the bit reader back** to the last committed checkpoint,
   and the state becomes `'need-input`.
3. After the next feed, the unit is decoded again from the checkpoint.

So a 300-byte dynamic header cut by a 1-byte chunk boundary is simply decoded
again whole. Feeding keeps only the unconsumed bytes. Decoded output is never
produced twice. A match longer than the request is kept as a remaining-length
count.

### Canonical Huffman decoding (puff.c's method)
`hf/walk` reads one bit at a time from a local copy of the accumulator. At each
length it compares the code so far with the first code of that length, using
only the counts per length, and recurses at most 15 deep. It answers
(canonical index, index within the length, length); the table maps that to a
symbol.

Table representation, chosen for the heap:
- **Dynamic tables** are `[counts lowc syms]`:
  - `syms` is **one byte per symbol** in canonical order (the symbol mod 256);
  - `lowc` says how many codes of each length are below 256.

  An earlier two-bytes-per-symbol table needed a 600-cell string, which the
  non-moving heap could not place once it was fragmented.
- **Fixed tables** are literal blocks in the loader heap with arithmetic symbol
  mapping. They cost **no** managed memory.
- **Building a table:**
  - for each length, `s/scan-set` (a shipped masm loop) finds the next symbol
    with that length;
  - the strings are joined by balanced recursion.
- **Validation follows zlib:** over-subscribed codes are refused. An incomplete
  code is accepted only if it is empty, or (for literal/length and distance) a
  single code of length 1.

### The window: segments, not one string
History is 8 segments of *w*/8 bytes, aligned to absolute output positions,
plus the current partial segment:
- **No copying.** A full segment shifts in by building an 8-element block.
- **Small allocations.** No allocation exceeds about *w*/8 cells.
- **Overlapping copies.** `dz/fetch` takes the last *d* bytes (at most 4
  pieces), and `hf/rep` repeats them to the match length by doubling. The final
  doubling adds only what is missing, so no allocation exceeds the length.

### Loops without loop words
Glon has no loops and about 84 frames of return stack. Every loop is balanced
recursion:
- `dz/loop b i k` runs a step up to 2^k times at depth k + 1;
- table strings are built by halving ranges;
- large reads are split in halves.

The deepest measured return stack was 3,864 of 8,192 cells.

### Host boundary: existing ABI only
`dzhost` uses the existing GLON_LIVE inbound byte event
(`r0_s1_g1a_live_event_bytes`):
- **Input.** Each chunk becomes `current-value`, a STRING!, and the dispatcher
  reclaims its loader-heap wrapper, so input of any length passes without
  loader growth.
- **Reply.** Each dispatch's result fragment is one status byte (R, N, D or E).
- **Output.** Decompressed bytes are printed as frames, `"D <n>"` and then *n*
  bytes, with `s/print`.
- **Progress** is `progress.glon`'s text line, also printed with `s/print`.

## Progress display

```text
Inflating [####----------------]  23%  in 270/1159  out 1536
Inflating [##########----------]  52%  in 610/1159  out 3328
Inflating [####################] 100%  in 1159/1159  out 6000
```

- **Measure.** The percentage is compressed bytes consumed over the known
  compressed size. Without a total (`dzhost -u`) only counters are shown,
  never an invented percentage.
- **Redrawing in place.** Glon's only byte output without masm is `s/print`,
  which always appends a newline, so a bare carriage return cannot overwrite a
  line. On a terminal (`-p 2`), each update is prefixed with ESC[1A ESC[2K CR
  (cursor up, clear line, return): the line is redrawn in place. `-p 1` prints
  one line per update.
- **Separation.** `progress.glon` knows nothing of DEFLATE: it is given four
  integers.

## Tests

Run with `make test`. Each run's class and bytes must equal C zlib's.
Report: `build/test-report.txt`.

**The corpus** (`tests/fixtures.py`, deterministic) has 164 fixtures.
- **44 valid or hand-made streams:**
  - empty payloads (stored, fixed, zlib levels 0 and 6), and empty input;
  - stored blocks, several of them, with full and sync flushes;
  - fixed Huffman (`Z_FIXED`), including a hand-made block with every distance
    1–8 and lengths 3–20, 257 and 258;
  - dynamic Huffman: default, `Z_HUFFMAN_ONLY`, `Z_RLE`, all 256 bytes;
  - dynamic headers using complete 286/30-symbol alphabets, repeat codes
    16/17/18, and a single-code distance tree;
  - multi-block streams (`memLevel` 1–2, mixed flushes, stored + dynamic);
  - highly compressible data (`a` × 20,000, a 10-byte pattern × 1,500);
  - incompressible noise;
  - one valid stream needing a 2,000-byte distance (expected: `'window`).
- **14 malformed hand-made streams:**
  - block type 3;
  - stored LEN/NLEN mismatch;
  - distance before the output;
  - literal/length symbol 286;
  - distance symbol 30;
  - HLIT 287 and HDIST 31;
  - over-subscribed and incomplete code-length codes;
  - repeat 16 first;
  - a repeat overflowing the lengths;
  - no end-of-block code;
  - over-subscribed and incomplete literal codes.
- **120 corrupted streams:** single and double bit flips in small valid
  streams. Their output is at most 1,024 bytes, so no distance can exceed the
  window, and the comparison with zlib is exact for every one of them.
- **Truncation:** an empty input, plus every truncation that the schedules
  produce.

**Schedules** (input chunk sizes / read sizes, used in turn):

| Schedule | Chunks (bytes) | Reads (bytes) |
|---|---|---|
| `c128/r100` | 128 | 100 |
| `c1/r1` | 1 | 1 |
| `awkward` | 3, 7, 1, 64, 2, 13, 128, 5 | 7, 128, 1, 1000, 100, 129, 4096, 255 |
| `c1/r4096` | 1 | 4096 |
| `c128/r65536` | 128 | 65536 |
| `eager` | all input fed and finished before the first read | |
| `c2/r1` | 2 | 1 (corrupted streams only) |

Reads above 256 are capped by the decoder. A 1-byte chunk boundary falls
inside symbols, extra bits and dynamic headers at every bit offset, and every
unit is then decoded again from its checkpoint. **Changing chunk or read
sizes never changed a byte of output.**

**Results** (native, 2026-11-09):

| Runtime | Runs passing |
|---|---|
| unchanged | **478 / 480** |
| scratch build with the proposed collector fix | **479 / 480** |

| Reference class | Unchanged | With fix |
|---|---|---|
| ok | 228 / 230 | 229 / 230 |
| malformed | 222 / 222 | 222 / 222 |
| truncated | 24 / 24 | 24 / 24 |
| beyond window | 4 / 4 | 4 / 4 |

The failures:
- `multi-memlevel1`, `c128/r100`, **both runtimes**: a heap-fragmentation
  fail-stop after 4,047 correct bytes. At the last collection 4,288 cells
  were live and 2,944 free, but in 19 holes, too small for the ~300-cell
  string a dynamic header needs.
- `multi-wbits9`, `awkward`, **unchanged runtime only**: the collector defect
  (a fail-stop in the mark phase's T_BLOCK range check), after 2,284 correct
  bytes.

**Glon unit tests** (`make unit`): 44 / 44 on the unchanged CLI. They cover:
- states and positions;
- byte-at-a-time feeds;
- `'need-input` against `'truncated`;
- good bytes before an error;
- trailing data;
- argument checks;
- independent decoders;
- the read cap.

`./check-frozen-s1.sh` passes. Nothing outside `experiments/glon-deflate/`
was changed except the entry in `GLON-ARCHITECTURE-MEMORY.md`.

**Progress demonstration** (`make demo`; `build/demo-output-fixed.txt`):

```text
-- known total (compressed size), reading 512 bytes per request, feeding 128-byte chunks
Inflating [--------------------]   0%  in 0/1159  out 0
Inflating [##------------------]  11%  in 128/1159  out 202
...
Inflating [###################-]  99%  in 1151/1159  out 5967
Inflating [####################] 100%  in 1159/1159  out 6000
-- unknown total: counters only
Inflating  in 128 bytes  out 202 bytes
...
-- data run: output EQUALS the original (6000 bytes)
```

On the unchanged runtime, the known-total run of this demo fail-stops at 11%:
the collector defect, a T_STRING range check (`build/demo-output.txt`). The
extra strings of the progress lines change the heap layout. The data run
completes.

## Benchmarks

`make bench` (`build/bench-report.txt`).

**Configuration:**
- native build, `-O1`, the unchanged runtime, through `dzhost`;
- input fed in 128-byte chunks, reads of 4,096 (capped at 256);
- C zlib 1.2.11 called through Python's `zlib.decompress`, 3,000 repetitions;
- WSL2 on x86-64, 2026-11-09.

| Workload | Compressed B | Output B | Glon KB/s | First output | Peak live (of 7,232) | Return stack (of 8,192) | Collections | zlib MB/s | zlib / Glon |
|---|---|---|---|---|---|---|---|---|---|
| text, dynamic | 3,558 | 20,000 | 2.43 | 503 ms | 4,496 | 3,864 | 159 | 838 | 336,653× |
| text, fixed | 1,015 | 4,000 | 2.16 | 229 ms | 3,824 | 2,865 | 24 | 1,224 | 553,228× |
| noise, dynamic | 3,044 | 3,000 | 0.45 | 878 ms | 4,544 | 3,604 | 185 | 270 | 584,958× |
| noise, stored | 3,010 | 3,000 | 14.5 | 12 ms | 3,808 | 2,306 | 11 | 8,648 | 582,003× |
| `a` × 20,000 | 37 | 20,000 | 12.4 | 157 ms | 4,528 | 3,864 | 102 | 588 | 46,322× |

**Where the time goes:**
- **Startup:** loading the six libraries (parse + run) takes 19 ms, using 105
  of 256 global bindings and 10,721 of 14,800 loader cells. Creating a decoder
  takes 1.8 ms.
- **Time to first output** is dominated by the first block header. A dynamic
  header (table construction in Glon) costs 0.5–0.9 s.
- **Per byte:**
  - each literal is one `judge`-protected unit of about 30 closure calls, plus
    a 1-byte string and a concatenation: about 0.4 ms per byte for text, 2 ms
    for literal-only noise;
  - stored bytes and long matches move in strings, through the shipped masm
    copy loops.

**Suspension and resumption** (text, dynamic; each read is a host round trip:
event dispatch, resume, frame output):

| Read size | Reads | Per read | Throughput |
|---|---|---|---|
| 1 byte | 2,005 | 3.2 ms | 0.30 KB/s |
| 100 bytes | 217 | 38 ms | 2.34 KB/s |
| 4,096 (capped at 256) | 88 | 91 ms | 2.41 KB/s |

So one suspend/resume round trip costs about 2.8 ms over the decoding itself.
Reads of 100 bytes or more are within 3% of the best throughput.

**With the proposed collector fix** (scratch build), the same table runs
5–30% slower. The fix walks the heap's object sizes for each closure-kind
candidate on the return stack.

**Honest summary:** the Glon decoder is 5 to 6 orders of magnitude slower than
C zlib. It is an interpreter, run on an emulated stack machine, built from
seven primitives, and it uses no loops and no bitwise operations.

## Runtime defect: the collector can write into a STRING!

**What happens.**
- **The scan.** The collector scans the return stack for closure pointers
  conservatively (`emit_scan_closures` → `emit_mark_closure_inline`,
  `r0_s1_runtime.c`). A cell value *p* counts as a closure if it is
  16-aligned, inside the managed heap, and the cell at `p − 15` (its would-be
  header flags) has the closure kind, `(flags / 4) mod 8 = 2`.
- **Why that is not enough:**
  - a STRING! stores **one byte per cell**, so inside a string any byte of
    8–11, 40–43, 72–75, … passes the kind test (9 even passes as allocated
    + closure);
  - an **integer** *n* is the cell `16n`, always 16-aligned, so a stale integer
    between 2,048 and 2,499 left in a stack context's unbound slot is a
    candidate.
- **The consequence.** `mark_push` writes `flags + 2` into what it took for the
  header: a **byte of a live, immutable string**. The sweep visits only real
  headers, so the change stays. Then `trace_closure` reads the following bytes
  as spec, body and context:
  - a byte with tag 6, 7, 11, 12 or 14 is a "pointer" below the heap, and the
    collector **fail-stops** (`out-of-range T_CONTEXT`, `T_USER`, `T_STRING`
    or `T_BLOCK`);
  - otherwise the **corruption is silent**.

**Reproduction** (`make repro`, plain Glon, no masm). It sprays one integer
into 14 unbound context slots, then allocates until the collector runs, and
checks a 300-byte string of byte 9:

```text
corrupted at 0 after spraying from 2078
11
```

**Why DEFLATE found it.** Several ingredients usually meet here:
- code-length strings are dense in bytes 7–9, which is why the decoder now
  stores length + 16;
- window and input strings hold arbitrary bytes;
- the decoder keeps many integers in the 2,048–2,499 range on the stack;
- the heap is near full, so collections happen deep in the stack.

On the unchanged runtime the defect stopped up to 24 of 164 runs in earlier
layouts. In the final layout it stops 1 of 480 runs, and the progress demo.
**A Glon program cannot avoid it**, because data bytes are arbitrary.

**The fix** (`proposal/gc-object-start-check.patch`). When this experiment was
recorded it was **not applied**. It was then reviewed and applied as a
separate correctness repair; see GLON-ARCHITECTURE-MEMORY.md, "Collector
defect".
- **What it does:** before marking a candidate, walk the heap's object sizes
  from `GC_HEAP_BASE` up to the frontier, and accept *p* only if the walk
  lands exactly on `p − 16`.
- **Size:** 17 lines in `emit_mark_closure_inline`. It does not touch S1, the
  memory map or any frozen file.
- **Cost:** the walk is O(objects) per candidate with the closure kind. It
  measured 5–30% slower on these benchmarks.
- **A cheaper exact check** would need an object-start bitmap (452 bits for
  the 64K heap) maintained by alloc and sweep: a larger change.

**Validation (in a scratch copy outside the repository):** with the patch, the
reproduction prints `intact`, and the corpus passes 479 of 480 (the remaining
failure is the fragmentation fail-stop).

To reproduce, copy the sources to a temporary directory, `git apply` the patch
there, and build `dzhost` against the copy. Then run:

```sh
python3 -I tests/run_tests.py --host /path/to/dzhost-fixed
```

## Findings: what the 64K machine lacks for this program

Each item gives the capability that is missing or limiting, why existing Glon
cannot express it, whether a Glon-level abstraction suffices, and the
smallest change if one is genuinely needed.

1. **A 32 KB history window.**
   - **Why not:** the managed heap is 7,232 cells at one cell per byte, and
     about 2,700 are taken before any data. No Glon operation stores more than
     one byte per cell: STRING! is the only byte array, and INTEGER! packing
     would need a mutable array, which Glon lacks.
   - **Glon-level:** a bounded window (done: `dz/new w`, with the `'window`
     error). That is enough for zlib `windowBits` ≤ 10 streams, not for
     ordinary `.gz` and `.zip` files (`windowBits` 15).
   - **Smallest change:** the already-proposed 256K memory profile
     (GLON-ARCHITECTURE-MEMORY.md §9) would hold a 32 KB window. It is not
     requested here.
2. **Large contiguous strings.**
   - **Why not:** the managed heap is non-moving, so one ~300-cell string can
     fail with 2,900 cells free.
   - **Glon-level, mostly:** segmented window, 1-byte-per-symbol tables, fixed
     tables as loader literals, reads capped at 256, input chunks ≤ 128. One
     fail-stop in 480 remains. Removing it would need every structure kept in
     pieces of ≤ 64 bytes.
   - **No ABI change proposed.**
3. **Accumulators passed down a recursion are all retained.**
   - **Why:** there are no tail calls, so each pending frame keeps its
     argument alive. An accumulating `hf/acc` held 5 copies of a 257-byte
     table, and a threaded state kept 9 copies of the lengths. Both failed
     with out-of-memory.
   - **Glon-level, solved:** balanced concatenation, or one variable updated in
     place by a closure. This is the main memory lesson of the experiment.
4. **Every function costs 32 managed cells.**
   - **Why:** a top-level `func` is a managed closure. The session's 64
     closures use 2,048 cells, 28% of the heap. Merging small helpers saved
     about 200.
   - **No change proposed.**
5. **No BLOCK!/closure type test without masm.**
   - **Why:** the shipped non-masm predicates cover STRING! (`s/raw-string?`)
     and INTEGER! (`s/raw-int?`) only. `iter.glon`'s `it/raw-tag` is masm, so
     `dz/` words cannot reject a non-decoder block, which then reaches the
     unchecked `block-at`/`invoke`.
   - **Glon-level:** only partial checks.
   - **Smallest change:** ship a checked tag word in the prelude, as
     `it/raw-tag` already is, in masm. That is a library addition, not an ABI
     change.
6. **No byte output without a newline.**
   - **Why:** `s/print` always appends one. An in-place progress bar uses ANSI
     cursor-up before the line instead of a bare carriage return, and binary
     output is framed by the host.
   - **No change needed** for this experiment.
7. **No bitwise operators.**
   - **Glon-level, sufficient:** `/`, `*` and `-` by powers of two. The
     accumulator never exceeds 2^23, safe for the 32-bit WASM build. WASM
     itself was not run (see *Not done*).

**Not done:**
- **WASM parity:** the browser host and WASM runs were not attempted.
- **zlib/gzip envelopes:** Adler-32 and CRC-32 are not implemented; Chris's
  module has them.
- **Performance:** no work beyond memory.

## Architectural assessment

**What made the decoder possible, all of it existing:**
1. **Closures over mutable contexts as objects** (GLON-ITER v001's protocol: a
   block of closures sharing a constructor's context). This is the iterator.
   It holds all decoder state between requests with no new datatype.
2. **`judge` / `raise` with SIN! values** as a non-local exit. A unit of
   decoding can be abandoned when input runs out, and the checkpoint is
   restored. This is what makes bit-level resumption simple: retry the unit,
   instead of a hand-written state machine for every partial symbol.
3. **STRING! as an immutable byte array.** It is used for the input, the
   window segments, code-length tables and symbol tables. The shipped
   `s/scan-set`, `s/slice` and `s/+` provide the only bulk byte loops, as
   masm, through validated wrappers.
4. **Balanced recursion** as the loop form. It keeps every loop within about
   40 frames, under the ~84-call limit.
5. **The GLON_LIVE byte-event ABI.** It moves input of any size into Glon in
   chunks, reclaiming the loader heap after each dispatch, with no new host
   call.
6. **Arithmetic natives** replacing bitwise operators.

**What did not need changing:** S1, the evaluator, the binding and escape
laws, the ABI, the loader. The decoder adds no masm and no native.

**What the machine limits:**
- the window (memory);
- contiguous allocation (a non-moving heap);
- throughput (5–6 orders of magnitude below C).

**What the experiment exposed:** a soundness defect in the collector's
conservative return-stack scan. It can halt, or silently modify a STRING!,
for any program that holds binary data and integers in the 2,048–2,499
range. It is reproduced in plain Glon, and a fix is proposed but not applied.

**Verdict.** A small frozen machine can express a real streaming compression
algorithm, including bit-level decoding, canonical Huffman trees, sliding
windows and resumable iteration, without masm or new runtime machinery. On
the 64K reference machine it does so only within a 768–1,024-byte window and
at about 2 KB/s. Its reliability now depends on the collector fix.
