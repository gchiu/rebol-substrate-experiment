# G6d — A-3 Side-Corridor Entrance Obstruction Fix

Status: fixed, tested on Linux and native Windows.

- Branch/worktree: `glon-avp-g6d-win`
- Fix commit: `7121aab`
- Baseline for the Windows port: `c935fe6`
- Frozen S1: unchanged at
  `f90496c26dc45c7a387d8fc8639cd2781507d3d2`

## 1. Symptom and root cause

The A-3 side corridor is the north-turn opening at:

```
x = 1152 .. 1280
y = 384
```

A large rusty `GT_ART_PANEL` ventilation panel appeared to cover this opening
from the A-3 corridor. The side corridor remained visible through gaps on both
sides of the panel, and the player could walk through the panel.

This was **not** a wall, a door, a depth-sorting error, or a collision defect.
It was decorative south-wall corridor geometry that was drawn past the end of
the actual south wall and across the open side entrance.

`gl_corridor_static()` contains two panel loops over
`x = 780, 860, ..., 1180`:

1. Recessed service housing and `GT_SERVICE` quads at `y ≈ 373.9–383`.
2. `GT_ART_PANEL` ventilation-panel quads at `y = 377.9`.

Both loops originally used the full panel width (`x+3 .. x+61`) regardless of
the wall extent. The south wall ends at `x = 1152`; the opening is
`x = 1152..1280`. Therefore:

- the panel at `x=1100` extended to `x=1161` and crossed the left edge;
- the panel at `x=1180` extended to `x=1241` and sat almost entirely in the
  opening: this is the large rusty vent panel in the screenshot;
- the backing boxes, trims, ribs, service quad, warm floor strip, south pipe
  and accent boxes had the same overrun.

The panel plane was at `y=377.9`, roughly six units inside A-3 from the
opening plane at `y=384`. From A-3 the quads floated in front of the open side
corridor. From the side corridor the same quads were visible through the
opening. Because they were ordinary decorative draw calls, they had no
collision; the Glon wall opening was already traversable.

## 2. Git history comparison

- G5o greybox `98e96ad`: no `377.9f` south-wall panel loop. The side opening
  was unobstructed.
- G5p prototype corridor art `a3886fc`: introduced the south-wall
  `GT_ART_PANEL` loop at `y=377.9`, including the unconditioned
  `x=780..1260` extent.
- G5v/G5t/G5y later changed materials, lighting and room detail but did not
  change this panel extent.

The obstruction is therefore a long-standing G5p decorative-panel placement
bug that became visually prominent with the dark/rusty corridor material. It
is not new in G6d; G6d inherited the same host geometry.

## 3. Smallest correction

The fix changes only the south-wall decoration loops:

- Added `SIDE_ENTRANCE_X0` (`1152`) and `SIDE_ENTRANCE_X1` (`1280`).
- Added `corridor_south_panel_span()` which clips a south panel's right edge
  to `x=1152`.
- The `GT_SERVICE` backing/trim/service quad, lower warm strip, protective
  bars, south rib, south pipe and accent boxes stop at the opening.
- The `GT_ART_PANEL` south ventilation quads use the clipped span and clipped
  U coordinate; panels whose span is entirely inside the opening are skipped.
- North-wall panels, the far bulkhead, A-3 lighting, all other room/corridor
  geometry, Glon programs, collision, audio and the Windows platform adapter
  are unchanged.

No collision handling was modified. The player could already traverse the
opening; the defect was visual-only.

## 4. Regression test

New `--entrance-test`:

- iterates the corridor south-panel loop positions;
- asserts every clipped south panel span is at or left of `x=1152` and cannot
  cross the side-opening interval;
- runs `doom/check-all` at `(1200,384)` (the entrance) and `(1200,500)`
  (inside the side corridor) and requires both to be open.

It is part of `make test`.

New `--gl-entrance-shots DIR W H` captures three fixed poses for before/after
visual inspection:

- `01-side-corridor` — side corridor looking toward A-3;
- `02-side-corridor-close` — closer to the opening;
- `03-a3-toward-side` — A-3 looking into the side corridor (the blocked view).

## 5. Results

Before the fix:

```
FAIL south wall panel clear of side opening
FAIL south wall panel clear of side opening
PASS side entrance traversable
PASS side corridor traversable
entrance-test: FAILED
```

After the fix:

```
PASS south wall panel clear of side opening   (x=780..1080)
PASS south wall panel clear of side opening   (x=1100 clipped)
PASS south wall panel clear of side opening   (x=1180 skipped)
PASS side entrance traversable
PASS side corridor traversable
entrance-test: PASSED
```

Linux:

- full `make test` passes, including the new entrance test;
- frozen S1 check passes;
- A-3 side entrance is visually open in the after captures.

Native Windows (rebuilt distribution):

- `doom.exe --self-test`: PASSED;
- `doom.exe --entrance-test`: PASSED;
- `doom.exe --audio-device-test`: WASAPI, 44100 Hz/2ch/256 samples;
- `doom.exe --sfx-device-test`: 6 effects through WASAPI;
- `doom.exe --gl-entrance-shots`: native OpenGL/AMD capture, opening clear.

Windows executable after the fix: **6,550,528 bytes**, SHA-256
`79a3ded7f9e9a3ca3f1abc19ccd1a47653c56bd9c66cb1eacb078f2e564a3799`.
Distribution: **22,079,161 bytes**.

## 6. Evidence

```
evidence/g6d-entrance/
  before/01-side-corridor.png
  before/03-a3-toward-side.png
  after/01-side-corridor.png
  after/03-a3-toward-side.png
  before-entrance-test.log
  after-entrance-test.log
  before-shots.log
  after-shots.log
  full-test.log / linux-test.log
  git-history.txt
  windows/self-test.log
  windows/entrance-test.log
  windows/audio-device-test.log
  windows/sfx-device-test.log
  windows/entrance-shots.log
  windows/shots/ (native Windows after-fix poses)
```

No push, merge, tag or deploy.
