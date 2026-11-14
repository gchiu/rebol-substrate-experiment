# G5z — GLON CORPORATION Industrial Equipment

Status: implemented, tested and locally committed.

- Base: G5y `94699ebf25bf3bf9b411be564afa89eaf3f8227e`
- Branch/worktree: `glon-avp-g5z` at
  `/mnt/d/repos/glon-worktrees/glon-avp-g5z`
- Implementation commit: `ff2ea47`
- Scope: renderer-only changes in `experiments/glon-doom/doom-host.c`

## 1. Original identification

### Objects

The six props are a fixed scenery list in
`experiments/glon-doom/objects.glon:7-14`:

| id | visual role | position `(x,y)` | extents `(hw,hd,h)` |
|---|---|---|---|
| 0 | red drum | `(910,272)` | `(13,12,62)` |
| 0 | red drum | `(940,370)` | `(13,12,62)` |
| 1 | olive-green crate | `(860,270)` | `(28,19,48)` |
| 1 | olive-green crate | `(1040,370)` | `(30,18,48)` |
| 2 | grey cabinet | `(980,270)` | `(18,12,70)` |
| 3 | low dark-green container | `(1100,370)` | `(36,16,38)` |

They are **fixed cargo/equipment props**, not movable cargo. There is no
motion, inventory or interaction logic for them. In object mode they are
static collision obstacles.

### Collision

- Object collision data is projected by `doom/check-object` and checked by
  `doom/check-objects` in `objects.glon:50-70`.
- Movement calls it from `doom/check-all` in `doom.glon:266`, gated by
  `doom/objects-on` (`doom.glon:1033`, default zero). Object mode sets it to
  one; `objects_test()` verifies player and Alien hits.
- The collision tests use the original rectangular `hw`/`hd` extents above.
  G5z did not change those extents, `objects.glon`, `doom.glon`, or the frozen
  S1 substrate.
- Decorative handles, bands, lids and label plates are visual only and are not
  separately collidable.

### Rendering before G5z

- The OpenGL path projects the six records through `get_objects()` and draws
  them in `gl_prop_3d()` (`doom-host.c:3034`). Before G5z the drums and crates
  were simple flat-coloured boxes/cylinders with a few trim boxes.
- The CPU path uses the existing 64x96 object sprites via `object_sprite()`;
  it was not replaced.
- The north-wall engineering control bank is built in `eng_walls()`.
  Before G5z it consisted of three flat coloured rectangles, a flat status
  strip, six red/green indicator pairs and a flat manufacturer strip.
- Reusable assets already existed: `GT_ART_FLOOR`, `GT_ART_WALL` and
  `GT_ART_PANEL` textures, `gl_box_tex`, `gl_cyl_*`, `gl_tex_quad` and
  `gl_upload`. There was no reusable stencil-font/atlas for machine labels.

## 2. Baseline

Baseline source was G5y `94699eb` plus capture/benchmark-only diagnostics.

- Clean G5y executable: **6,146,536 bytes**,
  SHA-256 `96193903a00b857936bcb89312028268bd4ac35f2277a2691ec105616342a344`.
- Diagnostic baseline executable: **6,150,992 bytes**,
  SHA-256 `210822736261b3d8f256e012d01f20a78b2e25d6098b4eb99d56b78925766f63`.
- G5y GL texture memory: **9,894,784 bytes**.
- Baseline full `make test` passed, including frozen S1 at
  `f90496c26dc45c7a387d8fc8639cd2781507d3d2`.
- Matching baseline views use the same seven poses as the after captures and
  were produced by adding only the pose/benchmark diagnostics to the G5y
  visual source.

Evidence: `experiments/glon-doom/evidence/g5z/before/`.

## 3. Containers

The existing red drums and olive-green crates retain their positions and
collision extents. Their identity colours are preserved, but the geometry and
materials are now industrial.

### Crates (`g5z_crate`, `doom-host.c:2909`)

- Textured painted-steel body with a raised rim, dark recessed lid and latch.
- Reinforced corner posts with caps and a central rivet.
- Recessed side panels in steel frames, with corner fasteners.
- Top and bottom bands, lifting handles and hazard placards.
- Sparse rust runs on seams and lower edges.
- Stencilled long-face branding, inventory codes and a secondary marking.
- Olive crates use codes `GC-204`/`GC-205`; the low green container uses
  `GC-208` and an authorized-personnel marking.

### Drums (`g5z_drum`, `doom-host.c:2983`)

- Textured red cylinder with rolled top/bottom rings and four body hoops.
- Yellow/black hazard band between the upper hoops.
- Side lifting handles on the X faces.
- Riveted north/south label plates mounted clear of the lower hoop.
- Raised plates carry a bold `GLON` logo, `CORPORATION` and `GC-207`, with rust
  runs on the body.

### Branding and orientation

- A single generated **512x256 RGBA atlas** (`GT_G5Z_ATLAS`,
  `g5z_build_atlas()`, `doom-host.c:2403`) holds all stencil labels and
  instrument panels.
- Text is drawn with a hand-coded 5x7 industrial font (`g5z_font`), with
  shadow/wear and an optional bold 2x path for the drum logo.
- Labels are transparent stencil quads and use `GL_CLAMP_TO_EDGE`.
- Face orientation is explicit: looking north the viewer's right is `-x`,
  looking south it is `+x`, east `+y`, west `-y`. Crate and drum label helpers
  swap their coordinate order on the appropriate faces; the control-bank
  screens swap the x arguments in `gl_tex_quad` so north-wall text reads
  correctly from the corridor.
- Atlas cost: 512 x 256 x 4 = **524,288 bytes**.

## 4. Engineering control bank

The flat rectangles are replaced by recessed, bezelled displays in
`eng_walls()` (`doom-host.c:3983`). Positions and screen roles are retained:

- Teal **FLOW A** waveform at `x572..632, z54..76`.
- Orange **PWR BUS** bar graph at `x646..706, z54..76`; bars show `78`, `64`
  and `91` with A/B/C labels.
- Green auxiliary gauge at `x660..700, z26..44`; its former `OK` text was
  replaced by a small green lamp.
- The original six red/green status-indicator pairs remain visible, now in a
  dark status strip at `z78..84`.
- Each screen has a recessed dark backing, a metallic frame, corner screws and
  a subtle bezel highlight.
- A manufacturer/service plate at `z15..25` carries `GLON CORPORATION` and
  `ENGINEERING DIVISION` in the correct left-to-right order.
- The displays are explicitly **decorative**. No readout was connected to game
  state, and no functional controls were implied.

## 5. Verification

- Full `make test` passes, RC 0. Log:
  `evidence/g5z/after/make-test.log`.
- Frozen S1: `OK: S1 substrate frozen at
  f90496c26dc45c7a387d8fc8639cd2781507d3d2; all frozen files unchanged.`
- Matching before/after captures: 960x600 and 1920x1080, seven poses each:
  containers normal, crate close-up, drum close-up, control bank, control-bank
  close-up, room wide, corridor wide. See `evidence/g5z/compare/`.
- Visual checks:
  1. Containers read as weathered industrial equipment at normal distance.
  2. `GLON CORPORATION` is readable on crate long faces and the drum plate.
  3. No mirrored or reversed text. Vertical UV mapping and horizontal face
     order were both checked in the close-ups.
  4. No z-fighting or clipped seams on the reviewed views. The drum plate was
     moved above the lower hoop to remove a hoop/label overlap.
  5. Screens sit in dark recesses with metallic frames and screws.
  6. The original red/green indicator pairs remain visible.
  7. `objects-test` and the full suite pass; no movement/AI source changed.
  8. See performance below.
- The implementation is renderer-only. `s1.c`, `s1.h`, `tests.c`,
  `adversarial.c`, `claims.c`, all `.glon` files and the frozen resource layout
  are unchanged.

## 6. Size, texture memory and performance

- Final executable: **6,172,112 bytes**
  (`+25,576` vs clean G5y; `+21,120` vs diagnostic baseline).
- SHA-256: `9eb0bbe26e79067786c0aff8af53d66040be60e2be58c5b744aefb3bd4f1ff37`.
- GL texture memory: **10,419,072 bytes** = 9,894,784 + 524,288 for the atlas.
- Loader/heap in matching captures: `54,601 / 33,600` (unchanged).

Five 180-frame runs per case, WSLg D3D12, 2026-11-15, same session. Baseline
is the G5y visual source with diagnostics only.

| Case | Baseline median | Final median | Change |
|---|---:|---:|---:|
| 960x600 containers pose 0 | 115.6 FPS | 102.2 FPS | -11.6% |
| 960x600 control bank pose 1 | 116.3 FPS | 115.8 FPS | -0.4% |
| 960x600 standard object benchmark | 42.1 FPS | 45.8 FPS | +8.8% |
| 1920x1080 containers pose 0 | 84.5 FPS | 73.4 FPS | -13.1% |
| 1920x1080 control bank pose 1 | 84.7 FPS | 78.5 FPS | -7.3% |

Observed ranges are recorded in `evidence/g5z/benchmarks.txt`. The standard
gameplay benchmark improves. The container-only close diagnostic is the one
localized cost: the newly textured large crate/drum surfaces are fill-rate
bound, while the control bank is effectively unchanged. The medians remain
102.2 FPS at 960x600 and 73.4 FPS at 1920x1080; no simulation, AI or collision
timing changed.

The generic cylinder path and the drum hazard band were batched into single
draw calls during the pass. A backface-culling trial was rejected because the
renderer's shared box helpers are not consistently wound and the trial removed
visible crate panels/lids.

## 7. Remaining limitations

- At the specific normal pose `01-containers-normal`, the near drum occludes the
  left part of the near crate's end stencil; the long-face brand and the
  close-up views show the full text. This is an occlusion, not mirrored or
  corrupt text.
- In the close crate view, the weapon partially covers the lower secondary
  marking; the primary brand and inventory code are clear.
- The stencil font is deliberately low-resolution and becomes pixelated at
  extreme close range, consistent with painted industrial lettering.
- The displays are decorative and do not reflect live game state, as required
  by the scope.
- Textured container surfaces cost fill rate in the container-only diagnostic
  view; standard gameplay is not slower.

## 8. Evidence layout

```
evidence/g5z/
  before/{960,1920}/          matched G5y visual baseline
  after/{960,1920}/           G5z final captures
  before/bench/               baseline benchmark logs
  after/bench/                final benchmark logs
  compare/                    before/after side-by-side montages (1920)
  benchmarks.txt              medians and ranges
  before/metadata.txt         baseline build/settings
  after/metadata.txt          final build/settings
```

Commands:

```sh
make build/doom
DOOM_ROOT=../.. ./build/doom --gl-g5z-shots /tmp/g5z-shots 1920 1080 objects
DOOM_ROOT=../.. ./build/doom --gl-g5z-bench 0 180 960 600 objects
```

Implementation commit: `ff2ea47`. This report and the project-memory updates
are committed separately.
