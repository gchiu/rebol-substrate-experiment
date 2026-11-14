# G5p corrective investigation — materials and performance

Branch/worktree: `glon-avp-g5p`, `/tmp/opencode/glon-avp-g5p`  
Base: `ced9611b4d76f6e78170f0bb1948c1ca664edf46`  
Commit: `ec598372e1e5b7baffc9630d7ea9b9add5795745`

No new artwork, props, rooms or VM features were added. No push/merge/tag/deploy.

## A. Blank surfaces and viewpoint consistency

### Reproduction

The reported doorway view was reproduced at the spawn point (320,320, facing
east): `/tmp/g5p-spawn-before.png`. The room walls, floor, ceiling and closed
door were flat dark/grey, while the corridor wall artwork appeared only after
walking through the door.

### Cause

This was **not occlusion or winding**. The surfaces were visible but had no
material assignment:

- `gl_corridor()` called `gl_solid_plane()` / `gl_solid_wall()` for the base
  floor, ceiling and all ten `gl_walls` segments.
- The close door was a solid olive box with no textured face.
- Room floor used the solid base plane; room walls used flat wall colours.
- The unused textured helpers `gl_wall()` / `gl_plane()` already existed but
  were not called.
- G5o recessed-panel and bulkhead artwork covered only the corridor overlay
  quads, which explains why the corridor looked textured from another viewpoint.

### Fix and inventory

| Visible surface | Material now |
|---|---|
| Ten `gl_walls` room/corridor wall segments | existing `GT_PANEL` / `GT_SERVICE`, tiled by wall length |
| Base floor, room and corridor | G5o `GT_ART_FLOOR` in art mode, `GT_FLOOR` in placeholder mode |
| Room/corridor ceiling | existing `GT_PANEL`, dark-tinted (0.30) |
| Near door, room side and corridor side | existing `GT_DOOR`, dark-tinted (0.50), both faces |
| Corridor recessed side panels | G5o `GT_ART_PANEL` |
| Far destination bulkhead | G5o `GT_ART_WALL` |
| Lower service housings | existing `GT_SERVICE`, tiled and dark-tinted (0.55) |
| Ceiling light recess/frames/grilles | deliberately flat dark structural geometry |
| Structural ribs, pipes, door frames | deliberately flat/dark structural materials |
| Props and Alien | unchanged existing geometry/materials |

Deliberately dark recesses, light housings, ribs, pipes and door frames were not
covered with bright textures.

### Viewpoint checks

- Spawn doorway, room side: `/tmp/g5p-fix-final960c/12-spawn-doorway.png`
- Looking backwards into the room: `/tmp/g5p-fix-final960c/13-room-turnaround.png`
- Closed door from corridor side: `/tmp/g5p-fix-final960c/14-corridor-door-closed.png`
- Corridor views: `/tmp/g5p-fix-final960c/01-entrance.png`,
  `02-mid-corridor.png`, `04-look-back.png`
- Actual maximised WSLg spawn view: `/tmp/g5p-max-corrective.png`
- Matched before/after doorway sheet: `/tmp/g5p-corrective-before-after.png`
  (before left, corrective right; Game Boy-style desktop crop normalized)

The near door is now textured on both faces. The far bulkhead has the existing
G5o steel face. Both sides remain consistent when turning around.

No gameplay collision or object records were changed in this corrective pass.

## B. Severe interactive slowdown

### Measurement method

Added timers around, separately:

1. Glon `tick()` (VM + game state update);
2. `get_state`/`get_status` extraction;
3. corridor/immediate-mode OpenGL submission;
4. explicit GPU `glFinish` (only when requested with `GLON_FINISH=1`);
5. `SDL_GL_SwapWindow`.

The interactive loop logs 120-frame averages. A temporary G5o checkout with the
same instrumentation was used for matched comparisons.

### Findings

- The initial G5p report of `sim≈23 ms` / ~32 FPS was **not reproducible** after
  a clean rerun of the same geometry. Repeated clean runs were 52–55 FPS at
  960×600. The unstable value is consistent with WSLg/D3D12 backpressure and
  CPU scheduling, not corridor rebuild cost.
- Corridor static geometry is rebuilt every frame, but CPU submission measured
  only **0.5–0.9 ms**. A display-list cache was tested and **rejected**: on this
  WSLg D3D12 stack it collapsed to under 1 FPS (≈1.4–1.6 s/frame).
- Forced `glFinish()` cost **2.3–6 ms/frame**. It is no longer used in normal
  interactive play; `glReadPixels`/screenshots still synchronize. Set
  `GLON_FINISH=1` to restore explicit synchronization for profiling.
- Stable interactive cost is dominated by Glon:
  - `tick`: ~6–9 ms typical, with occasional 12 ms scheduling spikes;
  - state/status extraction: ~5–6 ms;
  - corridor submission: ~0.5–0.9 ms;
  - swap: ~4.5–6 ms;
  - total: ~16–22 ms at maximised window.
- G5o has the same tick/state cost profile; this is not a G5p regression and
  cannot be fixed by renderer batching.

### Matched performance (clean runs)

| Build | Resolution | sim | submit | total | FPS |
|---|---:|---:|---:|---:|---:|
| G5o | 960×600 | 12.5 ms | 3.1 ms | 18.6 ms | 53.7 |
| G5p corrective | 960×600 | 10.6–13.2 ms | 0.7–0.9 ms | 16.2–19.5 ms | 51.4–61.7 |
| G5o | 2080×1300 | 16.1 ms | 5.3 ms | 29.4 ms | 34.0 |
| G5p corrective | 2080×1300 | 10.8–11.7 ms | 0.8–1.1 ms | 22.5–24.6 ms | 40.7–44.5 |

G5o figures include forced finish. G5p figures are the normal interactive path.
The remaining ceiling on 960×600 is Glon tick/state extraction, not static
geometry construction.

Loader after host: **14,082 used / 718 free**; heap **33,600**; executable
**4,189,752 bytes**.

## C. Regression tests

`make test` passes:

- frozen S1 (`f90496c26dc45c7a387d8fc8639cd2781507d3d2`);
- input/turning, doors, collision/objects;
- tracker and resizing;
- renderer/texture checks, Alien behaviour;
- projection, stationary cache, raw/default self-tests.

`git diff --check` passes.

## D. Remaining work

- The near door uses the existing `GT_DOOR` texture, which is brighter and more
  hazard-striped than the references; it is not a new art pass.
- Props remain solid existing materials, not final prop textures.
- Ceiling/light lens/vent and final seamless deck materials remain artwork
  dependencies from G5p.
- If perceived slowness remains, the next measured target is Glon tick/state
  extraction (about 11–15 ms combined), not OpenGL geometry.
