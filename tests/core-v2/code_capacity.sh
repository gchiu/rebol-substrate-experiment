#!/bin/sh
# tests/core-v2/code_capacity.sh - GLON-VIEW-CORE-V3 capacity: executable-code
# region and main return stack per memory profile (make code-capacity-check).
# For each profile: the code limit and main return-stack size the profile
# declares (CODE_CAP = R0S1_DS_INIT), the deepest `lim/sum n` recursion (one
# closure activation per level) that returns n(n+1)/2, and a clean machine
# fail-stop one level deeper.  Desktop profiles must have the V3 code region
# (code limit 17,152) and keep at least 70 levels (C2's limits.glon floor).
set -u
cd "$(dirname "$0")/../.."
MAKE=${MAKE:-make}
status=0
tmp=build/core-v2; mkdir -p $tmp
probe() { printf 'lim/sum: func [n] [ either = n 0 [ 0 ] [ + n lim/sum - n 1 ] ]\nvalues [ lim/sum %d ]\n' $1 > $tmp/depth.glon
          ./glon $tmp/depth.glon 2>&1 | tr -d '\r' | tail -n 1; }
for p in $(cd profiles && ls *.profile | sed 's/\.profile$//'); do
    $MAKE --no-print-directory glon MODULES= MEM_PROFILE=$p >/dev/null 2>&1 || { echo "  FAIL: $p build"; status=1; continue; }
    cap=$(sed -n 's/^CODE_CAP=//p' profiles/$p.profile); rs=$((24576 - cap)); limit=$((cap - 256))
    lo=1; hi=300
    while [ $lo -lt $hi ]; do mid=$(( (lo + hi + 1) / 2 )); if [ "$(probe $mid)" = "$(( mid * (mid + 1) / 2 ))" ]; then lo=$mid; else hi=$((mid - 1)); fi; done
    beyond=$(probe $((lo + 1)))
    case "$beyond" in *"machine-level fail-stop"*) clean=clean ;; *) clean="NOT CLEAN: $beyond" ;; esac
    line="$p: code limit $limit ($((limit - 256)) cells), main return stack $rs cells, recursion limit $lo, beyond it: $clean"
    bad=""
    [ "$clean" = clean ] || bad="$bad unclean-overflow"
    case $p in desktop-*) [ $limit -eq 17152 ] || bad="$bad code-limit"; [ $lo -ge 70 ] || bad="$bad depth<70" ;; esac
    if [ -z "$bad" ]; then echo "  ok: $line"; else echo "  FAIL:$bad: $line"; status=1; fi
done
$MAKE --no-print-directory glon MODULES= >/dev/null 2>&1
[ $status -eq 0 ] && echo "code-capacity-check: PASS" || echo "code-capacity-check: FAIL"
exit $status
