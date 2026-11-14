# G5w — Engineering Machinery Textures and UV Mapping

**Status:** complete for machinery texture coverage. The engineering room's
turbine drum/ring, both coolant-vessel bodies/caps/valves and overhead/wall
pipe bodies are now textured on their actual curved geometry. Layout, gameplay,
corridor, floor, doors, tracker, rifle, Alien art, frozen S1 and VM code are
unchanged.

**Branch/worktree:** `glon-avp-g5w` at `/tmp/opencode/glon-avp-g5w`, based on
verified G5v HEAD `5029ea360ce596ed4ddf89fb8773f92f190d91e0`.

**Implementation commit:** `d9680384dd2a642f11b7d1f6509919ec5a22f566`
(`Texture G5w engineering machinery with cylindrical UVs`). This report is
committed locally after the implementation; see branch HEAD. No push, merge,
tag or deploy.

## 1. Root causes: why the machinery was flat

The renderer was already capable of displaying detailed textures: the rifle,
floor, cabinet faces, wall panels and corridor art all use the existing
`gl_tex[...]` uploads. The machinery failed for four concrete reasons:

1. **The curved-surface helpers explicitly disabled texturing.**  
   `gl_cyl_x`, `gl_cyl_y`, `gl_cyl_z`, `gl_cyl_*_lit`, `gl_disc_x`,
   `gl_disc_y` and `gl_turbine_face` all called
   `glDisable(GL_TEXTURE_2D)` and then `gl_rgb(...)`. They drew solid colour
   only.

2. **They emitted no texture coordinates.**  
   Even if a caller had left a texture bound, the cylinder and disc vertices
   had no `glTexCoord2f` at all. The machinery geometry therefore had no UV
   mapping to sample.

3. **The pipe helpers used the same flat primitive path.**  
   `eng_pipe_x()` called `gl_cyl_x_lit()`, and `eng_pipe_z()` called
   `gl_cyl_z_lit()`. Couplings added flat cylinder rings and brackets added flat
   boxes. The long overhead runs, wall feeds and coolant connections were all
   therefore uniform colour.

4. **G5v's textured cabinet work did not cover these functions.**  
   G5v correctly introduced welded/panel/cabinet texture boxes and fixed the
   material-enum vs `gl_tex[...]` bug on wall bays. It did not alter the
   cylinder/disc/pipe helpers, so every prominent machine surface remained on
   the older flat path. Large grey tanks, brown pipe runs, a brown turbine
   housing and red valve discs were the visible result.

A material enum is an index; `glBindTexture()` needs the `GLuint` name returned
by `gl_tex[...]`. G5w follows the corrected G5v rule for all machinery:
`gl_tex[GT_ART_*]`, never a raw material enum.

## 2. Texture and UV implementation

### New smooth cylindrical helpers

- `gl_cyl_x_tex_smooth()` — pipe/coupling axis along X.
- `gl_cyl_y_tex_smooth()` — turbine drum axis along Y.
- `gl_cyl_z_tex_smooth()` and `gl_cyl_z_tex_rot()` — vertical tanks and pipes;
  the rotated form maps the texture length along Z and circumference around Z.
- `gl_disc_z_tex()` / `gl_disc_y_tex()` — planar-mapped end caps.
- `gl_disc_y_tex_rgb()` / `gl_disc_x_tex_rgb()` — textured painted valve
  wheels with an explicit red tint.
- `gl_ring_y_tex()` — annular turbine housing ring with angle/radius UVs.
- `eng_pipe_x_tex()` now delegates to the proven `eng_pipe_x()` path.

Each helper:

- enables `GL_TEXTURE_2D`, binds one `gl_tex[...]` name, emits static UVs and
  disables texturing afterwards;
- shades per **vertex** with a `cos(angle)` factor instead of alternating flat
  per-face colours, so cylinders read round rather than faceted;
- uses one static UV generation path per surface, with no per-frame uploads and
  no new texture objects.

### Mapping conventions

| Surface | U | V | Notes |
|---|---|---|---|
| X-axis pipe | along pipe length | around circumference | rust/steel crop chosen from material colour; u repeat controls length density. |
| Y-axis turbine drum | around circumference | along drum length | full safe bulkhead strip; no A-3. |
| Z-axis tank/pipe | along vessel length | around circumference | rotated mapping keeps wear running along the vessel. |
| Turbine annulus | angle around ring | radius | concentric steel/scratch mapping, no planar radial star. |
| End caps / valve faces | planar X/Z or Y/Z | planar | separate mapping from the cylindrical body. |

### Safe texture crops

The corridor textures are not seamless cylinder tiles. G5w uses crops that are
clean and, critically, do not contain the A-3 sign:

- Bulkhead steel **top strip**: `u 0.02–0.98`, `v 0.02–0.38`.
- Bulkhead steel **bottom strip**: `u 0.02–0.52` / `0.52–0.98`,
  `v 0.64–0.98`.
- Bulkhead steel left rust strip for warm pipes: `u 0.02–0.14`,
  `v 0.02–0.98`.
- Floor plate clean upper field: `u 0.18–0.82`, `v 0.05–0.42`, and central
  clean plate `u 0.30–0.70`, `v 0.30–0.70`.
- Valve wheel red paint: floor-plate clean field modulated by
  `(0.95, 0.28, 0.08)`.

The A-3 sign lies around `u 0.17–0.60`, `v 0.42–0.62`; every crop above either
stops above/below it or stays left of it. Final room captures contain no A-3
signage.

## 3. Which surfaces are genuinely textured

### Turbine

- **Drum side:** `gl_cyl_y_tex_smooth()` with bulkhead top strip, around the
  circumference and along the drum length. Seams, dark ribs and bolts retained.
- **Front housing ring:** `gl_ring_y_tex()` with floor-plate clean field,
  angle/radius UVs and a cool tint. The former flat brown ring is replaced by a
  textured annular steel/machined surface.
- **Casing/plinth:** existing G5v cropped bulkhead-steel boxes retained.
- **Rotor:** orange core, slats, hub and internal illumination retained.
- Small front rim and bolt heads remain solid metal; they are hardware, not the
  main housing.

### Coolant vessels

Both vessels now have:

- textured main body mapped along Z with circumference around Z;
- textured top and bottom caps mapped separately;
- textured red valve wheels on the Y and X faces;
- existing bands, couplings, pipe connections, indicators and labels retained.

The two vessels use different crops/tints so they are not identical copies.
The body texture is a dark rust/steel panel crop; the painted valve faces read
as worn red metal rather than flat plastic.

### Pipes

- Overhead X-axis runs: smooth cylindrical UVs, steel or warm/rust crop, u
  along the run and v around it.
- Wall/vertical Z feeds: rotated cylindrical UVs along the run.
- Couplings, hangers and brackets retain their geometry and dark metal
  appearance.
- No rectangle-in-front-of-pipe texture overlay was used.

### Cabinets, valves and rails

- Cabinet bodies and wall bays retain G5v's cropped bulkhead/panel textures.
- Valve assemblies have textured wheel faces and existing steel/dark hubs.
- Guardrails keep their worn paint variation and dark brackets. They are
  painted metal geometry rather than a new texture surface; no decorative
  geometry was added to hide missing texture.

## 4. Phase 3 representative-machine gate

All evidence is actual OpenGL output at 960×600 (with 1920×1080 duplicates),
same cameras and settings as G5v.

- **Turbine close-up:** `/tmp/g5w-final/compare-turbine-close.png`
- **Turbine second angle:** `/tmp/g5w-final/compare-turbine-angle2.png`
- **Coolant close-up:** `/tmp/g5w-final/compare-coolant-close.png`
- **Coolant second angle:** `/tmp/g5w-final/compare-coolant-angle2.png`
- **Overhead pipe close-up:** `/tmp/g5w-final/compare-pipe-close.png`
- **Pipe second angle:** `/tmp/g5w-final/compare-pipe-angle2.png`

The gate passed for visible curved-surface texture: the turbine drum and ring,
the coolant body/caps/valve and the pipe bodies all show material detail that
was absent in G5v. The close-range pipe pattern still reads as fine ribbing
rather than smooth brushed steel; that is the existing-art limitation described
below, not a flat-colour fallback.

## 5. Full before/after captures

Before images are G5v final at the same settings:

- `/tmp/g5w-before/room/01-inside-entrance.png`
- `/tmp/g5w-before/room/02-centre-generator.png`
- `/tmp/g5w-before/room/03-opposite-corner.png`
- `/tmp/g5w-before/room/04-corridor-to-room.png`
- `/tmp/g5w-before/room/07-coolant-bay.png`
- `/tmp/g5w-before/normal-spawn.png`
- `/tmp/g5w-before/corridor/01-entrance.png`
- `/tmp/g5w-before/roommax/` (1920×1080)

After images are G5w final:

- `/tmp/g5w-final/room/01-inside-entrance.png`
- `/tmp/g5w-final/room/02-centre-generator.png`
- `/tmp/g5w-final/room/03-opposite-corner.png`
- `/tmp/g5w-final/room/04-corridor-to-room.png`
- `/tmp/g5w-final/room/07-coolant-bay.png`
- `/tmp/g5w-final/nav/00-normal-start.png`
- `/tmp/g5w-final/corridor/01-entrance.png`
- `/tmp/g5w-final/roommax/` and `/tmp/g5w-final/navmax/` (1920×1080)

Side-by-sides:

- `/tmp/g5w-final/compare-full-room.png`
- `/tmp/g5w-final/compare-normal-spawn.png`
- `/tmp/g5w-final/compare-corridor.png`
- `/tmp/g5w-final/compare-turbine-close.png`
- `/tmp/g5w-final/compare-coolant-close.png`
- `/tmp/g5w-final/compare-pipe-close.png`
- `/tmp/g5w-final/compare-turbine-angle2.png`
- `/tmp/g5w-final/compare-coolant-angle2.png`
- `/tmp/g5w-final/compare-pipe-angle2.png`

The A-3 corridor comparison has a **`None`** difference bounding box: the
corridor is pixel-identical. The rifle and floor remain unchanged.

## 6. Performance

Matched 960×600, 180-frame runs on WSLg D3D12 AMD Radeon. Medians:

| Configuration | G5v verified report | G5v matched this session | G5w final |
|---|---:|---:|---:|
| No objects | 91.2 FPS | 88.7 FPS | **91.1 FPS** |
| With objects | 60.1 FPS | 59.6 FPS | **60.4 FPS** |

G5w final details:

- No objects: sim 4.06 ms, submit 1.10 ms, total 10.98 ms.
- Objects: sim 9.58 ms, submit 1.26 ms, total 16.57 ms.

There is no substantial regression; on these matched runs G5w is level with
G5v. The implementation adds textured quads but no per-frame image processing,
no texture uploads and no extra texture objects.

- Executable: **6,117,184 bytes** (G5v 6,113,024; +4,160 bytes).
- Loader high-water: **53,705** cells without objects, **54,569** with objects.
- Heap high-water: 33,440 / 33,600.
- Additional texture memory: **0 bytes**; all art is existing corridor art.
- Loader table probe still has 30 cells free before refusal; no VM capacity or
  loader change.

### Rejected optimisation experiment

Texturing the 16 small flange/coupling surfaces added roughly 20% to the
matched frame cost for a very small visible gain. Those flanges remain flat
dark steel. The main pipe bodies, tanks, valves and turbine ring remain
textured. This is why not every ring on every pipe is textured.

## 7. Tests and regressions

- `make test`: **pass**; closed-door, activation, corridor and strafe traces
  retained.
- `./check-frozen-s1.sh`: **pass** at
  `f90496c26dc45c7a387d8fc8639cd2781507d3d2`.
- Normal interactive OpenGL launch: 640×400, `player=320,320 angle=240`,
  `alien=1120,320 state=0`, door closed; exited only at the expected timeout.
- G5u barrel navigation: start state unchanged; barrel route reaches
  `(884,348)` at tick 60 and attack range `(854,348)` at tick 120; route
  behaviour retained.
- G5s directional Alien: all seven directional captures produced; no asset or
  state changes.
- G5r rifle/ammunition: rifle and live `24` visible in room, spawn and
  directional captures.
- Doors, controls, motion tracker, spawn positions and room layout unchanged.
- A-3 corridor: pixel-identical.
- Frozen S1 and seven-primitive VM: untouched.

## 8. Remaining limitations

- **Existing art is not a seamless machinery material.** The corridor's
  bulkhead and floor textures carry rivets, plate seams and strong rust
  streaks. Reusing clean crops avoids A-3 signage and obvious grid artefacts,
  but close-up pipes still read as fine ribbing/knurling rather than a smooth
  brushed tube. A dedicated seamless weathered-steel/pipe texture would improve
  this; none was added, because that requires new assets rather than another
  renderer change.
- **Couplings, flanges and brackets remain flat dark steel.** Texturing them
  was measured and rejected for a ~20% frame cost. Geometry and placement are
  retained.
- **The coolant body is a dark rust/steel crop.** It no longer looks like a
  smooth grey plastic drum, but it is not a bright brushed-steel vessel; the
  existing art has no clean seamless metal surface.
- **The turbine ring is a machined/rusted annulus**, not a smooth forged steel
  ring. It is clearly textured and no longer flat brown, but the source texture
  still contains plate/rust structure.
- **No dynamic lighting or shadows were added.** The pass is texture coverage
  and UV mapping; the existing baked/additive room lighting remains.
- **No new gameplay or collision.** G5t machinery remains visual-only with no
  collision, and G5s close-up Alien resolution remains an existing limitation.
- No new texture assets, no new texture pipeline, no new VM primitives, no
  frozen-S1 changes.

## 9. Acceptance summary

- **Turbine:** curved drum side and front ring are visibly textured in
  weathered/machined steel rather than flat brown.
- **Coolant vessels:** cylindrical bodies and separate caps have visible
  mapped texture; valve wheels are worn red metal, not flat discs.
- **Pipes:** curved pipe surfaces have longitudinal/circumferential UVs and
  material detail; no rectangle overlay and no A-3 sign.
- **Consistency:** machinery now has texture coverage comparable in kind to the
  cabinets/corridor, though the existing art limits close-up smoothness.
- **Architecture:** no frozen-S1/VM change and no unnecessary rendering
  framework.
- **Performance:** matched G5w medians 91.1/60.4 FPS, level with G5v.
- **Evidence:** actual before/after close-ups and full-room captures listed
  above.
