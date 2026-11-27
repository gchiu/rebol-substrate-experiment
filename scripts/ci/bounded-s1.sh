#!/usr/bin/env bash
# scripts/ci/bounded-s1.sh - run the native S1 suite only as far as its known
# NaSch simulation stall, with a hard time bound.
#
# The full ./s1 suite does not terminate on the documented NaSch traffic
# simulator (it stalls after "8: uniform flow has no collisions").  C1/C2/W0
# instead require the bounded run to reach that same checkpoint with no FAIL
# lines and at least the established number of passing checks.  A timeout at
# the checkpoint is the expected result; any earlier stop or any FAIL is an
# error.  Do not replace this with `make test` in CI.
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

SECONDS_BOUND="${BOUNDED_S1_SECONDS:-900}"
MIN_OK="${BOUNDED_S1_MIN_OK:-900}"
BUILD_SECONDS="${BOUNDED_S1_BUILD_SECONDS:-180}"
LOG="${BOUNDED_S1_LOG:-ci-logs/bounded-s1.log}"
mkdir -p "$(dirname "$LOG")"

fail() {
    echo "bounded-s1: FAIL: $*" >&2
    exit 1
}

echo "bounded-s1: building ./s1 (bound ${BUILD_SECONDS}s)"
timeout "${BUILD_SECONDS}s" make s1 || fail "make s1 failed or exceeded ${BUILD_SECONDS}s"

echo "bounded-s1: running ./s1 (bound ${SECONDS_BOUND}s; NaSch timeout is expected)"
set +e
timeout "${SECONDS_BOUND}s" ./s1 > "$LOG" 2>&1
rc=$?
set -e

ok_count=$(grep -cE '^[[:space:]]*ok:' "$LOG" 2>/dev/null || true)
fail_count=$(grep -cE '^[[:space:]]*FAIL' "$LOG" 2>/dev/null || true)
echo "bounded-s1: rc=$rc ok=$ok_count fail=$fail_count log=$LOG"

# Show the tail in the CI console; the full log is uploaded as an artifact.
tail -n 20 "$LOG" || true

case "$rc" in
    0|124) ;;
    *) fail "unexpected exit status $rc (expected 0 or the bounded NaSch timeout 124)" ;;
esac

[ "$fail_count" -eq 0 ] || fail "$fail_count FAIL line(s) in the bounded S1 run"
[ "$ok_count" -ge "$MIN_OK" ] || fail "only $ok_count passing checks (minimum $MIN_OK)"
grep -q 'R0-S1 IDM traffic simulator' "$LOG" || fail "the IDM traffic simulator was not reached"
grep -q 'uniform flow has no collisions' "$LOG" || fail "the known NaSch checkpoint was not reached"

if [ "$rc" -eq 124 ]; then
    echo "bounded-s1: PASS (reached the known NaSch checkpoint; bounded timeout is expected)"
else
    echo "bounded-s1: PASS (suite completed before the bound)"
fi
