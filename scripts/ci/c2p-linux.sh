#!/usr/bin/env bash
# scripts/ci/c2p-linux.sh - Linux native C2P regression suite.
#
# Verifies the unified C2 core and W0 server without running the unbounded
# `make test` (its NaSch simulator is handled by bounded-s1.sh).  Every step is
# bounded; logs and a summary are written to CI_LOG_DIR for upload on failure.
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

LOG_DIR="${CI_LOG_DIR:-ci-logs/c2p-linux}"
STEP_TIMEOUT="${C2P_STEP_TIMEOUT:-900}"
mkdir -p "$LOG_DIR"
SUMMARY="$LOG_DIR/summary.md"
FAIL=0
TOTAL=0
: > "$SUMMARY"
printf '| Step | Result | Log |\n|---|---|---|\n' >> "$SUMMARY"

run_step_with_limit() {
    name="$1"; limit="$2"; shift 2
    TOTAL=$((TOTAL + 1))
    echo "::group::$name"
    timeout "${limit}s" "$@" 2>&1 | tee "$LOG_DIR/$name.log"
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

run_step() {
    run_step_with_limit "$1" "$STEP_TIMEOUT" "${@:2}"
}

# Frozen substrate and C2 profile/layout invariants.
run_step frozen bash check-frozen-s1.sh
run_step arch-build make arch-check
run_step arch-run ./arch-check
run_step profile-check make profile-check
run_step profile-reject-check make profile-reject-check
run_step profile-common-test make profile-common-test
run_step build-deps-check make build-deps-check
run_step global-ctx-hash make global-ctx-hash-test
run_step m1-large make m1-large-test MEM_PROFILE=large

# JSON / HTTPS / Glon Worlds regressions.
run_step json make json-test
run_step json-j1 make json-j1-test
run_step json-j2 make json-j2-test
run_step h3e make h3e-test
run_step h3c make h3c-test
run_step g3h make g3h-test
run_step json-j2-g3h make json-j2-g3h-test
run_step w0-routes make w0-routes-test
run_step https-g3a make https-g3a-test
run_step sqlite make sqlite-test
run_step sec make sec-test

# The only native S1 run in CI; stops at the documented NaSch checkpoint.
run_step_with_limit bounded-s1 1000 bash scripts/ci/bounded-s1.sh

cat "$SUMMARY"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    cat "$SUMMARY" >> "$GITHUB_STEP_SUMMARY"
fi
printf '\nC2P_LINUX result steps=%s failures=%s\n' "$TOTAL" "$FAIL"
[ "$FAIL" -eq 0 ]
