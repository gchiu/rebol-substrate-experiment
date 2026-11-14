# G5x — High-Resolution Sliding Blast Door

**Status: implemented in OpenGL.**

The closed door now uses native-resolution forged blast-door leaf artwork, an
8-unit-thick two-leaf slab, a deep casing with side receiving pockets, and an
opening animation driven directly by Glon's `door_open`. The previous 64×64
door texture and flat 2-unit box are gone from the OpenGL path.

**Branch/worktree:** `glon-avp-g5x` at `/tmp/opencode/glon-avp-g5x`.
**Starting point:** G5w report `53044b610fff2eb4eb507c1afba181701a2f2045`.
**G5x implementation:** `6800f5fd4c1b0ebcf3a2f32b7e4ae0c77fa61da7`.
**G5x report commit:** this commit.

No push, merge, tag or deploy. Frozen S1, the VM, Glon door state/collision and
all G5u/G5s/G5r/G5v/G5w work are unchanged.

## 1. Artwork used

The supplied concept sheet was found at
`/mnt/c/Users/anon_/Downloads/door_texture.png` (the requested `/mnt/d/users/...`
path does not exist on this host). The untouched sheet is committed as
`assets/doors/reference/blast-door-reference.png`, 1536×1024 RGB.

The large front view is the only view with enough resolution for the door
leaves. `tools/convert_g5x_door.py` takes two native-resolution crops:

| Leaf | Crop | Native size | Runtime bytes |
|---|---|---:|---:|
| left | `(210,145,660,930)` | 450×785 | 1,059,750 RGB |
| right | `(660,145,1070,930)` | 410×785 | 965,550 RGB |

The two leaves retain the reference sheet's red/white service stripe and
`A-3` marking, windows, bolts, ribs, paint wear and rust. There is **no
upscaling, retouching or procedural replacement** of the supplied artwork.
The PNG crops are kept for inspection; runtime uses the same pixels in raw RGB
files to avoid a large static array in the executable.

## 2. Why the old door was blurry

- The old `tex_door` was **64×64** and covered a 128×128 world-unit face.
- That is **0.5 source texels per world unit** (2 world units per texel).
- At 960 close range one texel was magnified to roughly 9 screen pixels; at
  1920 roughly 18. `GL_LINEAR` could only smooth the blocks.
- The new leaves have about **7 texels per world unit** horizontally and
  **7 per unit** vertically (450/64 and 785/112), and are drawn at their native
  pixel density.

## 3. Geometry

The OpenGL door is no longer a 2-unit box:

- Two leaves, each 64 units wide × 112 units high, **8 units thick**
  (`x=768..776`, `z=8..120`).
- Casing is **24 units deep** (`x=760..784`) with side jambs, header, threshold,
  receiving pockets (`y=192..256` and `384..448`) and front pocket covers that
  the leaves slide behind.
- Visible edge returns, inner interlock strips, outer hinge/roller blocks and a
  steel lock housing with red/green status indicators.
- Header warning lamps and a threshold plate.
- Room-side and corridor-side leaf faces are both textured; the less expensive
  face is skipped when the camera is not on that side.

The existing A-3 corridor, floor, room machinery, rifle, Alien, tracker and
controls are untouched.

## 4. Animation and state

The renderer now reads `Status.door_open` directly; it no longer keys the door
only off `door_blocked`:

```text
travel = clamp(door_open, 0, 64)
north leaf = [256 - travel .. 320 - travel]
south leaf = [320 + travel .. 384 + travel]
```

- `door_open == 0`: leaves meet at the centre seam.
- `door_open == 24`: partial opening, collision still blocked (24 < 32).
- `door_open == 32`: collision clears; the clear gap is 64 units.
- `door_open == 64`: both leaves are parked inside their side pockets.

Glon remains authoritative. No second renderer-side state machine, no collision
change, no door threshold change. The partial-open screenshots below are actual
renderer output, not composites.

## 5. Evidence

All paths are actual OpenGL captures from the implementation commit.

### Before/after, same camera

- 960 closed: `/tmp/g5x-final/compare-closed-960.png`
- 1920 closed: `/tmp/g5x-final/compare-closed-1920.png`
- 960 open: `/tmp/g5x-final/compare-open-960.png`
- close route view: `/tmp/g5x-final/compare-route-closed-960.png`

### G5x final stills

- `/tmp/g5x-final/960/01-door-closed.png`
- `/tmp/g5x-final/960/02-door-partial-24.png`
- `/tmp/g5x-final/960/03-door-opening-40.png`
- `/tmp/g5x-final/960/04-door-open-64.png`
- `/tmp/g5x-final/960/05-door-oblique-closed.png`
- `/tmp/g5x-final/960/06-door-oblique-partial.png`
- `/tmp/g5x-final/1920/` — same poses at 1920×1080.
- `/tmp/g5x-final/nav/02-route-at-closed-door.png`
- `/tmp/g5x-final/nav/03-route-door-open.png`
- `/tmp/g5x-final/tour/` — full 15-pose tour; all poses completed.

Recapture command:

```sh
DOOM_ROOT=../.. ./build/doom --gl-door-shots DIR 960 600
DOOM_ROOT=../.. ./build/doom --gl-door-shots DIR 1920 1080
```

## 6. Tests and measurements

- `make test`: **pass**, including the new G5x asset check.
- `./check-frozen-s1.sh`: **pass** at
  `f90496c26dc45c7a387d8fc8639cd2781507d3d2`.
- Interactive OpenGL launch: **pass**; assets loaded, normal spawn
  `(320,320, angle=240)`, closed door, Alien at `(1120,320)`.
- G5u navigation: **pass**; door opens/closes and the barrel route still
  completes (`02-route-at-closed-door`, `03-route-door-open`,
  `10-barrel-240`).
- Door semantics: unchanged; `door_open=48` open passage and closed-door
  collision tests still pass.

Matched 960×600, 180-frame runs, WSLg D3D12 AMD Radeon:

| Configuration | G5w matched | G5x final |
|---|---:|---:|
| No objects | 81.9 FPS | **82.1 FPS** |
| With objects | 54.0 FPS | **53.4 FPS** |

G5x scene/submit time is unchanged within run noise. The earlier verified G5w
report's 91.1/60.4 was a faster session; the matched runs above are the correct
comparison.

Other measurements:

- Executable: **6,121,456 bytes** (G5w 6,117,184; +4,272).
- Loader high-water: **53,705 / 54,569** cells; heap **33,440 / 33,600**.
- New GPU texture memory: **2,700,400 bytes** for the two leaves, loaded once.
- No per-frame uploads; no new shaders; no runtime texture generation.

### Asset-embedding finding

An initial version embedded the two leaves as C arrays. That added 2.7 MB to
`.rodata`, shifted `.bss` by 2.7 MB, and consistently slowed the same CPU/GL
benchmarks (object GL run about 48 FPS versus about 57 FPS) even though the
tick/render code was otherwise unchanged. Loading raw RGB assets at startup
restored the original BSS layout and performance above. The assets therefore
live under `assets/doors/`, loaded by
`gl_upload_rgb_file()`; a missing asset fails loudly instead of silently
falling back to the old blurry texture.

## 7. Audio

No audio engine was added. SDL is still initialized as
`SDL_INIT_VIDEO | SDL_INIT_TIMER`. Prepared event points:

| Glon event | Host detection | Future sound |
|---|---|---|
| Lock disengages | `door_target` 0→64 | servo/latch click |
| Opening | `door_open` increasing | hydraulic loop |
| Closing warning | `door_target` 64→0 | reversing chirp |
| Closing | `door_open` decreasing | hydraulic close |
| Fully shut | `door_open` reaches 0 | heavy clunk |
| Collision change | `door_blocked` edge | lock/unlock thunk |

## 8. Limitations

- The supplied sheet's large view yields **450/410-pixel-wide leaves**, not the
  1024×2048-per-leaf ideal. They are native pixels and approximately 7
  texels/world-unit, so the normal 960/1920 close views are detailed, but the
  extreme 24-unit route close-up still shows source softness. No fake upscale
  was used.
- The concept sheet has baked front lighting and some perspective; no attempt
  was made to relight it.
- Parked leaves disappear behind pocket covers. The guide tracks are suggested
  by rails and covers, not a full exposed roller mechanism.
- The CPU fallback renderer still uses the old 64×64 `tex_door`; G5x is an
  OpenGL-path upgrade.
- Audio remains deferred.

**The previous blocked close-up failure is resolved: the door is detailed at
normal close range, has real thickness/casing depth, and opens partially and
fully at the same `door_open` values Glon uses for collision.**
