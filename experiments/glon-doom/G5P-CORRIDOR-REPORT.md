# G5p — Corridor visual upgrade

Branch/worktree: `glon-avp-g5p`, `/tmp/opencode/glon-avp-g5p`  
Base: `a3886fcf69910a0fcbac7b5ee0b3a9de4838cd8d`  
Commit: `63685291513613b7a2cc9424bad2985d7f42558f`

Reference pack: `/mnt/c/Users/anon_/Downloads/glon-g5o-corridor-visual-references.zip`
(`corridor-reference-01/02/03.png`). These were used only as visual direction;
no regions were cropped or presented as seamless game textures.

## Changed files

- `experiments/glon-doom/doom-host.c`
- `experiments/glon-doom/objects.glon`
- `experiments/glon-doom/README.md`
- `experiments/glon-doom/G5P-CORRIDOR-REPORT.md`
- `GLON-ARCHITECTURE-MEMORY.md`, `MEMORY.md`

## 1. What was implemented

### Ceiling and lighting

Replaced the flat peach ceiling rectangles with recessed fixtures containing:

- dark ceiling recess and perimeter frame boxes with actual depth;
- a smaller warm lens;
- protective grille bars in front of the lens;
- adjacent vent-slat geometry.

The existing Glon `light-phase` pulse still drives lens intensity. No shader or
renderer rewrite was made.

### Floor

- Removed the prominent placeholder black grid from art mode.
- Applied the existing G5o floor texture as **one world-scaled deck surface** with
  UVs tied to 128-unit plates (four plate spans along the corridor, one across),
  instead of stretching an image over each 36×100 rectangle.
- Added plate seams, a central drainage channel and narrow drainage grille bars
  along both corridor edges.
- `--placeholder-corridor` still renders the original greybox floor for A/B
  comparison.

The floor still uses the G5o prototype material. Its source edges are not
seamless, and the material is lower-contrast than the reference. This remains
an artwork dependency.

### Props and composition

- Reduced object-record dimensions while keeping them as the single source of
  visual and collision truth. Records are still the original six Glon objects;
  no separate object state was introduced.
- Repositioned the first drum by ten world units to keep the existing gameplay
  collision test valid after downsizing.
- Props remain against the side walls, leaving the centre walking lane clear.
- Added restrained colours, crate hazard bands, drum bands, cabinet vent/doors
  and container service details.
- Kept the visible far bulkhead and the existing A-3 artwork.
- No Alien redesign.

### Preserved

Frozen seven-primitive S1 VM, Glon movement/turning/door/collision state,
object records, circular tracker, crosshair, resizable window, existing
OpenGL backend, G5o wall artwork, Alien behaviour and lighting pulse.

## 2. Reference details reproduced

- Framed ceiling light housings with grilles and dark recesses.
- Warm lights against dark steel.
- Long central deck plates with panel seams and edge drainage.
- Centre of corridor kept clear; crates/drums/cabinets against walls.
- Structural ribs and a visible destination bulkhead.

The references are more detailed and wider. The implementation is a geometric
approximation, not a copy of the illustrations.

## 3. Material status

| Status | Item |
|---|---|
| Supplied prototype artwork | G5o bulkhead, recessed-wall panel and floor textures. Not certified seamless/final. |
| Newly prepared textures | None in G5p; the additions are geometry and material colours. |
| Temporary placeholders | Ceiling lens/frame colour, lamp grille, deck seams/drains, ribs, pipes, prop surfaces. |
| Missing artwork | Final seamless deck material, final ceiling/light-lens/vent textures, prop albedo/roughness materials, Alien artwork. |

No reference crop was used as a texture. No new textures were fabricated in C.

### Explicit asset requests

1. **Deck plate material**, 1024×1024, seamless, physically one plate per
   128×128 world units, with restrained rust/grime and no directional text.
2. **Ceiling light insert**, 512×512, warm diffuser plus grille (or separate
   emissive lens and grille masks), tileable within one fixture.
3. **Ceiling recess/vent panel**, 512×512, dark industrial steel, seamless.
4. **Prop materials**, 512×512 each: crate painted metal, red drum, grey
   machinery cabinet, service container; restrained wear, no baked perspective.

## 4. Screenshots

Actual 960×600 framebuffer captures:

`/tmp/g5p-final960c/`

Required views:

1. `/tmp/g5p-final960c/01-entrance.png`
2. `/tmp/g5p-final960c/02-mid-corridor.png`
3. `/tmp/g5p-final960c/03-close-wall.png`
4. `/tmp/g5p-final960c/09-floor-detail.png`
5. `/tmp/g5p-final960c/10-ceiling-lights.png`
6. `/tmp/g5p-final960c/07-crates-tracker.png`
7. `/tmp/g5p-final960c/05-door-closed.png`
8. `/tmp/g5p-final960c/06-door-open.png`
9. `/tmp/g5p-final960c/04-look-back.png`
10. `/tmp/g5p-final960c/11-alien-tracker.png`

Contact sheet: `/tmp/g5p-tour-final-contact.png`

Actual maximised WSLg window (player mid-corridor): `/tmp/g5p-max-final3.png`

G5o/G5p matching-camera comparison sheet:
`/tmp/g5p-before-after-final.png` (G5o on the left, G5p right).

Views 4 and 5 use a **render-only diagnostic pitch** to show the floor and
ceiling. Gameplay camera/pitch is unchanged.

## 5. Performance and memory

Measured on WSLg D3D12, AMD Radeon integrated:

- 960×600: **32.2 FPS**, total **31.01 ms** (sim 23.09 ms, submit 4.22 ms)
- 640×400: **32.8 FPS**, total **30.49 ms** (sim 23.88 ms, submit 3.26 ms)
- Loader after host: **14,082 used / 718 free**; code 12,454; heap 33,600; GC 0.
- Executable: **4,185,600 bytes**.

This is below G5o's approximately 51 FPS at 960×600. The extra immediate-mode
boxes/texture work increased submission and simulation time. No safe batching
or display-list rewrite was attempted in this milestone; further optimisation
would need a focused geometry-batching pass. The measured 32 FPS is playable but
is a regression against G5o.

## 6. Tests

`make test` passes after the object-record adjustment:

- object record count/determinism, player object collision, alien object collision;
- frozen S1 (`f90496c26dc45c7a387d8fc8639cd2781507d3d2`);
- input/turning, doors, collision, tracker;
- window resizing;
- renderer/texture checks and Alien behaviour;
- projection, stationary cache, raw/default self-tests.

An initial run failed `alien object collision` after aggressive prop shrinking;
the first drum record was repositioned so visual and collision truth remain
consistent and the original test passes unchanged.

## 7. Remaining artwork dependencies

The G5p corridor is geometrically closer to the references: framed ceiling
lights, scaled deck plates with edge drainage, smaller side-mounted props and a
clear centre lane. It is **not** visually final. The largest remaining gaps are
the non-seamless floor material, flat placeholder ceiling/prop surfaces, and the
untextured light lens/grille. Final seamless assets listed above are required
before claiming visual acceptance.
