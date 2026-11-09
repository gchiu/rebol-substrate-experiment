# Glon/Binary — feasibility of an optional binary-buffer library

> **Status (2026-11-09): investigation and proposal only. Nothing here is
> implemented.** No ABI, runtime or language change is made or requested by
> this document without approval (AGENTS.md; GLON-ARCHITECTURE-MEMORY.md §9).
>
> **Context.** The DEFLATE experiment (`experiments/glon-deflate/`) showed:
> - a streaming decompressor can be written in plain Glon;
> - one cell per byte, a 7,232-cell non-moving heap and immutable strings
>   limit it to a 768-byte history window, and leave it at the edge of
>   fragmentation.
>
> The question here is whether Glon can gain Rebol/Core-like binary storage
> from an **optional, independently implementable library with a stable
> contract**, without enlarging the frozen machine.

Every figure is labelled. **[M]** is measured (2026-11-09, 64K reference
configuration, native). **[E]** is an estimate from the memory map and code,
not measured.

## 1. What exists today

| Mechanism | What it gives a library | Limits |
|---|---|---|
| **masm library code** (`masm`/RAW, assembled into the code area; e.g. `strings-ext.glon`) | arbitrary loops over Glon memory with the 7 S1 primitives; portable to native and WASM unchanged, because it is S1 code | runs inside the 65,536-cell machine; a third budget, code space (~2,900 cells free with prelude + strings [M, GLON-ITER v001]); unchecked unless wrapped |
| **M3 USER objects** (`T_USER`, `GC_KIND_USER`, `trace_user`) | a GC-managed, typed object `[desc, count, cell…]`. The collector marks `desc` and the first `count` cells only, and checks `count` against the extent, so **cells after `count` are never scanned**: room for packed raw data in a managed, collectable object | allocated from the 7,232-cell managed heap; a descriptor (`binary!`) must be made once |
| **GLON_LIVE asynchronous host request/reply** (`host_call` op/arg out, `glon_event_bytes` in; native harness and `glon-live.wasm`) | **host-owned resources by handle.** `glon-web` already does this for DOM documents, with generation-checked handles whose lifetime Glon owns | **asynchronous only:** one outstanding request per dispatch, and the reply is a new dispatch. Only GLON_LIVE hosts; the `glon` CLI has none |
| **Host output frames** (bytes printed by Glon, parsed by the host, as `dzhost` does) | a write-only channel to host-owned state | write-only; host-specific |

**What does not exist, and is excluded by recorded decisions:**
- **A synchronous Glon → host call returning a value.** The frozen S1 `HOST`
  set is a closed enum:
  - `ADD SUB MUL DIV MOD NEG EQ NE LT GT LE GE`
  - `ALLOC PUTCHAR PRINT DUMP`

  GLON-ARCHITECTURE-MEMORY.md §7 records the **[DECISION]** that the host
  boundary is asynchronous and gets no new primitive. Writing replies into
  Glon memory from inside `host_print` was **rejected** as a side channel.
- **Memory outside the 65,536 cells.** `MEM_CELLS` is fixed in the frozen
  `s1.h`.

So the existing ABI can expose **synchronous** binary operations only on
storage **inside the 65,536-cell machine**. Host-owned buffers are reachable
only **asynchronously**.

## 2. Can a 32 KiB DEFLATE window avoid 32,768 heap cells?

| Representation | Cells for 32,768 bytes | Fits the 64K machine? |
|---|---|---|
| STRING! (one byte per cell; today) | 32,768 [M: layout] | no: the managed heap is 7,232 |
| packed in a USER object, 3 bytes per cell (safe in 32-bit WASM, where cells are `int32` and integers are ±2^27) | 10,923 [E] | no |
| packed, 4 bytes per cell (all 32 bits of a raw cell; the masm code must handle the sign) | 8,192 [E] | **no**: more than the whole managed heap, before libraries (≈2,700 live [M]) |
| packed, 8 bytes per cell (64-bit native only) | 4,096 [E] | not portable: WASM cells are 32-bit |
| in the M1 task arena (10,200 cells, unused when no task runs), 4 bytes per cell | 8,192 [E] | **yes**, but it is a memory-map decision (AGENTS.md: the map changes only by an approved experiment) and excludes M1 tasks in that session |
| managed heap of the proposed 256K profile (~65,536 cells) | 8,192 [E] | yes; needs `MEM_CELLS` changed in frozen `s1.h` (an existing, unapproved [PROPOSAL]) |
| host-owned buffer (GLON_LIVE, by handle) | **0** (a handle) [E] | yes, but asynchronous only (below) |

**What would fit, inside the 64K managed heap** [E]:
- about 4,000 cells of free heap remain after the libraries and a decoder's
  tables;
- packed 4 bytes per cell, that is about 12–16 KiB of window, in objects of a
  few hundred cells;
- that covers zlib `windowBits` ≤ 13 or 14, not the `windowBits` 15 of
  ordinary `.gz` and `.zip` files.

## 3. Proposed library contract (`bin/`) — interface only

The contract is **implementation-neutral**: a heap-backed masm library and a
host-backed library must be interchangeable behind it. It is synchronous;
section 4 explains which backings can honour that.

```text
bin/make cap          a new buffer, length 0, capacity cap bytes (cap > 0).
                      Raises 'resource if it cannot be allocated (catchable;
                      it is not a machine fail-stop).
bin/length b          bytes stored (0 <= length <= capacity)
bin/capacity b        bytes it can hold
bin/at b i            the byte at 0 <= i < length; raises 'range otherwise
bin/poke b i byte     store byte (0..255) at 0 <= i < length; raises 'range / 'type
bin/append b s        append a STRING!'s bytes, or another buffer's; raises 'range on overflow
bin/append-byte b v   append one byte
bin/copy-within b dst src n
                      move n bytes inside b, memmove semantics (overlap safe as
                      if through a temporary); all four of dst, src, n and
                      the range bounds are checked
bin/append-back b d n append n bytes, each copied from d bytes before the
                      current end (LZ77 semantics: n > d repeats the last d
                      bytes). 1 <= d <= length
bin/slice b start n   a NEW STRING! of n bytes (the bridge to the s/ vocabulary)
bin/drop-front b n    discard the first n bytes (a ring/history buffer keeps
                      its last k bytes this way); O(1) in a ring implementation
bin/release b         end the buffer's life; later use raises 'released
bin/released? b       1 after release
```

**Contract rules:**
- **Errors.** Every failure is a SIN! of type `'range`, `'type`,
  `'resource` or `'released`, raised before any byte changes. Nothing reads or
  writes out of bounds: the masm workers are reached only through the checked
  wrappers, as `strings-ext.glon` requires.
- **Ownership.** Glon owns each buffer's lifetime.
  - Heap-backed: the collector reclaims an unreferenced buffer, and `release`
    only marks it dead.
  - Host-backed: the host frees it on `release` or at the end of the session,
    and changes the handle's generation, so a stale or forged handle is
    refused, never dereferenced (as in `glon-web`).
- **Type.** A buffer is a `binary!` USER value (M3), so `type?` and `accepts?`
  work, and the `dz/`-style problem of untestable BLOCK!/closure arguments
  does not arise.
- **No views.** Slices copy into a STRING!. Views would create aliasing and
  lifetime coupling for little gain at this scale; they are deferred.
- **Optional loading.** `binary.glon` is loaded after the core libraries only
  by sessions that use it, like `strings-ext.glon`.
  - **Cost [E]:** about 15 bindings, about 600–900 code cells for the packed
    accessors and copy loops (a third of the free code space), and 1–2K
    loader cells.

## 4. Which backings can honour the contract

| Backing | Synchronous? | ABI change | Capacity on 64K | Portability |
|---|---|---|---|---|
| **H. Heap-backed masm library** (USER object, packed bytes) | yes | **none** (masm + M3 USER objects exist) | ≈ 12–16 KiB total [E] | native and WASM identical (S1 code) |
| **R. Arena-backed masm library** (M1 task arena as a byte region) | yes | none, but a **memory-map decision** | ≈ 40 KiB [E] | identical; no M1 tasks in that session |
| **A. Host-backed, GLON_LIVE async** | **no**: each operation is a request answered by a later dispatch | none | host memory [E] | GLON_LIVE hosts only (native harness, `glon-live.wasm`) |
| **S. Host-backed, synchronous** | yes | **new frozen-S1 HOST operation** | host memory | every host must implement it |

Backing A cannot implement `bin/at` synchronously. It suits only write-mostly
streaming with batched commands, for example a DEFLATE decoder sending
"literal run" and "append-back d n" commands per dispatch while the host keeps
the window and the output. That is a different, asynchronous contract
(`bin-async/`), and it exists today without any ABI change.

**The smallest general-purpose mechanism for backing S** (not to be
implemented without approval):
- **The opcode.** One new `HOST` id, `HOST_CAP`: pop a capability id and a
  fixed number of integer arguments, call the host's registered capability
  table, push one integer result. Errors come back as a result code that the
  Glon wrapper turns into a SIN!.
- **Why it is the smallest:** it adds no control flow, so it keeps "HOST is
  mundane", and it is general: the same id would also serve `file/` and
  `time/` capabilities.
- **What it costs:** it **changes frozen S1** (a new enum value and case), and
  it reverses the [DECISION] that the host boundary is asynchronous. It is
  therefore a language-level decision, not a library detail.

## 5. Architecture comparison (Phase 4)

| | A. Pure Glon, today's string/heap model | B. Pure Glon, better allocation, no new primitive | C. Glon + optional native binary library |
|---|---|---|---|
| **DEFLATE window on 64K** | 768 B [M] (1,024 B works, closer to the edge [M]) | about 2 KiB [E] (strings remain one cell per byte) | 12–16 KiB heap-backed [E]; 32 KiB host-backed or arena-backed [E] |
| **Peak live memory for DEFLATE** | 4.5–5.0K of 7.2K cells [M] | about the same; fewer large blocks [E] | window at a quarter of the cells; tables unchanged [E] |
| **Robustness** | 478 of 480 runs pass; 1 fragmentation fail-stop and 1 GC defect, which the GC fix removes [M] | the fragmentation fail-stop removable by keeping every string ≤ 64 bytes [E] | heap-backed: a few large buffer objects, so fragmentation is still possible [E] |
| **Throughput** | 2.4 KB/s text [M] | about the same or slower: more pieces [E] | byte reads 2–5× cheaper than the `s/at` wrapper path; copies done by masm loops [E]; overall still dominated by Huffman decoding in Glon [E] |
| **Implementation complexity** | 588 lines of Glon [M] | higher: chunked ropes or segment trees for every byte structure [E] | the library: about 300 lines of masm + Glon wrappers [E]; the decoder becomes simpler [E] |
| **Portability** | identical everywhere [M native; WASM not run] | identical | heap/arena-backed: identical; host-backed: each host must implement it |
| **Impact on the frozen ABI** | none | none | H: none. R: memory-map decision. A: none (async). S: frozen S1 change |

**Measured versus estimated.** Column A is measured (DEFLATE experiment,
`experiments/glon-deflate/results/`). B and C are estimates from the memory
map, object sizes and the measured per-operation costs. None of them has been
built.

## 6. Recommendation

1. **Repair the collector first, independently.** It was a correctness defect
   for *every* program holding binary data; it is now fixed and tested
   (GLON-ARCHITECTURE-MEMORY.md, "Collector defect").
2. **If binary capability is wanted, start with backing H**: a heap-backed
   masm library behind the `bin/` contract.
   - It needs **no ABI or frozen change**, runs identically in native and
     WASM, and is an optional library like `strings-ext.glon`.
   - It fixes what the DEFLATE experiment found to be the real costs: mutable
     storage, constant-time byte access, copy loops outside Glon recursion, and
     a quarter of the cells per byte.
3. **Do not pursue a 32 KiB window on the 64K reference machine.** No
   representation inside the managed heap fits it.
   - The arena (R) and 256K-profile routes are memory-map decisions.
   - The host-synchronous route (S) reverses a recorded decision and changes
     frozen S1.
   - If ordinary `.gz` files must be read, the cheapest honest options are
     **A** (asynchronous host window, no ABI change) or the existing 256K
     **[PROPOSAL]**. Each needs its own approval.
4. **Keep the contract implementation-neutral**, so that a later host-backed
   implementation, if ever approved, replaces the heap-backed one without
   changing programs.

This follows the architectural principle: the machine stays small and frozen,
and practical capability arrives as an optional library with a stable
contract. On the 64K reference machine, that library's capacity is honestly
bounded by the machine's memory.
