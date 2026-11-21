#!/usr/bin/env bash
# scripts/ci/native-suite.sh - the native verification suite for one runner.
#
# The workflow supplies CI_PLATFORM (linux|macos|windows) and, on Windows,
# CFLAGS with the small CRT name mappings used by the POSIX-oriented test
# harness.  This script never changes Glon semantics: it builds the same
# configurations and runs the same tests on every supported host.
#
# Exit status is 0 only when every step applicable to the host passes.  All
# steps run even after a failure so one platform produces a complete diagnosis.

set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

PLATFORM="${CI_PLATFORM:-$(uname -s | tr '[:upper:]' '[:lower:]')}"
case "$PLATFORM" in
    linux) ;;
    macos) ;;
    windows) ;;
    *) echo "unknown CI_PLATFORM=$PLATFORM" >&2; exit 2 ;;
esac
SUPPORT_FILE="${CI_SUPPORT_FILE:-1}"
if [ "$PLATFORM" = windows ]; then
    SUPPORT_FILE=1
    # The native CRT maps /tmp/... to <current-drive>:\tmp.  The test harness
    # uses that path for stderr-capture files; create it before the suite.
    mkdir -p /d/tmp 2>/dev/null || true
    mkdir -p /c/tmp 2>/dev/null || true
fi
LOG_DIR="${CI_LOG_DIR:-ci-logs}"
mkdir -p "$LOG_DIR"
SUMMARY="$LOG_DIR/summary.md"
FAIL=0
TOTAL=0
: > "$SUMMARY"
printf '| Step | Result | Log |\n|---|---|---|\n' >> "$SUMMARY"

run_step() {
    name="$1"; shift
    TOTAL=$((TOTAL + 1))
    echo "::group::$name"
    ( "$@" ) 2>&1 | tee "$LOG_DIR/$name.log"
    rc=${PIPESTATUS[0]}
    echo "::endgroup::"
    if [ "$rc" -eq 0 ]; then
        echo "PASS $name"
        printf '| %s | PASS | `%s/%s.log` |\n' "$name" "$LOG_DIR" "$name" >> "$SUMMARY"
    else
        echo "FAIL $name (exit $rc)"
        FAIL=$((FAIL + 1))
        printf '| %s | **FAIL** (exit %s) | `%s/%s.log` |\n' "$name" "$rc" "$LOG_DIR" "$name" >> "$SUMMARY"
    fi
}

run_suite() {
    name="$1"; shift
    run_step "$name" "$@"
}

glon_bin() {
    if [ -f ./glon.exe ]; then echo ./glon.exe; elif [ -x ./glon ]; then echo ./glon; else echo ./glon; fi
}

conformance() {
    out="$("$(glon_bin)" glon-lib/strings-ext.glon ci/conformance.glon | grep -v '^#\[func\]$')"
    printf '%s\n' "$out"
    printf '%s\n' "$out" | grep -qx 'C15=576460752303423487' || return 1
    printf '%s\n' "$out" | grep -qx 'C16=-576460752303423488' || return 1
}

https_windows() {
    make glon MODULES=file,http
    bash tests/https-g1/make-cert.sh
    python3 tests/https-g1/acceptance.py --glon "$(glon_bin)"
    python3 tests/https-g2/acceptance.py --glon "$(glon_bin)"
    make glon MODULES=http GLON_HTTP_RECLAIM_M1=1
    python3 tests/https-g3/acceptance.py --glon "$(glon_bin)"
    make glon MODULES=file,http MEM_PROFILE=large MEM_LOADER_CELLS=500000
    python3 tests/https-g3a/acceptance.py --glon "$(glon_bin)"
}

https_posix() {
    make https-g3-test
}

sizes() {
    out="$LOG_DIR/sizes.txt"
    : > "$out"
    record_size() {
        label="$1"; shift
        make glon "$@"
        bin="$(glon_bin)"
        cp "$bin" "$LOG_DIR/glon-$label"
        bytes=$(wc -c < "$bin" | tr -d ' ')
        printf '%s %s\n' "$label" "$bytes" | tee -a "$out"
    }
    record_size minimal MODULES=
    record_size file MODULES=file
    record_size combined MODULES=file,http
    record_size large MODULES=file,http MEM_PROFILE=large MEM_LOADER_CELLS=500000
}

# ---- frozen substrate and the complete native regression -------------------
run_step frozen bash check-frozen-s1.sh
run_step core make test
run_step arch-build make arch-check
run_step arch-run ./arch-check
run_step json make json-test
run_step iterators make iterators-test
run_step m1-large make m1-large-test MEM_PROFILE=large

# ---- minimal executable, conformance probe, out-of-tree smoke -------------
run_step build-minimal make glon MODULES=
run_step conformance conformance
if [ "$PLATFORM" != windows ]; then
    run_step smoke bash glon-smoke.sh
fi

# ---- optional file module --------------------------------------------------
if [ "$SUPPORT_FILE" = 1 ]; then
    if [ "$PLATFORM" = windows ]; then
        run_step file-acceptance make io-file-windows-test
    else
        run_step file-c make io-file-c-test
        run_step file-acceptance make io-file-test
        run_step file-inplace make io-file-inplace-test
    fi
fi

# ---- HTTPS G1/G2/G3 (and G3A where the file module is supported) ----------
if [ "$PLATFORM" = windows ]; then
    run_step https-g1g2g3 https_windows
else
    run_step https-g1g2g3 https_posix
    run_step https-g3a make https-g3a-test
fi

# ---- explicit build matrix and sizes --------------------------------------
run_step sizes sizes

# ---- per-platform summary --------------------------------------------------
cat "$SUMMARY"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    summary_path="$GITHUB_STEP_SUMMARY"
    if command -v cygpath >/dev/null 2>&1; then
        summary_path="$(cygpath -u "$summary_path")"
    fi
    cat "$SUMMARY" >> "$summary_path"
fi
printf '\nRESULT platform=%s arch=%s steps=%s failures=%s\n' \
    "$PLATFORM" "${CI_ARCH:-unknown}" "$TOTAL" "$FAIL"
[ "$FAIL" -eq 0 ]
