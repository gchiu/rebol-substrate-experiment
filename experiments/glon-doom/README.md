# Glon AvP — G5c: Atmospheric Corridors

G5c keeps the G5b Marine, door, Alien, motion tracker and fixed-tick behaviour,
while replacing flat surfaces with a recognisable industrial corridor.

The world remains one room, one door and a corridor with a 90-degree turn. The
new work is visual depth: metallic wall panels, seams, structural bands,
floor/ceiling detail and distance shading. Player speed, turning speed and
Alien behaviour are unchanged.

## Build and run

From this directory:

```sh
make
DOOM_ROOT=../.. ./build/doom
```

Requires a C compiler and SDL2 development files (`libsdl2-dev` on Ubuntu).
Tested under WSLg / Ubuntu 22.04. `make run` is equivalent after building.

Rendering modes:

```sh
DOOM_ROOT=../.. ./build/doom          # detailed (default); press R to toggle
DOOM_ROOT=../.. ./build/doom --simple # start with flat shaded walls, no details
```

Headless tools:

```sh
./build/doom --self-test
./build/doom --trace
./build/doom --shots /tmp/g5c-shots
DOOM_ROOT=../.. ./build/doom --bench 60
DOOM_ROOT=../.. ./build/doom --bench 60 simple
```

`--shots` writes six deterministic PPM views: room, near-door, corridor,
alien, inside and turn.

## Controls

| Key | Action |
| --- | --- |
| W / S | move forward / backward |
| A / D | strafe left / right |
| Left / Right arrows | turn |
| E | open/close the door when near |
| R | toggle detailed/simple rendering |
| Escape | quit |

The controls are repeated in the window title.

## Wall detail

Every Glon wall column now reports:

- projected top and bottom;
- wall material (room steel, corridor steel, blue turn wall, amber door);
- world-space surface coordinate `u` along the wall;
- deterministic distance-shade level 0–3.

The native host rasterises those values:

- panel boundaries repeat every 128 world subunits;
- vertical seams appear at panel boundaries using `u mod 128`;
- every fourth panel gets a darker support-rib band;
- horizontal structural bands are fractions of the projected wall span;
- the door has a darker frame, top/bottom rails and amber panels;
- distance shading darkens the same material at 0/1/2/3 levels.

Because `u` is the actual wall-intersection coordinate, seams and ribs remain
anchored to the world when the player moves or turns. No screen-space scrolling
stripe is used.

## Floor and ceiling detail

Glon projects a bounded set of world-space 3D line segments:

- room and corridor floor-grid divisions;
- east/north corridor transverse grid lines;
- gold centreline markings and cyan edge lines;
- matching ceiling panel divisions;
- bright overhead light strips.

Each segment is projected with the same camera basis, including a near-plane
clip so lines crossing behind the camera still contribute their visible part.
The host rasterises only the projected endpoints. This is a low-resolution
line-detail pass, not a full texture-mapping engine.

The detail pass emits **38 lines** (a count plus six integers per line), even
when a line is off-screen. Wall projection emits **5 integers × 160 columns**;
status emits 16 integers. All are fixed, bounded command shapes.

## Projection alignment fix

The floor/ceiling line pass and wall raycaster originally used different
horizontal camera scaling.

Wall rays use the camera plane `(diry, -dirx) * 42/64`. A wall point with
subunit depth `D` and perpendicular right offset `s` projected at:

```text
x_wall = 160 + (160 * 64 / 42) * (s / D)  ~= 160 + 243 * (s / D)
```

The detail-line projector instead used the plane-scaled side value with a
factor of 160, effectively:

```text
x_line = 160 + 160 * (42 * s) / (64 * D) ~= 160 + 105 * (s / D)
```

Vertical projection (`z` and distance) already matched. The horizontal scale
was therefore compressed by roughly 2.3×, so floor and ceiling lines slid
against wall boundaries as the camera turned.

The common fix is in Glon's `doom/detail-line`:

- compute the right-side coordinate from the unit right vector
  `(diry, -dirx)`;
- use a horizontal focal length of 243, derived from the same 42/64 camera
  plane;
- keep the existing vertical projection 246 and near-plane clipping;
- use the corrected side units consistently for both endpoints.

This changes one shared camera transformation, not cosmetic line placement.
Wall geometry, collision, player movement speed, door/Alien behaviour and the
motion tracker are untouched.

Diagnostic alignment tests place a floor line on the door's base and a ceiling
line on the same wall's top, then compare against the rendered wall boundary
at multiple camera positions and angles. Six on-screen, unobstructed cases
(angles 0°, 45° and 180°) align within four framebuffer pixels. The tested
90°/135° door views are occluded or off-screen at the chosen positions, so they
are reported as skipped rather than forced. Near-plane clipping and reversed
endpoint ordering are also tested.

Before/after views use identical camera states:

- `/tmp/g5c-before-fix/`
- `/tmp/g5c-after-fix/`
- comparison: `/tmp/g5c-before-after.png`

## Distance shading

Glon chooses a shade level from the wall intersection distance:

```text
distance < 256 subunits  -> shade 3
distance < 512 subunits  -> shade 2
distance < 1024 subunits -> shade 1
otherwise                -> shade 0
```

The host maps these to deterministic brightness levels (100/82/64/48 percent)
while keeping the amber door and green Alien readable. Shading is computed in
Glon and contains no time-dependent component.

## Division of labour

Glon owns:

- camera, ray, world geometry and occlusion;
- wall intersections and world-surface coordinate `u`;
- distance-shade decisions;
- 3D floor/ceiling line endpoints and near-plane clipping;
- door, Alien, line-of-sight, sprite rectangle and tracker calculations;
- all movement and collision.

The native host only:

- supplies SDL input/timing/presentation;
- runs pre-parsed Glon blocks;
- parses bounded integer streams;
- rasterises wall spans using Glon `u`/shade/material;
- rasterises the Glon-projected floor/ceiling line segments;
- draws the Alien rectangle supplied by Glon and the tracker HUD;
- measures performance.

No raycasting, wall intersection, surface coordinate or perspective calculation
was moved into C.

## Rendering modes and performance

Detailed mode adds the wall pattern and floor/ceiling line pass. Simple mode
skips the detail block and wall patterns, retaining flat shaded walls, the door,
Alien and tracker.

G5b baseline, measured again before G5c changes:

- FPS **7.9**
- simulation **2.00 ms/tick**
- projection **115.07 ms/frame**
- presentation **8.80 ms/frame**
- total frame **125.98 ms**

Pre-fix G5c detailed/simple were **6.9/8.1 FPS**. After the shared projection
correction:

G5c detailed mode (final):

- FPS **7.6**
- simulation **1.83 ms/tick**
- wall + detail + status projection **122.64 ms/frame**
- native presentation **7.03 ms/frame**
- total frame **131.59 ms**

G5c simple mode (final):

- FPS **8.4**
- simulation **1.79 ms/tick**
- projection **110.63 ms/frame**
- presentation **6.88 ms/frame**
- total frame **119.37 ms**

Managed heap high-water: **33,440 cells**. No GC cycles in measured frames
(`gc=0`, `live=0`). The loader frontier was **54,146 cells** and identical
across three fresh sessions.

Detailed mode is slower than G5b but remains playable for evaluating movement
and space; simple mode is available when interactivity matters more than
surface detail. No general VM or renderer optimisation was attempted.

## Tests

`make test` preserves all G5a/G5b tests and adds:

1. world-anchored wall panel coordinates (`u` is world surface, not screen x);
2. stable panel anchoring under forward translation;
3. projected seam distribution across wall surfaces;
4. floor detail hash changes with camera translation;
5. ceiling detail hash changes with turning;
6. deterministic shade levels and frame hashes;
7. door collision/visibility remains consistent;
8. Alien occlusion and line-of-sight remain correct;
9. motion tracker direction/distance/detection remain functional;
10. framebuffer and command-shape bounds, including 38 detail lines and
    5×160 wall fields;
11. identical world state produces identical wall/detail hashes;
12. floor line on a wall base and ceiling line on the same wall top align
    with the rendered wall boundary wherever visible/unobstructed;
13. near-plane clipping and reversed endpoint ordering;
14. repeated sessions have no loader growth;
15. frozen-S1 compliance.

Also preserved: all ten linedef collision probes for player and Alien, corridor
traversal, Alien waiting/approach/attack and repeated simulation determinism.

## Verification screenshots

Deterministic WSLg-path outputs from `--shots /tmp/g5c-shots`:

- `/tmp/g5c-shots/room.png`
- `/tmp/g5c-shots/near-door.png`
- `/tmp/g5c-shots/corridor.png`
- `/tmp/g5c-shots/alien.png`
- `/tmp/g5c-shots/inside.png`
- `/tmp/g5c-shots/turn.png`

Contact sheet: `/tmp/g5c-contact-final5.png`.

## Doom relationship

G5c remains a precursor, not a WAD-compatible Doom renderer. It now has:

- Doom-like vertex/linedef/sector geometry and a door segment;
- material IDs and world surface coordinates suitable for later texture UVs;
- bounded floor/ceiling detail projected through the same camera;
- deterministic distance shading and fixed-tick entity state.

Still temporary:

- low-resolution line detail instead of texture mapping;
- 160 sampled columns expanded to 320 pixels;
- axis-aligned wall tests;
- flat floor/ceiling base;
- manually toggled door threshold;
- simple billboard and tracker rendering.

No Doom source or assets are copied here.

## Known limitations

- Detailed rendering runs at about **7.6 FPS**; simple mode about **8.4 FPS**.
- Wall panels are low-resolution 2-pixel strips; seams/ribs can alias at grazing
  angles.
- Floor/ceiling detail is line work, not textured floor casting; grid coverage
  is deliberately sparse.
- Near-plane clipping can extend projected lines toward the screen edges.
- Simple mode still computes Glon projection/surface data but skips detail
  rasterisation.
- Door is a manual binary threshold; Alien is range-based with no damage,
  death, animation or pathfinding.
- No BSP, WAD, textures, sprites beyond the single Alien, weapons, sound,
  inventory, networking, menus or editor.
