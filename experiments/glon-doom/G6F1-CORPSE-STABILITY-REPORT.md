# G6f.1 — Corpse Stability and Rifle Feedback

Status: implemented and tested on Linux and native Windows. Physical Windows
playtest remains for the user.

- Branch/worktree: `glon-avp-g6f1` at
  `/mnt/d/repos/glon-worktrees/glon-avp-g6f1`
- G6f baseline: `42d4156` (unchanged)
- Frozen S1: unchanged at
  `f90496c26dc45c7a387d8fc8639cd2781507d3d2`
- Implementation commit: `7bd5338`
- Documentation/evidence commit: see the commit following `7bd5338`

## 1. Root cause of apparent corpse movement

Simulation coordinates were **not** moving. `--corpse-trace` recorded:

- Alien before lethal hit: `1120,320`.
- After the first hit and through all 18 collapse ticks: `1118,320`.
- Final corpse: `1118,320`, phase 0, state 5.
- Collision centre: `1118,320`, corpse radius² 144.
- Door: open 64 / blocked 0 / target 64 throughout.
- Projected corpse centre from both east and west views: screen x 160.

Two renderer defects combined to make it look like the corpse moved:

1. **`alien_frame_select()` in `doom-host.c` only short-circuited states 3 and
   4.** State 5 fell through the living-Alien direction/frame logic. Its
   selected artwork and flip could therefore change with camera direction,
   movement smoothing and side offset. This was a real corpse-render state bug.
2. **The corpse was a camera-facing billboard.** The G6f OpenGL path rolled the
   billboard around the camera-right axis by 90°, and the CPU path used a
   screen-space sprite rotation. At 90° the body extended along camera-right,
   so reversing the camera put the body on the opposite **world** side of its
   anchor. The anchor, collision centre and door were unchanged.

The trace/evidence shows both:
- `evidence/g6f1/corpse-trace-before.log`: state 5, world position fixed,
  roll 90° under the old renderer.
- Rendered frames before the fix: the body extends left from the door from the
  east and right from the west, despite the same world anchor.

This was not simulation movement and not collision movement. It was
billboard/frame-selection orientation combined with projection.

## 2. Stability fix

- State 5 is now included in the `alien_frame_select()` early return and
  always uses the idle frame; it no longer runs living direction logic.
- The final corpse is no longer camera-facing. `gl_corpse_planes()` draws two
  low, world-aligned planes at the death location:
  - fixed long axis **world +X**;
  - fixed cross plane **world Y**;
  - no camera-right axis, no roll toward the camera;
  - height 22 world units, length 36, width 20.
- The CPU renderer draws a centered, flattened existing-sprite silhouette at
  the projected world anchor. It has no camera-facing rotation.
- Dying state 4 uses centered shrinking geometry plus low world planes, not a
  camera-right roll.
- Corpse collision remains at the exact death position and unchanged from G6f
  (`dx² + dy² < player-radius² + 144`).
- Death remains terminal until reset; reset removes the corpse and restores the
  living alien.

The corpse world x/y, state, phase and collision centre are invariant. The
visual body no longer swaps sides when the camera crosses it.

## 3. Ten-second stillness test

New `--corpse-stability-test` holds the player and camera fixed and runs 600
fixed frames (10 seconds at 60 Hz) after state 5:

- asserts `state=5`, `phase=0` every frame;
- asserts `alien-x/alien-y` never change;
- hashes the full rendered frame;
- hashes the corpse crop `(127,130)..(193,153)`;
- hashes a corpse-only sprite layer;
- saves first/last full frames when a directory is supplied.

Result:

```
corpse-stability-test: PASSED alien=1120,320
region=127,130..193,153 frames=600
```

The same test passes in the native Windows executable.

## 4. Muzzle flash and recoil

The existing authoritative event is still the Glon-owned ammunition decrease.
`audio_sfx_update()` already detects that event for rifle audio. The same
branch now starts:

- `muzzle_start_ns`;
- `muzzle_until_ns = start + 80 ms`.

The OpenGL weapon renderer:

- plays an additive yellow-white muzzle star at the barrel tip;
- shifts the weapon down/left slightly for recoil over the 80 ms;
- positions the flash in screen space attached to the weapon, so turning and
  movement do not detach it.

Tests verify:

- accepted shot: rifle SFX, muzzle active, duration exactly 80 ms;
- empty shot: no ammo decrease, no rifle SFX, no muzzle flash;
- rejected shot cannot flash.

The CPU renderer has no weapon overlay, so the muzzle flash is a GL-presentation
feature only. It is not a gameplay rule.

## 5. Metallic contact sound trace

The metallic sound is **not** gunfire or the door. It is the generic movement
collision SFX path:

```
move tick -> doom/check-all sets player_hit
          -> audio_sfx_update blocked=1
          -> sfx_collision_event(blocked,cargo,...)
          -> sfx_near_cargo(px,py,40)
```

- Near a crate: `SFX_CARGO` (`cargo_impact.wav`).
- Otherwise: `SFX_WALL` (`wall_thud.wav`).

At the reproduced corpse-contact point `(1098,320)`, `sfx_near_cargo` returned
1 because the crate at `(1100,370)` lies within 40 units. The actual emitted
event is therefore `cargo_impact.wav`. The test also isolates the generic path
and confirms `wall_thud.wav` away from cargo.

This is intentional existing collision feedback, not a corpse-specific sound.
Audio was not removed or replaced.

## 6. Verification

Linux full `make test`: **PASSED** RC 0. Frozen S1 passes.

Added/updated tests:

- corpse coordinates invariant before/through/after collapse;
- collision centre invariant and centre/offset clearance;
- corpse persistence while the player moves to the door and views from the
  opposite side;
- corpse cannot be damaged, move or attack;
- reset restores state 0, health 100, ammo 24 and removes corpse collision;
- 600-frame stationary corpse pixel/hash test;
- accepted-shot muzzle flash, 80 ms duration, and no flash when empty;
- actual cargo/wall collision-SFX classification;
- existing G6e/G6f combat, audio, traversal, projection, raw-wall, objects and
  entrance regressions.

Linux executable: **6,220,096 bytes**, SHA-256
`f9e90c0a972a4c503d95c054ea31fc010a49203d53420934c0ae198332f5a77d`.

## 7. Native Windows

- Windows executable: **6,582,784 bytes**, SHA-256
  `081cd7d06571f108a485327d12ddc871e0dbc05525a4a18e109e6f3a8d2bb2e7`.
- Distribution: **22,113,748 bytes**.
- Launcher: `dist/windows/run-g6f1.bat`.

Native Windows results:

- self-test: PASSED;
- combat-test: PASSED;
- corpse-stability-test: PASSED (600 frames);
- audio-e2e: PASSED, including muzzle/cargo/wall checks;
- WASAPI audio-device test: PASSED;
- SFX-device test: six effects through WASAPI;
- `run-g6f1.bat --self-test`: PASSED;
- GL corpse shots and muzzle shot rendered.

## 8. Visual evidence and renderer limitations

Before/after frames are under `evidence/g6f1/before/` and
`evidence/g6f1/after/`. The after corpse shots hide the first-person weapon in
the diagnostic camera so the low body is visible; in ordinary gameplay the
weapon can occlude a very close corpse.

What was visually inspected:

- before: corpse body appeared on opposite sides as the camera crossed;
- after: the low corpse stays centered at the same world position from east,
  west and before-door positions;
- muzzle star is visible at the barrel tip; the no-flash frame is clean.

Honest limitations:

- The corpse is **not** a true prone 3D body. It is two fixed low planes using
  the existing Alien sprite, plus a centered CPU silhouette. It can look like a
  low cutout and may intersect the floor/weapon at close range.
- Collision is a 12-unit-radius circle, while the visual plane is 36 × 20
  units. The collision is deliberately smaller than the full visual length, not
  a capsule or exact prone ellipse.
- The CPU renderer has no rifle overlay, so it cannot show the muzzle flash.
- The collapse is a centered shrink/plane transition, not an articulated fall.
- Frames were inspected, not physical playback. Physical Windows input, audio
  and perceived stability remain for the user.

## 9. Playtest

```bat
D:
cd D:\repos\glon-worktrees\glon-avp-g6f1\dist\windows
run-g6f1.bat
```

Check the corpse from both sides of the door, then hold still for 10 seconds,
and verify the muzzle flash on accepted shots only.

## 10. Evidence

```
evidence/g6f1/
  before/{east-of-corpse,west-of-corpse,before-door}.png
  after/{east-of-corpse,west-of-corpse,before-door}.png
  stability/corpse-stability-{first,last}.png
  muzzle/{muzzle-flash,no-flash}.png
  corpse-trace-before.log / corpse-trace-after.log
  linux-full-test.log / loader.txt / windows-build.log / metadata.txt

evidence/g6f1-win/
  self-test.log / combat-test.log / corpse-stability-test.log
  audio-e2e.log / audio-device-test.log / sfx-device-test.log
  batch-self-test.log / gl-corpse-shots.log / gl-muzzle-shots.log
  shots/...
```

No merge, push, tag or deploy. G6d/G6e/G6f baselines remain unchanged.
