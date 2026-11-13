# Glon AvP G5g — execution-cost investigation

Branch: `glon-avp-g5g`  
Base: `13e0f5bd53f49b724495e4dafcb7e770802bc2ea` (G5f)  
Configuration: native 64K reference machine, WSL2, AMD Ryzen 5 7535U,
GCC 11.4.0, `-std=c17 -O2`, SDL2. No frozen-S1, primitive, resolution,
artwork or gameplay change.

## 1. Root cause of the G5f wall regression

**The regression is in the G5f host binary, not in the wall algorithm and not
in the row emitter.** Cross-build measurements with the same game source and
runtime:

| Host binary | Game source | Wall ms | Texrow ms | Total ms | FPS |
|---|---|---:|---:|---:|---:|
| G5e host, adapted only for G5f's row output | G5e | 106.69 | 51.79 | 166.99 | 6.0 |
| G5e host, adapted only for G5f's row output | G5f | **103.65** | **18.47** | **131.28** | **7.6** |
| G5f host | G5e | 138.95 | 68.27 | 217.43 | 4.6 |
| G5f host | G5f | 136.62 | 23.99 | 170.25 | 5.9 |
| G5f host with projection-test code omitted | G5f | **103.12** | **18.23** | **130.56** | **7.7** |
| G5f host with projection-test code omitted | G5e | **103.02** | 51.90 | 163.03 | 6.1 |

The wall instruction count is identical for the G5e and G5f game sources:
**33,377,988 S1 operations per frame**. Adding the G5f projection diagnostic
code to the host changes the C translation unit/link layout and moves the hot
interpreter (`s1_run`, for example, from `0x16210` to `0x16440` in these
builds). This changes wall time by ~30% without changing one Glon instruction.

The exact microarchitectural mechanism (I-cache/branch layout versus another
code-placement effect) is **undetermined**. `-falign-functions=64
-falign-loops=64` made it worse (153 ms), and `-fno-inline` also made it worse
(150 ms), so a one-flag alignment fix is not the answer. The actionable fact is
that host code composition is part of the measurement and must be isolated in
G5h.

The G5f row emitter itself is confirmed effective: under the same host, G5f
reduces texture-row S1 operations from **16,622,624** to **5,188,130** per
frame (68.8%) and row time from ~51.8 ms to ~18.5 ms.

## 2. Baseline (G5f commit unchanged)

Five runs of the unchanged G5f `--bench 120`, median:

| Metric | Textured | Simple |
|---|---:|---:|
| FPS | 5.9 | 6.7 |
| simulation | 2.12 ms | 2.14 ms |
| wall projection | 136.49 ms | 138.20 ms |
| floor/ceiling | 24.05 ms | 2.57 ms |
| native raster | 0.42 ms | 0.05 ms |
| presentation | 6.11 ms | 6.12 ms |
| total frame | 169.26 ms | 149.31 ms |
| managed frontier | 33,440 | 33,440 |
| GC / live | 0 / 0 | 0 / 0 |
| loader (self-test) | 54,162 | 54,162 |

Warm-up was measured separately in the new fixed-state benchmark: the first 20
frames averaged 165–174 ms wall time (median 167.22); the following 80 steady
frames had a median 135.90 ms wall, 23.80 ms texrow and 165.92 ms total. Warm-up
is therefore excluded from steady-state comparisons below.

The unchanged G5f tests passed before any G5g edit.

## 3. Wall projector cost

Diagnostic build: `build/doom-opcode`, linking the diagnostic copy
`s1_prof.c`. It counts events only and is not the production binary. Per
textured frame at the fixed 320,320,0 camera:

| Wall-stage event | Count/frame |
|---|---:|
| S1 operations | 33,377,988 |
| LIT | 13,916,076 |
| DUP | 807,652 |
| DROP | 288,962 |
| `@` memory reads | 7,060,306 |
| `!` memory writes | 3,129,774 |
| 0BRANCH | 2,160,772 |
| branches taken | 1,777,598 (82.3%) |
| HOST calls | 6,014,444 |
| HALT | 2 (two 80-column blocks) |

HOST wall breakdown: `+` 2,339,802; `=` 1,403,064; `*` 677,438; `mod`
275,638; `/` 221,112; `-` 319,706; `<` 118,434; `>` 622,208; print 800.

This is **208,612 S1 operations per ray** and **20,861 per wall candidate**
(160 rays × 10 checks). The source-level wall code contains only ten divisions
per ray plus the projection division; the much larger HOST `/` count is
evaluator/dispatch/hash work, not the ten intersection arithmetic operations.

Other textured stages:

| Stage | S1 ops/frame | HOST calls/frame | Branches/frame | Taken |
|---|---:|---:|---:|---:|
| wall | 33,377,988 | 6,014,444 | 2,160,772 | 1,777,598 |
| texrow (G5f) | 5,188,130 | 952,452 | 333,425 | 273,575 |
| texrow (G5e) | 16,622,624 | 3,152,737 | 1,086,773 | 959,960 |
| status | 534,418 | 102,192 | 35,384 | 31,610 |
| tick | 507,869 | 93,612 | 32,522 | 27,115 |

The top wall execution buckets are runtime evaluator regions at IP buckets
5632, 4352, 5376 and 9216, not the game's source text. Wall projection is
therefore dominated by evaluator work around each source-level operation, not
by the ten-wall loop count alone.

## 4. Isolated benchmarks

`bench/execution-bench` is outside the game loader. S1 workloads are assembled
directly with the frozen assembler API. Glon workloads use the normal
evaluator and prelude. Native C workloads use the same deterministic formulas
and published checksums. Native numbers are **algorithmic floors**, not a
measurement of interpreter dispatch alone.

### S1 micro-loops (median of 3 runs, 500,000 iterations)

| Loop | ns/iteration |
|---|---:|
| empty control loop | 22.5 |
| one add body | 30.9 |
| one multiply body | 30.2 |
| one divide body | 28.9 |
| two conditional branches | 43.2 |
| load/add/store body | 35.0 |

After subtracting the empty-loop control cost, the additional primitive cost
is roughly **1.6–3.1 ns per S1 operation**. This is close to native C
(0.24–0.68 ns/op) and is not where the game's 136 ms goes.

### Glon evaluator workloads (median, 60 recursive levels)

| Workload | ns/level |
|---|---:|
| add | 22,612 |
| multiply | 24,779 |
| divide | 26,859 |
| branch | 30,200 |
| memory load/store | 33,477 |
| binary tree (per call) | 35,000 |

The macro cost is three orders of magnitude above the S1 primitive cost:
function activation, lookup, block evaluation and stack traffic dominate.

### Isolated 160-ray / 10-wall workload

Five independent runs:

- Glon: 108.55, 109.10, 112.41, 117.89, 118.88 ms/frame; **median 112.41 ms**
- Native C: 0.00122–0.00133 ms/frame; **median 0.00126 ms**
- Both checksums: **93,040** (`checksums_equal=1`)

Ratio: approximately **89,000×**. The isolated Glon workload includes the ray
loop/recursion overhead but no host span parsing or 800-number printing, so it
is not expected to equal the 136 ms in-game wall stage.

### Native arithmetic floors

add 0.24 ns/op, multiply 0.25, divide 0.50, branch 0.45, memory 0.68.

## 5. Instrumentation and safety

- Production still links frozen `s1.c`.
- `make diagnostics` builds `build/doom-opcode` with the existing diagnostic
  copy `s1_prof.c` and `-DDOOM_S1_PROFILE`.
- The diagnostic copy has the same semantics; it adds event counters and a
  branch-taken counter. No primitive or frozen file changed.
- The ordinary and instrumented fixed-frame builds produce identical hashes:
  spans `7471d718`, rows `66eb2481`, status `3ce33745`.
- `--self-test`, `--projection-test`, `--stationary-test`, `--trace` and the
  frozen-S1 check all pass in both builds.
- Exact VM instruction counts are available only in this separate diagnostic
  build. The production runtime's HOST counters still reset per run and are not
  a frame-level API.

## 6. Stationary-camera experiment

Reversible mode: `--stationary` (interactive) and `--stationary-bench`.
The cache stores the last scene only while camera, player, door, Alien/tracker
and render mode are unchanged. Simulation (`tick`) and input processing
continue. A cache hit skips Glon wall/row/status re-projection and
`draw_buffer`; presentation still occurs. There is no general scene cache.

Results at 320,320,0:

| Mode | Hits/Misses | Hit rate | Full frame | Cached frame | Sim | Present | Input→update |
|---|---:|---:|---:|---:|---:|---:|---:|
| busy, 180 frames | 179/1 | 99.4% | 167.74 ms | 6.68 ms | 2.16 ms | 2.55 ms | 167.74 ms |
| 60 Hz throttled, 180 frames | 179/1 | 99.4% | 178.63 ms | 7.45 ms | 2.48 ms | 2.87 ms | 178.63 ms |

CPU use was ~83.5% in the busy loop and ~41.0% at 60 Hz (8.1 ms CPU/frame,
19.7 ms wall/frame). Cache hits reduce frame production ~96%, but **input to
visible update remains a full ~168–179 ms render** because a
turn/move/door/Alien change invalidates the view. The cache improves idle cost,
not responsiveness.

Tests confirm: first frame is a miss; an unchanged frame is a hit with
identical pixels; camera turn, door state and visual changes invalidate; a
forced uncached frame at the same state matches the cached frame; and the cache
does not alter player/status/game state. The interactive `--stationary` path
shares the same cache; its scripted equivalent was measured in the benchmark.

## 7. Loader frontier and capacity

`M[GC_LOADER_HP]` is a **bump pointer for permanent loader data**, not managed
heap and not executable code. The 64K map reserves `[40000, 54800)` = 14,800
cells; the hard limit is `R0S1_HEAP_LIMIT = 54800`, above which the M1 task
arenas begin. The loader is never swept.

Measured stages:

| Stage | Loader used | Free |
|---|---:|---:|
| after `r0_s1_init` | 771 | 14,029 |
| after prelude | 1,228 | 13,572 |
| after game source | 12,102 | 2,698 |
| after host blocks | 14,162 | 638 |
| fixed benchmark with camera block | 14,185 | 615 |

The game has **no masm**, so it adds **0 cells of executable code**: runtime
code remains 12,454 cells against the 16,128 code limit. The game's loader use
is source/constant/block/symbol data. Host-generated diagnostic blocks add
2,060 cells by themselves.

An integer table probe measured ~**1.125 loader cells per element** (128
elements = 144 cells; 256 = 272). With 222 cells free after those probes, 320
elements were refused. A conservative 160-ray × 4-integer candidate table
would need ~720 cells; a 1600-integer candidate table ~1,800 cells. Both exceed
the remaining frontier. This is why the G5f static wall-table attempt did not
fit.

The 64K size is a deliberate **[DECISION]**; the loader slice was historically
tuned by shrinking the M1 arena, not by enlarging the machine. The limit is
therefore a deliberate resource boundary with an incidentally tight current
margin, not a reason to raise `MEM_CELLS`.

## 8. Architectural alternatives

Ranked for G5h, assuming the host-layout regression is first removed from the
measurement path:

1. **C — prepared world + dynamic camera (recommended direction).** Keep the
   ten wall endpoints/materials prepared, but reduce the repeated
   camera/global/evaluator work and encode the dynamic traversal compactly.
   This addresses the measured 84% wall share. Loader capacity and evaluator
   overhead are the main risks.
2. **B — compact spatial/candidate representation.** A grid or angular
   candidate list could cut 1,600 checks toward 160–480, but a per-ray table
   does not fit the current loader margin. A compact string/byte encoding is
   possible; it adds decode instructions. A BSP is not justified for ten walls.
3. **A — existing raycaster with local arithmetic/instruction reductions.**
   Lowest risk, but the dominant cost is evaluator activation/lookup around the
   source operations, not the primitive arithmetic. Expected gain is limited
   unless the representation changes.
4. **D — selective VM extension.** S1 primitive cost is already ~1.6–3.1 ns;
   the 22–33 µs macro cost is the problem. A new primitive would need to remove
   evaluator/lookup work, which is a frozen-S1 change with low expected payoff.
   Not recommended for G5h.

Do not add a BSP merely because Doom used one: this level has ten walls.

## 9. Recommendation for G5h

1. Separate the production host from diagnostic/cache code into a distinct
   translation unit or binary so the wall measurement is not coupled to host
   code layout. Re-baseline the production G5f host at the fast ~103 ms wall
   configuration before making renderer claims.
2. Use the diagnostic opcode/HOST counters as the acceptance metric, not only
   wall-clock: wall is ~84% of frame S1 operations and is the only stage worth
   changing.
3. Prototype one compact prepared-wall representation that fits the 615–638
   cell loader margin, with a measured decode cost. Do not raise the 64K map.
4. Keep the stationary cache as an optional idle optimization only; it does not
   improve input-to-visible latency.

## 10. Tests and unresolved items

Passing:
- `make test` (texture check, self-test, projection-test, stationary-test,
  trace, frozen-S1 check).
- `make diagnostics`; `doom-opcode --self-test`, `--projection-test`,
  `--stationary-test`.
- Isolated wall checksums equal 93,040; instrumented/ordinary fixed hashes
  equal.
- Loader and GC stability: loader unchanged across repeated sessions
  (self-test `repeated session loader = PASS`); GC 0; managed frontier 33,440.

Unresolved:
- Exact microarchitectural cause of host code-layout sensitivity is
  undetermined; the effect is reproduced but not reduced to one compiler flag.
- No interactive WSLg input-latency trace was captured; the stationary benchmark
  uses the same cache path with scripted turn input.
- No renderer optimization or production performance improvement is claimed.
- The G5g host includes diagnostic/cache code and is not a performance
  deliverable; its absolute `--bench` numbers are therefore not compared as
  before/after release numbers.

## 11. Commands

```sh
cd experiments/glon-doom
make test
make diagnostics
DOOM_ROOT=../.. ./build/doom --bench-fixed 80
DOOM_ROOT=../.. ./build/doom --bench-fixed 80 simple
DOOM_ROOT=../.. ./build/doom --stationary-bench 180
DOOM_ROOT=../.. ./build/doom --stationary-bench 240 throttle
DOOM_ROOT=../.. ./build/doom-opcode --opcode-bench 5
DOOM_ROOT=../.. ./build/doom --loader-report
cd bench
./execution-bench --s1 500000 7
./execution-bench --glon 100
./execution-bench --wall 10
./execution-bench --native 5000000 5
```
