# GLON-VIEW-CORE-V4 — Unified core acceptance

Date: 2026-11-28.

| | |
|---|---|
| **Branch** | `integration/c2-view` (`/mnt/d/repos/glon-core-view-integration`) |
| **Status** | Committed locally only. Nothing pushed, merged, rebased or promoted. **Stopped for review.** |

**Evidence tags:**

| Tag | Meaning |
|---|---|
| **[M]** | Measured in V4 |
| **[S]** | A source fact |
| **[H]** | A historical report (an earlier milestone or the user) |
| **[A]** | An assumption |

Logs are in `/mnt/d/repos/diagnostics/core-v4/` and `/mnt/d/repos/diagnostics/core-v4-export.*`.

**Success criterion:** one C2-derived core runs the server-side and desktop language stacks. **Met for every target this environment can build and test.** The HTTPS server, Windows and Android are blocked here by missing dependencies, and are listed as such (§4).

## 1. Git provenance [M]

| | |
|---|---|
| Integration HEAD | **`97b0fcd09deda6550a775ec2e38fd41ccef66214`**; working tree clean |
| Base | C2 `4dcae2aa10e275f23e7b2c1904fa45f8b888e243` (`origin/backup/glon-core-c2`, unchanged) |
| View source | glon-gui-f2 `17b819dbadba827fb9fb1057ba757ded6f604ebd` (unchanged) |
| Glon Worlds | `/mnt/d/repos/glon-worlds`, branch `main`, `863fbf7` (W1E; no remote configured). Its pinned core is `3bccc6e9`. |
| Commits on C2 | 23: V1 (7), V2 (5), V3 (6: the gate stop, 4 stages, the report), the capacity change (1), the `r0_s1.h` M fix (1), **V4 (3)** |

**V4 commits:**

| Commit | Change |
|---|---|
| `50acdb4b` | `standalone/glon.c`: `memchr`, `strchr` and `snprintf` for the no-libc WASM builds (§4) |
| `8f19d742` | `tests/vocab1`: a known-defect report for one-level adoption (V3) |
| `97b0fcd0` | `view`: `test-gui-f1b` builds `widget_gallery_shot`; glon-gui-f2's executable modes restored (§3) |

**Baseline confirmed from committed source** (a `git archive` export, not the working tree) [M]:
- **Frozen S1:** `check-frozen-s1.sh` reports `f90496c26dc45c7a387d8fc8639cd2781507d3d2`, all frozen files unchanged; `git diff 4dcae2aa HEAD` of the frozen files is empty.
- **C2 is authoritative for the memory map and profiles:** the generator, the layout checks and the fixed-radix hash (`R0S1_HASH_RADIX`) are as C2 has them. The only profile change is the approved desktop `CODE_CAP`.
- **SITE-1:** the `SITES` / `SITE_CAP` profile keys; desktop 2,048 / 4,096 / 8,192.
- **MEM-2/3/5 and INTENT-7** are present (`GC_FREE_HINT`, `RV_FINDMODE`), with their differential switches.
- **The parser fix** (`PB_ROOM`) and **the VOCAB-1 loader** are present.
- **View uses the checked `block-set!`** (`view/glon/core_words.glon`; also `demo/shop/block.glon`). `block-set-raw!` is internal.
- **The window library is optional:** `gv_glon_host` loads `intent_windows.glon` only when `windows_lib` is set.

## 2. Integrated architecture [S]

```
frozen S1 (s1.c, f90496c) ── C2 memory map + profile generator (profiles/*.profile, r0_s1_layout.h)
  └─ r0_s1 runtime: C2 + SITE-1 region + MEM-2/3/5 + INTENT-7 (C2 hash) + parser bound
       + GLON-VOCAB-1 loader (lexical locals, loader vocabularies)
  ├─ shared libraries: prelude, binding, strings (s/), strings-ext, strings-text, case,
  │    block (checked block-set!), list, numbers (n/)
  ├─ server stack: modules/ json, file, file-stream, file-atomic, file-replace, http, web,
  │    fetch (s/ members legitimate; s/index-of)
  ├─ Kaka, patrol, desktop host, Jupyter host (native)
  ├─ WASM: standalone/glon.c no-libc shim (+memchr/strchr/snprintf) → glon*.wasm, GlonBot
  └─ view/: View engine, VID, Intent, components, layouts, native windows (optional)
       on desktop-256k/1m/4m (CODE_CAP 17,408)
```

Glon Worlds consumes the core only through **GlonBot**: its `tools/build-wasm.sh` compiles a core repository's GLON_LIVE sources (`standalone/glon.c`, `r0_s1_g1a*`, `s1.c`, `r0_s1_runtime.c`, `r0_s1_show.c`) into `glonbot.wasm`, and its harness loads the prelude, `common.glon`, `strings.glon` and `strings-ext.glon` from that repository. The Worlds server (W1A–W1E) is Python and runs no Glon. No Worlds code was copied or merged.

## 3. Regression results (exit statuses, bounded) [M]

### 3.1 Root (export of `46460e55`)

All exit 0:
- `arch-check`, `profile-check`, `profile-reject-check`, `profile-common-test`;
- `global-ctx-hash-test`, `hash-capacity-check`, `gc-worklist-overflow`, `primer --check`;
- `json-test`, `m1-large-test`;
- `site-capacity-test`, `code-capacity-check`;
- `literal-block-gc-check`, `literal-store-check`, `intent7-set-check`;
- `vocab1`, `lib1-test`, `s-find-check`, `modules-check`, `library-audit`;
- `mem-diff-test` (538 s);
- `glon-live-native`, `patrol-test`, `kaka-test` (7);
- `io-file-test`, `io-file-stream-test`, `io-file-inplace-test` (36/0);
- `desktop-test`, `machine-integrity-test`, `iterators-test`, `host-test`.

**`build-deps-check`: exit 2, environmental.** It fails 27/30, exactly as on C2: the 3 CivetWeb checks need `openssl/bn.h`.

### 3.2 `make -C view test-c1-accept`

**At HEAD `97b0fcd0` in the clean worktree** (the frozen-S1 step needs the git tag): **C1 ACCEPTANCE: PASS, 25/25 steps exit 0.**

| Step | Seconds |
|---|---:|
| build | 45 |
| frozen-s1 | 0 |
| root-test (`make -C .. test`: s1, GC worklist, hash, primer, test-vocab1) | 377 |
| build-deps | 21 |
| lib1 | 29 |
| view-test | 18 |
| gui-p2 | 78 |
| gui-f1b | 13 |
| intent | 34 |
| inst1 | 5 |
| inst2 | 22 |
| inst3 | 13 |
| win3 | 38 |
| c1-accept-4m | 4 |
| c1-accept-1m | 4 |
| members-4m | 53 |
| members-1m | 96 |
| intent7-4m | 17 |
| members-winlib-4m | 35 |
| site-capacity | 2 |
| global-capacity | 8 |
| vid-load-64k | 6 |
| mem2-alloc-diff (variant) | 162 |
| mem5-closure-diff (variant) | 121 |
| intent7-set-diff (variant) | 134 |

**From the clean export of `46460e55` the same suite failed three steps.** All three were explained and fixed or attributed:
- `frozen-s1` and `root-test`: an export has no `.git`, so `check-frozen-s1.sh` cannot resolve its tag (a property of the export method);
- `gui-f1b`: a missing make prerequisite, fixed in `97b0fcd0` and verified from a fresh export.

The other 22 steps passed from the export.

### 3.3 Other suites

- **View suites outside the acceptance set** (export of `46460e55`; the later commits do not affect them), all exit 0:
  - `test-wg-runner`, `test-vg-runner`, `test-widget-gallery`;
  - `test-gui-core`, `test-gui3`, `test-gui4`, `test-gui4-scroll`, `test-gui4b2c`, `test-gui-f2`;
  - `test-global-capacity`, `test-vocab`, `test-table-host`;
  - **`test-view-asan`, `test-table-asan`** (AddressSanitizer and UBSan).
- **Membership with the window library at 1M** (10 cases, `GV_TEST_WINDOWS_LIB=1`), not in the acceptance suite: all exit 0.

**Glon Worlds** (scratch copy of `main 863fbf7`; nothing written to the Worlds checkout):

| Test | Result |
|---|---|
| GlonBot `node tests/test.js`, with `glonbot.wasm` rebuilt from the Worlds pin `3bccc6e9` | **51 passed, 0 failed** |
| GlonBot, rebuilt from C2 `4dcae2aa` | **51 / 0** |
| GlonBot, rebuilt from **integration HEAD `97b0fcd0`** (clean export) | **51 / 0** (before `50acdb4b` the WASM did not link: §4) |
| Worlds server W1A / W1B / W1C / W1D / W1D.1 / W1E (Python; core-independent) | 33 / 58 / 34 / 43 / 47 / 46 passed, 0 failed |

**Not run here:** HTTPS G1–G3A acceptance, Windows, Android, macOS, the X11/WebKitGTK section, and real-browser WASM pages. See §4.

## 4. Deployment targets [M]

| Target | Builds | Runs | Tests pass | Notes |
|---|---|---|---|---|
| Linux CLI (`glon`, constrained / large / server / desktop profiles) | yes | yes | yes | §3.1 |
| Linux desktop GUI (View, SDL2 + Cairo + Pango) | yes | yes (SDL dummy driver) | yes | §3.2. **Manual WSLg:** the user reports the three-window demo verified on WSLg [H]; this V4 run opened no window. |
| HTTPS server (`MODULES=http`, CivetWeb) | **no** | — | not tested | **Blocked:** no OpenSSL development headers (`openssl/bn.h`). The HTTP / web / JSON / G3 / G3A sources load in a bare `server`-profile session, and the static audit finds no lexical-binding divergence (V2). That is **not** HTTPS compatibility. |
| WASM (emcc 6.0.9 and node 24.19.0, from `~/emsdk`) | yes | yes (node) | 10 of 11 root WASM tests, **the same as C2**; GlonBot 51/51 | `wasm-standalone-test` fails on C2 too: its rule omits `r0_s1_g1a.c`, so a fresh build does not link and an unbuilt tree tests the stale committed module (a pre-existing C2 defect, not changed). **Correction:** V0–V3 said WASM was unavailable; emsdk was installed but not on `PATH`. |
| Windows | no | — | not tested | **Blocked:** no MinGW-w64 here. C2's CI matrix (GitHub Actions) covers it, but needs a pushed branch, which is not authorised. |
| Android | no | — | not tested | **Blocked:** no NDK or SDK (`scripts/dist/build_android_d1.sh`). |
| macOS | no | — | not tested | Not available here. |

## 5. Memory profiles [M]

| Profile | Status | Cells | Code (limit) | Main RS | Managed heap | Worklist | Site table | Loader | Globals / symbols |
|---|---|---:|---:|---:|---:|---:|---|---:|---|
| constrained | reference | 65,536 | 15,872 (16,128) | 8,192 | 7,232 | 256 (fixed) | 640 (fixed) | 14,800 | 256 / 512 |
| constrained-notask | verified | 65,536 | 15,872 | 8,192 | 7,232 | 256 | 640 | 25,536 | 256 / 512 |
| desktop-256k | provisional | 262,144 | **16,896 (17,152)** | **7,168** | 65,536 | 2,048 | **2,048** | 149,008 | 1,024 / 2,048 |
| desktop-1m | provisional | 1,048,576 | **16,896** | **7,168** | 262,144 | 8,192 | **4,096** | 730,640 | 4,096 / 8,192 |
| desktop-4m | provisional | 4,194,304 | **16,896** | **7,168** | 1,048,576 | 32,768 | **8,192** | 3,061,264 | 16,384 / 32,768 |
| large | verified | 1,333,104 | 15,872 | 8,192 | 1,048,576 | 40,000 | 640 | 200,000 | 256 / 512 |
| server | verified | 1,633,104 | 15,872 | 8,192 | 1,048,576 | 40,000 | 640 | 500,000 | 256 / 512 |
| server-https | provisional | 1,633,104 | 19,968 (20,224) | 4,096 | 1,048,576 | 40,000 | 640 | 500,000 | 512 / 1,024 |

**Invariants [M]:**
- Every generated `layout.txt` was parsed: all regions are ordered, non-overlapping and end within `MEM_CELLS`.
- `r0_s1_layout.h`'s compile-time checks hold in every build.
- `code-capacity-check`:
  - recursion limit 84 at 8,192 RS cells;
  - 73 at 7,168 (the desktop profiles);
  - 41 at 4,096 (server-https);
  - every profile fails cleanly one level deeper.

**Full Intent sessions (desktop):**

| Session | Code cells free | Globals | Sites |
|---|---:|---:|---:|
| membership | **1,034** | 1,067 | 889 |
| c1 with window library | — | 1,011 (1,015 after its window cycles) | 849 |

The run-time return-stack peak is **4,124 / 7,168** cells (V3).

**Sensitivity to allocation order and library loading, against glon-gui-f2's PREP-1 logs [M][H]:**

| Measure | glon-gui-f2 | HEAD |
|---|---|---|
| layout-instance ceilings, 4M / 1M | 100 / 49 | 100 / 49 |
| free cells after a refused window, 4M | 971,888 | 971,824 (−64) |
| free cells after a refused window, 1M | 185,456 | 185,392 (−64) |
| 1M membership live set | 109,472 | 109,600 (+128) |

The small differences match the three extra checked-store words. The 1M membership reload, which INTENT-7 identified as sensitive to fragmentation, passes, and so does the 1M session with the window library added. The allocator and collector were not changed in V4.

## 6. Language and memory safety [M]

| Property | Regression coverage (all pass) |
|---|---|
| Lexical-local isolation | vocab1 `locals`, `library-locals`, `nested-shadow`, `outer-levels`; `library-audit` (no divergence in the C2 modules, Kaka or HTTPS apps) |
| Nested vocabulary membership | vocab1 `nested`, `nested-shadow`; `c1-accept` (component definitions in a nested vocabulary) |
| Duplicate / unknown-member rejection | vocab1 `error-unknown`, `error-define`, `error-redefine`, `error-dup`, `error-global`, `error-place`, `error-setparam` |
| Explicit global mutation | vocab1 `outer-mutation`, `computed-set`, `outer-levels`; `c1-accept` (`set 'c1-last`) |
| Literal-block managed-value store rejection | `literal-store-check` (64K / 1M / 4M): refused with SIN! `'escape 'block-set!`, the literal intact after collections |
| Stale handles, window-instance isolation | `c1-accept` (a closed window's handles stale; keys by window; per-instance state); `win3-lifecycle`, `win3-failure` |
| Safe node-ID reuse | `inst3-lifecycle`; `c1-accept` (20 open/close cycles keep 128 free ids, `view/id` 183, sites and globals); `win3-scale` |
| Memory errors in View's C core | `test-view-asan`, `test-table-asan` |

**Outside the checked guarantee [S]:**
- **Raw `masm` stores remain outside the checked block-store guarantee.** Any `masm` word, and the 11 private `block-set!` copies in demos and HTTPS examples, writes raw memory. `literal-block-gc-check` and vocab1's `known-literal-block-gc` still reproduce the GC defect through such a word.
- **VID's `vid/set-block!`** and **Intent's two id-table stores** use the internal raw store under documented preconditions: their targets are library-allocated managed blocks.

**Known vocabulary defects, current status (unchanged; no new semantics):**
- **V1:** relative forward reference to a nested vocabulary; fail-stops at run time.
- **V2:** the same reference fully qualified; refused at load.
- **V3:** one-level adoption; a later `a/b/c` is refused ("no such vocabulary"). Now a committed `known` report.
- **V4:** prefixed definitions before the declaration in one source are refused ("no member 'x' in vocabulary p").
- **`get` / `intent/get` collision:** unchanged (`intent.glon` is not wrapped in a vocabulary).

## 7. Performance [M]

**Setup:**
- Workloads are 5 runs each, median ms; every run's exit status is checked.
- C2 is the `4dcae2aa` export; V4 is the HEAD `97b0fcd0` export.
- glon CLI.
- **Normal:** `-O2` (the configuration of the View and Android builds; C2's own default is `-O0 -g`).
- **Fixed alignment:** `-O2 -falign-functions=64 -falign-jumps=64 -falign-loops=64 -falign-labels=64`.

| Workload | Profile | Normal: C2 → V4 (ms) | Fixed alignment: C2 → V4 (ms) |
|---|---|---|---|
| interpreter: `fib 23` | 64K | 1,300 → 1,368 | 1,359 → 1,389 |
| interpreter: `fib 23` | 1M | 1,271 → **1,732** | 1,362 → **1,370** |
| allocation + GC: 24,000 string + block allocations | 64K | 2,497 → 1,657 | 2,532 → 1,690 |
| allocation + GC | 1M | **28,800 → 1,518** | **28,406 → 1,278** |
| vocabulary lookup / set-words: 200 globals | 64K | 1,073 → 755 | 1,175 → 775 |
| vocabulary lookup / set-words: 2,000 globals | 1M | 4,993 → 781 | 5,555 → 660 |
| JSON module test suite | large | 437 → 214 | — |

**Reading the table:**
- **The `fib` 1M normal-build gap** (+36%) disappears with fixed alignment (+0.6%). It is a code-placement effect (INST-3A), not an interpreter change: the emitted S1 runtime differs by only 20 cells.
- **Allocation and GC are up to 22× faster at 1M** (MEM-2/3 free-block hint; MEM-5).
- **Vocabulary lookup and set-words are up to 8× faster** (INTENT-7).
- The first lookup workload exceeded the parser's 511-cell block bound and failed on **both** runtimes (`FAIL rc=1`, recorded as such). It was split and re-run (`perf-lookup.sh`).

**View** (normal `-O2`; median of 3 runs; glon-gui-f2 `17b819db` on its MS-1 runtime → HEAD):

| Case | 1M (ms) | 4M (ms) |
|---|---|---|
| intent6-load | 1,708 → 1,698 | 1,303 → 1,313 |
| intent7-ops | 6,707 → 6,831 | 4,987 → 4,946 |
| intent7-search | 14,959 → 16,094 (+7.6%) | 9,559 → 9,612 |
| intent6-reload | 6,134 → 6,241 | 4,438 → 4,419 |
| c1-accept | 3,668 → 3,721 | 3,021 → 3,057 |
| win3-windows | 882 → 880 | 875 → 886 |
| win3-scale | 22,762 → 23,372 | 18,508 → 19,043 |
| inst2-instances | 883 → 899 | 879 → 882 |

The per-operation membership timings (select, search, save, new, delete) and the window open/close costs (2.3–2.5 ms per window) agree within a few ms.

**Not compared:** View with fixed alignment, and HTTPS server workloads (blocked, §4). The JSON row stands in for the server-side language work.

## 8. Unresolved defects and environmental blockers

**Environmental (not code defects):**
- OpenSSL headers: HTTPS server builds, `build-deps-check` cases 5, the HTTPS acceptance suites.
- MinGW (Windows), the Android NDK, macOS.
- `jupyter_client` (Jupyter `kernel-test` and `kernelspec-test`).

**Pre-existing C2 defects (unchanged):**
- `wasm-standalone-test`: the `standalone/glon.wasm` rule omits `r0_s1_g1a.c`.
- C2's root `.gitignore` pattern `glon` hides any `glon/` directory (fixed for `view/glon` in V3).

**Open language and safety items:**
- vocabulary defects V1–V4;
- the `get` / `intent/get` collision;
- raw-`masm` literal stores (the collector still treats literal blocks as leaves).

**Status labels:** the desktop profiles are `provisional` and server-https is `provisional`. Promotion is the C2 owner's decision.

**Environment note:** git's `core.fileMode=false` on this NTFS mount loses executable bits on import (corrected for the 13 View files in V4).

## 9. Recommended next milestone

**GLON-VIEW-CORE-V5 — server-side acceptance and promotion readiness:**
1. **Run the HTTPS G1–G3A acceptance suites on the integrated core** where OpenSSL development headers exist: a machine with them, or approval to install `libssl-dev` here. Do the same for C2's CI matrix (Windows, macOS, Linux ARM64) on a pushed review branch, if authorised. This is the remaining unverified server stack.
2. **Fix C2's `standalone/glon.wasm` rule** (add `r0_s1_g1a.c`), so all 11 WASM tests pass, and **run the browser WASM pages** (primer, live, Kaka, GlonBot) in a real browser.
3. **Decide the profile status:** promote desktop-256k / 1m / 4m from `provisional` to `verified` on this evidence; keep or re-measure server-https.
4. **Make separate language decisions:** the vocabulary defects V1–V4 and the `get` / `intent/get` collision. Fix them, or document them as rules.

Then publish or promote the integrated core, after review.
