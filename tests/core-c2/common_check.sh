#!/bin/sh
# tests/core-c2/common_check.sh - GLON-CORE-C2 cross-profile behaviour
# (make profile-common-test).
#
# For every declared profile: build the minimal glon CLI, run
# tests/core-c2/common.glon and require output byte-identical to the
# constrained reference; then run tests/core-c2/limits.glon, which must print
# the correct deep-recursion sum where the main return stack allows it and
# otherwise stop with a clean machine fail-stop (never another value).
set -u
cd "$(dirname "$0")/../.."
MAKE=${MAKE:-make}
profiles=${PROFILES:-$(cd profiles && ls *.profile | sed 's/\.profile$//')}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/glon-common.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
fails=0
run_glon() { timeout 120 ./glon glon-lib/strings-ext.glon "$@" 2>&1 | grep -v '^#\[func\]$'; }

$MAKE --no-print-directory glon MODULES= MEM_PROFILE=constrained >/dev/null || { echo "build constrained failed"; exit 1; }
run_glon tests/core-c2/common.glon > "$tmp/reference"
grep -q '^done$' "$tmp/reference" || { echo "  FAIL: the constrained reference run did not complete"; cat "$tmp/reference"; exit 1; }
for p in $profiles; do
    if ! $MAKE --no-print-directory glon MODULES= MEM_PROFILE="$p" >"$tmp/build-$p" 2>&1; then
        echo "  FAIL: $p: build failed"; tail -3 "$tmp/build-$p"; fails=$((fails + 1)); continue
    fi
    run_glon tests/core-c2/common.glon > "$tmp/$p"
    if cmp -s "$tmp/reference" "$tmp/$p"; then
        echo "  ok: $p: common.glon output identical to constrained ($(wc -l < "$tmp/$p") lines)"
    else
        echo "  FAIL: $p: common.glon output differs"; diff "$tmp/reference" "$tmp/$p" | head -5; fails=$((fails + 1))
    fi
    lim=$(run_glon tests/core-c2/limits.glon)
    rs=$(sed -n 's/^#define R0S1_DS_INIT \([0-9]*\)L$/\1/p' "build/profile/$p/glon_profile.h" 2>/dev/null)
    rs=$((24576 - ${rs:-16384}))
    case "$lim" in
        "deep 2485") echo "  ok: $p: limits.glon = 2485 (main return stack $rs cells)" ;;
        *"machine-level fail-stop"*)
            if [ "$rs" -lt 8192 ]; then echo "  ok: $p: limits.glon stops cleanly (main return stack $rs cells)"
            else echo "  FAIL: $p: limits.glon fail-stop with a $rs-cell return stack"; fails=$((fails + 1)); fi ;;
        *) echo "  FAIL: $p: limits.glon gave: $lim"; fails=$((fails + 1)) ;;
    esac
done
$MAKE --no-print-directory glon MODULES= MEM_PROFILE=constrained >/dev/null
if [ "$fails" -eq 0 ]; then echo "profile-common-test: PASS"; else echo "profile-common-test: FAIL ($fails)"; exit 1; fi
