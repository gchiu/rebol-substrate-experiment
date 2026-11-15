# G6h — scuttler invasion: blocked by loader capacity

Branch/worktree: `glon-avp-g6h`,
`/mnt/d/repos/glon-worktrees/glon-avp-g6h`  
Baseline: G6g `57988d2` (G6g physical acceptance still pending)  
Status: **SUPERSEDED** by the project-owner capacity authorization and the
completed G6h implementation/report; retained as historical evidence of the
earlier measured blocker.

## 1. Decision

The full G6h implementation was stopped before changing gameplay because the
existing loader arena has no credible room for four independent enemies, their
shared AI/target-selection code, state status, and test support. The repository
rules prohibit raising the loader/runtime capacity or changing the memory map as
an incidental measure. The requested fallback is therefore to stop and report
the measured limitation rather than introduce an unverified workaround.

## 2. Measured baseline (G6g, unchanged)

`./build/doom --loader-report` on the unchanged G6g source:

- loader arena base `40000`, limit `54800`, capacity **14,800 cells**;
- after game load: used **13,438**, free **1,362**;
- after host blocks (normal G6g path): used **14,738**, free **62** cells;
- code cells **12,454**; globals **201/256**;
- managed heap high **33,472** cells;
- full `--self-test`: **PASSED**; frozen S1 passes.

For measurement only, forcing lazy tick-block materialisation raised the
post-host-block headroom to **510 cells**. That is still only a small fraction
of the code needed for three independent stateful scuttlers, shared accessors,
AI, collision, target selection and status. The normal game path is tighter
(free **62**). No capacity increase was attempted.

Evidence:
`evidence/g6h/measurements/loader-baseline.log`, `logs/baseline-self-test.log`,
`logs/frozen-s1.log`.

## 3. Artwork work completed

The supplied `scuttler_sheet.png` is a labelled presentation contact sheet, not
a transparent atlas. It was inspected and an extraction prototype was written:

- source copied to `assets/scuttlers/scuttler_sheet.png`;
- `tools/extract_scuttler_g6h.py` isolates grid cells, removes grid lines,
  derives an alpha matte from the near-black background, tightly crops the
  creature, and places it in a **64x32** bottom-anchored frame;
- extracted **65 frames** (all four 8-frame scuttle directions, turn rows,
  attack 6, hit 4, dying 6, dead 1);
- generated **49 runtime frames** in `scuttler_sprites_g6h.h` for four scuttle
  directions plus attack/hit/dying/dead;
- wrote `sprites/scuttlers_g6h/manifest.json` with source coordinates, source
  alpha bounding boxes, output placement and anchor.

The exported PNGs are actual **RGBA**, with alpha 0 transparent backgrounds and
partial alpha at resampled edges. They are not used by the game because the
multi-entity implementation is blocked. Some dark holes inside the artwork are
left transparent rather than filled; this is documented and visible in the
extraction contact sheet. The sprite extraction is evidence, not an integration
claim.

Evidence: `evidence/g6h/art/`, `sprites/scuttlers_g6h/`,
`scuttler_sprites_g6h.h`, `assets/scuttlers/scuttler_sheet.png`.

## 4. Proposed architecture (not implemented)

The intended design was:

- one existing adult plus three independently addressed scuttlers;
- per-scuttler position, health, AI state and timer;
- shared Glon AI/collision/target-selection helpers with a bounded index;
- scuttler low top around 14 world units, faster movement, two 25-damage hits,
  deterministic pause/scuttle/lunge cycles;
- target selection by aim, range, near depth and LOS;
- native OpenGL directional sprites from the extracted frames, with camera
  follow and G6f.3 muzzle anchor preserved.

None of this was committed as game code. The proposed architecture is recorded
here only to make the blocker auditable; it is not a claim of implemented
functionality.

## 5. What was preserved

- G6f.2 accepted baseline and tag `avp-g6f2-accepted`.
- G6f.3, G6f.4 and G6g worktrees and their unaccepted statuses.
- Frozen S1 and all existing tests.
- No merge, push, tag, release or deployment.
- No overwritten adult/scuttler builds.

## 6. Next requirement

A full G6h needs either:

1. an explicitly approved capacity/memory-map scaling experiment with measured
   headroom for four entities; or
2. a separately approved redesign that reduces the existing Glon code/loader
   footprint enough to preserve all G6e–G6g behaviour.

Without one of those, implementing three stateful scuttlers would require a
workaround outside the verified constraints.
