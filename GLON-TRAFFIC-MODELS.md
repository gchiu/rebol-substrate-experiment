# Glon Alpha traffic laboratory — model comparison

Companion to `GLON-TRAFFIC-IDM.md`, which is left intact. This document covers
three additional, deliberately different traffic-model families added to the
same frozen-Glon laboratory, purely as an application-substrate stress test:

> Can frozen Glon naturally express several genuinely different kinds of
> traffic dynamics, and where does it resist?

It is **not** a traffic-engineering document and does not aim for a
comprehensive simulator. Multi-lane, merging, accidents and tuple-space
agentization are explicitly out of scope for this pass (see `PART 12`/`PART
13` of the brief this responds to).

Frozen Glon Alpha (`glon-alpha-v1` = `d6064f765edca67f389864bfb8384341e1951666`)
and frozen S1 (`f90496c...`) are unchanged by this work; see "Freeze
verification" at the end.

## 0. Shared laboratory

All four models (IDM plus the three new ones) share the same physical
laboratory where it is meaningful to do so:

- 1 km periodic single-lane ring (`road = 100000` cm), 25 vehicles, fixed
  vehicle order (vehicle `i`'s leader is `i+1 mod 25`).
- Units: centimetres, tenths of a second (`dt = 0.1 s`), unless a model's
  canonical form is intrinsically discrete (NaSch — see §3).
- Every model reuses the *shape* of the existing IDM scenario (uniform
  initial flow at `v0/2`, then vehicle 0 governed to a lower speed for one
  disturbance window) rather than inventing a new one, so results are
  comparable. Where a model's own units don't map cleanly onto that scenario,
  the mapping is documented per-model and the mismatch is not hidden.
- Each new model gets its **own** Glon file, its own function names (prefixed
  `newell-`, `ovm-`, `nasch-`), and its **own** state blocks. The existing IDM
  file (`demo/shop/demos/traffic.glon`, its bare `step`/`init`/`mean-v`/...
  names, and the already-shipped Baseline/Pacing scenario) is not touched —
  it remains the control. Reason: Glon has one flat global context, so a
  second model reusing IDM's bare names would silently shadow it. Prefixing
  is the smallest fix; it is not a shared "physics interface," because the
  models' state genuinely differs (continuous cm positions with float-free
  fixed point vs. discrete cell indices vs. position *history*), and forcing
  a common interface across genuinely different state shapes would be a false
  equivalence.
- "Model" (physics law) is kept separate from "Scenario" (disturbance).
  Pacing is IDM-specific (it was built and characterized against IDM's own
  dynamics) and is not claimed to be meaningful for the other three models;
  the new models expose only Baseline / brake-disturbance.

## 1. Glon arithmetic audit (done before choosing model 2)

Frozen Glon Alpha's only native arithmetic is `+ - * /` on tagged integers
(`/` truncates toward zero, matching C); there is `=`, `< > <= >=`, `either`,
recursion, and the `masm`/`raw` escape hatch for hand-written S1 fragments
(used already for `block-at`/`block-set!`/`mk-string`). There is no `mod`
native (built from `-`, `/`, `*`: `a mod m = a - (a/m)*m`, exactly the
technique this document uses for the PRNG), no square root, no floating
point, no transcendental function of any kind.

A second, sharper constraint that matters for this task specifically: every
Glon integer is tagged as `payload*16 + tag` (`r0_s1.h`, `mk_int`). Arithmetic
natives untag, compute in a plain machine integer, then **retag by
multiplying by 16** before the result is usable as a Glon value again. That
retag step needs 4 spare bits, so a Glon-visible integer only ever has
`(cell width) - 4` usable bits — roughly ±2^59 on the 64-bit native build used
for all testing in this pass, but only **roughly ±2^27 (≈134,000,000) on a
32-bit WASM `cell`** (Emscripten's default target; `standalone/glon.c`/
`r0_s1_g1a.c` build to `wasm32`, not `wasm64`). This repository's `s1.h`
defines `cell` as `intptr_t`, so this is a real, width-dependent limit, not a
hypothetical one; it directly shapes the PRNG design below (§3.2).

### Candidates considered for "model 2" (continuous car-following, not IDM)

| candidate | canonical form needs | verdict |
|---|---|---|
| Gipps (1981) | both regimes (free-flow *and* the safe-braking regime) evaluate a runtime square root of a state-dependent expression, not a fixed constant | **rejected** — Glon has no sqrt native, and IDM's own established precedent (`GLON-TRAFFIC-IDM.md` §1) is to structure the *scenario* so the one sqrt it needed collapsed to a precomputed constant, not to implement a general sqrt. Gipps' sqrt argument depends on `v(t)` and the leader's speed at every tick, so it cannot be precomputed the same way. A hand-written Newton's-method integer sqrt would work numerically, but that is implementing a missing general capability to make one model fit, which the brief asks to avoid ("do not hack around it with a poor approximation just to tick the box" — and an iterative sqrt is not "poor" exactly, but it *is* exactly the kind of general capability the brief says to report as friction rather than quietly build). **Logged as friction, not implemented.**
| OVM, Bando et al. (1995), canonical `V(Δx) = V1 + V2·tanh(C1(Δx−lc) − C2)` | `tanh` | **rejected as stated** — no transcendental functions in Glon. |
| FVDM, Jiang/Wu/Zhu (2001), built on the same `tanh` `V(·)` | `tanh` | same rejection as above for the optimal-velocity function. |

### Model chosen: FVDM with a piecewise-linear optimal-velocity function

Jiang, Wu & Zhu's Full Velocity Difference Model's *acceleration law* itself
needs only `+ - *` — it is the optimal-velocity function `V(Δx)` inside it
that needs `tanh`. A piecewise-linear `V` is a standard, published
simplification of Bando's S-shaped curve (the qualitative shape — flat at 0
below a jam gap, flat at `vmax` above a free gap, linear ramp between — is
the same one many car-following texts use to explain OVM's mechanism before
introducing the smooth `tanh` version). Substituting it keeps the model's
defining mathematical character (an *attractor* dynamic pulling velocity
toward a gap-dependent optimal speed, plus a velocity-difference damping
term) — genuinely different from IDM's gap-and-relative-speed *deceleration*
law — while staying exactly inside `+ - * /`. This is the "strongest
published model that can be represented faithfully with the existing
substrate" the brief asks for, honestly labelled as using a simplified `V`,
not silently substituted.

No missing capability blocked implementing FVDM/piecewise-OVM or NaSch; only
Gipps and canonical (tanh) OVM were rejected. This is reported as friction
(§7), not treated as a stopping condition — the brief's stop condition
("every reasonable candidate requires a missing capability") did not trigger,
because the strongest-available candidate was found.

## 2. Model A — Newell (2002) simplified car-following

**Source.** G. F. Newell, "A simplified car-following theory: a lower order
model", Transportation Research Part B 36 (2002). The model: each vehicle's
trajectory is a time-shifted, space-shifted copy of its leader's trajectory,
subject to a free-flow speed cap:

```
x_n(t+τ) = min( x_n(t) + v_max·τ ,  x_{n-1}(t) - d )
```

where `τ` is the driver's reaction/response time and `d` is the jam spacing
(the gap at a dead stop). There is no acceleration term at all — velocity
itself is the delayed, capped quantity; this is Newell's point (a genuinely
lower-order model than IDM/OVM).

**State variables.** Position `x_n(t)` as usual, but *also* a short history
of each vehicle's own past positions, because the update needs
`x_{n-1}` from `τ` ago, not from now.

**Discretization for this tick-based lab (`dt = 0.1 s`).** `τ` is fixed at 10
ticks (1.0 s, a standard reaction-time order of magnitude). Each vehicle
keeps a `τ+1`-entry ring buffer of its own last positions (`newell-hist`, a
flat `25*(τ+1)` block, `block-at hist (i*(τ+1) + slot)`); every tick appends
the vehicle's position for *this* tick and the update reads the leader's
entry from `τ` ticks back. This is the "honestly implement the history state"
case the brief anticipates — there is no way to state Newell's law without
it, and no attempt is made to fake a memoryless approximation.

**Update rule (per vehicle, synchronous — see §5).**
```
free   = x_n(t) + v_max * τ * dt              (v_max*τ pre-scaled to cm)
follow = leader_pos_delayed - d
x_n(t+dt) = min(free, follow)                 (ring-wrapped, see below)
v_n(t+dt) = (x_n(t+dt) - x_n(t)) / dt          (derived observable, not stored state)
```
`leader_pos_delayed` is read from the leader's history ring, wrapped by
`+road` exactly like IDM's gap computation when the leader has crossed the
origin (net-position subtraction, not modular index arithmetic — same
technique IDM already established, and for the same reason: it is only
correct because vehicles never overtake).

**Parameters.** `v_max = 3000` cm/s (same as IDM's `v0`), `τ = 10` ticks,
`d = 500` cm (net jam spacing ≈ one vehicle length).

**Initial condition — corrected during implementation, not copied from IDM.**
The first attempt seeded history at IDM's own settle speed (`v0/2 = 1500`
cm/s), copying IDM's initial condition directly. That produced a large
one-tick jump at `t=0`: with this ring's spacing (`road/nveh = 4000` cm) and
the parameters above, `vmax·τ + d = 3000·1 + 500 = 3500 < 4000`, so free-flow
at `vmax` is Newell's *own* stable equilibrium here — cruising at `1500`
is not a fixed point of Newell's rule under these parameters, so the model
correctly (if abruptly) corrected toward the trajectory it considered
consistent. This is Newell's genuine response to an artificially inconsistent
seed, not a bug, but it is also not the zero-transient control the shared
scenario wants, so the initial condition was changed to `vmax` (verified
analytically: `x_n(t-τ)+vmax·τ` reduces identically to `x_n(t)` whenever the
vehicle has truly been at `vmax` for the whole `τ` window, at any tick
granularity — a linear identity, not an approximation), eliminating the
transient. See §7 for this logged as a friction-adjacent finding (not a
language issue — a modelling-consistency issue).

**Timestep / units.** cm, 0.1 s ticks, matching the shared lab.

**Synchronous?** Yes — every vehicle's `t+dt` position is computed from the
*same* tick-`t` history snapshot before any vehicle's ring buffer is updated
for `t+dt` (see §5).

**Expected qualitative behaviour.** Newell is a *kinematic* model (a
following vehicle's whole future trajectory is fixed by its leader's past
trajectory, capped by free-flow speed) with **no explicit stabilizing
feedback term** the way IDM's squared gap-ratio brake or FVDM's
velocity-difference term provide. The classical result (Newell 2002; the
model is equivalent to a simple traffic-flow "triangular fundamental
diagram" kinematic-wave model) is that disturbances propagate backward at a
constant speed related to `-d/τ` without amplifying or damping under this
minimal law — i.e. the wave should translate rather than grow or decay. That
is a concrete, falsifiable prediction this document checks against the
measured results (§8), not an assumption.

## 3. Model B — FVDM / piecewise-linear OVM

**Source.** Acceleration law: Jiang, R., Wu, Q., Zhu, Z. (2001), "Full
velocity difference model for a car-following theory", Physical Review E 64,
017101. Optimal-velocity function: simplified (piecewise-linear) in place of
Bando et al. (1995)'s `tanh` form, for the reason given in §1.

**State variables.** Position `x_n`, velocity `v_n` (both loader blocks,
matching IDM's `xs`/`vs`).

**Optimal-velocity function** (net gap `h`, i.e. already minus vehicle
length, exactly as IDM's `gap` computes it):
```
V(h) = 0                                    h <= hc
V(h) = vmax * (h - hc) / (hmax - hc)        hc < h < hmax
V(h) = vmax                                 h >= hmax
```
`hc = 200` cm (same as IDM's jam gap `s0`), `hmax = 4000` cm (chosen so the
settled uniform-flow net gap of the shared scenario, ~3500 cm, sits just
below the flat top — the ring should settle near, not exactly at, `vmax`,
mirroring how IDM's own settle behaves).

**Acceleration law (FVDM).**
```
a_n = a0 * (V(h_n) - v_n)  +  lambda * (v_lead - v_n)
```
`a0` (relaxation-rate sensitivity, 1/s) and `lambda` (velocity-difference
sensitivity, 1/s) are represented as Glon integer fractions `num/den` (the
same "scale by a plain fraction" idiom IDM already uses for `headway = 3`
meaning `3/2`), applied as `(num * (...)) / den` — no new arithmetic
capability. Two parameter sets are run (§8): a literature-typical "moderate"
set and a smaller-`a0` "OVM-instability" set, chosen from the model's own
published linear-stability criterion (roughly `a0 < 2·V'(h*)` is unstable),
**not** tuned post hoc to produce a nicer chart.

**Timestep.** `dt = 0.1 s`; `v_{n}(t+dt) = v_n(t) + a_n*dt`,
`x_n(t+dt) = x_n(t) + v_n(t+dt)*dt`, matching IDM's own integration exactly
(same `/10` scaling idiom, so the two models' numerical treatment is
comparable, not just their force laws).

**Synchronous?** Yes, same two-pass (`ovm-accels` then `ovm-update`)
structure as IDM's `accels`/`update`, for the same reason (§5).

**Expected qualitative behaviour.** At the "moderate" parameter set, FVDM
should behave qualitatively like a softer IDM — a disturbance damps out,
though the mechanism (pulling every vehicle toward a gap-implied optimal
speed) is different from IDM's explicit two-term brake. At the
"OVM-instability" set, the literature predicts the *opposite*: small
perturbations should **grow** as they propagate backward rather than damp,
the well-known OVM stop-and-go instability. Both predictions are checked
against measurement, and a negative result (no instability observed at the
chosen "unstable" set) would itself be reported, not suppressed.

## 4. Model C — Nagel–Schreckenberg (NaSch) cellular automaton

**Source.** K. Nagel, M. Schreckenberg, "A cellular automaton model for
freeway traffic", Journal de Physique I 2 (1992).

**State variables.** Discrete cell index `pos_n` (0..`ncells-1`) and discrete
integer velocity `v_n` (0..`vmax_cells`, in cells/tick) per vehicle. This is
intentionally a different representation from the continuous models: no
centimetres, no fixed-point scale-1000 fractions — a cell is the length
unit and a tick is the time unit, by construction.

**Update rules, applied in this exact published order, synchronously (all
four rules read the previous tick's *entire* state before any vehicle
commits a new position — see §5):**
```
1. Acceleration:   if v_n < vmax:  v_n = v_n + 1
2. Safety braking:  v_n = min(v_n, gap_n)         (gap_n in cells, to the car ahead)
3. Randomization:   with probability p:  v_n = max(v_n - 1, 0)
4. Movement:        pos_n = pos_n + v_n            (ring-wrapped)
```

**Mapping onto the shared 1 km / 25-vehicle lab.** A cell is 750 cm (7.5 m,
the NaSch-literature convention of one cell ≈ one vehicle length + typical
jam spacing), giving `ncells = 100000/750 = 133` (rounded down; the ring is
`133*750 = 99750` cm, 0.25% short of the nominal 1 km — documented, not
hidden: NaSch cells don't divide 100000 evenly and forcing them to would
distort the cell length). `vmax_cells = 5` (the canonical NaSch value,
representing ~135 km/h at 7.5 m/cell, 0.1 s... **the canonical NaSch tick is
1 s, not 0.1 s** — using this lab's 0.1 s tick with `vmax=5` cells/tick would
imply an unrealistic ~2700 km/h. This lab therefore gives NaSch its own tick
duration, `dt_nasch = 1 s`, documented as a deliberate, model-appropriate
deviation from the continuous models' 0.1 s — forcing NaSch onto 0.1 s ticks
to make "tick count" directly comparable across models would be the false
equivalence the brief warns against; ticks are compared as "simulated
seconds elapsed," not "tick-for-tick," in §8.

**PRNG.** See §3.2 below — this is the one place NaSch genuinely needs
something IDM/Newell/FVDM did not: a source of numbers. It is written
entirely in ordinary Glon (`nasch-rand`), seeded, deterministic, and is
*application* state, not a runtime feature.

### 4.1 Why NaSch is architecturally the odd one out

Every other model in this lab reads as "continuous state, evaluated every
tick from a formula." NaSch is "discrete state, evaluated every tick from a
small rule table with a random draw." That is the point of including it —
it tests whether Glon's block/recursion/either substrate is equally
comfortable with a rule-table cellular-automaton style as with a
closed-form differential-style law, not just whether it can do more
arithmetic.

### 4.2 PRNG: seeded linear congruential generator, written in Glon

`nasch-rand-next: func [seed] [ ... ]` implements
```
seed' = (seed * A + C) mod M
```
with `A = 101`, `C = 12345`, `M = 1048576` (2^20). These constants are not
arbitrary: they satisfy the Hull–Dobell full-period theorem for a
power-of-two modulus (`A ≡ 1 (mod 4)`: `101 mod 4 = 1`; `C` odd and
therefore coprime to `M`), so every seed in `[0, M)` visits all `M` = 1,048,576
states before repeating — comfortably more than any run in this pass draws
(25 vehicles × a few thousand ticks). `mod` is `a - (a/m)*m` (§1). The
**deciding constraint was width, not period**: the largest intermediate value
the generator ever forms is `seed * A`, at most `(M-1)*101 ≈ 105,910,475` —
under the ≈134,000,000 ceiling a 32-bit tagged Glon `cell` can hold (§1) with
about 20% headroom, so the same sequence is reproducible on a 32-bit WASM
build as on the 64-bit native build this pass actually tested against, with
no reliance on wraparound/undefined-overflow behaviour anywhere. A
textbook-standard LCG (e.g. the Numerical-Recipes `A=1664525, C=1013904223,
M=2^32`) was considered first and rejected for exactly this reason: its
`seed*A` term alone can exceed 2^61, which overflows even the 64-bit build
used here, let alone wasm32.

`nasch-rand-next` returns the new seed itself as the draw (standard LCG
convention). The slowdown test compares the draw against a fixed threshold
`(M*3)/10 = 314572` for `p = 0.3` (truncating integer division; the resulting
actual probability is `314572/1048576 ≈ 0.29999...`, off from exactly 0.3 by
less than 0.001%, documented rather than hidden).

**Application-level, not runtime.** The seed is ordinary Glon state (one
global word, `nasch-seed`), threaded explicitly through every call — there
is no host `Math.random()`, no `Date.now()`, no hidden generator, and no
Glon/S1 native involved at all; `nasch-rand-next` is pure `func`
arithmetic.

## 5. Synchronous update law (all models)

Continuous-model pattern (Newell, FVDM — copying IDM's own established
two-pass shape exactly): `MODEL-accels`/`MODEL-next-positions` (or
equivalent) walks all 25 vehicles computing the *new* value from the
*previous* tick's untouched state into a scratch array, then a second pass
commits it. No vehicle's freshly-committed state is ever read by another
vehicle's computation within the same tick. NaSch does the same thing across
all four rules: rules 1–3 compute each vehicle's *new* velocity from the
*previous* tick's positions/velocities into a scratch `nasch-vs2` block, and
only then does rule 4 (movement) commit positions using the new velocities.

## 6. Common observables

Per model, per scenario: mean/min/max speed, speed amplitude (max−min),
minimum gap (or minimum-occupancy safety metric for NaSch, in cells),
whether/how far a disturbance propagates and in which direction, wave speed
where it can be measured (Newell and NaSch — see §8), damping vs. persistence
vs. growth, a throughput proxy (mean speed × 25, cars/lab/s-equivalent), and
performance (ticks/sec, vehicle-updates/sec, wall time for a fixed-length
run, loader-heap high-water mark). For NaSch specifically: the exact seed,
same-seed reproducibility, and variability across seeds `{1, 2, 3, 12345}`.

## 7. Glon friction log

| requirement | model exposing it | handled cleanly? | workaround | category |
|---|---|---|---|---|
| delay / history state | Newell | yes | 25×(τ+1) flat ring-buffer block, plain `block-at`/`block-set!` | application inconvenience (needs an explicit buffer + index math; nothing missing) |
| square root (runtime-dependent argument) | Gipps (rejected) | **no** | none attempted — logged instead of hacked | arithmetic limitation |
| transcendental function (`tanh`/`exp`) | canonical OVM/FVDM (rejected) | **no** | piecewise-linear `V(gap)` substituted, documented | arithmetic limitation |
| modulo | NaSch (PRNG, cell wrap), FVDM/Newell (none needed) | yes | `a - (a/m)*m`, same idiom already used for gap wrap | application inconvenience |
| bounded-width integer arithmetic for a full-period PRNG | NaSch | yes, but required care | LCG constants chosen so the largest intermediate (`seed*A`) stays under the 32-bit-tagged-cell ceiling with margin, not the textbook constants | arithmetic limitation (tag width, not missing op) — see §1 |
| per-vehicle 2D-ish state (position history, two-generation velocity arrays) | Newell, NaSch | yes | flat blocks with manual stride indexing (`i*stride + slot`); no vectors/records exist, but the indexing is mechanical, not awkward | application inconvenience |
| four ordered discrete rules per tick, not a closed-form formula | NaSch | yes | ordinary sequential `either`/recursion, same shape as IDM's `accels`/`update`, just more steps | application inconvenience |
| model switching (multiple physics laws coexisting) | UI / dispatch | yes | prefixed function names per model + one dispatch table (§8/UI), no shared "physics interface" forced | application inconvenience |
| observability across 4 differently-shaped state representations | UI / dispatch | yes | each model exposes its own observable words; the status line asks the *active* model for its own numbers, nothing is generalized across models | application inconvenience |
| loader-heap budget for loading several model families in ONE running session | browser UI (standalone page, all 4 models) | **no** — capped, not solved | measured directly: common+IDM+dispatch = 48,060/54,800 cells; +newell = 50,654; +ovm = 52,910; +nasch as a *third* addition overflows it, and can leave so little headroom that even switching back to an already-loaded model then fails. The page therefore loads at most ONE non-IDM model per session, refused client-side *before* attempting `glon_load` for a second one, rather than discovered by breaking the page. Native tests are unaffected (each model's own C test loads only bootstrap.glon + that one model). | application inconvenience (a fixed, never-reclaimed loader-heap budget — not a missing capability, and not fixed by enlarging the heap, which this task forbids) |

No entry in this table was escalated to "possible language pressure": every
requirement that Glon's existing arithmetic and block/recursion primitives
could express, it expressed without strain once the right idiom (fixed-point
fractions, flat strided blocks, `a-(a/m)*m` modulo) was used — the same
idioms IDM had already established. The two arithmetic-limitation rows
(sqrt, transcendentals) are real, and are exactly why Gipps and canonical
OVM were rejected rather than faked; they did not block this task because a
faithful alternative (FVDM with a linear `V`) existed.

## 8. Results

All numbers below came from the native `s1` binary (real Glon/S1 execution,
not Python or hand-modelling) via the permanent regression tests in
`r0_s1_{newell,ovm,nasch}_tests.c`, and are pinned there exactly. IDM's own
numbers are unchanged from the pacing-experiment pass (`r0_s1_traffic_tests.c`).

### 8.1 IDM (control, unchanged)

Baseline at tick 600: mean-v 1799 cm/s, amplitude 998 cm/s. Pacing at tick
600: mean-v 1654, amplitude 834 (a throughput-for-amplitude trade, not a free
win — see test 22 in `r0_s1_traffic_tests.c` and §11.3). Unchanged by this
task.

### 8.2 Newell

- Initial condition: uniform flow at `vmax` (3000 cm/s), **not** IDM's `v0/2`
  — seeding at `v0/2` is not a fixed point of Newell's own rule under this
  ring's spacing/τ/d (verified: it produced an immediate large one-tick
  correction, Newell's genuine response to an inconsistent history, not a
  bug); seeding at `vmax` is an exact fixed point (algebraic identity), and
  reproduces zero transient for 5+ ticks before any disturbance.
- Disturbance (tick 200–215): min-v drops to 500 cm/s, reaching vehicle 24
  (the disturbed vehicle's immediate follower) by construction.
- **Qualitative behaviour: persistence, not damping or unbounded growth** —
  matching the predicted outcome in §2. By tick 300 the ring settles into a
  **stable 6-vehicle slow sub-platoon** (vehicles 0, 20–24 locked at exactly
  500 cm/s; vehicles 1–19 at exactly 3000 cm/s; mean-v 2400, min-gap 3000)
  and stays there **unchanged through tick 3000** (2700 further ticks,
  checked) — no further vehicles join the slow group and none rejoin the
  fast group. Newell's minimal law has no mechanism to dissolve a
  once-formed, internally-consistent slower platoon; the wave front
  stabilizes rather than either healing or continuing to eat more vehicles.
  Wave direction: backward (toward decreasing/leader-relative index, i.e.
  away from the braked vehicle 0 through its followers), consistent with the
  `-d/τ` sign in Newell's own theory.
- No vehicle ever overlaps over 600 ticks; two full runs are bit-for-bit
  deterministic.

### 8.3 FVDM / piecewise-linear OVM

- `V(h)` hand-verified at each piece (0 at/below `hc`=200, linear ramp,
  `vmax`=3000 at/above `hmax`=4000); one-step acceleration hand-computed
  (v=vl=1500, h=3500 → 552 cm/s²) and matched exactly.
- **Moderate parameter set** (a0=0.5/s, λ=0.5/s): the ring relaxes smoothly
  (no snap, unlike Newell) toward `V(3500)`≈2605 cm/s before the disturbance;
  after it, **the wave damps**: amplitude falls from 1857 (tick 215) to 761
  (tick 600); mean-v recovers from 2443 to 2608.
- **Low-a0 set** (a0=0.1/s, at the model's own published instability
  boundary): amplitude falls from 1808 (tick 215) to a minimum of ~675
  (tick ~400), then **rises again** to 726 by tick 600 — a real, if modest
  (not dramatic within this 600-tick window), sign of the growing
  oscillation the literature predicts for under-sensitive OVM/FVDM. Reported
  as observed, not amplified by parameter search.
- No vehicle ever overlaps under either parameter set over 600 ticks; two
  full runs are bit-for-bit deterministic.

### 8.4 NaSch

- LCG hand-verified: `rand-next(1)` = 12446 = `(1·101 + 12345) mod 2^20`,
  matched exactly.
- **Same-seed reproducibility**: two 200-tick runs from seed 1 produce an
  identical 50-value (position+velocity ×25) signature.
- **Cross-seed variability**: seeds {1, 2, 3, 12345} produce four *pairwise
  distinct* 200-tick trajectories (not just "differ from seed 1" — checked
  all six pairs).
- **Rule order** verified directly (acceleration, then the gap-safety cap,
  then randomization, then movement) on a hand-set two-vehicle case where
  the safety cap must override the acceleration rule.
- Collision exclusion (gap ≥ 0) and the velocity bound `[0, vmax]` both hold
  over 600 ticks; ring wrap (position always in `[0, ncells)`) holds over
  400 ticks with an independent seed.
- **Spontaneous jamming (a genuine, unforced finding, not tuned)**: this
  ring's actual density (25 vehicles / 133 cells) is *above* NaSch's own
  critical density for `vmax`=5 (`ncells/(vmax+1)` = 22.2 vehicles; this lab
  runs 25). Min-gap collapses to 0 by tick 19 — **before the disturbance
  window (tick 20) even starts** — from ordinary car-following dynamics
  plus the stochastic slowdown rule alone. A sub-critical `vmax`=3
  parameterization was tried as a control (critical count 33.25, comfortably
  above 25) and *also* eventually jams (by ~tick 100), just later and with
  smaller amplitude (2–3 cells/tick vs. 5) — consistent with NaSch's own
  well-documented "phantom traffic jam" phenomenon (spontaneous jam
  formation from local rules + randomness alone, the discovery the original
  1992 paper is best known for), not an implementation defect. The
  sub-critical variant is characterized here but not shipped in the browser
  page (kept out to save loader-heap budget — see §7).

### 8.5 Performance

Measured with the same `-O0` build used throughout this repository's native
test suite (no build-mode change), a fresh machine per model (bootstrap.glon
+ exactly one model file, matching each model's own C test harness), the
step form pre-parsed once and re-run via `r0_s1_run_persistent` in a loop
(the same methodology `r0_s1_traffic_bench.c` uses for IDM, so the IDM
number here is directly comparable to that existing benchmark's own
methodology, not a new one):

| model | ticks/sec | vehicle-updates/sec |
|---|---|---|
| IDM | 20 | 495 |
| Newell | 22 | 548 |
| FVDM/OVM | 25 | 636 |
| NaSch | 29 | 722 |

Relative ordering only is meaningful here (absolute numbers reflect this
`-O0`, per-tick-dispatch harness's overhead, not a tuned production loop —
this task explicitly asks not to optimize the runtime). NaSch is the
cheapest per tick (integer cellular rules, no fixed-point division chains);
IDM is the most expensive (the most fixed-point arithmetic per vehicle:
two nested `square` calls for `(v/v0)^4`, plus the `(s*/gap)^2` term).
Newell and FVDM/OVM fall in between, consistent with their simpler
per-vehicle formulas relative to IDM's.

Loader-heap high-water mark, each model loaded alone alongside
bootstrap.glon: well within the 14,800-cell budget in every case (the
tightness in §7's friction-log row is specific to loading *several* model
families into the *same* running session, which only the standalone page's
all-in-one bundle attempts).

## 9. Which model is the best first candidate for tuple-space agentization?

Assessment only — no implementation here (explicitly out of scope, §13 of
the brief this responds to).

**NaSch.** Its update is already the closest in shape to an independent-agent
model: each vehicle's next state is a small, local, rule-based decision
(read your own state + your leader's position, apply four fixed rules,
done) with no continuous-time integration and no fixed-point chain to keep
synchronized across agents. Newell needs shared, precisely-timed history
state (the τ-deep ring buffer) that a tuple-space agent model would have to
reinvent as explicit tuples anyway, which is extra design work before the
concurrency question can even be asked. IDM and FVDM/OVM are continuous
differential-style laws; agentizing them changes nothing about their
*mathematical* character, so they would not exercise anything new about
tuple-space concurrency that the existing IDM work hasn't already. NaSch's
cellular, rule-table structure is also the cheapest per tick (§8.5),
leaving the most headroom for whatever coordination overhead tuple-space
agents add. The one caution: NaSch's own seeded PRNG state (`nasch-seed`)
would need a clear, explicit ownership rule under concurrent agents (which
agent advances it, and when) to keep the same-seed-reproducibility property
this task verified (§8.4) — a real design question, but a narrow, well-scoped
one, not a blocker.

## 10. Freeze verification

Unchanged from the pacing-experiment pass: `check-frozen-s1.sh` green,
`git diff glon-alpha-v1..HEAD -- r0_s1_runtime.c r0_s1.h GLON-ALPHA-LAWS.md`
carries no new language/runtime-law changes from this work (only the
pre-existing comment-only fix already present before this task began), and
`glon-alpha-v1` still resolves to `d6064f765edca67f389864bfb8384341e1951666`.

## 11. Closeout (2026-09-24)

A final verification pass over the committed traffic work (`0092ebc`,
`546fb32`, `e386f6c`, `8fcc40a`, `b2e64b1`, `dfb3dfe`), made at `d6bb0f2`. No
traffic source file changed after `dfb3dfe`; the only change in this closeout
is test coverage (§11.4) and this section.

### 11.1 What exists

Four model families on the same 1 km, 25-vehicle ring: IDM
(`demos/traffic.glon`, the control, with the Baseline/Pacing scenario), Newell
(`demos/newell.glon`), FVDM / piecewise-linear OVM (`demos/ovm.glon`), and
Nagel–Schreckenberg (`demos/nasch.glon`, with its own seeded LCG). The model
selector lives in `demos/models-dispatch.glon`.

### 11.2 What runs where

- **Glon (on the R0/S1 runtime):** every model equation, integration step,
  state update, disturbance, the pacing governor, the NaSch PRNG, every
  observable (tick, mean/min speed, gaps), the status text, and the drawing
  itself. Each render emits a canvas command script (`L` road line, one
  `D id x y` per vehicle, positions scaled in Glon), plus the status HTML.
- **WASM:** the same frozen runtime compiled with Emscripten
  (`standalone/glon.c` + `r0_s1_g1a.c` + `s1.c` + `r0_s1_runtime.c` →
  `demo/shop/glon.wasm`). It exposes `glon_load` / `glon_event` /
  `glon_event_value`.
- **JavaScript (`traffic-host.js`):** hosting only. It loads the Glon source
  embedded in `traffic.html`, lazily loads one extra model per session
  (§7 loader-heap budget), forwards button and selector events as tokens,
  schedules `traffic-advance` events with `requestAnimationFrame`, and draws
  the `L`/`D` commands Glon emitted. It computes no speed, gap, acceleration,
  random number or other model quantity. The other four `<script>` blocks in
  `traffic.html` are Glon source, not JavaScript.

### 11.3 The two meanings of "pacing"

1. **The Pacing scenario (Glon, IDM only).** `step-pacing` runs the ordinary
   IDM `step`, then, for every tick from 200 onwards (`tick0`), caps
   vehicle 24 (the vehicle directly behind the braked vehicle 0) at
   1800 cm/s if it is faster. It does not change the IDM equations. It does
   deliberately change the simulated traffic: it is a traffic-control
   experiment, and its result is pinned (tick 600: baseline mean-v 1799 /
   amplitude 998; pacing mean-v 1654 / amplitude 834). The honest reading is
   a throughput-for-amplitude trade, not free wave damping. It is not
   offered for the other three models.
2. **Playback speed (JavaScript, all models).** The 1x / 2x / 5x selector
   changes only the wall-clock period between `traffic-advance` events
   (500 ms / speed). Each event runs a fixed amount of simulated time in
   Glon: `advance 5` (0.5 s) for the continuous models, one tick for NaSch.
   Changing the playback speed never changes any simulated number.

### 11.4 Verification

- **Native suite** (`make s1 && ./s1`, WSL, `-O0`, built from a clean
  `git archive` of `d6bb0f2` in an isolated directory): make rc 0, suite rc 0,
  703 ok, 0 FAIL ("all tests passed").
- **Traffic groups alone** (IDM, Newell, FVDM/OVM, NaSch test objects from
  that same clean build, linked into a throwaway driver): rc 0, 62 ok,
  0 failures.
- **Page bundle:** `python3 demo/shop/build-traffic.py` regenerates
  `traffic.html` byte-identical to the committed file, so the page embeds
  exactly the tested Glon sources.
- **Node/WASM** (`node demo/shop/traffic_node_test.js`, run against the
  existing local WASM described in the next item): passes, exit 0.
  This closeout adds check 11 to that test: FVDM/OVM and NaSch each get a
  fresh WASM session (the page allows one extra model per session). Each
  must give a clean tick-0 start, the road plus 25 Glon-drawn vehicles, an
  advancing tick, and a clean switch back to IDM. Before this, only IDM and
  Newell were exercised through WASM. A deliberately wrong expected tag
  makes the check fail, so it is not vacuous.
- **Which WASM was tested.** Emscripten is not available on the closeout
  machine, so no WASM was rebuilt locally. The local Node run above used an
  existing, untracked `demo/shop/glon.wasm` (built 2026-09-24 01:34). That
  build predates `d6bb0f2`, so it does not carry the current runtime, and
  this local run does not validate current HEAD's WASM. Separately, GitHub
  Actions run #45 (commit `d6bb0f2`) succeeded. That workflow checks out
  fresh, builds `glon.wasm` from that commit's runtime with Emscripten 6.0.9,
  then runs `make wasm-traffic-test`, so the Node test as committed at
  `d6bb0f2` (IDM baseline and pacing, Newell) passed against a WASM built
  from the current runtime. The new FVDM/NaSch check will first run against
  a CI-built WASM on the next push.
- **Frozen S1:** `bash check-frozen-s1.sh` → "S1 substrate frozen at
  f90496c…; all frozen files unchanged."

### 11.5 Runtime and language

The traffic work changed no runtime, language law or S1 file. The runtime
did change later, for unrelated reasons (`c1a036c`: closure-origin binding
law and CASE; `d6bb0f2`: escape-time binding law). Every native traffic test
above passes on the current runtime, and so does the CI WASM traffic test
described in §11.4.

### 11.6 Limitations

This is an architectural demonstration that useful simulator logic can be
written in and run by Glon. It is not a traffic-engineering tool and is not
comparable to mature packages such as SUMO: one lane, one ring, 25 vehicles,
fixed-point integer arithmetic, piecewise-linear OVM in place of `tanh`
(§7), no merging, lanes or routing. The standalone page can load only one
non-IDM model per session because the loader heap is never reclaimed
(§7, `traffic-host.js`). Throughput is tens of ticks per second at `-O0`
natively (§8.5).
