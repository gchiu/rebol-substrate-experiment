# GLON-MEM v001 — a minimal addressable-memory vocabulary (prototype)

> **Status (2026-11-09): experiment, not deployed.** An optional loadable
> library, `mem.glon`, on the unchanged machine:
> - no change to frozen S1, the ABI, the runtime or language semantics;
> - built on the existing M3 USER object and masm mechanisms.
>
> It depends on the collector repair of 2026-11-09 (commit `e988c19`). On the
> runtime before it, these tests fail-stop (§5).

**Objective.** Forth's approach, not Rebol's: a few general-purpose memory
operations (`@ ! C@ C! ALLOT MOVE`) from which libraries build buffers, rings,
bitstreams and tables. It is not a `binary!` datatype. There are no string,
compression or image operations, only two bridges to the existing STRING! type.

## 1. What existed, and what was missing

| Operation | Glon language | masm (RAW trapdoor) |
|---|---|---|
| cell fetch/store at an address (`@ !`) | none | yes, on **any** address, unchecked |
| byte fetch/store (`C@ C!`) | read only: `s/at` on an **immutable** STRING!, one byte per cell | via `@ !` and arithmetic |
| allocate (`ALLOT`) | only by building values (`reduce`, `mk-string`, `s/+`) | `CALL alloc` (managed heap) |
| copy (`MOVE`) | only by building new strings (`s/slice`, `s/+`) | loops over `@ !` |
| size query | `s/length`, `block-len` | header reads |
| release | none: the collector reclaims | none |

**The limitation is not semantic.** S1's `@` and `!` already address every
cell, and masm reaches them. What Glon lacked was a **checked, typed, owned,
mutable** storage object at the language level. Glon code could only make
immutable strings at one byte per cell.

**The storage limit is separate.** The managed heap is 7,232 cells of a
65,536-cell machine. No vocabulary changes that (§6).

## 2. The vocabulary (specification)

Names follow the `domain/operation` convention (`s/at`, `it/next`).

| Word | Meaning | Forth |
|---|---|---|
| `mem/alloc n` | a new region of `n` bytes, 1 ≤ n ≤ `mem/max`, zero-filled | `ALLOT` |
| `mem/release r` | end the region's life | — |
| `mem/released? r` | 1 after release | — |
| `mem/size r` | `n` | — |
| `mem/byte r i` / `mem/set-byte r i b` | one byte, 0 ≤ i < n, 0 ≤ b ≤ 255 | `C@` / `C!` |
| `mem/cell r i` / `mem/set-cell r i v` | one cell: bytes [i, i+4) | `@` / `!` |
| `mem/move src si dst di k` | copy `k` bytes, overlap-safe | `MOVE` |
| `mem/to-string r i k` / `mem/from-string r i s` | bridges to STRING! (copies) | — |

**Bounds.**
- Every index, count and value is checked before anything changes. A failed
  write changes nothing (test 4p).
- Ranges are half-open, `0 ≤ i` and `i + k ≤ size`, with no negative indices
  and no clamping, as in `strings-ext.glon`.

**Cells, alignment, byte order.**
- A cell is 4 bytes, little-endian, two's complement, at **any** byte offset:
  there is no alignment rule.
- Values are limited to **[−2^27, 2^27)**, the integers every Glon build holds
  (32-bit WASM's range).
  - `set-cell` outside that raises `'range`.
  - So does `cell` on 4 bytes, written one by one, whose value is outside it.
    Its top byte must then be 0–7 or 248–255.
- Only integers are stored. Region memory is not traced by the collector, so
  storing a reference would not keep its object alive; the wrappers refuse it.

**Overlapping copies.**
- `mem/move` behaves as if through a temporary (Forth `MOVE`, C `memmove`).
- Within one region with `di > si`, it copies from the last byte backward; in
  every other case it copies forward.
- There is no separate `CMOVE`/`CMOVE>`. A directional copy that reads its own
  output (an LZ77 match) is a loop of byte operations, or a library over
  `move`, not a primitive.

**Errors.** SIN!s, as everywhere in Glon:

| SIN! | Raised for |
|---|---|
| `'type` | not a region, not an integer |
| `'range` | an index, count, byte or cell value out of range |
| `'released` | a released region |

Allocation beyond the heap is the existing out-of-memory **fail-stop**
(GLON-ALPHA-LAWS.md §6a), as for every allocation in Glon. Requests over
`mem/max` (1,000,000 bytes) raise `'range`.

**Ownership, lifetime, GC.**
- **Ownership.** A region is a value; whoever holds it may use it.
- **Release.** `mem/release` ends its use deterministically: every later
  operation raises `'released`.
- **Reclamation.** The collector reclaims the storage once nothing refers to
  it. Release does not free storage at once: a non-moving collector cannot
  safely free an object that may still be referenced.
- **Heavy churn works.** 400 regions of 3,000 bytes (409,600 cells) cycle
  through the 7,232-cell heap (test 6).

**Representation.** A region is an **M3 USER object** (`T_USER`,
`GC_KIND_USER`):

| Cell | Holds |
|---|---|
| `p+0` | the descriptor `mem/kind`, a unique block, which is the region's identity |
| `p+1` | `count` = 0, so the collector traces **no** content cell |
| `p+2` | the size |
| `p+3` | the live/released state |
| `p+4…` | the bytes, packed **3 per cell**: byte `i` is bits `8·(i mod 3)` of cell `p+4+i/3` |

Three bytes keep every stored value below 2^24, so the layout and arithmetic
are identical in 64-bit native and 32-bit WASM.

**Portability.** It is masm and Glon only: the same file runs unchanged in the
native CLI and in `glon.wasm` (§4).

## 3. Assessment of the M3 extension mechanism

**What M3 supplies, in every session:**
- the `T_USER` tag, `GC_KIND_USER` allocation (`CALL alloc`), and the
  collector's generic `trace_user`;
- `trace_user` checks `count` against the extent, marks the descriptor and
  the first `count` cells, and **never scans the cells after `count`**.

That is exactly what a raw storage object needs: managed, collectable,
type-tagged, with an untraced payload. masm exports the names it needs
(`T_USER`, `GC_KIND_USER`, `alloc`).

**What M3 does not supply in ordinary sessions:**
- **The datatype vocabulary.** `make datatype!`, `type?` and `accepts?` exist
  only in the M3 test libraries, not in the CLI, Jupyter or page sessions.
  - So the library uses a unique block as descriptor, and checks a value with
    a masm tag test plus descriptor identity.
  - In an M3 session, `type?` on a region returns that block.
- **Safe raw storage before the repair.** The return-stack scan's object-start
  check (commit `e988c19`) is a **prerequisite**. A packed cell of three 9
  bytes (592,137) carries the closure kind in its low bits, so on the runtime
  before the repair, stale integers on the stack made the collector trace
  region contents. That runtime fail-stops on these tests (§5).

**Conclusion.** The vocabulary is implementable as an optional library with
**no** change to frozen S1, the ABI or the runtime, given the repaired
collector.

## 4. Tests

| Run | Result |
|---|---|
| native CLI (`make test`, repository runtime) | **66 / 66** |
| WASM (`make wasm-test`: `glon.wasm` built from the unchanged `standalone/glon.c`, under node 24) | **64 / 64, 2 skipped** |
| native CLI on the runtime **before** the collector repair (commit `2fb4cbb`) | fail-stop in the mark phase (the out-of-range T_USER check) |

The two WASM skips are 3j/3k. Values beyond the portable cell range are not
integers at all in 32-bit WASM, so those cases cannot be expressed there; the
test says so instead of counting a pass.

**Sections** (`mem-tests.glon`):
1. **Allocation and release:** zero fill, `'released` after release, `alloc 0`
   and negative or non-integer sizes refused.
2. **Bytes 0, 128 and 255** at all three positions within a packed cell. A
   middle-byte rewrite leaves its neighbours. A 300-byte pattern reads back.
3. **Cells:**
   - `0x01020304` is stored low byte first;
   - −1 at an odd offset is four 255 bytes;
   - the extremes −2^27 and 2^27 − 1;
   - overlapping cell writes;
   - an out-of-range cell assembled from bytes raises `'range`.
4. **Bounds and types:** indices −1 and `size`, a cell over the end, bytes 256
   and −1, moves past either end, negative counts. Strings, blocks and `none`
   are not regions. A failed write changes nothing.
5. **Overlapping moves:** forward (dst > src) and backward (dst < src) over
   100 bytes, a 1-byte overlap, between regions, `k = 0`, a move onto itself,
   and the two string bridges.
6. **Repeated allocation and release:** 400 regions of 3,000 bytes.
7. **GC safety and lifetime:**
   - contents survive about 2,000 collections' worth of garbage;
   - a region referenced only by an argument survives;
   - a region full of 9 bytes stays unchanged while every integer
     2,048–2,499 sits in a live stack context and the collector runs (the
     interior-pointer case of the repaired defect).

## 5. Measurements (native, `-O1`, 2026-11-09; `make measure`)

**Library cost:** 28 global bindings, 1,931 loader cells, **1,098 code
cells**, 608 managed cells (its closures).
- The code cells are the masm workers. They draw on the code space shared by
  every masm library, which grows towards the data stack (top 16,384):
  - with prelude, strings and strings-ext loaded, code ended at 14,193;
  - `mem.glon` moves that to 15,291, leaving 1,093 cells of data-stack room
    (measured).

**Storage**, in cells including the 16-cell header; layout prediction = live
cells measured after a collection:

| Bytes | Region | STRING! | STRING! / region |
|---|---|---|---|
| 1 | 32 | 32 | 1.00 |
| 100 | 64 | 128 | 2.00 |
| 1,000 | 368 | 1,024 | 2.78 |
| 3,000 | 1,024 | 3,024 | 2.95 |
| 6,000 | 2,032 | 6,032 | 2.97 |

So a region holds about 3 bytes per cell against a STRING!'s 1. Within the
64K heap a single region of 32 KiB would need 10,944 cells, more than the
whole 7,232-cell heap.

**Cost per operation** (3,000 operations each, the empty balanced loop's
39 µs/op subtracted):

| Operation | Cost |
|---|---|
| `mem/byte` (checked) | 94 µs |
| `mem/set-byte` (checked) | 118 µs |
| `mem/cell` (checked, 4 byte reads) | 145 µs |
| `mem/set-cell` (checked) | 282 µs |
| `mem/raw-get` (the unchecked masm worker) | **3 µs** |
| `s/at` on a STRING!, for comparison | 32 µs |
| `mem/move` | **0.5–0.6 µs per byte** (3,000 bytes, between regions or overlapping) |
| `mem/to-string` | 264 µs per byte |
| `mem/from-string` | 110 µs per byte |
| `mem/alloc` (3,000 bytes, zero-filled) | 115 µs |

**What the numbers say:**
- **Bulk operations are cheap.** `move` is a masm loop: about 200× cheaper per
  byte than a checked byte access.
- **Single checked accesses are expensive,** about 3× `s/at`. The cost is the
  wrapper's Glon checks (region identity, liveness, integer, range: five
  function calls), not memory access: the worker itself takes 3 µs.
- **The string bridges are Glon recursions** and slow. A masm bulk copy, like
  `move`, would be expected at about 1 µs per byte. That is a projection, not
  built.

## 6. DEFLATE feasibility (projections from §5; DEFLATE was not changed)

| Need | With `mem/` on the 64K machine | Measured or projected |
|---|---|---|
| **Full 32 KiB circular window** | **no.** 10,944 cells for one region, more than the 7,232-cell heap. A window of about 8 KiB (2,752 cells) fits beside the libraries and the decoder (≈ 2,700 live, measured in the DEFLATE experiment) | the cell counts are measured formulas; the fit is projected |
| **Full window in a 256K profile** | yes: about 11K cells of a heap of about 196K | projected; the profile needs approval (GLON-MEMORY-PROFILES.md) |
| **Dynamic Huffman tables** | yes. Mutable count and symbol tables fill in place (two regions of ~300 bytes, 128 cells each), with none of the concatenation garbage or ~300-cell contiguous strings behind the one fragmentation fail-stop | projected |
| **Incremental input and output** | yes, but the string bridges must become masm bulk copies; at the measured 110–264 µs per byte they would be the bottleneck | measured cost; the fix is projected |
| **Bounded memory** | yes: one window region and two table regions, allocated once and reused; nothing grows with the stream | projected |
| **Throughput** | **modest at best.** Matches would copy at 0.6 µs per byte instead of building strings. But Huffman decoding stays in Glon (about 400 µs per byte today), and a checked byte write (118 µs) costs more than `s/at`. Only an unchecked fast path for trusted library code (3 µs per byte) would change the per-literal cost substantially | measured parts; the totals projected |

## 7. Files

| File | Purpose |
|---|---|
| `mem.glon` | the library: masm workers (`mem/raw-…`, unchecked) and checked wrappers |
| `mem-tests.glon` | 66 tests |
| `run-wasm.js` | runs Glon files, in order, in one `glon.wasm` session under node |
| `measure.c` | storage and per-operation measurements. Measurement only: it defines a masm `m-gc` probe |
| `Makefile` | `make test`, `make wasm-test`, `make measure` |
| `results-measure.txt` | the `make measure` output recorded on 2026-11-09 |
