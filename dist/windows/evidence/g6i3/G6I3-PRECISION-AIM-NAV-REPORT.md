# G6i.3 — precision aiming and reliable scuttler navigation

Branch: `glon-avp-g6i3`, based on G6i.2 `a3ddac6`
Source commit: `7ed73cf1a05c65685ad01fd387b8d12bc9e54890`  
Status: **implemented, packaged and verified; unaccepted pending physical Windows testing**  
Windows executable: `dist/windows/doom.exe`, **7,052,288 bytes**  
SHA-256: `8606416faaf31ee7e020093aa19c97475ea97aeff7ef865c8d7a1a446e13f654`  
Launcher: `dist/windows/run-g6i3.bat`

G6h.2, G6i, G6i.1 and G6i.2 were preserved. No merge, push, tag, release or
deploy. Frozen S1 remains unchanged
(`f90496c26dc45c7a387d8fc8639cd2781507d3d2`).

## 1. Precision downward aiming

Pitch is now stored internally in **millidegrees**, not whole degrees.

Named constants in `doom-host.c`:

- `G6I3_PITCH_DEG_PER_SEC = 30.0`
- `G6I3_PITCH_RETURN_DEG_PER_SEC = 300.0`
- `G6I3_PITCH_STEP_MDEG = 500` (0.5° per 60 Hz tick)
- `G6I3_PITCH_RETURN_STEP_MDEG = 5000` (5° per tick; 75° in 15 ticks / 0.25 s)

Behaviour:

- Down held: +0.5° per fixed simulation tick; clamped at 75°.
- Down released: **holds the current angle indefinitely**.
- R press: starts a smooth 300°/s return to 0°; R does not reset the level,
  player position or simulation.
- Down cancels an R return.
- Restart resets pitch to zero.
- Camera basis remains the G6i.2 camera-local right/up/forward basis; no roll.
- Firing still uses `tan(pitch)*64`; camera and firing direction stay synced.
- Weapon/reticle remain screen-space.
- No mouse aiming.

Tests cover pitch 0/10/20/30/45/60/75° in the camera basis, a one-tick Down
press (0.5°), release-and-hold, the 30°/s rate, clamp, R smooth return, pitch
held while moving/turning, height-aware shot sweeps, Space firing, horizontal
feet miss and restart reset. Native Windows framebuffer captures cover
0/10/20/30/45/60/75° plus a scuttler at the player's feet.

## 2. Scuttler navigation

The G6i.2 short-lived perpendicular steering was replaced with a **persistent
detour waypoint** rather than a longer timer:

1. Each scuttler tries direct axis pursuit.
2. If direct movement makes no progress, it tests a perpendicular detour
   waypoint 64 world units to one side, using the real wall/door/object
   collision at the target.
3. If that side is blocked, it tests the opposite side.
4. The chosen waypoint is persisted independently per scuttler until reached
   or blocked; there is no fixed 12-tick return to direct pursuit.
5. It then resumes direct pursuit; if blocked again, a new waypoint is chosen.
6. A blocked waypoint is abandoned and replanned rather than pushed forever.

Per-scuttler state reuses the existing unused `e1*`/`e2*` cells. **No new
top-level Glon variables or functions were added**, so the frozen VM map,
loader/global capacity and heap layout are unchanged. The route target is
validated at the actual collision radius (8 world units); movement still uses
the same collision checks as the player/props.

This is a compact deterministic **waypoint detour planner**, not A* or a full
grid. It is materially different from the G6i.2 timer and can move temporarily
away from the player. It is still local in scope: a future level with a deep
concave maze would need a waypoint graph or grid.

Actual-geometry regression scenarios in `--g6i2-gameplay-test` (retained and
extended) use the real objects at `(940,370)` and `(860,270)`:

- scuttler west of the barrel, player east — progresses past the barrel;
- scuttler east of the barrel, player west — progresses past it;
- scuttler beside the cabinet — progresses around it;
- three independent scuttlers — all move and remain collision-clear;
- player changes position mid-route — route responds;
- room/corridor corner — bounded progress, no clipping;
- closed door — scuttler remains on the blocked side;
- barrel no-rapid-oscillation check;
- final positions are checked against the real `doom/check-all` geometry.

Trace example from the barrel case: final scuttler `(954,370)` after 180 ticks,
or `(896,370)` on the opposite side, with a collision-clear final position. A
successful path search alone is not sufficient; the tests verify the simulated
scuttler actually moves and reaches a valid position.

## 3. Preserved player collision and gameplay

G6i.2 collision corrections are retained:

- diagonal speed normalized, cardinal speed 8;
- wall sliding via sequential X/Y resolution;
- cabinet retreat, corner escape, jump-adjacent-wall and alternating-input tests;
- no penetration and no permanent loss of movement.

Preserved: startup/credits/C/Escape/Enter, live memory, Space fire, Up jump,
W+Up, adult alien, three scuttlers, height-aware shooting, billboards, LOS,
proximity monitor, sounds, death/restart. No new enemies, acid, damage, weapons,
audio assets or asset optimizations.

## 4. Tests and measurements

- Linux `make test`: **PASSED**, including frozen-S1 verification.
- Native Windows title at 640×400/800×600/960×600/1280×720: **PASSED**.
- Native Windows input, pitch-input, look, camera, collision/navigation,
  projection, self, tracker, resize, stationary, entrance, collision probe,
  multi-alien, tracker-multi, scuttler/G6h.2 GL, jump, alien-collision, combat,
  door-sync, corpse, muzzle, flat/objects, raw-wall, trace, audio, sfx,
  audio-e2e and bench: **PASSED**.
- Loader: reserved 25,000; used **18,918**; free **6,082**; globals **277/512**;
  managed heap high **34,240**.
- Live title-state Windows memory: working **96,120,832 B**, peak **98,258,944 B**,
  private committed **114,925,568 B**.
- Benchmarks: Linux `sim=12.25 ms`, `frame=203.38 ms`; native Windows
  `sim=11.18 ms`, `frame=186.34 ms`. These are current measurements; the extra
  waypoint logic is measurable and no improvement is claimed.

## 5. Evidence and limitations

Evidence: `experiments/glon-doom/evidence/g6i3/` and
`dist/windows/evidence/g6i3/`.

- `camera/camera-00,10,20,30,45,60,75.png`, `camera-feet.png`
- `title-640x400` … `title-1280x720` (title, credits, title-after-Escape,
  gameplay)
- `nav/nav-before.png`, `nav-approach.png`, `nav-around.png`
- logs with title live memory, camera test, pitch input test, full navigation
  scenario assertions, and benchmark numbers.

G6i.3 remains **unaccepted** pending physical Windows testing. The navigation
implementation is a compact waypoint detour, not a general graph; deep concave
geometry remains a future limitation. Scuttler damage attacks and distinctive
movement/proximity sounds remain outstanding. The CPU renderer retains its
legacy 2D view.
