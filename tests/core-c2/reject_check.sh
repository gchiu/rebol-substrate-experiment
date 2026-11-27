#!/bin/sh
# tests/core-c2/reject_check.sh - GLON-CORE-C2: invalid memory layouts are
# rejected deterministically (make profile-reject-check).
#
# Each tests/core-c2/reject/*.profile is invalid in one way and names the
# expected diagnostic in its "# EXPECT:" line.  The generator must refuse it,
# or, if the generator accepts it, compiling the runtime against the generated
# header must fail with that diagnostic.  Two further cases bypass the
# generator with hand-written -D flags (the layout check must still catch
# them): D1's Kaka loader override and the removed reclaimed-arena switch.
set -u
CC=${CC:-cc}
cd "$(dirname "$0")/../.."
tmp=$(mktemp -d "${TMPDIR:-/tmp}/glon-reject.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
fails=0
pass() { echo "  ok: $1"; }
fail() { echo "  FAIL: $1"; fails=$((fails + 1)); }

for pf in tests/core-c2/reject/*.profile; do
    name=$(basename "$pf" .profile)
    want=$(sed -n 's/^# EXPECT: //p' "$pf")
    out="$tmp/$name"
    if sh scripts/glon-profile.sh "$pf" "$out" 2>"$tmp/$name.gen"; then
        if $CC -std=c17 -fsyntax-only -I. -include "$out/glon_profile.h" r0_s1_runtime.c 2>"$tmp/$name.cc"; then
            fail "$name: accepted (expected: $want)"
        elif grep -q "$want" "$tmp/$name.cc"; then
            pass "$name: rejected at compile time ($want)"
        else
            fail "$name: rejected for another reason: $(grep -m1 error "$tmp/$name.cc")"
        fi
    elif grep -q "$want" "$tmp/$name.gen"; then
        pass "$name: rejected by the generator ($want)"
    else
        fail "$name: generator failed for another reason: $(cat "$tmp/$name.gen")"
    fi
done

flags_case() {  # flags_case LABEL EXPECT FLAGS...
    label=$1; want=$2; shift 2
    if $CC -std=c17 -fsyntax-only -I. "$@" r0_s1_runtime.c 2>"$tmp/flags.cc"; then
        fail "$label: accepted (expected: $want)"
    elif grep -q "$want" "$tmp/flags.cc"; then
        pass "$label: rejected ($want)"
    else
        fail "$label: rejected for another reason: $(grep -m1 error "$tmp/flags.cc")"
    fi
}
flags_case "D1 Kaka override -DR0S1_HEAP_LIMIT=65536" \
    "overlaps the M1 task arena or task table" -DR0S1_HEAP_LIMIT=65536
flags_case "D1 reclaim override -DR0S1_HEAP_LIMIT=65000" \
    "overlaps the M1 task arena or task table" -DR0S1_HEAP_LIMIT=65000
flags_case "removed -DR0S1_ARENA_RECLAIMED" \
    "R0S1_ARENA_RECLAIMED is not supported" -DR0S1_ARENA_RECLAIMED=1 -DR0S1_HEAP_LIMIT=65000

if [ "$fails" -eq 0 ]; then echo "profile-reject-check: PASS"; else echo "profile-reject-check: FAIL ($fails)"; exit 1; fi
