# G6h.2 — world-space scuttlers, scale, aim and multi-contact tracker

Worktree: `/mnt/d/repos/glon-worktrees/glon-avp-g6h2`  
Branch: `glon-avp-g6h2`  
Base: G6h.1 `329e389`  
Implementation: `3ca2f4680a8187a5a6ae73a4ac733f4d806e6d20`

Status: **IMPLEMENTED AND AUTOMATED-VERIFIED; PHYSICAL WINDOWS ACCEPTANCE
PENDING.** G6h.2 is unaccepted and has no tag.

## 1. World-space rendering

The screen-space overlay was removed. Scuttlers are now explicit camera-facing
world-space billboards in the existing scene pass:

- floor-anchored at the actual `z=0` floor, 60 world wide x **30 world tall**;
- camera-facing right axis from the player's yaw;
- `GL_DEPTH_TEST` enabled, depth writes on, alpha test and blending for the
  extracted RGBA frames;
- drawn after world props and before the adult and weapon, so walls/doors/props
  occlude them correctly and the rifle remains foreground;
- LOS mask still prevents unreached enemies behind closed doors from leaking
  through geometry;
- directional/animated frames and flips preserved.

The generic `gl_billboard` helper was not usable for the sparse-alpha scuttler
texture: world-space draws through it produced zero differing framebuffer pixels
while an explicit quad with the same texture sampled correctly. The explicit
quad is the verified correction; screen-space pasting and draw-order reversal are
gone.

## 2. Scale and aim

- Visual size is **60 x 30 world units**, within the requested 28-32 height.
- Collision remains intentionally low and separate: top 14, radius² 64, so jump
  clearance and existing collision behavior are retained.
- `consider` now takes an explicit bounded aim tolerance: adult remains 16 world
  units lateral; scuttlers allow **32** world units. A shot 20 world units to the
  side of a visible scuttler now registers; LOS is still required, so no
  through-wall/door targeting.

## 3. Proximity monitor

The HUD no longer reads only the legacy single-alien `detected/tside/tdepth`
fields. `tracker_contacts` derives a local contact list from the authoritative
Glon `Status` each frame:

- adult plus all three living scuttlers within the existing **700-unit** range;
- relative side/depth from current player position and facing;
- adult drawn as the existing larger green/red square; scuttlers as smaller
  cyan/amber squares;
- state 4/5 and zero-health contacts removed;
- no LOS filtering (matching the existing tracker, and explicitly required);
- no host-owned enemy registry and no duplicate simulation state;
- reset restores all contacts from the Glon reset state.

## 4. Verification

Real OpenGL framebuffer test `--gl-g6h2-test`, comparing pixels with scuttlers
shown vs hidden:

| Scenario | 960x600 | Result |
|---|---:|---|
| Unobstructed on floor | 3,790 px | PASS |
| Partly behind wall | 886 px, less than unobstructed | PASS |
| Behind closed door | **0 px** | PASS |
| Near weapon, world-space | 3,380 px | PASS |
| Approaching along corridor | 12,934 px | PASS |
| Shot directed at visible scuttler | health 50 -> 25 | PASS |
| Jump over living scuttler | x > 1120, z lands 0 | PASS |
| HUD all four contacts | 4 contacts, four contact pixels | PASS |

The same test passed natively on Windows at 960x600 and 1280x720. Actual HUD
screenshots are `evidence/g6h2/live-960/08-hud-four.png` and the 1280 variant;
scenario captures include unobstructed, partly wall, closed door, near weapon,
approaching, shot, jump-before and jump-after.

Deterministic tracker test `--tracker-multi-test` covers four simultaneous
contacts, movement, player rotation, death contact removal and restart.

Existing Linux regressions, all G6e-G6g tests, multi-alien, visibility, tracker
and frozen S1 pass. Native Windows multi, tracker, self, combat, door, corpse,
muzzle, audio, jump, alien collision and collision-probe pass. Packaged
`run-g6h2 --self-test`, `--multi-alien-test`, `--tracker-multi-test` and
`--gl-g6h2-test` pass.

## 5. Measurements

- Loader: 25,000; after game **16,779 used / 8,221 free**; after host
  **18,114 used / 6,886 free**.
- Globals: **251/512**; managed heap high **34,080**; code cells **12,454**.
- Tick-only `--bench 60`: **12.01 ms/tick** (G6g 2.74; G6h 8.43; G6h.1 8.96).
- Windows exe: **7,005,184 bytes**, SHA-256
  `6b5e78232139aea6fd10a937595e4617fcd623126729f344b40281a395b8f1c1`.
- Windows distribution: **22,548,125 bytes**.
- Launcher: `dist/windows/run-g6h2.bat`.

## 6. Preserved builds

G6h (`e6f68555...`), G6h.1 (`8b150d82...`) and all earlier worktrees/builds are
preserved. G6h.2 is a separate branch and distribution. No merge, push, tag,
release or deployment.

## 7. Known limitations

- Physical Windows acceptance pending; G6h.2 remains unaccepted.
- LOS gates rendering, but tracker contacts intentionally do not use LOS.
- Scuttler attack remains visual/timing only; no player-health damage was added.
- Visual and collision dimensions are deliberately separate: 30-high visuals,
  14-height collision.
- Tracker contacts can overlap when enemies share nearly identical bearing and
  range; the four-contact test uses distinct positions and all markers are
  present.
- No additional enemy types, acid pools or HUD redesign.
