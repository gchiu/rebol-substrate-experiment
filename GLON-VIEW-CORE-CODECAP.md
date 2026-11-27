# GLON-VIEW-CORE-CODECAP — larger executable-code region for the desktop profiles

Date: 2026-11-27.

| | |
|---|---|
| **Branch** | `integration/c2-view`, after `7f08549c` (V3 stopped at the code-space gate: 10 cells free) |
| **Approved** | Option 1 of `GLON-VIEW-CORE-V3.md` §3: more code through C2's existing desktop profiles |
| **Status** | Committed locally only. |

**Unchanged:**
- frozen S1 (`f90496c…`);
- C2's memory-map architecture;
- the constrained, constrained-notask, large, server and server-https profiles;
- `R0S1_DS_RESERVE` (256);
- the checked `block-set!`.

Evidence: `/mnt/d/repos/diagnostics/codecap.*` (variants, recursion probes) and `/mnt/d/repos/diagnostics/codecap-suite/` (regression).

## 1. Change

`profiles/desktop-256k.profile`, `desktop-1m.profile` and `desktop-4m.profile` change `CODE_CAP` from 16,384 to **17,408**.

`CODE_CAP` is C2's profile parameter for `R0S1_DS_INIT`, the boundary between code and the data stack. The generator writes it into the profile's S1 copy (`DS_INIT`) and its header. The code limit is `DS_INIT − R0S1_DS_RESERVE`.

| Region | Before | After |
|---|---|---|
| code | `[256, 16128)` = 15,872 cells | `[256, 17152)` = **16,896** cells (+1,024) |
| data-stack reserve | `[16128, 16384)` | `[17152, 17408)` (256, unchanged) |
| main return stack | `[16384, 24576)` = 8,192 | `[17408, 24576)` = **7,168** |
| fixed state, heap, worklist, site table, loader, tasks | — | **unchanged** (the loader and every address ≥ 24,576 are identical) |

`r0_s1_layout.h` checks the result at compile time. All its invariants hold:
- code ≥ 16,384 cap;
- return stack ≥ 4,096;
- no overlap with the fixed state at 24,576;
- the ordered regions end within `MEM_CELLS`.

`profile-check` verifies that the S1 copy and header agree after `s1_reset` (SP = `R0S1_DS_INIT` = 17,408).

## 2. Audit of fixed-address assumptions [S]

- **Frozen `s1.c`** uses `DS_INIT`, `RS_INIT` and `HEAP_BASE` only in `s1_reset` (SP, RP, HP), with no bounds checks. The generated copy changes only `DS_INIT`. `RS_INIT` (24,576) and `HEAP_BASE` (32,768) stay frozen; the generator asserts both.
- **Runtime C and emitted code** refer to the boundary only symbolically, through `R0S1_DS_INIT`, `R0S1_RS_INIT` and `R0S1_CODE_LIMIT`:
  - the `masm` assembler's code-limit refusal;
  - the collector's main-stack root range `[DS_INIT, RS_INIT)` and its main-world test (`SP ≤ DS_INIT`; task stacks sit far above in the arena);
  - the data-stack guard and floor (`RV_DSLIM` = end of code + 64);
  - the return-stack guard (`RP − 96 < DS top`);
  - the escape-law stack-context range;
  - the scheduler record;
  - the stack sentry's diagnostics.

  Each is emitted with the profile's value. No literal 16,384 or 24,576 appears in emitted code.
- **C test harnesses with literal 16,384 / 24,576** are all built for the constrained profile, which is unchanged: `r0_s1_debug_tests.c`, `r0_s1_m1_tests.c`, `r0_s1_integrity_tests.c`, the Kaka and desktop harnesses, and others. Those literals are buffer sizes or comments, or describe the 64K map.
- **M1 tasks** keep their own stacks in the arena, and the arena is unchanged.

## 3. Measured capacity [M]

**Variants.** Built from scratch copies of `desktop-1m.profile` with each `CODE_CAP`. Recursion is measured with `lim/sum n`, one closure activation per level (C2's `limits.glon` shape).

| CODE_CAP | Code cells | Return stack | Recursion limit | Beyond the limit | Intent stack + c1 app: code free |
|---:|---:|---:|---:|---|---:|
| 16,384 (C2) | 15,872 | 8,192 | 84 | clean fail-stop | **10** |
| **17,408 (chosen)** | 16,896 | 7,168 | **73** | clean fail-stop | **1,034** |
| 18,432 | 17,920 | 6,144 | 62 | clean fail-stop | 2,058 |
| 19,456 | 18,944 | 5,120 | 52 | clean fail-stop | 3,082 |
| 20,480 (layout maximum: 4,096-cell RS minimum) | 19,968 | 4,096 | 41 | clean fail-stop | 4,106 |

**About the numbers:**
- One activation uses about 97 return-stack cells. The runtime reserves at most 96 per invocation plus the call, so every 1,024 code cells cost about 10.5 levels of recursion.
- The Intent stack + c1 measurement uses the checked `block-set!` (V3 `core_words.glon`). Its globals (1,009), sites (849) and loader use are identical in every variant; only code headroom changes.
- **Return-stack use of the Intent stack's own load:** at most 1,122 cells (`intent.glon`, about 12 activations), measured with `libload`'s new `rs_peak`.
- **Not yet known:** the run-time peak of the View and Intent suites. They aren't imported until V3. Measuring it there is the input to any further increase.

**Available range and recommendation [A].** Any `CODE_CAP` from 16,384 to 20,480 satisfies the layout invariants. **17,408** restores roughly 1,000 cells of code headroom for the View stack (100× what remains at 16,384) while keeping 73 levels of recursion. That is above C2's own 70-level `limits.glon` floor, and within 10 levels of the original 84. Go further (18,432 for about 2,000 free cells and 62 levels) only if V3's measured View/Intent run-time return-stack peak leaves margin at 6,144 cells.

**V3 measurement, added after the View import [M]:**
- The View/Intent run-time return-stack peak is **4,124 cells** (`inst1-components`; `c1-accept` 3,964; membership at most 1,932).
- The margin is **3,044 cells at 17,408**, about 2,020 at 18,432, and about 1,000 at 19,456.
- In the membership session, the code headroom is 1,034 cells at 17,408 (GLON-VIEW-CORE-V3.md §3).

**Recommendation: keep 17,408.** It leaves about 1,000 free cells on both the code side and the return-stack side, and it passes the full View acceptance suite.

## 4. Tests

All steps ran sequentially with exit statuses recorded (`diagnostics/codecap-suite`). **Every step exits 0 except `build-deps-check`:**
- `check-frozen-s1`, `arch-check`;
- `profile-check`, `profile-reject-check`;
- `profile-common-test`: every profile's `common.glon` output is identical to constrained, and the desktop profiles return `limits.glon` = 2485 with their 7,168-cell return stack;
- `global-ctx-hash-test`, `hash-capacity-check`, `gc-worklist-overflow`, `primer --check`;
- `json-test`, `m1-large-test`;
- `site-capacity-test`, `code-capacity-check`;
- `literal-block-gc-check`, `literal-store-check`, `intent7-set-check`;
- `vocab1` (64K, and separately desktop-1m and desktop-4m: 23/23, the 3 known reports unchanged);
- `lib1-test` (47/47 in 64K/1M/4M);
- `s-find-check`, `modules-check`, `library-audit`;
- `mem-diff-test` (64K/256K/1M/4M);
- `glon-live-native`, `patrol-test`, `kaka-test` (7);
- `io-file-test`, `io-file-stream-test`, `io-file-inplace-test` (36/0);
- `desktop-test`, `machine-integrity-test`, `iterators-test`, `host-test`.

**`./s1`:** exit 0, 1,025 `ok:` lines, 0 FAIL.

**`build-deps-check`:** 27/30, exactly as on unchanged C2: the 3 CivetWeb cases need OpenSSL headers, which are absent here.

**New: `make code-capacity-check`.** For every profile it reports the code limit, the return stack and the recursion limit (binary search), and checks for a clean fail-stop one level deeper. It asserts that the desktop profiles have code limit 17,152 and at least 70 levels. All 8 profiles pass:
- constrained, constrained-notask, large, server: 15,872 code cells, 84 levels;
- server-https: 19,968 code cells, 41 levels;
- desktop-256k, 1m, 4m: 16,896 code cells, 73 levels.
