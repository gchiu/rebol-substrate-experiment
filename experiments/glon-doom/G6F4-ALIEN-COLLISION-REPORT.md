# G6f.4 — alien collision investigation and correction

Branch/worktree: `glon-avp-g6f4`,
`/mnt/d/repos/glon-worktrees/glon-avp-g6f4`  
Baseline: G6f.3 HEAD `05f18a7` (G6f.3 physical acceptance still pending)  
Implementation commit: `3266519d191cfcc0bdf71445549b4e94d0705bac`

Status: **IMPLEMENTED AND AUTOMATED-VERIFIED; AWAITING PHYSICAL WINDOWS
VERIFICATION.** G6f.4 is not an accepted baseline and has no acceptance tag.

## 1. User observation and root cause

User-confirmed physical Windows observations from G6f.3:

- the player could walk through the living alien;
- after killing it, the player could not pass its apparent corpse position.

Agent diagnosis, established with source tracing and identical-start movement
traces:

1. **Living aliens were never collided with the player.** `doom/check-all`
   contained wall/door/object checks and one state-5-only alien check. Player
   `doom/move-x`/`doom/move-y` called `check-all`, but there was no check for
   states 0–4. The living alien was therefore passable.
2. **The apparent corpse block was a Glon collision, not a host or world
   collider.** The only alien condition was in `doom/check-all` for state 5:

   ```
   dx² + dy² < player-radius² + 144
   ```

   That legacy corpse footprint was applied to the player movement path and
   rejected candidates within roughly 23.3 units. It was not a separate corpse
   body or OpenGL/world object.

The two observations are related defects in the same collision path but are
not one condition: living collision was missing, while a legacy state-5-only
collider caused the post-death block. Both belong to Glon simulation. Wall and
door collision belong to the existing `check-all` path and were unchanged.

## 2. Correction

- Removed the state-5 corpse block from `doom/check-all`. Wall/door/object
  collision still uses that shared path; alien movement is not given a
  self-collision problem.
- Added `doom/check-alien`, parameterized by alien centre, lifecycle state,
  combined collision radius², and candidate player position.
- Player movement checks the alien only after the wall/door/object check.
- Combined player+alien footprint is `(20 + 16)² = 1296` (36 world units).
- State 5 is explicitly excluded from `check-alien` and is therefore
  non-blocking. States 0–4 are solid.
- The alien AI and its wall collision were not changed. In current gameplay it
  attacks from outside this footprint; future multiple enemies can call the same
  state-aware helper with each enemy's centre/radius.

### Collision-state table

| Alien state | Meaning | Player collision |
|---|---|---|
| 0 | idle/undetected living | solid |
| 1 | approach/chase living | solid |
| 2 | attack living | solid |
| 3 | hit reaction | solid |
| 4 | dying collapse, 18 ticks | solid |
| 5 | terminal corpse | **non-blocking**, still rendered |

## 3. Before/after movement traces

The diagnostic uses player `1000,320`, alien `1120,320`, angle 0, open door,
and the same `+8` forward step each tick. It records before, proposed, actual,
alien state/position, distance², door state, collision flag and reason.

- Before (G6f3 Glon): `evidence/g6f4/before/movement-trace-baseline.log`
  - living state 3: reaches `1160,320` — passes through;
  - dying state 4: passes through before/while becoming state 5;
  - dead state 5: stops at `1096,320`; every candidate `1104` is rejected with
    `reason=legacy-corpse-footprint` and distance² `576`.
- After (G6f4):
  - living state 3: stops at `1080,320`; candidate `1088` is rejected with
    `reason=alien-footprint`;
  - dying state 4: stops at `1080` while state 4, transitions to state 5 on the
    deterministic 18-tick boundary, then moves through;
  - dead state 5: reaches `1160,320` with `hit=0, moved=1`.
  - Full trace: `evidence/g6f4/after/movement-trace-fixed.log`.

The diagnostic distinguishes rejection (`actual` equals `before`, `hit=1`) from
input/camera/world causes. The test corridor is open, the door is open, and the
only blocking condition in the scenario is the alien footprint.

## 4. Verification

Linux full `make test`: **PASSED**, including all G6e/G6f/G6f.1/G6f.2/G6f.3
regressions, the new `--alien-collision-test`, and frozen S1 unchanged at
`f90496c26dc45c7a387d8fc8639cd2781507d3d2`.

New focused test `--alien-collision-test` verifies:

- state 0 living collision; state 4 dying collision; state 5 non-blocking;
- player cannot pass a living alien;
- player can cross the dead alien's former position;
- dying collision changes to passable at the terminal state;
- walls remain solid and closed doors remain solid;
- reset restores living collision;
- five death/reset cycles leave no invisible blocker;
- existing door/open and wall paths remain traversable/solid as before.

Native Windows automated results:

- `--alien-collision-test`: PASSED
- `--alien-collision-trace`: PASSED
- `--self-test`: PASSED
- `--combat-test`: PASSED
- `--door-sync-test`: PASSED
- `--corpse-stability-test`: PASSED
- `--muzzle-anchor-test`: PASSED
- `--audio-e2e`: PASSED
- packaged `run-g6f4.bat --self-test`: PASSED

## 5. Windows build

- Executable: `dist/windows/doom.exe`
- SHA-256:
  `54828854abc4979ad80d3b349538bb4abd5a60c0d1dd1db0a015ebed806e3363`
- Size: 6,605,312 bytes
- Distribution: 22,137,018 bytes
- Launch: `dist/windows/run-g6f4.bat`

G6f.2 acceptance/tag and the unaccepted G6f.3 build remain separate and
untouched. G6f.4 has no tag.

## 6. Known limitations

- Physical Windows verification is pending; G6f.4 is not accepted.
- G6f.3 physical acceptance remains a separate pending status.
- Collision is one circular footprint, not a sprite-shaped or articulated
  capsule. Dying remains solid for the 18-tick collapse; only state 5 is
  passable.
- The runtime still contains one alien; `check-alien` is parameterized for a
  future collection but no multi-alien behaviour is implemented.
- The alien AI does not receive a reciprocal player collider; its current attack
  range keeps it outside the footprint, but future multi-enemy work must review
  mutual collision.
- G6f.2's corpse/audio/loader limitations and G6f.3's muzzle-anchor limitations
  remain unchanged.
