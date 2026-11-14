# G5n corridor — replacement art manifest (evaluation pack v0.1)

## Status
These three images are **art-direction prototypes**, cropped from an AI-generated material presentation sheet. They are **not certified seamless**, physically based, or final game-ready assets. The source sheet also contains visual concepts for the other eleven materials, but these are not supplied as textures. Do not mark the artwork gate passed.

Approved direction: weathered charcoal-blue steel, restrained oxidisation, industrial panel seams, realistic grime, low-saturation accents, warm recessed lighting, subtle floor detail. Keep original Glon-owned simulation and OpenGL renderer.

## Supplied files
| File | Size | Role | Acceptance status |
|---|---|---|---|
| `textures/wall_bulkhead_steel.png` | 1024×1024 | Main wall test | Prototype: edges not seamless |
| `textures/wall_recess_panel.png` | 512×512 | Recessed wall test | Prototype: panel-aligned, check UV |
| `textures/floor_plate.png` | 1024×1024 | Floor test | Prototype: edges not seamless |

## Remaining materials from original G5N-ASSETS.md
`rib_structural`, `service_housing`, `floor_grate`, `ceiling_panel`, `light_lens`, `pipe_metal`, `conduit_red`, `bulkhead_door`, `cargo_crate`, `cabinet_metal`, `drum_painted`.

## Integration rules
1. Preserve original asset identifiers and geometry. These images are **visual candidates only**; do not blindly tile them across a long corridor.
2. First render a single wall section and one floor section using these files, with screenshot comparison against the approved corridor reference.
3. Inspect seams, UV orientation, texture stretching, exposure, material scale and in-game readability. If seams are visible, stop and request proper tileable artwork; don't hide the problem by claiming these images seamless.
4. Don't bake a second set of light sources into textures; preserve existing dynamic ceiling pulse.
5. Keep the HUD, doors, controls, Alien behaviour, resize fix and frozen S1 intact.
6. Do not declare a visual-quality pass without user approval of actual in-game screenshots.

## Source
`material-concepts.png` is an **art-direction board**, not a texture atlas. Text, labels, and the apparent 3D crate/drum images on that board are not UV-ready textures.
