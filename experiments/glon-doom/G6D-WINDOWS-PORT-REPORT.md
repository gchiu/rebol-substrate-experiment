# G6d — Native Windows x86-64 Port and Audio Experiment

Status: Windows build produced and exercised as a native Windows PE process;
physical Windows playtest remains for the user.

- Baseline: G6c `82448b8` (`glon-avp-g6c`)
- Branch/worktree: `glon-avp-g6d-win` at
  `/mnt/d/repos/glon-worktrees/glon-avp-g6d-win`
- Implementation commit: `c935fe6`
- Windows output: `dist/windows/doom.exe`
- Linux source remains buildable and tested

## 1. Host audit and feasibility

The game host already uses SDL2 for window, OpenGL context, keyboard/mouse,
audio device, callback, events and delay. Most of the host is portable. The
non-portable pieces were small and isolated:

- `#include <unistd.h>` and POSIX `clock_gettime()` (monotonic, realtime,
  process CPU time).
- `setenv()` and the Linux `open_memstream()` capture path.
- Assignment to `stdout` for in-memory capture (not assignable on MinGW).
- The default project root `../..` and `long`-based timestamps. MinGW `long`
  is 32-bit, so high-resolution timestamps overflowed after ~2 seconds.
- Linux link flags (`-lGL`, `-lpthread`) and SDL2 discovery via `pkg-config`.

No architectural rewrite was needed. The same `doom-host.c`, Glon runtime,
`doom.glon`, `objects.glon`, assets and game semantics compile for both
targets. The only platform-specific source is
`experiments/glon-doom/platform_win.h`, included conditionally on `_WIN32`.

## 2. Toolchain and dependencies

The environment had no MinGW compiler, Wine, or SDL2 Windows development
package. To avoid changing the system toolchain:

- downloaded a standalone **Zig 0.13.0** toolchain to
  `/home/graham/.cache/glon-g6d` (not `/tmp`; not system-wide),
- downloaded the **SDL2 2.30.9 MinGW x86-64 development package** into the
  worktree `.deps/` directory (ignored by git),
- used Zig's `cc` with target `x86_64-windows-gnu` and the supplied
  `libSDL2.dll.a`.

No large SDK was installed system-wide and no sudo/apt install was performed.
The Windows distribution does not require Zig or MinGW on the target machine.

Build command (also wrapped by `build-windows.sh`):

```sh
cd /mnt/d/repos/glon-worktrees/glon-avp-g6d-win/experiments/glon-doom
ZIG=/home/graham/.cache/glon-g6d/zig-linux-x86_64-0.13.0/zig
make windows \
  WIN_CC="$ZIG cc -target x86_64-windows-gnu" \
  WIN_SDL="../../.deps/SDL2-2.30.9/x86_64-w64-mingw32"
```

The final PE is:

```
dist/windows/doom.exe: PE32+ executable (console) x86-64, for MS Windows
size: 6,546,432 bytes
sha256: 51e3a44444a8a71bdfa2624397e9096655a2d60e7e8a6d3374c1411e49f21ef2
```

## 3. Windows implementation

Files added/changed:

- `experiments/glon-doom/platform_win.h` (new)
  - `clock_gettime` via `QueryPerformanceCounter`, `GetProcessTimes`, and
    `GetSystemTimeAsFileTime`;
  - `setenv` via `_putenv_s`;
  - `near`/`far` macro cleanup for Windows headers.
- `experiments/glon-doom/doom-host.c`
  - conditional POSIX includes;
  - Windows default `root_dir="."`;
  - Windows capture stream via `tmpfile()` and `_dup`/`_dup2` of stdout;
  - all high-resolution timestamp variables changed from `long` to `int64_t`;
  - PulseAudio latency environment setting only on non-Windows.
- `experiments/glon-doom/Makefile`
  - `windows` target and `build-win/doom.exe`;
  - SDL2 MinGW import library, `-lopengl32`, Windows system libraries.
- `experiments/glon-doom/build-windows.sh`
  - reproducible cross-build and distribution assembly.
- `experiments/glon-doom/tools/wasapi_latency.c`
  - Windows WASAPI period/buffer/stream-latency probe.
- `dist/windows/`
  - `doom.exe`, `SDL2.dll`, SDL2 licence, game `.glon` files, runtime assets,
    audio attribution files and `run-g6d.bat`.

Window/OpenGL/input/audio use SDL2's native Windows backends:
- OpenGL through WGL/OPENGL32;
- input through SDL2 Windows events/keyboard state;
- audio through SDL2's **WASAPI** driver;
- timing through the platform adapter.

No PulseAudio, Linux windowing library or WSLg audio bridge is used by the
Windows executable.

## 4. Windows distribution and launch

Distribution path in this worktree:

```
D:\repos\glon-worktrees\glon-avp-g6d-win\dist\windows
```

It contains 22 files, total **22,075,065 bytes**. Required non-system DLL:
`SDL2.dll` (shipped). System dependencies: `OPENGL32.dll` and Windows UCRT
`api-ms-win-crt-*` DLLs. No MinGW runtime DLL is imported.

Launch from Windows:

```bat
D:
cd D:\repos\glon-worktrees\glon-avp-g6d-win\dist\windows
run-g6d.bat
```

`run-g6d.bat` sets `DOOM_ROOT` to the distribution root, changes to that
directory and launches `doom.exe`. Controls: W/A/S/D, arrows, E door,
F/left mouse rifle preview, M mute, `[`/`]` volume, Esc quit.

`SDL2.dll`, SDL2 licence, Freesound attribution files and the Windows README
are included.

## 5. Verification performed

### Linux regression

The complete Linux build and test path was re-run after the portability edits:

- `make` succeeds.
- Full `make test` passes RC 0, including `--audio-test`, `--sfx-test`,
  `--audio-e2e`, texture/asset checks and frozen S1.
- Frozen S1:
  `f90496c26dc45c7a387d8fc8639cd2781507d3d2`.
- Linux executable: **6,203,168 bytes**,
  SHA-256 `f221e3475cfd1dc3454efe100a084ee03a23978c34d26d5803efe480f0895037`.

### Windows PE tests

The PE was run as an actual Windows process through WSL's Windows interop,
**not Wine**, with `DOOM_ROOT` pointing at the distribution. It used Windows
OpenGL and WASAPI:

- `--self-test`: PASSED; same loader/heap/determinism results.
- `--input-test`: PASSED (SDL key mapping).
- `--audio-test`: PASSED.
- `--sfx-test`: PASSED.
- `--audio-e2e`: PASSED (F input, E activation, wall and cargo ticks, PCM).
- `--audio-device-test`: opened **WASAPI**, 44100 Hz/2ch/256 samples.
- `--sfx-device-test`: six effects through WASAPI.
- `--portal-test`: PASSED, 7,018 changed pixels in the contained mask.
- `--gl-portal-shots` produced native Windows 1920x1080 portal frames.
- `--portal-smoke --smoke-frames 180`: native Windows graphical loop exited
  RC 0; a frame log reported 51.5 FPS at 640x400 and the correct portal pose.
- `--sfx-smoke --smoke-frames 240`: real SDL F/E/W events produced rifle,
  door-open, door-close, wall and cargo requests and callbacks.
- `run-g6d.bat --self-test`: PASSED, confirming the batch/root setup.

These results are automated/native-process checks, not a physical user
playtest. Physical Windows keyboard, mouse and speakers remain for the user.

## 6. Audio timing

Native Windows path:

- SDL2 audio driver: **wasapi**.
- Callback buffer: 256 frames; 44,100 Hz stereo.
- Request-to-callback traces: **0.66–9.29 ms** (one latency probe: 3.05 ms).
- WASAPI default device probe:
  - mix format 48,000 Hz / 2 channels,
  - default period **10.000 ms**, minimum period **3.000 ms**,
  - buffer **1,056 frames** (~22 ms at 48 kHz),
  - `GetStreamLatency` reported **0.000 ms**.
- The game's callback/mixer path is therefore small; the WASAPI shared-mode
  engine period is about 10 ms, not the ~632–649 ms reported by WSLg RDPSink.

This is the experiment: it removes the WSLg PulseAudio/RDP transport and uses
the native Windows audio engine. Acoustic latency has **not** been measured;
it requires the user's speakers/headphones and a physical playtest.

The Linux/PulseAudio path remains operational and unchanged; its RDPSink
measurements remain in the G6c evidence.

## 7. Limitations and unverified items

- Physical Windows launch, graphics composition, keyboard/mouse feel and
  speaker latency have not been tested by the agent. The automated native PE
  tests exercise initialization and event paths, not human input or acoustics.
- Wine was not installed; the PE was executed through Windows interop.
  Windows-interop execution is a native Windows process, but it is not a
  substitute for a desktop playtest.
- WASAPI shared-mode latency can vary with the selected Windows endpoint and
  audio enhancements. `GetStreamLatency` reported 0; the measured period and
  buffer are the useful figures.
- The distribution requires Windows UCRT (Windows 10/11) and keeps
  `SDL2.dll` beside the executable. No installer is provided.
- The Windows port does not add exclusive-mode WASAPI or a custom mixer.
  If the playtest still shows unacceptable latency, the next experiment should
  select a lower-latency Windows endpoint or add WASAPI exclusive mode.
- No portal, rifle, room, HUD or audio asset was redesigned. The rifle remains
  a presentation-only preview.
- The Windows binary is 6,546,432 bytes; the distribution is 22,075,065 bytes.
  The larger size includes SDL2.dll and duplicated runtime assets.

## 8. Recommended next step

Perform the native Windows playtest from the exact `run-g6d.bat` path above.
Focus on:

1. rifle input-to-sound delay;
2. door open/close synchronization;
3. continuous ambience;
4. portal animation and rendering;
5. physical input responsiveness and clean exit.

If those are acceptable, the WSLg RDPSink hypothesis is supported and the
Windows build is a viable playtest host. If not, capture the Windows default
audio endpoint, WASAPI shared/exclusive mode and an external acoustic
measurement before changing code.

## 9. Evidence

```
evidence/g6d-win/
  self-test.log / input-test.log / audio-test.log / sfx-test.log
  audio-e2e.log / audio-device-test.log / sfx-device-test.log
  audio-latency-trace.log
  sfx-smoke.log
  portal-test.log / portal-smoke.log / portal-shots.log
  portal/ (native Windows 1920x1080 animation frames)
  wasapi-latency.txt
  batch-self-test.log
  linux-test.log / linux-size.txt / linux-sha256.txt
  win-size.txt / win-sha256.txt / distribution-size.txt
  pe.txt / dlls.txt / windows-build.log
  metadata.txt
```

Implementation commit: `c935fe6`. This report and the project-memory updates
are committed separately.

No push, merge, tag or deploy.
