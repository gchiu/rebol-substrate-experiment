# KERNEL-SELECTION.md — selected DEFLATE kernel

## Selected source

**File:** `experiments/glon-deflate/inflate.glon`
**Function:** `hf/left`
**Source commit:** `2fb4cbbcd9d98c56690060d4b2363471b4cd8f27` (the DEFLATE source
is unchanged at the B8/C1 repository HEAD)

Exact source, copied without modification to `reference/hf_left.glon`:

```
hf/left: func [c l left] [
    either > l 15 [ left ] [
        either < - * 2 left block-at c l 0 [ -1 ] [ hf/left c + l 1 - * 2 left block-at c l ]
    ]
]
```

The extracted slice and the authoritative function block have the same SHA-256:
`cc9986942244cac9403961e850ca4a481b20207b70749398e6ea2aa9b3682c91`.
No whitespace, operator, token or output was changed.

`hf/left` is the Huffman code-space validation step used by `hf/build`: given a
block of per-length code counts `c`, starting length `l` and remaining code
space `left`, it folds through lengths 1–15; a negative remainder means the
code is over-subscribed, and a positive final value means incomplete (or empty).
It is real DEFLATE code, not a substitute.

## Why this kernel

B8 ranked bit extraction first, Huffman decoding second, Huffman table
construction third, and LZ77 third/fourth. `hf/left` is in the Huffman table
construction family and needs the smallest genuine dependency set:

- block value parameter (`c`);
- `block-at`;
- recursion, integer arithmetic/comparison and `either`;
- no set-words/locals, strings, division, closures, `do`, `judge`, `invoke`,
  `s/` or masm;
- it exercises the five candidate operations: CALL, RET, >R, R>, R@ (via the
  function's parameter prologue/reads), but no BRANCH native.

More ambitious functions need additional features:

- `hf/walk` adds set-words/locals (`hf-c`, `hf-k`) and division;
- the bit reader adds strings and closures (`br/make*` returns a closure block);
- `hf/build`/`dz/dynamic` add multiple locals, strings/blocks, errors and
  closures;
- LZ77 copying adds strings, slices and managed-block operations.

Choosing `hf/left` gives a real, unchanged kernel whose only missing compiler
piece is `block-at` plus function-only module support. It does not exercise
input buffering, fragmentation or bit-buffer preservation; those limitations are
explicitly carried forward to C2 rather than hidden.
