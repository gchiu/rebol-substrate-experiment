# Glon AvP — G5d: Genuine Texture Mapping

G5d replaces G5c's flat surfaces and projected line detail with pixel-textured
walls, floors, ceilings and a door. The Marine, door, Alien, motion tracker,
collision, fixed ticks and controls are unchanged.

## Build and run

From this directory:

```sh
make
DOOM_ROOT=../.. ./build/doom
```

Textured mode is the default. Press **R** to toggle simple flat rendering, or
start with:

```sh
DOOM_ROOT=../.. ./build/doom --simple
```

Headless tools:

```sh
./build/doom --self-test
./build/doom --trace
./build/doom --shots /tmp/g5d-shots
DOOM_ROOT=../.. ./build/doom --bench 60
DOOM_ROOT=../.. ./build/doom --bench 60 simple
```

Requires SDL2 (`libsdl2-dev`) and Python 3 for reproducible texture generation.

## Textures

Four original 64x64 pixel textures are generated deterministically at build
time by `tools/gen_textures.py` into `build/textures.h`:

- **Wall:** steel panels, seams, bolts, recessed vent slits and subtle wear.
- **Floor:** dark grating with raised structural bars, voids and wear.
- **Ceiling:** recessed panels, warm light strips, ventilation slots and seams.
- **Door:** amber industrial slabs, dark mechanical frame, beams, bolts and a
  centre seam.

No copied Doom/AvP or third-party texture pixels are used. The generator uses
only integer hashes and fixed drawing rules.

## Wall texture mapping

Glon still computes wall intersection, distance, material, world-surface U and
distance shade. The host samples the 64x64 texture using:

- horizontal U from Glon's world-space wall coordinate, repeating every four
  world units;
- vertical V from the projected wall span (top..bottom maps to 0..63);
- door U normalized across the known door width;
- deterministic distance shade 0..3 applied after texture lookup;
- subtle material tint for room, corridor and blue turn walls.

Walls, seams, bolts and door framing therefore remain attached to world
geometry while moving or turning.

## Floor and ceiling texture mapping

Glon generates a 200-row bounded perspective table. Each record contains:

```text
screen y, valid, world-base-x, world-base-y,
world-step-x-per-screen-pixel, world-step-y-per-screen-pixel, shade
```

The row transform uses the same camera direction/right camera plane and G5c
horizontal focal correction as walls. The native host loops pixels and samples
the floor or ceiling 64x64 texture at the resulting world coordinates. There is
no Glon interpreter invocation per pixel.

The host masks texture coordinates to 0..63 and applies the row's deterministic
shade. Horizon rows are marked invalid and keep the flat horizon base colour.

## Rendering modes

- **Textured (default):** genuine wall/floor/ceiling/door pixel textures.
- **Simple:** G5c-style flat shaded walls and flat floor/ceiling, no texture
  sampling or perspective floor table.
- **R** toggles between them at runtime.

Both modes retain the 160 sampled wall columns and 320x200 framebuffer. No
resolution reduction was introduced.

## Division of labour

Glon owns:

- camera, wall ray intersection, wall surface U and distance shade;
- door/Alien/tracker/occlusion state;
- the 200-row floor/ceiling perspective table, including shared camera
  conventions, world base/step and row shade;
- sprite rectangle and gameplay state.

The native host owns only:

- SDL input/timing/presentation;
- running pre-parsed Glon blocks and parsing bounded integer streams;
- sampling the generated 64x64 textures at Glon-supplied coordinates;
- writing framebuffer pixels;
- drawing the Alien rectangle and tracker HUD supplied by Glon;
- performance measurement.

The camera, world geometry and perspective transforms remain in Glon.

## Preserved gameplay

Unchanged: player speed/turning, collision geometry, door transitions,
Alien detection/movement/attack, Alien sprite, motion tracker, fixed 60 Hz
simulation, frozen S1, detailed/simple toggle and controls.

## Tests

`make test` preserves the G5a/G5b/G5c tests and adds texture checks:

1. wall texture U remains world-anchored under translation;
2. diagnostic wall U distribution and texture row validity at 0°/45°/90°/135°/
   180° camera states;
3. wall vertical V covers 0..63 across a span;
4. floor/ceiling rows are deterministic and valid;
5. texture row shade is bounded 0..3;
6. floor base row world coordinates align with the door wall base;
7. ceiling top row world coordinates align with the same wall top;
8. floor/ceiling row hashes change with translation and turning;
9. textured framebuffer hashes are deterministic and change with movement;
10. door visibility/collision remains consistent;
11. Alien occlusion over textured walls remains correct;
12. tracker behavior remains correct;
13. 160 wall columns × 5 fields, 200 texture rows, bounded texture indices;
14. repeated frames and fresh sessions show no loader growth;
15. frozen-S1 compliance.

The G5c diagnostic line block was retained as an eight-line projection
regression check; G5d's textured path no longer draws line detail.

## Measured performance

G5c baseline before G5d (recorded again):

- FPS **7.7**
- simulation 1.68 ms/tick
- wall projection 120.99 ms/frame
- presentation 6.59 ms/frame
- total frame 129.34 ms

G5d textured mode (final):

- FPS **6.1**
- simulation **1.65 ms/tick**
- Glon wall projection **101.80 ms/frame**
- Glon texture-row generation **52.51 ms/frame**
- native texture sampling/rasterisation **0.46 ms/frame**
- native presentation **6.60 ms/frame**
- total frame **163.02 ms**

G5d simple mode (final):

- FPS **8.5**
- simulation **1.72 ms/tick**
- wall projection **106.71 ms/frame**
- texture-row/status update **2.01 ms/frame**
- rasterisation **0.06 ms/frame**
- presentation **6.79 ms/frame**
- total frame **117.29 ms**

Managed heap high-water: **33,504 cells**; GC=0 and live=0 in measured frames.
Loader frontier: **54,729 cells**, stable across three fresh sessions.

The bottleneck is Glon wall projection plus row-coordinate generation, not
native texture sampling. Textured mode remains playable but choppy; simple mode
is retained for comparison.

## Screenshots

Final generated views:

- `/tmp/g5d-shots/room.png`
- `/tmp/g5d-shots/near-door.png`
- `/tmp/g5d-shots/corridor.png`
- `/tmp/g5d-shots/alien.png`
- `/tmp/g5d-shots/inside.png`
- `/tmp/g5d-shots/turn.png`
- contact sheet: `/tmp/g5d-contact-final.png`
- actual WSLg window: `/tmp/g5d-gui.png`

## Known limitations

- Textured mode is about **6.1 FPS**; disabling texture sampling alone does not
  recover G5c speed, because wall projection and 200-row generation dominate.
- 64x64 nearest-neighbour textures with no mipmaps or anisotropic filtering;
  distant surfaces can alias/shimmer.
- 160 sampled wall columns expanded to 320 pixels; pixel textures are genuine
  but horizontal sampling remains 2-pixel.
- Floor/ceiling row interface is 200 rows; it avoids per-pixel Glon calls at the
  cost of a substantial Glon row-generation pass.
- The loader is close to its frontier after the texture-row table; no larger
  texture format or per-pixel Glon path is possible without a different design.
- Door is manually toggled; Alien has no damage/death/animation/pathfinding.
- No BSP, WAD, weapons, sound, networking, menus or editor.
