# G6c — Portal Energy Surface and Audio Latency Diagnosis

Status: implemented, tested and locally committed.

- Base: G6b.1 `a9e0a05`
- Branch/worktree: `glon-avp-g6c` at
  `/mnt/d/repos/glon-worktrees/glon-avp-g6c`
- Implementation commit: `b0b7c86`
- Scope: procedural OpenGL portal effect; audio diagnosis and door-close source
  correction. No frozen-S1 or gameplay changes.

## Part A — Engineering-room portal

### Location and existing structure

The circular engine face is the front turbine in `eng_generator()`: axis
north–south, face at approximately `y=250.5`, centre `(x,z)=(325,64)`, outer
radius 54, with the existing textured outer ring, rotor spokes, central hub,
drum body, pipework, yellow safety railings, floor/platform and room lighting.
The old inner face was a flat orange disc.

### Procedural surface

`gl_energy_surface()` is called inside `gl_turbine_face()` at the inner face
radius (`0.52 * 54`). It draws only procedural geometry:

- warm orange plasma bed with an angular shimmer;
- five expanding/contracting ripple rings, alternating orange and blue-white;
- two slowly rotating shimmer bands;
- four thin blue-white/orange electrical arc ribbons, in front of the rotor
  spokes and outside the central hub;
- additive blending with depth writes disabled, so the surface is luminous but
  remains contained behind the existing outer structure.

All radii stay at or below 0.52 of the rotor face; the outer turbine ring,
spokes, hub, railings and room are untouched. Animation uses simulation time
(`sim_tick / 60`), not wall-clock randomness or external assets.

Portal-only geometry/visual controls:

```sh
./build/doom --portal-test
./build/doom --gl-portal-shots /tmp/portal 1920 1080
./build/doom --gl-portal-bench 180 1920 1080
./build/doom --portal-smoke --smoke-frames 180
```

`--portal-test` renders the portal view at `t=0` and `t=0.75 s` and verifies:

- 7,018 pixels differ between the two times;
- the changed bounding box is `414,238..529,361` at 960x600, inside the central
  portal mask.

The portal-only 180-frame runs measured **171–216 FPS at 960x600** and
**109–134 FPS at 1920x1080** on the WSLg D3D12 driver. Full before/after
screenshots and detail crops are under `evidence/g6c/portal-*`.

## Part B — Audio latency diagnosis

### Source onset

All active event assets were measured for the first sample above a 500
amplitude threshold:

| Clip | First audible | Notes |
|---|---:|---|
| Rifle | 0.023 ms | no leading silence |
| Door open | 8.957 ms | immediate mechanism |
| Door close | 1.043 ms | after the G6c re-cut |
| Wall thud | 0.317 ms | immediate |
| Cargo impact | 14.059 ms | was 78.685 ms before G6b.1 trim |
| Alien shriek | 14.150 ms | was 64.150 ms before G6b.1 trim |

The door clips were the only content with a long quiet lead-in: the old
closing excerpt began at source 2.2 s, but its loud impact is at 3.9 s, so the
perceived closing “hit” arrived about 1.7 s after the trigger. G6c re-cuts the
closing asset from **3.15–4.35 s**. It still contains closing motion and the
final impact, which now arrives about **0.75 s** into the clip; onset is
1.043 ms. This is a source-content correction, not an arbitrary playback
offset.

### Application, mixer and callback

G6b.1 already showed that the input → event → voice → callback path is short:
the F key sets fire in the SDL event pass; `audio_sfx_update()` requests on the
same interactive frame; the 256-frame buffer callback follows in **0.1–8.8
ms**. G6c did not change that engine.

### PulseAudio and the actual detected cause

Two small supported-API probes were added:

- `tools/pa_latency.c` — lists sink inputs with their buffer and sink latency.
- `tools/pa_sinks.c` — lists sinks and their reported/configured latency.

The current WSLg server reports:

```
sink index=1 name=RDPSink desc=RDP Sink class=abstract
  latency=1209.40-1236.30 ms, configured=6.26 ms

sink_input app=doom media=Audio Stream
  buffer=20.32 ms, sink=628.42 ms, total=648.74 ms
# another query: buffer=20.32 ms, sink=611.99 ms, total=632.31 ms
```

This is the cause of the remaining user-observed delay: the game’s output is
being carried through the WSLg **RDPSink** (RDP/Windows audio transport), not
through a low-latency local sink. The app-side buffer is already only ~20 ms;
the sink reports roughly **0.6–1.2 seconds**. No application buffer tuning or
sample offset can remove that transport latency. G6b.1’s 256-frame buffer and
`PULSE_LATENCY_MSEC=25` target remain in place, but they address only the
application-side portion.

The environment exposes no alternate PulseAudio sink in this session; WSL2 /
Windows output settings are outside the game process. This is a documented
remaining limitation, not something G6c masks with a fake offset.

### Remaining audio limitation

- Rifle and door effects are immediate in the app and callback traces, but the
  WSLg RDPSink adds 0.6–1.2 s before the Windows side hears them.
- A playtest on this WSLg/Windows audio path will still sound delayed until the
  PC’s audio transport/sink latency is reduced. A native Linux audio sink or a
  lower-latency WSLg/RDP audio configuration would be required.
- The door-close source now delivers its impact much sooner, but it is still a
  re-cut of the supplied recording rather than a re-designed effect.
- No subjective listening verification was performed by the agent.

## Verification

Full `make test` passes RC 0, including:

- `tests/check_g6a_audio.py` and updated `tests/check_g6b_audio.py`
  (door-close hash/frame count updated for the re-cut);
- `--audio-test`, `--sfx-test`, `--audio-e2e`;
- the new input F test and frozen-S1 check.

Frozen S1:

```
OK: S1 substrate frozen at f90496c26dc45c7a387d8fc8639cd2781507d3d2
```

Additional checks:

- `--portal-test` passes (7,018 changed pixels, contained bbox).
- `--portal-smoke --smoke-frames 180` runs the graphical portal view with
  PulseAudio and exits cleanly.
- `--audio-e2e` exercises F input, E activation, tick-into-wall and
  tick-into-cargo with nonzero PCM.
- Real PulseAudio and unavailable-driver paths for ambient and effects pass.
- `pa_latency` / `pa_sinks` outputs are recorded in evidence.

**No subjective listening verification was performed.** The portal look,
door-close edit and audio transport behavior require user playtest.

## Measurements and memory

- Final executable: **6,203,168 bytes** (`+4,216` from G6b.1).
- SHA-256: `955c9cab90798b42ee1287ffbd61d8f5b8638b3a654778d6ed35ce16b7e7a64c`.
- Audio assets on disk: **3,723,298 bytes** (ambient 2,998,920 + effects
  724,378).
- Runtime decoded audio: **3,634,438 bytes** (ambient loop 2,910,606 + effects
  723,832).
- Mixer cost: about 0.0013–0.0014 ms per 256-frame block (5.80 ms budget).
- GL texture memory unchanged at 10,419,072 bytes.
- Portal-only rendering: 171–216 FPS at 960x600, 109–134 FPS at 1920x1080.
- No new external texture/audio assets; PulseAudio probing tools are diagnostic
  only.

## Evidence

```
evidence/g6c/
  portal-before/                 baseline static orange face
  portal-after/                  portal t=0, 0.5, 1.0, 1.5 s and t=0 room view
  portal-compare.png             side-by-side before/after
  portal-before-detail.png
  portal-after-detail.png
  portal-test.log
  portal-smoke.log
  portal-bench-960.log
  portal-bench-1920.log
  pa-latency.txt                 sink-input buffer/sink latency
  pa-sinks.txt                   RDPSink report
  pa_latency.c / pa_sinks.c      diagnostic source snapshots
  audio-e2e.log
  sfx-test.log
  audio-test.log
  audio-device-test*.log
  sfx-device-test*.log
  make-test.log
  audio-properties.txt
  onsets.txt
  memory.txt
  metadata.txt
  executable-size.txt
  executable-sha256.txt
```

Implementation commit: `b0b7c86`. This report and the project-memory updates
are committed separately.

Launch:

```sh
cd /mnt/d/repos/glon-worktrees/glon-avp-g6c/experiments/glon-doom
DOOM_ROOT=../.. ./build/doom
```

Portal views:

```sh
./build/doom --portal-smoke --smoke-frames 180
./build/doom --gl-portal-shots /tmp/portal 1920 1080
```

`F`/left mouse is still the presentation-only rifle preview; `E` operates the
door; `M` mutes; `[` / `]` adjust volume.
