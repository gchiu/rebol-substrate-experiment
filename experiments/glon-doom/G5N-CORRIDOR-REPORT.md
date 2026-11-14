# G5n — Realistic industrial corridor

Branch: `glon-avp-g5n`  
Base: `55985c4b819b7eb89f9abf751913635c0f4bfe72` (G5m)  
Reference: `/mnt/c/users/anon_/downloads/corridor.png`, used as art direction
only; no crops are used as textures.

## 1. What was implemented

The OpenGL corridor now has actual geometry rather than a flat texture-only
treatment:

- ten solid bulkhead wall planes;
- repeating side-wall recessed panels with trim and louver bars;
- projecting structural ribs on walls, ceiling and floor;
- lower-wall service housings with warm floor-level strips;
- longitudinal pipes and secondary conduits;
- recessed ceiling fixtures with emissive lenses and existing light pulse;
- floor plates, grate bars and drainage channels;
- substantial framed bulkhead door at the entrance and a visual destination
  bulkhead at the far end;
- 3D drums, crates, cabinet and container using existing Glon object records;
- existing Alien sprite, motion tracker, HUD and controls preserved.

All architecture remains renderer-side placeholder geometry; Glon continues to
own player/collision/door/Alien/object state. Frozen S1 and the 64K map are
unchanged.

## 2. Artwork status

**The visual-quality gate remains open.** The corridor is a coherent greybox
with correct depth, scale and reusable structure, but the flat solid materials
are temporary placeholders. There is no final painted/weathered bulkhead, floor
grate, pipe or door artwork. No reference crop or new low-quality procedural
texture set is claimed as final.

The Alien is also still the previous functional sprite set and remains pending
separate artwork replacement. No AI/gameplay changes were made.

Required final assets are listed in `G5N-ASSETS.md`.

## 3. Screenshots

Actual GL framebuffer captures (640×400), all eight required views:

- `/tmp/g5n-tour640b/01-entrance.png`
- `/tmp/g5n-tour640b/02-mid-corridor.png`
- `/tmp/g5n-tour640b/03-close-wall.png`
- `/tmp/g5n-tour640b/04-look-back.png`
- `/tmp/g5n-tour640b/05-door-closed.png`
- `/tmp/g5n-tour640b/06-door-open.png`
- `/tmp/g5n-tour640b/07-crates-tracker.png`
- `/tmp/g5n-tour640b/08-far-bulkhead.png`
- contact: `/tmp/g5n-tour-contact2.png`

Larger-window captures:

- `/tmp/g5n-tour960b/01-entrance.png` … `08-far-bulkhead.png`
- Actual maximized WSLg window: `/tmp/g5n-max.png`

The maximized screenshot shows the full scene filling the drawable with the
capped circular tracker correctly anchored.

## 4. Performance

Measured on the verified WSLg D3D12/AMD Radeon path, 120 frames, objects/Alien:

- 640×400: **31–46 FPS**, simulation ~19–20 ms, submit/finish ~2.6–6.5 ms.
- 960×600: **36.2 FPS**, simulation ~16.8 ms, submit/finish ~5.0 ms.

The immediate-mode geometry adds draw cost; performance remains interactive but
is below the G5m baseline. No performance claim beyond these measurements is
made. The first GL benchmark process reported a null renderer name and is not
used as evidence.

Loader: **14,082 used / 718 free**. Managed frontier: **33,600**, GC **0**.
Code cursor: **13,126 / 16,128**. Executable: **1,035,848 bytes**.

## 5. Verification

Existing `make test` covers frozen S1, input, doors, collision/objects, tracker,
resize invariants, projection, textures/Alien assets, raw-wall and stationary
cache. `--gl-tour` and `--gl-bench` exercise the new geometry on the GPU.
No regressions were observed. Manual WSLg navigation remains the acceptance
check for movement/door/tracker feel.

## 6. Remaining visual deficiencies

- Flat placeholder colours; no weathered painted steel, grime, scratches,
  drainage stains or believable edge wear.
- Pipes/conduits are simplified boxes/cylinders; no final joint/cable artwork.
- Crates/cabinet/drums are placeholder 3D masses pending final material art.
- Ceiling lights are simple emissive panels; no volumetric/area-light detail.
- Alien remains the prior stylized creature; separate artwork still required.
- Far bulkhead is a visual destination; the functioning gameplay door remains
  the G5m door.

## 7. Recommendation

Proceed only with final material artwork matching `G5N-ASSETS.md` before
declaring the corridor visually complete. The geometry and integration are
ready for that asset pass. Keep the greybox build available for collision,
navigation and performance testing.
