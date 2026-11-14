# G5v — Engineering Room Materials and Lighting

**Status:** complete for the visual material pass. The existing engineering-room
layout, machinery, corridor, gameplay and frozen S1 are preserved. The room now
uses the corridor's actual weathered textures, has readable panel/casing
detail, worn metalwork and warm/cool baked lighting.

**Branch/worktree:** `glon-avp-g5v` at `/tmp/opencode/glon-avp-g5v`, based on
G5u `0a1cd79d11ee7f6d32a1f3c3051b6727a7ff569e`.

**Implementation commit:** `7b9ebc21b3880cd69bf128a95bc07bd4b9d7c977`
(`Upgrade G5v engineering room materials and lighting`). The report is committed
after the implementation; see branch HEAD. No push, merge, tag or deploy.

## 1. Inspection: how the corridor gets its look

The corridor uses three existing 512×512 art textures, uploaded through the
same `gl_upload()` path as the rest of the game:

| Material | Upload | Usage |
|---|---|---|
| `GT_ART_WALL` | `wall_bulkhead_steel` | Far A-3 bulkhead; weathered painted steel, seams, rivets, rust streaks and the A-3 sign. |
| `GT_ART_PANEL` | `wall_recess_panel` | Recessed wall bays, vent slots, hinges, handles, rivets and rust. Corridor side panels. |
| `GT_ART_FLOOR` | `floor_plate` | Deck plates, scratches, rivets and stains. |

- **Filtering/wrapping:** linear min/mag filtering and `GL_REPEAT`.
- **Coordinate convention:** `gl_upload()` vertically flips the source array;
  UVs are ordinary 0..1 or tiled 0..N coordinates. Corridor walls use
  `gl_tex_quad` on full panels; the deck uses a world-scaled cropped UV window.
- **Shading:** fixed-function immediate mode with no GL lights, no normal
  arrays and no dynamic shadows. Richness comes from the albedo textures,
  per-face tint multipliers, dark ribs/frames, warm floor strips and additive
  `gl_light_panel` lenses.
- **Why the room looked flat:** its walls/cabinets were mostly flat-coloured
  `gl_box` primitives, the room light was a single uniform fixture palette, and
  several room panel calls passed a material **enum** to
  `gl_tex_quad_tint()` even though that helper expects a **GL texture name**
  (`gl_tex[TEX]`). The numeric name silently sampled a different texture.
  The machinery was untextured or used small 64×64 service flats.

The machine casing, turbine, tanks and pipes are now assigned to the same
corridor material family. No new texture assets or texture pipeline were
created.

## 2. Material assignments

### Walls, bays, cabinets and control bank

- Recessed wall bays and cabinet faces: `GT_ART_PANEL` (recessed vent panel,
  hinges, handle, rust). These now use the correct `gl_tex[GT_ART_PANEL]` name.
- Room panel albedo is re-exposed with the fixed-function `GL_ADD` texture
  environment (not a second texture pass): texture detail is retained while the
  dark panel texture reaches corridor-like visibility.
- Lower wall panels were added as the same recessed steel material, so the
  room's wall field no longer stops at the upper bays.
- Cabinet bodies and generator casing: a cropped `GT_ART_WALL` steel region
  above the A-3 plate (no corridor signage), with seams/rivets/rust streaks.
- Control bank casing: steel; control face: recessed panel.
- New cabinet hardware: dark hinges, handle bars, yellow/black identification
  labels and small red/green status indicators.

### Turbine and generator

- Plinth and casing: cropped weathered bulkhead steel, with panel seams from
  the texture and dark structural ribs.
- Turbine drum: flat-lit steel with circumferential dark bands and rust bands.
  The drum deliberately does **not** wrap the plate texture around its small
  radius, which produced a braided/chainmail artifact in earlier passes.
- Rotor face: metallic outer ring with twelve fasteners, dark housing ring,
  existing radial orange core, stronger orange internal glow and a warm floor
  pool.
- Exhaust: dark rust/soot decals on the wall/casing and an oil stain on the
  floor apron.
- Existing orange core slats and indicator lights remain.

### Coolant vessels

- Tanks: flat-lit brushed steel with circumferential joints/seams, darkened
  fittings and longitudinal wear/highlight bands.
- Existing red valve wheels remain distinct; yellow/black identification plates
  and small status lamps have been added.
- The tanks do not use the plate texture wrapped around the cylinder because
  that read as crushed foil; wear is expressed with bands and lighting instead.

### Pipes, trays and guardrails

- Pipes: flat-lit metal with per-segment shading (darker undersides), varied
  warm/cool steel tints, couplings/flanges and new vertical pipe hangers.
- Cable trays keep their existing banded/cable construction and brackets.
- Guardrails: alternating worn yellow/olive paint values, brighter worn top
  edges, dark mounting brackets and dark lower kick strips instead of uniform
  bright yellow.

### Wall hardware and lighting fixtures

- Cool work-lamp lenses above the bays, dark frames and baked wall wash.
- Cool ceiling light rails between the structural beams.
- Warm turbine/floor glow and warm wall wash.
- Small status LEDs remain restrained; no extra UI or gameplay effect.

## 3. Lighting changes

There is no dynamic light or shadow system. The renderer is unchanged in kind:

- Existing per-face colour shading supplies darker undersides and recesses.
- `GL_ADD` texture-environment mode re-exposes the dark room panel albedo.
- Additive emissive quads/lenses supply:
  - cool overhead rails and work lamps;
  - cool floor pools under the fixtures;
  - warm orange turbine core, wall wash and floor pool;
  - panel-level cool/warm wash;
  - soot/oil shadows.
- Room-only ambient wash quads are drawn on the room's interior surfaces, so
  the adjoining A-3 corridor is not relit or altered.

The result has a cool overhead/industrial cast, a warm turbine focus, darker
pipe/casing undersides and visible panel recesses. The room remains readable
for navigation; it is not a dark horror pass.

## 4. Screenshots

All are actual OpenGL captures through the existing renderer. 960×600 unless
noted; same camera height, filtering, objects mode and exposure for the
comparable before/after shots.

### Required views — after

1. **Normal player spawn:** `/tmp/g5v-after-final/nav/00-normal-start.png`
2. **Turbine close-up:** `/tmp/g5v-after-final/room/02-centre-generator.png`
3. **Coolant vessels / service cabinets:** `/tmp/g5v-after-final/room/07-coolant-bay.png`
4. **Overhead pipes / lighting:** `/tmp/g5v-after-final/room/01-inside-entrance.png`
   and `03-opposite-corner.png`
5. **Room from doorway:** `/tmp/g5v-after-final/room/04-corridor-to-room.png`
6. **A-3 corridor, comparable settings:** `/tmp/g5v-after-final/corridor/01-entrance.png`
7. **Alien in engineering room with rifle:** `/tmp/g5v-after-final/nav/05-route-back-in-room.png`

Additional after captures:

- `/tmp/g5v-after-final/room/05-room-to-corridor.png`
- `/tmp/g5v-after-final/room/06-alien-near-generator.png`
- `/tmp/g5v-after-final/corridor/02-mid-corridor.png`
- `/tmp/g5v-after-final/nav/06-barrel-start.png`
- `/tmp/g5v-after-final/nav/08-barrel-60.png`
- `/tmp/g5v-after-final/dir/01-headon-long.png` … `07-headon-attack.png`
- 1920×1080: `/tmp/g5v-after-final/roommax/` and `navmax/`

### Before

G5u/G5t room and corridor captures at the same 960×600 settings:

- `/tmp/g5v-before/room/01-inside-entrance.png`
- `/tmp/g5v-before/room/02-centre-generator.png`
- `/tmp/g5v-before/room/03-opposite-corner.png`
- `/tmp/g5v-before/room/04-corridor-to-room.png`
- `/tmp/g5v-before/room/07-coolant-bay.png`
- `/tmp/g5v-before/corridor/01-entrance.png`
- G5u normal spawn: `/tmp/g5u-nav-after/00-normal-start.png`

### Side-by-side comparisons

- `/tmp/g5v-after-final/compare-room-01-inside.png`
- `/tmp/g5v-after-final/compare-room-02-turbine.png`
- `/tmp/g5v-after-final/compare-room-04-doorway.png`
- `/tmp/g5v-after-final/compare-room-07-coolant.png`
- `/tmp/g5v-after-final/compare-normal-spawn.png`
- `/tmp/g5v-after-final/compare-corridor-01.png`

The corridor comparison is **pixel-identical** (difference bounding box
`None`); only the room changed.

The key comparison is `compare-room-01-inside.png`: before is flat dark
geometry with untextured panels; after has lit recessed panels, weathered steel
casing, warm turbine light, cool overhead rail, visible pipe joints, worn rails
and a far wall that reads as part of the same ageing ship as the corridor.

## 5. Performance

Matched 180-frame runs, three each, 960×600, same WSLg D3D12 AMD Radeon
environment. Medians:

| Configuration | G5u before | G5v after | Delta |
|---|---:|---:|---:|
| No objects | 84.5 FPS (11.83 ms) | **91.2 FPS** (10.97 ms) | +7.9% |
| With objects | 54.9 FPS (18.22 ms) | **60.1 FPS** (16.63 ms) | +9.5% |

G5v after detail:

- No objects: sim 3.91 ms, submit 1.13 ms, total 10.97 ms.
- Objects: sim 9.58 ms, submit 1.17 ms, total 16.63 ms.

The measured result is slightly better than the matched G5u runs, with no
unexplained regression. The room adds textured surfaces and emissive quads but
avoids per-frame uploads, extra texture assets, high-poly decoration and a
renderer rewrite. Repeated static machinery is still drawn immediately; caching
it in a display list remains a bad fit for this WSLg driver, so it was not used.

- Executable: **6,113,024 bytes** (G5u 6,100,344; +12,680 bytes).
- Loader high-water: 53,705 cells without objects, 54,569 with objects.
- Additional texture memory: **0 bytes**; the room reuses the three existing
  512×512 art textures and the existing 64×64 service/door/object textures.
- Loader table probe still has 30 cells free before refusal; no loader capacity
  or VM change.

## 6. Tests and regressions

- `make test`: **pass**; trace retains closed-door, activation, corridor and
  strafe behaviour.
- `./check-frozen-s1.sh`: **pass**, hash
  `f90496c26dc45c7a387d8fc8639cd2781507d3d2`.
- Normal interactive launch: 640×400 OpenGL, `player=320,320 angle=240`,
  Alien `1120,320` state 0, door closed; ran until timeout without error.
- G5u navigation: start state unchanged; barrel route reaches
  `(884,348)` by tick 60 and stops at attack range `(854,348)` at tick 120;
  leave/return route still puts the Alien at `(502,320)` in the room.
- Directional Alien: `--gl-directional` completed; `01-headon-long` through
  `07-headon-attack` captured.
- Rifle/ammo: visible in all captures with live `24`.
- Doors, controls, motion tracker, spawn orientation, object collision and
  G5s/G5r assets are unchanged.
- A-3 corridor: pixel-identical before/after.

## 7. Remaining visual limitations

- This is improved albedo/emissive/baked shading, not physically based
  rendering or true local illumination. There are no dynamic shadows.
- The room remains somewhat darker and less busy than the narrow A-3 corridor;
  the corridor has denser panels and larger wall coverage.
- The turbine drum, tanks and pipes use flat-lit metal plus bands/wear rather
  than wrapped plate textures. Wrapping the plate around small cylinders caused
  braiding/foil artifacts, so that trade-off was deliberate.
- The room still lacks machinery collision (existing G5t limitation) and the
  CPU fallback does not have the OpenGL engineering-room materials.
- G5s close-up Alien resolution remains below its own acceptance threshold;
  this pass does not change Alien art.
- Room wall panels and cabinet faces use `GL_ADD` re-exposure; this deliberately
  lifts the dark albedo but is not a general exposure/lighting system.
- No new art pack, weapon, pathfinding, Alien state or gameplay mechanic was
  introduced.

## 8. Acceptance summary

- **Visual materials:** existing corridor steel/panel/floor materials are now
  applied to the room's walls, cabinets, casing, pipes, tanks, rails and
  turbine, with recessed vents, seams, fasteners, hinges, handles, status
  lamps, warning labels, soot/oil and worn paint.
- **Lighting/atmosphere:** cool overhead industrial light and warm turbine
  illumination provide contrast and depth without dynamic shadows or a renderer
  rewrite.
- **Corridor match:** the room uses the same texture family and comparable
  camera/filter settings; the corridor itself is pixel-identical.
- **Performance:** no regression; matched medians improved slightly.
- **Gameplay/architecture:** G5u navigation, G5s directional Alien, G5r rifle
  and ammo, doors, controls, tracker, layout, corridor and frozen S1 are intact.
