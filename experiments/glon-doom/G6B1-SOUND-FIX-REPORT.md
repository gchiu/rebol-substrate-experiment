# G6b.1 — Interactive Sound Latency and Missing Effects

Status: implemented, tested and locally committed.

- Base: G6b `b37f66e`
- Branch/worktree: `glon-avp-g6b1` at
  `/mnt/d/repos/glon-worktrees/glon-avp-g6b1`
- Implementation commit: `88bdc6f`
- Scope: host audio/event code plus a report-only movement flag in `doom.glon`

## 1. User-visible defects and root causes

### Rifle: ~1 s between input and shot

The application path was already immediate:

- F keydown / left mouse sets `held.fire` in the same SDL event pass.
- `audio_sfx_update()` runs every interactive frame and requests the rifle
  event on that frame.
- The rifle WAV has its first above-threshold sample at **0.023 ms**; it is not
  a leading-silence artefact.
- With the old 1024-frame buffer, the traced request-to-callback time was
  **0.1–8.8 ms** before the change. With the new 256-frame buffer it is
  **0.07–8.37 ms**. The callback is not a 1 s application delay.

The remaining stage was the PulseAudio output queue / server target, which SDL
did not request at a low latency. G6b1 now:

- uses a **256-frame** callback buffer (5.80 ms of audio per block), and
- sets `PULSE_LATENCY_MSEC=25` when the environment has not already set it,
  before the SDL audio subsystem opens.

This is the concrete software change. The speaker/server latency itself was not
reliably captured by a monitor loopback in this WSLg environment; a fresh user
playtest is required to confirm perceived latency. The software request →
callback path is now under 9 ms in the traces.

### Blast-door opening was silent

`SFX_DOOR_OPEN` was the first enum value, therefore **0**. `audio_sfx_update()`
dispatched with `if (ev)`, so the opening event was indistinguishable from
“no event” and was discarded. Closing had enum value 1 and did request a voice.
This explains why door closing could work while opening was silent.

The enum now starts with an explicit **`SFX_NONE=0`** sentinel:
`SFX_DOOR_OPEN=1`, `SFX_DOOR_CLOSE=2`, etc. Both opening and closing are
submitted. The door trace before/after shows the fix directly:

```
# G6b before (opening absent, only close seen)
door event=2 open=60 target=0
request id=2

# G6b.1 after
door event=1 open=8 target=64   request id=1   callback 2.67 ms
door event=2 open=60 target=0  request id=2   callback 6.38 ms
```

The door clips are still split from the supplied 799198 source at 2.2 s; they
start with the actual `door_open`/`door_target` transition and reverse without
stacking (exclusive door voice, 125 ms replacement fade). Distance
attenuation remains at the door coordinate `(768,320)`.

### Wall collisions had no sound

G6b required **both** `px` and `py` to be unchanged between frames. A collision
can block one axis while the other slides, and a one-tick approach may not have
settled against the wall yet, so many genuine impacts failed this test. It also
used low wall gain.

The existing Glon movement resolver now sets a **report-only**
`doom/player-hit` flag whenever `move-x` or `move-y` is blocked. The flag is
cleared at the start of each tick and exposed in the status block. Movement,
collision geometry and gameplay behaviour are unchanged.

`audio_sfx_update()` uses `st.player_hit` with the existing movement-input mask,
so a genuine blocked movement produces one wall/cargo event. A contact latch and
350 ms cooldown prevent continuous retriggering while holding a key. Wall gain
was raised from 0.45 to **0.90**.

### Cargo collisions

The same resolver flag now feeds cargo classification. The existing object
extents for ids 0/1/3 (drums, crates, low container) are probed within 40 world
units; cargo takes priority over wall so one impact does not produce two
sounds. Cargo gain was raised from 0.50 to **1.00**.

The cargo source had **78.7 ms of leading silence**; Alien had **64.1 ms**.
Both were trimmed to about **14 ms** onset. The other onset measurements are:

| Clip | First audible (>500) |
|---|---:|
| Rifle | 0.023 ms |
| Door open | 8.957 ms |
| Door close | 0.862 ms |
| Wall thud | 0.317 ms |
| Cargo impact | 14.059 ms (was 78.685) |
| Alien shriek | 14.150 ms (was 64.150) |

The rifle remains a **presentation-only** F/left-mouse preview because G5r has
no implemented firing mechanic. No ammo, damage, AI, collision or combat state
changed.

## 2. Exact changes

- `doom-host.c`
  - `SFX_NONE=0` sentinel; all effect IDs shifted to non-zero values.
  - `AUDIO_SAMPLES` 1024 → 256.
  - `PULSE_LATENCY_MSEC=25` set when unset, before audio initialization.
  - Door/cargo/Alien/rifle/wall gains rebalanced:
    door 1.00, Alien 0.90, rifle 0.60, wall 0.90, cargo 1.00.
  - Collision gate now uses `st.player_hit` rather than whole-position
    equality.
  - Added `--audio-e2e`, `--sfx-smoke`, `--audio-latency-test`, and
    `GLON_AUDIO_TRACE=1` timing/event logs.
- `doom.glon`
  - Added report-only `doom/player-hit`, cleared per tick and set by the
    existing `move-x`/`move-y` blocked branches; exposed as status field 18.
  - No change to movement, collision extents, doors, Alien or any game rule.
- `assets/audio/sfx/alien_shriek.wav`, `cargo_impact.wav`
  - Leading silence trimmed; converted output remains 44.1 kHz mono S16.
- `assets/audio/sfx/SOURCE.txt`
  - Documents the trims and retains all G6b licence/attribution details.
- `tests/check_g6b_audio.py`
  - Updated hashes/frame counts for the two trimmed assets.
- `Makefile`
  - Adds `--audio-e2e` to the full test target.

All original licences are unchanged: Freesound 799198/737275/850829/530486 are
CC0, 620830 is F.M.Audio CC BY 4.0, and the retained 873687 ambience is
Tommaso Motteran CC BY 4.0. The five effect sources are not re-synthesised.

## 3. End-to-end tests

`--audio-e2e` uses the dummy SDL device and exercises the same paths as the
interactive loop, then checks active voices and nonzero PCM:

- F input handler → `audio_sfx_update()` → rifle voice → nonzero PCM.
- Actual E activation block → target 64 → door-open voice and PCM; then close
  transition → door-close voice and PCM.
- Actual `tick(1)` movement into the west wall → `player_hit` → wall voice and
  PCM.
- Actual `tick(1)` movement into the crate → `player_hit` → cargo voice and
  PCM.
- All six clips pass the <50 ms onset check.

Full `make test` RC 0 includes `tests/check_g6a_audio.py`,
`tests/check_g6b_audio.py`, `--audio-test`, `--sfx-test`, `--audio-e2e`, the
new input F test and frozen S1:

```
OK: S1 substrate frozen at f90496c26dc45c7a387d8fc8639cd2781507d3d2
```

The real PulseAudio device test and the no-driver fallback both pass. A
graphical `--sfx-smoke` loop with real SDL F/E/W events produced:

- rifle request id 4, callback 1.72 ms;
- door open request id 1, callback 2.67 ms;
- door close request id 2, callback 6.38 ms;
- wall request id 5, callback 0.89 ms;
- cargo request id 6, callback 6.47 ms.

The separate `--audio-latency-test` measured a request-to-callback time of
**5.58 ms** with the 256-frame buffer. This measures the SDL callback path, not
the physical speaker.

**No subjective listening verification was performed by the agent.** The user
must playtest the new buffer/PulseAudio target and the rebalanced levels; the
automated evidence only establishes that the correct requests reach the mixer
and produce nonzero PCM.

## 4. Memory and performance

- Final executable: **6,198,952 bytes**
  (`+8,488` from G6b).
- SHA-256: `2d8defda450f9f0e579a479f628bdc4cb99bcaffee586b6e2d090b6871f275b0`.
- Audio assets on disk: **3,811,498 bytes**
  (ambient 2,998,920 + effects 812,578).
- Runtime decoded audio: **3,722,638 bytes**
  (ambient loop 2,910,606 + effects 812,032).
- Mixer cost: about **0.0016–0.0019 ms per 256-frame block**, against a
  5.80 ms real-time budget.
- Six 360-frame graphical smoke runs: **132.0 FPS median with audio**,
  **139.2 FPS without** (-5.2%); ranges 111.3–136.1 and 132.9–161.1 overlap.
- GL texture memory unchanged at 10,419,072 bytes.
- No Glon loader/heap, render, gameplay, AI or frozen-S1 change beyond the
  report-only player-hit status field.

## 5. Remaining limitations

- Actual speaker / PulseAudio server output latency was not captured
  reliably; the user must confirm the perceived rifle latency. The application
  path is now measured at <9 ms request-to-callback.
- The rifle remains presentation-only; no firing gameplay exists.
- The door open/close split at 2.2 s remains an envelope-based split, not a
  subjective edit.
- Wall/cargo detection now fires when either movement axis is blocked, but a
  purely sliding path with neither resolver rejection produces no impact, by
  design.
- Effect gains were raised for audibility under the ambient loop; final balance
  is subjective and has not been listened to by the agent.
- The 256-frame buffer is aggressive; no underruns were observed in the smoke
  runs, but a different PulseAudio server could behave differently.
- The existing G6b door/cargo/Alien clips remain mono duplicated to stereo.

## 6. Evidence

```
evidence/g6b1/
  before-smoke.log               pre-fix GUI trace: missing door open, cargo as wall
  smoke-events.log               post-fix GUI F/E/W events and callbacks
  latency-before.txt             pre-fix 1024-frame trace
  latency.txt                    post-fix 256-frame trace
  audio-e2e.log                  actual input/gameplay-path PCM tests
  sfx-test.log                   unit/event/mixer tests
  audio-test.log                 ambient/asset tests
  audio-device-test.log          real PulseAudio lifecycle
  audio-device-test-no-driver.log
  sfx-device-test.log            all effects through PulseAudio
  sfx-device-test-no-driver.log
  smoke-audio.log                graphical pulse-audio smoke
  smoke-no-audio.log
  perf-audio.log                 6 x 360-frame FPS
  perf-no-audio.log
  make-test.log                  full suite, RC 0
  audio-properties.txt
  onsets.txt
  memory.txt
  metadata.txt
  executable-size.txt
  executable-sha256.txt
```

Implementation commit: `88bdc6f`. This report and the project-memory updates
are committed separately.

Launch:

```sh
cd /mnt/d/repos/glon-worktrees/glon-avp-g6b1/experiments/glon-doom
DOOM_ROOT=../.. ./build/doom
```

`F` or left mouse exercises the rifle preview; `E` operates the door; `M`
mutes; `[` / `]` adjust volume.
