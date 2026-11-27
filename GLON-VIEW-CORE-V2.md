# GLON-VIEW-CORE-V2 — Library compatibility and literal safety

Date: 2026-11-27.

| | |
|---|---|
| **Worktree** | `/mnt/d/repos/glon-core-view-integration` |
| **Branch** | `integration/c2-view`, after V1 `91e7d0f2` |
| **Specification** | `GLON-VIEW-CORE-V1.md` §5 and §7; `GLON-VIEW-CORE-V0.md` (glon-gui-f2 worktree) |
| **Status** | Committed locally only. Nothing pushed or merged. |

**What V2 leaves unchanged:**
- C2's memory map, profiles and fixed-radix hash encoding.
- The runtime, `glon.c`, the profile generator and the profiles: `git diff 91e7d0f2 -- 'r0_s1*' s1.c s1.h glon.c scripts profiles` is empty, and the emitted runtime is still 12,890 cells.
- Frozen S1 (`f90496c…`).
- No View, VID, Intent or native-window code was imported (that is V3).

**Evidence tags:**

| Tag | Meaning |
|---|---|
| **[M]** | Measured |
| **[S]** | A source fact |
| **[A]** | An assumption |

Diagnostics are in `/mnt/d/repos/diagnostics/core-v2/`:
- `tools/` (the analyzers);
- `lexaudit/` (per-session audits);
- `lbaudit.*` (the instrumented store audit);
- `measure.*`, `v1-export.*`, `s0/` (C2 harness baselines);
- `v2-suite/` (the regression run).

## 1. Commits

| Commit | Subject |
|---|---|
| `1ea05c09` | `s/index-of` for C2's two-argument search; `s/` experiments as vocabulary members |
| `26559654` | `modules/strings.glon` and `modules/file-replace.glon` as legitimate `s` vocabulary members |
| `9bdc7074` | static library audit: lexical-binding divergence and duplicate definitions |
| `acb46052` | literal-safe `block-set!`: a managed value only into a managed block |
| (this commit) | report and milestone notes |

## 2. The `s/find` conflict

**The two definitions [S]:**

| | C2 `experiments/masm-vocabulary/find.glon` | Shared `demo/shop/strings-text.glon` (GLON-VOCAB-1) |
|---|---|---|
| Call | `s/find text pattern` | `s/find s i p` |
| Not found | **-1** | **the length** (scan clamp) |
| Start index | none (always 0) | `i`, clamped |
| Error | SIN! `'type 's/find` | SIN! `'type 's/find` |

**Call sites of the 2-argument form [S].** No C2 module, test program or C source calls it. The callers are all in `experiments/`:
- `masm-vocabulary/file-stream-masm.glon`, which `io-file-inplace-test` loads;
- `masm3/file-stream-masm3.glon` (4 calls);
- `masm2/trace.glon` (a counting wrapper);
- `masm-vocabulary/cases.py`.

The 3-argument form is used by `strings-text.glon` itself, View (V3) and `tests/vocab1/s-app.glon`.

**Decision.** These are different operations, so neither meaning changes. The 2-argument search is now **`s/index-of text pattern`**:
- same results and same -1;
- a non-string raises SIN! `'type` with the id `'s/index-of` (the operation's full name, the `s` vocabulary convention);
- `s/find` remains the shared vocabulary's.

`find.glon`'s definitions are now members of the reopened `s` vocabulary. `s/find-glon`, `s/find-masm`, `s/raw-find` and the helpers keep their names, and `index-of: :find-masm` selects the implementation. Its callers were updated, along with the masm READMEs.

GLON-VOCAB-1 refuses definitions of `s/` members from outside the vocabulary, so:
- `masm2/trace.glon` defines `trace/` functions and **assigns** the `s/` members to them;
- `masm3/slice2*.glon` were wrapped the same way.

**Verification [M]:**
- `make s-find-check` (new) runs `cases.py`: 221 differential cases on `s/index-of` (Glon reference, MASM, C) plus input immutability and SIN! `'type`, all **identical to C2's own run**.
- Both searches load in one session in either order, and each gives its own results (`4 -1` and `4 3 4`).
- Also identical to C2: `masm3/slice2_cases.py`, `masm3` and `masm2` `stream_compare.py`, and `masm-vocabulary/stream_integration.py`.

Historical design documents (`docs/design/GLON-MASM-VOCABULARY-ACCELERATION*.md` and others) keep their recorded `s/find`. `MEMORY.md` and `GLON-ARCHITECTURE-MEMORY.md` note the rename.

## 3. C2 modules as vocabulary members

- **`modules/strings.glon`** (what `glon-desktop` serves the browser as `/strings.glon`) was a byte-for-byte copy of the pre-VOCAB-1 `demo/shop/strings.glon`. It is now identical to the current shared file: the same words, as members. [S]
- **`modules/file-replace.glon`:** `s/replace-first` is a member of the reopened `s` vocabulary. Its body is unchanged, and `replace/` is untouched.

**Verification [M]:**
- `make modules-check` (new):
  - the copy is identical, and works alone (`4 1 #[SIN! type s/length 5]`);
  - in a bare session, these stacks load without a refusal: file, file + MASM find/u32, fetch + recipe-0004 (64K), HTTP + web + JSON, the G3A register and the G3 server (`server` profile);
  - `s/replace-first` returns `"the rebol is wide" "x web" "none here"`, the same as on C2.
- **`io-file-inplace-test`: 36 passed, 0 failed.** That was 5/31 at V1, and 36/0 on C2.

## 4. Shared libraries, collisions and lexical binding

**Already integrated.** The remaining View string, numeric and collection libraries were already integrated in V1 and are byte-identical to glon-gui-f2 `17b819db` [S]:
- `strings`, `strings-ext`, `strings-text`;
- `case`, `block`, `list`, `numbers`, `binding`.

Every other View-side `.glon` file outside `view/` is a GUI experiment or a `sc1` diagnostic. It belongs to V3, or nowhere.

**`make library-audit` (new)** runs two static analyses over 10 sessions: the shared libraries; JSON; file + replace; file + MASM; fetch; HTTPS G3A, G3 and examples; Kaka; Kaka + shared libraries.
- **`lexaudit.py`** approximates the GLON-VOCAB-1 rules and reports every function set-word whose binding changes under them:
  - **A:** writes an earlier source's function or native;
  - **B:** writes a later source's global;
  - **C:** creates a global at run time that another function reads (nested-closure scopes respected);
  - **U:** an earlier global of unknown kind.

  It was checked on a synthetic A/B/C case. **Result: no divergence in any session** [M], covering all C2 modules, the HTTPS example and test applications, and Kaka.
- **`defs.py`** reports names defined by more than one source. **Result: only deliberate overrides**, listed per session:
  - `masm2/u32.glon`'s `io/u32` and `file-stream-masm.glon`'s 8 `stream/` words (the experiment replaces the module's words);
  - the G3A server setting the documented `json/max-items`;
  - `kaka-lib.glon`'s own `block-set!` when it is loaded with `block.glon`.

**Tests run:**
- `make lib1-test` (new; glon-gui-f2 `view/tests/lib1_tests.c` ported to the root, runtime only): **47/47** list and text checks in 64K, 1M and 4M.
- `vocab1`: 23 checks pass; the 3 known reports are unchanged.

## 5. Literal-block safety

**The hazard** (GLON-LANG-1 §2.4; V1 §5.2): the collector treats loader (literal) blocks as leaves. No runtime native mutates a block; the stores come from raw `masm` words, chiefly `block-set!` (`demo/shop/block.glon`, View's `core_words.glon`, and 11 private copies).

### 5.1 Audit of the callers [M]

**Method.** Every `block-set!` definition outside `tests/` and `experiments/` (12) was instrumented in a scratch export to classify each store. The instrumented `./s1` and the Kaka harnesses were then run.

| Caller | Stores | Target | Value |
|---|---:|---|---|
| traffic | 466,641 | literal | immediate |
| newell | 259,150 | literal | immediate |
| ovm | 180,851 | literal | immediate |
| nasch | 168,896 | literal | immediate |
| merchant-flow | 291 | literal | immediate |
| tuple-space | 120 | literal | immediate |
| Kaka (`kaka-lib`) | 116,624 | **managed** | any |
| **any program** | **0** | **literal** | **managed** |

**Static checks [S]:**
- linda's `arena` and `waiters` are `new-block`, so managed.
- The HTTPS G3 register is `reduce`d, and so are the G3A slot groups.
- `list.glon` writes only into blocks it `reduce`d itself: the list header, its nodes, and `loop/collect`'s result. A list is recognised by identity with a managed `list/magic` block.

**Audit-build artefacts, not V2 results.** The instrumented `./s1` and `kaka-fighter-test` fail: the per-store prints and the extra code push the fighter past its 25,536-cell loader, which it already uses 25,286 cells of on C2. These are diagnostics of the audit build only.

### 5.2 Rules compared

| Rule | Breaks | Prevents the hazard |
|---|---|---|
| Refuse **every** store into a literal block | 6 C2 demos (traffic, newell, nasch, ovm, merchant-flow, tuple-space) | yes |
| Refuse only a **managed value** into a non-managed block | **none measured** | yes: every hazardous store is refused |

**Chosen: the second rule.** A managed value may be stored only into a managed block.

### 5.3 Implementation (`demo/shop/block.glon`; no collector or memory-map change)

- **`block-set! b i v`** stores unless `v` is a managed reference and `b` is not a managed block. Otherwise it raises SIN! `'escape 'block-set! v` and stores nothing.
  - **Managed reference:** tag ≥ 6, with its payload in `[GC_HEAP_BASE 32768, REG_HP)`. Natives, raw code and bound references never address that range.
  - **Managed block:** the same range test on `b`'s payload.
  - It answers `none`. The old `masm` word answered no value, and using that as a value already fail-stopped, so no working program depends on it [M].
  - `v` is passed on as `:v`, so a closure is stored, not called. That bug was caught by the test matrix during development.
- **`managed-ref?`** is a 42-cell `masm` word. It computes `(addr × (addr < REG_HP)) ≥ 32768`, reading the address once with no scratch cell. A first version cost 165 cells.
- **`block-store-ok?`** is ordinary Glon, so it uses no code space.
- **`block-set-raw!`** is the old unchecked store.
- **`list.glon` uses `block-set-raw!`.** Every block it writes is one it made. The check would never refuse there, and it would cost about 25% of a list workload: 1,201 list rounds take 38.5 s raw against 48.5 s checked (`-O0`) [M].
- **Raw `masm` stores stay unchecked by definition.** `tests/core-v1/literal_block_gc.glon` and vocab1's `known` report still reproduce the defect through their own `masm` word, and `literal-block-gc-check` says so. The 11 private copies are unchanged: by the audit, they store immediates or write managed targets only.

### 5.4 Verification [M]

`make literal-store-check` (new), in 64K, 1M and 4M:
- **The store matrix:**
  - Targets: managed and literal.
  - Values: managed string, integer, word, literal block, closure.
  - Results: refused stores raise the SIN! and leave the literal unchanged. A stored closure is still callable (`42`). The managed store survives three collections.
- **The GLON-LANG-1 hazard program with the shared word:**
  - The store is refused: `#[SIN! escape block-set! [ "glon" 1 2 3 ]]`.
  - The literal's item reads `0` after the collections.

## 6. C2 modules and Kaka under lexical-local binding [M]

| Module | Runtime test | Static audit (`lexaudit`) | Bare-session load |
|---|---|---|---|
| JSON | `json-test` PASS | no divergence | ok |
| file, stream, atomic, replace | `io-file-test` 39/0, stream 0 failures, **in-place 36/0** | none | ok |
| fetch, recipe-0004 | — | none | ok |
| HTTP, web; G3/G3A apps; HTTPS test apps | **not runnable here**: CivetWeb needs the OpenSSL headers | none (all 9 HTTPS sessions) | ok (`server` profile) |
| Kaka (7 harnesses) | `kaka-test` PASS | none | — |

**`persist.glon` (G3A)** needs the file host natives. It fails to load in a bare runner on C2 and on V2 alike, so it was covered by the static audit only.

## 7. Regression suite (V2 HEAD) [M]

All steps ran sequentially with exit statuses recorded (`diagnostics/core-v2/v2-suite`).

**Every step exits 0 except `build-deps-check`:**
- `check-frozen-s1`, `arch-check`, `profile-check`, `profile-reject-check`, `profile-common-test`;
- `global-ctx-hash-test`, `hash-capacity-check`, `gc-worklist-overflow`, `primer --check`;
- `json-test`, `m1-large-test`, `site-capacity-test`;
- `literal-block-gc-check`, `literal-store-check`, `intent7-set-check`, `vocab1`;
- `s-find-check`, `modules-check`, `library-audit`, `lib1-test`, `mem-diff-test`;
- `glon-live-native`, `patrol-test`, `kaka-test` (7);
- `io-file-test`, `io-file-stream-test`, `io-file-inplace-test`;
- `desktop-test`, `machine-integrity-test`, `iterators-test`, `host-test`.

**Re-runs.** `block.glon` reached its final form during the run. The steps that load it (`literal-store-check`, `vocab1`, `s-find-check`, `modules-check`, `library-audit`, `lib1-test`) were re-run afterwards, and all pass. `./s1` and the C2 suites don't load it [S].

**`./s1`: exit 0, 1,025 `ok:` lines, 0 FAIL.** That is identical to V1, apart from dump addresses and interleaved output.

**Environmental failures** (identical on unchanged C2):
- `build-deps-check` 27/30: the 3 CivetWeb cases need `openssl/bn.h`;
- `kernel-test` and `kernelspec-test`: Python `jupyter_client` is absent.

**Not run:**
- HTTPS acceptance (no OpenSSL headers);
- WASM (no emcc, clang or node);
- Windows (no MinGW);
- any View suites (V3).

**No unexplained C2 regression remains.**

## 8. Measurements against V1 [M]

The emitted runtime is unchanged at 12,890 cells, as are the function-site capacities (640 / 4,096 / 8,192 at 64K / 1M / 4M).

**Shared library set**, loaded in a bare session (prelude, binding, strings, strings-ext, case, block, list, strings-text, numbers). The deltas are the same in 64K, 1M and 4M; the absolute values are for 64K:

| | V1 | V2 | Δ |
|---|---:|---:|---:|
| globals | 139 | 142 | +3 (`block-set-raw!`, `managed-ref?`, `block-store-ok?`) |
| sites | 113 | 115 | +2 |
| code cells free | 1,308 | 1,266 | −42 (`managed-ref?`) |
| loader cells (64K) | 12,948 | 13,124 | +176 |
| load time (bare runner, 64K) | 52 ms | 53 ms | — |

**`block.glon` alone:** +3 globals, +2 sites, +42 code cells and +186 loader cells.

**`lib1` checks, live heap afterwards:**

| | V1 | V2 |
|---|---:|---:|
| 64K | 4,240 / 7,232 | 4,304 / 7,232 |
| 1M | 80,976 | 81,008 |

**C2 module stacks:**
- The file stack (64K) didn't load at V1 (`file-replace` refused). At V2 it loads: 93 globals, 107 sites, 8,184 loader cells.
- The HTTP stack plus the G3A register (`server`) is identical in V1 and V2: 177 globals, 129 sites, 517 code cells free, 31,341 loader cells.

## 9. Remaining limitations and V3 dependencies

1. **View's `core_words.glon` must change in V3.**
   - `list.glon` now calls `block-set-raw!`. Loading glon-gui-f2's unchanged View libraries over V2 fails in `intent.glon` (it runs list code at load) [M]. V1 loads the same stack.
   - V3 must define `block-set-raw!` in `core_words.glon` (renaming its `masm` store costs 0 cells).
   - It should adopt the checked `block-set!`, which needs `managed-ref?`: 42 code cells against the 52 free in Intent sessions, leaving about 10 [A]. If that is too tight, V3 must choose between that cost and leaving View's `block-set!` unchecked (View never stores managed values into literals, by convention). **This is the main V3 decision V2 surfaces.**
2. **Raw `masm` stores remain unchecked.** The 11 private `block-set!` copies and any application `masm` word can still store a managed value into a literal block. Only a collector change (tracing loader blocks, or a remembered set) closes that, and it needs separate justification and the C2 owner (V1 §5.2 options A and D).
3. **WASM targets embed the loader** and have not been rebuilt with VOCAB-1. Two browser paths now serve vocabulary syntax: `modules/strings.glon` (via `glon-desktop`) and the primer's libraries. They need a WASM build from current sources.
4. **HTTPS runtime behaviour** under the lexical rules is checked statically and by load only.
5. **Known vocabulary defects V1–V4** and the `get` / `intent/get` collision are unchanged.

## 10. Reproduce

```sh
make test literal-store-check literal-block-gc-check lib1-test s-find-check \
     modules-check library-audit intent7-set-check site-capacity-test mem-diff-test
make io-file-test io-file-stream-test io-file-inplace-test json-test kaka-test
```
