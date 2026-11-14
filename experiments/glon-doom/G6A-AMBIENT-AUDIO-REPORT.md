# G6a — Spaceship Ambient Audio

Status: implemented, tested and locally committed.

- Base: G5z `6168609`
- Branch/worktree: `glon-avp-g6a` at
  `/mnt/d/repos/glon-worktrees/glon-avp-g6a`
- Implementation commit: `69614a6`
- Scope: host audio only in `experiments/glon-doom`

## 1. Inspection

The downloaded pack was found at:

```
/mnt/c/Users/anon_/Downloads/g5y.zip
```

It contains 11 mono PCM WAV files, 44,100 Hz, 16-bit, plus `README.md` and
`manifest.json`. The archive README describes them as original procedural
synthesis, not recordings. G6a uses only the two continuous ambience loops:

| File | Duration | Frames | Format | Bytes | SHA-256 |
|---|---:|---:|---|---:|---|
| `ship_ambience_loop.wav` | 24.000 s | 1,058,400 | mono PCM s16le, 44.1 kHz | 2,116,844 | `63107c5d14759bdebb66c402481d0b1017a650165d6649dd6c0ac03f22e588e4` |
| `ventilation_loop.wav` | 12.000 s | 529,200 | mono PCM s16le, 44.1 kHz | 1,058,444 | `158ba4c6c096af9ff28d9f4af10907f3f1c7aa20807f67aaa2ce62ef40c3ba6d` |

The door, Alien, tracker, rifle and metal-impact WAVs are deliberately **not**
integrated in this pass. The source files were copied unchanged into
`assets/audio/`; `tests/check_g6a_audio.py` checks their hashes, channel count,
sample width, sample rate, frame count and duration.

### Existing infrastructure

- The native host already links SDL2 for video, input and timing.
- The interactive paths are `interactive_gl()` (default) and `interactive()`
  (CPU fallback). Both used `SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER)`; there
  was no audio initialization.
- Benchmarks, screenshots, `--trace` and self-tests do not use the interactive
  path. Audio is deliberately started only in the two interactive loops, after
  session setup, so diagnostics remain silent and unchanged.
- SDL2 audio already has PulseAudio/ALSA backends on this WSLg system. No new
  framework or direct audio library was added.

## 2. Playback implementation

The implementation is in `doom-host.c`:

- `audio_load_loop()` uses `SDL_LoadWAV`, converts to 44.1 kHz S16 mono with
  `SDL_AudioCVT` when necessary, builds a crossfaded loop, and frees the
  temporary decoded source.
- Ship loop: **1.0 s** (44,100-frame) crossfade; resulting loop 23.0 s.
- Ventilation loop: **0.5 s** (22,050-frame) crossfade; resulting loop 11.5 s.
- The crossfade moves the source tail into the loop head, so the loop wrap
  follows adjacent source samples. A linear crossfade is used.
- `audio_mix()` mixes both layers on SDL's audio thread. Layer gains are 0.70
  (ship) and 0.35 (ventilation); the user volume is squared before applying it
  to keep low settings gentle.
- Device format: 44.1 kHz, `AUDIO_S16SYS`, 2 channels, 1024-frame buffer.
- `audio_start()` initializes `SDL_INIT_AUDIO`, loads both assets and opens the
  device. It is called from both interactive loops.
- `audio_stop()` pauses and closes the device, frees both loop buffers and
  quits the audio subsystem before `SDL_Quit`. It runs on normal Esc/quit.
- If SDL audio is unavailable, assets are missing, or the device does not
  provide the required format, the game logs a diagnostic and continues
  without ambient audio. There is no fatal path and no render-loop blocking.

Default ambient volume is **60%**. At the default level the programmatic mix
peaks at about 2,898/32,767 and RMS is about 1,425, leaving headroom for future
effects.

## 3. Controls

Command line:

```sh
--ambient-volume 0..100   # default 60; clamped
--no-audio                # skip audio initialization entirely
--audio-test              # load/mix/volume/mute checks, no device
--audio-device-test       # open device, play 300 ms, shut down
--smoke-frames N          # normal interactive loop, auto-quit after N frames
```

Keyboard, in both GL and CPU interactive loops:

- `M` — mute/unmute (keeps the current volume setting)
- `[` — lower ambient volume by 5
- `]` — raise ambient volume by 5

The current volume/mute changes are printed to the console.

## 4. Verification

Full `make test` passes, RC 0. The new asset test and `--audio-test` are part
of the target; frozen S1 remains:

```
OK: S1 substrate frozen at f90496c26dc45c7a387d8fc8639cd2781507d3d2
```

Programmatic audio checks (`--audio-test`):

- Both source WAVs load and convert successfully.
- Resulting loop lengths: 1,014,300 frames (23.000 s) and 507,150 frames
  (11.500 s).
- Loop-wrap sample differences: 51 and 229 (16-bit amplitudes).
- Default mix: peak 2,898, RMS 1,425, no integer overflow.
- Volume 0 and mute both produce an all-zero buffer.
- Volume/mute API round-trip passes.
- Mixer cost measured at about **0.003–0.005 ms per 1024-frame block**, against
  a 23.22 ms real-time budget.

Device checks:

- `--audio-device-test` opened the real **PulseAudio** device at 44100 Hz,
  2 channels, played for 300 ms and closed with RC 0.
- `SDL_AUDIODRIVER=dummy` also opened and closed cleanly.
- `SDL_AUDIODRIVER=definitely_not_a_driver` reported the missing target,
  continued in silence and exited RC 0.

Interactive smoke checks:

- `--smoke-frames 180` with audio started both layers, ran the normal GL game
  loop, and exited cleanly with RC 0.
- The same loop with `--no-audio` exited RC 0.
- The same loop with an unavailable audio driver exited RC 0 without audio.

**Listening was not performed by the agent.** These results verify loading,
mixing, controls, device startup/shutdown, graceful fallback and the loop
sample seam programmatically. They do not establish subjective sound quality,
loudness balance in the room, or whether the crossfade is inaudible on every
device. The user should launch the game and listen; the default can be changed
with `--ambient-volume` and `[`/`]`.

## 5. Performance and memory

- Final executable: **6,181,624 bytes**
  (`+9,512` vs G5z `6,172,112`).
- SHA-256: `91002f2e20b6f2c5050dac0aeb91b81959990f6537c3f06eb32613ca168a009e`.
- Audio assets on disk: **3,175,288 bytes** (2,116,844 + 1,058,444).
- Decoded loop buffers: **3,042,900 bytes**
  (`(1,014,300 + 507,150) * sizeof(int16_t)`).
- GL texture memory is unchanged from G5z: **10,419,072 bytes**.
- No new direct library dependency: SDL2 was already linked. PulseAudio/ALSA
  are transitive SDL2 backends.
- Mixer CPU: about 0.003–0.005 ms per 23.22 ms block.
- Ten 360-frame interactive smoke runs, alternating audio/no-audio:
  **82.1 FPS median with audio**, **86.1 FPS median without** (-4.6%);
  observed ranges 58.1–96.0 and 66.8–93.9 overlap. The render path itself is
  unchanged; run-to-run variance on this WSLg system is larger than the
  measured median difference.
- No Glon loader/heap, collision, AI or frozen-S1 change.

## 6. Known limitations

- Playback has not been judged by ear by the agent. The crossfade and levels
  are programmatically verified only.
- The 1.0 s / 0.5 s crossfades shorten the loops to 23.0 s and 11.5 s. They
  remove the sample discontinuity at the wrap, but the source material's
  intended seam has not been auditioned.
- Both mono sources are duplicated to left/right; there is no positional or
  distance audio.
- Only continuous ambience is integrated. Door, Alien, tracker, rifle and
  impact effects remain future work.
- Audio starts only in the interactive GL/CPU game loops; screenshot,
  benchmark and trace paths remain silent.
- The device open requires 44.1 kHz S16 stereo. Other formats fall back to no
  audio rather than resampling in the callback.
- Default 60% is a conservative starting level; room, speakers and WSLg mixer
  will determine the final level.

## 7. Evidence

```
evidence/g6a/
  audio-test.log                  loop/mix/control self-test
  audio-device-test.log           real PulseAudio open/play/close
  audio-device-test-no-driver.log graceful no-device path
  audio-device-test-dummy.log     SDL dummy device path
  smoke-audio.log                 interactive GL smoke with audio
  smoke-no-audio.log              interactive GL smoke without audio
  perf-audio.log                  10 x 360-frame smoke FPS
  perf-no-audio.log               matching no-audio runs
  make-test.log                   full test suite, RC 0
  audio-properties.txt            source hashes and ffprobe data
  executable-size.txt
  executable-sha256.txt
  metadata.txt
```

Implementation commit: `69614a6`. This report and the project-memory updates
are committed separately.
