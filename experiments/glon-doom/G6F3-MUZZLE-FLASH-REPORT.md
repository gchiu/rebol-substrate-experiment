# G6f.3 — muzzle-flash alignment

Branch/worktree: `glon-avp-g6f3`,
`/mnt/d/repos/glon-worktrees/glon-avp-g6f3`  
Baseline: accepted G6f.2, commit `d68ee43` / tag `avp-g6f2-accepted`  
Implementation commit: `00c37ff5570c3f6645bd2d7446bfa2e9f89dbb0d`

Status: **IMPLEMENTED AND AUTOMATED-VERIFIED; AWAITING PHYSICAL WINDOWS
VERIFICATION.** G6f.3 is not an accepted baseline.

## 1. Observation and diagnosis

The user's physical Windows playtest of G6f.2 reported that the muzzle flash
appeared approximately one hand-width away from the weapon muzzle, towards the
supporting hand. That is the user observation; the diagnosis below is the
agent's.

The complete weapon/flash path was traced:

- the weapon is the 768x406 `weapon_pulse` sprite (`weapon_sprites.h`);
- `gl_draw_weapon()` places it in an orthographic screen-space pass:
  width 60% of the drawable, aspect preserved, anchored bottom-right;
- the old flash anchor was the hard-coded normalized sprite coordinate
  `(0.20w, 0.55h)`; that location is on the supporting hand/forearm, not the
  barrel;
- the visible barrel exit in the supplied reference is reference `(805,570)`,
  which maps to weapon-sprite `(188,96)` — normalized `(0.24479,0.23645)`;
- weapon quad, live ammo display and flash all already used the same viewport
  and orthographic projection. Resizing changed `gl_w/gl_h` once per frame;
  recoil shifted the weapon rectangle. The defect was therefore an incorrect
  anchor in weapon space, not a viewport/resize discrepancy;
- CPU rendering has no first-person weapon overlay and therefore no muzzle
  flash. This remains a GL-presentation correction.

The flash was not fixed by a screen-pixel offset. The generator now emits the
barrel anchor from the reference muzzle, and the runtime transforms it through
the same `x0/y0/w/h` weapon transform, including recoil.

## 2. Changes

- `weapon_sprites.h` / `tools/convert_g5r_weapon.py`: generated
  `WEAPON_MUZZLE_X 188`, `WEAPON_MUZZLE_Y 96` from reference muzzle `(805,570)`.
- `doom-host.c`:
  - shared `gl_weapon_geometry_at()` for weapon placement and recoil;
  - `gl_weapon_muzzle_at()` maps the sprite anchor through that geometry;
  - flash uses the shared anchor; old `(0.20,0.55)` remains only as a red
    diagnostic X in `--gl-muzzle-shots`;
  - deterministic five-frame capture at rest, first shot, peak, recoil and
    return; native Windows GL captures at 960x600 and 1280x720;
  - `--muzzle-anchor-test` verifies normalized anchor placement, sprite
    opacity/ROI membership and recoil following at 960x600, 1280x720 and
    1920x1080.
- `Makefile`: focused anchor test added to `make test`.

No change to flash duration (80 ms), firing authorization, ammunition, damage,
collision, LOS, audio, enemy behaviour, doors or frozen S1.

## 3. Measurements

At 960x600 the old flash was `(489.60, 474.98)` and the barrel anchor is
`(515.40, 379.50)`: delta `(+25.80, -95.48)` pixels. At 1280x720 the comparison
is `(652.80, 551.70)` to `(687.20, 424.40)`: delta `(+34.40, -127.30)`. At
1920x1080 it is `(979.20, 827.55)` to `(1030.80, 636.60)`: delta
`(+51.60, -190.95)`. The normalized anchor is invariant at
`(0.24479, 0.23645)` across all three sizes.

At a controlled half-flash recoil (3 px), the anchor follows the weapon by
`(-1.05, +3.00)` pixels, exactly the weapon-space transform. The native Windows
GL captures were rendered by `AMD Radeon(TM) Graphics`, OpenGL 4.6 compatibility
context.

## 4. Evidence

- `evidence/g6f3/diagnostics/before-after.png` — G6f2 hand flash versus G6f3.
- `evidence/g6f3/diagnostics/before-flash-diagnostic.png` — old frame with old
  and barrel anchors marked.
- `evidence/g6f3/diagnostics/after-flash-diagnostic.png` — in-engine green
  anchor/flash origin and retired red X.
- `evidence/g6f3/diagnostics/weapon-sprite-anchor.png` — source sprite anchor
  `(188,96)` and retired `(0.20,0.55)`.
- `evidence/g6f3/after/windows-960/01-rest.png` through `05-return.png` and the
  corresponding `windows-1280/` five frames.
- `evidence/g6f3/logs/` — Linux full test/build, native Windows anchor, self,
  combat, door-sync, corpse-stability, audio-e2e, GL shots and distribution
  self-test logs.

## 5. Verification

Linux `make test`: **PASSED**, including frozen S1
`f90496c26dc45c7a387d8fc8639cd2781507d3d2`. Native Windows automated:
`--muzzle-anchor-test`, `--self-test`, `--combat-test`, `--door-sync-test`,
`--corpse-stability-test` and `--audio-e2e` all **PASSED**. The packaged
`run-g6f3.bat --self-test` also passed. The Windows distribution executable was
not rebuilt during verification beyond the final implementation build.

## 6. Windows build

- Executable: `dist/windows/doom.exe`
- SHA-256:
  `cf3b106e423a16b62b4a45efdf928dba307101d4f746c9f1ee36b638a3cd2563`
- Size: 6,596,608 bytes
- Distribution: 22,128,021 bytes
- Launch: `dist/windows/run-g6f3.bat`

The accepted G6f.2 distribution and tag are untouched.

## 7. Remaining limitations

- Physical Windows verification of G6f.3 is pending; do not mark this build
  accepted until the user has played it.
- Only the two captured viewports were visually inspected; arbitrary aspect
  ratios use the same transform but were not separately photographed.
- The CPU fallback still has no weapon or muzzle flash.
- The flash is anchored to the supplied sprite's visible masked muzzle; this is
  not a 3D barrel model and there is no separate muzzle-flash sprite.
- G6f.2's documented limitations remain: approximate CPU/GL corpse planes,
  constrained loader/memory, and the closing-audio 410 ms asset offset.
- The diagnostic overlay is enabled only by `--gl-muzzle-shots`; normal play has
  no crosshair markers.
