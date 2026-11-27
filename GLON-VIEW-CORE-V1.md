# GLON-VIEW-CORE-V1 — C2 core plus View runtime and language changes

Date: 2026-11-27.

| | |
|---|---|
| **Worktree** | `/mnt/d/repos/glon-core-view-integration` |
| **Branch** | `integration/c2-view`, created from exactly C2 `4dcae2aa10e275f23e7b2c1904fa45f8b888e243` |
| **Specification** | `GLON-VIEW-CORE-V0.md` (glon-gui-f2 worktree, uncommitted audit) |
| **Status** | Committed locally on `integration/c2-view` only. Nothing pushed or merged. `backup/glon-core-c2` and `glon-gui-f2` are untouched, as are both of their worktrees. |

**What V1 does and does not include:**
- C2 supplies the machine: S1 execution, the memory map, memory profiles and the fixed-radix hash encoding.
- glon-gui-f2 supplies five runtime and language changes, ported in separately committed and verified steps.
- No View, VID, Intent, SDL or window code was migrated (that is V3).
- Frozen S1 is unchanged (`f90496c…`, checked after every step).

**Evidence tags:**

| Tag | Meaning |
|---|---|
| **[M]** | Measured in this milestone |
| **[S]** | A source fact |
| **[A]** | An assumption or estimate |

Logs are in `/mnt/d/repos/diagnostics/core-v1/`: `s0-baseline`, `s1-site`, `s2-mem`, `s3-intent7`, `s4-parser`, `s5-vocab`, and `tools/`.

## 1. Commits and source provenance

| Commit | Step | Ported from glon-gui-f2 | Files |
|---|---|---|---|
| `3c3ff630` | 1/5 SITE-1 | `6fc3f404` (adapted: new profile region) | `scripts/glon-profile.sh`, `profiles/*.profile`, `r0_s1.h`, `r0_s1_layout.h`, `r0_s1_runtime.c` (`r0_s1_site_usage`), `tests/core-c2/reject/*`, `reject_check.sh`, `tests/core-v1/site_capacity_test.c` (from `view/tests/test_site_capacity.c`), `Makefile` |
| `db21baa4` | literal-block GC regression test | test sources from `tests/vocab1` (`aff57259`) | `tests/core-v1/literal_block_gc.{glon,sh}`, `managed_block_gc.glon`, `Makefile` |
| `b86aa793` | 2/5 allocator and collector | `689aeb51` (MEM-2), `593c8103` (MEM-3), `34128ac1` (MEM-5), `5f7ff51b` (MEM-4, diagnostic only), `00ccff8b` (GC interior tests) | `r0_s1.h`, `r0_s1_runtime.c`, `r0_s1_gc_interior_tests.c`, `tests/core-v1/{mem2_alloc_diff.c,mem5_closure_diff.c,gc_interior_main.c,mem_diff.sh}` (from `view/tests/`), `Makefile` |
| `ef0bdec2` | 3/5 INTENT-7 | `cdab6d12` | `r0_s1.h`, `r0_s1_runtime.c`, `tests/core-v1/intent7_set.{glon,sh}`, `Makefile` |
| `ac4a8878` | 4/5 parser fix | `01c636eb` | `r0_s1_runtime.c`, `r0_s1_parse_hardening_tests.c` |
| `42d49bb8` | 5/5 GLON-VOCAB-1 | `3b7b7f7e` (loader, show, glon.c, primer), `19b76e40` (debug tests, vocab1), `aff57259` (vocab1 additions); libraries as of `17b819db` | `r0_s1_runtime.c`, `r0_s1_show.c`, `glon.c`, `r0_s1_debug_tests.c`, `Makefile`, `demo/shop/{binding,numbers,strings,strings-ext,strings-text,block,list}.glon`, `demo/shop/primer.{txt,html}`, `GLON-PRIMER.md`, `tests/vocab1/` |
| (this commit) | report | `7fdc920e` (`GLON-ALPHA-LAWS.md` VOCAB-1 laws, `GLON-VOCAB-1.md`) | this file, `GLON-ALPHA-LAWS.md`, `GLON-VOCAB-1.md`, `MEMORY.md`, `GLON-ARCHITECTURE-MEMORY.md` |

**Porting method.** Runtime diffs were generated from each glon-gui-f2 commit, restricted to the runtime and test files. They were applied with `patch` in the order above. Every hunk applied without conflict. No commit was cherry-picked or merged.

**Not ported, on purpose:**
- **The runtime part of M-1 `86850213`** (hash entry = `id*cap+slot`). C2's encoding is authoritative (§2).
- **`experiments/ms1/`** (MS-1 alternate headers). C2's profile generator replaces them.
- **Everything under `view/`.**

## 2. Conflict resolutions

| Area | glon-gui-f2 | C2 | Resolution |
|---|---|---|---|
| **Hash-entry encoding** | `id*cap+slot`, per context (M-1) | `id*R0S1_HASH_RADIX+slot`, radix = global capacity | **C2 kept unchanged.** INTENT-7's FIND-ADDR runs inside C2's LOOKUP, which decodes with the radix and derives the value-cell address from the decoded slot. No hash code was edited. [S] Checked by `intent7-set-check` at radix 256 / 4,096 / 16,384 and by the hashed-vs-linear differential tests [M] |
| **Function-site table** | MS-1 moves the managed heap up and puts the table at 32,768 | heap fixed at 32,768; table fixed at 32,024, 640 sites, in every profile | **New declared profile region.** `SITES=fixed` keeps the 64K slot; `SITES=after-worklist` + `SITE_CAP` places it between the heap or worklist and the loader. Desktop profiles: 2,048 / 4,096 / 8,192. All other profiles stay fixed/640, so their layouts are unchanged. The generator rejects a non-640 fixed table and a region below 640. `r0_s1_layout.h` rejects an overlapping table. |
| **RV cells** | `GC_FREE_HINT` = RV_BASE+37, `RV_FINDMODE` = RV_BASE+38 | both unused (36 → 40) | same cells, with the existing static assertions [S] |
| **Collector loader-range test** | loader from `R0S1_HEAP_BASE` | `R0S1_LOAD_BASE` plus the worklist-gap corruption check | C2 kept. The site table is raw ids and is never scanned, so it sits safely in the C2 gap [S] |
| **Libraries** | VOCAB-1 versions of `strings.glon` / `strings-ext.glon` | unchanged from the merge base | glon-gui-f2 versions (C2 made no edits) [S] |
| **`demo/shop/primer.html`** | VOCAB-1 regenerated | C2 edited the home view | VOCAB-1's diff applied over C2's file; `build-primer.py --check` passes [M] |

## 3. Measurements (64K = `constrained`, 1M = `desktop-1m`, 4M = `desktop-4m`) [M]

### 3.1 Emitted runtime and code headroom

Emitted cells are the same in every profile.

| After | Emitted cells | Code free, default libraries (prelude, binding\*, strings) | Code free, root library set† |
|---|---:|---:|---:|
| C2 baseline | 12,870 | 2,282 | — |
| 1 SITE-1 | 12,870 | 2,282 | — |
| 2 MEM-2/3/4/5 | 12,924 | 2,228 | — |
| 3 INTENT-7 | 12,890 | 2,262 | — |
| 4 parser fix | 12,890 | 2,262 | — |
| 5 VOCAB-1 | 12,890 | 2,195\* | 1,308 |

\* `binding.glon` is loaded from step 5 on.
† prelude, binding, strings, strings-ext, case, block, list, strings-text, numbers.

- **Code limit:** `R0S1_CODE_LIMIT` is 16,128 everywhere.
- **Per-step changes:**
  - MEM-2 adds 55 cells; MEM-3 removes 1.
  - INTENT-7 removes 34 (SET reuses LOOKUP).
  - VOCAB-1 changes the loader only.

**Intent headroom check.** The full View/Intent library stack, the window library and the c1 acceptance application were read from glon-gui-f2 and loaded into the integrated runtime. They were not migrated.

| Runtime | Profile | Globals | Sites | Loader used | Managed above base | Code free |
|---|---|---:|---:|---:|---:|---:|
| integrated (C2 + V1) | 1M | 1,006 / 4,096 | 847 / 4,096 | 110,825 | 41,168 | **52** |
| integrated (C2 + V1) | 4M | 1,006 / 16,384 | 847 / 8,192 | 147,689 | 41,168 | **52** |
| glon-gui-f2 (MS-1 build, same probe) | 1M | 1,006 / 4,096 | 847 / 4,096 | 110,825 | 41,168 | 58 |
| glon-gui-f2 (MS-1 build, same probe) | 4M | 1,006 / 16,384 | 847 / 8,192 | 147,689 | 41,168 | 58 |

- Globals, sites and loader use are identical. Managed use relative to the heap base is identical too.
- Intent sessions lose **6 code cells**, from 58 to 52: the integrated runtime emits 12,890 cells against glon-gui-f2's 12,884.
  - C2 adds 9 cells; the V1 ports add 20.
  - The glon-gui-f2 figure of 12,884 includes the same ports on the older base, 12,861.
- `GLON-VIEW-CORE-V0` estimated about 49 cells free; the measurement is 52.

### 3.2 Capacities and layout

| Profile | Managed | Worklist | Site table | Loader | Globals / symbols / radix |
|---|---:|---:|---|---:|---|
| 64K | 7,232 | 256 (fixed) | 32,024 + 640 (fixed) | 14,800 | 256 / 512 / 256 |
| 1M | 262,144 | 8,192 | 303,104 + 4,096 | **730,640** (C2: 734,736) | 4,096 / 8,192 / 4,096 |
| 4M | 1,048,576 | 32,768 | 1,114,112 + 8,192 | **3,061,264** (C2: 3,069,456) | 16,384 / 32,768 / 16,384 |

- Only the desktop loaders shrink, each by exactly its site table.
- The 64K map is unchanged: `arch-check` and `profile-check` reproduce C2's `LAYOUT` line exactly.
- Function-site capacity from `site-capacity-test`, all 8 profiles: 640 (64K, notask, large, server, server-https), 2,048 (256K), 4,096 (1M), 8,192 (4M).

### 3.3 Performance of the ported optimisations

All timings are C2's `-O0 -g` builds, so they are relative only.

| Workload | Old code | Ported | |
|---|---:|---:|---|
| MEM-2/3 allocator workload, 4M | 71.6 s | 25.3 s | scan from base vs hint |
| MEM-2/3 allocator workload, 1M | 27.0 s | 20.9 s | |
| MEM-5 closure workload, 4M | 46.9 s | 36.6 s | walk always vs mark-aware check |
| MEM-5 closure workload, 1M | 20.0 s | 17.1 s | |
| INTENT-7: 3,000 globals, set-word-heavy functions, 1M | 17.8 s | 0.51 s | linear vs hashed set |
| 7 Kaka harnesses (`make kaka-test`) | 121 s (step 1) | 64 s (step 2), 55 s (step 3) | |
| `./s1` | 704 s (C2), 725 s (step 1) | 657 s (step 2), 371 s (step 3), 353 s (step 4) | step 5: 467 s, run alongside the 9-minute differential suite |

## 4. Tests and results per step [M]

Each step ran the full sequence below, with exit statuses recorded, plus a bounded `./s1` (960 s) from a copy of the binary.

The sequence:
- `check-frozen-s1`, `arch-check`, `profile-check`, `profile-reject-check`, `profile-common-test`;
- `global-ctx-hash-test`, `hash-capacity-check`;
- `gc-worklist-overflow`, `primer --check`, `json-test`, `m1-large-test`;
- `glon-live-native-test`, `patrol-test`, `kaka-test` (all 7 harnesses);
- `build-deps-check`.

Each new test joins from its step on:
- `site-capacity-test` (1);
- `literal-block-gc-check`;
- `mem-diff-test` (2, 3, 5; skipped at 4, a parser-only change);
- `intent7-set-check` (3);
- `vocab1`, `io-file-*` (5).

| Check | C2 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| The common sequence above, except `build-deps` | pass | pass | pass | pass | pass | pass |
| `build-deps-check` | **27/30** | 27/30 | 27/30 | 27/30 | 27/30 | 27/30 |
| `./s1` `ok:` lines / FAIL lines | 1,010 / 0 | 1,010 / 0 | 1,016 / 0 (+6 GC interior) | 1,016 / 0 | 1,025 / 0 (+9 parser O1–O9) | 1,025 / 0 |
| `site-capacity-test` (8 profiles) | — | pass | pass | pass | pass | pass |
| `literal-block-gc-check` (64K/1M/4M) | defect reproduced | = | = | = | = | = |
| `mem-diff-test` (64K/256K/1M/4M) | — | — | 16 ok | 24 ok | — | 24 ok |
| `intent7-set-check` (64K/1M/4M) | — | — | — | pass | pass | pass |
| `vocab1` (`tests/vocab1/run.sh`) | 2 of 23 (V0 audit) | — | — | — | — | **23 / 23**, 3 known reports unchanged; also in 1M and 4M |
| `io-file-test` / stream / in-place | 39/0, 0 fail, 36/0 | — | — | — | — | 39/0, 0 fail, **5 pass / 31 fail** (§5.1) |
| `desktop-test`, `machine-integrity-test`, `iterators-test`, `host-test` | pass | — | — | — | — | pass (identical) |

The `./s1` lines were compared after each step; dump addresses and interleaved stderr were ignored. Apart from the added checks, the `ok:` lines are identical to C2's.

**Environmental failures.** These fail identically on unchanged C2, for reasons outside this repository:
- `build-deps-check` cases 5 (3 checks): CivetWeb needs `openssl/bn.h`, and the OpenSSL headers are not installed.
- `kernel-test` and `kernelspec-test`: the Python module `jupyter_client` is not installed.

**Not run:**
- the HTTPS G1–G3A acceptance suites (OpenSSL headers absent);
- WASM targets (no emcc, clang or node);
- Windows targets (no MinGW);
- View, VID and Intent suites (V3);
- X11, WSLg and WebKitGTK (out of V1 scope).

C2's report records a NaSch stall in `./s1`. Here `./s1` completes and exits 0, at C2 and at every step.

## 5. Remaining limitations and open issues

### 5.1 Known incompatibility: C2 `s/` code outside the `s` vocabulary (V2)

`io-file-inplace-test` fails 31 of its 36 cases after step 5; on C2 all 36 pass. This is explained, and it was not worked around:
- **The refused definitions.** The test loads `experiments/masm-vocabulary/find.glon`. That file defines 7 `s/` members (`s/find-glon-small`, …) from outside `vocabulary s`, which GLON-VOCAB-1 refuses at load by design:
  `r0_s1: s/find-glon-small: no member 'find-glon-small' in vocabulary s`.
- **A real public-name collision.** C2's `find.glon` makes `s/find` mean `text pattern` (2 arguments). VOCAB-1's `strings-text.glon` defines `s/find s i p` (3 arguments).
- **More code of the same kind.** `modules/strings.glon`, `modules/file-replace.glon` (`s/replace-first`, …), `experiments/masm2/trace.glon` and `experiments/masm3/slice2*.glon` also define `s/` members from outside the vocabulary.

A diagnostic copy shows the minimal conversion works. It moves `find.glon`'s definitions inside `vocabulary s [ … ]` and sets `find: :find-masm` inside the body. It reproduces C2's results (`4 11 2`) and was not committed. **V2 must decide:** convert the C2 sources, and choose the meaning of `s/find`. Either rename C2's `s/find` to `s/index-of` or similar, or rename View's.

### 5.2 Literal-block GC hazard: unresolved, now an explicit test

`make literal-block-gc-check` passes only while the recorded defect reproduces exactly (`"xxxxxxxxyyyyyyyy" 1`). It reproduces in 64K, 1M and 4M at every step, and the control (a managed block) survives. A changed outcome makes the check exit non-zero.

**Cause [S].** The collector classifies a `T_BLOCK` whose payload lies in `[R0S1_LOAD_BASE, R0S1_HEAP_LIMIT)` as a permanent loader block with no children (`emit_mark_value`, the "loader block: no children" exit). Loader blocks have no header and no mark bit. The runtime has no native that mutates a block: the natives are arithmetic, comparison, `print`, `values`, `either`, `do`, `invoke`, `reduce` and the SIN! words. So the only way a managed value reaches a literal block is a raw `masm` store. The common one is the library word `block-set!`, which is defined in `demo/shop/block.glon`, View's `view/glon/core_words.glon`, and 11 private copies: seven `demo/shop/demos/*`, Kaka's `kaka-lib.glon` and three HTTPS examples. `experiments/` and `tests/` were not counted.

**Correction options.** Analysed only; none implemented, and the literal semantics are unchanged.

| Option | Change | Safety | Cost and semantic impact |
|---|---|---|---|
| A. Trace loader blocks' elements | collector | complete for every store, including raw `masm` | Loader blocks have no mark bit, so cycles made by stores would loop or overflow the worklist. It needs a visited bitmap for the loader region (about 46K bits at 1M), which is a C2 memory-map change. Every collection would also retrace all reachable literal code; the global-context retrace is already the dominant collection cost (GLON-MEM-5). |
| B. A checked `block-set!` refuses a managed value stored into a loader block | library (`block.glon`, `core_words.glon`) | complete for the checked word; raw `masm` stays unchecked by definition | Smallest change. Immediate values (integers, words) keep working; the demos are expected to store only those [A], which is unverified. Storing a managed value into a literal block becomes a SIN! instead of silent corruption. About 20 more `masm` code cells per definition [A], against 52 free in Intent sessions. The 11 private copies would need the same change. |
| C. Copy a literal before the first store | semantics | complete | Changes literal identity, which GLON-LANG-1 §2.4 defines as shared. Rejected. |
| D. A remembered set: the checked store records the target as a root | collector plus a fixed-state root table | as B for the checked word | Keeps the stores working, but needs a root table and collector changes (a C2 memory-map decision). |

**Recommendation.** Make B the smallest safe correction, as its own milestone:
- `block-set!` refuses with `SIN! 'type 'block-set!` when the target lies in the loader range and the value is a managed reference;
- the 11 private copies are switched to the checked word;
- `literal-block-gc-check` changes to expect the refusal.

A and D need the C2 owner, because they change the collector or the memory map. Before B, measure whether any current program stores managed values into literal blocks. Not measured here [A]: View, Intent and VID do not, by convention (`GLON-VIEW-C1-CONTRACT.md` §1.4).

### 5.3 Vocabulary defects carried over unchanged

- **V1, V2:** forward references to nested vocabularies. `vocab1` still reports them as `known`.
- **V3:** adoption is one level deep.
- **V4:** prefixed definitions before the declaration in the same source.
- **The `get` / `intent/get` collision:** recorded in GLON-VIEW-PREP-1.

None was changed; these are post-integration fixes.

### 5.4 Other

- **Desktop profiles** are still marked `provisional` in `profiles/*.profile`. Their capacities now match the View records and are measured here, but V3 (the View suites on these profiles) must pass before they become `verified`.
- **Hash entries at 4M:** the largest entry is 32,768 × 16,384 − 1 = 536,870,911, which is under C2's 32-bit assertion. A further doubling of either capacity is refused at build time.
- **WASM loaders** embed the same loader and have not been rebuilt or run with VOCAB-1.
- **`modules/strings.glon`** is what `glon-desktop` serves to the browser as `/strings.glon`. It is a flat pre-VOCAB-1 copy and needs converting in V2.
- **`tests/vocab1/run.sh`** messages refer to `GLON-VIEW-C1-CONTRACT.md`, which lives on glon-gui-f2 and not in this branch.
- **`experiments/ms1/`** and View's `GV_*` macros are not here. V3 must switch `view/Makefile` to `MEM_PROFILE=desktop-*`.

## 6. Reproduce

From the worktree:

```sh
make test                      # check-frozen-s1, s1, gc-worklist-overflow, hash, primer, test-vocab1
make site-capacity-test literal-block-gc-check intent7-set-check mem-diff-test
make profile-check profile-reject-check profile-common-test global-ctx-hash-test
make json-test m1-large-test kaka-test patrol-test io-file-test io-file-stream-test
make io-file-inplace-test      # expected to fail 31/36 until V2 (§5.1)
```

The diagnostics scripts used for the per-step records are in `/mnt/d/repos/diagnostics/core-v1/tools/`:
- `suite.sh`, `s1bounded.sh`, `measure.sh`, `probe.c`, `extra.sh`.

## 7. Next

- **V2: language libraries and C2 module conversion.** Decide on `s/find` (§5.1) and convert the outside `s/` definitions. Check that all C2 `.glon` modules load under the lexical rules, which includes running HTTPS on a machine with OpenSSL.
- **The literal-block correction** (§5.2) as its own milestone.
- **V3: View/Intent onto `MEM_PROFILE=desktop-*`,** accepted by `make -C view test-c1-accept`.
- **Do not start V2 before this report is reviewed.**
