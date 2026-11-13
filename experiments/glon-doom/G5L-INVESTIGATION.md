# Glon AvP G5l — OpenGL rendering

Branch: `glon-avp-g5l`  
Base: `51bc7684f42a9075fd11c983b85e1cd3b14a51a5` (G5k)  
Configuration: WSLg, AMD Ryzen 5 7535U, AMD Radeon integrated GPU,
Mesa OpenGL 4.2 compatibility, SDL2, GCC 11.4.0 `-std=c17 -O2`.

Verified renderer:
`D3D12 (AMD Radeon(TM) Graphics)`, vendor `Microsoft Corporation`,
version `4.2 (Compatibility Profile) Mesa 23.2.1-1ubuntu3.1~22.04.4`.

## 1. Architecture

```text
Glon source / S1 VM
  player, collision, doors, Alien AI, object identity, simulation ticks
        |
        | compact per-frame status/object records (existing text ABI)
        v
native host
  chooses CPU or OpenGL backend, owns no independent game state
        |
        +--> CPU raycaster + SDL framebuffer (G5k, fallback/reference)
        |
        +--> OpenGL backend: static level geometry uploaded once,
             dynamic camera/door/object/Alien/light state per frame
```

Glon still owns game state. The GL backend owns GPU resources, projection,
depth testing, texture sampling and rasterisation. The CPU path remains the
default reference; GL is explicit.

## 2. Scene/rendering ABI

Static renderer ABI (uploaded once):

- ten wall segments: `{x1,y1,x2,y2,material}`;
- one door segment: `{768,256,768,384,material=5}`;
- floor/ceiling planes at z=0/128 and texture/material IDs;
- 64×64 wall/floor/ceiling/door textures from G5j;
- four 64×96 object sprites and five 64×128 Alien frames.

Dynamic data supplied by Glon each frame:

- camera `px, py, angle` (`get_state`);
- `door_open, door_blocked, door_target`;
- Alien `ax, ay, state, detected, health, visible` and tracker/status values;
- six object records `{type,x,y,half-width,half-depth,height,visible}`;
- `light_phase` from the Glon simulation.

No per-pixel graphics commands are issued by S1. The host submits one scene per
frame. Static geometry is uploaded once; textures are uploaded once.

The current static wall arrays are a renderer-side ABI copy of the fixed level.
The next ABI revision should transfer them from a compact Glon scene block so
there is one authoritative copy.

## 3. OpenGL implementation

- SDL creates the window and OpenGL 2.1 compatibility context.
- Fixed-function perspective projection and view matrix.
- World mapping: `(x,y,z) -> (x/64, z/64, -y/64)`, y-up.
- Textured wall quads, floor and ceiling planes, door quad.
- Linear GPU texture sampling; G5j seamless textures.
- Depth buffer handles walls, doors, objects and Alien.
- Alpha-tested/depth-tested billboards for objects and Alien; transparent edges
  remain transparent, crates partially conceal the Alien.
- Static ceiling illumination comes from the ceiling texture; four localized
  emissive ceiling strips pulse with the Glon `light-phase`.
- CPU renderer remains available and unchanged as the reference.

Commands:

```sh
./build/doom --gl-bench 120 320 200 objects
./build/doom --gl-bench 120 640 400 objects
./build/doom --gl-bench 120 960 600 objects
./build/doom --gl-demo 640 400 objects
./build/doom --gl-demo 640 400 objects --alien-cover
./build/doom --gl-demo 640 400 objects --gl-door
./build/doom --gl-shots /tmp/g5l-shot 640 400 objects
```

CPU remains the default. If GL context creation fails, the GL command exits with
a clear SDL error; it does not report GL performance from the CPU path.

## 4. Benchmarks

Same machine, same scene/route, 120 frames, objects and Alien active where
applicable. CPU values are G5k C/D; GL values are the verified hardware path.

| Configuration | Renderer | Resolution | Median FPS | Sim | Submit+finish | Total |
|---|---|---|---:|---:|---:|---:|
| A | G5k CPU flat+objects | 320×200 | **27.5** | 5.1 ms | — | 36.4 ms |
| B | OpenGL | 320×200 | **60.0** | 12.6 ms | 2.08 ms | 16.66 ms |
| C | OpenGL | 640×400 | **62.4** | 11.9 ms | 2.05 ms | 16.02 ms |
| D | OpenGL | 960×600 | **57.7** | 12.4 ms | 2.34 ms | 17.33 ms |

Ranges from three runs: 320: 58.8–61.0; 640: 57.4–62.8; 960: 53.5–57.9.
`submit+finish` includes `glFinish`; it is not merely command-submission time.
The GPU is not resolution-bound at these sizes; the remaining CPU cost is the
Glon simulation/status/object projection path. The no-object GL run at
640×400 was ~108 FPS, confirming that object collision/projection/status
dominates the GL frame budget.

The GL backend does not execute the Glon wall or texrow stages. G5k's object
profile remains representative of Glon dynamic work: ~1.31M tick S1 ops/frame
with objects, and status/object records in addition. The seven primitives and
the G5h prepared-masm path are untouched.

## 5. Memory and executable size

- Loader: **14,082 used / 718 free** in GL object mode; CPU reference unchanged.
- Managed frontier: **33,600**; GC **0**.
- Code cursor: **13,126 / 16,128** (raw-wall CPU remains 14,828).
- Executable: **596,696 bytes** (`text 564,074`), versus G5k CPU **578,432**
  (`text 552,114`).
- Static scene/texture assets reside in native memory; no Glon loader table.

## 6. Lighting, objects and occlusion

- Ceiling panels are illuminated by the existing ceiling texture, not black.
- `light_phase` is incremented by Glon's tick and printed in status. The GL
  backend uses it to modulate four localized ceiling strips subtly. Two actual
  WSLg phase captures were taken; the pulse is intentionally local and subtle.
- Objects: two drums, two crates, cabinet, low container; original sprites.
- Depth testing gives wall/door occlusion and object/object/Alien ordering.
- Alpha testing preserves transparent sprite edges; the Alien is partially
  visible around a crate and fully hidden when directly behind it.
- Door closed is depth-rendered by the GPU and hides the corridor/objects.
- Glon collision prevents Marine/Alien penetration. No AI changes.

## 7. Screenshots

- `/tmp/g5l-corridor.png` — actual WSLg GL corridor, 640×400.
- `/tmp/g5l-door.png` — actual WSLg GL closed door + illuminated ceiling.
- `/tmp/g5l-cover.png` — actual WSLg GL Alien cover.
- `/tmp/g5l-pulseA.png`, `/tmp/g5l-pulseB.png` — light phases.
- `/tmp/g5l-gl-320/corridor.png`, `/tmp/g5l-gl-640/corridor.png`,
  `/tmp/g5l-gl-960/corridor.png` — actual GL framebuffer captures.
- `/tmp/g5l-contact-sheet.png` — labelled CPU/GL 320/640/960, door, cover and
  pulse comparison.
- `/tmp/g5k-cfinal/corridor.png` — CPU G5k comparison.

## 8. Sound interface proposal

No audio was implemented. A future host should accept event records from Glon:
`footstep`, `door-start`, `door-stop`, `tracker-beep`, `alien-hiss`,
`ambient-machine`. Glon decides event timing/phase; the host maps events to
short samples and mixes them. No sound engine in Glon or the renderer is needed.

## 9. Tests and limitations

Passing: all G5a–G5k texture, Alien, projection, stationary-cache, trace,
raw-wall, object-collision and frozen-S1 tests. GL smoke/bench completes on the
verified device; CPU remains the regression reference.

Limitations:

- Fixed-function OpenGL 2.1 compatibility, not a shader pipeline.
- Static wall geometry is currently a renderer-side ABI copy; moving it into a
  compact Glon scene block is the next architectural cleanup.
- No mipmaps/anisotropic filtering; texture sampling is linear.
- No sound implementation.
- CPU/GPU pixel equality is not expected; scene-state/geometry invariants are
  the regression basis.
- GL is explicit (`--gl-*`), not the default; CPU fallback is retained.

## 10. Recommendation for G5m

Make the GL scene ABI authoritative in Glon (one compact static-scene block),
then enable GL by default with CPU fallback. Add event-driven sound next. The
GPU result is real: ~60 FPS at 640×400 with objects versus 27.5 FPS CPU, and
~58 FPS at 960×600, with illuminated ceilings and correct depth occlusion.

## 11. Changed files

- `doom-host.c` — GL context/textures/geometry/billboards/bench/demo/shots,
  light-phase status, stationary-cache light handling.
- `doom.glon` — `light-phase` state/output.
- `objects.glon` — object world fields in the ABI.
- `G5L-INVESTIGATION.md`, `README.md`, Makefile `-lGL`.
- `GLON-ARCHITECTURE-MEMORY.md`, `MEMORY.md`.

Final commit hash is reported in the delivery message.
