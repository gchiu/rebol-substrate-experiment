#!/usr/bin/env bash
# Build and package the relocatable native distributions for one CI platform.
# Runs only after the native verification job succeeds.  Uploads Actions
# artifacts; it never creates or changes a GitHub Release.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
PLATFORM="${CI_PLATFORM:?}"
ARCH="${CI_ARCH:?}"
COMMIT="$(git rev-parse HEAD)"
SHORT="$(git rev-parse --short=8 HEAD)"
VERSION="${GLON_CI_VERSION:-v0.1.0-alpha.2-ci.$SHORT}"
DIST="$ROOT/dist"
rm -rf "$DIST"
mkdir -p "$DIST"
BUNDLE="$ROOT/bundle-libs"

glon_bin() {
    if [ -f ./glon.exe ]; then echo ./glon.exe; elif [ -x ./glon ]; then echo ./glon; else echo ./glon; fi
}

bundle_macos() {
    sslpath="$(otool -L ./glon | awk '/libssl.*\.dylib/ {print $1; exit}')"
    cryptopath="$(otool -L ./glon | awk '/libcrypto.*\.dylib/ {print $1; exit}')"
    if [ -z "$sslpath" ] || [ -z "$cryptopath" ]; then
        echo "package: cannot find OpenSSL dylib links in glon" >&2
        otool -L ./glon >&2
        exit 1
    fi
    rm -rf "$BUNDLE"; mkdir -p "$BUNDLE"
    cp "$sslpath" "$BUNDLE/libssl.3.dylib"
    cp "$cryptopath" "$BUNDLE/libcrypto.3.dylib"
    install_name_tool -change "$sslpath" @executable_path/lib/libssl.3.dylib ./glon
    install_name_tool -change "$cryptopath" @executable_path/lib/libcrypto.3.dylib ./glon
    install_name_tool -id @executable_path/lib/libssl.3.dylib "$BUNDLE/libssl.3.dylib"
    install_name_tool -change "$cryptopath" @executable_path/lib/libcrypto.3.dylib "$BUNDLE/libssl.3.dylib"
    install_name_tool -id @executable_path/lib/libcrypto.3.dylib "$BUNDLE/libcrypto.3.dylib"
}

bundle_windows() {
    rm -rf "$BUNDLE"; mkdir -p "$BUNDLE"
    ldd "$(glon_bin)" | awk '/mingw64/ {print $3}' | while IFS= read -r dll; do
        [ -f "$dll" ] && cp "$dll" "$BUNDLE/"
    done
}

package_variant() {
    variant="$1"; shift
    make glon "$@"
    extra=()
    if [ "$variant" = full ]; then
        make m1-smoke
        if [ -f ./m1-smoke.exe ]; then
            extra+=(--extra-file "m1-smoke.exe=./m1-smoke.exe")
        elif [ -x ./m1-smoke ]; then
            extra+=(--extra-file "m1-smoke=./m1-smoke")
        fi
    fi
    libarg=()
    if [ "$PLATFORM" = macos ] && [ "$variant" = full ]; then
        bundle_macos
        libarg=(--lib-dir "$BUNDLE")
    elif [ "$PLATFORM" = windows ]; then
        bundle_windows
        libarg=(--lib-dir "$BUNDLE")
    fi
    compiler="$("${CC:-cc}" --version 2>/dev/null | head -n 1 || true)"
    python3 scripts/ci/package.py \
        --platform "$PLATFORM" --arch "$ARCH" --variant "$variant" \
        --version "$VERSION" --commit "$COMMIT" --compiler "$compiler" \
        --glon "$(glon_bin)" --out "$DIST" ${libarg[@]+"${libarg[@]}"} ${extra[@]+"${extra[@]}"} \
        --summary "$DIST/summary-$variant.md"
    if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
        sp="$GITHUB_STEP_SUMMARY"
        if command -v cygpath >/dev/null 2>&1; then sp="$(cygpath -u "$sp")"; fi
        cat "$DIST/summary-$variant.md" >> "$sp"
    fi
}

smoke_archive() {
    archive="$1"; stage="$2"
    rm -rf "$stage"; mkdir -p "$stage"
    case "$archive" in
        *.zip) (cd "$stage" && unzip -q "$archive") ;;
        *)     tar -xzf "$archive" -C "$stage" ;;
    esac
    dir="$(find "$stage" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
    if [ -z "$dir" ]; then echo "package smoke: no top-level directory" >&2; exit 1; fi
    if [ "$PLATFORM" = windows ]; then
        python3 scripts/ci/check_windows_deps.py --exe "$dir/glon.exe" --dir "$dir" \
            --objdump "$(command -v objdump)"
        if ! (cd "$dir" && ./glon.exe glon-lib/strings-ext.glon examples/conformance.glon > conformance.out 2>&1); then
            echo "PACKAGE_CONFORMANCE_EXEC_FAILED $archive" >&2
            cat "$dir/conformance.out" >&2
            ls -l "$dir" "$dir/glon-lib" >&2 || true
            exit 2
        fi
    else
        if ! (cd "$dir" && ./glon glon-lib/strings-ext.glon examples/conformance.glon > conformance.out 2>&1); then
            echo "PACKAGE_CONFORMANCE_EXEC_FAILED $archive" >&2
            cat "$dir/conformance.out" >&2
            ls -l "$dir" "$dir/glon-lib" >&2 || true
            exit 2
        fi
    fi
    if ! grep -q 'CONFORMANCE PASS' "$dir/conformance.out"; then
        echo "PACKAGE_CONFORMANCE_FAILED $archive" >&2
        cat "$dir/conformance.out" >&2
        ls -l "$dir" "$dir/glon-lib" >&2 || true
        exit 2
    fi
    case "$archive" in
        *-full.tar.gz|*-full.zip)
            if [ "$PLATFORM" = windows ]; then
                (cd "$dir" && ./glon.exe examples/hello-d1.glon && \
                 ./glon.exe glon-lib/strings-ext.glon modules/json.glon examples/json-smoke.glon && \
                 ./glon.exe --io-root . --allow-file-read --allow-file-write examples/file-smoke.glon && \
                 ./m1-smoke.exe)
            else
                (cd "$dir" && ./d1-smoke.sh)
            fi
            ;;
    esac
    echo "PACKAGE_SMOKE_PASS $archive"
}

case "$PLATFORM" in
    linux)
        package_variant minimal MODULES=
        package_variant full MODULES=file,http MEM_PROFILE=large MEM_LOADER_CELLS=500000
        ;;
    macos)
        export CFLAGS="${CFLAGS:--std=c17 -Wall -Wextra -O0 -g} -Wl,-headerpad_max_install_names"
        package_variant minimal MODULES=
        package_variant full MODULES=file,http MEM_PROFILE=large MEM_LOADER_CELLS=500000
        ;;
    windows)
        package_variant minimal MODULES=
        package_variant full MODULES=file,http MEM_PROFILE=large MEM_LOADER_CELLS=500000
        ;;
    *) echo "unknown CI_PLATFORM=$PLATFORM" >&2; exit 2 ;;
esac

for a in "$DIST"/*.tar.gz "$DIST"/*.zip; do
    [ -e "$a" ] || continue
    smoke_archive "$a" "$DIST/smoke-$(basename "$a")"
done

if command -v sha256sum >/dev/null 2>&1; then
    hashcmd=(sha256sum)
else
    hashcmd=(shasum -a 256)
fi
: > "$DIST/SHA256SUMS"
find "$DIST" -maxdepth 1 -type f \( -name '*.tar.gz' -o -name '*.zip' \) -print | sort | while IFS= read -r f; do
    "${hashcmd[@]}" "$f" >> "$DIST/SHA256SUMS"
done
if [ "${GLON_D1:-0}" = "1" ]; then
    full="$(find "$DIST" -maxdepth 1 -type f \( -name '*-full.tar.gz' -o -name '*-full.zip' \) | sort | head -n 1)"
    case "$full" in
        *.tar.gz) ext=tar.gz ;;
        *.zip) ext=zip ;;
        *) echo "D1: no full archive produced" >&2; exit 1 ;;
    esac
    d1="$DIST/glon-test-$PLATFORM-$ARCH.$ext"
    cp "$full" "$d1"
    "${hashcmd[@]}" "$d1" > "$d1.sha256"
    echo "D1_ARTIFACT=$d1"
fi
cat "$DIST"/summary-*.md
