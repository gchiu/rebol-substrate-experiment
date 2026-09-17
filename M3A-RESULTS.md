# M3A-RESULTS.md — extensible structured datatypes (M3A) results

M3A is implemented and hardened per architectural review. S1 stays frozen at
seven primitives; the evaluator and parser are unchanged; the M2 collector gains
exactly one generic user-object trace path plus two fail-stop corruption checks.

## Final verification

- **Complete native suite:** exit 0.
- **Assertions:** **225 `ok:`** (up from 196 at M2), 0 failures.
- **M3 acceptance tests:** A–Q all pass.
- **`./check-frozen-s1.sh`:** **OK** (`f90496c26dc45c7a387d8fc8639cd2781507d3d2`).
- **Evaluator/parser:** `git diff` shows **0** changes to any `emit_subexpr` /
  `emit_native` / `emit_lookup` / `emit_block_eval` / `parse_*` function.

## Final code size

- Base emitted code: **6798 cells** (was 6509 at M2; +289 for the generic
  user-object GC support — `trace_user`, the `mark_value` T_USER case with
  range check, the drain `GC_KIND_USER` case, the `BUILTIN_TYPE` root, and the
  two fail-stop checks).
- Datatype-library preamble (parse-time RAW fragments + GLON): ~1625 cells.
- Heap impact: 12 bootstrap descriptors (384 cells) seeded at `[32768, 33152)`;
  `REG_HP` starts at 33152 for M3.

## Hardening (per review)

1. **`mark_value` T_USER range check** — a `T_USER` whose payload is outside
   `[GC_HEAP_BASE, GC_HEAP_LIMIT)` is fail-stop heap corruption (HALT), never
   blindly pushed to the worklist.
2. **`trace_user` COUNT validation** — `count > size - 18` is fail-stop heap
   corruption (HALT), never silently clamped.
3. **Recursive datatypes deferred** — `person!: make datatype! [friend:
   person!]` remains inexpressible in ordinary GLON (eager type resolution);
   documented in `M3-DATATYPE-DESIGN.md` §18b. The RAW-manufactured cycle test
   (E) proves GC cycle-correctness.

## New regression tests

- **P** — forged/out-of-range `T_USER` → clean corruption halt (no `bad
  opcode`).
- **Q** — USER `count` beyond allocated extent → clean corruption halt.

## Files to be committed

Modified (tracked):

- `M2-GC-DESIGN.md` — memory-map M3 revision note.
- `Makefile` — build `r0_s1_m3_tests.o`.
- `m1_layout.h` — M1 cells relocated to 9001..9024.
- `main.c` — run `r0_s1_m3_tests`.
- `r0_s1.h` — `T_USER=11`, `GC_KIND_USER=5`, `GC_META`/`BUILTIN_BASE`/scratch
  constants, RV/GC-state relocation, `r0_s1_alloc_addr`/`r0_s1_lookup_addr`.
- `r0_s1_runtime.c` — `trace_user`, `mark_value` T_USER case, drain case,
  `BUILTIN_TYPE` root, bootstrap seeding + word binding, introspection.

New (untracked, to be added):

- `r0_s1_m3_tests.c` — M3A acceptance suite (A–Q).
- `M3-DATATYPE-DESIGN.md` — the M3 design/architecture record.

Pre-existing unrelated untracked files (left untouched): `EXAMPLES-REPORT.md`,
`PHASE4B-REPORT.md`, `R0-CONSISTENCY-NOTES.md`, `R0-S1-AUDIT-NOTES.md`,
`README_GLON.md`, `standalone/app-m1.glon`, `standalone/node_test-m1.js`.

No commit or tag has been made.
