# G6f — Alien Death, Collapse and Persistent Corpse

Status: implemented and tested on Linux and native Windows. Physical Windows
playtest remains for the user.

- Branch/worktree: `glon-avp-g6f` at
  `/mnt/d/repos/glon-worktrees/glon-avp-g6f`
- G6e baseline: `6c608a1` (unchanged)
- Frozen S1: unchanged at
  `f90496c26dc45c7a387d8fc8639cd2781507d3d2`
- Implementation commit: `95eb53e`
- Documentation/evidence commit: see the commit following `95eb53e`

## 1. Lifecycle

The Alien lifecycle remains fully authoritative in `doom.glon`:

| State | Meaning | Movement/attack | Collision |
|---|---|---|---|
| 0 | idle | normal AI | none |
| 1 | aware | normal AI | none |
| 2 | attacking | normal AI | none |
| 3 | hit reaction | paused | none |
| 4 | dying/collapsing | none | none |
| 5 | corpse | none, terminal | low corpse circle |

A non-lethal hit sets state 3 and a four-tick reaction counter. The host uses
that counter for a red flash and a small recoil offset; the state itself is
still assigned only by Glon.

A lethal hit sets health 0, state 4 and an 18-tick collapse counter. Each fixed
tick decrements the counter; at zero Glon sets state 5. State 5 skips the living
Alien update, so the corpse cannot move, pursue or attack. Reset restores
health 100, ammo 24, state 0 and clears the reaction/collapse counter.

The same `e2dep` Glon cell is reused as the state-local phase timer for hit and
death. No new entity framework, state list or VM primitive was added.

## 2. Combat and corpse rules

Existing G6e combat is preserved:

- one accepted shot per F/left-mouse press;
- ammo consumption, 640-unit range, forward depth, aim-ray circle test and
  wall/door `doom/los`;
- 25 damage, four hits to kill;
- no independent host firing cadence; rifle SFX follows authoritative ammo
  decrease.

Additional G6f rule: a shot can damage the Alien only when
`doom/alien-health > 0`. Shooting a dying/corpse Alien still spends ammo and
can play rifle SFX, but cannot change health or state.

## 3. Corpse collision

`doom/check-all` now adds a low corpse footprint when state 5:

```
dx² + dy² < player-radius² + 144
```

The corpse's own radius is 12 units, versus the standing Alien's conceptual
16-unit radius. This is deliberately a small circular footprint. The player
cannot walk through the corpse, but can pass it: the tests require a hit at the
corpse centre and a clear result 30 units away.

This is not a true prone capsule or ellipse. It is the smallest collision model
that prevents standing-dimension blocking while allowing the player around the
body. See limitations.

## 4. Rendering

No new art, mesh or animation framework was introduced. The existing Alien
sprite is transformed:

- **Hit:** state 3 draws the normal billboard with a red flash and a small
  camera-right recoil offset driven by the Glon phase.
- **Collapse:** state 4 rotates the same billboard around its ground pivot from
  upright to roughly horizontal over 18 fixed updates. OpenGL uses a rotated
  quad; the CPU renderer uses nearest-neighbour sprite rotation.
- **Corpse:** state 5 draws the same sprite at 90°, dimmed to 35% in OpenGL or
  30% brightness in the CPU renderer.

The host consumes `state` and `phase` from the Glon status capture. It never
chooses living/dying/corpse state.

### Renderer limitations

- This is a **rigid billboard tipping over**, not an articulated death. It is
  not a convincing biomechanical collapse.
- The corpse can visually intersect the floor or the weapon because the sprite
  plane has no depth volume; the OpenGL frames show this.
- There is no separate corpse mesh, gore, blood or body-part animation.
- The CPU renderer uses nearest-neighbour rotation and can show aliasing.
- The collapse is 18 discrete frames; it is deterministic, not smooth
  interpolated animation.

I inspected the rendered sequence frames on Linux OpenGL and native Windows
OpenGL. They show progression from upright through leaning to a dark prone
silhouette. That makes the lifecycle readable, but I do not claim the collapse
is visually convincing.

## 5. Tests

`--combat-test` was extended. It now covers:

- non-lethal hit: ammo 23, health 75, state 3, phase 4;
- hit reaction holds for three updates, then expires alive at state 1;
- aim, range and closed-door misses spend ammo without damage;
- four hits: ammo 20, health 0, state 4, phase 18;
- collapse holds state 4 through 17 updates with phase 1;
- exact transition to state 5 at phase 0;
- corpse remains state 5 through 120 further updates;
- corpse position is unchanged;
- corpse detection stays off;
- corpse collision hit at centre and clear 30 units away;
- shooting the corpse spends ammo but does not damage or reset it;
- reset removes corpse collision and restores state 0, health 100, ammo 24;
- deterministic replay of damage/state/ammo;
- visible hashes differ for living, hit, mid-collapse and corpse frames.

All existing G6e regressions remain in `make test`, including audio, traversal,
projection, raw-wall, objects, entrance and frozen S1.

Linux full suite: **PASSED** RC 0. Frozen S1 passes.
Linux executable: **6,211,552 bytes**, SHA-256
`3474c68f7ebf74a73c701d55a679e6025dd327b1f213ebdd01fa8e88ff91e13f`.

## 6. Native Windows

Built from this branch with the G6d Zig/SDL2 toolchain and SDL2 2.30.9 WASAPI.

- Windows executable: **6,564,864 bytes**, SHA-256
  `f1cbab09e37c0e0b9f1480476d5aa21a4d700d876862e79d7ca106ccb6d8ec74`
- Distribution: **22,095,709 bytes**
- Launcher: `dist/windows/run-g6f.bat`

Native Windows results:

- `--self-test`: PASSED;
- `--combat-test`: PASSED, including lifecycle/corpse/collision/reset;
- `--audio-e2e`: PASSED;
- `--audio-device-test`: WASAPI, 44100 Hz/2ch/256 samples;
- `--sfx-device-test`: six effects through WASAPI;
- `--gl-combat-shots`: native OpenGL collapse frames rendered;
- `run-g6f.bat --self-test`: PASSED.

## 7. Loader

No capacity, memory-map or frozen S1 change.

- After game load: `used=13104 free=1696`.
- After host blocks: `used=14610 free=190`, globals `187/256`.

The host still uses normalized movement masks and minimal render blocks in
non-rendering tests inherited from G6e. The G6f status capture adds only the
existing Glon `e2dep` phase; no new game global was added.

## 8. Physical Windows playtest

```bat
D:
cd D:\repos\glon-worktrees\glon-avp-g6f\dist\windows
run-g6f.bat
```

Check:

1. non-lethal hits show a visible red flash and recoil;
2. four hits trigger the collapse;
3. the collapse ends in a persistent dark corpse;
4. the corpse cannot attack, move or be damaged;
5. the player can walk around, not through, the corpse;
6. reset restores the standing living Alien and 24 rounds;
7. rifle/door/ambient audio still behaves as in G6e.

Physical input, acoustic feedback and whether the 18-frame collapse looks
acceptable on the target desktop remain unverified.

## 9. Evidence

```
evidence/g6f/
  metadata.txt
  linux-full-test.log
  combat-test.log
  audio-e2e.log
  loader.txt
  windows-build.log
  gl-collapse/01-dying-18.png ... 05-corpse.png

evidence/g6f-win/
  self-test.log
  combat-test.log
  audio-e2e.log
  audio-device-test.log
  sfx-device-test.log
  batch-self-test.log
  gl-combat-shots.log
  shots/01-dying-18.png ... 05-corpse.png
```

No merge, push, tag or deploy without approval. G6d/G6e baselines are not
modified.
