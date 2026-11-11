# B12 — seven vs nine vs twelve primitives on whole DEFLATE

B12 reuses the B11 experimental machine/runtime and the unchanged DEFLATE
fixtures. It provides three selectable evaluator builds:

- **S1-7** — frozen seven-primitive `s1.c` and original runtime emitter.
- **S1-9** — B12 machine with native CALL/RET only; `>R`, `R>`, `R@` remain
  seven-primitive assembler macros. Native return-stack opcodes are rejected by
  the machine's guard if encountered.
- **S1-12** — B12 machine with native CALL/RET/>R/R>/R@.

All builds use the same O2 settings and fixtures. `make test` runs both
profiles' 480/480 conformance and 44/44 unit tests plus frozen-S1; `make bench`
writes `baseline/b12-results.json`.

The S1-9 runtime raw-masm accounting uses the 14-cell macro sizes for >R/R>.
The S1-12 runtime uses the 1-cell native sizes. `build/b12-guard-nine` proves
the nine-profile machine rejects a manually introduced native >R.

No Glon source, DEFLATE, heap, GC, production runtime, frozen S1 or deployed
browser page is changed.
