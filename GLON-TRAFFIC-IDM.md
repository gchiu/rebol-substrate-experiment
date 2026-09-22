# Glon Alpha traffic simulator (IDM) — friction log

The first post-freeze Glon Alpha application is a deterministic microscopic
traffic simulator: 25 vehicles following the Intelligent Driver Model (IDM) on
a single-lane ring road, driven by a host loop of synchronous `step` calls. It
runs entirely in frozen Glon Alpha — no new native, no new opcode, no floating
point, no `sqrt`.

## What was built

- `demo/shop/demos/traffic.glon` — the simulation (physics + observables + a
  canvas view + run/step/reset controls), ~228 lines of Glon.
- `r0_s1_traffic_tests.c` — headless test (14 checks) that loads the demo and
  asserts the physics invariants and determinism.

## Constraints the frozen language imposed (and how each was met)

### 1. No floating point, no `sqrt`

The IDM needs two fractional powers, `(v/v0)^4` and `(s*/s)^2`, and one
irrational constant `2*sqrt(a*b)`. All three are met with integer fixed point:

- a scale-1000 fraction is `num*1000/den`;
- `square(f) = f*f/1000` stays in scale 1000, so `(v/v0)^4` is
  `square (square (frac v v0))` and `(s*/gap)^2` is `square (frac ss gap)`;
- the constant is chosen to be integer by construction: `b = a = 100 cm/s^2`,
  so `K = 2*sqrt(a*b) = 200` is precomputed, not computed.

The acceleration is then `(1000 - rv4 - rs2)/10` (since `amax = 100 cm/s^2`
cancels the scale-1000 denominator to `/10`). Hand-checked spot values are
asserted in the test (e.g. `idm-accel 1500 1500 3500` → `44`).

### 2. Integer division truncates toward zero

`/` is C integer division. Negative accelerations (braking) are truncated, not
floored, but the error is bounded by one unit of the scale-1000 fraction and is
irrelevant at the observed magnitudes; the test pins the exact truncated values.

### 3. Ring wrap must not use index arithmetic

The leader of vehicle `n-1` is index `0`, but after enough motion a *different*
vehicle crosses the origin while it is still a leader. The original index-based
wrap (`either = ln 0 [...][...]`) was wrong and let the last gap go negative
(a silent collision). The fix wraps on the **sign** of the raw separation: a
negative `leader_pos - own_pos` means the leader has crossed the origin, so add
`road`. No overtaking keeps every gap `< road`, so one wrap is always correct.

### 4. Return-stack depth is bounded (~85 nested closures)

The frozen return stack overflows around 85 nested closures (measured). The
simulation therefore cannot `run 600` as one Glon recursion. Instead the host
drives time: `step` performs one synchronous update, and the host (C test loop,
or the browser `advance` control) calls it repeatedly. The demo's `advance`
recurrence is bounded to a 25-tick batch, well under the guard. This also keeps
the per-vehicle passes (`accels`, `update`) shallow (25 deep).

### 5. No `and`/`or`, no `while`, no `mod`

- The disturbance window `tick0 <= t < tick1` is a nested `either` chain.
- Iteration over the 25 vehicles is explicit recursion (as everywhere in Glon).
- No modulo was needed: the leader wrap uses `either = + i 1 n [0][+ i 1]`.

### 6. Loader heap is at capacity — a fifth launcher demo does not fit

The loader heap is `[40000, 54800)` = 14800 cells. The bootstrap plus the four
existing demos (shop, guide, merchant-flow, tuple-space) already reach ~54714
cells, leaving ~86. The traffic demo needs ~2600 more cells, so registering it
in the shared launcher exhausts the heap (verified: `glon_route('traffic')`
fails after loading all demos).

Because the task forbids heap/layout changes, the traffic simulator ships as a
**headless demo with its own test**, not as a fifth launcher page. Its view and
controls are defined and verified headlessly (`render-traffic` emits the road
line + 25 vehicle dots into the `G1_VIS` buffer), but are not wired into the
launcher menu.

## What was *not* changed

No language change, no native, no opcode, no GC/heap/layout change. The frozen
S1 substrate is untouched (`check-frozen-s1.sh` green). All 581 native tests
pass and the WASM/Node launcher test still passes.

## Verification

- `make s1 && ./s1` — 581 `ok`, 0 fail (13 new traffic checks + 1 canvas check).
- `./check-frozen-s1.sh` — substrate unchanged at `f90496c…`.
- `make wasm-g1a-test` — launcher + 4 demos still green.
