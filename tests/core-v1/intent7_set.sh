#!/bin/sh
# tests/core-v1/intent7_set.sh - GLON-VIEW-CORE-V1: INTENT-7's hashed
# set-word search on C2's fixed-radix hash encoding (make intent7-set-check).
# intent7_set.glon in each profile (radix 256 / 4,096 / 16,384) must give the
# same values as the linear search (-DR0S1_SET_LINEAR) and as recorded.
set -u
cd "$(dirname "$0")/../.."
MAKE=${MAKE:-make}
WANT='22 15 2 106 21 -6'
status=0
for p in "$@"; do
    $MAKE --no-print-directory glon MODULES= MEM_PROFILE="$p" >/dev/null 2>&1 || { echo "  FAIL: $p build"; status=1; continue; }
    got=$(./glon tests/core-v1/intent7_set.glon 2>&1 | tail -n 1)
    if [ "$got" = "$WANT" ]; then echo "  ok: $p hashed set: $got"; else echo "  FAIL: $p hashed set: '$got' (want $WANT)"; status=1; fi
    inc=""; s1=s1.c
    [ "$p" = constrained ] || { inc="-include build/profile/$p/glon_profile.h"; s1=build/profile/$p/s1_profile.c; }
    mkdir -p build/core-v1
    ${CC:-cc} -std=c17 -fwrapv -O0 -w -DR0S1_SET_LINEAR $inc -I. -o build/core-v1/glon-setlin-$p glon.c r0_s1_session.c r0_s1_show.c r0_s1_runtime.c $s1 || { echo "  FAIL: $p linear build"; status=1; continue; }
    # the linear build reads glon-lib/ beside itself
    ln -sfn ../../glon-lib build/core-v1/glon-lib
    lin=$(build/core-v1/glon-setlin-$p tests/core-v1/intent7_set.glon 2>&1 | tail -n 1)
    if [ "$lin" = "$got" ]; then echo "  ok: $p linear set (-DR0S1_SET_LINEAR) agrees: $lin"; else echo "  FAIL: $p linear set '$lin' differs"; status=1; fi
done
$MAKE --no-print-directory glon MODULES= >/dev/null 2>&1
[ $status -eq 0 ] && echo "intent7-set-check: PASS ($*)" || echo "intent7-set-check: FAIL"
exit $status
