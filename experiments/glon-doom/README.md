# Glon AvP — G5e: Industrial Artwork Integration

G5e replaces G5d's noisy generated textures with artwork extracted from the
supplied industrial contact sheet. The texture mapper, geometry, collision,
player, door, Alien and tracker are unchanged.

## Artwork source

Reference:

```text
sprites/Glon_AvP_Industrial_Texture_Set.png
```

The sheet is 1536x1024 and is preserved byte-for-byte. Its SHA-256 is
`b677ed632f117d60a5cab21246af27db72a384df4b2a5d53d1529aec0dfde412`.

Extracted panels, all 64x64 game assets:

| Asset | Source panel |
| --- | --- |
| `wall_panel.png` | W1 Panel (A) |
| `wall_service.png` | W4 Service Panel |
| `vent_grille.png` | W3 Grille / Vent |
| `rib.png` | W6 Ribbed Support |
| `pipes.png` | W7 Pipes / Conduit |
| `floor.png` | F1 Grating |
| `ceiling_light.png` | C1 Light Panel |
| `ceiling_panel.png` | C2 Ceiling Plate |
| `door.png` | D1 Closed |

The extraction in `tools/extract_textures.py` uses interior artwork rectangles;
labels, magenta frames, palette swatches and example corridor renders are
excluded. PNG crops are nearest-neighbour resized to 64x64 and stored under
`sprites/textures/`. `textures.h` embeds the resulting pixels for the build.

## Material assignment

Existing wall linedefs retain their geometry and colour but now carry a
deterministic material ID:

| Linedef | Colour / role | Material |
| --- | --- | --- |
| 0 | room south | panel |
| 1 | room east lower | service panel |
| 2 | corridor south | vent grille |
| 3 | east end | pipes |
| 4 | north end | panel |
| 5 | north west | rib |
| 6 | corridor north | vent grille |
| 7 | room east upper | service panel |
| 8 | room north | panel |
| 9 | room west | rib |
| door | amber bulkhead | door |

Material IDs are packed with the shade level into one bounded wall field:
`meta = material * 4 + shade`. Glon still returns top, bottom, colour, U and
this material/shade field.

## Mapping and architecture

Glon calculates:

- wall rays, intersections, world-space U, distance shade and material;
- the 200-row floor/ceiling perspective table with world base, per-pixel step,
  validity and shade;
- door, Alien, line-of-sight, sprite rectangle and tracker state.

The host only:

- selects the artwork texture from the Glon material ID;
- samples 64x64 texels at Glon-supplied U, vertical V, or floor/ceiling world
  coordinates;
- writes framebuffer pixels;
- draws the Alien rectangle and tracker HUD;
- handles SDL input/timing/presentation.

Wall U remains fixed in world space, including during translation and turning.
Door U is normalized across the bulkhead width. Floor/ceiling world coordinates
remain anchored through the shared G5c camera/focal convention.

## Rendering modes

- **Textured (default):** artwork textures for walls, floors, ceilings and door.
- **Simple:** flat-shaded G5c-style surfaces.
- **R** toggles; `--simple` starts flat.

No resolution change: wall projection remains 160 sampled columns expanded to
320 pixels.

## Tests

`make test` preserves all G5a-G5d tests and adds:

- contact-sheet SHA validation and all nine asset dimensions/modes;
- texture-row count, validity, shade bounds and deterministic hashes;
- wall material bounds and deterministic material selection;
- door material selection;
- world-anchored U, wall V mapping 0..63 and floor/ceiling row alignment with
  the door wall base/top;
- row changes under translation/turning;
- deterministic textured framebuffer hashes and movement changes;
- door collision/visibility, Alien occlusion and tracker preservation;
- bounded texture indices, 160x5 wall fields, 200 rows and loader stability;
- frozen-S1 compliance;
- preserved G5a-G5d gameplay/collision/projection tests.

The G5c diagnostic detail block remains an eight-line regression check; it is
not used by the textured renderer.

## Performance

Same-environment G5d comparison:

| Mode | G5d | G5e |
| --- | ---: | ---: |
| Textured FPS | 5.2 | 4.9 |
| Simple FPS | 7.1 | 7.3 |

G5e textured mode (final):
- simulation **2.20 ms/tick**
- Glon wall projection **130.38 ms/frame**
- Glon floor/ceiling rows **62.86 ms/frame**
- native texture sampling **0.54 ms/frame**
- presentation **8.10 ms/frame**
- total **204.07 ms**, **4.9 FPS**

G5e simple mode:
- simulation **2.05 ms/tick**
- wall projection **125.00 ms/frame**
- row/status **2.32 ms/frame**
- rasterisation **0.07 ms/frame**
- presentation **7.63 ms/frame**
- total **137.07 ms**, **7.3 FPS**

Managed heap high-water: **33,504 cells**; GC=0 and live=0 in measured frames.
Loader frontier: **54,729 cells**, stable across sessions. Texture sampling is
not the bottleneck; Glon wall/row generation dominates.

## Screenshots

- `/tmp/g5e-shots/room.png`
- `/tmp/g5e-shots/near-door.png`
- `/tmp/g5e-shots/corridor.png`
- `/tmp/g5e-shots/alien.png`
- `/tmp/g5e-shots/inside.png`
- `/tmp/g5e-shots/turn.png`
- contact sheet: `/tmp/g5e-contact-final.png`
- G5d/G5e same-view comparison: `/tmp/g5e-vs-g5d.png`
- actual WSLg window: `/tmp/g5e-gui-final.png`

## Known limitations

- Textured mode is about **4.9 FPS**; simple mode is about **7.3 FPS**.
- Textures are 64x64 nearest-neighbour panels; no mipmaps or filtering.
- The panels are independent tiles upscaled from a contact sheet, not bespoke
  128x128 material maps; some repetition and tiling seams remain visible.
- Wall projection and 200-row floor/ceiling generation dominate frame time,
  not native texture sampling.
- Loader capacity is nearly exhausted; no larger texture/table format fits
  without redesign.
- Door/Alien behaviour, collision and gameplay are unchanged.
- No BSP, WAD, weapons, sound, networking, menus or editor.
