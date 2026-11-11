# F1 — remaining DEFLATE conformance failure

## Reproduction

```
make -C experiments/glon-deflate test
```

Current result (also reproduced with the diagnostic command below):

- unit tests: **44/44 pass**;
- conformance: **479/480 pass**;
- failing case: `multi-memlevel1`, schedule `c128/r100`;
- expected zlib output: **9,000 bytes**;
- actual prefix before host failure: **4,047 bytes**;
- failure class: host allocation fail-stop, not a wrong-byte decode.

Focused diagnostic command:

```
cd experiments/glon-deflate
GLON_F1_DIAG=1 build/dzhost -L ../.. -c 128 -r 100 \
  -o /tmp/f1.out build/fixtures/multi-memlevel1.deflate
```

Three repeated direct runs produced the same failure and the same heap state.

## Root cause

The managed heap is a **non-moving first-fit heap** with a sweep-time
adjacent-free coalescer (`r0_s1_runtime.c`, `emit_collect`/`emit_alloc`). The
failing allocation is a **GC_KIND_STRING** request:

```
request_extent = 320 cells
request_kind   = 6 (GC_KIND_STRING)
largest_free   = 304 cells
free_cells     = 2,944 in 19 blocks
live_cells     = 4,288 (87 live objects: 59 closures, 19 strings,
                        3 contexts, 6 blocks)
```

The allocator cannot place the 320-cell extent because the largest contiguous
free block is 304 cells. Total free space is not the issue; **external
fragmentation** is. The heap dump shows no free block >= 320 and no adjacent
free blocks that the sweep failed to coalesce. The 19 free blocks are separated
by live objects.

The failing request is consistent with the string library's rounded
allocation in `demo/shop/strings.glon` (`s/raw-cat`, the
`(length + 16) / 16 * 16` payload calculation): a payload rounded to 304 cells
plus the 16-cell managed header requires 320 cells. Two free blocks are exactly
304 cells, one short of that extent. (The kind and size identify the string
allocation class; the fail-stop return address is 13,604, within the generated
code range `code_end=14,341`, but no symbol map is available for that code.)

## Alternatives eliminated

- **Heap exhaustion:** no; 2,944 free cells remain.
- **Stack limit:** no; the stack sentry did not fail.
- **Coalescing bug:** the sweep's free list has no adjacent free blocks; the
  heap dump lists all gaps.
- **Wrong-byte decode:** no; the host fails on allocation after producing a
  prefix, and other schedules produce all 9,000 bytes.
- **Simple allocator policy fix:** a best-fit experimental allocator still
  failed (request 304 / largest 288 in that run); a no-split allocator failed
  earlier with the heap full. Neither is a general fix.
- **GC leak/exhaustion:** not established. The live set at failure (4,288
  cells, max_live 4,368, 128 collections) is below the successful schedule's
  observed max_live (4,960); the failure is placement, not total live size.

## Why no code fix was committed

A correct fix requires changing the memory model or object representation, not
a small allocator policy tweak:

1. **Compaction/moving GC** would have to update every managed pointer and
   every raw pointer held across allocation in masm/library code (e.g. the
   string concatenation scratch registers). The current heap and libraries
   explicitly rely on non-moving objects; this is a large, high-risk change.
2. **Larger/segmented heap or a reserved large-string region** would be a
   memory-layout / capacity decision and is outside F1's constraints.
3. **Exact string allocation / compact string headers or chunked strings**
   would change the existing string representation/library ABI and require
   decoder/library changes, which F1 forbids.
4. Best-fit/no-split policy experiments did not solve it and are not a general
   correctness fix.

No heap size, allocation limit, expected result, fixture, schedule or decoder
was changed. No speculative fix was committed.

## Consequences / measurements

- Before: conformance 479/480; unit 44/44.
- After: N/A (no fix).
- Failure peak live: 4,288 cells; total free at failure: 2,944 cells in 19
  blocks; largest free: 304 cells; requested extent: 320 cells; GC count:128;
  successful-schedule observed max_live:4,960 cells.
- Execution/throughput before vs after: N/A (no fix).
- The failing case is deterministic under the reported schedule.

## Proposed general remedy

To reach 480/480 without hiding the problem, a separate approved memory-model
change is required. The least disruptive direction is likely:

- a **size-class/large-object allocator** with a reserved contiguous string
  region, or
- **chunked/rope strings** so no single contiguous 320-cell string is required,
  or
- a **moving collector** with a complete pointer/root audit, including raw
  masm scratch pointers.

Each is a design/ABI change, not an F1 patch. The fragmentation failure should
remain a known blocker until one is approved and validated.

## Changed files

- `experiments/glon-deflate/dzhost.c` — gated `GLON_F1_DIAG` heap dump only;
  default behavior is unchanged.
- `experiments/glon-deflate/tests/f1-reproduce.sh` — focused reproduction.
- `experiments/glon-deflate/tests/f1-heap-dump.txt` — captured diagnostic.
- this report.

No runtime, frozen S1, production host ABI, DEFLATE decoder or application
source was changed.
