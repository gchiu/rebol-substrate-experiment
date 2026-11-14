# G5s — Alien directional asset-quality gate

Date: 2026-11-14  
Source pack: `/mnt/c/Users/anon_/Downloads/glon-alien-g5s-directional-reference.zip`  
Extracted source: `assets/alien/directional/glon-alien-g5s-pack/`

## Sheet inspection

| Sheet | Dimensions | Mode | Alpha range | Empty-background alpha |
|---|---|---|---|---|
| `alien-directional-sheet-a.png` | 1536×1024 | RGBA | 0–254 | 0, with faint noise up to 36 |
| `alien-directional-sheet-b.png` | 1536×1024 | RGBA | 0–254 | 0, with faint noise up to 36 |

The sheets are nominally transparent but are **not clean production atlases**.
The RGB channels contain dark values under fully transparent pixels, and there is
low-alpha noise across the background. Extraction therefore uses alpha-component
bounds plus a low-alpha cleanup, not the full sheet or a fixed cell grid.

Pose components were segmented at alpha >64. This produced 40 isolated components
in sheet A and 26 in sheet B. No chosen pose overlaps another selected pose.
Some tails run close to neighbouring figures, so every crop below is based on the
component's own alpha bounds, with only a small antialias margin.

## Selected poses

| Role | Sheet | Source rectangle (x0,y0,x1,y1) | Effective pixels (w×h) | Notes |
|---|---|---:|---:|---|
| Frontal idle | A | 48, 8, 181, 196 | 133×188 | Neutral frontal stance, closed mouth |
| Frontal approach A | B | 911, 11, 1047, 234 | 136×223 | Upright, left leg planted; distinct step |
| Frontal approach B | B | 1234, 13, 1365, 230 | 131×217 | Opposite/raised-leg step; distinct silhouette |
| Three-quarter approach | B | 287, 255, 504, 469 | 217×214 | Right-facing three-quarter lope |
| Side movement (right-facing) | B | 288, 501, 542, 690 | 254×189 | Full side run with tail |
| Rear movement | B | 179, 732, 318, 1000 | 139×268 | Back view, tail down corridor |
| Frontal attack | B | 1090, 737, 1299, 986 | 209×249 | Open-mouth forward attack |

The two frontal approach poses are not duplicates. After bottom alignment and
centre alignment, their alpha silhouettes have IoU **0.643** and mean overlapping
RGB difference **26.5**; B5/B6 and B7/B8 similarly differ. This is enough for a
two-pose alternating gait, but not a smooth multi-frame animation.

## Alpha and anatomy checks

- Chosen rectangles are component bounds, not 1536×1024 sheet exports.
- No selected rectangle includes an adjacent figure.
- No selected pose has a cropped head, foot or tail.
- The frontal poses share the same general anatomy, but hand/tail placement varies
  because they are separate generated poses rather than an animation rig.
- The side pose is right-facing only. A left-facing crossing will use a horizontal
  mirror of that side artwork, **not** a rotation and **not** a false frontal view.
- No sharpening, generative detail or upscaling is planned.

## Resolution gate

All poses will be extracted at **1:1 source pixels** with no resampling. The
eventual world billboard will map each pose's tight effective height to the same
1.5-world-unit height so the creature does not change apparent size by pose.

Approximate displayed heights at 960×600 use the existing projection
(`fovy = 2·atan(100/246)`): `screen_px ≈ 1.845 × 600 / depth_world`.

| Pose | Source height | Output scale | Displayed height at tdepth 96 (≈1.5 world) | at tdepth 256 (4 world) |
|---|---:|---:|---:|---:|
| Frontal idle | 188 | 1.00× | ≈738 px (clipped by viewport) | ≈277 px |
| Frontal approach A | 223 | 1.00× | ≈738 px | ≈277 px |
| Frontal approach B | 217 | 1.00× | ≈738 px | ≈277 px |
| Three-quarter | 214 | 1.00× | ≈738 px | ≈277 px |
| Side | 189 | 1.00× | ≈738 px | ≈277 px |
| Rear | 268 | 1.00× | ≈738 px | ≈277 px |
| Frontal attack | 249 | 1.00× | ≈738 px | ≈277 px |

At attack/near range the 188–268 px source is stretched to roughly 600–740 screen
pixels, i.e. about **2.2–3.9× magnification**. This is expected to remain visibly
soft. **The close-up resolution gate is not satisfied by the supplied artwork.**
Directional acceptance can still be evaluated separately; genuinely sharp close-ups
require individually rendered 768–1024 px pose sprites.

## Reproduction

- Original PNGs: `assets/alien/directional/glon-alien-g5s-pack/`
- Extraction script: `tools/convert_g5s_alien.py`
- Extracted per-pose PNGs: `assets/alien/directional/extracted/`
- Generated runtime header: `alien_sprites_g5s.h`

The script records source rectangles, alpha cleanup, pivot and ground-baseline
metadata, and writes no resampled pixels.
