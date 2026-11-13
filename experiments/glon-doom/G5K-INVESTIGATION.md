# Glon AvP G5k — industrial clutter, cover and floor optimisation

Branch: `glon-avp-g5k`
Base: `c461c5a105018ff6501a205c9c70afefe8ab70be` (G5j)
Configuration: native 64K, WSL2, AMD Ryzen 5 7535U, GCC 11.4.0,
`-std=c17 -O2`, SDL2. No frozen-S1, primitive, memory-map, player, door or
Alien-AI rewrite.

## 1. Implementation summary

- `--flat-surfaces`: skips all Glon texrow calls and draws a cheap native
  vertical depth gradient plus horizon line. Walls, doors, Alien, tracker,
  camera and movement are unchanged.
- Six compact static objects are defined in `objects.glon`, loaded only for
  `--objects`: two drums, two crates, one cabinet, one low container.
  Each record is `[type x y half-width half-depth height]`.
- Shared Glon routines project object records and add their collision footprints
  to the existing `check-all`. The Marine and Alien therefore collide with them.
- Shared native sprite routines rasterise objects and the existing Alien.
  Sprites are depth-sorted far-to-near and alpha-composited, giving partial
  concealment behind crates and drums.
- `--alien-cover`, `--demo-partial`, `--demo-emerge` and `--demo-side` position
  the Alien behind a crate for deterministic cover/parallax views.
- G5h prepared-masm wall execution and G5i Alien animation remain intact.

## 2. Floor/ceiling savings

Same raw-wall path, 120-frame benchmark, three runs:

| Mode | FPS | Wall | Floor/ceiling | Sim | Raster | Total |
|---|---:|---:|---:|---:|---:|---:|
| A — G5j textured, no objects | **20.6** | 17.0 ms | **21.4 ms** | 2.0 ms | 0.51 ms | 48.6 ms |
| B — flat, no objects | **33.2** | 17.2 ms | **2.35 ms** | 2.1 ms | 0.12 ms | 30.1 ms |
| C — flat + clutter | **27.5** | 18.0 ms | 5.3 ms | 5.1 ms | 0.12 ms | 36.4 ms |
| D — flat + clutter + cover | **26.7** | 18.5 ms | 5.8 ms | 5.2 ms | 0.13 ms | 37.4 ms |

Flat mode saves about **19 ms** of floor/ceiling work and raises textured FPS
from **20.6 to 33.2**. Static objects then spend about **6.3 ms** total
(projection/status, collision and object raster), leaving C at **27.5 FPS** and
D at **26.7 FPS**. The flat floor is visually emptier than the textured floor;
with clutter it reads acceptably as a dark industrial deck. This is reported as
a trade-off, not hidden.

Opcode profile with flat+objects: Glon texrow stage is **0 S1 ops / 0 HOST
calls** (it is not executed). Wall remains **4,555,404 S1 / 846,833 HOST** per
frame. Object collision increases tick cost from ~508k to ~1.31M S1 ops/frame.

## 3. Static object representation

`objects.glon` contains exactly six rows; no duplicated executable blocks:

```
[type x y half-width half-depth height]
[0 900 272 20 12 92]   drum
[0 940 368 20 12 92]   drum
[1 860 268 38 14 76]   crate
[1 1040 370 40 14 76]  crate
[2 980 270 22 10 104]  cabinet
[3 1100 368 50 14 58]  low container
```

Shared routines:

- `doom/object-project` — depth/side/screen rectangle and visibility in Glon;
- `doom/object-reports` — emits six compact records to the host;
- `doom/check-object` / `doom/check-objects` — circular Marine/Alien collision
  against each object's AABB, called from the existing collision routine;
- native `draw_sprite_rect` — samples four original 64×96 transparent prop
  sprites.

The object module is loaded only with `--objects`; the base game retains only
`doom/objects-on: 0`, so A/B and existing tests do not pay object loader cost.

## 4. Occlusion method and limitations

- Objects and the Alien are alpha-sorted by Glon-derived depth (far to near),
  then composited. A crate in front of the Alien covers its opaque pixels while
  leaving transparent edges visible; this gives full and partial concealment.
- Wall/door visibility for the Alien still comes from existing Glon LOS.
- Object records use screen/depth visibility in Glon. Per-object wall LOS was
  removed from the per-frame path because six LOS traversals cost roughly the
  floor saving. The host conservatively hides all objects when the door is closed
  and the Marine is still in the room.
- **Limitation:** object-vs-wall and object-vs-door occlusion is screen-view /
  door-state based, not a per-pixel wall-depth test. Static placement avoids
  most cases, but this is not a general 3D depth buffer. Multiple
  object-vs-object and object-vs-Alien overlap is handled by depth sorting.

## 5. Alien cover behaviour

The Alien remains collision-bound by the shared object footprints. Existing
two-axis movement attempts allow it to slide around a crate; no pathfinding or
new AI was added. Cover/visibility, physical collision and LOS remain separate.
The G5i sprite animation, door behaviour and pursuit state are unchanged.

Visual verification:

- hidden: `/tmp/g5k-hidden.png`
- emerging: `/tmp/g5k-emerge.png`
- sideways parallax: `/tmp/g5k-side.png`

The hidden view shows the crate/drum and no Alien; the emerging view shows the
Alien around the cover edge. These are actual WSLg captures.

## 6. Performance, instructions and memory

- Object sprites: 4 × 64×96 = **98,304 bytes** compiled; PNG 9,068 bytes.
- Visible object/Alien sprite cost: **0.21–0.28 ms/frame** in close tests.
- Managed frontier: **33,632** (objects) vs 33,472 (base); GC **0**.
- Object mode loader: **14,210 used / 590 free** raw; base raw default is
  14,322/478. Object module + 16 tick source blocks replace 64 tick blocks in
  objects mode; no frozen-map change.
- Code cursor: **14,828 / 16,128** (raw wall), unchanged by object Glon source
  (Glon source lives in loader, not emitted code).
- Loader does not overflow; no undocumented workaround. Default non-object mode
  remains on 64 tick blocks.

## 7. Screenshots

- Mode contact: `/tmp/g5k-contact-sheet.png`
- A: `/tmp/g5i-shots/corridor.png`
- B: `/tmp/g5k-flat2/corridor.png`
- C: `/tmp/g5k-cfinal/corridor.png`
- D hidden: `/tmp/g5k-hidden.png`
- D emerging: `/tmp/g5k-emerge.png`
- D sideways parallax: `/tmp/g5k-side.png`
- Flat/object contact: `/tmp/g5k-obj-contact3.png`

A/B/C are actual framebuffer captures; D images are actual WSLg windows. No
mock-ups were used.

## 8. Tests and failures

`make test` passes:

- all existing G5a–G5j texture, Alien, projection, stationary-cache, trace,
  raw-wall and frozen-S1 tests;
- `--flat-surfaces --objects-test`: six deterministic object records, player
  object collision, clear-space non-collision, and Alien object collision;
- flat opcode profile confirms texrow execution is zero;
- no existing assertion was weakened.

No failures. The per-pixel object/wall depth limitation above is explicit; it is
not claimed as a general occlusion system.

## 9. Recommendation

Adopt **C** (flat surfaces + static clutter) as the best demonstrated
configuration: it recovers roughly 19 ms of floor work, spends roughly 6 ms on
objects/collision, remains at **27.5 FPS**, and makes the corridor read as an
industrial place rather than an empty grid. Keep D's cover/parallax support and
Alien animation. If the flat deck is considered too austere, add a cheap
perspective floor-line overlay in G5l; do not restore the full Glon texrow cost
without a measured need.

A per-pixel wall/object depth solution is the next architectural step if object
placement behind partial walls becomes important. Do not enlarge the frozen
memory map for it.

## 10. Changed files

- `doom-host.c` — flat surfaces, object flags/status/drawing, cover/demo modes,
  tests/metrics.
- `doom.glon` — minimal objects-on flag and collision hook.
- `objects.glon` — compact six-object records, shared projection/collision.
- `object_sprites.h`, `sprites/objects/*.png`, `tools/gen_g5k_objects.py`.
- `Makefile`, `README.md`, `G5K-INVESTIGATION.md`.
- `GLON-ARCHITECTURE-MEMORY.md`, `MEMORY.md`.

Final commit hash is reported in the delivery message.
