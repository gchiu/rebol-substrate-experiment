#!/bin/sh
# Cross-build the Windows x86-64 host.  Requires a Zig 0.13 toolchain and the
# SDL2 2.30.9 MinGW development package; neither is installed system-wide.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
ZIG=${ZIG:-/home/graham/.cache/glon-g6d/zig-linux-x86_64-0.13.0/zig}
SDL_MINGW=${SDL2_MINGW:-$ROOT/.deps/SDL2-2.30.9/x86_64-w64-mingw32}
[ -x "$ZIG" ] || { echo "missing Zig: $ZIG" >&2; exit 1; }
[ -f "$SDL_MINGW/lib/libSDL2.dll.a" ] || { echo "missing SDL2 MinGW libs: $SDL_MINGW" >&2; exit 1; }
make -C "$HERE" windows \
    WIN_CC="$ZIG cc -target x86_64-windows-gnu" \
    WIN_SDL="$SDL_MINGW"
DIST="$ROOT/dist/windows"
rm -f "$DIST/run-g6d.bat" "$DIST/run-g6e.bat"
mkdir -p "$DIST/experiments/glon-doom/assets/doors" \
         "$DIST/experiments/glon-doom/assets/audio/sfx" "$DIST/jupyter"
cp "$HERE/build-win/doom.exe" "$DIST/doom.exe"
cp "$SDL_MINGW/bin/SDL2.dll" "$DIST/SDL2.dll"
SDL_ROOT=$(CDPATH= cd -- "$SDL_MINGW/.." && pwd)
cp "$SDL_ROOT/LICENSE.txt" "$DIST/SDL2-LICENSE.txt"
cp "$ROOT/jupyter/prelude.glon" "$DIST/jupyter/prelude.glon"
cp "$HERE"/doom.glon "$HERE"/objects.glon "$HERE"/raw-wall.glon "$DIST/experiments/glon-doom/"
cp "$HERE"/assets/doors/*.rgb "$DIST/experiments/glon-doom/assets/doors/"
cp "$HERE"/assets/audio/ambient_loop.wav "$DIST/experiments/glon-doom/assets/audio/"
cp "$HERE"/assets/audio/sfx/*.wav "$DIST/experiments/glon-doom/assets/audio/sfx/"
cp "$HERE"/assets/audio/SOURCE.txt "$DIST/assets-audio-SOURCE.txt"
cp "$HERE"/assets/audio/sfx/SOURCE.txt "$DIST/assets-audio-sfx-SOURCE.txt"
cp "$HERE"/WINDOWS-README.txt "$DIST/README-WINDOWS.txt"
cp "$HERE"/run-g6f.bat "$DIST/run-g6f.bat"
echo "Windows distribution: $DIST"
