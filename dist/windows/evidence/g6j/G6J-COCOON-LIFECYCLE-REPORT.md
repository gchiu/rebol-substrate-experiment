# G6j — cocoon lifecycle

Branch: `glon-avp-g6j`, based on G6i.3 `405f2a1`  
Status: **implemented, packaged and verified; unaccepted pending physical Windows testing**
Source commit: `32fbb8b2a9802e7221ae5419f1ae607a5423f178`
Package/evidence commit: `0b2e48361cba9cac42e78a1fded42120c903e55b`  
Windows executable: `dist/windows/doom.exe`, **8,044,544 bytes**  
SHA-256: `b4d14347aab75d42a357023bb9f66c8e6cb6c8b8eb1527b25f2342a900583f02`  
Launcher: `dist/windows/run-g6j.bat`

G6h.2 through G6i.3 were preserved. No merge, push, tag, release or deploy.
Frozen S1 remains unchanged
(`f90496c26dc45c7a387d8fc8639cd2781507d3d2`).

## 1. Source artwork and extracted frames

The supplied contact sheet was found at
`/mnt/c/Users/anon_/Downloads/cocoons.png` (1536×1024 RGB) and copied to
`experiments/glon-doom/assets/cocoons.png`.

Crops used the panel image regions, removing captions, labels and panel
borders. Foreground alpha came from a colour/brightness key plus the largest
connected component, with a small alpha blur. Frames were normalised to
128×128 with bottom-centre alignment.

**15 frames** were retained:

- 1 dormant
- 4 alert/pulse frames
- 8 hatch frames
- 1 hatched/empty
- 1 destroyed/ruined

The frames are extracted and cleaned from the contact sheet, **not newly
generated high-resolution artwork**. Some low-alpha floor debris/shadow remains
around the base of a few frames; there are no rectangular background panels or
captions. The destroyed state is one static variant. The source sheet itself is
the provenance reference.

`cocoon_sprites.h` contains the 128×128 ARGB frames used by the existing
OpenGL texture uploader. Texture bytes: 15×128×128×4 = **983,040 bytes**.

## 2. Cocoon state machines

Cocoon state is in Glon. A single `doom/cocoon-step` function implements the
shared transition logic; the three entities differ only by which existing Glon
state cells they use. This is **three independently updated records in one
fixed-tick simulation loop**, not parallel execution and not independently
scheduled Glon contexts.

States:

- 0 DORMANT
- 1 ALERTED
- 2 HATCHING
- 3 HATCHED
- 4 DESTROYED

Locations and spawn points:

| Cocoon | Position | Scuttler spawn |
|---|---|---|
| 0 | (480,500) | (480,440) |
| 1 | (620,200) | (620,260) |
| 2 | (700,500) | (700,560) |

Constants in the shared logic: activation radius **192 world units** (squared
36,864), hatch delay **480 ticks (8 s)**, hatching animation **48 ticks**
across 8 frames, cocoon health **2** rifle hits.

Initial reset places all three scuttlers in dormant state **6** with health 0 at
their cocoon spawn positions. They are not updated, not rendered, not visible
to the proximity monitor and do not pursue. On a completed hatch the associated
scuttler is set to pursuit state 1 with health 50 and its spawn position; no
other scuttler is activated.

Destroying a cocoon sets state 4, health 0, clears its timer and never spawns
its scuttler. Hatched cocoons have health 0 and remain visible as empty shells.
No cocoon can hatch twice.

The 12 state cells were implemented by reusing existing unused Glon cells, and
one cocoon-step function was added while obsolete unused top-level declarations
were removed. No VM capacity or arena boundary changed.

## 3. Rendering

Cocoons use the existing world-space billboard/depth pipeline. They sit on the
floor, use perspective scaling, alpha testing/blending and depth testing, and
are drawn before scuttlers/weapon. Textures are uploaded once during GL
initialisation; animation only changes the bound texture. No per-frame
allocation or texture upload is introduced.

The dormant/alerted/hatching/hatched/destroyed states are visually distinct;
damaged cocoons get a red tint. Native Windows captures are in
`evidence/g6j/cocoon/`.

## 4. Combat integration

The existing height-aware `doom/consider` path now also targets cocoons 0/1/2
with target ids 10/11/12, centre z 40 and half-height 40. Existing range, lateral
tolerance, camera pitch and wall/door LOS rules apply. Two hits destroy a
cocoon. A cocoon behind a wall is not damageable. A shot selects one nearest
valid target; no multi-damage change was made.

The scuttler hit path and adult hit path are unchanged. The cocoon test verifies
two-hit destruction, cancellation of hatching, no scuttler release, and wall
blocking.

## 5. Scuttler/proximity integration

The associated scuttlers use the existing G6i.3 movement, health, death and
local navigation logic after hatching. Before hatching they are state 6 and
health 0; GL rendering and proximity contacts skip them. On hatch they appear
in the existing tracker/monitor; destroyed cocoons never reveal contacts. The
adult alien remains active and independent.

The G6i.3 navigation limitations remain unchanged; cocoon placement gives each
spawn a reasonable connection to the existing room/corridor routes.

## 6. Tests

`--cocoon-test` and native Windows runs cover:

- all three dormant on reset;
- no immediate scuttler pursuit;
- approaching cocoon 0 activates only cocoon 0;
- independent timers when cocoon 0 and later cocoon 1 are approached;
- cocoon 0 countdown has no scuttler until completion;
- first and second hits reduce/destroy cocoon 0;
- destroyed cocoon never hatches or releases a scuttler;
- cocoon 1 and cocoon 2 hatch independently with exactly their own scuttler;
- proximity contact after hatch, none while dormant;
- all three destroyed without releasing any scuttler;
- wall blocks cocoon damage;
- restart restores all three dormant/full-health cocoons.

Full Linux `make test` passes, including frozen S1. Native Windows: title at
640×400/800×600/960×600/1280×720, cocoon lifecycle, native cocoon screenshots,
and the complete existing G6i.3 regression suite (input/pitch/camera/collision/
navigation/multi/tracker/GL/combat/jump/door/corpse/audio/etc.) all pass.

## 7. Measurements

Before G6j (G6i.3): loader 18,918 / 25,000; globals 277 / 512; heap high 34,240.

After G6j, measured live:

- loader reserved **25,000**, used **20,006**, free **4,994**;
- globals **270 / 512** (unused top-level cells removed);
- managed-heap high **34,272**;
- live title-state Windows: working **98,443,264 B**, peak **100,560,896 B**,
  private committed **119,541,760 B**;
- Linux `--bench 60`: `sim=14.83 ms`, `frame=222.79 ms`;
- native Windows `--bench 60`: `sim=12.15 ms`, `frame=185.15 ms`.

The cocoon state is bounded to 12 cells and 15 shared texture frames. No
unrelated memory/asset optimization was done. No physical acceptance is
claimed.

## 8. Limitations / outstanding

- G6j remains **unaccepted** pending physical Windows testing.
- The cocoon sprites are extracted from a contact sheet; some floor debris and
  soft edges remain in a few frames. The destroyed frame is a single static
  variant rather than a fully cleaned bespoke atlas.
- Cocoons are rendered but not added as solid player-collision volumes in this
  milestone; the requirement was visibility/accessibility and world-space
  rendering.
- Scuttler damage attacks and distinctive movement/proximity sounds remain
  future work.
- The G6i.3 local waypoint navigation limitation for deep concave geometry
  remains.
- CPU renderer remains the legacy 2D path; cocoon presentation is native
  OpenGL.

## 9. Evidence

`experiments/glon-doom/evidence/g6j/` and `dist/windows/evidence/g6j/`:

- `cocoon/dormant.png`, `alerted.png`, `hatching.png`, `hatched.png`,
  `destroyed.png`
- `title-640x400` … `title-1280x720`
- logs: `windows-cocoon-test.log`, `windows-cocoon-shots.log`,
  title/live-memory logs, full regression logs and `windows-bench.log`
- source contact sheet and extracted frames under
  `experiments/glon-doom/assets/`
