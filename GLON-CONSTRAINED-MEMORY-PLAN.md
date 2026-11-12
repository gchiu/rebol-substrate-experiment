# Glon Constrained-Memory Runtime — Investigation Plan

**Status:** investigation charter and current-state record. Documentation only.
This does not authorize a runtime, GC, heap, language, compiler or application
change. S1-7 remains the frozen reference and production default; S1-12 remains
an optional experimental native evaluator profile.

## 1. Verified current memory architecture

### Address space and cell model

- The machine is a flat array of **65,536 cells** (`MEM_CELLS` in `s1.h`).
  Native and WASM use the same cell-indexed machine; the cell is `intptr_t`
  (`s1.h`): 64-bit in the native build, 32-bit in WASM.
- The memory map is fixed, not a build option: registers/scratch, emitted code,
  data stack (downward from 16,384), return stack (downward from 24,576),
  runtime state/GC, managed heap, loader heap and M1 arenas
  (`m1_layout.h`, `r0_s1.h`). The managed heap is
  `GC_HEAP_BASE=32768` to `GC_HEAP_LIMIT=40000`, i.e. **7,232 cells**.
- The heap is a sequence of 16-cell-stride headers and payloads. Each object
  has a header size at `M[h+GC_HDR_SIZE]`, flags at `M[h+GC_HDR_FLAGS]`
  (allocated bit, mark bit and kind), and 16-cell alignment
  (`GC_HDR_STRIDE=16`; `r0_s1.h`). Every managed object has at least
  `GC_MIN_EXTENT=32` cells.
- Object kinds are context, closure, RAW, USER, STRING and BLOCK
  (`GC_KIND_*`). Strings keep **one byte per managed cell**. There is no
  separate object table or handle array.

### Values and references

- Glon values are **tagged cells**: the low four bits are a tag; object tags
  include `T_BLOCK`, `T_CONTEXT`, `T_CLOSURE`, `T_USER`, `T_STRING`
  (`r0_s1.h`; `mk_*`, `r0_tag`, `r0_untag`). A tagged object value is the
  payload address plus tag, not a handle or a table index.
- Object payloads are raw cell addresses. The evaluator/runtime also holds
  **untagged raw pointers and code addresses** in special RV cells, scratch
  cells and assembler-generated code.
- There is no indirection layer that would allow an object to move and keep the
  same Glon value. A moving collector must rewrite every tagged pointer and
  every live raw pointer that points into the managed heap.

### Allocation

`emit_alloc` in `r0_s1_runtime.c` is the allocator:

- calculates `extent = n + GC_HDR_STRIDE`;
- scans from `GC_HEAP_BASE` to `REG_HP` for a free block whose size is
  `>= extent` (first fit);
- otherwise bumps `HP` if space remains;
- if neither works, calls `emit_collect` once and retries; a second failure is
  a host/machine fail-stop;
- when a free block is large enough, splits it if the remainder is at least
  `2 * GC_HDR_STRIDE` (32 cells).

There is **no explicit linked free list** and no `free` primitive. Free space is
represented by unallocated blocks in the heap sequence; the sweep discovers
them by scanning headers.

### Collection and free-space management

`emit_collect` is a **stop-the-world, non-moving, exact mark/sweep** collector:

- roots are marked: `GC_GLOBAL_CTX`, `RV_CTX`, `RV_CHILD`, `RV_CLOSURE`,
  `RV_FRAME`, the built-in descriptor table, active data/return stacks, task
  records and saved task stacks (`r0_s1_runtime.c`, `emit_collect`);
- tracing follows context, closure, USER, STRING and BLOCK contents
  (`emit_trace_ctx`, `emit_trace_closure`, `emit_trace_user`,
  `emit_trace_string`, `emit_trace_block`);
- the mark worklist is fixed at 256 entries (`GC_WORKLIST_CAP`); overflow is a
  deliberate fail-stop;
- sweep clears marks, frees dead objects, and **coalesces adjacent free runs**
  while scanning (`emit_collect`; the free/coalesce path is inline in the
  sweep).

There is no object relocation, no forwarding pointer and no compaction. The
collector records `GC_LIVE_CELLS`, `GC_LIVE_OBJS`, `GC_FREE_BLOCKS`,
`GC_FREE_CELLS`; allocation failure is a machine failure, not a recoverable
Glon exception.

### Roots and ownership

Verified roots and lifetimes:

- data stack: values are expected to be tagged Glon values at allocation
  safepoints (`emit_alloc` has an explicit GC-safepoint invariant comment);
- return stack: the collector scans cells in the managed heap as closure
  pointers (`emit_scan_closures`); code addresses and other non-heap cells are
  not managed objects;
- global context, current context/child/closures/frames and built-in
  descriptors are explicit roots;
- M1 task records and their saved stacks are roots while runnable;
- host values are **copied** across the native/WASM boundary. `GLON_LIVE` builds
  source text from bytes and renders result bytes; `standalone/glon.c` writes
  results into host buffers and exposes result pointer/length after a run. The
  host does not own a durable managed-heap object pointer in the normal ABI.

Ownership is runtime/GC-owned. Loader-heap objects are permanent and outside
the managed heap. Managed objects live until unreachable from the roots above.
There is no manual free or pinning API.

### Native versus WASM

Both targets run the same `s1.c`/`r0_s1_runtime.c` machine and the same
65,536-cell map. The concrete differences are:

- `cell` is 64-bit natively and 32-bit in the WASM build;
- native process memory includes C host buffers outside `M`; WASM exposes
  linear memory plus host JS buffers;
- native hosts can inspect `M` directly, while the browser host receives copied
  bytes/HTML/canvas buffers;
- S1-12 is a native-only experimental evaluator profile; no S1-12 WASM port
  exists.

Neither target currently has a movable heap or a host pinning contract. Native
and WASM memory behavior must therefore be measured separately; a native result
does not prove WASM behavior.

## 2. The fragmentation problem

These are distinct issues:

| Issue | Definition | Glon status |
|---|---|---|
| Unreachable memory awaiting collection | Dead objects not yet swept | Normal; collected at allocation-triggered stop-the-world collection. Not fragmentation. |
| External fragmentation | Enough total free cells, but no contiguous block large enough | **Demonstrated in specific cases**: F1 dynamic-header request 320 vs largest free 304 with 2,944 free cells; DOM v001 at its heap limit found ~600 free cells in 32-cell holes while an 80-cell string failed. F2 removed the DEFLATE trigger with a bounded buffer. |
| Internal fragmentation | Unused bytes inside an allocated extent or fixed buffer | Present by design: 16-cell headers, 16-cell string rounding, `GC_MIN_EXTENT`, fixed F2 code-length buffer, fixed iterator/context structures. Not generally measured end-to-end. |
| Live-object retention | Reachable objects keep memory | Normal and necessary; F1/B10 show live closures/contexts/strings. Retention is not a leak without an unreachable-root proof. |
| Fixed-capacity limits | The 7,232-cell heap, 65,536-cell machine, 768-byte DEFLATE window, loader-heap and code-stack boundaries | Hard limits; not fragmentation. A larger contiguous allocation can fail even with free cells. |

Current evidence supports **specific external-fragmentation failures**, not a
general claim that Glon applications are broadly fragmented. F1's failure was
real and deterministic; F2 avoided it without a moving collector. Iterator/DOM
evidence shows the same class at a different boundary. Any proposed GC or
handle redesign must first reproduce a concrete application failure that
existing non-moving management cannot fix.

## 3. Candidate architectures

| Candidate | What changes | Complexity | Runtime/memory overhead | Root/relocation issues | Fit for constrained devices |
|---|---|---|---|---|---|
| **A. Existing non-moving heap, improved allocation/coalescing** | Keep tagged raw pointers and object addresses; improve placement, split/coalesce policy or size classes | Low–medium | Metadata only; no indirection | No relocation; roots unchanged; still cannot guarantee a contiguous block | Best first candidate; predictable, preserves ABI, but cannot eliminate external fragmentation |
| **B. Stable object handles** | Replace object values with indices into a handle table; objects can move independently | High | One indirection per object access; handle table cells; handle reuse metadata | Every reference path must use/update handles; stale-handle protection; host/pinning; raw interior pointers remain | Handles alone do **not** solve fragmentation; they only make relocation possible and add overhead |
| **C. Compacting GC** | Move live objects and update all references; recover contiguous free space | Very high | Forwarding/relocation metadata; stop-the-world or incremental barriers | Requires complete root/raw-pointer enumeration; all tagged and untagged heap pointers updated; host pinning; interior pointers | Strong fragmentation recovery, but high correctness risk and pause cost on constrained devices |
| **D. Hybrid memory** | Separate movable/pinned regions; handles/barriers for movable objects, pin host/raw objects | High | Region metadata, pin set, barriers, possible table | Correct pin/unpin and root tracing; object kinds have different movement rules; complexity in every allocator/GC path | Can be appropriate after an audit, but is not the smallest first step |

Handles do not by themselves make a collector correct or reduce total memory;
compaction requires a complete reference/root audit. The current runtime has no
handle layer and no pin API, so B/C/D are architectural projects, not local
allocator tweaks.

## 4. Prerequisites for safe compaction

A moving collector must identify and update **every live reference**. The
following audit is required before any compaction prototype:

| Reference location | Current evidence | Compaction prerequisite |
|---|---|---|
| Data stack | Tagged values by invariant; scanned by `r_scan_values` | Update every tagged heap pointer; preserve non-heap tags/values |
| Return stack | Closure pointers recognized by `r_scan_closures`; code addresses are not objects | Distinguish and update closure pointers; leave code addresses unchanged |
| Consequent/evaluator stacks | `RV_*` state, frames and task records; explicit roots listed in `emit_collect` | Add every managed pointer to a root map; do not assume only the named roots are live |
| Closures/contexts/blocks/user objects | Fields traced by `emit_trace_*` | Update traced fields, including interior-structure references where legal |
| RV/scratch cells | Many are raw implementation cells; not all are GC roots | Audit which can hold a managed pointer across allocation; root, pin or forbid |
| MASM/library raw pointers | `strings.glon`, `f2buf`, iterator helpers and runtime `asm_*` can retain raw addresses in scratch/return stack | Remove, pin or register each raw pointer across safepoints; current code does not provide a general mechanism |
| Interior pointers | Runtime uses payload addresses and offsets; object-start checks exist for marks but not a general relocation contract | Forbid interior references, update them, or pin their objects |
| Host-held references | Normal host boundary copies bytes; native hosts can inspect `M`; no pin API | Define host handle/pin lifetime and stale-reference behavior before moving objects |
| Loader/permanent objects | Loader heap is outside managed heap | Never relocate; root rules remain separate |

**Blockers:** the current runtime has no complete, explicit mapping of raw
managed pointers in `RV_*`, scratch cells, MASM code and temporary evaluator
state. `emit_alloc` only states that the **data stack** is safe at a collection
safepoint; it does not prove that all raw implementation pointers are pinned or
updated. A moving collector cannot be declared safe without resolving those
paths. The DOM/iterator and DEFLATE experiments provide additional raw-pointer
and fragmentation evidence, but not a complete relocation audit.

## 5. Measurement plan

No new benchmark is required to begin. Use existing applications and existing
runtime counters; add instrumentation only after approval and keep it outside
production behavior.

### Workloads

- DEFLATE fixed, dynamic and streaming fixtures from B9/B12 (existing
  fixtures; reports include instruction/time baselines).
- LaTeX v001/v002 and FizzBuzz under S1-7/S1-12 native profiles (B13).
- DOM iterator and host-backed link traversal, including depth/fragmentation
  cases (ITER/DOM reports).
- A future constrained-memory application test must be named; do not invent a
  smartwatch/glasses hardware specification.

### Metrics

| Metric | How to measure | Completion condition |
|---|---|---|
| Peak managed memory | `GC_LIVE_CELLS`, high-water `REG_HP`, existing `dzhost -s`/B10 profile | Reported per workload with initialization separated |
| Peak process memory | Native `/proc/self/status`/`getrusage`; WASM `WebAssembly.Memory.buffer.byteLength` | Native and WASM reported separately |
| Live objects/allocations | `GC_LIVE_OBJS`, `PF_ALLOCS`, `PF_ALLOC_CELLS`, GC counters | Counts and object kinds recorded |
| Largest free block | Existing heap/free-block scan (F1 diagnostic) or a read-only host probe | Largest free block at peak/failure; fragmentation ratio |
| Repeated allocate/free fragmentation | Bounded repetition of a real workload with collection boundaries | Same workload passes within stated free-cell budget |
| GC frequency/pause | `GC_COLLECT_CNT`, timestamps around collection; existing per-run timing | Median/max pause and collections per workload |
| Allocation latency/failure | Timed allocation/dispatch; record fail-stop and error class | Explicit pass/fail threshold; no hidden truncation |
| Handle/metadata overhead | If prototyping B/D: table cells, indirections and root-map cells | Added cells and latency measured against A |

The existing 64K configuration is the first constrained case. A later reduced
heap test must use an approved test-only configuration; do not change the
production map or claim a device limit. Measure native and WASM independently.

## 6. Smallest justified engineering step

**Recommended first step: complete the reference/pinning audit and add a
bounded, read-only fragmentation measurement to the existing runtime tests.**
Do not implement handles, compaction or a new GC yet.

Justification:

- F1/F2 and DOM evidence show specific external-fragmentation limits, not a
  general handle/compaction requirement.
- The current collector is non-moving, exact and fully tested; its allocation
  and coalescing behavior is simple and predictable.
- The missing evidence is not "which GC algorithm is theoretically best"; it is
  whether every live managed/raw pointer can be enumerated and whether a real
  application fails after existing bounded-buffer/allocator improvements.
- A full handle/moving-GC rewrite would add indirection, root-map and host/pin
  complexity to every application before an actual blocking application case is
  demonstrated.

The first step should produce:

1. a source-level reference inventory (all roots, RV/scratch, MASM, host and
   interior-pointer paths);
2. a fragmentation trace on DEFLATE/DOM workloads with largest-free-block,
   live/free cells, GC count and pause;
3. a decision: retain non-moving A, pursue a bounded allocator policy change, or
   authorize B/C/D only if a reproducible application failure remains.

**Stopping condition:** stop after the reference audit and measurements if no
reproducible failure remains after existing allocator/buffer strategies. Stop
before implementing handles or compaction if any raw/host/interior reference
path lacks a safe update or pinning rule.

## 7. What must not change

- Frozen S1 and its seven-primitive semantics.
- The 65,536-cell map, heap limit or loader layout by an incidental fix.
- Glon language semantics or application source compatibility.
- S1-12's experimental status and native-only boundary; no production default
  promotion.
- Native and WASM claims: they remain separate until both are tested.
- Existing correctness discrepancies, including the root traffic test
  discrepancy, remain visible.

**Conclusion:** Glon's current non-moving heap is suitable for continued
application work, and the demonstrated fragmentation cases do not yet justify a
moving GC or handle layer. The smallest justified next step is a reference/root
audit plus bounded fragmentation measurements; any compaction or handle design
starts only after that evidence and a specific failing application requirement.
