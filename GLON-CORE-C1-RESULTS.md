# GLON-CORE-C1 — Hash and capacity unification

Date: 2026-11-27
Branch: `core/c1`, from D1 `3a194a2` (the verified C0.1 baseline). Worktree:
`/home/developer/projects/glon-core-c1`. No later branch was merged or
cherry-picked. Builds and tests ran one at a time, each with a time limit.

**Result:** the global-context hash radix is now the configured global-context
capacity, and global and symbol capacities are profile parameters. The
constrained 64K reference is unchanged; this is verified cell by cell and by
D1's full check set. Every acceptance criterion passed.

## 1. Changes

| file | change |
|---|---|
| `r0_s1.h` | `R0S1_GLOBAL_CAP` (default 256) and `R0S1_SYM_CAP` (default 512) are overridable, and `R0S1_HASH_RADIX` is defined equal to `R0S1_GLOBAL_CAP`. Six static assertions (below). The hash-layout comment now names the radix. |
| `r0_s1_runtime.c` | Six substitutions, all same-size: `syms[R0S1_SYM_CAP]`; `make_context(R0_NONE, R0S1_GLOBAL_CAP)`; the radix in the C bootstrap `bind` and in the emitted lookup and insert (`asm_lit(256)` becomes `asm_lit(R0S1_HASH_RADIX)`, so emitted code size is unchanged). |
| `Makefile` | `MEM_GLOBAL_CAP` and `MEM_SYM_CAP`, passed only by the large profile. **The constrained profile refuses any other value.** Both are tracked in `.glon-config`. New targets: `global-ctx-hash-constrained` (added to `make test`), `global-ctx-hash-profile`, `global-ctx-hash-test`, `hash-capacity-check`. |
| `global_ctx_hash_test.c` | New regression test, built once per profile. |
| `.gitignore` | The test binaries. |

**Static assertions**, each rejecting a build at compile time:

1. global capacity ≥ 256;
2. child-context capacity (16) ≤ radix;
3. global capacity ≤ radix;
4. symbols ≥ global bindings;
5. **largest entry, `SYM_CAP × radix − 1`, ≤ 2,147,483,647**, so the encoding fits a 32-bit cell (the WASM build);
6. the global context (3 + 3 × cap cells) fits in the loader heap.

**Not changed:** frozen S1, the HOST table, the memory map, the loader, compiler
and evaluator semantics, task scheduling, JSON, HTTPS and applications. No
diagnostics (`HOST_TRACE_FRAME`, H2Z, halt-site tracing) were introduced.

## 2. Profiles and measured layout

| | constrained (64K reference) | large (D1 default) | large + G3A loader |
|---|---|---|---|
| S1 object | frozen `s1.c` | generated copy (only `MEM_CELLS` differs) | generated copy |
| memory cells | 65,536 | 1,333,104 | 1,633,104 |
| code / data stack / return stack | code from 256, limit 16,128; data stack 16,384 down; return stack 24,576 down | same | same |
| managed heap | [32,768, 40,000) = 7,232 | [32,768, 1,081,344) = 1,048,576 | same |
| GC worklist | 25,057, 256 entries | 1,081,344, 40,000 entries | same |
| loader | [40,000, 54,800) = 14,800 | [1,121,344, 1,321,344) = 200,000 | [1,121,344, 1,621,344) = 500,000 |
| task arena / table | 54,800 / 65,000 (3 × 3,400) | 1,321,344 / 1,331,544 | 1,621,344 / 1,631,544 |
| globals / symbols / radix | **256 / 512 / 256 (fixed)** | 256 / 512 / 256 (default; configurable) | same |

`arch-check` on the constrained build reports `mem_cells=65536 gc_heap=32768..40000
loader=40000..54800 m1_arena=54800 m1_task_table=65000 worklist_cap=256` and
`ARCH_CHECK PASS`, identical to D1.

## 3. Test results (`core/c1`)

| test | result |
|---|---|
| `check-frozen-s1.sh` | **PASS**: all frozen files equal `f90496c…` |
| `arch-check` | **PASS**, the constrained map unchanged |
| `global-ctx-hash-test`, cap 256 (constrained) | **PASS**: 252 entries all exactly `id*256+slot`, every slot indexed once, largest entry 67,067; every binding resolves; high-slot rebind and nested function call; fill to 256; one more is a clean `CONTEXT_FULL` that leaves the context unchanged |
| … cap 512 / 1,024 symbols (large) | **PASS**: 508 entries exact; bindings in slots below and above 256 resolve; largest entry 265,211 |
| … cap 4,096 / 8,192 symbols (large) | **PASS**: 4,092 entries exact; largest entry 16,801,787 (limit 2,147,483,647) |
| `hash-capacity-check` | **PASS**: rejects 128 globals, 256 symbols under 512 globals, 4,096 × 1,048,576 (32-bit overflow) and 8,192 globals in the 64K loader, each by its own assertion; 4,096 / 8,192 compiles |
| The defect is detected | radix forced to 256 with cap 512: the build is refused ("a global-context slot would not fit below the hash radix"); with that assertion removed, the test fails 7 checks ("252 wrong; first hx-234") |
| `./s1` (900 s bound, stdout) | **956 named passing checks, identical to D1's set; 0 FAIL lines**; same stopping point, NaSch "8: uniform flow has no collisions" |
| G1E tuple-space checks 26–46 (inside `s1`) | **PASS**, so constrained code-space headroom is unchanged |
| `gc-worklist-overflow-test` | PASS |
| primer `--check` | PASS (30 examples) |
| `json-test` (large, `MODULES=`) | **JSON_TESTS PASS** |
| `m1-large-test` (large) | **all M1 tests passed, `M1_LARGE failures=0`** |
| `https-g3a-test` (large, file+http, loader 500,000) | **HTTPS_G3A_ACCEPTANCE PASS** (10 groups) |

All three targets named in the acceptance criteria exist at D1 and were run.
`make test` as a whole wasn't run as one command, because `./s1` doesn't finish
within the bound (the known NaSch stall, present at D1); its five steps were run
individually as above.

## 4. Compatibility matrix

| line | hash encoding | globals / symbols | compatible with `core/c1`? |
|---|---|---|---|
| D1 / constrained reference | `id*256+slot` | 256 / 512 | **identical** (verified) |
| AVP G6h `adc9ab6` (`CTX_HASH_STRIDE` = cap: 256, or 512 under the AVP flag) | radix = cap | 256 or 512 / 512 | **same design.** The AVP 512 build corresponds to `MEM_GLOBAL_CAP=512`, but needs `MEM_SYM_CAP=512` (allowed: symbols ≥ globals). Its loader-in-task-arena override is separate (C2). |
| GUI M-1 `8685021` (recorded in OpenViking only) | `id*cap+slot` | 256/512 … 16,384/32,768 | **same design** as recorded. Every recorded GUI profile passes the 32-bit assertion: 4M is 16,384 × 32,768 = 2²⁹ (largest entry 536,870,911); a further doubling (32,768 / 65,536 = 2³¹) would be rejected. Not verifiable: the commits are missing. |
| HTTPS `1512bbb` (fixed radix 65,536; 1,024 symbols everywhere) | `id*65536+slot` | 256–512 / 1,024 | **Not adopted:** it changes the constrained image. Its server configuration maps to `MEM_GLOBAL_CAP=512 MEM_SYM_CAP=1024`. |

## 5. Unresolved dependencies (for C2 and later)

1. **The GUI branch is missing** (`8685021`, MS-1 `c1b3638`, …: "not pushed", in
   `/mnt/d/repos` worktrees). Its recorded capacities (up to 16,384 / 32,768 at
   4M) fit the 32-bit assertion. Any profile beyond them needs a decision: cap
   it, or apply the 32-bit limit only to WASM builds.
2. **The single-generator profile mechanism is not done** (C2): HTTPS
   `MEM_CODE_CAP` with the 73fcc44 return-stack guard, GUI's alternate-header
   presets, AVP's `R0S1_HEAP_LIMIT` override.
3. **The task-record extensions, HTTP bridge, H3E and JSON J1/J2** remain on the
   HTTPS line, to be ported in later milestones.
4. The NaSch simulation stall in `./s1` (pre-existing at D1) limits `make test`
   as a single command.
5. The minimum global capacity (256) is a conservative assertion. Smaller
   embedded profiles would need the bootstrap binding count measured first.

Committed locally on `core/c1` only. Not pushed, merged, rebased or deployed. C2
not started.
