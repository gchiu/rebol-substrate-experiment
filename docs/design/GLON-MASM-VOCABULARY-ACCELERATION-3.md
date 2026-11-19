# MASM-3 — profile-guided vocabulary acceleration

**Status:** completed isolated experiment. No frozen S1 file, HOST id, parser,
evaluator, GC, M1 scheduler, IO-3 protocol/chunk size, published vocabulary, or
MASM-1/MASM-2 baseline was changed.
**Branch:** `experiment/masm-vocabulary-3`
**Base:** `4a054ec7a9912f52c9c18a6bf2cfc210557b7de5` (MASM-2)
**Implementation:** `experiments/masm3/`

The selected operation is `s/slice2`, a new reusable immutable-string
operation that slices the virtual concatenation of two strings without building
the concatenation. In an experimental streaming copy it removes the
intermediate carry+chunk string and, together with a zero-carry fast path,
reduces matched/boundary streaming by about 8–10% at 1 MiB and 7–9% at 4 MiB.
The 1 MiB no-match case is neutral (about ±2%), because the remaining cost is
dominated by garbage collection, not by one vocabulary operation.

This is a positive but limited result, and the negative part is engineering
evidence: **the collector, not any remaining string primitive, is the dominant
callback cost.** Merely loading a library that adds ten live global closures
raised the same 64 KiB streaming callback from 1.13 s to 1.34 s (+18%) and GC
instructions from 191.5 M to 241.8 M (+26%) with *identical* allocations.

---

## 1. Restored context

The repository was checked before work: branch `experiment/masm-vocabulary-3`,
HEAD and merge-base both `4a054ec…`, clean working tree. The design reports for
IO-3, the HOST investigation, MASM-1, IO-4 and MASM-2 were read, and the
OpenViking Glon engineering-memory collection was checked. The previously
requested memory synchronisation had completed: `io-masm-milestones.md` and
`masm-vocabulary-acceleration.md` contain MASM-2 at `4a054ec`, and the README
indexes the track through MASM-2. Git remains authoritative; the new entries
are added at the end of this experiment.

---

## 2. What the profile actually says

Two matched profiles were taken with a copy of the IO-4 driver
(`experiments/masm3/profile_stream.c`), the instrumented S1 counter
(`s1_prof.c`), and a measurement-only runtime copy that exposes the exact GC
code range (`experiments/masm3/r0_s1_runtime_geo.c`). `-O2` profiling build,
same host state, same inputs.

### 2.1 1 MiB no-match, MASM-2

| Stage | Seconds / count |
|---|---:|
| Reply callback/state run | **18.987 s** |
| Worker wait/join (1 ms polling) | 5.852 s |
| Worker submit | 0.562 s |
| Request extraction | 0.111 s |
| Parser | 0.051 s |
| Reply source build | 0.093 s |
| Requests | 5,250 |
| Host calls | 76,074 |
| GC collections | **1,311** |
| Managed allocations | 65,601 |
| Allocated cells | **5,622,800** |
| Post-collection live | 2,864 cells |
| Total S1 opcodes | 4.291 B |
| Opcodes in the GC region | **3.101 B (72.3%)** |
| Opcodes in `alloc` | 0.171 B (4.0%) |

The historical MASM-2 report measured a 26.894 s callback under different host
load. The *share* is the stable result: GC is about 72% of executed callback
instructions, and `alloc` about 4%. The 64 KiB profile shows the same shape
(191.5 M of 267.2 M opcodes, 71.7%).

### 2.2 The GC cost depends on live roots, not only on allocation volume

This was measured directly. Variant A is MASM-2. Variant B is the same MASM-2
code with `experiments/masm3/slice2.glon` loaded but **never called**. Inputs,
requests and allocations are identical.

| 64 KiB no-match | A: MASM-2 | B: + full slice2 library |
|---|---:|---:|
| Callback run | 1.130 s | 1.335 s (+18%) |
| GC collections | 81 | 82 |
| Managed allocations | 4,151 | 4,151 |
| Allocated cells | 353,328 | 353,328 |
| GC-region opcodes | 191.5 M | **241.8 M (+26%)** |

The full library adds reference/error/test helper closures as permanent global
roots. The deployment-minimal `slice2-min.glon` keeps one worker and one checked
wrapper. In the MASM-3 streaming run this alone changed the 64 KiB callback
from 1.235 s (full library) to 1.107 s (minimal library). A reusable vocabulary
operation therefore has a **live-root cost per collection** in addition to its
call cost. That cost must be measured, not assumed.

### 2.3 Controlled per-operation costs

`experiments/masm3/opcost.c` runs one parsed expression repeatedly and reports
wall time, S1 opcodes, HOST calls and GC. The 400-byte cases allocate a result
on every call; their wall cost therefore includes amortised collection.

| Operation, representative size | Wall per call | GC per call | Note |
|---|---:|---:|---|
| empty `[ 0 ]` (session overhead) | 5.4 µs | 0 | subtract for net cost |
| `block-at b 3` | 9.6 µs | 0 | already MASM |
| `block-len b` | 8.4 µs | 0 | already MASM |
| `s/length s` | 21.5 µs | 0 | `s/raw-length` alone is 10.2 µs |
| `s/slice s 0 400` | 0.99 ms | 0.30 | checked wrapper |
| `s/raw-slice s 0 400` | 0.91 ms | 0.30 | wrapper overhead ≈ 8% |
| `s/+ carry s400` | 0.81 ms | 0.30 | already MASM |
| `mk-string reduce [65]` | 0.136 ms | 0.04 | |
| `reduce [a b]` / `[a b c]` / `[a b c d e]` | 0.057–0.061 ms | 0.02 | |
| `io/u32` ordinary / MASM-2 | 0.892 / 0.064 ms | 0.225 / 0.02 | MASM-2 baseline |
| `io/handle-field` ordinary / MASM-2 u32 | 1.955 / 0.260 ms | 0.47 / 0.06 | |
| one request+reply cycle, ordinary / MASM-2 u32 | 3.19 / 0.742 ms | 0.75 / 0.14 | |
| no-match callback fragment (cat + two slices) | 1.86 ms | 0.60 | |

These are measured, not estimated. They show why another wrapper fusion is not
the answer: `s/slice`'s checks are only about 8% of a 400-byte call, while the
allocation/collection part is the rest. `block-at`, `block-len`, `s/length` and
`reduce` are microseconds against a millisecond-scale callback, and `s/+`,
`block-at`, `mk-string` and the raw slice/cat copies are already MASM.

### 2.4 Ranked hotspots (evidence order)

1. **GC mark/sweep:** ~72% of callback opcodes; 1,311 collections per 1 MiB
   no-match; cost rises sharply with the number of live global closures.
2. **Callback string allocation/copy:** ~1,058 cells per request at 64 KiB
   (combined ≈ 416, safe ≈ 416, carry ≈ 16–32, request record/fields/handle/u32
   ≈ 224); ~1,071 cells per request at 1 MiB.
3. **Worker wait:** 5.85 s per 1 MiB no-match (about 23% of the profile total);
   explicitly out of scope (IO-5).
4. **Request construction/reply state machine:** remaining allocation and
   `block-at`/`reduce`/closure work; already partly MASM-2.
5. **Search and evaluator/binding overhead:** visible but not separable from
   the state-machine callback without a second operation.
6. **`block-at`, `s/length`, `s/slice` wrapper checks, `reduce`:** individually
   small according to §2.3.

---

## 3. Candidate selection

Candidates considered were `block-at`, `s/length`, `s/slice`, residual `s/+`
and `mk-string`, `reduce`, `io/request`, and a new variable-length slice.

- `block-at`, `s/length`, `s/+`, `mk-string` and the raw slice/cat copies are
  already MASM. Their checked wrappers add measurable but small overhead.
- Fusing `s/slice` checks into a worker would remove about 8% of one call. It
  would not remove the allocation that drives GC, so it was rejected as the
  primary operation.
- `reduce`/request blocks are already fast individually; replacing request
  bookkeeping with globals is an application-specific composition change, not a
  reusable vocabulary operation. It is noted as a follow-up, not implemented.
- The dominant removable allocation in the no-match callback is the
  **materialised carry+chunk string** (~416 cells per request). Extracting
  `safe`, `next-carry`, `prefix` or `suffix` from the two sources directly
  removes it.

Selected operation: **`s/slice2 a b start n`**, the n bytes at offset `start`
of the virtual concatenation `a ++ b`. It is reusable (any window across two
adjacent pieces), variable-length, immutable, and implementable with the seven
S1 instructions and the existing allocator. The experiment also tests the
ordinary-composition change that makes it useful in streaming: when the carry
is empty, write the chunk directly instead of copying it through `s/+`.

---

## 4. Contract and implementations

`experiments/masm3/slice2.glon`:

- `s/slice2-glon a b start n` — ordinary reference: validate, then
  `s/slice (s/+ a b) start n` (two allocations).
- `s/raw-slice2 a b start n` — unchecked MASM worker.
- `s/slice2-masm` — checked public wrapper; `s/slice2` is selected to it.
- `slice2-min.glon` — deployment-minimal worker + checked wrapper, used by the
  streaming experiment to avoid extra helper roots.

Contract:

- `a`, `b` are STRING!; `start`, `n` are INTEGER!.
- `0 <= start`, `0 <= n`, `start <= len(a)+len(b)`, and
  `n <= len(a)+len(b)-start`. The last form is used instead of `start+n <= …`
  so a huge tagged `n` cannot overflow the comparison.
- Result is a **new** STRING! of exactly `n` bytes; neither input changes.
- `n = 0` returns a new empty string.
- Wrong type raises `SIN! 'type 's/slice2` with the offending value; a bad range
  raises `SIN! 'range 's/slice2 [start n]`. The reference and MASM wrappers use
  the same classification.

The MASM worker copies directly in two phases (a-part, then b-part) so the
inner loop is a tight byte copy. Encoded size of `s/raw-slice2`: **674 cells /
5,392 bytes**.

---

## 5. Variable-length managed-value investigation

MASM-2 proved a *fixed* four-byte string. `s/slice2` needs a variable extent,
so each item was checked:

| Concern | Finding / rule used |
|---|---|
| Size calculation | `ceil((n+1)/16)*16 + 16` cells. The first term is the payload (`length` + `n` bytes, 16-aligned); the last 16 cells are an untraced workspace inside the same allocation. |
| Length validation / overflow | Wrapper checks non-negative and `n <= total-start`; the worker trusts the checked wrapper. A raw unchecked call with an invalid `n` is outside the contract (as other raw workers). |
| Source rooting across allocation | `a` and `b` remain on the data stack through `CALL alloc`; the collector scans the active data stack. |
| Scratch across allocator execution | The allocator uses `GC_A1/A2` and `GC_T*`, not `SCRATCH_A/B/C/F`. The worker keeps `n`/`start` in `SCRATCH_F/B` across the call. |
| Destination rooting | The result is **not** pushed before filling. This is safe only because there is exactly one allocation and no allocation, `HOST`, yield or call after it, so no collection can run during the fill. The result is pushed after the fill. |
| Payload initialisation | Length is written first; every one of the `n` bytes is written; the tail workspace is local and untraced. |
| GC during allocation | Only tagged integers are held in scratch at that point; managed sources are on the data stack. |
| Lifetime after return | An ordinary managed STRING!; no reference to the workspace or any raw pointer survives the return. |
| Cooperative tasks | Tested under M1 `spawn`/`yield`/`run-tasks` (§7). The worker never yields or allocates after the first allocation. |
| Maximum size | Measured 2,048-byte result succeeds; 2,500 bytes fails with a heap fail-stop in the test harness. The fixed ~5k-cell managed heap, the two sources and the 16-cell tail bound this. The tail reduces the maximum by 16 cells versus a minimal allocation. |

The trailing-workspace technique is an extension of the existing
`s/raw-scan-set` convention (a length-0 string used as a workspace), but fused
into the result allocation. It depends on the current string/header/GC layout:
the declared length is less than the allocated extent, and the collector traces
no string contents, so the extra cells are never interpreted. It is not a
general representation guarantee; a future string-layout change must re-audit
it.

No new HOST service and no raw managed-object construction outside the
documented `alloc`/`GC_KIND_STRING` path are used. Allocation failure remains
the existing machine fail-stop.

---

## 6. Streaming integration

`experiments/masm3/file-stream-masm3.glon` is an experimental copy of the
MASM-1 streaming copy. It keeps the MASM `s/find`, MASM-2 `io/u32`, the 400-byte
protocol, one outstanding request and the same error/cleanup paths.

Two changes:

1. **Zero-carry fast path.** When the carry is empty (the first chunk, and
   every chunk after the replacement has been emitted), write `chunk`
   directly. The old code built `s/+ "" chunk`, copying the whole chunk only to
   write it.
2. **No materialised carry+chunk.** Before replacement, search a small boundary
   string (`carry ++ chunk[0..patlen-2]`) for matches starting in the carry,
   then search the chunk. `safe`, `next-carry`, `prefix` and `suffix` are
   extracted from the two sources with `s/slice2`. When the carry is empty the
   ordinary `s/slice` is used.

IO-3, MASM-1 and MASM-2 files are untouched. Exact output bytes and unchanged
source files were checked in every run.

---

## 7. Tests

`experiments/masm3/slice2_cases.py` (all pass):

- 169 differential cases: empty/single-byte, every split position, n = 0,
  16-cell rounding boundaries (14–17, 31–33, 399–401), arbitrary binary bytes
  including NUL, start at both source boundaries, and 60 random cases.
  Python-generated expected bytes are compared with both the ordinary reference
  and the MASM wrapper.
- Selected alias is the MASM implementation.
- Invalid types, negative/overflowing ranges and out-of-range starts raise
  `SIN!`; both wrappers classify identically.
- 2,048-byte result probe passes; 2,500-byte probe is the measured heap limit.
- Result and an unrelated sentinel survive 120 subsequent string allocations
  with collection reporting enabled; both sources are unchanged.
- Two cooperative tasks each call `s/slice2`, yield, allocate under pressure,
  yield again, and read back their results; both verify exactly. This is a
  cooperative-context test only: the worker does not yield and arbitrary
  preemption/reentrancy is not claimed.

Regression runs (actual results):

| Check | Result |
|---|---|
| `./check-frozen-s1.sh` | PASS (hash `f90496c…`) |
| `make glon-smoke` / `MODULES=file` | PASS |
| `make io-file-test` | 30 passed, 0 failed |
| `make io-file-stream-test` | PASS, 0 failures |
| MASM-1 `cases.py` | 221/221 differential, immutability, invalid type |
| MASM-2 `u32_cases.py` | 217/217, allocation pressure |
| IO-4 loader lifetime | PASS |
| New MASM-3 tests | all PASS (above) |
| Non-simulation native runner | 948 checks, 0 failures |
| `make wasm-test` | not run: `emcc` is unavailable in this environment |
| Full `./s1` | not used as a pass claim; it stalls in the documented long traffic/Newell/OVM simulations |

---

## 8. Benchmarks

All timings are WSL2 and host-load sensitive; matched runs are the reliable
evidence. `glon` was built `MODULES=file`; `-O0` unless stated.

### 8.1 Standalone `s/slice2`

`slice2_bench.py`, 201 calls on 400-byte sources and a 400-byte result,
subtracting a one-call setup:

| Build | `s/slice2-glon` | `s/slice2-masm` | `s/raw-slice2` | MASM vs reference |
|---|---:|---:|---:|---:|
| `-O0` | 5,898 µs | 2,020 µs | 1,810 µs | 2.92× |
| `-O2` | 2,352 µs | 800 µs | 660 µs | 2.94× |
| `-Os` | 3,170 µs | 980 µs | 800 µs | 3.23× |

One instrumented call (`opcost-prof`, 400-byte result):

| Implementation | S1 opcodes | HOST calls |
|---|---:|---:|
| `s/slice2-glon` | 88,445 | 12,069 |
| `s/slice2-masm` | 44,121 | 6,506 |
| `s/raw-slice2` | 17,339 | 1,938 |

The checked wrapper is about half of the MASM path's instructions; the raw
worker is 5.1× fewer opcodes than the ordinary reference.

### 8.2 Streaming, matched inputs and exact outputs

`stream_compare.py`. `masm2+slice2min` loads the same extra library as MASM-3
but keeps the MASM-2 streaming copy, separating the live-root cost from the new
operation.

**4 KiB, seconds**

| Case | IO-3 | MASM-1 | MASM-2 | MASM-2+slice2min | MASM-3 |
|---|---:|---:|---:|---:|---:|
| match | 0.283 | 0.327 | 0.202 | 0.220 | 0.199 |
| no-match | 0.803 | 0.354 | 0.209 | 0.229 | 0.232 |
| boundary | 0.323 | 0.293 | 0.181 | 0.216 | 0.197 |

Copy-only: 0.243 s plain, 0.153 s with MASM-2 u32.

**64 KiB, seconds**

| Case | IO-3 | MASM-1 | MASM-2 | MASM-2+slice2min | MASM-3 |
|---|---:|---:|---:|---:|---:|
| match | 2.876 | 3.452 | 1.839 | 1.851 | 1.639 |
| no-match | 12.015 | 4.296 | 2.286 | 2.528 | 2.535 |
| boundary | 2.799 | 3.565 | 1.868 | 1.903 | 1.658 |

Copy-only: 2.337 s plain, 1.268 s with u32.

**1 MiB, seconds**

| Case | IO-3 | MASM-1 | MASM-2 | MASM-2+slice2min | MASM-3 |
|---|---:|---:|---:|---:|---:|
| match | 45.818 | 54.367 | 27.798 | 28.231 | **25.106** |
| no-match | 189.297 | 68.362 | 37.447 | 38.519 | 38.052 |
| boundary | 44.548 | 51.928 | 26.978 | 27.758 | **24.827** |

Copy-only: 35.224 s plain, 19.969 s with u32.

**4 MiB, seconds**

| Case | MASM-2 | MASM-3 |
|---|---:|---:|
| match | 110.428 | **102.237** |
| no-match | 149.531 | 153.014 |
| boundary | 109.572 | **99.571** |

Copy-only 4 MiB: 145.061 s plain, 79.486 s with u32. IO-3 and MASM-1 were not
rerun at 4 MiB; the MASM-2 report's 220.5/283.8 s MASM-1 figures remain the
historical reference.

Interpretation:

- Matched/boundary streaming improves by **8–10% at 1 MiB** and **7–9% at
  4 MiB**; the zero-carry direct write is the main contributor, with the
  allocation reduction from `s/slice2` secondary.
- No-match is neutral to slightly slower (+2% at 4 MiB) when the added library
  is charged to MASM-3. Against `masm2+slice2min` it is neutral at 64 KiB and
  1 MiB. The operation removes allocations but adds copy-loop instructions and
  a second small search; the GC benefit and CPU cost approximately cancel.
- The extra live vocabulary roots are visible: at 64 KiB, `masm2+slice2min` is
  about 11% slower than plain MASM-2 on no-match, and MASM-3 recovers almost
  all of it. This is why the deployment-minimal library exists.

### 8.3 1 MiB profile with the minimal library

| Metric | MASM-2 no-match | MASM-3 no-match | MASM-2 match | MASM-3 match |
|---|---:|---:|---:|---:|
| Callback run (s) | 18.987 | 18.948 | 12.969 | **11.272** |
| Worker wait (s) | 5.852 | 5.905 | 5.890 | 5.813 |
| Submit (s) | 0.562 | 0.576 | 0.538 | 0.499 |
| GC collections | 1,311 | **1,049** | 874 | **656** |
| Managed allocations | 65,601 | 65,599 | 62,985 | 57,740 |
| Allocated cells | 5,622,800 | **4,658,032** | 4,491,008 | **3,274,624** |
| Post-collection live cells | 2,864 | 2,064 | 1,968 | 2,272 |
| Host calls | 76,074 | 128,498 | 39,380 | 42,006 |
| Total opcodes | 4.291 B | 4.212 B | 2.821 B | 2.454 B |
| GC-region opcodes | 3.101 B (72.3%) | 2.757 B (65.5%) | 1.960 B (69.5%) | 1.629 B (66.4%) |

No-match: allocations −17%, collections −20%, callback time flat.
Match: allocations −27%, collections −25%, callback time −13%.
The added HOST calls come from the slice2/setup arithmetic; they are a small
instruction cost compared with the removed allocation/collection work.

---

## 9. Allocation and GC findings

1. **GC is the callback.** About 72% of callback opcodes are in the collector
   for the 1 MiB no-match shape; `alloc` is another ~4%.
2. **Collection cost scales with live roots.** Loading ten unused helper
   closures raised GC opcodes 26% with unchanged allocation. Vocabulary size is
   a performance variable, not a free abstraction.
3. **Reducing allocated cells helps, but not one-for-one.** MASM-3 removes 17%
   of allocated cells and 20% of collections in no-match, yet callback time is
   flat because slice2's copy/setup instructions replace part of the saving.
   Match shows the gain because the zero-carry path removes an entire copy per
   chunk as well.
4. **Request-path allocations remain.** About 224 of ~1,058 cells per request
   are request record/fields/handle/u32 blocks. Reducing them (globals instead
   of a record, cached handle fields, fused handle encoding) is the next
   allocation-composition opportunity, but it is application-specific and was
   not implemented here.
5. **Raw vs checked wrappers are not the lever.** The checked `s/slice` wrapper
   is ~8% of a 400-byte call; GC and copying are the rest.

---

## 10. Updated MASM conventions

For an **allocation-free computational worker** (MASM-1) and a **fixed-size
allocating worker** (MASM-2), the earlier rules stand unchanged.

For a **variable-length allocating worker** (MASM-3), add:

1. Validate length/range and type in the checked wrapper; keep the raw worker
   unchecked and document its preconditions.
2. Use `n <= total - start` (not `start + n <= total`) for range checks when
   the operands are tagged machine integers.
3. Keep managed sources on the data stack across the single allocation; do not
   hold managed values in scratch cells, which the collector does not scan.
4. Hold only integers in scratch across `CALL alloc`; verify the allocator's
   own scratch cells do not overlap `SCRATCH_A/B/C/F`.
5. Perform no allocation, `HOST`, yield or call after the allocation until the
   result is filled; if that rule cannot be met, root the result before any
   further allocation.
6. Prefer a tight two-phase copy over a per-byte source test; encode the phase
   counts before the loop if possible.
7. If a workspace is needed, either use the return stack only when no GC can
   run, or use the documented untraced tail/workspace convention and document
   the extent/layout dependency and the reduced maximum size.
8. Measure the **live-root cost** of every new library definition: helper
   reference/error functions are permanent roots. Ship a deployment-minimal
   library when the helpers are not needed at run time.
9. Test empty values, all split positions, binary content, rounding
   boundaries, invalid types/ranges, maximum size, repeated calls, allocation
   pressure, result/source survival across collections, unrelated live objects,
   and cooperative-task use.

Unchanged: MASM workers are non-reentrant and non-yielding; no new HOST
services; allocation failure is a machine fail-stop; representation/layout
dependencies remain explicit.

---

## 11. Remaining architectural blockers

- **GC cost and live-root sensitivity** dominate the remaining callback. There
  is no single vocabulary operation that removes that cost; it needs an
  allocation/GC or reachability experiment (not a capacity change in this
  task).
- **Worker polling** is still ~5.85 s per 1 MiB no-match. IO-5 owns that.
- **Request-path allocation** (~224 cells/request) remains and is
  application-specific.
- **Maximum variable result** is bounded by the fixed managed heap: 2,048 bytes
  succeeds, 2,500 bytes fail-stops in the test harness. The 16-cell tail
  workspace reduces the limit slightly.
- **No preemptive/reentrant claim.** Only cooperative, non-yielding task use
  was tested.
- **No WASM run** because `emcc` is unavailable here; no Windows/embedded file
  backend.
- **Long simulation suites** still stall; the non-simulation 948-check runner
  passes.

---

## 12. Recommendation

1. **Do not add more vocabulary primitives for the no-match path without a
   measured allocation/GC target.** `s/slice2` is a useful reusable operation
   (3× standalone; matched/boundary streaming −8–10%), but it does not move the
   no-match callback.
2. **Proceed with the independent IO-5 task** (replace 1 ms polling with a
   completion event/condition variable) before another MASM experiment; it is
   the largest non-GC measured cost and does not touch frozen S1.
3. **Then run a focused allocation-composition experiment** on the request/
   reply path (record block, cached handle fields, fused handle encoding) and
   a GC live-root measurement, not another string primitive.
4. Keep IO-3/MASM-1/MASM-2 baselines unchanged and do not integrate MASM-3
   into the published module without review.

Stop for architectural review. No push, merge, tag or deployment was
performed.
