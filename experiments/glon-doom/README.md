# Glon Doom — G5b: The Marine and the Alien

This extends G5a's first-person room/corridor into a small encounter:

- one working door between the room and the corridor;
- one deterministic Alien that detects and approaches the Marine;
- a billboard Alien silhouette rendered from Glon-calculated projection data;
- a Marine motion tracker drawn by the native host;
- deterministic tests for the door, Alien, tracker and existing G5a world.

No enemies beyond the single Alien, weapons, damage, sound, textures, BSP or
WAD loading are added.

## Build and run

From this directory:

```sh
make
DOOM_ROOT=../.. ./build/doom
```

Or `make run`. Requires a C compiler and SDL2 development files
(`libsdl2-dev` on Ubuntu). Tested under WSLg / Ubuntu 22.04.

Headless tools:

```sh
./build/doom --self-test
./build/doom --trace
DOOM_ROOT=../.. ./build/doom --bench 60
```

`DOOM_ROOT` defaults to `../..`. The host loads `jupyter/prelude.glon` and
`experiments/glon-doom/doom.glon`.

## Controls

| Key | Action |
| --- | --- |
| W | move forward |
| S | move backward |
| A | strafe left |
| D | strafe right |
| Left arrow | turn left |
| Right arrow | turn right |
| E | open/close the door when near it |
| Escape | quit |

The window title repeats the controls. Simulation is fixed at 60 ticks/second
with catch-up (up to 10 ticks per display frame).

## World and door

World units use 64 integer subunits per map unit. The one sector has floor z=0,
ceiling z=128, floor colour 6 and ceiling colour 7.

```text
                      north corridor
                      x=1152..1280
                      y=384..1024
                            |
 room x=128..768      east corridor x=768..1280
 y=128..640           y=256..384
       door at x=768, y=256..384
```

Ten solid boundary linedefs form the room/corridor union. The door is one
dynamic vertical segment across the room/corridor opening:

- starts closed (`door-open=0`, `door-blocked=1`, `door-target=0`);
- E within 160 subunits of the door centre toggles the target;
- opens/closes in 4-subunit steps, fully open at 64;
- becomes passable/transparent at `door-open >= 32`;
- is collision-solid and visible as colour 4 while blocked;
- appears as the same wall segment to player collision, Alien collision and
  line-of-sight tests.

The door is toggled manually; there is no automatic close or door-use animation.
Visibility switches at the collision threshold and remains consistent with it.

## Alien

The Alien's conceptual Linda-style tuple is:

```text
(enemy 1 alien x y health state)
```

Its fields are maintained in Glon globals so fixed ticks need no mutation
opcode or VM primitive.

Initial values:

```text
x=1120  y=320  health=100  state=0
```

States are `0` idle, `1` detected/approaching or blocked by the door, and `2`
attack/stop in range. Detection range is 700 subunits; attack range is 96
subunits. The Alien moves at 2 subunits/tick with axis-separated circle/wall
collision (radius 16). It waits behind a closed door rather than crossing or
attacking through it. Once the door is sufficiently open it approaches and
stops at attack range. There is no damage, death or enemy animation in G5b.

## Rendering the Alien

Glon calculates:

- relative vector, forward depth and side offset;
- whether the Alien is in front of the camera and inside the horizontal FOV;
- line of sight through every wall and the closed door;
- projected screen rectangle and visibility.

Only when Glon reports visible does the host rasterise a simple original
silhouette inside that rectangle (green body/head outline, red eyes). The host
does not calculate the Alien's position, depth, occlusion or screen rectangle.
This is a billboard sprite, not a general 3D model renderer.

## Motion tracker

Glon reports door state, Alien position/state/health, detection, squared
distance (`td2`), relative side (`tside`) and forward depth (`tdepth`), plus
sprite visibility. The native host draws the tracker ring in the lower-left:

- forward tick at the top;
- door-status bar (green open, amber blocked);
- Alien blip positioned from `tside`/`tdepth`, green when approaching and red
  in attack range;
- a dim centre mark when not detected.

The tracker updates from the same fixed-tick state as the game.

## Division of labour

Glon (`doom.glon`) owns:

- vertices/linedefs/sector data;
- player state, fixed-tick movement, strafing/turning and collision;
- door state, transitions, activation and collision/visibility gating;
- Alien state, detection, approach, wall/door collision and attack state;
- line-of-sight checks;
- sprite relative position, screen rectangle and visibility;
- motion-tracker relative direction/distance/detection values;
- projection of wall spans.

The native host (`doom-host.c`) owns only:

- SDL window, input/timing and E-key event delivery;
- running pre-parsed Glon tick/column/status blocks;
- parsing bounded `HOST_PRINT` output;
- rasterising Glon wall spans and the Glon-provided Alien rectangle;
- drawing the motion-tracker HUD from Glon values;
- SDL presentation and measurements.

The host does not simulate player, door or Alien behaviour.

## Projection

The logical framebuffer remains 320x200. Projection is sampled at 160 columns
(2 framebuffer pixels each) and expanded by the host. Renderer performance was
not optimised in G5b. Flat colours, fixed horizon, no textures, no BSP and no
floor casting remain.

## Tests

`make test` runs the preserved G5a checks plus G5b coverage:

- player start/direction, forward/backward, strafing, turning;
- collision against all ten linedefs and room/corridor corners;
- closed-door collision and open-door passage;
- door open/close transitions, state, visibility and Alien occlusion;
- Alien initial tuple state;
- detection range;
- Alien waiting behind a closed door;
- Alien approach after opening;
- Alien walls/door collision, with direct collision probes for all ten linedefs;
- Alien/player proximity and attack state;
- Alien line-of-sight blocked by a wall and clear through an open doorway;
- sprite projection bounds and door occlusion;
- tracker detection, direction and distance;
- repeated simulation determinism including status;
- framebuffer span bounds;
- repeated frames/fresh sessions without loader growth;
- frozen-S1 compliance.

The scripted trace is input-independent:

```text
approach-closed-door x=744 y=320 door=0/1 alien=1120,320 vis=0
activate-door        x=744 y=320 door=48/0 alien=1110,320 vis=1
observe-alien        x=744 y=320 door=64/0 alien=990,320 vis=1
enter-corridor       x=904 y=320 door=64/0 alien=960,320 state=2
```

## Measured performance and memory

Measured on WSLg with the SDL Wayland backend, 960x600 window:

`DOOM_ROOT=../.. ./build/doom --bench 60`:

- simulation: **1.79 ms/tick**
- wall + status projection: **99.04 ms/frame**
- native presentation: **7.17 ms/frame**
- total frame: **108.09 ms**
- measured FPS: **9.2** (frame metric 9.3)

Interactive runs with door/Alien/tracker active:

- **9.6–9.7 FPS**
- simulation: **9.0 ms/frame** (multiple 60 Hz ticks catch up)
- projection/status: **88 ms/frame**
- presentation: **6 ms/frame**
- total frame: **103 ms**

Managed heap high-water: **33,408 cells**. No GC cycles occurred in measured
frames (`gc=0`, `live=0`); the frame/status path allocates no managed objects.
Loader frontier was **53,410 cells** and identical across three fresh sessions.

## Doom relationship

G5b still does not load WAD files or claim Doom map compatibility. The useful
correspondences are:

- vertices/linedefs/sectors and a door segment;
- a player camera, wall span projection and billboard sprite depth rectangle;
- fixed-tick deterministic entity state (Alien) independent of the player;
- line-of-sight and collision against the same map geometry.

Temporary G5b choices include axis-aligned wall tests, a manually toggled door,
range-based detection, simple axis-step Alien movement, a flat-colour renderer
and a HUD-style tracker. Future work may add WAD/BSP/blockmap traversal,
textures, doors with real sector movement, sprite animation, damage and
Linda-style enemy tuple spaces.

No Doom source or assets are copied here.

## Known limitations

- 160 sampled columns produce 2-pixel blocky vertical strips.
- Flat colours, fixed horizon and no floor/ceiling detail.
- No renderer optimisation in this milestone; FPS is under 10.
- Door is a binary visibility/collision threshold at 50% travel, not a
  textured sliding animation.
- Alien movement is simple axis-separated stepping; no pathfinding; it may
  stop at corners in future maps.
- Alien detection is range-based and does not include a hearing/FOV model.
  Attack is a state only; there is no damage or death.
- No mouse look, weapons, sound, inventory, multiple enemies, BSP, WAD,
  textures, networking, menus or level editor.
