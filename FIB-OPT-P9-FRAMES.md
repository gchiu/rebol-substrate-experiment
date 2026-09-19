# FIB-OPT-P9 — Dense activation frame (allocate once, store at fixed offsets)

## 1. Hypothesis

P8 left the activation path dominated by context/frame machinery. P9 tests:

> Can activation-frame save/restore be made substantially denser by allocating
> the frame once and accessing its fixed fields directly, instead of repeatedly
> using derived `>R`/`R>` sequences for each field?

## 2. Exact current frame layout (unchanged, and preserved)

`r0_s1.h`:

```
FRAME_PREV = 0, FRAME_SITE = 1, FRAME_SP = 2, FRAME_RP = 3, FRAME_IP = 4,
FRAME_CTX = 5, FRAME_CUR = 6, FRAME_END = 7, FRAME_BLK = 8
```

Nine 16-bit cells, pushed on the return stack, base 16-aligned. The RAW ABI
exposes `FRAME_PREV/SITE_ID/SAVED_SP/SAVED_RP/SAVED_IP/SAVED_CTX/SAVED_CUR/
SAVED_END/SAVED_BLK`. P9 does **not** change this layout — only how it is
written and read.

## 3. Measured cost (before changing)

Static analysis (opcode cell counts) of the P8 `emit_invoke_closure` frame
path, per activation:

| part | ops |
|---|---|
| padding computation | 14 |
| padding zero loop (6 cells, each via `LIT 0` + derived `>R` = 16 ops + loop overhead) | ~237 |
| 9 field pushes (each `e_cell(X)` + derived `>R` = 17 ops) | ~175 |
| `RV_FRAME = frame base` | 6 |
| **frame save total** | **~432** |
| restore 4 fields | 44 |
| release (compute `frame.RP−1−frame`, add to RP) | 25 |
| `RV_FRAME = prev` | 7 |
| **frame restore total** | **76** |

Total ≈ **508 ops/activation** ≈ 123M ops ≈ **13% of the 925M total**. The
derived `>R` is 14 opcodes (`LIT RP @ LIT 1 SUB LIT RP ! LIT RP @ !`) — it
re-reads the memory-mapped RP and re-writes it on every push, which was the
hypothesised inefficiency.

## 4. Chosen strategy

Reserve the frame once, adjust RP once, store the 9 fields at fixed symbolic
`FRAME_*` offsets, and zero the padding with direct stores:

1. `padding = (RP - 9) mod 16` → `RV_T5`.
2. `frame base = RP - 9 - padding` → `RV_T6`.
3. `RP = frame base` (one store) and `RV_FRAME = frame base`.
4. Store each field: `e_cell(value) e_cell(RV_T6) LIT FRAME_x ADD !`.
5. Zero padding cells via a direct-store loop.

Restoration: load the 4 fields by offset (unchanged), then release with a
single `RP = frame.RP - 1` instead of `RP += (frame.RP - 1 - frame)` (valid
because RP == frame base at the normal epilogue).

## 5. Preservation of semantics / RAW ABI / alignment

- The physical layout, field order, 16-alignment, and padding are **identical**;
  only the emitted write/read instructions differ.
- RAW still sees `FRAME_*` at the same offsets; the frame chain (`prev`), site-id,
  saved SP/RP/IP/CTX/CUR/END/BLK all land in the same cells.
- Padding cells are still zeroed (the GC scans the return-stack region for
  closures, so stray bits must not look like `T_CLOSURE`).
- The `prev` field is stored from the old `RV_FRAME` *before* `RV_FRAME` is
  overwritten, so the frame chain is never broken.
- No S1 primitive, HOST semantic, or RAW ABI change.

## 6. Result

The hypothesis is **largely disproven**: the instruction count fell only
**0.94%** (925,982,568 → 917,242,308), and wall-clock is a wash. The derived
`>R`'s RP manipulation is *not* the bottleneck — moving the memory-mapped RP is
cheap (LIT/`@`/`!` memory access), while the fixed-offset stores introduce
`HOST ADD` address arithmetic that offsets the saving. Frame save/restore is a
large cost (~13%) but is not reducible by denser encoding.

See `FIB-OPT-P9-RESULTS.md` for numbers and the strategic conclusion.
