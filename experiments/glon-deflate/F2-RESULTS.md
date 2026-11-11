# F2 results — DEFLATE without managed-string dependence

## Result

**480/480 conformance runs pass.** The formerly failing case
`multi-memlevel1`, `c128/r100`, produces 9,000 bytes and matches zlib byte for
byte. The fix is a bounded code-length buffer used only during dynamic Huffman
headers, not a decoder rewrite, heap increase, GC change, new VM primitive or
host ABI change.

Verification:

```
make -C experiments/glon-deflate test
  unit tests: 44 passed, 0 failed
  DEFLATE conformance: 480/480 runs pass
  by class: malformed 222/222, ok 230/230, truncated 24/24, window-limit 4/4
  by schedule: c128/r100 164/164, c1/r1 34/34, awkward 44/44,
               c1/r4096 44/44, c128/r65536 44/44, eager 30/30, c2/r1 120/120

./check-frozen-s1.sh: OK
```

The focused original schedule also passes all four variants:

```
python3 -I experiments/glon-deflate/tests/run_tests.py \
  --only multi-memlevel1 -j 1
RESULT PASS (4/4 runs pass)
```

## Answers to the architectural questions

1. **Why does the existing decoder allocate managed strings?** STRING! is the
   only ordinary Glon byte store: one byte per managed cell. The old dynamic
   header accumulated `dz-ls` with `s/+`, so every added length made a new
   immutable string; the failure was a 288-byte request rounded to a 320-cell
   contiguous allocation.
2. **Which allocations are essential?** The code lengths, tables, state and
   output bytes are essential data. The specific *managed, growing, contiguous
   concatenation* is not. F2 removes that growth for code lengths. Table symbol
   strings, window segments, output/read strings and closure/context objects
   are still managed and are not required to be contiguous by DEFLATE itself.
3. **Can the seven-primitive machine support a bounded streaming decoder?**
   Yes. The decoder already has bounded state and resumable units. `f2buf.glon`
   uses only `alloc`, `@` and `!` through the existing masm trapdoor; no eighth
   primitive is needed.
4. **Is the 320-cell allocation necessary?** No. The same 288 code lengths fit
   in a fixed-capacity buffer, and the algorithm only needs the byte sequence
   plus its length.
5. **Can the decoder operate with fixed scratch and caller-provided output?**
   The header path can use fixed scratch (F2 does). The complete decoder cannot
   yet use caller-provided output without an API/storage change: `dz/read`
   returns STRING! and still assembles output strings.
6. **What host services are required?** None new for F2. The existing feed,
   read, finish, progress and output-frame host calls suffice. A future
   zero-allocation output design would need a caller buffer contract, but that
   is not a new VM primitive.
7. **Does the memory map permit the proposed buffers?** The F2 code-length
   buffer is 320 payload cells (336 cells including header) per dynamic header,
   and is garbage after the tables are built. This fits the 7,232-cell heap in
   all 480 runs. A 32 KiB history does **not** fit: one byte/cell needs 32,768
   cells, already four times the managed heap; packed 3-byte/cell still needs
   about 10,944 cells. The current 768-byte window remains a 64K resource limit.
8. **Would the solution generalise?** The bounded mutable scratch pattern does
   generalise to other streaming parsers and header/table builders. It is not a
   complete general replacement for all string use; a window/output redesign
   is still needed for zero-allocation streaming.
9. **Is a language-semantics change required?** No. F2 adds an experiment-local
   helper library; Glon syntax, values, GC semantics and the frozen S1 are
   unchanged.
10. **Does the twelve-operation candidate add benefit?** Not for this problem.
   CALL/RET/>R/R>/R@ are control/stack operations. They may reduce execution
   instructions or code size, but they do not change byte representation,
   allocation, contiguity or the 32 KiB window limit. The F2 failure is a
   representation problem, not a primitive-selection problem.

## Exact outcome versus F1

- The failed 320-cell request was `s/raw-cat` (`demo/shop/strings.glon:30`)
  called from the dynamic-header code-length accumulation in `inflate.glon`
  (original line 335).
- The inputs were a 287-byte code-length string and a one-byte code length
  (0x14 = length 4 + 16); 287 + 1 = 288, rounded payload = 304, header = 16,
  total = 320.
- It was temporary Huffman-header data, not compressed input, output, the
  window or a table used for the output bytes.
- F2 replaces that growth with a fixed 320-byte-capacity buffer per header
  (`f2buf.glon`, `fb/make`/`fb/set-length`/`fb/append-fill`) and range-based
  `hf/build` calls. The buffer is local to `dz/dynamic` and becomes garbage
  after the literal/distance tables are built.

## Memory and allocation measurements

Measured with `-DR0_S1_PROFILE`; counters are summed across dispatches because
each `r0_s1` run resets them. "Decode" is after the `open` dispatch; "total"
includes library loading and open.

### The failing workload (`multi-memlevel1`, c128/r100)

| Metric | F1 baseline | F2 prototype |
|---|---:|---:|
| Output | 4,047 then host fail | **9,000 (zlib match)** |
| Total managed allocations | 7,254 | 13,025 |
| Allocations during decode | 7,199 | 12,963 |
| Managed cells allocated during decode | 306,336 | 455,104 |
| Decode allocations per output byte | 1.78 | 1.44 |
| Decode cells per output byte | 75.7 | 50.6 |
| Peak live managed cells | 4,368 (before fail) | 4,832 |
| GC count | 128 (before fail) | 185 |
| `max_rs` | 3,864 | 3,548 |
| Full run | 5.62 s to failure | 11.26 s to 9,000 bytes |

The after-run is longer because it completes the stream; the before-run stops
at 4,047 bytes. This is not a fair speed comparison. Per byte produced, F2
allocates fewer managed objects and cells (1.44 vs 1.78 allocations/byte, 50.6
vs 75.7 cells/byte).

### Same already-passing workloads

| Fixture | Total allocs base/F2 | Decode allocs base/F2 | Decode cells base/F2 | Peak live base/F2 | GCs base/F2 | Run ms base/F2 | Out KB/s base/F2 |
|---|---:|---:|---:|---:|---:|---:|---:|
| `dynamic-text` c128/r100 | 4,368 / 4,148 | 4,313 / 4,086 | 191,056 / 171,568 | 4,336 / 4,464 | 82 / 79 | 7,299 / 7,669 | 1.20 / 1.15 |
| `compressible-a` c128/r100 | 4,100 / 4,051 | 4,045 / 3,989 | 197,184 / 192,240 | 4,352 / 4,464 | 115 / 118 | 3,792 / 3,868 | 5.15 / 5.05 |

The helper library adds seven loading allocations, but F2 allocates slightly
fewer decode objects and fewer decode cells on both workloads. F2 adds no
measured speed claim: the two passing workloads are about 2–5% slower in this
profiling build, and the full 480-run suite took 354–468 s in two final runs
versus 348 s for the F1 baseline. The wall-clock variance in this environment
is larger than the apparent difference; the per-fixture medians above are the
more useful comparison.

For reference, Python/zlib on the same compressed bytes with 128-byte input
chunks took about 0.033 ms for `dynamic-text` and 0.057 ms for `compressible-a`;
that is a C-library one-shot/streaming oracle, not a comparable VM benchmark.

**Unavailable measurement:** the host exposes allocation, GC, stack and native
counts, but not a retired VM-instruction counter. No instruction-count or
throughput improvement is claimed.

## Correctness evidence

- 44/44 unit tests, including iterator/resumption, overlap, stored blocks,
  errors, two independent decoders and the read cap.
- 480/480 conformance runs; all byte comparisons use zlib, never another Glon
  run. Fragmented input/output schedules, eager feeding, c1/r1, awkward splits
  and c128/r65536 all pass.
- Original failure schedule `multi-memlevel1` at c128/r100 passes unchanged.
- `fb/append-fill` and the new `dz/ls-add` guard preserve the `'repeat` error
  for malformed overlong runs; `bad-repeat-overflow` now terminates with
  `ERROR repeat` instead of overrunning the buffer.
- No heap-limit, expected-output, fixture or schedule change.

## Remaining risks

- The fixed buffer is 320 payload cells (336 with header) and is allocated once
  per dynamic header. A more fragmented future workload could fail that
  allocation; the tested 480-run corpus does not.
- The helper is unchecked internal masm; `dz/ls-add` checks the `dz-tot`
  overshoot but callers must continue to respect the 320-byte capacity.
- Table symbol strings, window segments, output/read strings and closures still
  use managed allocation. F2 removes the failure mechanism, not all managed
  strings.
- The 32 KiB RFC window remains impossible in the 64K heap; a full-window
  decoder needs an approved memory profile.
- `f2buf.glon` adds about 208 cells to `code_end` (15,003 versus 14,795 in the
  baseline build) and is loaded before `inflate.glon`.
- No production, frozen-S1, B1–B8, C1 or host-ABI change is proposed.

## Further implementation phase?

**No further phase is required for 480/480.** If the goal becomes
zero-allocation steady-state decoding or a full 32 KiB window, that is a
separately approved architecture phase: a mutable byte-region/output-buffer
contract (B/C) and, for 32K, a new memory profile. It should not be bundled
with this fix.
