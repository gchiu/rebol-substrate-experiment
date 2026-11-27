#!/usr/bin/env bash
# Build the provisional Android ARM64 (Termux/Bionic) D1 package.
# Requires ANDROID_NDK_HOME (or an NDK path as the first argument).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
NDK="${1:-${ANDROID_NDK_HOME:-}}"
if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
    echo "Android NDK not found (set ANDROID_NDK_HOME)" >&2
    exit 2
fi
TOOL="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
CC="$TOOL/aarch64-linux-android24-clang"
if [ ! -x "$CC" ]; then
    echo "missing $CC" >&2
    exit 2
fi
API=24
CFLAGS_COMMON="-std=c17 -fwrapv -O2 -fPIE -pie -I."
OUT="$ROOT/build/android-d1"
rm -rf "$OUT"
mkdir -p "$OUT"

# Minimal and large file-enabled Glon, then the focused M1 smoke binary.
# m1-smoke covers the verified M1 test body without repository source fixtures.
make --no-print-directory glon MODULES= CC="$CC" CFLAGS="$CFLAGS_COMMON"
cp glon "$OUT/glon-minimal"
make --no-print-directory glon MODULES=file MEM_PROFILE=server CC="$CC" CFLAGS="$CFLAGS_COMMON"
cp glon "$OUT/glon"
"$CC" $CFLAGS_COMMON -I. -o "$OUT/m1-smoke" tests/m1_smoke.c r0_s1_m1_tests.c r0_s1_runtime.c r0_s1_show.c s1.c -pthread

file "$OUT/glon" "$OUT/glon-minimal" "$OUT/m1-smoke"
"$TOOL/llvm-readelf" -h "$OUT/glon" | grep -E 'Machine|Class' || true

VERSION="d1-$(git rev-parse --short=8 HEAD)"
COMMIT="$(git rev-parse HEAD)"
python3 scripts/ci/package.py \
    --platform android --arch arm64 --variant full \
    --version "$VERSION" --commit "$COMMIT" \
    --compiler "Android NDK clang (API $API)" \
    --glon "$OUT/glon" --out "$OUT/dist" \
    --archive-name glon-test-android-arm64-termux.tar.gz \
    --extra-file m1-smoke="$OUT/m1-smoke" \
    --extra-file glon-minimal="$OUT/glon-minimal" \
    --extra-file GLON-PRIMER.md="$ROOT/GLON-PRIMER.md" \
    --extra-dir demo/shop="$ROOT/demo/shop" \
    --extra-dir tests/json="$ROOT/tests/json"

cp "$OUT/dist/glon-test-android-arm64-termux.tar.gz" "$ROOT/dist/" 2>/dev/null || true
mkdir -p "$ROOT/dist"
cp "$OUT/dist/glon-test-android-arm64-termux.tar.gz" "$ROOT/dist/"
(cd "$ROOT/dist" && sha256sum glon-test-android-arm64-termux.tar.gz > glon-test-android-arm64-termux.tar.gz.sha256)
ls -lh "$ROOT/dist/glon-test-android-arm64-termux.tar.gz"*
