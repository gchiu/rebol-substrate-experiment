# G5s — Directional Alien sprite integration

Date: 2026-11-14  
Repository: `https://github.com/gchiu/rebol-substrate-experiment`  
Worktree: `/tmp/opencode/glon-avp-g5s`  
Branch: `glon-avp-g5s`

## Integration commit and ancestry

- Integration commit: `5907936` (`Integrate G5s directional Alien sprites`)
- Asset-quality gate: `7ff8573` (`Document G5s directional Alien asset quality`)
- Common G5q Alien conversion ancestor: `a131440`
- G5r pulse-rifle base inherited unchanged: `7183ae6`
- G5p Alien orientation report was cherry-picked as `85ee74b`; **no merge was
  performed**.

Verified ancestry: G5p was `e25db` on top of `a131440`; G5r was `7183ae6` on top
of the same `a131440`. The G5p-only difference was
`G5Q-ORIENTATION-REPORT.md`; G5r contained the 16-file pulse-rifle work. G5s
started at G5r and cherry-picked the G5p report, preserving both lines.

## Selected source poses

Source pack:
`/mnt/c/Users/anon_/Downloads/glon-alien-g5s-directional-reference.zip`

Extracted source:
`experiments/glon-doom/assets/alien/directional/glon-alien-g5s-pack/`

| Role | Sheet | Source rect | Effective source | Output | Scale |
|---|---|---:|---:|---:|---:|
| Frontal idle | A | 48, 8, 181, 196 | 133×188 | 137×192 | 1.00× |
| Frontal approach A | B | 911, 11, 1047, 234 | 136×223 | 140×227 | 1.00× |
| Frontal approach B | B | 1234, 13, 1365, 230 | 131×217 | 135×221 | 1.00× |
| Three-quarter approach | B | 287, 255, 504, 469 | 217×214 | 221×218 | 1.00× |
| Side movement | B | 288, 501, 542, 690 | 254×189 | 258×193 | 1.00× |
| Rear movement | B | 179, 732, 318, 1000 | 139×268 | 143×272 | 1.00× |
| Frontal attack | B | 1090, 737, 1299, 986 | 209×249 | 213×253 | 1.00× |

The two frontal approach poses are genuinely different: bottom/centre-aligned
silhouette IoU is 0.643 and mean overlapping RGB difference is 26.5. The 188–268
pixel heights are the real source resolution, not the 1536×1024 sheet size.

## Extraction and pivot methodology

`tools/convert_g5s_alien.py`:

1. Segments alpha-connected components at alpha >64.
2. Finds the selected component by its measured source rectangle.
3. Keeps only that component plus a one-pixel antialias fringe. This matters: the
   side pose's rectangle overlaps the next figure's rectangle by about 5 px.
   Rectangular cropping alone would have included an adjacent sliver.
4. Clears alpha below 40, removing faint background noise without dark rectangles.
5. Measures the foot baseline from the central 50% of the figure and the horizontal
   foot pivot from the bottom eight rows. Tail extremities do not set the pivot.
6. Exports native pixels with no resampling and writes source rectangle, effective
   size, output size, pivot and anchor metadata into `alien_sprites_g5s.h`.

At draw time every pose's effective height maps to 1.5 world units. The texture
height includes transparent/antialias margin, and the foot anchor is offset so the
ground baseline stays put. Per-frame pivot metadata and width/height prevent the
wide side/rear silhouettes from shifting the creature body away from its world
position.

## Direction-selection logic

The renderer uses the Alien's existing `ax/ay` positions and the camera basis:

- World movement is smoothed with `v = 0.78·v + 0.22·delta` to avoid one-axis
  jitter when the AI steps on X and Y in successive ticks.
- Movement away from the player (`fdot > 0.15`) selects rear.
- Purely lateral movement, or a camera-relative bearing
  `|tside|/|tdepth| > 0.48`, selects side.
- Bearing above `0.28` selects three-quarter.
- Otherwise the frontal approach is used.
- Attack state is always the frontal attack pose.
- A direction must persist for two frames before changing. Attack bypasses the
  hold.
- Side/three-quarter sprites are horizontally mirrored when the Alien crosses in
  the opposite direction. No side image is rotated to fake a frontal view.
- Frontal approach alternates the two distinct frontal poses every 8 simulation
  ticks. Three-quarter, side and rear each have only one pose.

AI state, speed, collision, health, attack timing and the Glon VM are unchanged.

## Close-up resolution gate

No pose was upscaled during extraction. At 960×600 the existing projection makes
a 1.5-world-unit creature approximately 738 px tall at `tdepth=96` and 277 px at
`tdepth=256`. A 188–268 px source is therefore displayed at roughly 600–740 px
near attack range: about **2.1–3.9× magnification** depending on the exact
capture distance.

The close-up images confirm the expected softness/pixelation. **The resolution
acceptance gate fails.** A sharp close-up requires individually rendered
768–1024 px pose sprites. No sharpening, generative detail or upscaling was used.

## Visual verification

960×600 directional captures:

- Head-on long: `/tmp/g5s-dir/01-headon-long.png`
- Head-on medium: `/tmp/g5s-dir/02-headon-medium.png`
- Head-on medium, next gait pose: `/tmp/g5s-dir/02b-headon-medium-step2.png`
- Head-on close: `/tmp/g5s-dir/03-headon-close.png`
- Diagonal/three-quarter: `/tmp/g5s-dir/04-diagonal.png`
- Crossing/side: `/tmp/g5s-dir/05-crossing.png`
- Rear diagnostic: `/tmp/g5s-dir/06-rear-diagnostic.png`
- Frontal attack with pulse rifle: `/tmp/g5s-dir/07-headon-attack.png`

1920×1080 captures use the same names under `/tmp/g5s-dir-max/`.

Other evidence:

- Side-by-side G5q/G5s approach: `/tmp/g5s-vs-g5q.png`
- Head-on sequence: `/tmp/g5s-headon-sequence.png`
- Directional contact sheet: `/tmp/g5s-directional-contact.png`
- Extracted alpha on checker: `/tmp/g5s-extracted-checker.png`
- Extracted alpha on dark/light: `/tmp/g5s-extracted-dark.png`,
  `/tmp/g5s-extracted-light.png`
- Full tour: `/tmp/g5s-tour/11-alien-tracker.png`,
  `/tmp/g5s-tour/15-alien-attack.png`
- Previous side-shuffling G5q evidence: `/tmp/g5q-final/11-alien-tracker.png`,
  `/tmp/g5q-final/15-alien-attack.png`

Inspection notes:

- The head-on sequence shows a frontal silhouette at long, medium and close range,
  with two different frontal leg positions. There is still foot sliding because the
  two poses are a minimal gait, not a matched motion cycle.
- The three-quarter capture shows the Alien approaching diagonally with the
  three-quarter pose facing the player.
- The side capture is an edge-of-view crossing; the AI always closes on the
  player, so a side-on creature is necessarily off the view centre. Only the
  selected edge portion is visible in that still.
- The rear capture is explicitly a diagnostic still. The current AI has no retreat
  state, so rear movement cannot occur in normal gameplay yet. Rear selection is
  covered by the unit test.
- Alpha edges are clean on dark and light backgrounds; the earlier adjacent-tail
  sliver is gone.
- The rifle remains drawn and the live `24` round count remains visible.
- Depth testing preserves doorway/wall occlusion; no new wall intersection or
  sideways translation was observed.

`--gl-directional <dir> [w h] objects` reproduces the directional captures.
`06-rear-diagnostic` is generated by advancing a local movement history, not by
changing the game AI.

## Performance and tests

Configuration: WSLg, D3D12, AMD Radeon, 960×600, 120 frames.

| Run | FPS | Sim | Submit | Loader | Heap |
|---|---:|---:|---:|---:|---:|
| G5s, no object mode | 95.3 | 4.56 ms | 0.63 ms | 53,337 | 33,440 |
| G5s, objects | 54.9 | 11.61 ms | 0.83 ms | 54,201 | 33,600 |
| G5r baseline, no objects | 93.7 | 5.75 ms | 0.62 ms | 53,337 | 33,440 |
| G5r baseline, objects | 48.5 | 15.12 ms | 0.71 ms | 54,201 | 33,600 |

Executable: **6,074,896 bytes**. Generated Alien header:
**3,631,370 bytes**. Textures are uploaded once at initialisation; there are no
per-frame uploads or CPU sprite conversions.

`make test` passes, including:

- new `tests/check_g5s_alien.py` alpha/crop/header checks,
- existing Alien, texture, projection, input, tracker, resize and object tests,
- directional assertions for frontal, three-quarter, side, rear and attack,
- `check-frozen-s1.sh`: frozen at
  `f90496c26dc45c7a387d8fc8639cd2781507d3d2`.

## Acceptance status

**Directional acceptance: PASS.** A head-on approach now displays frontal
artwork with two distinct frontal gait poses instead of the G5q side shuffle.

**Resolution acceptance: FAIL.** The source creature heights are only 188–268 px;
at close/attack range they are magnified roughly 2.1–3.9× and remain visibly
blurry. This is an artwork dependency, not a renderer or VM problem.

## Remaining artwork limitations

1. Per-pose 768–1024 px frontal, three-quarter, side, rear and attack renders are
   required for a sharp close-up.
2. Only two frontal gait poses exist; three-quarter, side and rear have one pose
   each and therefore no true locomotion cycle.
3. Side artwork is right-facing only; left crossing uses a horizontal mirror.
4. The current AI does not retreat, so rear artwork is wired and tested but not
   exercised in normal play.
5. Generated poses vary slightly in hand/tail anatomy; no turn, hit or death
   sequences were added.
