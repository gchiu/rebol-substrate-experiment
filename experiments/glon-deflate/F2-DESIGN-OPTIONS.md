# F2 design options

## A. Existing decoder with bounded code-length storage — selected

Preserve the decoder, bit reader, table algorithms, window, rollback units and
public API. Replace only the growing `dz-ls` managed string in `dz/dynamic`
with a fixed-capacity byte buffer, then fill it in place.

Implementation (prototype):

- `experiments/glon-deflate/f2buf.glon` adds three internal helpers:
  - `fb/make n`: allocate a fixed-capacity STRING! with `n` payload cells and
    length 0;
  - `fb/set-length s n`: set the stored byte count;
  - `fb/append-fill s n b`: append `n` copies of byte `b` in S1, then set the
    new length.
  These are masm over the existing `alloc`/`@`/`!` mechanisms, not a new VM
  primitive or host ABI.
- `dz/dynamic` allocates **one 320-byte-capacity buffer per dynamic header**,
  resets it, appends each code length or run in place, then passes the buffer
  and explicit `[0, dz-nl)` / `[dz-nl, dz-tot)` ranges to `hf/build`.
- `dz/ls-add` rejects a malformed run that would exceed `dz-tot` before it can
  write past the buffer (preserving the `'repeat` error path).
- `hf/build`, `hf/counts`, `hf/syms` and `hf/cat` gained explicit range/base
  arguments so they can consume the full buffer without allocating `s/slice` /
  `s/span` copies.
- The buffer is local to the header and becomes garbage after the tables are
  built. It is not retained for the whole decoder, so it does not reduce the
  free extent while output is being produced.

Why this is the smallest change: it removes the actual failure mechanism
(growing contiguous managed allocation) while preserving DEFLATE semantics,
the existing streaming contract, the existing table format, and the existing
output API. It does not require a mutable-window rewrite or a decoder rewrite.

## B. Explicit streaming decoder state

The decoder already has this: the bit reader, pending match, window position,
input position, table selection and `need-input` resumption are explicit and
survive calls. The remaining state work would be to replace strings with
address/length buffers for input, window and output. That is larger than the
observed failure and would change the decoder's storage and output API.

Complexity: medium/high. Main risks: overlapping LZ77 copies into a mutable
window, root/GC safety for raw addresses, and output buffer lifetime.

## C. Forth-style address-and-length decoder

Use explicit memory regions, fixed tables, a bounded/circular history and a
caller-provided output buffer throughout. This is the cleanest long-term
architecture for zero-allocation steady state, but it is a decoder rewrite and
a new memory contract.

Complexity: high. Semantic changes: output representation, allocation/ownership,
error/resumption boundaries. It is not justified for the 320-cell failure.

## Comparison

| | A: bounded code lengths | B: explicit state | C: address/length rewrite |
|---|---|---|---|
| Scope | one dynamic-header path | input/window/output state | full decoder |
| Removes 320 failure | **yes** | yes | yes |
| Preserves API | yes | mostly | no (output/ownership) |
| Zero steady-state allocations | no | partially | yes, by design |
| 32 KiB window | no | no at 64K | no at 64K |
| Risk | low/medium | medium/high | high |
| F2 decision | **implemented and verified** | not needed for this failure | separate phase |

## Zero-allocation boundary

F2 proves that the **specific** code-length failure is avoidable. It does not
make the whole decoder zero-allocation:

- one fixed buffer is allocated per dynamic header;
- table symbol strings, window segments, output pieces and the 256-byte read
  result remain managed strings;
- Glon closures/contexts allocate per call;
- the app framing allocates per read.

A true zero-allocation steady state needs B/C plus a mutable byte-region
facility and a caller-owned output buffer. That is a separately approved
implementation phase, not part of F2.

## Rejections

- **Do not redesign the GC:** F1 already showed coalescing works; the failure
  is placement, and a moving collector would require a full raw-pointer audit.
- **Do not increase the heap:** forbidden, and it would hide rather than remove
  the repeated-growth representation.
- **Do not select C merely because it resembles Forth:** the smallest change
  that removes the failure is A.
