# G6a — Spaceship Ambient Audio (Freesound 873687)

Status: implemented, tested and locally committed.

- Base: G5z `6168609`
- Branch/worktree: `glon-avp-g6a` at
  `/mnt/d/repos/glon-worktrees/glon-avp-g6a`
- Initial G6a implementation: `69614a6`
- Freesound replacement implementation: `42230b3`
- Scope: host audio only in `experiments/glon-doom`

## 1. What changed

The initial G6a pass played two synthetic loops from the previously supplied
`g5y.zip` pack (`ship_ambience_loop.wav`, `ventilation_loop.wav`). User testing
confirmed that the old ambience was still audible. This revision removes both
old WAVs and plays **only** the newly supplied Freesound recording. No old
layer or hiss remains underneath it.

The existing audio engine, SDL2 device path, mixer, volume/mute controls,
graceful fallback, gameplay and Glon semantics are preserved. Only the active
asset, its loader entry, asset verifier and documentation/provenance changed.

## 2. Source and attribution

Source file, found in the Windows Downloads directory:

```
873687__tommasomotteran__alien-spaceship-interior-ambience-04-sci-fi-engine-hum-electronics.wav
```

- Work: **Alien Spaceship Interior Ambience 04 Sci-Fi Engine Hum &
  Electronics**
- Creator: **Tommaso Motteran**
- Freesound: <https://freesound.org/people/TommasoMotteran/sounds/873687/>
- Licence: **Creative Commons Attribution 4.0 (CC BY 4.0)**,
  <https://creativecommons.org/licenses/by/4.0/>
- Source SHA-256:
  `1280420e42c743f7dd243860a40f91b7324e264b2a3e503bd1893d85d0fd3a1e`

Modifications for this game:

1. Converted from 24-bit stereo 44,100 Hz PCM to 16-bit mono 44,100 Hz PCM,
   matching the existing mono mixer.
2. The runtime applies its existing 1.0 s crossfade at the loop boundary,
   shortening playback to 33.000068 s.
3. No other processing, regeneration or added layers.

Full provenance is in `assets/audio/SOURCE.txt`; the credit and licence are
also in `README.md`. No endorsement by the creator is implied.

## 3. Formats and asset size

| File | Format | Frames | Duration | Bytes |
|---|---|---:|---:|---:|
| Source download | PCM s24le, stereo, 44.1 kHz | 1,499,403 | 34.000068 s | 8,997,154 |
| Runtime `assets/audio/ambient_loop.wav` | PCM s16le, mono, 44.1 kHz | 1,499,403 | 34.000068 s | 2,998,920 |

Runtime asset SHA-256:
`679a60117ab498983a70afecabc1e38c5081c0678ee4a58cb05dd7496e9aa811`.

The conversion was necessary because the existing mixer consumes 16-bit mono
samples; the 44,100 Hz rate was already compatible. The original 24-bit stereo
file is not committed. `tests/check_g6a_audio.py` verifies the runtime asset's
hash, mono/16-bit/44.1 kHz format and exact frame count.

## 4. Playback and looping

The existing implementation in `doom-host.c` is retained:

- `audio_load_loop()` loads the runtime WAV with `SDL_LoadWAV`, converts to
  44.1 kHz S16 mono if required, and precomputes a crossfaded loop.
- `audio_load_layers()` now loads only
  `ambient_loop.wav` with gain 3.0. The gain is higher than the old synthetic
  layers because the supplied recording is quiet: its measured source maximum
  is -10.5 dBFS and mean is -30.3 dBFS. At the default 60% master volume the
  master gain is 0.36; at 100% the source peak remains below full scale.
- Loop crossfade: **1.0 s / 44,100 frames**; played loop 1,455,303 frames
  (33.000068 s). This is a short boundary treatment, not a re-edit of the body
  of the recording.
- `audio_mix()` mixes the single layer on SDL's audio thread into 44.1 kHz
  S16 stereo, 1024-frame buffer. The mono source is duplicated to both
  channels.
- `audio_start()` starts audio only in the interactive GL/CPU loops after
  session setup. `audio_stop()` pauses/closes the device and frees the loop
  before `SDL_Quit`.
- Missing device, missing asset or unsupported device format logs a diagnostic
  and continues silently. No fatal path and no per-frame main-thread audio
  work.

## 5. Controls

Command line (unchanged from G6a):

```sh
--ambient-volume 0..100   # default 60
--no-audio                # skip audio initialization
--audio-test              # load/mix/volume/mute checks, no device
--audio-device-test       # open device, play 300 ms, shut down
--smoke-frames N          # normal interactive loop, auto-quit after N frames
```

Keyboard, in both GL and CPU interactive loops:

- `M` — mute/unmute
- `[` — lower ambient volume by 5
- `]` — raise ambient volume by 5

## 6. Verification

Full `make test` passes, RC 0. This includes the updated
`tests/check_g6a_audio.py`, `--audio-test` and frozen S1:

```
OK: S1 substrate frozen at f90496c26dc45c7a387d8fc8639cd2781507d3d2
```

Active-asset check:

- `--audio-test` logs
  `loaded Freesound 873687 spaceship interior 1455303 frames (33.000s)`.
- It does not log either old synthetic file; both files were deleted and no
  code path references them.
- `tests/check_g6a_audio.py` reports:
  `g6a audio: OK: Freesound 873687 44100 Hz mono s16le ambience loop`.

Programmatic audio checks (`--audio-test`):

- Loop frames: 1,455,303 (33.000 s) after the 1.0 s crossfade.
- Loop-wrap sample difference: **11** (16-bit amplitude).
- First 1024-frame block at default: peak 1,788, RMS 751, no overflow.
- Volume 0 and mute produce all-zero output.
- Volume/mute API round-trip passes.
- Mixer cost: about **0.0035–0.0038 ms per 1024-frame block** (23.22 ms
  real-time budget).

Device checks:

- `--audio-device-test` opened the real **PulseAudio** device at 44100 Hz,
  2 channels, played 300 ms and closed with RC 0.
- `SDL_AUDIODRIVER=dummy` opened, played and closed with RC 0.
- `SDL_AUDIODRIVER=definitely_not_a_driver` reported the unavailable target,
  continued without audio and exited RC 0.

Interactive smoke checks:

- `--smoke-frames 180` with the new recording started PulseAudio, ran the
  normal game loop and exited RC 0.
- The same loop with `--no-audio` exited RC 0.

**Listening was not performed by the agent.** The checks verify loading,
conversion, looping samples, controls, device startup/shutdown, fallback and
that the active asset is the new recording. They do not establish subjective
sound quality or whether the crossfade is inaudible on the user's speakers.
Please launch and listen; use `]` or `--ambient-volume` to adjust.

## 7. Performance and memory

- Final executable: **6,181,632 bytes**
  (`+9,520` vs G5z `6,172,112`; `+8` vs the previous G6a build).
- SHA-256: `f525de382b5c6d03aea650f94d1ba535d3b53b8cdc4aa578b4a8b145b36c361b`.
- Runtime asset on disk: **2,998,920 bytes**.
- Decoded loop buffer: **2,910,606 bytes** (`1,455,303 * 2`).
- The old synthetic assets (3,175,288 bytes on disk) were removed; net asset
  size is lower than the previous G6a state.
- GL texture memory unchanged from G5z: **10,419,072 bytes**.
- No new direct library dependency; SDL2 was already linked, with
  PulseAudio/ALSA as its backends.
- Mixer CPU: about 0.0035–0.0038 ms per 23.22 ms block.
- Six 360-frame interactive smoke runs, alternating audio/no-audio:
  **110.8 FPS median with audio**, **124.0 FPS median without** (-10.6%);
  observed ranges 102.0–134.8 and 108.5–146.5 overlap. The render path is
  unchanged and the host system's run-to-run variance is large, but this is a
  measured cost and is reported rather than hidden.
- No Glon loader/heap, collision, AI or frozen-S1 change.

## 8. Known limitations

- Playback has not been judged by ear by the agent.
- The 1.0 s crossfade shortens the loop to 33.000068 s. The recording was not
  re-edited, but the seam has not been auditioned.
- The mono runtime asset is duplicated to left/right; there is no positional
  or distance audio.
- Only continuous ambience is integrated. Door, Alien, tracker, rifle and
  impact effects remain future work.
- Audio starts only in the interactive loops; benchmarks, screenshots and
  trace paths remain silent.
- The device requires 44.1 kHz S16 stereo; other formats fall back to silence
  rather than resampling in the callback.
- Default 60% and gain 3.0 are a conservative starting point for a quiet
  source; room and WSLg mixer levels will determine the final loudness.

## 9. Evidence

```
evidence/g6a/
  audio-test.log                  active new-asset load, loop/mix/control test
  audio-device-test.log           real PulseAudio open/play/close
  audio-device-test-no-driver.log graceful no-device path
  audio-device-test-dummy.log     SDL dummy device path
  smoke-audio.log                 interactive GL smoke with new audio
  smoke-no-audio.log              interactive GL smoke without audio
  perf-audio.log                  6 x 360-frame smoke FPS
  perf-no-audio.log               matching no-audio runs
  make-test.log                   full test suite, RC 0
  audio-properties.txt            source/runtime hashes and ffprobe data
  executable-size.txt
  executable-sha256.txt
  metadata.txt
```

Implementation commit: `42230b3`. This report and the project-memory updates
are committed separately.
