# G5u — Alien Navigation and Room Accessibility

**Status:** complete for the reported barrel failure and the engineering-room
discovery issue. The Alien now routes around the implicated barrel; the normal
start room is confirmed to be the G5t engineering room; normal GL launch faces
the player toward it while keeping the door visible.

**Branch/worktree:** `glon-avp-g5u` at `/tmp/opencode/glon-avp-g5u`, based on
G5t `0501c7616dfb5cc0ac5dff1c39bdde6ffe081074`.

**Implementation commit:** `4097c3e1f96468e0d557d33f1aec68dc8718d523`
(`Fix G5u Alien obstacle navigation and spawn visibility`). The report is
committed after the implementation; see branch HEAD. No push, merge, tag or
deploy was done.

## 1. Actual starting state and map

Normal interactive launch uses `interactive_gl()` with `objects_mode=1`, then
`load_session()`, `build_blocks()` and a one-time spawn orientation. The Glon
reset defaults remain:

- Player: `(320,320)`, angle `0`; normal GL launch then applies `angle=240`
  (`doom/start-angle` itself remains `0` for tests, trace and scripted modes).
- Alien: `(1120,320)`, state `0`, detected `0`.
- Door: `x=768, y=256..384`, initially `door-open=0`, `door-blocked=1`.
- Player radius: 20 subunits; Alien radius: 16 subunits.

Map bounds:

- Engineering room: `x=128..768, y=128..640`.
- Corridor: `x=768..1280, y=256..384`.
- The door joins them at the room's east side.
- The long north corridor is the existing `x=768..1152, y=384..1024` area.

**The G5t engineering room is the normal spawn room.** It is not a second room
or a different location. The G5t `--gl-rooms` captures used deliberately chosen
poses (`700,320`, `450,560`, etc.); they are the same world, but not the normal
spawn camera. The old normal camera at angle 0 faced the closed door and showed
only side/ceiling detail, which explains why the room could be mistaken for the
old starting room. Normal GL launch now starts at angle 240, showing both the
door on the left and engineering machinery on the right.

Object records (`objects.glon`), in normal object mode:

| Row | Type | Center | Half width | Half depth | Height |
|---:|---|---:|---:|---:|---:|
| 0 | barrel | 910,272 | 13 | 12 | 62 |
| 1 | barrel | **940,370** | 13 | 12 | 62 |
| 2 | crate | 860,270 | 28 | 19 | 48 |
| 3 | crate | 1040,370 | 30 | 18 | 48 |
| 4 | barrel | 980,270 | 18 | 12 | 70 |
| 5 | crate | 1100,370 | 36 | 16 | 38 |

## 2. Collision ownership

- **Glon-owned gameplay collision:** `doom/check-all` tests the room/corridor
  line boundaries and the door gap. When `doom/objects-on=1`, it also calls
  `doom/check-objects`, which treats every barrel/crate row as an axis-aligned
  solid with the Alien/player radius.
- Normal interactive launch sets `objects_mode=1`; `build_blocks` runs
  `objects-on` in that mode. The barrels and crates really are gameplay
  obstacles on the normal path.
- **Renderer-only geometry:** the G5t turbine, rails, cabinets, pipes, wall
  bays and other room detail are immediate-mode OpenGL geometry in
  `doom-host.c`. They have no Glon collision and did not cause the barrel
  incident.
- **Doors:** Glon owns the opening state and the `door-bars` rule. The renderer
  only draws the door and its frame.

The G5t machinery remains visually solid but non-colliding, as recorded in
`G5T-ROOMS-REPORT.md`. G5u did not add machinery colliders.

## 3. Reproduction of the barrel incident

Deterministic scenario: player just inside the engineering room at `(760,350)`
looking east, door open, Alien at `(970,368)`, state 1. This matches a player
retreating to the room and looking back through the doorway at the red barrel.

The `--g5u-nav` log, **before correction** (legacy AI), is:

```text
tick=000 player=760,350 alien=970,368 target=-210,-18 intended=-2,-2
actual tick=001 alien=970,366 moved=0,-2 reject_x=1 reject_y=0
actual tick=002 alien=970,364 moved=0,-2 reject_x=1 reject_y=0
actual tick=003 alien=970,362 moved=0,-2 reject_x=1 reject_y=0
tick=015 player=760,350 alien=968,352 target=-208,-2 intended=-2,0
actual tick=016 alien=968,352 moved=0,0 reject_x=1 reject_y=0
...
tick=239 player=760,350 alien=968,352 target=-208,-2 intended=-2,0
actual tick=240 alien=968,352 moved=0,0 reject_x=1 reject_y=0
```

Root cause:

1. The Alien's direct x move is rejected by the `(940,370)` barrel.
2. Its direct y move is allowed while there is a y difference, but it stops
   once the Alien is aligned with the player (`dy=-2`, below `alien-speed=2`).
3. The old AI had no fallback when both direct axes stopped making progress.
   It therefore held position at `(968,352)` indefinitely. This is not a render
   freeze: `state=1`, `detected=1`, no door bars.

The related wall-pocket variant (player at `(700,400)`) also reproduces: x is
rejected by the barrel and y toward the player is rejected by the corridor wall.
Both observations are the same missing local avoidance.

## 4. Exact correction

### Glon AI

`experiments/glon-doom/doom.glon` adds one signed field, `doom/alien-avoid`
(±14 ticks). The existing direct x/y pursuit still runs first.

When direct movement produces **no position change** and the target is still
outside `alien-attack2`:

- If x is the dominant target axis, the Alien sidesteps in y; otherwise it
  sidesteps in x.
- The side is chosen from the existing corridor geometry:
  `y>320` chooses the lower-y side, otherwise the upper-y side; for y-dominant
  cases, `x>768` chooses the lower-x side.
- During the 14-tick sidestep, the dominant direct axis is retried each tick.
  Once it moves, the sidestep is cleared and normal pursuit resumes.
- Every attempted step still goes through `doom/check-all`; walls, closed
  doors and barrels/crates remain solid. There is no teleport and no
  renderer-side movement.
- The existing state machine (`state=1` pursue, `state=2` attack), detection,
  door bars and attack range are unchanged.

The existing scratch fields `doom/adx`/`doom/ady` are reused for the
before-move positions; the only new Glon global is `doom/alien-avoid`.

### Normal launch orientation

`interactive_gl()` applies `[doom/angle: 240 doom/aim]` once after loading. This
is a normal-launch-only presentation/discoverability adjustment: the player
still starts at `(320,320)`, and `doom/start-angle` remains `0` so tests,
`--trace`, `--gl-tour` and scripted captures are unchanged. The view shows the
door on the left and engineering detail on the right instead of facing only the
closed door.

### Test/capture mode

`doom-host.c` adds `--g5u-nav DIR W H [legacy]`:

- `legacy` resets `doom/alien-avoid` after each tick to reproduce the old AI.
- Without `legacy`, it runs the corrected AI.
- It captures the normal start, the leave/return route, and the barrel
  before/after frames through the real OpenGL renderer.

## 5. Regression scenarios and results

| Scenario | Result |
|---|---|
| Unobstructed corridor pursuit | `--trace`: Alien moves from 1120 toward the player; state 1 then attack state 2. |
| Barrel/crate obstacle | Before: stuck at `968,352` for 240 ticks. After: `(970,368) -> (964,346) -> (884,348)` by tick 60, then attack range. |
| Open doorway | Route: door `48/0`, then `64/0`; Alien follows from `1120,320` to `1070,320` in corridor. |
| Alien enters engineering room | Route continues; Alien is at `(502,320)` inside the room after the player returns. |
| Closed door / solid wall | Route at closed door has `door=0/1`; Alien remains at `1120,320`. Existing collision probes and self-test pass. |
| Player enters/leaves room | Route: spawn -> door `(744,320)` -> corridor `(904,320)` -> back through door to room `(152,320)`. |
| No permanent barrel stall | Corrected trace keeps moving each relevant tick; final state is attack, not idle/stuck. |
| G5s directional animation | `--gl-directional` completes; frontal/three-quarter/side/rear frames still render. |
| G5r rifle/ammo | Visible with live `24` in all GL captures. |
| Frozen S1 | Passes at `f90496c26dc45c7a387d8fc8639cd2781507d3d2`. |

## 6. Interactive verification and screenshots

The ordinary executable was launched at 640×400 (`timeout 8 ./build/doom`); it
entered the normal OpenGL path and logged:

```text
G5p renderer=OpenGL framebuffer=640x400 interactive=on
player=320,320 angle=240 door=0/1 alien=1120,320 state=0
```

Actual GL captures, 960×600:

- `/tmp/g5u-nav-after/00-normal-start.png` — normal spawn, angle 240
- `/tmp/g5u-nav-after/01-route-facing-door.png`
- `/tmp/g5u-nav-after/02-route-at-closed-door.png`
- `/tmp/g5u-nav-after/03-route-door-open.png`
- `/tmp/g5u-nav-after/04-route-in-corridor.png`
- `/tmp/g5u-nav-after/05-route-back-in-room.png` — Alien inside the room
- `/tmp/g5u-nav-after/06-barrel-start.png`
- `/tmp/g5u-nav-after/07-barrel-30.png`
- `/tmp/g5u-nav-after/08-barrel-60.png` — Alien has rounded the barrel
- `/tmp/g5u-nav-after/09-barrel-120.png`
- `/tmp/g5u-nav-after/10-barrel-240.png`

Before correction:

- `/tmp/g5u-nav-before/00-normal-start.png` — old angle-0 spawn view
- `/tmp/g5u-nav-before/06-barrel-start.png`
- `/tmp/g5u-nav-before/08-barrel-60.png` — still behind the barrel
- `/tmp/g5u-nav-before/10-barrel-240.png`

Comparisons:

- `/tmp/g5u-compare-00-normal-start.png`
- `/tmp/g5u-compare-06-barrel-start.png`
- `/tmp/g5u-compare-08-barrel-60.png`

1920×1080:

- `/tmp/g5u-nav-after-max/00..10-*.png`

Directional Alien regression:

- `/tmp/g5u-dir/01-headon-long.png` … `07-headon-attack.png`

## 7. Performance at 960×600

Matched 180-frame runs, three runs each, same environment/device (WSLg D3D12
AMD Radeon), medians:

| Configuration | G5t | G5u | Delta |
|---|---:|---:|---:|
| No objects | 104.3 FPS (9.59 ms) | 86.5 FPS (11.56 ms) | −17.1% |
| With objects | 60.2 FPS (16.62 ms) | 51.4 FPS (19.46 ms) | −14.6% |

- G5u object-mode medians: sim 13.24 ms, submit 1.20 ms, total 19.46 ms.
- G5u no-object medians: sim 5.51 ms, submit 1.10 ms, total 11.56 ms.
- Loader after host blocks: 174 cells free; table probe largest 128. The compact
  fix is close to the 64K loader limit; no large pathfinding framework was added.
- Loader/heap high-water: 54,569/33,440 without objects, 54,569/33,600 with.
- Executable: 6,100,344 bytes.

The cost is the extra Glon avoidance branches on every tick. Direct pursuit
payoff is still simple; the no-object regression is real (~17%) and is reported
rather than hidden. The object-mode game remains above 50 FPS at 960×600.

## 8. Remaining limitations

- The correction is a bounded local sidestep, not a general navmesh or A*. It
  chooses a side from corridor position and clears ordinary barrel/crate
  pockets; a large concave obstacle, a very wide object, or a target beyond the
  14-tick sidestep may still require more sophisticated navigation.
- The side heuristic (`y>320`, `x>768`) is tuned to this map's corridor. It is
  documented rather than presented as general-purpose pathfinding.
- G5t machinery remains renderer-only geometry with no collision; the player
  can walk through turbine/cabinet volumes. This pass preserves that existing
  behavior rather than adding new world colliders.
- Normal spawn orientation is applied in `interactive_gl`; CPU fallback still
  uses angle 0 and has no G5t engineering-room geometry.
- The Alien stops at attack range (state 2) rather than physically entering the
  room in the close barrel scenario; with a farther player the route shows it
  entering the room normally.
- The loader has very little headroom (174 cells before the table probe), so
  further Glon-side navigation growth should be a measured, separate change.
- No new art pack, renderer architecture, weapon mechanic or frozen-S1 change
  was introduced.

## Acceptance

- **Navigation:** the confirmed barrel stall is reproduced, explained by the
  trace, and corrected; the Alien moves around the barrel while remaining
  subject to Glon wall/door/object collision.
- **Accessibility:** the engineering room is the spawn room; normal GL launch
  now visibly presents it, and the route leaves and re-enters it. The Alien also
  follows into the room when the player is far enough away.
- **Architecture:** Glon remains authoritative for pursuit, stopping, collision
  and door rules. No renderer-only gameplay workaround or S1 primitive was
  added.
- **Preservation:** G5s Alien art/AI states, G5r rifle/ammo, corridor, engineering
  room, tracker, controls, lighting and frozen S1 remain intact.
