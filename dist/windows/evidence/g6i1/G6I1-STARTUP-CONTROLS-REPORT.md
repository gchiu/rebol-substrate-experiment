# G6i.1 — startup correction, live memory, controls and vertical aiming

Worktree: `/mnt/d/repos/glon-worktrees/glon-avp-g6i1`  
Branch: `glon-avp-g6i1`, based on G6i `a780639`  
Status: **implemented, packaged and verified; unaccepted pending physical Windows testing**  
Windows executable: `dist/windows/doom.exe`, **7,026,688 bytes**  
SHA-256: `7a5be766f9687b9a2ecef0798f75a2c799a2eca647ed75f13f7f5cb284b71d73`  
Launcher: `dist/windows/run-g6i1.bat`

G6h.2, G6i and the G6i.0 audit worktree/distributions were preserved. No merge,
push, tag, release or deploy was performed. Frozen S1 is unchanged
(`f90496c26dc45c7a387d8fc8639cd2781507d3d2`).

## 1. Startup layout

The old fixed-position title and inline credits were replaced with a small
font-metric layout plan. It uses the 5×7 G5Z font, **7-pixel text height plus a
2-pixel leading gap**, explicit margins, and a uniform scale
`u = min(drawable_width/320, drawable_height/200)`. Lines are added through a
single cursor; no title/credits line positions are independently hard-coded.

The main title now contains:

- GLON VS ALIEN
- INFESTATION PROTOCOL
- GLON SUBSTRATE INITIALIZED
- VM capacity
- live loader reserved / occupied
- live loader available
- live globals
- live managed-heap high-water
- live Windows current/peak working set and private committed (or N/A)
- live graphics resolution and audio status
- control help
- PRESS ENTER TO START
- C CREDITS / ESC QUIT

Credits are a **separate screen** reached with `C`; `Esc` returns to the title,
and `Esc` from the title quits. The credits screen fits independently and never
collides with the start prompt. The Windows title is now **GLON vs ALIEN**.

The layout plan is validated by tests at **640×400, 800×600, 960×600 and
1280×720**: every line is on-screen, the text width is bounded, line boxes do
not overlap, and the final line is above the bottom margin. Native Windows
framebuffer captures exist for all four sizes (title and credits). Visual review
found no clipping or collision.

## 2. Live memory reporting

The title reads live runtime counters; neither G6i.0 nor G6h.2 numbers are
hard-coded or presented as live. At the measured G6i.1 title state:

- VM: **65,536 cells / 524,288 bytes** (one contiguous static VM array; x64
  cell = 8 bytes; address-space capacity, not physical RAM)
- loader reserved: **25,000 cells / 200,000 bytes**
- loader occupied: **17,910 cells**, available **7,090 cells**
- globals: **277 / 512**
- managed heap observed high-water: **34,240**
- Windows live title-state working set: about **95.9–96.3 MB**
- Windows live peak working set at that point: about **97.9–98.4 MB**
- Windows live private committed: about **113.0–118.2 MB**

These are live `GetProcessMemoryInfo` values printed on the title and recorded
in the title-test logs. The historical G6i.0 packaged gameplay figures
(~172–175 MB working set, ~153 MB private committed) are **not** displayed as
G6i.1 live RAM and are only retained as audit reference. No extra RAM was
allocated to inflate the display; no VM capacity or arena layout changed.

### Loader difference explained

G6i.0 reported **18,498** loader cells for G6h.2, while G6i reported **17,830**.
The 668-cell difference is a **measurement-stage/configuration difference**, not
a capacity difference:

- `--loader-report --objects` runs with `gl_active = 0`; `build_blocks()` then
  creates the two CPU render host blocks (`!gl_active && !combat_minimal`),
  consuming about **668 cells**.
- The G6i/G6i.1 title runs after the OpenGL context is active, so those CPU
  render blocks are correctly absent; 17,830 / 17,910 are the **GL runtime**
  loader occupancy.
- G6i.1 adds three top-level Glon counters (`aim-slope`, `aimz`, `vd`), which
  account for the live title change 17,830 → **17,910** and globals 274 → **277**.

This was reproduced with current code: `--loader-report --objects` reports
18,578 after host blocks, while the GL title reports 17,910; the delta is the
same 668 cells. The title always displays the current executable's live value.

## 3. Controls

| Key | Action |
|---|---|
| W/S/A/D | forward/back/strafe |
| Left/Right arrow | turn |
| Up arrow | jump (edge-triggered, repeat suppressed) |
| Down arrow | held look-down, clamped at 75°; smooth return on release |
| Space | fire (edge-triggered; no jump) |
| Left mouse | existing alternate fire |
| Enter | start from title |
| C | credits from title |
| Escape | credits→title; title/game→quit |
| E | existing door control |

Legacy F fire was removed from the player-facing/bindable path; the smoke test
now uses Space. Space fires through the existing `FIRE_MASK`, with normal ammo,
muzzle flash, sound and cooldown. Up no longer fires and Space no longer jumps.
W + Up carries the forward movement mask and the jump edge into the same fixed
tick. Held Up repeats do not re-trigger a jump. Existing air control is
untouched.

The camera pitch model is deterministic: +3° per fixed tick while Down is held,
−5° per fixed tick after release, clamped to 0..75°, reset by restart. Camera
position/horizontal orientation still come from the existing player simulation;
the weapon and reticle remain screen-consistent. No upward-look control was
added.

## 4. Height-aware shooting

`doom/consider` now accepts a target vertical centre and half-height and tests
the shot's actual 3D direction. The eye is 64 world units; the host computes a
bounded aim slope `tan(pitch)*64` and passes it to the tick. The vertical shot
height at target depth is:

`aim_z = eye - aim_slope * depth / 64`

Target volumes:

- adult Xenomorph: centre 64, half-height 64 (remains shootable on a horizontal
  shot)
- scuttler: centre 15, half-height 16

Existing lateral aim tolerances (32 world units for scuttlers), range, wall and
closed-door LOS checks are preserved. The shot must pass range, lateral,
vertical and occlusion tests. This is not auto-targeting: the ray still uses the
camera's actual downward pitch, and walls/closed doors still block it.

Verified: horizontal shot at a scuttler 30 world units away is a miss; a 60°
downward shot hits it; a visible scuttler in front is hit by an aimed-down shot;
a scuttler behind a closed door remains safe; the adult remains shootable.

## 5. Tests run

- Full Linux `make test`: **PASSED**, including frozen-S1 verification.
- Native Windows suite:
  - self, projection, input, look, tracker, resize, stationary, entrance,
    collision-probe, multi-alien, tracker-multi, scuttler GL probe, G6h.2 GL,
    jump, alien-collision, combat, door-sync, corpse stability, muzzle anchor,
    flat/objects, raw-wall self-test, trace, audio, sfx, audio-e2e: **PASSED**
  - title at 640×400, 800×600, 960×600, 1280×720: **PASSED**, including
    no-overlap/no-clipping plan checks, live memory labels/values, credits
    navigation, Enter, Escape, no enemy ticks, and repeated restart without
    loader growth
  - native framebuffer title/credits captures at all four sizes and
    horizontal/downward gameplay captures: present in evidence
- Targeted new assertions:
  - Space fires and does not jump; Up jumps and does not fire
  - W + Up maps to forward + jump; repeat does not re-request
  - Down changes pitch, clamps, releases to horizontal, restart resets
  - horizontal feet scuttler miss, downward feet hit, front hit, closed-door
    safe, adult shootable
  - restart restores enemy state and pitch
  - simultaneous movement + jump + fire keeps ammo, airborne jump and displacement

Evidence: `experiments/glon-doom/evidence/g6i1/` (also copied under
`dist/windows/evidence/g6i1/`). Logs include exact live process values.

## 6. Performance observations

Measured with the existing methodology, not as a comparative claim:

- Linux `--bench 60`: `sim=8.51 ms`, `frame=145.82 ms` (`fps=6.9`), current
  environment.
- Native Windows `--bench 60`: `sim=9.61 ms`, `frame=166.54 ms` (`fps=6.0`),
  current environment.

No performance improvement is claimed; these are different environments and
measurement is not equivalent to prior builds. No rendering optimization was
attempted.

## 7. Limitations / outstanding work

- G6i.1 remains **unaccepted** until physical Windows playtesting against the
  ten acceptance criteria.
- Distinctive scuttler movement/proximity sounds remain **outstanding**; no new
  sound system or audio content was added.
- No new enemies, acid pools or attack damage.
- The title process RAM is **live at title state**, not gameplay; it should not
  be read as the historical packaged gameplay audit values.
- Down-look has no upward counterpart in this milestone.
- The CPU renderer retains its legacy 2D view; the downward camera and aimed
  shots are implemented in the OpenGL/gameplay path.

## 8. Artifact and evidence

- Windows distribution: `dist/windows` (`run-g6i1.bat`)
- Executable SHA-256:
  `7a5be766f9687b9a2ecef0798f75a2c799a2eca647ed75f13f7f5cb284b71d73`
- SDL2.dll SHA-256:
  `b67bca20a0a35c0e0689495f9f3095526aa6575543c2678646c1593107410800`
- Runtime `doom.glon` SHA-256:
  `4f14c9b6b8ccb7f0472e9fe49a2a35a98b84027c6da53f7b850f4d5a64e6d1ca`
- Source commit: `e7ebf8589cfc82ee42c22aa67f80a9b870d91ac4`
- Package/evidence and final documentation commits: recorded in the final
  repository log for branch `glon-avp-g6i1`.
- Evidence screenshots (PNG) and logs: `evidence/g6i1/` in both the worktree and
  the Windows distribution.
