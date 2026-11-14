# G5q — Alien reference conversion

Branch/worktree: `glon-avp-g5p`, `/tmp/opencode/glon-avp-g5p`  
Base: `01f7b8cf8b6c1e83f9c4e1d0759c00a5697d6e1c`  
Commit: `1fcf5af8ea26b5a03418d379c320a9a88db4516a`

Source ZIP: `/mnt/c/Users/anon_/Downloads/glon-alien-g5q-reference.zip`  
Sheet: `assets/alien/alien-animation-reference.png`  
Reference README: `assets/alien/REFERENCE-README.md`

No new procedural creature was generated. Corridor, floor, lighting, gameplay
collision, Alien AI and frozen S1 were not changed. No push/merge/tag/deploy.

## 1. Asset inspection

- Sheet: 1536×1024 RGBA.
- Background alpha is 0; foreground alpha reaches 254, so the sheet does carry a
  transparency channel despite the reference README warning. It is still **not**
  a validated atlas: it includes black frame labels, an information box, variable
  spacing, and merged/clipped silhouettes in some rows.
- Alpha components were measured by row and column. Labels/info were excluded.
  Selected frames are isolated creatures, not a fixed-grid split.
- Candidate frames were rendered on a checkerboard:
  `/tmp/g5q-candidates.png`. No white/grey box or label contamination remains.

## 2. Conversion

`tools/convert_g5q_alien.py` performs the conversion. It selects five isolated
poses from clean alpha regions, removes the rest of the sheet, and:

- resizes RGBA in premultiplied space to avoid dark transparent fringes;
- uses one common scale (**0.8780**) across all five poses;
- aligns every pose to a common bottom-foot baseline;
- centers an alpha-weighted foot pivot;
- outputs **192×144** frames and the runtime `alien_sprites_g5q.h`.

Why 192×144 rather than the sheet's labelled 128×128: the source side profiles
include a long tail and are wider than tall. Forcing them into 128×128 clipped or
over-squashed the silhouette. The 4:3 canvas retains the supplied silhouette.

Frame contact sheet: `/tmp/g5q-frames.png`  
Checker/alpha sheet: `/tmp/g5q-checker.png`  
Individual files: `assets/alien/frames/{idle,walk1,walk2,attack,side}.png`

## 3. Integration

Replaced the include of the old procedural header with `alien_sprites_g5q.h`.
The existing `alien_frame_for()` mapping and timing are unchanged:

- state idle → `alien_idle`
- walking → alternating `alien_walk1` / `alien_walk2` on the same `sim_tick/8`
- attack → `alien_attack`
- far side visibility → `alien_side`

The GL billboard width was changed from 1.0 to **1.8** while height remains 1.5,
because the new 4:3 side-profile frame would otherwise be horizontally squashed.
This is a rendering-only change; Alien collision, position, detection, health,
state and timing are untouched. CPU fallback uses the same five replacement
textures and existing sprite rectangles.

Transparency is clean in-game at near, mid and attack distances; there is no
white rectangle or dark halo. Feet read on the deck baseline.

## 4. What was not converted

- Full idle (6), walk (8), turn (6), hit (4) and death (8) sequences.
- Per-frame pivot/foot-baseline metadata beyond the five selected poses.
- Directional/facing variants.
- A production sprite atlas with manually verified frame rectangles.

The existing Alien renderer only requests five frames (idle, two walk phases,
attack, side). Preserving its timing means those are the only frames replaced.
Turn/hit/death remain missing artwork for any future state expansion.

## 5. Verification

Screenshots:

- Frame contact: `/tmp/g5q-frames.png`
- Alpha/checker inspection: `/tmp/g5q-checker.png`
- Entrance/mid/far: `/tmp/g5q-final/01-entrance.png`,
  `/tmp/g5q-final/02-mid-corridor.png`,
  `/tmp/g5q-final/11-alien-tracker.png`
- Attack: `/tmp/g5q-final/15-alien-attack.png`
- Previous vs G5q: `/tmp/g5q-before-after.png`

Performance (960×600, 120 frames): **51.6 FPS**, sim 13.33 ms, submit 0.76 ms,
total 19.39 ms. Loader unchanged at **14,082 used / 718 free**; heap 33,600.
Executable **4,251,192 bytes**.

`make test` passes, including frozen S1
(`f90496c26dc45c7a387d8fc8639cd2781507d3d2`), animation frame selection,
input/turning, doors, collision/objects, tracker, resizing, renderer/texture
checks and Alien behaviour. `git diff --check` passes.

## 6. Remaining artwork

The five replaced poses are reference-derived, manually cropped and reasonably
aligned, but they are not a complete production animation set. Missing:
validated full idle/walk/turn/hit/death sequences, explicit per-frame pivots,
and any direction-specific views. Do not describe the supplied illustration as a
validated transparent sprite atlas.
