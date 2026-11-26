# Glon Memory Profile M1 — Application-Specific Memory Layout (feasibility study)

Date: 2026-11-26
Branch: `experiment/glon-mem-profile-m1`
Baseline: G3J.12, commit `b804352`
Host: Debian 13 VPS, 2 vCPU, 967 MiB RAM.
**Design/measurement only — no runtime, capacity or auth changes.**

## 1. Current memory map (verified)

S1 profile constants (frozen `s1.c`/`s1.h`, mirrored in `build/profile/s1_profile.c`):

| Region | Range | Source | Configurable? |
|---|---|---|---|
| Compiled code | `[CODE_BASE=256, R0S1_CODE_LIMIT)` grows up | frozen `s1.c` / masm | code limit derived |
| Data stack | `[R0S1_CODE_LIMIT, DS_INIT=16384)` grows down | frozen `s1.c` | reserve (`CODE_RESERVE`) |
| Return stack | `[RS_INIT=24576 …]` grows down | frozen `s1.c` | no |
| Managed heap | `[GC_HEAP_BASE=32768, GC_HEAP_LIMIT=R0S1_HEAP_BASE)` | frozen base + `MEM_MANAGED_CELLS` | size (large profile) |
| Loader arena | `[R0S1_LOAD_BASE, M1_ARENA_BASE=54800)` | `MEM_LOADER_CELLS` / `M1_ARENA_BASE` | size/base |
| M1 task table | `M1_TASK_TABLE=65000` | `M1_TASK_TABLE` | base |
| M1 task stacks | `M1_ARENA_BASE + slot*M1_TASK_CELLS` | `M1_TASKS`, `M1_TASK_CELLS`, `M1_DS_OFF`, `M1_RS_OFF` | yes |
| Symbol table | `syms[SYM_CAP=1024]` | `r0_s1_runtime.c` | **fixed** |
| Global context | cap 256 | `r0_s1_runtime.c:3872` | **fixed** |

- **Compile-time constants (frozen S1):** `CODE_BASE=256`, `DS_INIT=16384`,
  `RS_INIT=24576`, `HEAP_BASE=32768` (in frozen `s1.c`/`s1.h`).
- **Derived:** `R0S1_CODE_LIMIT = DS_INIT - R0S1_DS_RESERVE` (G3J.12 made the
  reserve overridable, default 256).
- **Build-configurable (Makefile, large profile):** `MEM_MANAGED_CELLS`,
  `MEM_WORKLIST_CELLS`, `MEM_LOADER_CELLS`, `M1_TASKS`, `M1_TASK_CELLS`,
  `M1_DS_OFF`, `M1_RS_OFF`, `M1_WRAPPER_DELTA`, `CODE_RESERVE`.
- **Embedded in compiled S1 instructions:** the assembled code size itself (the
  masm refuses a block ending above `R0S1_CODE_LIMIT`); RAW/machine addresses
  reference `M1_ARENA_BASE`, `M1_TASK_TABLE` (compile-time).

**No overlaps (current):** code `[256,16128)`, data `[16128,16384)`, return
stack `[24576,…]`, managed heap `[32768, R0S1_HEAP_BASE)`, loader
`[R0S1_LOAD_BASE, 54800)`, M1 arena `[54800, …)`.

## 2. Measured requirements (G3J auth app, task build)

- **Compiled code:** the task build (`file,http,sqlite,sec,task`) + `json.glon`
  needs **521 code cells**; the libs+app already use ~16,207 (only 49 free below
  the 16256 limit at `CODE_RESERVE=128`). Total static need ≈ **16,728 cells** —
  which exceeds `DS_INIT` (16384). (Static, exact: the masm reports the need.)
- **Symbols:** ~233 global entries used (G3J.5); `SYM_CAP=1024` (fixed).
- **Global context:** ~233/256 (near the fixed 256 cap).
- **Loader:** `MEM_LOADER_CELLS=500000` (config); high-water per request is small
  (a few hundred cells), reclaimed per expression.
- **Tasks:** 3 (M1_TASKS=3), each `M1_TASK_CELLS=3400` (400 DS + 3000 RS).
- **Data stack:** deepest measured 87 cells (reserve 256 is generous).

Exact static: code 16,728 cells, symbols ~233, global ctx ~233. Runtime high-water:
loader ~500 cells/request, data stack ≤87.

## 3. Configuration mechanisms

| Option | Complexity | Frozen-S1 impact | Constrained compat |
|---|---|---|---|
| A. Existing fixed profiles | none | none | yes (default) |
| B. App-specific generated build config (Makefile/profile per app) | med (generate profile, set `DS_INIT`/map) | **changes the code/data boundary** (`DS_INIT`) — frozen `s1.c` | keep constrained default |
| C. Startup-selectable layout | high (runtime map selection) | large (map must be valid at reset) | risky |

- **A** cannot fit the auth app (code 16,728 > 16,384).
- **B** is the smallest viable: generate an app-specific profile that raises the
  code region (e.g., `DS_INIT` 16384 → 20480) and re-derives `CODE_LIMIT`,
  keeping the frozen 64K constrained profile as the default. It changes the
  frozen `s1.c` constants for that build only (a scaling experiment).
- **C** is out of scope (runtime map selection would touch the reset path).

## 4. Proposed profile (application request)

An app-specific generated build could request:
- `min_code_cells` (e.g., 20,480) → sets `DS_INIT`/`CODE_LIMIT` for that build.
- `max_tasks` (3) + `task_cells` (3400) + DS/RS offsets.
- `sym_cap` (1024) and `global_ctx_cap` (256) — currently fixed; would need
  build-time overrides (not in this study).
- `managed_cells` (1,048,576) + `worklist` + `loader_cells`.
- Safety margins: data-stack reserve ≥ deepest use (87) + guard (64) → ≥151;
  keep 256.

Only the code/data boundary and the M1 arena base are interdependent (the loader
grows to `M1_ARENA_BASE`; the code grows to `DS_INIT`). Not all regions resize
independently: raising `DS_INIT` pushes the return stack / heap / loader / M1
bases up (they are laid out sequentially), so the total must stay within 65,536.

## 8. Risks / compatibility

- Changing `DS_INIT` (frozen `s1.c`) for a build alters the memory map — a
  scaling experiment, not the default. The frozen 64K constrained profile must
  remain the conformance baseline.
- The M1 arena / task table / loader bases shift when `DS_INIT` moves; all
  dependent `_Static_assert`s and RAW addresses must be re-derived.
- Symbol/global-context caps are fixed arrays; raising them is a separate change.

## 9. Smallest safe implementation step

Generate an **app-specific profile** (option B) that sets `DS_INIT` (and thus
`CODE_LIMIT`) high enough for the app's measured code (e.g., 20,480), re-derives
the downstream bases (RS/heap/loader/M1) within 65,536, and keeps the frozen
constrained profile as the default. This is a build-time scaling experiment
requiring approval (it changes frozen `s1.c` constants for that build only).

## 10. Status

Feasibility established: the auth app needs ~16,728 code cells, which exceeds the
frozen 16,384 code/data boundary; a bounded `CODE_RESERVE` tweak cannot fit it.
An app-specific generated profile (raising `DS_INIT`) is the smallest viable
mechanism, but it changes frozen-S1 constants and needs approval. No runtime/
capacity/auth change was made in this study.