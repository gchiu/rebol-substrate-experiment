# Glon AvP G5h — prepared wall execution

Branch: `glon-avp-g5h`  
Base: `6a255bc41e6891cee2d6fbce8dca1b675c40ca0f` (G5g)  
Configuration: native 64K reference machine, WSL2, AMD Ryzen 5 7535U,
GCC 11.4.0, `-std=c17 -O2`, SDL2. No frozen-S1, primitive, resolution,
artwork or gameplay change.

## Result

The existing seven-primitive substrate can execute the wall calculation with
far fewer instructions by preparing the calculation with the existing `masm`
lowerer. The integrated experimental path (`--raw-wall`) is correct on the
existing self-test and reduces wall execution by about **86%**:

| Textured benchmark, same host binary | Existing | Prepared |
|---|---:|---:|
| FPS | **6.2** | **19.5–20.3** |
| Wall projection | **129.34 ms** (median of 3 × 120) | **18.53 ms** (median) |
| Texrow | 22.90 ms | 23.48 ms |
| Total frame | 160.53 ms | 51.23 ms |
| Wall S1 ops/frame | **33,377,988** | **4,555,404** |
| Wall HOST calls/frame | **6,014,444** | **846,833** |
| S1 ops/ray | 208,612 | 28,471 |
| S1 ops/candidate | 20,861 | 2,847 |
| HOST calls/candidate | 3,759 | 529 |

Simple mode: **6.7 FPS → ~30.4 FPS** (wall 19.65 ms, texrow 2.68 ms,
total 32.90 ms, 3 × 120-frame runs).

This is a measured result from the same production binary. No host-layout
change is being credited as an algorithmic improvement.

## 1. Baseline and benchmark stability

G5g found that host C layout can move wall time between ~103 and ~136 ms. G5h
keeps the host layout fixed and compares the default and prepared render blocks
inside one binary.

- Compiler: `cc (Ubuntu 11.4.0)`, flags `-std=c17 -O2 -Wall -Wextra`.
- Production binary: `build/doom`, SHA-256
  `e57436d35578378c204514a44f19f39d6057e8cd4311befb518c74da3ae36fd8`.
- Diagnostic opcode binary: `build/doom-opcode`, SHA-256
  `3d90f440724722ef99cce59395a578210c955ac952449666338001ccdf0a88f4`.
- Raw worker source: `raw-wall.glon`, SHA-256
  `2f0e08b144f0c13a0b634a2e6fd7191fbcc226b4a8e4f8fee0596db17687dd4c`.
- Inputs: deterministic fixed tick sequence, `--bench 120`, 320×200
  framebuffer, 160 projection columns.
- Variation: default wall 129.12–129.93 ms; prepared wall 17.95–18.65 ms
  over 3 × 120 frames. Medians are reported.

The default path is unchanged from G5g apart from the added experimental mode
and report code. It still measures ~6 FPS; the prepared path is the only
behavioral difference in the comparison.

## 2. Annotated trace of one wall candidate

The default wall block evaluates the same source structure for every candidate.
The G5g opcode profile gives the following per-candidate attribution for the
successful/failed candidate population (33,377,988 wall operations / 1,600
candidates):

| Category | Existing operations/candidate | Prepared operations/candidate |
|---|---:|---:|
| LIT constants/temporaries | 8,698 | 1,183 |
| DUP | 505 | 57 |
| DROP | 181 | 20 |
| `@` reads (globals/context/stack) | 4,413 | 622 |
| `!` writes (globals/context/temp) | 1,956 | 255 |
| 0BRANCH | 1,350 | 182 |
| branches taken | 1,111 | 154 |
| HOST calls | 3,759 | 529 |
| **Total S1 operations** | **20,861** | **2,847** |

Default HOST arithmetic per candidate is approximately: add 1,462; subtract
200; multiply 423; divide 138; modulo 172; equality 877. The prepared worker
reduces these to roughly add 221; subtract 26; multiply 66; divide 16;
modulo 20; equality 103. The mathematical calculation itself is only about
10–15 operations; the remaining 20,000-odd default instructions are expression
traversal, activation, lookup, temporary storage and result/control machinery.

The trace of one candidate is therefore:

1. **Source expression evaluation.** The wall block's `either`/infix forms are
   evaluated by the block/expression evaluator for every column.
2. **Activation and control.** The evaluator pushes/saves frames and
   continuations even though the wall source has no source-level function call
   per candidate.
3. **Variable access.** Named globals (`rayx`, `rayy`, `best`, wall constants)
   require context/lexical lookup and memory traffic.
4. **Arithmetic.** One intersection division and a few multiplies/adds.
5. **Conditional branches.** Parallel-ray guards, positive-distance guards,
   endpoint bounds, and nearest-wall selection.
6. **Temporary/result handling.** Candidate updates and the five output values
   (`top bot color u meta`) are built and printed.

Prepared execution removes the repeated lookup/traversal/activation by
assembling the same calculation once into S1 (`masm`), with constants and
branch targets resolved at assembly time.

## 3. Reusable execution structure

The following were identified as structure-level work repeated for every
candidate:

- operator/native lookup for each infix word;
- expression traversal and block dispatch;
- branch destination resolution;
- context/global binding lookup;
- temporary slot management;
- repeated constant loads;
- function/evaluator dispatch around the wall block.

The prepared representation uses the existing `masm`/RAW path, not a new
compiler. It assembles:

- `doom/raw-setup` (4 args): unpack camera/ray values and initialise
  best/packed state;
- `doom/raw-walls1` (0 args): first five wall checks;
- `doom/raw-walls2` (1 arg): last five walls plus the conditional door wall;
- `doom/raw-finish-print` (0 args): projection, shading, packing and five
  `HOST_PRINT`s;
- a small Glon wrapper around the camera transform and these workers.

The workers use only the frozen seven portables plus the existing derived
`CALL`/`EXIT` macros and existing scratch cells. Masm source is not interned in
the loader; only the callable records and Glon wrapper/block source survive.

## 4. Prepared representation and correctness

Isolated prototype: `bench/prepared.glon`; integrated experimental path:
`raw-wall.glon` and `--raw-wall`.

Representation details:

- ten wall rows are encoded as immediate axis/coordinate/bounds/color/material
  constants, not a runtime loader table;
- the door wall is a conditional final check using `doom/door-blocked`;
- packed state stores `color*8 + material` and best distance; u is recomputed
  from best and the appropriate ray component at output;
- projection/shading and `top`/`bot` clamps preserve the Glon formulas;
- no intersection code is implemented in native C.

Correctness tests:

- 10 isolated cases covering both endpoints, mid-ray, parallel components,
  near-zero components, alternate camera positions/orientations, boundaries
  and door open/closed: **10/10**, outputs identical to the native reference.
- Isolated full 160-ray checksum: **112,340** for prepared and native C.
- Full game `--self-test --raw-wall`: **passed**, including deterministic
  framebuffer, wall/floor/ceiling alignment, texture coordinates, materials,
  door, Alien, occlusion, collision and repeated-session memory stability.
- Default `--self-test`: **passed** unchanged.

## 5. Instruction and HOST reduction

Integrated game, textured wall stage:

| Stage | Existing | Prepared |
|---|---:|---:|
| S1 ops/frame | 33,377,988 | **4,555,404** |
| S1 ops/ray | 208,612 | **28,471** |
| S1 ops/candidate | 20,861 | **2,847** |
| HOST/frame | 6,014,444 | **846,833** |
| HOST/candidate | 3,759 | **529** |
| Reduction | — | **86.4% S1, 85.9% HOST** |

Isolated prepared prototype (no game wrapper/loader): one ray **18,752 S1
ops**; a full 160-ray frame **2,133,773 S1 ops** and **363,049 HOST calls**;
9.1 ms including 800 output integers. This confirms the integrated numbers are
not a host-layout artifact.

## 6. Native C comparison

The native reference is a correctness/performance floor, not a measurement of
interpreter dispatch alone.

- full 1,600-check isolated wall: **0.00126 ms**
- prepared isolated full frame: **9.1 ms** (including prints)
- existing isolated Glon wall: ~112 ms (G5g)
- checksums: prepared **112,340**, native **112,340**

The prepared path is still interpreted as S1 by the frozen machine. Its
remaining cost is the S1 interpreter plus Glon wrapper/output, not repeated
source lookup.

## 7. Loader and code-space budget

All numbers are the same native 64K machine; no capacity was raised.

| Measurement | Default | Prepared/raw |
|---|---:|---:|
| Loader used after game | 12,102 | 12,272 |
| Loader after host blocks | 14,162 | **14,322** |
| Loader free | 638 | **478** |
| Code cursor (`asm_here`) | 13,126 | **14,828** |
| Code limit | 16,128 | 16,128 |
| Code headroom | 3,002 | **1,300** |
| Managed frontier | 33,440 | **33,472** |
| GC | 0 | 0 |

Prepared construction adds about **170 loader cells** and **1,702 code cells**.
Masm source itself does not consume loader cells; the added loader use is the
callable records plus the wrapper/block source. There is no data table, no
managed allocation, and no steady-state growth. The raw file is loaded only in
the experimental mode; default mode is unchanged.

Preparation took about **0.39 ms** in the isolated benchmark. Peak construction
and steady-state loader use are the same 14,322 cells after load; code is
14,828 cells, so no temporary expansion is hidden in the managed heap.

## 8. Complete wall workload and game FPS

The raw path preserves nearest-wall selection, texture u, shading, door,
occlusion and the five-value span protocol. The host's existing raster/texture
and floor/ceiling path is unchanged.

| Textured | Existing | Prepared |
|---|---:|---:|
| FPS | 6.2 | **19.5–20.3** |
| Wall | 129.34 ms | **18.53 ms** |
| Texrow | 22.90 ms | 23.48 ms |
| Total | 160.53 ms | **51.23 ms** |

Simple mode: **6.7 → 30.4 FPS**. The prepared wall is no longer the dominant
CPU stage in textured mode; texture rows and host presentation are now the
larger remaining costs.

## 9. Reserve primitives

**No additional primitive is justified by this experiment.** The prepared
worker already uses the frozen seven primitives (`LIT DUP DROP @ ! 0BRANCH
HOST`) plus the existing derived `CALL`/`EXIT` macros. It eliminates 86% of the
operations without a VM change.

A hypothetical fused arithmetic or block primitive would shave at most a small
fraction of the remaining 2,847 operations/candidate; it would not remove the
Glon wrapper or output machinery. The primary problem was **program
representation and repeated evaluator lookup**, not missing instructions. Do
not add an instruction to compensate for source-level inefficiency.

## 10. Tests, changed files and recommendation

### Tests

- `make test`: default texture check, self-test, projection-test,
  stationary-test, **raw-wall self-test**, trace, frozen-S1 check.
- `make diagnostics`: opcode build and isolated benchmarks.
- `doom-opcode --opcode-bench 5 --raw-wall`.
- Isolated ten-case prepared correctness and full checksum.
- No failures. Frozen S1 check passes.

### Changed files

- `experiments/glon-doom/doom-host.c` — experimental `--raw-wall` mode,
  raw-file loading, raw render blocks, loader reporting, diagnostic support.
- `experiments/glon-doom/raw-wall.glon` — prepared masm wall workers and
  game wrapper.
- `experiments/glon-doom/Makefile` — raw-wall self-test and diagnostics.
- `experiments/glon-doom/README.md` — G5h summary and commands.
- `experiments/glon-doom/G5H-INVESTIGATION.md` — this report.
- `experiments/glon-doom/bench/` — prepared prototype, frame input,
  execution benchmarks, opcode benchmark target.
- `GLON-ARCHITECTURE-MEMORY.md`, `MEMORY.md` — measured result entry.

### Recommendation for G5i

1. Make the prepared wall path the default after an explicit code review of the
   unchecked masm workers and their input domain; the 1,300-cell code headroom
   is positive but no longer large.
2. Keep the default Glon raycaster as a reference/oracle for regression tests
   until the raw path has passed camera/door/parallel/boundary cases in the
   normal suite.
3. Optimize texture rows or presentation next; wall is no longer the dominant
   textured cost.
4. Do not add a VM primitive. If more headroom is needed, compact the raw
   worker code or split optional features; do not enlarge the 64K map.
