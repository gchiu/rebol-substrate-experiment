# Glon memory profiles — the 64K reference machine and larger profiles

> **Status (2026-11-09): analysis and proposal only.** No profile other than
> the 64K reference exists or is approved. Nothing here changes frozen files
> (AGENTS.md; GLON-ARCHITECTURE-MEMORY.md §8, §9).

Labels: **[M]** measured, **[C]** read from the code, **[E]** estimated.

## 1. What fixes the size of the machine today [C]

| Where | What | Frozen? |
|---|---|---|
| `s1.h` | `MEM_CELLS 65536`; `DS_INIT 16384`, `RS_INIT 24576`, `HEAP_BASE 32768` (`s1_reset` loads them) | **yes** |
| `s1.c` | `cell M[MEM_CELLS]`. `@` and `!` index `M` **unchecked**: S1 has no address bound of its own | **yes** |
| `r0_s1.h` | the runtime map, with `#error` / `_Static_assert` guards that refuse other values: `R0S1_DS_INIT`/`RS_INIT`, runtime state 24,576–32,768, `GC_HEAP_BASE` 32,768, `GC_HEAP_LIMIT` = `R0S1_HEAP_BASE` 40,000, loader to 54,800, `GC_WORKLIST_CAP` 256 | no, but each value is a [DECISION] of the 64K map |
| `m1_layout.h` | task arena 54,800 (3 × 3,400), task table 65,000 | no; same |
| runtime logic | the region **order**: "SP ≤ DS_INIT ⇒ main world", stack-local, managed and loader classification by address range (GLON-ARCHITECTURE-MEMORY.md §5) | — |
| runtime and tests | literal addresses (39,984, 40,000, 54,800, 65,000) in `r0_s1_runtime.c` and 6 test files | — |

## 2. The three options

### A. The 64K reference profile (unchanged)
- **Space:** a 7,232-cell managed heap and 14,800 loader cells. The return stack
  allows about 84 nested calls [M].
- **Status:** it stays the conformance baseline, and every result to date is
  for it.

### B. A larger profile (for example 256K = 262,144 cells), the same architecture
- **Not configuration-only.** `MEM_CELLS` is in frozen `s1.h`, and `cell
  M[MEM_CELLS]` in frozen `s1.c` sizes the array. The minimum frozen change is
  that one constant.
  - `DS_INIT`/`RS_INIT`/`HEAP_BASE` need not change: the runtime may keep the
    lower 32K as it is.
  - A profile that also deepens recursion would move the stacks, and the
    runtime already sets `REG_*` after `s1_reset`.
- **Runtime-header changes** (not frozen, but each a [DECISION] of the 64K map):
  - the managed heap's limit;
  - the loader heap's base and limit;
  - the task arena and table;
  - the seeded-datatype addresses, which move only if the heap base moves; an
    order-preserving map that grows the heap upward keeps them;
  - a GC worklist sized by profile ([PROPOSAL] (c) in §9; the build-time
    assertion refuses a larger heap without it);
  - every test with a literal address.
- **GC assumptions:**
  - the worklist must hold heap / 32 entries (6,144 for a 196K-cell heap);
  - the mark phase is iterative (worklist), so heap size does not deepen the
    return stack [C];
  - sweep is O(heap);
  - the new object-start check is O(objects) for each closure-kind candidate on
    the return stack. With about 6,000 objects it could cost about 30× more per
    candidate than today [E]. A larger profile should come with the cheaper
    exact check (an object-start bitmap: 1 bit per 16 cells, 12K bits for 196K
    cells) [E].
- **Cell width and addresses:**
  - WASM cells are 32-bit (`intptr_t`). Pointers are tagged (`address + tag`),
    so addresses up to 2^31 are representable.
  - Glon integers are ±2^27 in WASM, but no part of the runtime or of
    `mem.glon` turns an address into a Glon integer [C].
  - A 256K or even 4M-cell profile is representable in both builds [E].
  - Integers 2,048 up to heap end / 16 then look like heap addresses to the
    conservative return-stack scan. The repaired scan rejects all of them that
    are not exact object starts [C].
- **Native and WASM memory:**
  - native: 262,144 × 8 bytes = 2 MiB;
  - WASM: 262,144 × 4 bytes = 1 MiB of linear memory, against 256 KiB today.
    Both are trivial [E].
- **Task scheduling** stays valid if the arenas keep their place in the order,
  above the loader heap [C]:
  - the collector classifies a task's stacks by the arena's address range;
  - "SP ≤ DS_INIT ⇒ main world" holds while the main stacks stay at the bottom.

  More or larger task arenas become possible.
- **A 32 KiB DEFLATE window** fits easily:

  | Holding the window as | Cells |
  |---|---|
  | a `mem/` region | 10,944 (measured formula) |
  | strings | 32,768 |

  Either fits in a heap of about 196K cells, beside the interpreter, the
  libraries (about 2,700 cells live) and the tables [E]. The return-stack
  limit (about 84 calls) is unchanged unless the stacks move.

### C. Host-backed memory through the existing asynchronous interface
- **Mechanism:** GLON_LIVE `host_call` requests and `glon_event_bytes` replies.
  - Regions live in host memory, addressed by generation-checked handles that
    Glon owns, as `glon-web` does for documents.
- **ABI cost:** no frozen or ABI change, but only in GLON_LIVE hosts. The `glon`
  CLI and the standalone WASM module have no `host_call`.
- **Not the `mem/` vocabulary:**
  - every read is a request answered by a later dispatch;
  - one request is outstanding per dispatch;
  - a dispatch round trip cost about 2.8 ms in `dzhost` [M].
- **What it suits:** write-mostly streaming with batched commands (append,
  copy-within), not random byte access.
- **A 32 KiB DEFLATE window:** possible if the host keeps the window and applies
  batched "literal run / copy (distance, length)" commands. The decoder never
  needs to read the window back. This is a different program shape and a
  different, asynchronous contract.

## 3. Comparison

| | A. 64K | B. 256K, same architecture | C. Host-backed, async |
|---|---|---|---|
| Frozen-file change | none | **`MEM_CELLS`** in `s1.h` | none |
| Other changes | none | runtime and M1 map constants, worklist sized by profile, an object-start bitmap (recommended), address-literal tests | a host capability protocol per host |
| Managed heap | 7,232 | ≈ 196K [E] | 7,232, plus host memory |
| Synchronous `mem/` vocabulary | yes, ≈ 8 KiB of regions in practice [E] | yes, ≈ 500 KiB of regions [E] | **no** |
| 32 KiB DEFLATE window | no | yes [E] | yes, as host-applied commands [E] |
| Native/WASM | identical [M: `mem/` tests] | identical; 1 MiB WASM memory [E] | browser and native GLON_LIVE hosts only |
| Conformance baseline | **is** the baseline | measured against A; A stays buildable and tested | unchanged machine |

## 4. Recommendation (proposal)

1. **Keep A as the reference and conformance baseline.**
2. **For practical applications, B is the smallest general step.**
   - **One frozen constant.** It changes one frozen constant, and the language,
     the ABI, `mem/` and every library stay the same.
   - **Separate approval.** It needs its own explicit approval: a frozen-file
     change and a memory-map change (§8 of the architecture memory).
   - **What lands with it:**
     - a worklist sized by profile, which the build already enforces;
     - preferably an object-start bitmap, so that the repaired scan stays cheap
       in a large heap;
     - an address-literal sweep of the tests.
3. **Use C only for host-owned bulk data** (documents, files, streams) under
   its asynchronous contract, not as general memory.
