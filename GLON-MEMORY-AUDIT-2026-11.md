# Glon Reference Safety and Constrained-Memory Audit — November 2026

**Status:** investigation only. No runtime, GC, allocator, language, application or
deployment change is authorised. S1-7 remains the frozen reference and
production default; S1-12 remains the optional experimental native profile.

**Scope:** determine whether the existing non-moving memory architecture is
safe and sufficient for constrained nodes, and what the smallest justified
change would be. This document separates source-verified facts, measured
results, estimates and unresolved questions.

## 1. Current architecture (source-verified)

### Memory map and objects

- The machine is a fixed 65,536-cell array (`MEM_CELLS`, `s1.h`); `M` is
  `extern cell M[MEM_CELLS]` (`r0_s1.h`). The managed heap is
  `GC_HEAP_BASE=32768` to `GC_HEAP_LIMIT=40000` = **7,232 cells**
  (`r0_s1.h`; `GLON-ARCHITECTURE-MEMORY.md`).
- Heap objects have a 16-cell header stride: size at `h+GC_HDR_SIZE`, flags at
  `h+GC_HDR_FLAGS`, and kind in the flags (`GC_HDR_STRIDE=16`, `GC_KIND_*`,
  `GC_MIN_EXTENT=32`; `r0_s1.h`). Object payload starts at `h+16`.
- Tags are the low four bits of a cell (`r0_s1.h`: `T_BLOCK`=6, `T_CONTEXT`=7,
  `T_CLOSURE`=8, `T_USER`=11, `T_STRING`=12, etc.). `mk_*`, `r0_tag` and
  `r0_untag` show that a tagged object value contains the **raw payload
  address**, plus a tag. There is no handle table or object index.

### Allocation

`emit_alloc` (`r0_s1_runtime.c`) calculates `extent = n + 16`, scans the heap
from `GC_HEAP_BASE` to the current `REG_HP` frontier for a free block of at
least `extent` (first fit), otherwise bumps the frontier. If neither succeeds,
it calls `emit_collect` once and retries; a second failure is a machine/host
fail-stop. A free block is split if the remainder is at least 32 cells. There is
no explicit linked free list; free blocks are implicit in the header sequence.

### Collection and coalescing

`emit_collect` is a **stop-the-world, exact, non-moving mark/sweep** collector:

- roots: `GC_GLOBAL_CTX`, `RV_CTX`, `RV_CHILD`, `RV_CLOSURE`, `RV_FRAME`,
  built-in descriptors, active data/return stacks, task records and saved task
  stacks;
- tracing: contexts, closures, USER, STRING and BLOCK (`emit_trace_*`);
- mark worklist capacity is 256 (`GC_WORKLIST_CAP`); overflow is a fail-stop;
- sweep clears marks, frees dead objects and **coalesces adjacent free runs**
  in the same sweep (`emit_collect`; no separate free-list structure).
- counts are exposed as `GC_LIVE_CELLS`, `GC_LIVE_OBJS`, `GC_FREE_BLOCKS`,
  `GC_FREE_CELLS`; there is no moving/forwarding step.

Allocation failure is a machine fail-stop, not a recoverable Glon exception.

### Ownership and roots

- The runtime/GC owns managed objects. Loader-heap objects are permanent and
  outside managed GC. There is no manual free or pinning API.
- Data-stack values are expected to be tagged Glon values at allocation
  safepoints; `emit_alloc` records this GC-safepoint invariant explicitly.
- The return-stack scanner (`emit_scan_closures`) treats heap-resident cells
  with a closure header as closure references; code addresses and non-heap
  values are not managed objects. Raw/untagged return-stack entries need an
  explicit audit before relocation.
- `RV_CTX`, `RV_CHILD`, `RV_CLOSURE`, `RV_FRAME`, `GC_GLOBAL_CTX` and built-in
  descriptors are explicit roots. Other `RV_*` and scratch cells are not
  automatically roots.

### Native versus WASM

Both targets use the same S1/runtime and 65,536-cell map. `cell` is
`intptr_t`: 64-bit native, 32-bit WASM (`s1.h`). Managed references are cell
addresses, not host pointers. Native hosts can inspect `M`; browser hosts receive
copied bytes/HTML/canvas buffers. `GLON_LIVE` builds source text from input
bytes and copies result bytes (`r0_s1_g1a_live.c`); `standalone/glon.c` exposes
result buffers, not managed objects. S1-12 is native-only.

## 2. Reference inventory and relocation classification

Legend: **R** relocatable (needs rewriting/root map if objects move), **P**
requires pinning, **D** requires redesign, **U** unresolved. A category's
current GC traceability is stated separately; “R” does not mean the current
collector can relocate it.

| Reference category | Created/stored | Current GC visibility | Moving-target effect | Class |
|---|---|---|---|---|
| Tagged Glon values on data stack | evaluator results, literals, arguments; `M[SP..]` | `emit_scan_values` from SP to DS top | Update tagged payload addresses | **R** |
| Tagged values in globals/contexts/closures/blocks | `mk_*`, context/closure allocation; trace fields | `emit_trace_*`, `RV_CTX`/`RV_CHILD`/global roots | Update traced fields | **R** |
| Return-stack closure pointers | `asm_call`/`asm_toR`; saved activations | `emit_scan_closures` checks heap, alignment, kind and object start | Update closure addresses; code addresses stay | **R** (closures) |
| Return-stack raw payload/frame cells | evaluator internals; saved across subexpressions | Not all are roots; scan is closure-specific | Raw pointer would become stale | **D/U** |
| `RV_CTX`, `RV_CHILD`, `RV_CLOSURE`, `RV_FRAME` | evaluator current state; explicit roots | Marked in `emit_collect` | Update roots | **R** |
| Other `RV_*` and GC_T scratch | allocation, tracing, reduce/call temporaries | Not generally roots; some are raw payloads | Cannot safely update without a complete map | **D/U** |
| MASM `SCRATCH_A..F` / `RV_SCRATCH_*` | strings, f2buf, libraries, runtime `asm_*` | Not traced as general roots | Raw pointers across allocation would be stale | **P/D** |
| Managed strings/blocks/user objects | `mk_string`, `reduce`, `create-sin`; payload cells | Traced by kind (`trace_string/user/block`) | Update payload value; strings are leaves | **R** |
| Host references | `glon.c` result buffer, `GLON_LIVE` bytes, browser host | Host copies; no durable managed pointer in normal ABI | Direct `M` access would need pin/handle rules | **P/D** (if direct) |
| Interior pointers (payload+offset) | runtime/context/block/string operations | Object-start checks exist for closure marking, not a general relocation contract | Interior reference cannot be updated generically | **D** |
| Code addresses | emitted code, return stack, labels | Not managed objects | Never move with managed GC | **Fixed** |
| Loader/permanent objects | loader heap, global built-ins | Permanent roots | Must remain pinned/non-managed | **Fixed/pinned** |

### Cross-allocation findings

- `emit_alloc` explicitly requires only tagged values on the **data stack** at
  collection. There is no equivalent guarantee for every `RV_*`, scratch or
  MASM-held payload pointer.
- `emit_reduce_block` saves a raw result-block payload on the return stack
  while evaluating subexpressions; the return-stack scanner only recognizes
  closure-kind objects. This is an **audit item**, not a claim of a current
  correctness bug.
- MASM/library code uses raw scratch pointers (strings, F2, iterators) and no
  general pin/root registration exists.
- Interior pointers are used by runtime/context/block operations. The closure
  scanner's object-start validation prevents a known false mark, but is not a
  general safe-relocation mechanism.
- Normal host boundaries copy bytes; direct native `M` access is a special
  integration path, not a universal host object contract.

**Audit conclusion:** tagged managed values are traceable and could be updated
in a moving design; raw evaluator/scratch/MASM/interior/host paths are not fully
enumerated. Those are the blockers for safe compaction.

## 3. Bounded-memory measurements

Measurement helper: `experiments/glon-memory-audit/measure_memory.py` builds a
temporary read-only host probe in `/tmp` from the existing `dzhost.c`; it does
not modify committed runtime/application source. It scans existing heap headers
after each fixture and records `max_live` (STATS), frontier high-water,
live/free blocks, largest free block, GC count, and native process maximum RSS
via `/usr/bin/time -v`. Fixtures and schedules are the existing B9 DEFLATE
fixed, dynamic and streaming workloads.

| Workload | Output bytes | Peak live cells | Frontier high-water | End free cells | End largest free | GCs | Max RSS |
|---|---:|---:|---:|---:|---:|---:|---:|
| fixed | 2,000 | 4,288 | 7,216 | 592 | 432 | 13 | 2,240 KB |
| dynamic | 9,000 | 4,464 | 7,216 | 1,456 | 416 | 79 | 2,376 KB |
| streaming | 9,000 | 4,832 | 7,216 | 1,760 | 384 | 185 | 2,168 KB |

The frontier is the end-of-run high-water marker; “end free” is the actual
unallocated header sequence at process end, not the last collection's
`GC_FREE_CELLS` (which can be stale between collections). None of these three
workloads failed.

FizzBuzz and LaTeX were run through the existing B13 native profiles; their
managed heap is not exposed by the CLI. Native maximum RSS was 1,852–1,980 KB
for FizzBuzz and both LaTeX suites under S1-7/S1-12. The process memory includes
the native host and static machine, not a managed-heap peak.

### Confirmed fragmentation evidence

- **F1 DEFLATE:** a dynamic-header code-length request needed **320 cells**;
  largest free was **304**, total free was **2,944**, and the allocation failed.
  Coalescing had run; this is external fragmentation, not an unreachable
  collection backlog. F2 replaced the growing string with a bounded per-header
  buffer and the full 480/480 suite passes.
- **DOM iterator v001:** at the heap-limit depth test, about **600 cells were
  free in 32-cell holes**, but an **80-cell** string allocation failed. This is
  external fragmentation at a different workload and heap frontier.
- These are confirmed specific failures. The measured DEFLATE fixed/dynamic/
  streaming workloads had end-free spaces of 592–1,760 cells and largest free
  blocks of 384–432; they completed without failure. This does **not** establish
  general application-wide fragmentation.
- Repeated allocation/collection evidence already exists in ITER/DOM (201
  fragments, deep walks under collection, 10,000-node host traversal); B10
  recorded 846–12,963 allocation calls for the DEFLATE workloads. No new
  repeated-cycle benchmark was added.

### Unavailable metrics

- **GC pause duration:** no timestamp instrumentation exists around
  `emit_collect`; measuring it without a runtime change is not available.
- **Per-allocation latency:** the host exposes run/read/feed timing and
  allocation counts, not latency around each `emit_alloc`; per-allocation
  timing is unavailable without runtime instrumentation.
- **Managed live/heap for FizzBuzz/LaTeX:** the CLI has no managed-heap stats
  endpoint; process RSS only was measured.
- **Reduced-heap configuration:** the 65,536-cell map and 7,232-cell heap are
  fixed; a smaller heap requires an approved test-only runtime copy. No
  smartwatch/glasses hardware limit is invented here.
- **WASM memory:** the WASM build was not instrumented for this audit; native
  results are not presented as WASM results.
- **Metadata overhead:** the static structural cost is source-verifiable (16-cell
  header per managed object, 4-bit tags, no handle table). A handle/table
  prototype's cells and dereference cost were not measured because no prototype
  was built.

## 4. Architectural options

| Option | Assessment |
|---|---|
| **A. Retain current collector** | Correct and predictable for tested workloads. Non-moving, exact mark/sweep with first-fit and coalescing; no handles or pauses beyond STW collection. It cannot guarantee a contiguous block under arbitrary fragmentation. The measured workloads pass; F1's specific failure was removed by F2. |
| **B. Improve non-moving allocation** | Lowest-risk extension of A: placement/size-class/free-space policy, bounded buffers, or workload-specific capacity planning. No pointer updates, no host/handle changes. It can reduce fragmentation likelihood but cannot eliminate the need for a contiguous block or recover a fragmented heap. Must be evidence-driven; F1's earlier best-fit/no-split experiments did not provide a safe general fix. |
| **C. Stable handles + compaction** | Handles make relocation possible but do not perform tracing or guarantee correctness. Requires converting every managed reference (tagged values, return stack, `RV_*`, contexts/closures, blocks/strings, globals), handling raw/interior/host vectors, handle-table storage, reuse/stale protection, and object move/update. Every access pays indirection; compaction pauses and root mapping add complexity. No current evidence shows this is needed for the tested applications. |
| **D. Hybrid memory** | Pinned/stationary objects plus movable/managed regions. Useful if host or raw references must remain pinned, but requires a pin set, movement rules, barriers/root maps and separate allocation policies. It is not simpler than C; it adds a second memory contract to every GC path. |

For small, long-running, reprogrammable nodes, A has the strongest current
safety/predictability evidence. B is the only incremental option that preserves
the frozen representation. C/D are architectural projects whose correctness
cost is not justified by the current workload evidence.

## 5. Remaining correctness risks and blockers

1. **Incomplete raw-reference map** in `RV_*`, MASM scratch and evaluator
   temporaries. Until every live managed pointer is rooted or pinned, moving
   collection is unsafe.
2. **Interior references** are not governed by a general relocation contract.
3. **Host/direct-memory access** has no pinning or stale-reference protocol.
4. **STW pause** and per-allocation latency are unmeasured; constrained device
   predictability is therefore an estimate, not a demonstrated property.
5. **Native/WASM parity** for any future memory design is unverified; WASM cell
   width and host memory behavior differ.
6. **Specific fragmentation** remains possible in non-moving A, even if F2
   handles the known DEFLATE case. This is a capability limit, not a reason to
   adopt a moving collector without a new failing application requirement.
7. **Root traffic test 8 discrepancy** remains an independent verification
   issue; it is not attributed to memory management here.

## 6. Recommendation and stopping condition

**Recommendation: retain Option A (non-moving collector) for the tested
workloads, and do not implement handles or compaction now.** The evidence shows
specific external-fragmentation failures but no general application-wide
failure that justifies changing the object representation. Handles alone would
not eliminate tracing or fragmentation; compaction would require the
unresolved raw/interior/host map above.

The smallest justified next engineering action is:

1. complete the source-level reference/pinning audit for every `RV_*`, scratch,
   MASM, interior and host-held managed pointer;
2. add bounded, read-only fragmentation measurements to the existing workloads
   (largest free, live/free, GC count) using the helper in
   `experiments/glon-memory-audit/`;
3. only if a reproducible application still fails after bounded-buffer and
   allocation-policy improvements (Option B), write a separate architecture
   proposal for handles/compaction with a pin/relocation design and WASM plan.

**Stopping condition:** stop after the audit and measurements if the tested
applications remain correct within the 7,232-cell heap. Stop before any moving
GC or handle prototype if any raw/interior/host reference path cannot be
enumerated or pinned. Do not begin B14 or a runtime rewrite from this document.
