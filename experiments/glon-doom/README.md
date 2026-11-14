# Glon AvP — G5f: Compile the World

G5f investigates which rendering work is invariant and can be compiled once.
No VM, resolution, artwork or gameplay changes were made.

## Result summary

The floor/ceiling row stage was substantially reduced, but total FPS did not
improve because wall projection remained dominant and became slower in this
build. The 15 FPS target was **not** met.

## Original algorithm (G5e)

Per textured frame:

- 160 wall columns × 10 linedefs = 1,600 wall test evaluations;
- one successful nearest-wall result per column;
- a 200-row floor/ceiling pass that recomputed a distance division and camera
  transform for every row;
- native pixel sampling and presentation afterward.

G5e measured baseline in the same environment:

- Textured **6.0 FPS**
- Simple **8.6 FPS**
- simulation 1.65 ms/tick
- wall projection 106.05 ms/frame
- floor/ceiling rows 51.94 ms/frame
- native sampling 0.45 ms/frame
- presentation 6.27 ms/frame
- total 166.37 ms
- managed high-water 33,504 cells; GC 0

VM instruction counts are not exposed by the frozen runtime, and no
instrumentation was added to frozen S1. The session API reports wall rays and
HOST service usage indirectly; exact VM instruction counts remain unavailable
without an intrusive VM change, which was out of scope.

## Invariant vs dynamic

Invariant or level-static:

- the 10 wall endpoints, materials and solid boundary roles;
- door location and width;
- projection/focal constants;
- floor/ceiling row distances and shade thresholds;
- texture assets and material IDs.

Dynamic:

- camera position and direction;
- visible wall selection and intersection distances;
- screen projection;
- texture U/V and floor/ceiling world coordinates;
- door state, Alien, tracker and player collision.

## Redundancy found

1. The floor and ceiling rows are mirror pairs around the horizon and share the
   same distance, base, step and shade. G5e recomputed both independently.
2. Row distance `(projection << 16) / |y-horizon|` is camera-independent but was
   divided again for every row, every frame.
3. The wall endpoints/materials were already compiled into specialized,
   unrolled checks in the game source; no runtime wall table was being rebuilt.
4. Wall ray tests still used per-wall divisions.

## Implemented

- A compiled 100-entry row-distance table (`doom/rowdist`) is loaded once with
  the level.
- A mirror row emitter computes one distance/base/step/shade for a floor/ceiling
  pair and emits both rows.
- G5c diagnostic detail-line code was moved to `tests/detail.glon`, freeing
  loader capacity and keeping those tests in `--projection-test`.
- The projection/alignment diagnostics now run in a separate minimal session.

These changes reduce floor/ceiling row generation from **51.94 ms** to about
**24 ms**, roughly half.

## Attempts rejected

- A fully compiled wall candidate table did not fit the 54.8K loader frontier.
- Per-ray reciprocal/slope wall coefficients did not produce a measured gain.
- Inline near-plane wall culling hit an evaluator branch-path failure at 90° and
  exceeded loader capacity; it was reverted.

## Final G5f measurements

- Textured **5.9 FPS**
- Simple **6.7 FPS**
- simulation 2.18 ms/tick
- wall projection **136.19 ms/frame**
- floor/ceiling **24.11 ms/frame**
- native sampling **0.41 ms/frame**
- presentation **6.59 ms/frame**
- total **169.48 ms**
- managed high-water **33,440 cells**; GC 0
- loader frontier **54,162 cells**

The row stage improved, but wall projection rose from ~106 ms to ~136 ms in
this build, offsetting the gain and regressing simple mode. This is recorded as
a failed performance hypothesis, not a claimed speedup.

## Correctness tests

`make test` includes:

- the full G5a–G5e self-test in the normal session;
- a separate `--projection-test` for loader-heavy diagnostic camera views,
  floor/ceiling-to-wall alignment, near-plane clipping and endpoint ordering;
- deterministic texture-row and framebuffer tests;
- floor/ceiling compiled-table lookup and mirror-pair output;
- door closed/partially open/fully open cases;
- Alien occlusion/tracker preservation;
- loader growth and frozen-S1 checks.

All tests pass. Visual texture output is unchanged from G5e.

## Screenshots

- `/tmp/g5f-shots/room.png`
- `/tmp/g5f-shots/near-door.png`
- `/tmp/g5f-shots/corridor.png`
- `/tmp/g5f-shots/alien.png`
- `/tmp/g5f-shots/inside.png`
- `/tmp/g5f-shots/turn.png`
- contact sheet: `/tmp/g5f-contact.png`
- G5e/G5f comparison: `/tmp/g5f-vs-g5e.png`
- actual WSLg window: `/tmp/g5f-gui.png`

## Remaining bottleneck

Wall projection is still a full per-column scan of ten walls. The loader frontier
prevents adding a useful wall candidate/spatial table without moving geometry
into C or redesigning the command interface. A future structural improvement
would need a compact compiled sector/grid representation and a bounded way to
share it with Glon, not a throughput tweak.

## Known limitations

- No FPS improvement; simple mode regressed under this build.
- VM instruction counts unavailable without a frozen-runtime change.
- Static row table costs 100 loader cells; loader margin is now tiny.
- Wall projection remains O(160 × 10) with camera-dependent divisions.
- No camera/world geometry was moved to C.
- No resolution, artwork, gameplay or frozen-S1 changes.

## G5g execution-cost investigation

G5g does not add a renderer optimisation. It measures where the time and
loader cells go. See `G5G-INVESTIGATION.md` for the full report and commands.

Key measured results:

- The G5f wall regression is in the **host C build layout**, not the wall
  algorithm: the G5f game runs at ~103.7 ms wall under an equivalent host that
  omits the G5f projection diagnostic code, versus ~136.6 ms in the G5f host.
  The wall-stage instruction count is identical (33,377,988 S1 operations).
- Wall is ~84% of rendering S1 work: 33.38M operations/frame in wall, 5.19M in
  the G5f texture rows. The G5f row compiler is real (G5e rows: 16.62M
  operations).
- The host is not where the wall cost is: 1,600 checks cost ~0.0013 ms in C,
  ~112 ms in the isolated Glon wall benchmark, and ~136 ms in the game.
- Instrumentation is a separate `build/doom-opcode` binary linking the
  diagnostic `s1_prof.c`; production still links frozen `s1.c`.
- `--stationary` / `--stationary-bench` retain a rendered scene while
  simulation and input continue. Idle frame production drops from ~168 ms to
  ~7 ms, but input-to-update remains a full render (~168 ms). This is an idle
  optimisation, not an interactive-speed improvement.
- Loader frontier: 14,800 cells, 638 free after load; the game adds zero
  executable code (no masm). A 320-integer table does not fit.

## G5h prepared wall execution

`--raw-wall` is an experimental prepared-execution path for the wall stage.
It uses the existing `masm` lowerer and the frozen seven S1 primitives. It is
loaded from `raw-wall.glon` only in that mode; the default Glon raycaster is
unchanged and remains the regression oracle.

Measured on the same host binary (`--bench 120`, textured):

| | Default | `--raw-wall` |
|---|---:|---:|
| FPS | 6.2 | **19.5–20.3** |
| Wall | 129.34 ms | **18.53 ms** |
| S1 wall ops/frame | 33,377,988 | **4,555,404** |
| HOST wall calls/frame | 6,014,444 | **846,833** |
| Loader free (after blocks) | 638 | 478 |
| Code cursor / limit | 13,126 / 16,128 | **14,828 / 16,128** |

Simple mode improves from ~6.7 to ~30.4 FPS. `--self-test --raw-wall` passes
the complete default self-test. See `G5H-INVESTIGATION.md` for the trace,
correctness cases, memory accounting and limitations.

Use `--bench 120 --raw-wall`, `--self-test --raw-wall`, or
`--loader-report --raw-wall`.

## G5i Alien sprites and visual diagnosis

The Alien is now rendered from five original transparent 64x128 pixel-art
frames (`sprites/aliens/`): idle, two walk frames, attack and side. Glon still
owns position, state, door/LOS occlusion and the projected rectangle; the host
does alpha sampling, distance shading and simulation-tick-driven animation.
There is no green placeholder rectangle.

Commands:

```sh
./build/doom --raw-wall --demo-close      # actual WSLg close Alien
./build/doom --raw-wall --demo-walk       # actual WSLg walking Alien
./build/doom --shots /tmp/g5i-shots --raw-wall
./build/doom --shots /tmp/g5i-checker --checker
./build/doom --raw-wall --demo-close --filter linear
```

Wall diagnosis: `--checker` preserves geometry/materials and shows repeated
64x64 tiles without strip gaps. `--filter linear` is an optional final
framebuffer softening, not a default and not a substitute for fixing texture
density/tiling. See `G5I-INVESTIGATION.md`.

## G5j wall tiling and shading diagnosis

The Tetris-like environment was traced primarily to **non-tileable G5d texture
crops**: opposite-edge RGB differences were 160–414 for the old wall assets,
versus 0.4–7.7 for the new original seamless textures. The UV grid found no
wall-strip gaps; low resolution, 2-pixel rays and four shading levels amplify
the seams but are not the primary defect.

Experimental flags:

```sh
./build/doom --raw-wall --bench 120 --new-textures
./build/doom --raw-wall --bench 120 --smooth-shading
./build/doom --raw-wall --bench 120 --new-textures --smooth-shading
./build/doom --shots /tmp/g5j-new --new-textures
./build/doom --shots /tmp/g5j-checker --checker
```

The new textures are
`sprites/textures_g5j/panel_v2.png` and `service_v2.png`. Recommended
configuration remains 320×200/160 rays with the prepared-masm wall path.
320-ray and 640×400 variants are blocked by loader and Glon projection/texrow
coupling, respectively; see `G5J-INVESTIGATION.md`.

## G5k industrial clutter, cover and flat surfaces

`--flat-surfaces` removes the Glon texrow pass and draws a cheap native depth
gradient. `--objects` loads six compact Glon-defined industrial props (two
drums, two crates, cabinet, low container) with shared projection/collision and
original transparent sprites.

Measured raw-wall run medians:

| Mode | FPS | Floor/ceiling | Total |
|---|---:|---:|---:|
| A textured | 20.6 | 21.4 ms | 48.6 ms |
| B flat | 33.2 | 2.35 ms | 30.1 ms |
| C flat + objects | 27.5 | 5.3 ms | 36.4 ms |
| D flat + objects + cover | 26.7 | 5.8 ms | 37.4 ms |

The flat deck is visibly emptier than the textured floor but reads acceptably
with clutter. Object-vs-sprite occlusion is alpha/depth sorted; object-vs-wall
is screen/door-state based, not per-pixel. See `G5K-INVESTIGATION.md`.

```sh
./build/doom --raw-wall --bench 120 --flat-surfaces
./build/doom --raw-wall --bench 120 --flat-surfaces --objects
./build/doom --demo-close --raw-wall --flat-surfaces --objects --alien-cover
./build/doom --demo-close --raw-wall --flat-surfaces --demo-emerge
./build/doom --shots /tmp/g5k-clutter --flat-surfaces --objects
```

## G5l OpenGL renderer

The G5k CPU renderer remains the reference. `--gl-bench`, `--gl-demo` and
`--gl-shots` use an SDL/OpenGL backend on the verified WSLg D3D12/Mesa device:

| Configuration | FPS |
|---|---:|
| CPU 320×200 flat+objects | 27.5 |
| GL 320×200 | 60.0 |
| GL 640×400 | 62.4 |
| GL 960×600 | 57.7 |

```sh
./build/doom --gl-bench 120 640 400 objects
./build/doom --gl-demo 640 400 objects
./build/doom --gl-shots /tmp/g5l-shot 640 400 objects
```

The OpenGL backend renders static walls/floor/ceiling, illuminated ceiling
panels, six objects, the Alien and the door with depth testing and alpha-tested
billboards. Glon still owns camera, movement, collision, door/Alien state,
object records and light phase. Static geometry is currently a renderer-side
ABI copy; moving it to a Glon scene block is the next cleanup step. See
`G5L-INVESTIGATION.md`.

## G5m playable OpenGL and creature replacement

Normal launch now uses OpenGL at 640x400 with the six G5k objects; CPU remains
available with `--renderer cpu`. `--renderer gl`, `--gl-bench`, `--gl-demo` and
`--gl-shots` are retained.

The circular motion tracker is restored as a depth-disabled 2D GL overlay using
the same Glon `Status` bearing/distance/detection state as the CPU tracker.

The G5l upside-down sprite was fixed by reversing source rows during GL texture
upload. The new creature is 128x192 original procedural biomechanical art with
idle/two walk/attack/side/death frames in `sprites/aliens_g5m/`.

Real WSLg key events verified W movement, one-shot E door opening, Left/Right
turning and D strafing on both GL and CPU backends. `--input-test` covers the
key mapping; manual verification steps are in `G5M-INVESTIGATION.md`. Use
`--gl-orient DIR 640 400` for the asymmetric TOP/BOTTOM upload diagnostic.

Measured GL 640x400 with objects: ~45.8 FPS (sim 14.9-17.0 ms), 960x600:
~41.5 FPS, versus G5k CPU 27.5 FPS. Ceiling lighting, object/Alien depth
occlusion, door occlusion and CPU fallback remain.

## G5n industrial corridor greybox

The GL corridor now has real 3D structure: recessed wall panels, ribs, ceiling
fixtures, pipes, floor plates/drains, service housings, a framed bulkhead and
3D props. All materials are explicitly temporary neutral solids; final artwork
is still required. See `G5N-CORRIDOR-REPORT.md` and `G5N-ASSETS.md`.

```sh
./build/doom --gl-tour /tmp/g5n-tour 640 400 objects
./build/doom --gl-tour /tmp/g5n-tour960 960 600 objects
./build/doom --gl-bench 120 640 400 objects
```

## G5o corridor artwork pipeline

The supplied prototype art pack is installed under `assets/corridor/` and
converted to runtime arrays by `tools/gen_corridor_textures.py`. Recessed wall
panels and the far bulkhead are visibly improved; the floor texture is applied
but low-contrast/stretched and the supplied images are not seamless. See
`G5O-ART-PIPELINE-REPORT.md`. `--placeholder-corridor` retains the greybox A/B
comparison. Only the first three textures were installed; the other eleven
materials remain pending.


## G5p corridor composition upgrade

Ceiling fixtures now have recessed frames and grilles; the deck uses world-scaled
plate UVs with edge drainage and no art-mode black grid; props were reduced and
kept against the walls. `--placeholder-corridor` remains available. Performance
at 960×600 is about 32 FPS (G5o was ~51), so further immediate-mode batching is
still needed. See `G5P-CORRIDOR-REPORT.md`; final seamless floor/ceiling/prop
artwork is still missing.

## G5p corrective pass

The spawn room, both door faces and all wall/floor/ceiling base surfaces now use
the existing GL materials instead of flat colours. Normal interactive play no
longer calls `glFinish` every frame; use `GLON_FINISH=1` when explicit GPU
synchronisation is required for profiling. A display-list cache was tested and
rejected because it ran below 1 FPS on WSLg D3D12. See
`G5P-CORRECTIVE-REPORT.md`.

## G5q Alien reference sprites

Five poses were cropped from the supplied transparent concept sheet using
`tools/convert_g5q_alien.py` and replace the previous procedural Alien textures.
Conversion uses a common scale, foot baseline and foot pivot; full turn/hit/death
sequences remain missing. See `G5Q-ALIEN-REPORT.md` and
`assets/alien/README.md`.

## G5r pulse-rifle reference integration

The supplied weapon references are full-scene corridor illustrations. The pulse
rifle/hand was manually masked into a transparent 768×406 sprite, and Glon now
owns `doom/ammo` (initial 24) with OpenGL drawing the live count in the blanked
display. The asset is static; firing, reload and the other three weapons are not
implemented. See `G5R-WEAPON-REPORT.md` and `assets/weapons/README.md`.

## G5s directional Alien integration

Seven native-resolution poses were extracted from two transparent reference
sheets: front idle, two distinct frontal walk poses, three-quarter, side, rear
and frontal attack. The Alien now approaches head-on with frontal artwork selected
from actual movement and camera-relative direction; crossing and rear paths use
the corresponding directional frames. The G5r pulse rifle and live ammunition
display remain visible. Directional acceptance passes, but close-up resolution
still fails because the source poses are only 188–268 pixels tall. See
`G5S-ASSET-REPORT.md` and `G5S-ALIEN-REPORT.md`.

## G5t engineering room

The spawn room is now a dedicated engineering compartment: a front-facing
turbine/generator with orange rotor, yellow guardrails, two coolant vessels with
valve wheels, service cabinets, a control bank, recessed wall bays, overhead
pipes, cable trays and ceiling fixtures. The corridor, deck material, Alien AI,
rifle/ammo, tracker, doors and controls are unchanged.

The room detail is renderer-side geometry; Glon's collision map is untouched.
The central door-to-spawn lane stays clear, and the machinery is placed around
the perimeter. `plain` reproduces the previous G5s room from identical poses for
before/after comparison.

```sh
./build/doom --gl-rooms /tmp/g5t-room 960 600 objects
./build/doom --gl-rooms /tmp/g5t-room-plain 960 600 objects plain
./build/doom --gl-rooms /tmp/g5t-room-max 1920 1080 objects
```

See `G5T-ROOMS-REPORT.md`.

## G5u Alien obstacle navigation and room accessibility

The normal game still starts in the G5t engineering room: player `(320,320)`,
Alien `(1120,320)`, closed door. The room is not a second location. Normal GL
launch now faces the player slightly toward the room (`angle=240`) so the
machinery and the door are both visible; `doom/start-angle` remains `0` for
tests, trace and scripted captures.

`doom.glon` now makes one bounded local sidestep when both pursuit axes are
blocked: it continues around the barrel/crate for a short interval, always
using the existing `check-all` collision checks, then resumes direct pursuit.
No pathfinding, teleport or new collision primitive was added.

```sh
./build/doom --g5u-nav /tmp/g5u-nav-after 960 600
./build/doom --g5u-nav /tmp/g5u-nav-before 960 600 legacy
```

`legacy` suppresses the new sidestep for the before/after reproduction.
See `G5U-NAVIGATION-REPORT.md`.
