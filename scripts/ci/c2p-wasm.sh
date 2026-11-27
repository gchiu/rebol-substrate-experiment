#!/usr/bin/env bash
# scripts/ci/c2p-wasm.sh - Emscripten/Node smoke tests for the frozen core and
# the G1A shop host.  Requires emcc and node on PATH (the workflow sets up
# Emscripten; GitHub's Ubuntu runner already provides Node).
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

LOG_DIR="${CI_LOG_DIR:-ci-logs/c2p-wasm}"
STEP_TIMEOUT="${C2P_WASM_STEP_TIMEOUT:-900}"
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
    timeout "${STEP_TIMEOUT}s" "$@" 2>&1 | tee "$LOG_DIR/$name.log"
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

run_step frozen bash check-frozen-s1.sh
# Several generated WASM/HTML artifacts are committed (web/demo.js,
# standalone/glon.wasm, demo/shop/app.html).  A fresh checkout gives every
# file the same mtime, so make can treat a stale committed artifact as
# up-to-date.  Remove them so the Node smoke tests exercise the current source.
run_step clean-generated bash -c 'rm -f web/demo.js docs/r0-counter.html standalone/glon.wasm demo/shop/glon.wasm demo/shop/app.html primer.html'
run_step wasm-core make wasm-test
run_step wasm-standalone make wasm-standalone-test
run_step wasm-g1a make wasm-g1a-test
run_step wasm-binding make wasm-binding-test
run_step wasm-abi make wasm-abi-test
run_step wasm-primer make wasm-primer-test

cat "$SUMMARY"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    cat "$SUMMARY" >> "$GITHUB_STEP_SUMMARY"
fi
printf '\nC2P_WASM result steps=%s failures=%s\n' "$TOTAL" "$FAIL"
[ "$FAIL" -eq 0 ]
