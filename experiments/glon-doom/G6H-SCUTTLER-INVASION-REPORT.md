# G6h — scuttler invasion

Branch/worktree: `glon-avp-g6h`,
`/mnt/d/repos/glon-worktrees/glon-avp-g6h`  
Baseline: G6g `57988d2` (G6g physical acceptance still pending)  
Implementation commit: `adc9ab67f44a3964da2d4a676e7f7e06f2b3b072`

Status: **IMPLEMENTED AND AUTOMATED-VERIFIED; AWAITING PHYSICAL WINDOWS
VERIFICATION.** G6h is not an accepted baseline and has no tag.

## 1. Authorized capacity change

The previous decision to block G6h on loader size was explicitly overridden for
this milestone. The 64K reference map cannot support 32,768 loader cells at base
40,000 (it would end at 72,768). The game-only build therefore uses the maximum
contiguous space before the task table:

- loader: `[40000, 65000)` = **25,000 cells** (up from 14,800);
- the game build reuses the otherwise-unused M1 task arena `[54800, 65000)`;
- frozen S1 files, seven primitives, evaluator semantics and the 64K map are
  unchanged;
- M1 multitasking remains disabled/not used by AVP; default/M1 builds are not
  given the game macro;
- game-only `GAME_CPPFLAGS`: `-DR0S1_HEAP_LIMIT=65000L -DGLON_AVP_G6H=1`.

The global context for this game build is enlarged from 256 to 512 bindings, and
the runtime hash stride is gated to 512 under `GLON_AVP_G6H`; default builds keep
the original 256-binding/stride-256 behavior. This was required for the new
multi-entity globals and objects mode.

## 2. Entity architecture

Glon owns four bounded entities: the existing adult plus three scuttlers.

Per scuttler state: `x`, `y`, `health`, `state`, `timer`. Shared getter/setter
helpers, one common AI update, one shared collision circle, one shared target
consideration and one damage helper implement the reusable behavior. The adult
keeps its existing globals/AI/render path.

AI states:

| State | Behaviour |
|---|---|
| 0 | deterministic pause; timer expiry begins scuttle |
| 1 | pursue player at speed 4; timer cycles back to pause |
| 2 | short-range attack/lunge pose, then pause |
| 3 | hit/flinch, then resume pursuit |
| 4 | dying timer then terminal corpse |
| 5 | dead, stationary |

Scuttler health is 50; 25 damage per selected shot; two hits kill. Attack range
is 48 world units. Timers make behaviour deterministic with no random system.
Scuttlers use `check-all` for walls, closed doors and objects; they cannot cross
solid geometry.

## 3. Combat, collision and jump

- Fire spends one round, then considers adult and all three scuttlers using
  forward depth, aim-circle, 640-unit range and line of sight; nearest valid
  depth wins.
- Only the selected target takes damage; other enemies continue their AI.
- All living scuttlers have low collision footprints (combined radius² 784) and
  a 14-world-unit top. Grounded players are blocked; jumping with feet at or
  above 112 z-units clears them.
- Dead scuttlers are low corpse obstacles under the same height rule.
- Adult living/dying remains full-height solid; adult corpse/game behavior is
  unchanged.
- Reset restores adult and all scuttlers to spawn positions/health/states.
  Repeated death/reset cycles are covered.

## 4. Sprites and rendering

Artwork was extracted from the supplied labelled contact sheet, not treated as
an atlas:

- source: `assets/scuttlers/scuttler_sheet.png`;
- 65 exported frames, 49 runtime frames in `scuttler_sprites_g6h.h`;
- 64x32 RGBA frames, transparent background, bottom-centre anchors;
- manifest: `sprites/scuttlers_g6h/manifest.json`;
- four scuttle directions x 8 frames, attack 6, hit 4, dying 6, dead 1;
- turn-left/right frames are extracted but not runtime-selected.

Native OpenGL uses directional selection from the camera-relative bearing and
mirrors side/three-quarter views. Animation uses the entity state/timer for
attack/hit/dying and a deterministic tick-derived loop for scuttling. The
diagnostic captures hide the weapon so the low creatures are visible; the
normal game still draws the weapon and preserves G6f.3's muzzle anchor.

Because the small frames were not surviving the world-billboard alpha/depth path
on the AMD GL driver, they are rasterised through a screen-space perspective
overlay using the same camera-relative depth/side/floor scaling. This is a
documented limitation: scuttler depth occlusion against world geometry is not
perfect, though their world collision and AI are exact.

## 5. Measurements (G6g -> G6h)

| Metric | G6g | G6h |
|---|---:|---:|
| Loader capacity | 14,800 | **25,000** |
| Loader after game | 13,438 / free 1,362 | 16,651 / free 8,349 |
| Loader after host blocks | 14,738 / free 62 | 17,986 / free 7,014 |
| Loader code cells | 12,454 | 12,454 |
| Globals | 201/256 | 251/512 |
| Managed heap high | 33,472 | 34,080 |
| Glon source bytes | 39,198 | 45,855 |
| Scuttler header | — | 1,213,107 bytes |
| Sprite texture memory | — | 49 x 64x32x4 = 401,408 bytes |
| Windows exe | 6,616,576 | 6,990,336 |
| Windows distribution | 22,151,038 | 22,531,893 |

`--bench-fixed 120` on the available Linux software/GL path: G6g total mean
134.82 ms/frame; G6h 178.37 ms/frame. This is a measurement of the test path, not
a claim of optimization. No performance improvement is claimed.

## 6. Verification

Linux full `make test`: **PASSED**, including all G6e-G6g regressions,
`--multi-alien-test`, `--jump-test`, `--alien-collision-test`,
`--collision-probe-test` and frozen S1
`f90496c26dc45c7a387d8fc8639cd2781507d3d2`.

Native Windows tests: multi-alien, self, combat, door-sync, corpse-stability,
muzzle-anchor, audio-e2e, jump, alien-collision and collision-probe all
**PASSED**. Packaged `run-g6h.bat --self-test` and `--multi-alien-test` also
passed.

`--multi-alien-test` verifies four spawns, independent health/positions,
same-shot target isolation, LOS/closed-door protection, independent pursuit,
wall/door blocking, jump-over clearance, stable corpse, reset, three
death/reset cycles, sprite alpha/frame boundaries and four direction choices.

Evidence: `evidence/g6h/` including `traces/multi-alien-test.log`,
`measurements/`, `logs/` and Windows GL screenshots.

## 7. Windows build and playtest

- Executable: `dist/windows/doom.exe`
- SHA-256:
  `e6f68555aa3f1aa0e651d9087b294a5aceefef10e10233d9304cac53afac1de1`
- Size: 6,990,336 bytes
- Distribution: 22,531,893 bytes
- Launcher: `dist/windows/run-g6h.bat`

Instructions:

```
D:
cd D:\repos\glon-worktrees\glon-avp-g6h\dist\windows
run-g6h.bat
```

Controls: W/A/S/D move, arrows turn, E door, F/left mouse fire, Space jump,
M mute, [ / ] volume, Esc quit.

Physical checks: see three scuttlers progressively, confirm head-on/side
appearance, two visible at once, attack/hit/die independently, jump over living
or dead scuttlers, walls/closed doors remain solid, adult intact, restart
restores all four.

## 8. Known limitations

- Physical Windows acceptance pending; G6h has no tag.
- G6f.2 remains the accepted baseline; G6f.3, G6f.4 and G6g remain separately
  pending.
- Scuttler rendering is a screen-space perspective overlay; world-depth
  occlusion against props/walls is approximate.
- Only four scuttler directions are runtime frames; turn sheets are not used.
- Scuttler attack is visual/timing only; no player-health damage model is added.
- No acid pools, facehuggers, boss mechanics or additional enemy types.
- Loader expansion reuses the unused M1 task arena and is game-build-only;
  default/M1 behavior is unchanged.

## 9. Deferred optimization

See `G6H-OPTIMIZATION-INVESTIGATION.md`. The fixed evaluator remains 12,454
code cells; growth is loader data. Game/entity data added ~3,213 loader cells and
host/test blocks ~483. The 25,000-cell arena could plausibly be reduced toward
~20,000 after gating lazy test blocks, packing entity fields and merging accessor
logic, but not to 14,800 without redesign. No optimization was applied before
packaging.
