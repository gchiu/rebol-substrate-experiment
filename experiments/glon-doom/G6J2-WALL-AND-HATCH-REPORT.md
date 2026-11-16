# G6j.2 — cocoon wall clearance and empty-shell hatched state

Branch: `glon-avp-g6j2`, based on G6j.1 `9879143`
Status: **implemented, packaged and verified; unaccepted pending physical Windows testing**
Source commit: `cb508d2b9e73eb395f1e59b609a24bd9e561ae31`
Windows executable: `dist/windows/doom.exe`, **8,183,296 bytes**
SHA-256: `cd19456c200e9a90baac0bc3efcf53bb56ebf9e09755bde38989f5747b7ab80b`
Launcher: `dist/windows/run-g6j2.bat`

G6j.1 and all earlier builds are preserved. No merge, push, tag, release or
deploy. Frozen S1 remains unchanged
(`f90496c26dc45c7a387d8fc8639cd2781507d3d2`).

## 1. Wall-occlusion root cause and correction

The affected cocoon was **c1**, not c2 or c0. Its original centre was
`(620,200)`; the FLOW A / Engineering Division north wall plane is at
approximately `y=204`. The centre therefore sat **4 world units inside the
wall**, and depth testing correctly removed most of the billboard. c2
`(700,500)` and c0 `(480,500)` were unaffected.

Diagnostic evidence:

- original c1 from player `(620,400)`, angle `192`: no cocoon visible;
- the same view with depth testing disabled for c1 only: complete cocoon
  silhouette drawn over the wall, proving wall/depth intersection rather than
  alpha loss;
- the HATCHED atlas alpha is complete.

The diagnostic depth-disabled path was removed after diagnosis; it is **not**
retained in the release candidate.

Correction: c1 was moved **50 units south** to `(620,250)`. Its billboard
half-width is 30.4 and the closest corner is `y=219.6`, giving 15.6 units of
clearance from the wall at every facing angle. The automated clearance test
checks all 256 billboard angles. A 100-unit move was not required: the 50-unit
position shows the full cocoon from the test views and passes clearance.

Changed c1 references:

- Glon activation/LOS/consider coordinates `620 200` -> `620 250`;
- host billboard draw position `cy[3]={500,200,500}` -> `{500,250,500}`;
- stale c1 comment updated.

c0, c2, wall geometry, camera, depth testing and cocoon lifecycle are unchanged.
The c1 scuttler spawn remains `(620,260)` and is still valid; it is now 10 units
south of the cocoon base.

## 2. Incorrect HATCHED appearance

The state-to-frame mapping was correct: state 3 selected `COCOON_HATCHED`. The
defect was the extracted **HATCHED asset**: it was a partial, off-centre crop of
the left side of the supplied HATCHED key-state panel, omitting the right half
of the shell. At game scale the resulting curved fragment read as a standing
alien head/neck with a long appendage.

Correction:

- re-extracted the complete HATCHED key-state panel from `assets/cocoons.png`;
- removed the detached outer drip and normalized to 128x128 bottom-centre;
- darkened the natural cavity so it reads as a hollow interior while retaining
  the irregular petal/shell rim;
- shortened the HATCHED billboard to height `0.82`, width `0.95`, with the base
  still anchored at floor level.

No synthetic shell geometry or new atlas frame was invented. DORMANT/ALERTED,
HATCHING and the G6j.1 DESTROYED/collapse artwork and timing are unchanged. All
three cocoons use the same corrected HATCHED frame.

## 3. Preserved behaviour

- Independent cocoon records and per-cocoon timers.
- 8-second activation countdown and 48-tick opening animation.
- Scuttler release, state, health and spawn coordinates.
- Two-hit destruction and first-hit upright damage tint.
- G6j.1 30-tick collapse to low floor-level remains.
- Correct depth testing and wall occlusion.
- Frozen S1 and existing Glon runtime invariants.

No gameplay, combat, timing, scuttler navigation, level geometry, camera or VM
layout changes were made.

## 4. Tests

Linux `make test`: **PASSED**, including projection, cocoon lifecycle,
wall-clearance checks, visual state capture commands and frozen S1.

Native Windows: cocoon lifecycle, wall occlusion, state captures, title,
projection, input/look/pitch/camera, G6i.2 gameplay, tracker/multi-tracker,
resize/stationary/entrance, collision probes, multi-alien, jump, alien
collision, combat, door sync, corpse stability, muzzle anchor, flat/objects,
raw self-test, trace, audio/SFX/e2e, GL scuttler/G6h2 and title tests:
**PASSED**. Projection was run from the game directory with `DOOM_ROOT` set so
its relative `tests/detail.glon` and repo assets both resolve.

Focused visual coverage in `evidence/g6j2/`:

- `cocoon/dormant.png` — intact dormant c2;
- `cocoon/opening.png` — opening c2;
- `cocoon/hatched-front.png` — hatched c2 frontal;
- `cocoon/hatched-oblique.png` — hatched c2 oblique;
- `cocoon/hatched-c0.png`, `cocoon/hatched-c1.png` — corrected shell at the
  other two locations;
- `cocoon/destroyed.png`, `cocoon/collapsed.png` — two-hit destruction and
  settled low remains;
- `occlusion/before-wall.png`, `occlusion/before-nodepth.png` — original c1
  occluded, and the depth-disabled diagnostic proving wall intersection;
- `occlusion/west.png`, `occlusion/side.png` — corrected c1 from two native
  Windows views;
- `hatched-frame-before-after.png` — extracted frame comparison.

## 5. Measurements

- Loader: 25,000 reserved, **20,070 used**, 4,930 free.
- Globals: **270 / 512**; managed-heap high **34,272**.
- Native Windows live title: working **93,224,960 B**, peak **93,650,944 B**,
  private **115,531,776 B**.
- Linux `--bench 60`: `sim=11.56 ms`, `frame=173.95 ms`.
- Native Windows `--bench 60`: `sim=11.53 ms`, `frame=206.37 ms`.

## 6. Exact source files changed

- `experiments/glon-doom/doom.glon` — c1 activation/LOS/target coordinates and
  comment.
- `experiments/glon-doom/doom-host.c` — c1 draw position, HATCHED dimensions,
  wall-clearance and empty-shell assertions, focused occlusion/state capture
  commands.
- `experiments/glon-doom/assets/cocoons/hatched.png` — corrected complete
  HATCHED shell.
- `experiments/glon-doom/cocoon_sprites.h` — regenerated HATCHED ARGB frame.
- `experiments/glon-doom/Makefile` — final Linux regression runs the wall
  clearance and visual-state commands.

## 7. Limitations

- G6j.2 remains **unaccepted** pending physical Windows testing.
- The HATCHED shell is derived from the supplied contact-sheet artwork; minor
  floor/debris and soft extraction edges remain.
- Cocoons remain non-solid player-collision volumes.
- c1's original spawn `(620,260)` is retained; after moving the cocoon to
  `(620,250)` it is 10 units south of the cocoon base.
- Scuttler damage attacks and distinctive movement/proximity sounds remain
  future work.
