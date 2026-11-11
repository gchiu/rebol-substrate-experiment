# F2 memory audit — DEFLATE in the 65,536-cell map

## Map

The reference machine is fixed at 65,536 cells (`GLON-ARCHITECTURE-MEMORY.md`
§5, `r0_s1.h`):

| Region | Cells |
|---|---:|
| registers/scratch | 256 |
| emitted runtime code | 256–12,593 (grows; current experiment ends at ~15,003) |
| main data stack | grows down from 16,384 |
| main return stack | grows down from 24,576 |
| runtime state, GC worklist, G1 buffers | 24,576–32,768 |
| **managed heap** | **32,768–40,000 = 7,232 cells** |
| loader heap | 40,000–54,800 = 14,800 |
| task arenas / table | 54,800–65,536 |

The managed heap is non-moving, mark/sweep, one cell per string byte, with a
16-cell header stride. A 32 KiB LZ77 history would need 32,768 cells by itself,
more than four times the managed heap. A packed 3-bytes-per-cell region would
still need about 10,944 cells, more than the whole heap. The current decoder's
768-byte window is therefore not an arbitrary API choice: it is a 64K
resource-limit choice. A full 32K window requires the separately documented
256K memory profile, not a local DEFLATE change.

## Allocation classes

| Class | Current implementation | Size / frequency | Lifetime | Approx. peak | Address+length possible? | Observable change |
|---|---|---|---:|---|---|---|
| Decoder state | `dz/new` block, two closures, `br` block/closure | 32–128 cells; once per decoder | whole decoder | ~0.3K | state can remain structured; not the failure | no |
| LZ77 history | `dz-ws`: eight STRING! segments; `dz-cur` partial | segment `w/8` = 96 bytes for w=768; 8 × 128-cell extents | whole decoder | ~1,024–1,280 | yes, with a mutable byte region; current strings are managed | no, if copy semantics kept |
| Input buffering | `br-in` string; `s/+ s/span` per feed | per feed, unconsumed input; bounded by host chunk | until consumed | tests ≤ ~12K chunk theoretically; conformance small | yes, host/caller buffer | no |
| Code-length array (failure) | repeated `s/+` into `dz-ls`; now one fixed buffer per dynamic header | max 316 bytes; 1 per dynamic header | header only | old growth ~320+; F2 buffer 336 cells | yes, fixed buffer; F2 does it | no |
| Code-length Huffman table | `[counts lowc syms]`; 19 symbols | small, per dynamic header | until header ends | <100 | yes | no |
| Literal/distance Huffman symbols | `syms` string in each table | ≤286 bytes each; one allocation per table | whole block | up to ~640 for two tables | yes, packed/chunked; not done in F2 | no |
| Huffman counts/lowc | blocks of 16 integers | 16 cells + header; per table | whole block | <200 | could be fixed | no |
| Temporary repeated bytes | `hf/rep` doubling strings for matches/zero runs | up to 258 bytes; per run | call | ~300 | yes; copy directly into output/history | no |
| Output piece | `dz-out`; literal `s/+` and match `hf/rep` | up to a read piece (≤256); per piece | one read | ~320 | yes, caller-provided output buffer | no |
| Read result/framing | `dz/read*` halves; `D n` frame in app | up to `dz/max-read` 256; per read | until host drains | ~320 + frame | yes, host output buffer | yes (frame format stays) |
| Window roll | `reduce` block of 8 segments | per full segment | whole decoder | small | yes | no |
| Closures/contexts | every Glon function call | 32/64 cells; per call | call frame or captured | substantial | language mechanism, not DEFLATE-specific | potential semantics/perf |
| App/progress | `s/from-int`, `s/+`, `pb/rep` | per read; progress mode | one line/read | small | presentation only | yes |

The measured maximum live managed cells in the F2 runs was 4,832 (multi),
4,464 (dynamic-text and compressible-a); baseline values were 4,336–4,368.
The managed heap remained the fixed 7,232 cells. `max_rs` was 3,448–3,548 in
F2, versus 3,768 baseline.

## Allocation counts

Profiling counters are reset by each `r0_s1` dispatch, so `dzhost.c` accumulates
per-dispatch deltas. On the failing workload `multi-memlevel1`, `-c 128 -r 100`:

| | F1 baseline | F2 prototype |
|---|---:|---:|
| Output | 4,047 then fail | 9,000 (matches zlib) |
| Total managed allocations incl. libs/open | 7,254 | 13,025 |
| Allocations during decoding (after open) | 7,199 | 12,963 |
| Managed cells allocated during decoding | 306,336 | 455,104 |
| Decode allocations per produced output byte | 1.78 | 1.44 |
| Decode cells per produced output byte | 75.7 | 50.6 |
| Max live managed cells | 4,368 (before failure) | 4,832 |
| GC count | 128 (before failure) | 185 |

The baseline row produces only 4,047 bytes before failing; the F2 row completes
9,000. Raw totals are therefore not a like-for-like workload, but the per-output
byte figures show that F2 allocates fewer managed objects and fewer cells for
each byte it actually produces.

On the already-passing `dynamic-text` (`-c 128 -r 100`): 4,368 total / 4,313
decode allocations baseline versus 4,148 total / 4,086 decode for F2 (about 5%
fewer); managed cells during decode fall from 191,056 to 171,568. On
`compressible-a`: 4,100 total / 4,045 decode baseline versus 4,051 total / 3,989
decode for F2; cells during decode fall from 197,184 to 192,240. The helper
library costs seven loading allocations, but the steady-state reduction more
than pays it back on these workloads.

## Feasibility conclusions

- **Code lengths:** a bounded fixed buffer is feasible and sufficient. F2 uses
  one 320-byte-capacity string per dynamic header (extent 336) and fills it in
  place; no 320-cell `s/+` request occurs.
- **Huffman symbol arrays:** could use the same idea; F2 deliberately does not,
  because the code-length failure is the actual failure and the symbol strings
  already fit in the tested schedules.
- **Sliding window:** a 32 KiB byte window is not feasible in the 64K managed
  heap by any representation; keep the bounded-window error unless a memory
  profile is approved.
- **Zero allocation:** not achievable in the current Glon API for every byte.
  Immutable STRING! is the only language-level byte store; output/read framing,
  window segments, closures and table symbol strings still allocate. A fully
  zero-allocation steady state needs a mutable byte-region facility and a
  caller-provided output buffer (separate implementation phase).
