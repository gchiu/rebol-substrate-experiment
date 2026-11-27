# GLON-VIEW-CORE-V3 — stopped at the code-space gate

Date: 2026-11-27.

| | |
|---|---|
| **Branch** | `integration/c2-view` after V2 `e14455aa` |
| **Status** | **Stopped before importing View, as the V3 brief requires.** No View, VID, Intent or native-window code is imported. Nothing else changed: runtime, memory map, profiles, hash encoding, GLON-VOCAB-1 semantics and frozen S1 are all as in V2. Committed locally only. |

**The gate.** Measure the combined runtime's code-space headroom with the literal-safety check loaded, and stop if fewer than 20 code cells remain.

## 1. Measurement [M]

**Runtime and libraries:**
- V2 runtime (12,890 emitted cells; `R0S1_CODE_LIMIT` 16,128) in `desktop-1m` and `desktop-4m`.
- The bare-session runner `tests/core-v2/libload.c`.
- The Intent session's library list exactly as `gv_glon_host_load_desktop_vid` loads it, then `intent_windows.glon` and the c1 acceptance application.
  - Shared `demo/shop` libraries: V2.
  - View libraries: read from glon-gui-f2 `17b819db`.
  - `view/glon/core_words.glon`: replaced by the V3 version below.

**The V3 `core_words.glon` probed:**

```
value-tag: masm 1 [ LIT 16 MOD LIT 16 MUL ARITY 1 EXIT ]
block-set-raw!: :vid/set-block!            ; internal raw store, no code of its own
managed-ref?: masm 1 [ DUP LIT 16 MOD SUB DUP LIT REG_HP @ LT MUL LIT 32768 GE
                       LIT 16 MUL ARITY 1 EXIT ]            ; 42 cells
block-store-ok?: func [b v] [ ... ]          ; ordinary Glon, as demo/shop/block.glon
block-set!: func [b i v] [ ... checked ... ] ; as demo/shop/block.glon (V2)
```

**Results** (both profiles give the same code figures):

| Point | Globals | Sites | Code cells free |
|---|---:|---:|---:|
| after `core_words.glon` | 108 | 53 | 1,441 |
| after `intent.glon` | 955 | 809 | **10** |
| after `intent_windows.glon` + c1 application | 1,009 | 849 | **10** |
| (V1/V2 reference, unchecked `core_words`) | 1,006 | 847 | 52 |

**10 < 20: the gate fails.** The checked `block-set!` costs exactly its 42-cell `managed-ref?` (52 → 10). No other code changed.

**Code cells assembled per library in the Intent session:**

| Library | Cells |
|---|---:|
| `view_cmd.glon` (12 masm words) | 992 |
| `jupyter/prelude.glon` | 416 |
| `strings-ext.glon` | 504 |
| `strings.glon` | 304 |
| `view_lib.glon` | 248 |
| `strings-text.glon` | 170 |
| `intent.glon` | 138 |
| `list.glon` | 131 |
| `core_words.glon` (value-tag 27 + managed-ref? 42) | 69 |
| every other View library | 0 |

The emitted runtime uses 12,890 of the 15,872 cells between code base 256 and the 16,128 limit. That leaves 2,982 for libraries; they use 2,972, leaving 10.

## 2. What was not done

- **The safety check was not removed or weakened.** Per the V3 brief, View does not keep an unchecked public `block-set!`.
- **C2's memory layout and profiles were not changed.**
- **View was not imported.** `make -C view test-c1-accept` and the 1M/4M membership suites were not run on the combined runtime, and no single-window or WSLg verification was attempted.

## 3. Options for a decision

None is implemented. Each needs approval.

| # | Option | Code cells gained | Who decides / what it changes |
|---|---|---|---|
| 1 | **Raise `CODE_CAP` in the desktop profiles.** C2 already supports it: `server-https` uses 20,480. For example, 16,384 → 17,408 for desktop-256k / 1m / 4m. | +1,024 (any amount; the main return stack shrinks by the same, here 8,192 → 7,168, still above the 4,096 minimum `r0_s1_layout.h` enforces) | C2 owner: a declared profile change, no runtime change. Recursion-depth headroom must be re-measured (View's deepest paths, the membership tests). |
| 2 | `R0S1_DS_RESERVE` 256 → 192 (GLON-MEM-4 §7) | +64 | C2 owner: a runtime constant (the data-stack guard). |
| 3 | Reduce library `masm` code: shared workers between `view_cmd.glon` (992) and `strings-ext.glon` / `strings-text.glon`, or fewer `EXIT`s (17 cells each) in multi-exit words | likely tens [A], unmeasured | View/library change; performance-sensitive code (GLON-MEM-*, INTENT-7 measured paths). |
| 4 | Put `managed-ref?` in the runtime as a native or `CALL` target | the 42 cells move into emitted code, so no gain unless it is much smaller there | C2 owner: a runtime change. |

**Recommendation [A].** Option 1 gives the most room for the least risk: C2's generator, layout checks and `server-https` precedent already cover it, and nothing in the runtime changes. It needs the C2 owner's agreement, because it changes three declared desktop profiles and their main return-stack depth. After that change, V3 resumes with the `core_words.glon` above, and the gate is re-measured.

## 4. Reproduce

```sh
make libload-one MEM_PROFILE=desktop-1m
build/core-v2/libload-desktop-1m <the file list in diagnostics/core-v3-headroom.*/files.txt>
```

The probe copy of `core_words.glon`, the file list and the per-library table are in `/mnt/d/repos/diagnostics/core-v3-headroom.*`.
