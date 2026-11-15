# G6g — jumping and vertical collision

Branch/worktree: `glon-avp-g6g`,
`/mnt/d/repos/glon-worktrees/glon-avp-g6g`  
Baseline: G6f.4 `6b6a1c9` (G6f.4 physical acceptance still pending)  
Status: **IMPLEMENTED AND AUTOMATED-VERIFIED; AWAITING PHYSICAL WINDOWS
VERIFICATION.** G6g is not an accepted baseline and has no acceptance tag.

## 1. Architecture and ownership

Glon owns the vertical state and all movement/collision decisions. The native
host owns Spacebar input, fixed-tick scheduling, OpenGL presentation and camera
height. No frozen S1 file or primitive changed.

New Glon state in `doom.glon`:

- `doom/jump-z` — feet height in **1/8 world units**;
- `doom/jump-vz` — vertical velocity in the same units per tick;
- `doom/grounded` — 1 on the floor, 0 airborne;
- `doom/jump-armed` — input latch requiring a release after a jump;
- `doom/jump-input` — edge request consumed inside `doom/tick`.

### Vertical equations and tuning

Fixed tick is 60 Hz, horizontal speed remains 8 world units/tick.

- initial upward velocity `v0 = 19` z-units/tick = 2.375 world units/tick;
- gravity `g = 1` z-unit/tick² = 0.125 world units/tick²;
- each airborne tick: `z += vz; vz -= g`;
- landing clamps `z=0`, `vz=0`, `grounded=1`.

Discrete result: **39 airborne ticks** (39/60 = 0.65 s), **apex 190 z-units =
23.75 world units** (two-tick plateau at the top). This clears the rendered
corpse top of 22 world units with margin. Air control is full: the normal 8-unit
horizontal input is applied while airborne. Jumping from rest adds no forward
impulse.

The jump input is edge/latched: holding Space does not start another jump after
landing; a release and new press is required.

## 2. Collision rules

`doom/check-all` continues to own full-height walls, closed/open doors and
objects. Its result does not depend on jump height. `doom/check-alien` handles
the alien separately so alien movement does not self-collide.

| Obstacle | Horizontal collision |
|---|---|
| Floor | supports player; landing clamps z |
| Full-height wall | solid at every jump height |
| Closed door | solid |
| Open door | traversable |
| Living alien states 0–2 | solid at every jump height |
| Hit reaction state 3 | solid |
| Dying state 4 | solid for its 18-tick collapse |
| Dead state 5 | low obstacle; blocks only while feet `z < 176` (22 world), then passable when `z >= 176` |

Collision radii are combined circles:

- living/dying: `(player 20 + alien 16)² = 1296`;
- corpse: `(player 20 + corpse 12)² = 1024`.

If the player is already overlapping a body—for example after landing on a low
corpse—movement **outward** is allowed. This prevents a permanent trap while
still rejecting inward movement.

The old G6f legacy state-5 footprint was removed in G6f.4 and is not restored;
G6g uses this general height-aware check.

## 3. Camera and rendering

`gl_view()` raises the OpenGL eye by `jump_z / 512` GL units, equivalent to
`z / 8` world units. Camera height therefore reaches `64 + 23.75 = 87.75` world
units, below the 128-unit ceiling. Reset sets z to zero, so there is no permanent
offset. The weapon overlay and G6f.3 muzzle anchor are unchanged. The CPU
fallback remains flat (no vertical camera), as documented below.

Native Windows GL captures use `AMD Radeon(TM) Graphics`, OpenGL 4.6:
`evidence/g6g/gl-windows-960/` and `gl-windows-1280/`, plus
`diagnostics/camera-rest-apex-landed.png`.

## 4. Input

Spacebar maps to an edge-triggered `jump_pending` in the native host. The host
sets Glon `jump-input` only for the next tick; Glon owns the arm/landing logic.
Holding Space does not retrigger. Existing W/A/S/D, arrows, E, F/mouse, M, [ ],
R and Esc mappings are unchanged.

## 5. Deterministic tests and traces

`--jump-test` covers all requested cases and prints the stationary trajectory.
`evidence/g6g/traces/jump-test.log` contains the takeoff/ascent/apex/descent/
landing trace (`z=19,37,...,190,190,...,19,0`). Key results:

- stationary and forward/diagonal jumps return to z=0;
- air control reverses direction;
- held jump does not retrigger;
- grounded corpse stops at x=1088; jumping clears it and lands past x=1120
  (test final x=1256 in the corridor);
- living and dying aliens remain solid during a jump;
- closed door and wall cannot be bypassed; open door traverses;
- three repeated jumps land at z=0 every time; reset restores z=0 and permits a
  fresh jump.

`--alien-collision-test` validates grounded corpse blocking vs
`z=176` clearance, living/dying solidity, walls and closed doors.
`--collision-probe-test` preserves the previous wall-radius, alien-radius and
LOS probes in a separate low-loader test.

Linux full `make test`: **PASSED**, including G6e/G6f/G6f.1/G6f.2/G6f.3/G6f.4
regressions and frozen S1 `f90496c26dc45c7a387d8fc8639cd2781507d3d2`.

Native Windows automated:
`--jump-test`, `--alien-collision-test`, `--collision-probe-test`,
`--self-test`, `--combat-test`, `--door-sync-test`,
`--corpse-stability-test`, `--muzzle-anchor-test`, `--audio-e2e`: **PASSED**.
Packaged `run-g6g.bat --self-test` and `--jump-test`: **PASSED**.

## 6. Windows build and playtest

- Executable: `dist/windows/doom.exe`
- SHA-256:
  `e9f6013c9cf2685bc085baa5ecb816b66eed307bf505c04c9b5777a27a0c228b`
- Size: 6,616,576 bytes
- Distribution: 22,151,038 bytes
- Launcher: `dist/windows/run-g6g.bat`

Windows instructions:

```
D:
cd D:\repos\glon-worktrees\glon-avp-g6g\dist\windows
run-g6g.bat
```

Controls: W/A/S/D move, arrow keys turn, E door, F or left mouse fire, **Space
jump**, M mute, [ / ] volume, Esc quit.

Physical checks required: jump while stationary and moving; land naturally;
jump a dead corpse without wall/door penetration; confirm living/dying aliens
remain solid; confirm camera rise/fall has no lasting offset and no ceiling
clipping.

## 7. Commits and evidence

Implementation commit: `690212629d9eca78fc75fed741353858a560a409`.
Evidence commit: the following commit containing this report,
`evidence/g6g/` and the distribution.

Evidence: `experiments/glon-doom/evidence/g6g/` (`traces/`, `logs/`,
`gl-windows-960/`, `gl-windows-1280/`, `diagnostics/`,
`artifact-sha256.txt`).

## 8. Known limitations

- Physical Windows acceptance is pending; G6g has no tag.
- G6f.2 remains the only accepted baseline; G6f.3 and G6f.4 physical
  acceptance remain separately pending.
- CPU fallback has no vertical camera or weapon overlay; the requirement is met
  by the native Windows OpenGL path.
- Camera follows the 60 Hz simulation without presentation interpolation.
  Normal 60 Hz rendering is step-free at the tick rate; interpolation was not
  added.
- No ceiling collision is implemented because maximum eye height is 87.75 below
  the 128-unit ceiling.
- Only the corpse is currently a low obstacle; walls/aliens are full-height.
  The helper is parameterized, but no general object-height table or
  multi-alien implementation was added.
- Loader capacity is tight. Normal gameplay pre-materialises all movement tick
  blocks; objects/raw-wall diagnostic modes materialise tick blocks lazily to
  fit the existing 14,800-cell loader arena. No capacity or memory-map change.
