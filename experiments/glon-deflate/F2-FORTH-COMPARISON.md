# F2 — comparison with a Forth-style DEFLATE architecture

## What a conventional streaming decoder needs

Forth-style DEFLATE is not a different algorithm. It is a different memory
contract:

| Need | Conventional representation | Current Glon decoder |
|---|---|---|
| compressed input | base address + length, usually caller-owned | managed STRING!; `br-in` is a string; `br/feed` keeps the unconsumed suffix |
| bit accumulator | two integer cells, `bits` and `count` | the bit reader closure's `br-bb`/`br-bc` cells |
| Huffman tables | fixed arrays/counts or prebuilt lookup tables | `[counts lowc syms]`; `syms` is a managed STRING! |
| 32 KiB history | fixed circular byte buffer (or a documented smaller window) | eight managed STRING! segments + a partial segment |
| output | caller address + capacity + current position | `dz-out`/`dz-cur` strings returned by `dz/read` |
| suspension | save indices/state and return a status | already explicit: `ready`/`need-input`/`done`/`error`; bit-reader rollback |
| resumption | continue from saved state | already explicit; units are rerun after `need-input` |
| returned result | caller's buffer or a copied result | immutable managed STRING! |

The current decoder is already an **explicit resumable streaming iterator**.
Its distinctive implementation choice is not its control flow; it is the use
of immutable, one-byte-per-cell managed strings for byte arrays and functional
`s/+` concatenation for growth.

## Concrete comparison

- **Input.** Glon's host delivers a STRING!; `br/feed` does `s/+ s/span ...`
  to retain only the unconsumed suffix. A Forth decoder would keep an address,
  a length, and an index. The current representation is safe and GC-managed;
  it is not the failing allocation.
- **Window.** The current 768-byte window is eight 96-byte segments. This is
  already a bounded-history design: distances beyond `w` raise `'window`.
  A 32 KiB byte history cannot fit in 7,232 managed cells at one byte/cell, and
  a packed 3-byte/cell region would still exceed the whole heap. A full window
  is therefore a 256K memory-profile question, not a small DEFLATE rewrite.
- **Huffman code lengths.** The old code accumulates `dz-ls` by immutable
  concatenation: 1 … 288 bytes, allocating a new string at every step. A
  Forth-style decoder would write into a fixed byte array and keep an index.
  This is the exact failure: the 288-byte sequence asks for a 320-cell
  contiguous allocation in a heap whose largest free block is 304.
- **Huffman tables.** The current symbol string is already a compact byte
  array (one byte per symbol). It is managed, but its bounded size fits the
  tested schedules once the code-length growth is removed.
- **Output.** A Forth decoder can write directly to a caller-provided output
  buffer. Glon currently returns an immutable STRING! from `dz/read`; the
  output piece and the 256-byte result assembly still allocate. This is an API
  choice, not an algorithm requirement.
- **Suspension.** Both designs need the same state: bit accumulator/count,
  table selection, pending match length/distance, input index, history position,
  current block state. Glon's `br` rollback units and `dz/make*` state closures
  already carry it. No extra primitive is required for resumption.

## The seven primitives and host services

The frozen machine has exactly `LIT DUP DROP @ ! 0BRANCH HOST`
(`GLON-ARCHITECTURE-MEMORY.md` §1). It can address every cell and the HOST
service covers allocation and I/O. The missing piece for pure Glon is not an
addressing primitive; it is a **checked, owned, mutable byte region** at the
language level. The optional `experiments/glon-mem/` library demonstrates one
possible contract using a `GC_KIND_USER` object with an untraced payload.
F2 uses a much smaller experiment-local masm helper instead: allocate a
fixed-capacity STRING! and write its payload cells with `@`/`!`.

No new VM primitive is needed. No production host ABI change is needed: the
host still sends feed bytes and asks for read output. The helper is loaded from
`experiments/glon-deflate/f2buf.glon`, uses only `alloc` and `!`, and is not a
language feature.

## Twelve-operation candidate

The B3/B8 twelve-operation candidate adds `CALL`, `RET`, `>R`, `R>`, `R@` (the
original seven plus these five). Those are control/stack operations. They can
reduce emitted code and executed instructions, but they do not change the
managed-string representation, the heap, the allocation request, or the
contiguity problem. On the F2 failure, the candidate offers no memory benefit
beyond execution speed; it cannot replace a bounded byte buffer. This confirms
the governing principle: the problem is representation, not primitive count.

## Verdict

Glon's seven-primitive machine can support a bounded streaming decoder if the
decoder has a way to own and mutate byte storage. The current implementation
already has the required streaming and suspension architecture; F2 only had to
replace one avoidable growing managed string with a bounded buffer. It does not
need a Forth interpreter, new syntax, a new VM primitive, a larger heap, or a
GC redesign.
