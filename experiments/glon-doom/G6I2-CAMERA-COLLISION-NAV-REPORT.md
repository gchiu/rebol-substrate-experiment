# G6i.2 — camera, player collision and scuttler navigation repair

Branch: `glon-avp-g6i2`, based on G6i.1 `abe524e`  
Status: **implemented, packaged and verified; unaccepted pending physical Windows testing**
Source commit: `98c69b1a945e847f153facc9f5f18ff7d630a079`
Package/evidence commit: `e9dd7428fd821d16eb853ec02b50a045165ca622`  
Windows executable: `dist/windows/doom.exe`  
SHA-256: `6070d95943d2c96df7b3f9091dbdca0ba965a169b7ada77571c8eebbd1f65404`  
Launcher: `dist/windows/run-g6i2.bat`

G6h.2, G6i and G6i.1 were preserved. No merge, push, tag, release or deploy.
Frozen S1 remains unchanged
(`f90496c26dc45c7a387d8fc8639cd2781507d3d2`).

## 1. Camera pitch diagnosis and repair

The G6i.1 view applied yaw first and then pitched around **world X**:

```c
glRotatef(90 - game_angle, 0, 1, 0);
glRotatef(-look_pitch, 1, 0, 0);
```

That ordering leaves the pitch axis in world space, so once the player is
yawed the "down" rotation also rolls the view. The defect was not in the pitch
value; it was in the camera basis/order.

`gl_view` now constructs an explicit right-handed orthonormal camera basis:

- forward from game yaw, then rotated down about the camera right axis;
- right remains horizontal (`r.y == 0`), so there is no roll;
- up is the orthogonal complement;
- the matrix rows are `right`, `up`, `-forward`, followed by the eye
  translation;
- pitch remains clamped to 0..75° and the existing smooth return/reset logic
  is unchanged;
- the weapon is still drawn with its own screen-space projection, so it does
  not tilt with the world; the reticle stays screen-centred.

`--camera-test` checks forward/right/up normalization, pairwise orthogonality,
handedness (`right × up = -forward`), zero roll, and the intended vertical
components at yaws 0/64/128/192/255 and pitches 0/30/60/75. Native Windows
framebuffer captures exist for 0°, 30°, 60° and 75°, plus a scuttler at the
player's feet.

## 2. Player collision and escape

The collision shape is still a circle against walls, doors and the existing
object AABBs; X and Y are resolved sequentially, which already provides sliding
when one axis is valid. The changes in this milestone are deliberately small:

- diagonal movement is normalized to the same speed as cardinal movement
  (45/64 factor when both forward/back and strafe are active); cardinal speed
  remains 8 world units per tick;
- the existing axis-separated checks are retained so a blocked forward axis
  does not prevent a valid sideways/backward axis;
- a blocked jump does not disable subsequent movement;
- already-overlapping low enemies still use the existing outward-escape rule.

The physical defect is covered by deterministic `--g6i2-gameplay-test`
scenarios: diagonal approach and slide along a wall, strafe along a vertical
wall, retreat from a cabinet, escape a corner, jump adjacent to a wall, and 64
alternating movement inputs near furniture. Every final position is checked
with the real `doom/check-all` geometry probe at player radius and must be
non-penetrating and mobile.

## 3. Scuttler obstacle navigation

The old pursuit moved only along X and then Y toward the player; when both axes
were blocked by a barrel/cabinet it simply stopped. G6i.2 adds a compact local
steering step in the existing pursuit state:

1. continue any persisted avoidance direction for up to 12 ticks;
2. otherwise try the existing direct axis pursuit;
3. if direct movement made no progress, test left and then right perpendicular
   headings against the real wall/door/object collision;
4. persist the first valid heading briefly, then resume direct pursuit;
5. if nothing is valid, remain stationary rather than pushing through geometry.

No new top-level Glon variables/functions were added: the existing unused
`e1dx/e1dy/e1dep/e2sx/e2sy` cells carry the local steering state, so the frozen
memory map, VM capacity and loader layout are unchanged. The timer prevents
left/right chatter. Direct pursuit is retried after the timer, so a cleared
route resumes direct motion.

Tests cover clear-corridor pursuit, barrel and cabinet obstacles, three
scuttlers, player position changes, corner progress, no rapid oscillation, and a
closed door. All scuttler final positions are checked against the real
collision geometry. Closed doors remain solid; a scuttler cannot cross one.
Adult behaviour is untouched.

Remaining limitation: this is a local steering solution, not a general
pathfinding graph. The tested corner/dead-end scenarios recover, but highly
concave future geometry could require a waypoint graph.

## 4. Startup C-key regression

The G6i.1 title loop only entered the title/credits input branch for
`G6I_TITLE`. After C set `G6I_CREDITS`, the next iteration fell through into the
gameplay path, so C appeared to start the game.

The loop now handles **both** `G6I_TITLE` and `G6I_CREDITS`. A dedicated
`g6i_title_event` path uses SDL scancode **and** keycode, ignores key repeats,
and handles:

- Enter on title → gameplay;
- C on title → credits;
- Escape on credits → title;
- Escape on title → quit;
- Enter and gameplay keys on credits → ignored.

`--title-test` sends actual SDL keyboard events through the title event queue
for C and through the title event path for the remaining transitions. It checks
that C leaves the simulation paused, that W and Enter do not start gameplay from
credits, that Escape returns to title, and that title Enter starts. Native
Windows screenshots show title before C, credits after C, title after Escape,
and gameplay after Enter at all four target resolutions.

## 5. Tests and native verification

- Linux `make test`: **PASSED**, including frozen-S1 verification.
- Native Windows:
  - title at 640×400, 800×600, 960×600 and 1280×720: **PASSED**
  - camera basis test and 0/30/60/75° framebuffer captures: **PASSED**
  - `--g6i2-gameplay-test` collision/navigation suite: **PASSED**
  - existing self/projection/input/look/tracker/resize/stationary/entrance/
    collision/multi/tracker-multi/scuttler-GL/G6h.2-GL/jump/alien-collision/
    combat/door/corpse/muzzle/flat/raw/trace/audio/sfx/audio-e2e: **PASSED**
  - `--bench 60`: **PASSED**
- Collision scenarios explicitly verify final positions against the actual
  geometry; enemy scenarios explicitly check no clipping and bounded progress.
- No enemy ticks occur before gameplay, and C/Escape return preserves the title
  state.

## 6. Memory and performance observations

- VM: 65,536 cells / 524,288 B; loader reserved 25,000, occupied **18,374**,
  free **6,626**; globals **277 / 512**; managed heap high-water **34,240**.
- Title-state live Windows process: working **96,190,464 B**, peak **98,316,288
  B**, private committed **113,688,576 B** (current run; environment-specific).
- Linux `--bench 60`: `sim=9.33 ms`, `frame=157.42 ms`.
- Native Windows `--bench 60`: `sim=9.53 ms`, `frame=163.66 ms`.

These are current measurements; no improvement claim is made against earlier
builds. No VM capacity or arena layout changed. Navigation performs no
per-frame dynamic allocation.

## 7. Evidence and remaining limitations

Evidence: `experiments/glon-doom/evidence/g6i2/` and
`dist/windows/evidence/g6i2/`.

- Camera captures: `camera/camera-00.png`, `camera-30.png`, `camera-60.png`,
  `camera-75.png`, `camera-feet.png`.
- Title/C-key captures under `title-640x400` … `title-1280x720`
  (`01-title`, `02-credits`, `03-title-after-esc`, `04-gameplay`).
- Collision and navigation scripts/results in
  `logs/windows-g6i2-gameplay-test.log`; scene captures in `nav/`.

G6i.2 remains **unaccepted** pending physical Windows playtesting. Scuttler
damage attacks and distinctive movement/proximity sounds remain outstanding;
no new enemies, acid, player damage, weapons, audio assets or asset
optimizations were added. The CPU renderer retains its legacy 2D view.
