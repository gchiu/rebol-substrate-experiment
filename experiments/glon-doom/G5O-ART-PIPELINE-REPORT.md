# G5o — corridor artwork installation and evaluation

Base/worktree: `glon-avp-g5n` at `98e96ad62b7f20d99a316cc73dc753bec24aca6a`.  
Art pack: `/mnt/c/Users/anon_/Downloads/glon-g5n-art-pack.zip`.

## 1. Installed pipeline

- Full-resolution supplied PNGs installed under `assets/corridor/`:
  - `wall_bulkhead_steel.png` 1024×1024
  - `wall_recess_panel.png` 512×512
  - `floor_plate.png` 1024×1024
- Replacement manifest installed at `G5N-ASSETS.md`. The original G5n manifest
  remains available in Git history (commit `98e96ad`).
- `tools/gen_corridor_textures.py` converts the assets into runtime `uint32_t`
  arrays in `corridor_textures.h`, downsampled to 512×512 to bound executable
  size. Source PNGs retain their supplied dimensions.
- No reference crops, generated replacement art, VM changes or additional eleven
  materials were added.

## 2. Integration

- `wall_recess_panel` is mapped once per recessed side-wall panel (one image
  per panel, separated by structural ribs), at correct upright V orientation.
- `wall_bulkhead_steel` is mapped once to the far bulkhead door face. The U
  direction was reversed after the first pass mirrored the A-3 sign; it is now
  upright/readable.
- `floor_plate` is mapped once per deck plate section, replacing the placeholder
  grate overlay. Placeholder drain channels remain.
- Existing geometry, doors, collision, Alien, tracker, HUD, lighting pulse,
  controls, renderer resize path and frozen S1 are unchanged.
- `--placeholder-corridor` reproduces the previous untextured greybox for A/B
  screenshots.

## 3. Visual evaluation

Material improvement is real but mixed:

- Side-wall recessed panels now read as weathered industrial panels with louvers
  and frames; this materially improves the corridor.
- The far bulkhead reads as a worn steel pressure door; the A-3 marking is
  upright and appropriately placed.
- The floor texture is integrated but contributes little: its square source is
  stretched over 36×100-unit plates, detail is low-contrast at game distance,
  and adjacent plates repeat the image. It is not a final floor solution.
- Source edge discontinuity measurements (mean absolute RGB across opposite
  edges): floor 44.9/104.8; recess panel 67.6/142.6; bulkhead 61.4/471.0.
  These are clearly **not seamless**, matching the supplied manifest.
- Panels are isolated by ribs so their non-seamless outer edges are less exposed;
  floor plates are adjacent and therefore show repeated plate boundaries. No
  filtering was used to hide the deficiency.
- No normal/Specular/roughness maps; no final PBR material response.
- Alien remains the prior functional stylized sprite and is still pending
  separate artwork. The corridor visual-quality gate remains open.

## 4. Screenshots

Before/after, 640×400 (`--placeholder-corridor` vs installed art):

- `/tmp/g5o-before-after2.png` (mid corridor, close wall, door open, crates,
  far bulkhead)
- `/tmp/g5o-art3/02-mid-corridor.png` (final mid view)
- `/tmp/g5o-art3/03-close-wall.png`
- `/tmp/g5o-art3/06-door-open.png`
- `/tmp/g5o-art3/07-crates-tracker.png`
- `/tmp/g5o-art3/08-far-bulkhead.png`

Larger window:

- `/tmp/g5o-before-after960.png`
- `/tmp/g5o-art960/` views
- Actual maximized WSLg (final): `/tmp/g5o-max2.png`

All are actual GL framebuffer/window captures, not mock-ups.

## 5. Performance

Same AMD Radeon/D3D12 WSLg path, 120 frames, objects active:

- 640×400: **37.6–56.4 FPS**, simulation ~12.6–15.9 ms,
  submit/finish ~1.3–5.2 ms.
- 960×600: **51.1 FPS**, simulation ~11.7 ms,
  submit/finish ~3.7 ms.

Loader remains **14,185 used / 615 free**; heap **33,600**; GC **0**.
Executable is **4,181,800 bytes**, dominated by the generated 512² texture
arrays (source PNGs remain separately stored).

## 6. Tests

`make test` passes: input, tracker, resize, objects, default/raw self-tests,
projection, stationary cache, texture/Alien checks and frozen-S1. No test was
weakened. The art pipeline itself is evaluated by visual screenshots above.

## 7. Remaining asset work

Required before a visual-quality pass:

- proper seamless, lower-stretch floor plate (or separately authored drainage/
  grate channels);
- final weathered wall/bulkhead materials with consistent texel density and
  non-repeating variation;
- the other eleven materials in `G5N-ASSETS.md`;
- normal/roughness/metallic response if the renderer is extended;
- replacement Alien artwork.

Do not proceed to the remaining eleven materials until these three are accepted
or replaced. The current art is a useful workflow validation, not final quality.
