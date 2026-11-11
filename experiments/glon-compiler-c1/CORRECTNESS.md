# CORRECTNESS.md — independent kernel tests

## Vectors

The vector is `(c, l, left)`, where `c` is 16 per-length code counts. The
reference is a direct Python/C recursive implementation of the unchanged
`hf/left` source; the production Glon path is also run for every vector.

| Vector | `l`, `left` | Expected |
|---|---|---:|
| all_zero | 1, 1 | 32768 |
| complete_len1_2 | 1, 1 | 0 |
| incomplete_len1_1 | 1, 1 | 16384 |
| over_len1_3 | 1, 1 | -1 |
| len2_4 | 1, 1 | 0 |
| len3_8 | 1, 1 | 0 |
| two_codes_len2 | 2, 1 | -1 |
| boundary_l16 | 16, 123 | 123 |
| zero_left | 1, 0 | 0 |
| partial | 1, 1 | 0 |

These cover full length-1..15 recursion, over-subscribed early exit, incomplete
code, empty table, zero remaining space, the `l > 15` boundary, and multiple
different code-count patterns. They reject constant-answer shortcuts.

## Results

- **Production Glon reference path:** 10/10 results match expected. Each test
  was executed as `./glon jupyter/prelude.glon reference/production/<vector>.glon`;
  the CLI emitted `#[func]` for the prelude and the expected integer.
- **C1 candidate (CALL/RET/>R/R>/R@):** 10/10 match expected; all stack balances
  pass. Aggregate 8,960 instructions, function code 157 cells, total with
  trampoline 160 cells.
- **Seven-operation direct lowering:** 10/10 match expected; all stack balances
  pass. Aggregate 17,644 instructions, function code 279 cells, total 301
  cells.

Reference values are also independently computed by the C harness
(`hf_ref`) and the Python vector/table generator. No output was precomputed
from the compiled code.

## Runtime invariants

After each call the harness checks:

- `SP == DS_INIT - 1`, exactly one result;
- `RP == RS_INIT`, no leaked return-stack entries;
- result equals the independent reference;
- no timeouts or machine bad-opcode failures.

The block is initialized for every vector; no stale block value is reused.

## Fragmentation distinction

The historical DEFLATE fragmentation failure concerns input buffering /
managed-heap placement in the full decoder. `hf/left` is an integer/count-table
function and does not exercise that path. Its original and compiled results
match on every vector; C1 does not claim to fix or reproduce the fragmentation
defect inside this kernel. See `FRAGMENTATION.md`.
