# G6b — Event-Driven Sound Effects

Status: implemented, tested and locally committed.

- Base: G6a `f816ab5`
- Branch/worktree: `glon-avp-g6b` at
  `/mnt/d/repos/glon-worktrees/glon-avp-g6b`
- Implementation commit: `ea2b670`
- Scope: host audio only in `experiments/glon-doom`

## 1. Sources, formats and licences

All source files were found in the Windows Downloads directory. No substitutes
were synthesised.

| Event | Source | Original format | Licence |
|---|---|---|---|
| Blast door | Freesound 799198, **Lab Door Open and Close Industrial Sci-Fi Games**, kentspublicdomain | 24-bit mono 44.1 kHz, 6.000 s | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |
| Alien | Freesound 737275, **Alien Monster Roaring and Screaming**, Mastersoundboy2005 | 16-bit stereo 48 kHz, 2.1667 s | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |
| Rifle | Freesound 850829, **Sci-Fi LMG Concept**, Heretic4522 | 32-bit mono 48 kHz, 1.8047 s | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |
| Wall | Freesound 620830, **Low Metal Thud 2.wav**, F.M.Audio | 24-bit mono 96 kHz, 3.9631 s | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Cargo | Freesound 530486, **Metal Impact**, Rickplayer | MP3 mono 44.1 kHz, 1.790 s | [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) |

Source pages:

- <https://freesound.org/people/kentspublicdomain/sounds/799198/>
- <https://freesound.org/people/mastersoundboy2005/sounds/737275/>
- <https://freesound.org/people/Heretic4522/sounds/850829/>
- <https://freesound.org/s/620830/>
- <https://freesound.org/people/Rickplayer/sounds/530486/>

The licences were checked on the Freesound pages; 620830 is Attribution 4.0,
not CC0. It is credited as **“Low Metal Thud 2.wav” by F.M.Audio, CC BY 4.0**.
The retained ambience is **“Alien Spaceship Interior Ambience 04 Sci-Fi Engine
Hum & Electronics” by Tommaso Motteran, Freesound 873687, CC BY 4.0**
(<https://freesound.org/people/TommasoMotteran/sounds/873687/>), unchanged from
G6a.

### Processing

All runtime assets are 44,100 Hz, 16-bit mono PCM WAVs under
`assets/audio/sfx/`:

| Asset | Frames | Duration | Processing |
|---|---:|---:|---|
| `door_open.wav` | 97,020 | 2.200 s | first 2.2 s of 799198; 5 ms in / 30 ms out fades |
| `door_close.wav` | 97,020 | 2.200 s | 2.2–4.4 s of 799198; 5 ms in / 30 ms out fades |
| `alien_shriek.wav` | 95,552 | 2.167 s | 48 kHz stereo → 44.1 kHz mono; short edge fades |
| `rifle_shot.wav` | 28,665 | 0.650 s | single-shot attack/early tail from 850829; 30 ms fade-out |
| `wall_thud.wav` | 52,920 | 1.200 s | 96 kHz → 44.1 kHz mono; 50 ms fade-out |
| `cargo_impact.wav` | 39,690 | 0.900 s | MP3 decoded; 40 ms fade-out |

The door source explicitly contains both opening and closing in one clip. The
split at 2.2 s was chosen from the envelope (the closing/slam section ends
around 4.4 s); the isolated clips have short fades and were not re-synthesised.
Full provenance and source hashes are in `assets/audio/sfx/SOURCE.txt` and
`evidence/g6b/audio-properties.txt`.

## 2. Implementation

The G6a SDL2 mixer is extended; a second audio subsystem was not added.

- `AudioClip` stores six decoded mono effects; `AudioVoice` is an 8-voice
  pool with position, gain, tag, order and linear fade-out state.
- `audio_mix()` retains the ambient loop and sums active voices before the
  master volume; output is clamped to the S16 range. The callback remains
  nonblocking and independent of the render/Glon loop.
- `audio_play()` takes the SDL device lock only around voice allocation.
  Voice allocation is bounded; when full, the oldest voice is replaced.
- Door and Alien events are exclusive channels: replacing a voice applies a
  125 ms fade rather than stacking the old and new event.
- Mute and volume use the existing atomics and mute every layer/effect:
  `M`, `[`/`]`, `--ambient-volume`, `--no-audio` are preserved.
- Missing assets or a missing/unsupported device remain nonfatal; the game
  logs and continues silently.

### Blast doors

`audio_sfx_update()` watches `door_open` and `door_target` transitions. A
change from stationary to opening plays `door_open`; a change to closing plays
`door_close`. No event repeats while the same motion continues. A reversal
changes the motion state, stops the previous door voice with a short fade and
starts the other clip. The door position is `(768,320)` and the trigger is
distance-attenuated.

### Alien

The existing `detected` and `state` fields are watched. A detection edge or a
transition into attack state (state 2) triggers one shriek, with a **2.2 s**
cooldown and distance attenuation from `st.ax/st.ay`. The Alien's navigation,
attack thresholds and health are untouched.

### Rifle

The source is a single LMG gunshot, so the 0.65 s attack/early tail is used as
a single-shot clip. A **110 ms** cadence gate and the 8-voice pool allow rapid
fire without restarting one long sample. All shot triggers are independent
voices; the pool and output clamp bound simultaneous playback.

**Important limitation:** the G5r pulse rifle is a static overlay. This base
has no implemented firing, ammo consumption, damage or hit behaviour. G6b
therefore provides an explicitly presentation-only **`F` / left-mouse fire
preview** so the effect can be exercised. It does **not** change ammo, damage,
AI, collision or Glon semantics. When real firing is implemented, it should call
the existing `sfx_rifle_event()`/`audio_play()` path instead of the preview
input.

### Wall and cargo collisions

The host records the previous position and whether movement input was present.
If input was present and the resolved position did not change, the movement was
blocked. Cargo is classified using the existing object extents for cargo ids
0/1/3 (drums, crates, low container) inside a 40-unit probe; otherwise the
impact is a wall. Cargo takes priority, so a single collision never produces
both sounds.

A **350 ms** cooldown and a contact latch prevent retriggering while a movement
key is held against an obstacle. Wall and cargo clips are kept at lower gains
than the door/Alien events. No objects became movable and no physics changed.

## 3. Deterministic tests

`--sfx-test` passes all of the following without opening an audio device:

1. Door opening transition, no per-frame retrigger, reversal to closing,
   closing no-retrigger and stopped state.
2. Alien detection edge, suppressed repeat, cooldown suppression, attack after
   cooldown.
3. Rifle first shot, 50 ms rejection, 110 ms acceptance.
4. Wall impact, contact no-retrigger, cooldown, release/re-impact.
5. Cargo impact, no retrigger, cargo priority over wall.
6. No-movement contact clearing and cooldown thresholds.
7. Distance attenuation at source, mid-distance and beyond the fade range.
8. Cargo classification using object extents; host-level door, wall and cargo
   integration paths.
9. Voice limit (8), oldest-voice replacement, multiple simultaneous effects,
   mute and volume-zero silence.
10. Missing-device fallback is verified separately with
    `--audio-device-test` and `--sfx-device-test`.

`--audio-test` also verifies the retained ambience and asset loading. Full
`make test` passes RC 0, including `tests/check_g6a_audio.py`,
`tests/check_g6b_audio.py`, `--audio-test`, `--sfx-test` and frozen S1:

```
OK: S1 substrate frozen at f90496c26dc45c7a387d8fc8639cd2781507d3d2
```

### Device and graphical checks

- `--audio-device-test` opened the real **PulseAudio** device at 44100 Hz,
  2 channels and closed cleanly.
- `--sfx-device-test` loaded the ambience and all six effects, played each
  through the real PulseAudio device, and closed cleanly.
- Both device tests with `SDL_AUDIODRIVER=definitely_not_a_driver` continued in
  silence and exited RC 0.
- `--smoke-frames 180` launched the graphical GL loop with PulseAudio, ran at
  the normal interactive size, and exited RC 0; the matching `--no-audio` run
  also exited RC 0.

**Listening was not performed by the agent.** These checks verify asset
conversion, event logic, mixing, device lifecycle, fallback and graphical loop
startup. They do not establish subjective mix balance or whether each clip
sounds right on the user's speakers. Please launch and listen.

## 4. Size, memory and performance

- Final executable: **6,190,464 bytes**
  (`+8,832` vs G6a `6,181,632`).
- SHA-256: `182f96cacee82d00eeef20e48c8bebcd5caf720e8e78cee2412585c3a76e10fb`.
- Audio assets on disk: **3,821,200 bytes**
  (ambient 2,998,920 + six effects 822,280).
- Decoded runtime audio: **3,732,340 bytes**
  (ambient played loop 2,910,606 + effects 821,734).
- GL texture memory unchanged from G5z/G6a: **10,419,072 bytes**.
- Voice limit: 8; door/Alien replacement fade: 125 ms; rifle cadence: 110 ms;
  collision cooldown: 350 ms.
- Mixer cost with the ambient loop plus 8 voices: about
  **0.007–0.009 ms per 1024-frame block** (23.22 ms budget).
- Six 360-frame graphical smoke runs, alternating audio/no-audio:
  **172.1 FPS median with audio**, **180.9 FPS median without** (-4.9%);
  observed ranges 159.3–178.8 and 169.1–187.1 overlap. The render path itself
  is unchanged; this is a measured host-audio-thread/scheduling cost, not a
  renderer change.
- No Glon loader/heap, collision, AI or frozen-S1 change.

## 5. Known limitations

- The weapon has no actual firing gameplay, so the rifle effect is a
  presentation-only preview. No ammo, damage or combat behaviour is claimed.
- Wall/cargo impact detection triggers on blocked resolved movement. A
  diagonal slide that still changes position may not trigger; the sound is
  intentionally not a continuous proximity/contact loop.
- The door open/close split at 2.2 s is based on the source envelope, not
  subjective listening. The door clips are longer than the 16-tick visual
  travel; they start with the motion but may continue after it.
- Wall/cargo classification uses the existing rectangular object extents and a
  40-unit probe; it is not a new physics system.
- Distance attenuation is linear over 96–1200 world units and is applied to
  door and Alien only; wall/cargo and rifle are player-local.
- The mono source clips are duplicated to left/right; there is no panning.
- Output is hard-clamped at S16 limits; the 8-voice limit bounds but does not
  guarantee an artifact-free mix at extreme simultaneous levels.
- No subjective listening or loudness balancing has been performed.

## 6. Evidence

```
evidence/g6b/
  sfx-test.log                    deterministic event/mixer checks
  audio-test.log                  retained ambience and assets
  audio-device-test.log           real PulseAudio lifecycle
  audio-device-test-no-driver.log graceful no-device path
  sfx-device-test.log             six effects through PulseAudio
  sfx-device-test-no-driver.log   graceful no-device effects path
  smoke-audio.log                 graphical GL smoke with audio
  smoke-no-audio.log              graphical GL smoke without audio
  perf-audio.log                  6 x 360-frame FPS with audio
  perf-no-audio.log               matching no-audio runs
  make-test.log                   full suite, RC 0
  audio-properties.txt            formats, sizes and hashes
  memory.txt                      decoded memory breakdown
  executable-size.txt
  executable-sha256.txt
  metadata.txt
```

Implementation commit: `ea2b670`. This report and the project-memory updates
are committed separately.

Launch:

```sh
cd /mnt/d/repos/glon-worktrees/glon-avp-g6b/experiments/glon-doom
DOOM_ROOT=../.. ./build/doom
```

`F` or left mouse exercises the rifle preview; `E` operates the door; `M`
mutes; `[` / `]` adjust volume.
