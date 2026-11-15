# G6h.1 — scuttler visibility fix

Worktree: `/mnt/d/repos/glon-worktrees/glon-avp-g6h1`  
Branch: `glon-avp-g6h1` (separate from the preserved G6h branch)  
Base: G6h `9967dba`  
Implementation: `192784bb53007c7b4e6134042ad5f13b0ffc9d19`

Status: **FIX IMPLEMENTED AND AUTOMATED-VERIFIED; PHYSICAL WINDOWS ACCEPTANCE
PENDING.** G6h.1 remains unaccepted and has no tag.

## 1. What failed

Physical playtest reported no visible scuttlers. The live-path diagnostics
confirmed:

- all three scuttlers spawned active (`s0=0,s1=0,s2=0`) and the AI/draw path ran;
- projection was not the failure: the projected boxes were on screen where
  expected;
- the extracted textures had visible alpha and RGB content;
- the renderer **was** invoked, including in the normal `gl_scene` path.

The actual failures were layout/order:

1. **Weapon occlusion.** The low floor sprites were drawn **before** the
   first-person weapon. At normal corridor floor height the weapon covered
   nearly the entire projected body. The marker probe showed s0's box behind
   the gun.
2. **No visibility gating.** Screen-space scuttlers were drawn through closed
   doors/walls, appearing as tiny unexplained specks in unvisited areas (or not
   perceived at all).
3. **Placement/scale.** The starting scuttler was behind the closed door and
   14 world-units high, producing a ~16px body at the initial view; the others
   were far down the corridor or off-screen.

Baseline evidence is in `evidence/g6h1/diagnosis-baseline/`: start, corridor and
marker captures plus `probe.log`.

## 2. Smallest verified correction

- **Draw order:** scuttlers are now rendered **after the weapon**, before the
  tracker HUD, so a low scuttler approaching in the corridor cannot disappear
  behind the gun.
- **LOS visibility:** Glon now computes a one-byte visibility mask with the
  existing `doom/los` against each scuttler. The host skips invisible entities.
  Behind-door/unreached scuttlers no longer leak through geometry.
- **Reachable placement:** s0 now starts in the starting room at `(650,360)`;
  s1 is at `(900,280)` and s2 at `(1050,360)`, both in the corridor after the
  door. The first scuttler is visible at the initial camera; the other two
  become visible when the player opens/reaches the corridor.
- **Scale:** visual sprite height increased 14 -> 20 world units (width 2x),
  while **collision top remains 14** and jump clearance is unchanged.
- No frozen S1, primitive, semantic, ABI or capacity change.

## 3. Visibility verification

The new `--gl-scuttler-probe` uses the real Windows GL path. For each scene it:

- logs player, scuttler positions/states/health, depth, bearing, projected box,
  visibility mask and draw calls;
- reads actual framebuffer pixels with scuttlers enabled and with them hidden;
- counts differing pixels, not merely draw calls;
- fails below 200 differing pixels;
- captures the live start, live corridor, control and magenta-marker frames.

Native Windows packaged probe results:

| Scene | 960x600 | 1280x720 |
|---|---:|---:|
| Start, one visible scuttler | **1,797 px** | **2,606 px** |
| Corridor after door, two visible | **9,617 px** | **13,920 px** |

Both scenes **PASSED** the real-pixel threshold. The 960/1280 PNGs are in
`evidence/g6h1/live-960/` and `live-1280/`; the start image shows a scuttler
clearly to the left of the weapon, and the corridor image shows two scuttlers.

## 4. Measurements

- Loader: 25,000 capacity; after game **16,779 used / 8,221 free**;
  after host blocks **18,114 used / 6,886 free**.
- Globals: 251/512.
- Managed heap high: **34,080**.
- Code cells: **12,454**.
- Tick-only `--bench 60`: **8.96 ms/tick** (G6g 2.74 ms/tick, prior G6h
  8.43 ms/tick).
- Windows exe: **6,994,944 bytes**, SHA-256
  `8b150d82820b77b9720008fd0f7640606510483e216121ef1bb032cf3276e68a`.
- Windows distribution: **22,537,334 bytes**.
- Launcher: `dist/windows/run-g6h1.bat`.

## 5. Verification

Linux full `make test`: **PASSED** (all G6e-G6g regressions, multi-alien, live
pixel visibility, frozen S1).

Native Windows: multi-alien, self, combat, door-sync, corpse-stability,
muzzle-anchor, audio-e2e, jump, alien-collision and collision-probe all
**PASSED**. Packaged `run-g6h1 --self-test`, `--multi-alien-test` and
`--gl-scuttler-probe` passed.

`--multi-alien-test` verifies active spawns, visibility mask at spawn, target
isolation, LOS/closed doors, independent pursuit, wall blocking, jump-over,
corpse stability, reset and repeated death/restart cycles.

## 6. Preserved G6h build

The original G6h worktree `/mnt/d/repos/glon-worktrees/glon-avp-g6h` is
untouched. Its implementation `adc9ab6`, evidence `13937de`, delivery `1a02792`
and executable SHA
`e6f68555aa3f1aa0e651d9087b294a5aceefef10e10233d9304cac53afac1de1`
remain available. G6h.1 is a separate branch/distribution.

## 7. Known limitations

- Physical Windows acceptance remains pending; do not mark accepted.
- Scuttlers still use a screen-space perspective overlay. LOS prevents drawing
  through walls/doors, but exact world-depth occlusion against props is
  approximate.
- Scuttler attack remains visual/timing only; no player-health damage was added.
- G6f.2 remains the accepted baseline; G6f.3/G6f.4/G6g remain separately pending.
- No merge, push, tag, release or deployment.
