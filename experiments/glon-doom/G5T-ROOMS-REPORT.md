# G5t — Engineering Room Upgrade

**Status:** complete for one room. The spawn room is now a distinct industrial
engineering compartment. Corridors, deck material, G5s Alien, G5r rifle/ammo,
tracker, doors, controls, Glon collision and frozen S1 are unchanged.

**Branch/worktree:** `glon-avp-g5t` at `/tmp/opencode/glon-avp-g5t`, based on
G5s `de375ca2df03b6517867e74b80c1277d7634e997`.

**Implementation commit:** `e7a94b268d73d70f838c6d6c2132c20a52eeaa33`
(`Build G5t engineering room geometry`). The report is committed after the
implementation; see the branch HEAD. No push, merge, tag or deploy was done.

## Changed files

- `experiments/glon-doom/doom-host.c` — reusable room components, turbine
  helpers, room draw call, `--gl-rooms` capture mode and `plain` comparison.
- `experiments/glon-doom/assets/rooms/reference/` — the two supplied concept
  sheets and their README, retained for provenance.
- `experiments/glon-doom/README.md` — G5t capture commands and scope.
- `MEMORY.md` — milestone entry.
- `GLON-ARCHITECTURE-MEMORY.md` — geometry, decision, measurement and limitation.
- `experiments/glon-doom/G5T-ROOMS-REPORT.md` — this report.

No map, collision, AI, weapon, memory-layout, frozen-S1 or language-semantics
file was changed.

## Artwork interpretation

The references were used as architectural guidance, not textures:

- `engineering-room-a.png`: central turbine with an orange rotor, yellow
  guardrails, large pipe elbows, service panels and painted hazard markings.
- `engineering-room-b.png`: side generator, coolant/service vessels, overhead
  conduits, cabinets, an open central floor and door sightline.

No part of either image is pasted onto a wall. Existing materials do the work;
new colour/shading is used only for steel, hazard paint and indicators.

## Room identity and geometry

### Main turbine/generator (north side)

- Footprint approximately `x=150..500, y=145..245`; the central door-to-spawn
  lane remains south of it.
- Front-facing turbine drum on a north–south axis, 55-unit radius, with a
  layered dark flange, orange 12-blade rotor and hub, visible from the room.
- Heavy plinth, cradle, end rings, top exhaust stack, side feeds, hazard-striped
  apron and yellow rail enclosure on the south/east/west sides.
- Existing indicator-style lights are controlled and small: green/red/amber
  status lamps, no new gameplay light.

### Coolant bay (west side)

- Two vertical vessels at approximately `(182,386)` and `(182,472)`, radius 23,
  with bands, top caps, manifold pipes, east-facing valve wheels and gauges.
- Containment curb, hazard paint, service rail and wall feeds. The vessels sit
  west of the central lane.

### Cabinets, control bank and wall depth

- South-wall service cabinets at `x=300..690`, `y≈614..640`, with framed doors,
  louvres, handles and status lamps.
- East-wall cabinets at `x≈738..762`, `y=430..610`, clear of the door opening.
- North-east control bank at `x=550..736`, `y=136..204`, with monitor panels,
  switch strips and indicator rows.
- Recessed maintenance bays on the west wall (`x=128`), north wall (`y=128`) and
  east return; grilles on north/south walls; vertical conduits and cable drops.
  Bays use real 8-unit depth, frames and a textured service-panel face.

### Overhead and structural work

- Four longitudinal pipe runs at `y=168, 206, 352, 372`, roughly `z=96..114`,
  with flanges and visible wall penetrations.
- Four cable trays at `y=148, 320, 560, 588`, with coloured cable bundles.
- Five recessed ceiling fixtures with dark housings, bright lenses and
  protective bars.
- Four structural posts and three cross-room ceiling beams.
- Yellow hazard railings, striped floor aprons, service grating and restrained
  status lighting complete the compartment language.

### Reusable components

`eng_hazard_x`, `eng_rail_x/y`, `eng_pipe_x/z`, `eng_tray_x`, `eng_fixture`,
`eng_wall_bay_x/y`, `eng_grille_y`, `eng_indicator`, `eng_cabinet_x/y`,
`eng_generator`, `eng_coolant`, `eng_walls`, `eng_overhead`.

New reusable primitive helpers: `gl_cyl_y`, `gl_disc_x/y`, `gl_cyl_z_lit`,
`gl_cyl_y_lit`, `gl_turbine_face`.

These are composed rather than hardcoding each wall; the same wall-bay,
cabinet, rail, tray and fixture components remain available for later rooms.

## Materials and lighting

- Deck uses the existing `GT_ART_FLOOR` material unchanged; corridor materials
  and geometry are untouched.
- Room wall/cabinet faces use existing `GT_SERVICE` texture at varied tints;
  frames, rails, machinery and piping use shaded flat steel colours.
- The orange rotor, hazard yellow/black paint, red/green/amber indicators and
  cable colours are deliberate new colour treatments, not new bitmap materials.
- Ceiling lenses use the existing additive `gl_light_panel` path and follow the
  existing Glon light pulse. Indicators and screens are static emissive-coloured
  geometry; no new light propagation system was added.
- No new texture pipeline, image atlas, shader or VM primitive was introduced.

## Navigation, collision and occlusion

- Player spawn `(320,320)` and the door opening `(768, 256..384)` remain clear.
  The central lane is kept visually open; the generator is north of it, coolant
  vessels west, cabinets south/east, and overhead work is above head height.
- **Glon collision is deliberately unchanged.** The room is renderer-side
  geometry, so machinery adds no colliders and cannot trap the player. Existing
  movement/controls work; `--trace` starts in the room and reaches the corridor
  normally. `05-room-to-corridor.png` shows the return view.
- Large geometry is outside the door and spawn path. There is no new navmesh,
  door logic or AI movement.
- The Alien still uses G5s directional artwork and the existing door-to-player
  path. Depth testing keeps machinery in front of/behind the billboard
  correctly; `06-alien-near-generator.png` and the directional suite
  (`/tmp/g5t-dir/`) exercise the room side.
- Limitation: because collision is untouched, a player who deliberately walks
  into a vessel or cabinet will pass through it. Adding machinery colliders is
  a separate gameplay/navmesh change, not part of this preservation pass.

## Screenshots

Actual GL gameplay captures, 960×600, with objects and the live HUD/rifle/ammo:

- `/tmp/g5t-room/01-inside-entrance.png`
- `/tmp/g5t-room/02-centre-generator.png`
- `/tmp/g5t-room/03-opposite-corner.png`
- `/tmp/g5t-room/04-corridor-to-room.png`
- `/tmp/g5t-room/05-room-to-corridor.png`
- `/tmp/g5t-room/06-alien-near-generator.png` (Alien occlusion/path check)
- `/tmp/g5t-room/07-coolant-bay.png` (coolant vessels/valves)

Before comparison from identical poses:

- `/tmp/g5t-room-plain/01-inside-entrance.png` … `07-coolant-bay.png`

Side-by-side comparisons:

- `/tmp/g5t-compare-02-centre-generator.png`
- `/tmp/g5t-compare-03-opposite-corner.png`
- `/tmp/g5t-compare-04-corridor-to-room.png`

1920×1080:

- `/tmp/g5t-room-max/` (01–07)

Directional Alien regression captures with the room present:

- `/tmp/g5t-dir/01-headon-long.png` … `07-headon-attack.png`

Reference sheets:

- `assets/rooms/reference/engineering-room-a.png`
- `assets/rooms/reference/engineering-room-b.png`

## Tests

- `make test`: passes (self-test, trace, renderer/physics/regression tests,
  G5s Alien checks).
- `./check-frozen-s1.sh`: passes at
  `f90496c26dc45c7a387d8fc8639cd2781507d3d2`.
- Room capture: `--gl-rooms` completed at 960×600 and 1920×1080, both detailed
  and `plain`, with no GL errors.
- Directional suite: `--gl-directional` completed with the room geometry active.

## Performance at 960×600

Matched 180-frame runs, three runs each, same session/device (WSLg D3D12 AMD
Radeon). Medians:

| Configuration | G5s baseline | G5t room | Delta |
|---|---:|---:|---:|
| No object mode | 117.5 FPS (8.51 ms) | 97.1 FPS (10.30 ms) | −17.4% |
| With objects | 65.7 FPS (15.23 ms) | 63.1 FPS (15.85 ms) | −4.0% |

- The absolute median cost is about +1.79 ms without objects and +0.62 ms with
  objects. The room remains above 60 FPS at 960×600 with objects.
- G5t object-mode sim median 10.35 ms, submit 1.11 ms; no-object sim 4.51 ms,
  submit 1.13 ms.
- Loader/heap unchanged: 53,337/33,440 without objects, 54,201/33,600 with.
- Executable: 6,092,080 bytes (G5s: 6,074,896).
- The G5s report recorded 95.3/54.9 FPS in a different session; use the matched
  medians above for the regression comparison.

## Remaining limitations / visual proposals (not implemented)

- Only this one room is upgraded. Cargo, medical and operations rooms are not
  started.
- Room geometry is immediate-mode renderer-side detail. No batching/instancing,
  collision, navmesh or occlusion culling was added; this is the main reason for
  the no-object FPS cost.
- The turbine rotor, valves, indicators and lights are static apart from the
  existing ceiling-light pulse. There is no rotor animation, steam, particle or
  sound pass.
- Recessed bays reuse the existing service-panel texture. A bespoke seamless
  room atlas would reduce repetition and is the next art proposal; the concept
  sheets are not seamless.
- Machinery has no physical colliders; deliberate walk-through is possible.
  Player/doors/Alien behaviour otherwise remain the preserved game.
- G5s close-up Alien resolution remains constrained by the 188–268 px source
  poses; no AI or weapon mechanics were added.

## Acceptance

The room is substantially three-dimensional and reads as a working machinery
space: turbine/rotor, coolant vessels, service cabinet/control bank, deep wall
bays, overhead pipes/trays, ventilation, ceiling fixtures, hazard rails and
indicator lighting, with an open central floor and clear door sightline. It is
not a corridor with decorative wall textures.
