# GLON-VIEW-CORE-V5 — Cross-platform verification and integration hardening

Date: 2026-11-28.

| | |
|---|---|
| **Branch** | `integration/c2-view` (`/mnt/d/repos/glon-core-view-integration`) |
| **Status** | Committed locally only. Nothing pushed, merged, rebased or promoted. **Stopped for review.** |

**Evidence tags:**

| Tag | Meaning |
|---|---|
| **[M]** | Measured in V5 |
| **[S]** | A source fact |
| **[H]** | A historical report (an earlier milestone or the user) |

Logs are in `/mnt/d/repos/diagnostics/core-v5/`. The main ones:
- `regression/` — the full regression;
- `https/` — HTTPS G1–G3A, integrated core and C2;
- `wasm-results/` — the WASM export run;
- `symcount/` — the symbol-table measurements.

**Result:**
- **Linux x86-64:** all of the following pass on the integrated core, from a clean detached worktree of the final source: the core, vocabulary, View, WASM (11/11), GlonBot, the HTTPS G1–G3A suites and the CI native suite.
- **Changes needed on the way:**
  - two defects inherited from C2 were fixed (the WASM build rule and a CI false pass);
  - three vocabulary defects were fixed;
  - one regression relative to C2 was found and fixed: the server profile's symbol table was too small for G3A (§3).
- **Not executed here:** Windows, macOS and Linux ARM64. The existing CI matrix covers them, but it has not run on this branch (§4).

## 1. Git provenance [M]

| | |
|---|---|
| Integration HEAD (source) | **`ff39c17b`**; this report is committed on top of it |
| Base | C2 `4dcae2aa10e275f23e7b2c1904fa45f8b888e243` (`backup/glon-core-c2`, unchanged) |
| View source | glon-gui-f2 `17b819dbadba827fb9fb1057ba757ded6f604ebd` (unchanged) |
| Frozen S1 | `f90496c26dc45c7a387d8fc8639cd2781507d3d2`. `check-frozen-s1.sh` passes, and `git diff f90496c -- s1.c s1.h tests.c adversarial.c claims.c` is empty. |
| Glon Worlds | `/mnt/d/repos/glon-worlds` `main` `863fbf7`, not modified. GlonBot was built from the integrated core in the V4 scratch copy. |
| Commits on C2 | 30: 24 up to and including the V4 report (`7c790891`), plus 5 V5 source commits (below) and this report |

**V5 commits:**

| Commit | Change |
|---|---|
| `1f630eb9` | WASM: the `standalone/glon.wasm` rule links `r0_s1_g1a.c`; `standalone/node_test.js` supplies the two G1A host imports as no-ops (§2) |
| `df545659` | Vocabulary: nested forward references and deep flat-global adoption, with tests (§5) |
| `42fa3216` | CI: integration steps in `scripts/ci/native-suite.sh`; `sizes` fails on a failed build; CR-tolerant output checks (§4) |
| `7d86a01b` | `profiles/server.profile`: `SYM_CAP` 512 → 1,024 (§3, §6) |
| `ff39c17b` | `global_ctx_hash_test.c`: the 512-symbol reference identity is checked in the reference build only (§6) |

**Baseline preserved [M][S]:**
- C2's memory map and profile generator;
- SITE-1, MEM-2/3/5, INTENT-7 and the parser fix;
- GLON-VOCAB-1 lexical binding;
- the checked `block-set!`;
- View / VID / Intent and the optional native windows;
- C2's server stack and Glon Worlds compatibility.

The only V5 profile change is the server `SYM_CAP` (§6). `M[]` is unchanged in every profile.

**`CODE_CAP` 17,408 and 16,896 code cells:**
- **17,408** (`CODE_CAP` = `R0S1_DS_INIT`) is the address where the data stack starts. It is the value in the desktop profiles.
- Code is limited to `DS_INIT − R0S1_DS_RESERVE (256)` = **17,152**.
- Usable code therefore occupies `[256, 17152)` = **16,896** cells; the first 256 cells are reserved below it.
- Reports that quote 16,896 are describing usable code cells; reports that quote 17,408 are describing the profile key. Both describe the same configuration.

## 2. WASM [M]

**Toolchain:** the installed Emscripten (`~/emsdk`, emcc 6.0.9, node 24.19.0) on `PATH`. No libc was added: the no-libc build keeps `standalone/glon.c`'s own `memchr`, `strchr` and `snprintf` (V4).

**C2's `wasm-standalone-test` defect:** `standalone/glon.c` calls `r0_s1_g1a_event` / `r0_s1_g1a_event_value` outside its `GLON_LIVE` branch.
- **The rule:** it did not link `r0_s1_g1a.c`. It now does, as `demo/shop/glon.wasm` already did.
- **The harness:** the node harness then failed on the module's `host_set_html` / `host_canvas_script` imports. It now supplies them as no-ops, because the counter never renders HTML or canvas.

**Results:** all 11 WASM targets pass, run with `make -B` (forced rebuild) both:
- in a `git archive` export (`wasm-run.sh`);
- in the final clean-worktree regression.

| Target | Result |
|---|---|
| `wasm-test`, `wasm-standalone-test`, `wasm-g1a-test`, `wasm-traffic-test`, `wasm-linda-test`, `wasm-binding-test`, `wasm-abi-test`, `wasm-live-test`, `wasm-primer-test`, `wasm-kaka-test`, `machine-integrity-wasm` | **11/11 exit 0** |

**Artefacts were built from the integrated core, not stale:**
- `-B` rebuilds every module from source.
- The rebuilt `standalone/glon.wasm` (sha256 `fa8c3dec…`) is byte-identical to the rebuilt `demo/shop/glon.wasm`.
- The **committed** `standalone/glon.wasm` (`28d42658…`) is stale. It was left unchanged, because refreshing published artefacts is a publication decision (§10).
- The WASM targets also rewrite `demo/shop/live.html`, `web/demo.js` and `web/demo.wasm`. They did so in the scratch worktree only; nothing was committed.

**GlonBot (Glon Worlds), built with `GLON_REPO` set to the integrated worktree:**
- `node tests/test.js`: **51 passed, 0 failed**.
- `tests/browser-test.js`, the existing headless-Chromium harness (Playwright, `--headless=new`, no desktop window): **9 passed, 0 failed**. It also passes with the Worlds pin `3bccc6e9`.

No new browser framework was introduced.

## 3. HTTPS [M]

**Dependency:** Ubuntu `libssl-dev` 3.0.2-0ubuntu1.30 was installed with the user's approval.
- **Before installing:** the candidate matched the installed `libssl3` and `openssl`, from the standard jammy-updates/security repositories.
- **The transaction:** 1 new package, 0 upgraded, 0 removed.
- **Check:** `pkg-config openssl` reports 3.0.2.

**Method:**
- Each build and each acceptance run is a separate step with its own timeout and exit status, so compilation and run-time results are recorded separately.
- All servers bind `127.0.0.1:8443` with a fresh self-signed certificate.
- The integrated core and C2 (`4dcae2aa` export) ran sequentially.

| Step | Integrated | C2 |
|---|---|---|
| builds: minimal, file, http, file+http, http and file+http `constrained-notask`, file+http `server` | all exit 0 | all exit 0 |
| minimal smoke | 0 | 0 |
| G1 (http; file+http; file+http constrained-notask) | 0 / 0 / 0 | 0 / 0 / 0 |
| G2 (same three builds) | 0 / 0 / 0 | 0 / 0 / 0 |
| G3 (http, constrained-notask) | 0 (21 s) | 0 (34 s) |
| **G3A acceptance (file+http, server)** | **1, then 0** (32 s) | 0 (212 s) |
| **G3A endurance, 1,000 requests** | **1, then 0** (74 s) | 0 (307 s) |
| **G3A scale, 10 / 100 / 1,000 records** | **1, then 0** (35 s) | 0 (254 s) |
| `build-deps-check` (now including the CivetWeb/OpenSSL cases) | 0 | 0 |

**The G3A failure:**
- **Symptom:** the first run on the integrated core failed at start-up: `persist.glon: ** resource error: symbol_table_full`.
- **Cause:** GLON-VOCAB-1 lexical binding interns every distinct function-local name a second time, as its decorated local word (`lx_deco_id`).
- **Measurement** (instrumented scratch builds):

| Load | C2 | V4 | V5 |
|---|---:|---:|---:|
| empty program | 211 | 250 | 250 |
| G3A libraries | 420 | 540 | 540 |
| full G3A application | — | — | **638** |

- **When it appeared:** the increase was already present at V4 (`7c790891`). V4 could not run HTTPS, so it went unnoticed. The V5 vocabulary fix adds no symbols.
- **Fix:** `server.profile` `SYM_CAP` 512 → 1,024 (§6). After it, all three G3A runs pass.

**Coverage verified by these suites** (the acceptance scripts' own assertions, all passed):
- **TLS:** handshake on a self-signed certificate; startup; clean shutdown on SIGTERM (exit 0) and after `--http-max-requests`; port freed.
- **Routing and methods:** routing, inspection, 404 / 405 / 415 / 400; GET; form and JSON POST; JSON PUT edit.
- **JSON:** parsing and encoding, malformed JSON refused.
- **Request limits:** oversized body 413; incomplete and oversized header lines.
- **Restart and persistence:** NDJSON persistence across restart; malformed and truncated logs.

**Repeated requests and memory** (G3A endurance, keep-alive, 1,010 requests):

| | Integrated | C2 |
|---|---|---|
| errors / rejected / write failures | 0 / 0 / 0 | 0 / 0 / 0 |
| live cells at end | 26,672 | 26,672 |
| GCs | 3 | 3 |
| heap high | 1,081,344 | 1,081,344 |
| file descriptors start → end | 5 → 4 | 5 → 4 |
| peak RSS (KiB) | 16,972 | 16,804 |
| median / p95 latency (ms) | **52 / 52** | 188 / 360 |

Memory behaviour is identical to C2's. The integrated core is about 3.6× faster at the median (MEM-2/3 and INTENT-7).

**Scale, startup restore time** for 10 / 100 / 1,000 records:

| | 10 | 100 | 1,000 |
|---|---:|---:|---:|
| Integrated | 0.31 s | 4.2 s | 23.6 s |
| C2 | 0.57 s | 10.8 s | 231.5 s |

The same results were reproduced inside the final regression by `scripts/ci/native-suite.sh`: steps `https-g1g2g3` and `https-g3a` both PASS from the clean worktree.

## 4. Windows [M][S]

**Existing CI.** C2's `.github/workflows/glon-native-matrix.yml` already covers:
- Linux x86-64 and AArch64;
- `windows-2025` (MSYS2 UCRT64: gcc, OpenSSL, Python);
- macOS ARM64 and x86-64.

It runs `scripts/ci/native-suite.sh` on push to `main`, `release/**`, `ci/**`, `distribution/**` and `glon-shop-g1a`, on tags `v*`, on `pull_request` and on `workflow_dispatch`. **No new workflow was written**, since it would duplicate this one.

**V5 changes to the shared script (`42fa3216`):**
- **New integration steps:** six steps run on every runner, Windows included:
  - capacity: `site-capacity-test`, `code-capacity-check`;
  - `literal-store-check` and `literal-block-gc-check`;
  - `intent7-set-check`;
  - `lib1-test` and `modules-check`;
  - `s-find-check`;
  - `library-audit`.
- **The `sizes` false pass (a C2 defect):** when a build failed, the step recorded the previous configuration's binary under the new label and passed. It now records `BUILD-FAILED` and fails.
- **Line endings:** the core-v1/core-v2 check scripts strip CR before comparing output, as the MSYS2 runner needs.

**Executed here:** the whole native suite as a Linux x86-64 runner would run it. **22/22 steps PASS** from the clean worktree, including both HTTPS steps.

**Not executed:**
- Windows, macOS and Linux ARM64: there is no MinGW or macOS toolchain here.
- The workflow has not run on this branch. Doing so means pushing it to a `ci/**` branch or opening a pull request, which needs the user's approval. **No Windows result is claimed.**
- View on Windows: still only compile-checked, never executed (`view/WINDOWS-NATIVE-PROBE.md`). The CI matrix does not build View.

## 5. Vocabulary corrections and tests [M]

GLON-VOCAB-1's four open items were reproduced and classified. **No first-class runtime vocabularies were added and binding semantics are unchanged.** Every program that loaded before still loads with the same bindings: vocab1, `library-audit`, `lib1-test`, `test-c1-accept` and the HTTPS / Kaka applications all pass.

| Item | Classification | V5 |
|---|---|---|
| **Nested forward references** (V1 relative: fail-stop at run time; V2 qualified: refused at load) | Genuine defect. `prescan_top` registered a nested vocabulary's name but not its members, so a reference before the nested body could not resolve. | **Fixed:** `prescan_top` predeclares a named nested vocabulary and its members (`lx_vocab_predeclare`, sharing `lx_vocab_create` with `lx_vocab_open`). |
| **Multi-level qualified adoption** (V3: a later `a/b/c` refused as "no such vocabulary") | Genuine defect. Adoption handled one level only. | **Fixed:** a qualified name whose vocabulary path does not exist falls back to an existing flat global of the same full name (`lx_global_bound`, which interns nothing). Unbound names are still refused. |
| **Global/member collision** (`get` vs `intent/get`) | Deliberate rule. Inside a vocabulary its member shadows a global of the same name; `global/NAME` reaches the global. | **Asserted by a test** (`member-shadow`). |
| **Prefixed definition before its declaration in the same source** (V4) | Deliberate restriction. Members are declared by the vocabulary body. | **Asserted by a test** (`err-prefix-before`: "no member 'x' in vocabulary p"). |

**Tests (`tests/vocab1`, 31 checks, all ok):**
- `forward-relative`, `forward-qualified` and `adopt-deep` move from `known` reports to checked results.
- New: `forward-deep` (`12 7 7`), `member-shadow` (`101 2 2 3`), `err-forward-member`, `err-adopt-unbound` and `err-prefix-before`.

The refusals still apply: an unknown nested member and an unbound qualified name are refused at load.

`known-literal-block-gc` remains a recorded defect of raw `masm` stores (§6).

## 6. Memory and capacity verification [M]

| Property | Evidence (all pass) |
|---|---|
| Safe lexical-local binding | vocab1 (locals, library-locals, nested-shadow, outer-levels); `library-audit` (no divergence in the C2 modules, Kaka or the HTTPS apps) |
| Checked managed-value block stores | `literal-store-check` (64K / 1M / 4M): a managed value stored into a literal block is refused with `SIN! 'escape 'block-set!`, and the literal is intact after collections |
| Collector and allocator optimisations | `mem-diff-test` (MEM-2 alloc, MEM-5 closure and INTENT-7 set differentials on constrained, desktop-256k, desktop-1m and desktop-4m: identical heap layouts); `test-c1-accept` 1M/4M variants; G3A endurance (identical live set and GC count to C2) |
| Instance and node-ID reuse | `test-c1-accept`: inst1 (0 sites, 0 globals per instance), inst2 (closing frees 64 ids, reused by the next instance), win3 |
| Repeated window lifecycles | `test-c1-accept`: 20 open/close cycles keep sites 849, globals 1,015, `view/id` 183 and 128 free ids; resize-only cycles keep 43,456 live cells |
| Memory errors in View's C core | `test-view-asan`, `test-table-asan` |
| Profiles and layouts | `profile-check`, `profile-reject-check`, `profile-common-test`, `code-capacity-check` (recursion limits as V4), `site-capacity-test`, `global-ctx-hash-test` (8 builds), `hash-capacity-check` |

**Server `SYM_CAP` 512 → 1,024 (`7d86a01b`):** this is a measured need (§3).
- **What it sizes:** `SYM_CAP` sizes host-side tables only: the interner's spelling pointers and three bitmaps, about 4 KB more.
- **What it leaves unchanged:** the regenerated server header differs only in `R0S1_SYM_CAP`. `M[]`, `GLOBAL_CAP` and the hash radix are unchanged.
- **Why 1,024:** it is the HTTPS line's own server value.
- **Other profiles:** constrained, constrained-notask, large and server-https are unchanged.
- **Consequence for `global_ctx_hash_test.c` (`ff39c17b`):** its reference identity ("radix 256, 512 symbols") had been asserted for every profile with 256 globals, so it failed for the server profile. It is now asserted in the reference build only. The encoding check (`word_id*256 + slot`, cell by cell) still runs for every 256-global profile, including server.

**Symbol headroom on the 64K profiles [M]:**
- Decorated locals cost every application symbols.
- The G3 HTTPS application (constrained-notask, `SYM_CAP` 512) needs **406** on the integrated core against **314** on C2. It still fits, with 106 to spare.
- An application that used more than about 420 symbols on C2 may now stop with `symbol_table_full` on the 64K profiles. The constrained reference's capacities were not changed (§10).

**Raw `masm` exception (unchanged, documented):**
- Raw `masm` stores remain outside the checked block-store guarantee.
- `literal-block-gc-check` and vocab1's `known-literal-block-gc` still reproduce the collector defect through such a word.
- VID's `vid/set-block!` and Intent's id-table stores use the internal raw store under documented preconditions.

The GC and C2's memory layout were not redesigned.

## 7. Full regression [M]

Run with `/mnt/d/repos/diagnostics/core-v5/v5-run.sh`, from a **clean detached git worktree** at `7d86a01b` (0 uncommitted changes at start), sequentially, every step under `timeout`. `ff39c17b` changes only `global_ctx_hash_test.c`; that test and `hash-capacity-check` were re-run in the same worktree at `ff39c17b`.

| Group | Steps | Result |
|---|---|---|
| Frozen S1 (tag check + direct diff) | 2 | **pass** |
| Root `make test` (s1, adversarial, claims, …) | 1 | **pass** (396 s) |
| Root suites: arch, profile check / reject / common, hash test (at `ff39c17b`), hash capacity, GC worklist overflow, primer, JSON, m1-large, site capacity, code capacity, literal block GC, literal store, INTENT-7, vocab1, lib1, `s-find`, modules, library audit, mem-diff (507 s), live-native, patrol, Kaka, io-file ×3, desktop, machine-integrity, iterators, host, `build-deps-check` | 32 | **pass** |
| **`make -C view test-c1-accept`** (`C1_ACCEPT_VARIANTS=1`) | 25 | **pass, 25/25** ("C1 ACCEPTANCE: PASS", 1,602 s) |
| Other View suites: wg/vg runners, widget gallery, gui-core, gui3, gui4, gui4-scroll, gui4b2c, gui-f2, global-capacity, vocab, table-host, view-asan, table-asan | 14 | **pass** |
| WASM (`make -B`) | 11 | **pass** |
| GlonBot: build-wasm, build-page, node 51/51, browser 9/9 | 4 | **pass** |
| CI native suite (Linux x86-64), including HTTPS G1–G3A | 22 | **pass** |

**Failures found and their status:**
- **`global-ctx-hash-test` exited 2 at `7d86a01b`:** its reference identity check fired for the server profile after the `SYM_CAP` change. Fixed in `ff39c17b`; it then passed (exit 0).
- **Before the fix**, HTTPS G3A failed on the integrated core (§3).
- **No other failures.**
- **No known inherited failures remain:** `wasm-standalone-test` (C2) is fixed. **No step is blocked by a missing dependency on Linux.** `build-deps-check` and HTTPS ran with OpenSSL.

**Not run:**
- Windows, macOS and AArch64 (no toolchain; §4);
- Android (no NDK);
- Jupyter `kernel-test` / `kernelspec-test` (no `jupyter_client`);
- View on a live display (SDL dummy only; the three-window demo was verified manually on WSLg by the user [H]).

**Against V4:** nothing that passed in V4 fails now. The changes are improvements:
- WASM 10/11 → 11/11;
- HTTPS and the `build-deps-check` OpenSSL cases blocked → pass.

## 8. Remaining platform limitations

- **Windows / macOS / Linux ARM64:** covered by the existing CI matrix, but not executed on this branch (needs an approved push).
- **View on Windows:** compile-checked only, never run.
- **Android:** no NDK here; not built.
- **64K profiles:** smaller symbol headroom than on C2 (§6).
- **Committed WASM artefacts:** stale (`standalone/glon.wasm` and those rewritten by the WASM targets). The build products are correct; the files in git are not refreshed.
- **Raw `masm` literal stores:** unchanged (outside the checked guarantee).
- **Profile status:** desktop-256k / 1m / 4m and server-https are `provisional`.
- **Git on this NTFS mount:** `core.fileMode=false` loses executable bits on import (environment, not repository).

## 9. Ready for promotion?

**Yes, as the common *development* baseline, on the evidence available here.** One C2-derived core runs:
- the server stack (HTTPS G1–G3A, with identical memory behaviour to C2 and faster);
- the desktop and View stack (`test-c1-accept` 25/25, ASan);
- the WASM targets (11/11) and GlonBot.

It does so from a clean worktree, with frozen S1 unchanged and no unexplained regressions relative to V4 or C2. The one regression relative to C2, the server symbol table, was measured, fixed and documented.

**Not yet ready as a *release*** until the CI matrix has run on Windows, macOS and ARM64, and the decisions below are made.

## 10. Decisions required before publication

1. **CI run.** Approve pushing `integration/c2-view` to a `ci/**` branch, or opening a pull request, so the existing matrix runs Windows, macOS and ARM64. Nothing was pushed.
2. **Server `SYM_CAP` 1,024.** Accept it, or choose another value; G3A needs at least 638 on the integrated core.
3. **64K symbol headroom.** Decide whether to:
   - accept the reduced headroom as a documented cost of lexical binding;
   - raise `SYM_CAP` on constrained-notask (leaving the constrained reference fixed);
   - reduce what decorated locals cost (a binding-implementation change, out of scope here).
4. **Profile status.** Promote desktop-256k / 1m / 4m from `provisional` to `verified`, and decide server-https's status.
5. **Committed WASM artefacts.** Decide whether to refresh `standalone/glon.wasm`, `web/demo.*` and `demo/shop/live.html` from the integrated core in a separate publication commit.
6. **Raw `masm` stores.** Keep them documented as outside the guarantee, or schedule collector support for literal blocks.
7. **Branch disposition.** Decide how `integration/c2-view` becomes the baseline (merge target, fast-forward of a new branch, or tag). Neither source branch was changed.
