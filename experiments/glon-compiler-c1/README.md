# GLON C1 — DEFLATE compiler coverage and first kernel

**Status:** isolated compiler/runtime feasibility experiment, 2026-11-10,
branch `glon-shop-g1a`. No production or frozen-S1 change. No C2 started.

## Result

**Level 3 achieved:** the unchanged existing DEFLATE function `hf/left` was
compiled onto the twelve-operation candidate machine (original seven plus CALL,
RET, >R, R>, R@), passed ten independent vectors, and matched the production
Glon path. The compiled candidate uses 157 function cells / 8,960 instructions
over the vector set; seven-op direct lowering uses 279 cells / 17,644
instructions.

It is not a complete decoder and not a Level 4 benchmark: original Glon was used
for correctness, not isolated execution-only timing.

## Selected source

- Authoritative file: `experiments/glon-deflate/inflate.glon`
- Function: `hf/left`
- File commit: `2fb4cbbcd9d98c56690060d4b2363471b4cd8f27`
- Extracted unchanged: `reference/hf_left.glon`
- SHA-256 of both extracted slice and authoritative slice:
  `cc9986942244cac9403961e850ca4a481b20207b70749398e6ea2aa9b3682c91`
- `hf/left` is genuine Huffman code-space validation from DEFLATE; no
  constant-answer substitute.

## What was added

Only under this directory:

- `c1_compiler.c/.h` — copy of the B3 compiler with:
  - function-only modules and named function entry;
  - a `block-at` intrinsic lowered with existing `LIT`/`HOST DIV`/`HOST ADD`/`@`.
- `tools/c1_kernel.c` — harness that installs a fixed 16-cell block, calls the
  compiled `hf/left` with candidate or seven-op flags, checks results and
  stack balance, and times execution.
- `reference/hf_left.glon` — exact extracted function.
- `reference/production/` — production Glon vector invocations.
- `tools/`, `results/` — reproducible measurement and evidence.

No VM primitive, parser syntax, frozen S1, production runtime, host ABI or
DEFLATE source was changed.

## Correctness

Ten vectors cover full recursion, over-subscribed, incomplete, empty, zero-left
and the `l>15` boundary. Production Glon and the compiled candidate both return
the expected value for all ten and maintain one result/clean return stack.
Seven-op direct lowering also passes all ten. See `CORRECTNESS.md` and
`results/reference.json` / `results/compiled.json`.

## Performance

Candidate vs seven-op direct lowering, same source and inputs:

| Measure | Seven-op | Candidate | Ratio |
|---|---:|---:|---:|
| Function cells | 279 | 157 | 1.78× |
| Total cells incl. trampoline | 301 | 160 | 1.88× |
| Aggregate instructions | 17,644 | 8,960 | 1.97× |
| Aggregate exec median | 63.604 µs | 38.661 µs | 1.65× |
| Peak ds/rs | 6 / 64 | 6 / 64 | same |

Timing is noisy; see `PERFORMANCE.md`. Original Glon execution-only timing is
**N/A** (CLI startup/loader/evaluator not isolated).

## Fragmentation

Current `make -C experiments/glon-deflate test`: **44/44 unit**, **479/480
conformance**. The one failure remains `multi-memlevel1` at `c128/r100`, a
heap-fragmentation host fail-stop after 4047 bytes, not a wrong-byte decode.
`hf/left` does not exercise that path. No fix attempted; see
`FRAGMENTATION.md`.

## Reproduce

```
make
./build/c1-count --measure candidate
./build/c1-count --measure none
./build/c1-bench --bench candidate
./build/c1-bench --bench none
python3 tools/run_c1.py
python3 tools/make_reference.py
```

`tools/make_reference.py` runs the production Glon path for each vector.
Machine-readable evidence is in `results/`.

## Documents

`DEFLATE-COVERAGE.md`, `KERNEL-SELECTION.md`, `COMPILER-DESIGN.md`,
`CORRECTNESS.md`, `PERFORMANCE.md`, `FRAGMENTATION.md`,
`C1-RECOMMENDATION.md`.

No production adoption is authorised. Await explicit approval before C2.
