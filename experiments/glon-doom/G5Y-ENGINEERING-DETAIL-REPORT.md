# G5y — Engineering-Room Cabinet and Ceiling Detail

**Status: implemented in OpenGL; committed locally, not pushed.**

G5y is a targeted visual-quality pass on the engineering room. It resolves the
black bars across the equipment-cabinet fronts and gives the room ceiling a
room-specific panel treatment. Gameplay, Glon semantics, collision, the G5x2
blast doors, Alien AI, weapons, navigation and Glon Paint are unchanged.

**Branch/worktree:** `glon-avp-g5y` at
`/mnt/d/repos/glon-worktrees/glon-avp-g5y`.
**Starting point:** G5x2 `133d5b0`.
**G5y implementation:** `5c0ef8504bc2f7775a4c208c4176326f5d39a8b9`.
**G5y evidence/report:** this commit.

No push, merge, tag or deploy was performed. Frozen S1 is unchanged.

## 1. Investigation

### 1.1 Cabinet bars

The full-height dark strips were in `eng_cabinet_y()` at the G5x2 starting
point (`doom-host.c:3520-3533`). The relevant calls were:

```c
gl_box(x1,y-29,z1,x2,y-26,z1+5,0.18f,0.16f,0.13f);       /* top rail */
gl_box(x1,y-29,z2-5,x2,y-26,z2,0.18f,0.16f,0.13f);       /* bottom rail */
gl_box(x1+(x2-x1)*0.48f,y-28,z1+6,
       x1+(x2-x1)*0.48f+3,y-26,z2-6,0.06f,0.07f,0.07f);  /* centre stile */
gl_box(x1+7,y-30,z2-18,x1+10,y-27,z2-12,0.06f,0.065f,0.06f);
gl_box(x1+7,y-30,z1+9, x1+10,y-27,z1+15, 0.06f,0.065f,0.06f);
```

The 3-unit-wide strip at 48% of the cabinet width was an intentional
double-door centre stile, not a texture artifact, missing texture or rendering
defect. It read as unfinished for two reasons: it was almost black
(`0.06,0.07,0.07`) with no metallic edge detail, and it ran through the centred
vent of the reused `GT_ART_PANEL` overlay. The two smaller dark boxes were
latches/hardware near the same panel.

The east-facing cabinets had a separate issue. `eng_cabinet_x()` drew its
`GT_ART_PANEL` overlay at `x+0.1f` (`doom-host.c:3538`), but the cabinet body
extends from `x` to `x+24`. The overlay was therefore inside the body and
occluded by the base steel; the visible face was mostly the weathered
`GT_ART_WALL` box. Its short dark latch blocks were not the full-height bar.

### 1.2 Ceiling

The floor uses the existing deck art (`GT_ART_FLOOR`/`GT_FLOOR`) through
`gl_corridor_static()`. The base ceiling was one world-spanning plane:

```c
gl_plane_tint(128, GT_PANEL, 0.30f);
```

at G5x2 `doom-host.c:3144`. `eng_overhead()` then added structural beams, corner
posts, pipes, cable trays and five recessed light housings at
`z` about 118–128 (`doom-host.c:3604-3641`). The room had no room-specific
ceiling surface, so between the beams it was the deliberately dark base plane.

## 2. Cabinet treatment

The cabinets are now rendered as single-door units rather than double-door
units. This removes the conflicting centre stile instead of disguising it:

- Steel top and bottom rails and steel side jambs frame the existing panel.
- Three hinges sit on one jamb; an edge pull sits on the opposite latch edge,
  clear of the centred vent.
- Narrow bright strips and a dark grip insert give the pull and hinges a
  readable metallic form.
- The existing panel, ID plate, green/red indicators and warning strip remain.
- The vent is unobstructed from normal and close viewpoints.

The east cabinets also had their panel overlay moved to `x-0.1f` and glow to
`x-0.04f`, in front of the cabinet face, so the intended panel is actually
visible. The east pull was moved from mid-width to the latch edge; it no longer
crosses the vent.

Cabinet positions, dimensions, wall bays, collision geometry and interaction
are unchanged. The changes are renderer-only and add no textures.

## 3. Ceiling treatment

The room now draws a shallow recessed ceiling layer below the world plane:

- The existing 64×64 `tex_ceiling_panel` is uploaded once as the new
  `GT_CEIL_PANEL`; no new art asset was created.
- A 6×4 grid of weathered-metal panels covers the room at `z=127.35`, with
  1.3-unit gaps exposing the dark base plane as seams.
- Per-panel tint variation is restrained (roughly 0.56–0.635); it is not a
  copy of the floor texture.
- A dark perimeter support ledge closes the panel field at the walls.
- Two recessed ventilation grilles with frames and slats sit below the panel
  layer.
- A very subtle cool additive bounce (`0.05` alpha) lifts the underside without
  changing the room's light fixtures or baked lighting scheme.

The original beams, corner posts, pipes, cable trays and light housings remain
in place. Room dimensions, player clearance, Alien navigation and collision are
unchanged.

## 4. Evidence

Persistent evidence is committed under `evidence/g5y/`:

- `before/` — G5x2 renderer with capture-only diagnostics: cabinet wall,
  cabinet close-up, east cabinets, ceiling-wide view, room-wide view and three
  `--gl-rooms` views, plus baseline test log, executable size/hash and logs.
- `after/` — matching G5y views, final test log, executable size/hash and logs.
- `compare-cabinet-closeup.png`, `compare-cabinet-wall.png`,
  `compare-ceiling-wide.png`, `compare-room-wide.png` — labelled montages.
- `bench/` — raw standard and room-specific benchmark logs.
- `metadata.txt` and `bench-summary.txt` — sizes, texture memory and medians.

Recapture commands:

```sh
DOOM_ROOT=../.. ./build/doom --gl-detail-shots DIR 960 600
DOOM_ROOT=../.. ./build/doom --gl-detail-shots DIR 1920 1080
DOOM_ROOT=../.. ./build/doom --gl-rooms DIR 960 600
DOOM_ROOT=../.. ./build/doom --gl-detail-bench 180 960 600 objects
DOOM_ROOT=../.. ./build/doom --gl-detail-bench 120 1920 1080 objects
```

`--gl-detail-shots` and `--gl-detail-bench` are diagnostics added for this
pass. The benchmark uses the room-wide ceiling/cabinet pose so the added
geometry is actually in frame; it is a static render-cost comparison, not a
simulated gameplay run.

The baseline detail pose `05-room-ceiling` looks downward because positive
`gl_debug_pitch` pitches down. The valid ceiling comparison is
`06-room-ceiling-wide`; `05` is not used as ceiling evidence.

## 5. Tests

`make test` passes, including all texture, Alien, G5s, G5j, G5x and G5x2
checks, the trace suite and `./check-frozen-s1.sh`. Frozen S1 remains at
`f90496c26dc45c7a387d8fc8639cd2781507d3d2`.

No new asset test was needed: G5y reuses `tex_ceiling_panel` and adds no door,
floor or texture file. The G5x2 door assets and checks are untouched.

## 6. Measurements

Configuration: WSLg D3D12 AMD Radeon, 64K S1 configuration. The baseline is the
G5x2 renderer plus the same capture-only diagnostics; the implementation build
also contains the new diagnostics.

| Item | G5x2 | G5y | Change |
|---|---:|---:|---:|
| Executable | 6,121,456 B | 6,146,536 B | +25,080 B |
| Baseline-with-diagnostics | — | 6,125,888 B | +20,648 B |
| GL texture memory | 9,878,400 B | 9,894,784 B | +16,384 B |

The texture increase is exactly one 64×64 RGBA upload. No other texture was
added or resized. Loader/heap are unchanged; the room benchmark reports
54,601/33,600 cells, the standard object benchmark 54,569/33,600.

Benchmarks are interleaved old/new runs on the same session:

| Test | G5x2 median | G5y median | Notes |
|---|---:|---:|---|
| Standard `--gl-bench 180 960 600 objects` | 52.7 FPS | 54.2 FPS | 3 runs each; no regression |
| Room detail bench, 960×600 objects | 209.4 FPS | 218.5 FPS | 5 runs each; within spread |
| Room detail bench, 1920×1080 objects | 122.8 FPS | 120.4 FPS | 12 runs each; −1.9%, ranges overlap |

The 1920 room benchmark is the ceiling-heavy worst case. Its old range was
101.3–134.1 FPS and its G5y range 104.5–133.2 FPS; the 1.9% median difference
is inside the observed run-to-run spread. At normal 960×600 the added detail is
not measurably slower, and the standard gameplay-representative benchmark is
slightly faster in this sample. These are render-cost numbers from a hidden
window, not a guarantee for other GPUs or drivers.

## 7. Visual assessment

**Cabinet:** the normal-view and close-up montages show no vertical black bar
over the vent. The panel's vent, weathered face, ID plate and indicators are
visible; steel rails/jambs, hinges and the edge pull read as hardware rather
than flat debug boxes. The east cabinet overlay is now visible instead of being
buried inside the body.

**Ceiling:** the wide 960 and 1920 views show a continuous grid of weathered
panels, recessed seams, a perimeter ledge and ventilation grilles. The original
beams, pipes, trays and light housings still dominate the structural read, so
the result remains industrial rather than becoming a second floor. No
z-fighting, clipping or exposed edge was observed in the captured poses. The
panel field stops cleanly at the room boundary.

No collision, navigation or interaction code was changed, so player and Alien
movement are unaffected. The blast doors remain G5x2.

## 8. Limitations

- The cabinets now read as single doors. This is a deliberate composition
  choice: it resolves the centred vent without a fake seam. A future
  double-door variant would need either an off-centre vent or two separate
  door panels.
- The ceiling is a shallow overlay, not a fully modelled recessed ceiling. The
  original world plane remains visible in the 1.3-unit seams and behind small
  gaps.
- The 6×4 panel grid repeats one existing vented panel texture; there is no
  unique normal map, parallax or dynamic shadowing.
- The added ventilation grilles and seams are regular. At extreme close range
  the ceiling texture's 64×64 source resolution becomes visible.
- The CPU fallback renderer is unchanged; G5y is an OpenGL-path pass.
- Performance is measured on the WSLg D3D12 driver. The 1920 ceiling-heavy
  median is within noise here but may differ on other GPUs.
- Audio, Alien AI, weapons, navigation, Glon Paint and parser semantics remain
  untouched.
