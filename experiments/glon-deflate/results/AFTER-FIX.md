# Results after the GC repair (2026-11-09)

The other files in this directory were recorded **before** the collector repair
and are kept unchanged. This rerun uses the repository runtime **with** the
object-start check (`proposal/gc-object-start-check.patch`, now applied in
`r0_s1_runtime.c`). The decoder is unchanged.

## What was run

| Check | Result |
|---|---|
| `make repro` (`tests/gc-interior-repro.glon`) | `intact` (was `corrupted at 0 ...`, byte 11) |
| `make unit` | 44 / 44 |
| DEFLATE corpus (`conformance-after-gc-repair.txt`) | **479 / 480** |

The corpus breaks down as follows:

| Class | Runs passing |
|---|---|
| ok | 229 / 230 |
| malformed | 222 / 222 |
| truncated | 24 / 24 |
| beyond window | 4 / 4 |

## The remaining failure

`multi-memlevel1` under `c128/r100` is the heap-fragmentation fail-stop already
recorded before the repair:
- it stops after 4,047 correct bytes;
- a dynamic header needs a string of about 300 contiguous cells;
- the non-moving heap had 2,944 cells free, but in 19 holes.

It is a resource limit of the 64K machine, not a collector defect, and it is
not addressed here.

## What the repair removed

Before the repair, the unchanged runtime also failed `multi-wbits9` under the
`awkward` schedule. That run stopped in a collector fail-stop, a T_BLOCK range
check, after 2,284 correct bytes.
