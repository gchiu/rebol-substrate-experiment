# G5x — High-Resolution Blast Door

**Status: BLOCKED ON ARTWORK. No high-resolution door texture exists in the
repository, and the close-up acceptance test fails with the existing 64×64
source. No fake upscale or substituted door art was committed. No G5x renderer
or geometry changes were made.**

**Branch/worktree:** `glon-avp-g5x` at `/tmp/opencode/glon-avp-g5x`, based on
G5w HEAD `53044b610fff2eb4eb507c1afba181701a2f2045`.

**Starting implementation:** G5w `d9680384dd2a642f11b7d1f6509919ec5a22f566`.
**G5x implementation:** none (blocked before door artwork-dependent changes).
**G5x report commit:** this commit; see branch HEAD.

No push, merge, tag or deploy.

## 1. What was inspected

- `tex_door` is a **64×64 embedded source array** in `textures.h:5494`.
- `gl_init()` uploads it with `gl_upload(gl_tex[GT_DOOR], tex_door, 64, 64)`
  at `doom-host.c:2353`.
- `gl_upload()` uses **`GL_LINEAR` minification and magnification**, no
  mipmaps, no anisotropic filtering, `GL_REPEAT` wrapping, and uploads the
  source array unchanged.
- The closed door is a **2-unit-thick box** at `x=768..770`,
  `y=256..384`, `z=0..128`. Both faces are full-quad UVs:
  - `gl_tex_quad_tint(... x=767.9 ..., 0,0,1,1, tint 0.50)`
  - `gl_tex_quad_tint(... x=770.1 ..., 0,0,1,1, tint 0.50)`
- The surrounding frame is made of flat `gl_box` slabs: 8-unit side/upper
  blocks, no recessed track, no leaf thickness, no lock housings and no
  warning lamps beyond flat geometry.
- `gl_tour` poses `05-door-closed`, `06-door-open`,
  `14-corridor-door-closed`, `13-room-turnaround` exercise door/room views.
- The normal interactive and scripted paths both use the same `gl_scene()`;
  there is no separate low-quality screenshot renderer.
- The only other door art is a **64×64 tile** in
  `sprites/Glon_AvP_Industrial_Texture_Set.png` (D1/D2/D3 concept sheet), not a
  usable high-resolution source.

## 2. Root cause of the blur

This is **source-resolution and effective-texel-density**, not a filter bug.

- The door face is **128 world units wide × 128 world units high**.
- One 64×64 texture is mapped across that whole face with UV `0..1`.
- Effective density: **64 texels / 128 world units = 0.5 texels per world
  unit**, i.e. **2 world units per texel**.
- At the close 960×600 pose the visible door is roughly 560 screen pixels wide,
  so one source texel is magnified to roughly **9 screen pixels**. At
  1920×1080 it is roughly **18 screen pixels per texel**.
- `GL_LINEAR` interpolation therefore produces large soft blocks. No filtering
  mode can recover detail that was never in the 64×64 source.
- The door frame and adjacent corridor panels use 512×512 art and look sharp
  beside the door, which confirms the renderer is not globally blurry.

The 1920×1080 close-up `/tmp/g5x-before/1920/05-door-closed.png` makes the
source texel size unmistakable.

## 3. Door geometry and animation gaps

These are real renderer gaps but were not patched without suitable artwork:

- **Thickness:** 2 world units. A heavy blast door needs 6–10 units plus a
  recessed frame/jamb, not a thin textured box.
- **Frame:** flat slabs. No side pockets, guide tracks, lock housings, hinge
  blocks, warning lamps or exposed inner edge.
- **Animation:** `gl_corridor(l, st->door_blocked)` receives only the boolean.
  `st->door_open` is read by the HUD/tests but **not by the OpenGL door**.
  The door is drawn only while `door_blocked` is true and disappears once the
  Glon threshold (`door_open >= 32`) clears it. There is no partial-open
  visual.
- **Scripted captures:** `gl_apply_pose()` sets `doom/door-open`, but because
  the renderer ignores that value, a “partially open” screenshot cannot be
  produced. This is why no valid partial-open G5x image exists.
- **State:** Glon owns `door-open`, `door-blocked` and `door-target`; the
  renderer must continue to read those values rather than create a second door
  state machine.

## 4. Required artwork (dependency request)

Please provide **one** of:

### Preferred: two leaf textures

- `assets/corridor/blast_door_left_1024x2048.png`
- `assets/corridor/blast_door_right_1024x2048.png`
- RGBA8 PNG, opaque, no baked perspective, clamp-safe edges.
- **16 texels per world unit** (each leaf is 64×128 world units).
- Left leaf: **U=0 outer edge**, **U=1 central seam**.
- Right leaf: **U=1 outer edge**, **U=0 central seam**.
- Mirror the reinforcement pattern; do not mirror readable markings.

### Alternative: one atlas

- `assets/corridor/blast_door_atlas_2048x2048.png`
- Left half `U 0.0–0.5`, right half `U 0.5–1.0`; each half 1024×2048.

### Art direction requirements

- Weathered industrial yellow paint with restrained saturation.
- Deep scratches, grime, rust bleed and exposed dark steel at edges.
- Heavy reinforced ribs and bolted plates; recessed seams at the leaf border.
- Central interlock seam with a dark rubber/steel gap and mechanical teeth.
- Lock housing near the inner edge at mid-height.
- Hinges/rollers visible at outer edges.
- Restrained yellow/black hazard strip near the bottom, not covering the whole
  leaf.
- Small red/green warning indicator area (can also be a separate emissive
  quad).
- Consistent neutral top-left lighting; **no baked cast shadows**, no baked
  perspective, no text except intentional small stencils.
- Scale: bolts roughly 24–32 px, scratches 2–4 px at 1024×2048.

The existing 64×64 `tex_door` must **not** be scaled up to 2048 and presented
as high resolution. That would preserve exactly the blur seen in the evidence.

## 5. Planned geometry and animation once art exists

- Two leaves, each **64×128 world units**, thickness **6–8 units**.
- Side jambs **16–20 units wide** with recessed guides and side pockets large
  enough to receive the leaves.
- Top header, threshold track, lock housings, hinge/roller blocks and small
  warning lamps.
- Leaves slide apart along the corridor Y axis into the side pockets.
- Visual travel derived directly from `Status.door_open` (0..64); no new state.
- Glon collision remains authoritative at `door_blocked`; the renderer only
  displays progress. At `door_open < 32`, the leaves still look closed enough
  to match the blocking state; at `>=32`, the clear gap agrees with passage.
- Existing Glon door thresholds, target toggling and collision are untouched.
- Polygon budget: simple boxes for frame/tracks, two textured leaves; no
  high-poly greebles.

This geometry/animation plan is documented but **not implemented** because the
face art would still be a 64×64 blur and the acceptance gate cannot pass.

## 6. Audio preparation (documented only)

Current initialization is `SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER)` in the
OpenGL path; **SDL audio is not initialized** and there is no mixer or audio
asset pipeline. No audio engine was added.

Future event points, detected by the host from consecutive `Status` snapshots
(not inside Glon):

| Glon event | Host detection | Future sound |
|---|---|---|
| Lock disengagement | `door_target` changes 0→64 | short servo/latch click |
| Hydraulic opening | `door_open` begins increasing | low hydraulic ram loop |
| Door closing warning | `door_target` changes 64→0 | warning chirp / reversing beep |
| Door closing | `door_open` begins decreasing | hydraulic close |
| Final mechanical clunk | `door_open` reaches 0 | heavy latch/clunk |
| Collision state change | `door_blocked` 1→0 or 0→1 | lock/unlock thunk |

Simplest future path: `SDL_Init(SDL_INIT_AUDIO)`, one `SDL_OpenAudioDevice`,
four/five short WAV one-shots queued on these edge transitions. Do not wire
per-frame sound calls.

## 7. Baseline screenshots (actual OpenGL output)

No G5w/G5x comparison is shown because **G5x made no visual change**; claiming
a comparison would be false.

- Close closed door, 960×600:
  `/tmp/g5x-before/960/05-door-closed.png`
- Close closed door, 1920×1080:
  `/tmp/g5x-before/1920/05-door-closed.png`
- Close closed door from the route:
  `/tmp/g5x-before/nav/02-route-at-closed-door.png`
- Closed door from the corridor:
  `/tmp/g5x-before/extra/14-corridor-door-closed.png`
- Closed door/frame oblique:
  `/tmp/g5x-before/oblique/14-corridor-door-oblique.png`
- Fully open doorway:
  `/tmp/g5x-before/960/06-door-open.png`
  and `/tmp/g5x-before/nav/03-route-door-open.png`
- Normal player spawn:
  `/tmp/g5x-before/nav/00-normal-start.png`
- Corridor with door open:
  `/tmp/g5x-before/960/04-look-back.png`

**Partial-open:** unavailable because the renderer has no partial-open visual;
this is itself part of the finding, not a missing capture attempt.

## 8. Tests and baseline measurements

- `make test`: **pass**, including closed-door collision, open passage and
  reopened-door collision checks.
- `./check-frozen-s1.sh`: **pass** at
  `f90496c26dc45c7a387d8fc8639cd2781507d3d2`.
- Normal interactive OpenGL launch: pass; `player=320,320 angle=240`,
  `door=0/1`, Alien `1120,320` state 0; expected timeout only.
- G5u navigation: pass; reopens/closes door and reproduces barrel route
  (`05` opens, `03-route-door-open`, barrel attack at `(854,348)`).
- G5s/G5r: unchanged by this branch.
- Performance baseline (no G5x code changes), matched 960×600, 180 frames:
  - G5x run: **85.1 FPS** no objects, **56.9 FPS** with objects.
  - G5w comparison: 91.1 / 60.4 FPS; difference is run-to-run GPU variance,
    not a G5x change.
- Executable: **6,117,184 bytes**.
- Loader high-water: **53,705** / **54,569** cells; heap 33,440 / 33,600.
- Additional texture memory: **0 bytes** (no texture added or changed).
- The G5x worktree is clean; only this report is committed.

## 9. Acceptance status

- **Detailed at close range:** **FAIL** — 64×64 source is magnified to soft
  blocks; filtering cannot fix it.
- **Physical depth:** **FAIL / not attempted** — current door remains a
  2-unit box with flat frame; geometry fix is blocked by the art gate.
- **Opening behaviour:** **FAIL** — Glon state/collision are correct, but the
  OpenGL door ignores `door_open` and has no partial animation.
- **Gameplay semantics:** **PASS** — unchanged; door collision and state tests
  pass.
- **High-resolution material:** **BLOCKED** — no suitable artwork in the
  repository.

**G5x is not complete and does not pass the close-up acceptance test. Required
next action: provide the 1024×2048 leaf textures (or 2048×2048 atlas) specified
above; then implement frame geometry and `door_open`-driven animation against
that artwork.**
