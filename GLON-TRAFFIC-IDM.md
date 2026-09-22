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

## Empirical characterization (what the Jaffas actually did)

Exact scenario (unchanged from the build):

- ring circumference `road = 100000 cm` = 1000 m;
- vehicle length `L = 500 cm` = 5 m;
- `nveh = 25` vehicles, so spacing = 40 m, initial gap = 35 m;
- initial positions `x[i] = i * 4000 cm` (even spacing), initial speed
  `v0/2 = 1500 cm/s` = 15 m/s uniform;
- timestep `dt = 0.1 s` (one tick advances `v` by `a/10` and `x` by `v/10` —
  explicit Euler);
- IDM: `v0 = 3000 cm/s` (30 m/s), `T = 1.5 s`, `s0 = 200 cm` (2 m),
  `a = b = 100 cm/s^2`, `K = 2*sqrt(a*b) = 200`, exponent `4`;
- fixed-point scale 1000 for the two fractional terms;
- disturbance: vehicle 0 is speed-capped at `brakespeed = 500 cm/s` (5 m/s)
  from tick 200 to 215 (t = 20.0..21.5 s), then normal IDM resumes.

Initial density = 25 veh/km. The IDM equilibrium spacing at `v0` is
`s0 + v0*T + L = 52 m`, i.e. a critical density of ~19.2 veh/km, so 25 veh/km
is **above critical** — the ring is congested from the start (the uniform
pre-disturbance speed settles at ~1880 cm/s = 18.8 m/s, well below `v0`).

Observed dynamics (measured from the simulator, not IDM theory):

- Before the brake (t ≈ 20 s) the ring is uniform: min = max = mean = 1880 cm/s,
  gap 3500 cm.
- The brake (vehicle 0 → 500 cm/s for 1.5 s) produces a clear deceleration wave.
  Minimum speed over the whole run drops to **500 cm/s** (the capped vehicle),
  minimum gap to **1750 cm** at t ≈ 27 s.
- The low-speed region travels **backward** through traffic (opposite to the
  travel direction): vehicle 24 (the follower of vehicle 0) is reached first,
  then 23, 22, … 1. Using "first speed below 1800 cm/s after t = 200 ticks" as a
  repeatable threshold, a linear fit over the 24 follower vehicles gives a wave
  speed of ~**23 m/s backward (~82 km/h)** (endpoint estimate 24 m/s).
- The **amplitude damps** as the wave travels: the follower just behind the
  braked vehicle reaches a minimum speed of 827 cm/s, the last vehicle reached
  only 1447 cm/s (i.e. the trough shallows monotonically with distance).
- The system does **not** return to uniform flow. It settles into a persistent,
  small-amplitude stop-and-go oscillation: steady-state min 1889 / max 2010 /
  mean 1953 cm/s (amplitude ~1.2 m/s), minimum gap 3131 cm, with a repeat period
  of roughly 120 s, stable out to the full 3600 s run. So the outcome is
  **sustained small-amplitude stop-and-go**, not a single damped pulse.

The scenario is therefore already in the interesting (congested, unstable)
regime; no density adjustment was needed, so none was made.

### Performance (load-once, no re-parse)

`r0_s1_traffic_bench.c` loads the bootstrap + demo once, runs `init` once, and
then executes a pre-parsed `advance 25` block repeatedly (the batch stays under
the return-stack guard and amortises host entry). Separating load / init /
simulation:

- source load ≈ 13–29 ms (one-off);
- init ≈ 1–4 ms (one-off);
- simulation (shipped `-O0` runtime): ~42–54 ms/tick ≈ **~460–550 vehicle
  updates/s**, ≈ **1.9 simulated-s/wall-s**;
- simulation (directly compiled `-O2` runtime): ~**16.4 ms/tick ≈ ~1500 vehicle
  updates/s ≈ 6.1 simulated-s/wall-s**.

A 3000-tick run = 75000 vehicle updates; a 36000-tick run = 900000 vehicle
updates (≈ 10.2 min wall at `-O0`, ≈ 3.9 min at `-O2`).

### Why the earlier 37.7 ms/tick number differs

The original `eval_int` figure (37.7 ms/tick) re-parsed `[ step ]` from source on
every tick. Re-measured with the parse amortised (a pre-parsed block executed
repeatedly), the re-parse cost is only ~1.6 ms of ~16 ms at `-O2` (and ~0 at
`-O0`), i.e. the re-parse is **not** the dominant cost — the S1 interpretation
of the physics is. The dominant factor in the old number is the `-O0` compiler
setting of the shipped runtime object: the same simulation runs ~2.6× faster
when `r0_s1_runtime.c` is compiled `-O2` (a build flag, not a language change).

## Issue classification

| Issue | Class |
|---|---|
| Ring wrap by vehicle index produced negative gaps | application bug (fixed) |
| No float/`sqrt` → fixed-point scale-1000 | application/library inconvenience |
| Integer `/` truncates toward zero | application/library inconvenience |
| No `and`/`or`/`while`/`mod` | application/library inconvenience |
| Return-stack depth ~85 → host-driven stepping | application/library inconvenience |
| Loader heap full (5th launcher demo does not fit) | host/packaging limitation (NOT a correctness issue in frozen Alpha) |
| Per-tick cost dominated by `-O0` interpreter | possible future language pressure (performance), not a blocker |

None of these threatens the correctness of frozen Alpha: the language is
untouched, and every item was met entirely in application space.
