# GLON-CORE-C2 — Single profile generator and layout safety

Date: 2026-11-27
Branch: `core/c2`, from C1 `df15c05`
Worktree: `/home/developer/projects/glon-core-c2`
Frozen S1: `f90496c26dc45c7a387d8fc8639cd2781507d3d2`
Status: implemented, tested and committed locally on `core/c2`. Not pushed,
merged, rebased or deployed. C3 not started.

**Result:** every capacity of a memory profile is now declared together in
`profiles/<name>.profile`, and one generator (`scripts/glon-profile.sh`) emits
the profile header, the S1 copy and the layout map. `r0_s1_layout.h` rejects
overlapping or unsafe regions at compile time. The one-at-a-time `MEM_*`
overrides that let profiles overlap task state are gone. The constrained 64K
map is unchanged and was verified cell by cell. All C2 acceptance tests and the
required regressions pass, with the limitations listed in §7.

## 1. Profiles

Eight profiles are declared. `STATUS` is the profile file's own classification:

| profile | status | MEM_CELLS | code cap / DS_INIT | main RS | managed heap | worklist | loader | tasks | globals / symbols / radix |
|---|---|---:|---:|---:|---:|---:|---:|---|---:|
| constrained | reference | 65,536 | 16,384 | 8,192 | 7,232 | 256 (fixed) | 14,800 | 3 × 3,400 | 256 / 512 / 256 |
| constrained-notask | verified | 65,536 | 16,384 | 8,192 | 7,232 | 256 (fixed) | 25,536 | none | 256 / 512 / 256 |
| desktop-256k | provisional | 262,144 | 16,384 | 8,192 | 65,536 | 2,048 | 151,056 | 3 × 3,400 | 1,024 / 2,048 / 1,024 |
| desktop-1m | provisional | 1,048,576 | 16,384 | 8,192 | 262,144 | 8,192 | 734,736 | 3 × 3,400 | 4,096 / 8,192 / 4,096 |
| desktop-4m | provisional | 4,194,304 | 16,384 | 8,192 | 1,048,576 | 32,768 | 3,069,456 | 3 × 3,400 | 16,384 / 32,768 / 16,384 |
| large | verified | 1,333,104 | 16,384 | 8,192 | 1,048,576 | 40,000 | 200,000 | 3 × 3,400 | 256 / 512 / 256 |
| server | verified | 1,633,104 | 16,384 | 8,192 | 1,048,576 | 40,000 | 500,000 | 3 × 3,400 | 256 / 512 / 256 |
| server-https | provisional | 1,633,104 | 20,480 | 4,096 | 1,048,576 | 40,000 | 500,000 | 3 × 3,400 | 512 / 1,024 / 512 |

`large`, `server` and `server-https` derive `MEM_CELLS` from the declared
regions plus a 1,024-cell tail. The desktop profiles come from the View/MS-1
records; their source commits are not present in this repository, so their
status remains provisional. `server-https` is provisional because the HTTPS
task/HTTP/JSON code it was measured with is not integrated here.

## 2. Memory-region layouts and task configuration

All profiles share the frozen lower map: code base 256, main return-stack top
24,576, fixed runtime state `[24576,32768)`, managed heap from 32,768.

| profile | heap range | worklist range | loader range | task arena | task table | tail |
|---|---|---|---|---|---|---|
| constrained | `[32768,40000)` | 25,057 + 256 | `[40000,54800)` | `[54800,65000)` | `[65000,65536)` | — |
| constrained-notask | `[32768,40000)` | 25,057 + 256 | `[40000,65536)` | none | none | — |
| desktop-256k | `[32768,98304)` | `[98304,100352)` | `[100352,251408)` | `[251408,261608)` | `[261608,262144)` | — |
| desktop-1m | `[32768,294912)` | `[294912,303104)` | `[303104,1037840)` | `[1037840,1048040)` | `[1048040,1048576)` | — |
| desktop-4m | `[32768,1081344)` | `[1081344,1114112)` | `[1114112,4183568)` | `[4183568,4193768)` | `[4193768,4194304)` | — |
| large | `[32768,1081344)` | `[1081344,1121344)` | `[1121344,1321344)` | `[1321344,1331544)` | `[1331544,1332080)` | `[1332080,1333104)` |
| server | `[32768,1081344)` | `[1081344,1121344)` | `[1121344,1621344)` | `[1621344,1631544)` | `[1631544,1632080)` | `[1632080,1633104)` |
| server-https | `[32768,1081344)` | `[1081344,1121344)` | `[1121344,1621344)` | `[1621344,1631544)` | `[1631544,1632080)` | `[1632080,1633104)` |

Task records use 16 cells each (`M1_TASK_REC_SIZE`); the 536-cell table region
holds the records plus wrapper blocks. A no-task profile declares
`M1_MAX_TASKS 0`, so it has no arena and no task table. `r0_s1_layout.h`
enforces the ordering, the 16,384 code minimum, the 4,096-cell main
return-stack minimum, worklist sizing, and task-slot fit.

## 3. Implementation

| file | change |
|---|---|
| `profiles/*.profile` | Eight profiles; every capacity declared together as `KEY=VALUE`; no per-key evaluation. |
| `scripts/glon-profile.sh` | The single generator: validates and lays out a profile, emits `glon_profile.h`, `s1_profile.[ch]` and `layout.txt`. Frozen `s1.c`/`s1.h` are read only. |
| `scripts/glon-stamp.sh` | Rewrites `.glon-config` and `build/civetweb.flags` only when their contents change. |
| `r0_s1_layout.h` | Compile-time layout check included by `r0_s1_runtime.c`. |
| `r0_s1.h` | `R0S1_DS_INIT` is a profile parameter; `R0S1_RS_INIT` stays frozen at 24,576. |
| `r0_s1_runtime.c` | Includes the layout check. |
| `Makefile` | Profile selection, generation, stamps, header dependencies, and the C2 test targets. One-at-a-time `MEM_*` overrides are hard errors. `GLON_HTTP_RECLAIM_M1=1` is a deprecated alias for `constrained-notask`. |
| `tests/core-c2/` | Layout probe, rejection cases, cross-profile program/limits checks, build-dependency regression. |
| CI, packaging, Android, shop HTML | Full/server builds use `MEM_PROFILE=server`; HTTP-only G3 uses `constrained-notask`. |

No frozen S1 file changed. No evaluator, parser, GC algorithm, language
semantics or runtime ABI changed.

### 3.1 Defects found and fixed during C2 acceptance

1. **Cross-profile program exceeded the minimum return stack.** The original
   `tests/core-c2/common.glon` nested 25×16 churn calls and a 50-deep string
   repeat; this overflowed `server-https`'s declared 4,096-cell main return
   stack. The churn nesting is now 20×16 (340 short-lived strings, still enough
   to force collections in the 7,232-cell heap) and the repeat is 40
   (160-byte string). All profiles now produce byte-identical output.
2. **Generated M1 addresses were not RAW-assembly safe.** The generator emitted
   `M1_ARENA_BASE` and `M1_TASK_TABLE` with an `L` suffix. The M1 library
   stringifies those macros into RAW S1 source, where `1331544L` tokenizes as
   `1331544` plus an unknown mnemonic `L`; this broke `m1-large-test` on every
   generated profile. The generator now emits plain integers for every `M1_*`
   address.

## 4. C2 acceptance tests

All commands were run sequentially with bounded timeouts.

| test | result |
|---|---|
| `make profile-check` | **PASS (8 profiles)**. Each profile builds, its S1 copy and header agree after `s1_reset`, and a recursive program returns 210. `profiles/constrained.profile` reproduces the built-in 64K `LAYOUT` line exactly. |
| `make profile-reject-check` | **PASS (15 cases)**. Twelve invalid profile files are rejected by the generator or by a named compile-time assertion; the D1 Kaka loader override, the D1 reclaim override and the removed arena-reclaim switch are rejected. |
| `make profile-common-test` | **PASS (8 profiles)**. `common.glon` output is byte-identical for every profile (10 lines). `limits.glon` returns 2,485 with an 8,192-cell main return stack and stops with a clean machine fail-stop on `server-https`'s 4,096-cell stack. |
| `make build-deps-check` | **PASS (30 checks)**. See §6. |

## 5. Existing regressions

| test | result |
|---|---|
| `./check-frozen-s1.sh` | **PASS**: frozen hash `f90496c…`, all frozen files unchanged. |
| `make arch-check` / `./arch-check` | **PASS**: constrained `mem_cells=65536 gc_heap=32768..40000 loader=40000..54800 m1_arena=54800 m1_task_table=65000 worklist_cap=256`, identical to C1/D1. |
| `make global-ctx-hash-test` | **PASS for all 8 profiles**: capacities 256, 256 (notask/large/server), 512, 1,024, 4,096 and 16,384; exact `word_id*radix+slot` entries; largest entry 268,533,755, below the 32-bit limit; clean `CONTEXT_FULL` at capacity. |
| Constrained G1E tuple-space checks | Checks 26–46 **PASS**, including the real 24-tuple space, three workers, router fan-out, drift, reset and determinism. |
| Bounded `./s1` (960 s bound) | **956 named passing checks, 0 `FAIL` lines**, matching C1's count and stopping point at NaSch check 8. The timeout is the pre-existing NaSch simulation stall, not a new regression. |
| `make json-test` | **`JSON_TESTS PASS`**. |
| `make m1-large-test MEM_PROFILE=large` | **`M1_LARGE failures=0`** after the M1 address-suffix fix. |
| `make https-g3a-test` | **`HTTPS_G3A_ACCEPTANCE PASS`** (10 scenario groups). |

## 6. Build safety and build-dependency regression

`make build-deps-check` passed all 30 checks across seven scenarios:

1. unchanged configuration: `glon`, the profile header and objects are up to
   date, and rerunning the same command builds nothing;
2. profile contents change: profile regenerated, `glon` relinked, new loader end
   and `MEM_CELLS` present in both generated files;
3. generator change: profile and `glon` become stale, then rebuild;
4. `r0_s1_layout.h`, `m1_layout.h` and `r0_s1.h` changes: runtime object and
   `glon` rebuild;
5. CivetWeb compiler flags: `civetweb.o` rebuilds only when the flags change;
6. switching named profiles: `glon` relinks, and an already generated profile is
   not regenerated;
7. unrelated targets do not rewrite `.glon-config`, and `glon` stays up to
   date.

Additional C2 checks:

- All eight `layout.txt` maps were parsed: regions are ordered, no region
  overlaps its predecessor, and each map ends at or below `MEM_CELLS`.
- The constrained profile's `LAYOUT` line and `arch-check` output match C1's
  64K map exactly.
- `constrained-notask` generates `M1_MAX_TASKS 0`, no arena and no task table,
  with loader `[40000,65536)`.
- Kaka's loader demand stays inside 25,536 cells (see §7), so the old
  loader/task-table overlap is gone.

## 7. Kaka migration

Kaka previously used two D1 overrides that let the loader overlap M1 task state:

- `GLON_HTTP_RECLAIM_M1=1` (`-DR0S1_HEAP_LIMIT=65000`) for G1–G3 HTTPS;
- `KAKA_LIMIT_FLAG` (`-DR0S1_HEAP_LIMIT=65536`) for Kaka native and WASM tests.

The second let the loader cover the task table `[65000,65536)` while the
harnesses still initialized task records there. C2 replaces both with
`MEM_PROFILE=constrained-notask`: no tasks, no task arena, no task table, and a
loader from 40,000 to 65,536 (25,536 cells).

Measured with the profile:

- `kaka-test`: loader high 24,128 / 25,536 after the self-test module.
- `kaka-fighter-test`: loader high **25,286 / 25,536** after
  `kaka-fighter.glon` (verified with a read-only probe).
- All seven Kaka native harnesses pass: `kaka-test`, `kaka-fighter-test`,
  `kaka-render-test`, `kaka-damage-test`, `kaka-terminal-test`,
  `kaka-wave-test`, `kaka-life-test`.
- `kaka-test` still reports `KAKA-SELFTEST PASS` and a bounded GC soak
  (`7232` cells of `7232`).

Kaka WASM was not rebuilt or executed in C2.

## 8. Cross-platform status

- Inspected the modified GitHub Actions endurance workflow, native suite,
  packaging script and Android D1 script. Their full/HTTPS builds now use
  `MEM_PROFILE=server`; HTTP-only G3 uses `constrained-notask`.
- Lightweight checks passed: `bash -n` on the CI/packaging scripts, `sh -n` on
  the POSIX C2 scripts, YAML parse of `glon-endurance.yml`, and `make -n` for
  minimal, server and notask builds.
- **Not executed:** Windows, macOS, Linux ARM64, Android, and WASM builds.
  No cross-platform, Android or WASM acceptance is claimed. The historical
  Windows/W5 and Android records remain authoritative only for their own
  configurations and dates.

## 9. Documentation

Updated current build guidance in `AGENTS.md`, `AI-QUICKSTART.md` and
`GLON-ARCHITECTURE-MEMORY.md` to the declared-profile mechanism. Historical
records (`GLON-HTTPS-SERVER-G3.md`, `GLON-HTTPS-SERVER-G3A.md`,
`GLON-WINDOWS-FILE-W5.md`, `release/RELEASE-MANIFEST.md`) keep their recorded
commands and measurements, with a dated C2 note giving the current equivalent
(`MEM_PROFILE=server` or `constrained-notask`) and stating that the old
one-at-a-time `MEM_*` spellings are rejected.

## 10. Remaining integration dependencies

- The GUI/MS-1 source commits behind `desktop-256k`, `desktop-1m` and
  `desktop-4m` are not present here; those profiles remain provisional.
- `server-https` remains provisional until the HTTPS task/HTTP/JSON code it was
  measured with is integrated into this core.
- The NaSch simulation stall still prevents `make test` as one command; the
  bounded `./s1` result above is the applicable evidence.
- C2 does not port the later HTTPS task-record extensions, HTTP bridge, H3E or
  JSON J1/J2 work from C1's dependency list.
- Windows, macOS, Android and WASM builds were not exercised.
- No push, merge, rebase, tag or deployment was performed. C3, vocabulary
  integration and Glon Worlds development were not started.
