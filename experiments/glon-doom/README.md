# Glon Doom — G5a: one room, one corridor

This is the first playable first-person environment for the Glon Doom
experiment. It is a deliberately small stepping stone toward executing real
Doom-engine concepts through Glon. There are no enemies, weapons, sound,
doors, textures or WAD loading yet.

The map is one room connected to an east corridor and then a north corridor
with one 90-degree turn. The player can walk, strafe, turn and collide with the
solid boundary. Geometry is real: no pre-rendered scene and no static fake
perspective.

## Build and run

From this directory:

```sh
make
DOOM_ROOT=../.. ./build/doom
```

Or:

```sh
make run
```

The build needs a C compiler and SDL2 development files (`libsdl2-dev` on
Ubuntu). Tested under WSLg / Ubuntu 22.04.

Useful headless modes:

```sh
./build/doom --self-test       # deterministic geometry/collision/render tests
./build/doom --trace           # replayable scripted movement trace
DOOM_ROOT=../.. ./build/doom --bench 60
```

The executable defaults `DOOM_ROOT` to `../..`; it loads:

- `jupyter/prelude.glon`
- `experiments/glon-doom/doom.glon`

## Controls

| Key | Action |
| --- | --- |
| W | move forward |
| S | move backward |
| A | strafe left |
| D | strafe right |
| Left arrow | turn left |
| Right arrow | turn right |
| Escape | quit |

The simulation runs at a fixed 60 ticks/second with accumulated catch-up (up
to 10 ticks per display frame), independent of the measured display rate.

## Map

World units use 64 integer subunits per map unit. The single sector has floor
z=0, ceiling z=128, floor colour 6 and ceiling colour 7. The walkable union is:

```text
                      north corridor
                      x=1152..1280
                      y=384..1024
                            |
 room x=128..768      east corridor x=768..1280
 y=128..640           y=256..384
```

The boundary is ten vertices and ten solid linedefs. The player starts at
`(320,320)` facing east. The map is represented in `doom.glon` as Doom-inspired
vertices, linedefs and one sector. There is no BSP tree yet; all ten linedefs
are tested directly.

The hot render path uses literal wall coordinates generated from the same map
to avoid repeated `block-at` calls. The map data remains the source of the
collision tests and documentation; keep them in sync if the map changes.

## Division of labour

Glon (`doom.glon`) performs the meaningful game and projection work:

- map vertices, linedefs and sector data;
- player state, fixed-tick movement, strafing and turning;
- circle-versus-wall collision using the linedefs, with axis-separated sliding;
- ray direction from the camera/plane vectors;
- ray-versus-axis-aligned-wall intersection;
- nearest-wall selection (occlusion);
- perspective wall span top/bottom and wall colour selection.

The native host (`doom-host.c`) provides only host services:

- SDL window, keyboard input and timing;
- running pre-parsed Glon tick and column blocks;
- capturing the bounded `HOST_PRINT` integer stream;
- filling flat floor/ceiling colours;
- rasterising the Glon-provided wall spans into the 320x200 framebuffer;
- SDL texture upload and presentation;
- file loading and bounded stdout capture;
- measurements.

The host does not calculate player position, collision, ray direction, wall
intersection, distance, occlusion or wall span geometry.

## Projection and framebuffer

The logical framebuffer is 320x200. For interactive speed, the host asks Glon
to project 160 columns (2 framebuffer pixels per projected column); the host
expands each returned span across its two pixels. This is a documented
performance compromise, not a hidden native renderer.

Glon returns three integers per column: top, bottom and wall colour. The host
rasterises those vertical spans over the fixed floor and ceiling. Flat colours
and the fixed horizon are temporary G5a choices. No BSP, textures, floor
casting or vertical look are implemented.

## Tests

`make test` runs:

1. starting position and direction;
2. forward movement;
3. backward movement;
4. strafing;
5. turning;
6. collision against all ten linedefs, plus movement into the west, south,
   north, room-east, corridor-end and north-end boundaries;
7. corner collision at the room's southeast corner;
8. full corridor traversal and 90-degree turn;
9. fixed-tick determinism (state and framebuffer hash);
10. framebuffer span bounds and ordering;
11. centre/edge wall visibility and occlusion hierarchy;
12. repeated frames and repeated fresh sessions without loader growth;
13. frozen-S1 compliance (`check-frozen-s1.sh`).

The scripted `--trace` run is independent of interactive input:

```text
forward-to-turn  x=1200 y=320 angle=0
turn-left-90     x=1200 y=320 angle=64
corridor-forward x=1200 y=1000 angle=64
back              x=1200 y=840 angle=64
turn-right-90    x=1200 y=840 angle=0
```

No debug HUD is exposed; the trace uses only the same tick and report functions
as the game.

## Measured performance and memory

Measured on the WSLg test machine with the SDL Wayland backend and a 960x600
window. These are actual measurements, not estimates.

`DOOM_ROOT=../.. ./build/doom --bench 60`:

- simulation: **2.12 ms/tick**
- Glon wall projection: **122.13 ms/frame**
- native presentation: **7.94 ms/frame**
- total frame: **132.27 ms**
- wall-clock benchmark FPS: **7.6**

An interactive run reported the following range over 120-frame windows:

- **7.3–10.4 FPS**
- simulation total **7.5–11.7 ms/frame** (multiple 60 Hz ticks catch up)
- projection **81–122 ms/frame**
- presentation **7.2–8.5 ms/frame**
- total frame **96–138 ms**

Managed heap high-water was **33,248 cells**. No garbage collections occurred
during measured gameplay (`gc=0`, `live=0` after collection diagnostics); the
frame path allocates no managed objects. The loader frontier was **47,506
cells** and remained identical across three fresh sessions.

## Doom relationship

Temporary G5a choices:

- axis-aligned room/corridor union;
- flat colours and a fixed floor/ceiling horizon;
- ten direct linedef tests, no BSP;
- 160 sampled columns expanded to 320 pixels;
- discrete circle/wall collision with axis-separated sliding;
- no textures, sprites, doors, height changes, enemies or sound.

Correspondences useful for later milestones:

- vertices/linedefs/sectors are already the map vocabulary;
- the player camera and wall projection are the first renderer step;
- collision uses the same linedef geometry that later BSP/blockmap traversal
  will organise;
- replayable fixed ticks and deterministic geometry tests are suitable for
  later enemy/state experiments.

There is no WAD compatibility claim. No Doom source or assets are copied here.
Future work may add WAD parsing, BSP traversal, textures, sprite projection,
Linda-style enemy state and Doom gameplay logic, but none of that is G5a.

## Known limitations

- 160-column sampled projection produces 2-pixel vertical strips; the view is
  deliberately blocky compared with Doom.
- Flat wall/floor/ceiling colours; no lighting, textures or floor casting.
- No vertical look or mouse look.
- Axis-aligned wall intersection is used for the G5a map; arbitrary-angle
  linedefs need the general segment path and more performance work.
- Collision is discrete and axis-separated at 8 subunits/tick with radius 20.
  This prevents tunneling at the configured speed, but a future high-speed
  movement path needs swept collision tests.
- No BSP, blockmap, WAD, doors, platforms, sprites, enemies, weapons, sound,
  inventory, networking, menus, editor or texture packs.
