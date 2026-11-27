#!/bin/sh
# tests/core-v1/literal_block_gc.sh - GLON-VIEW-CORE-V1: the literal-block GC
# hazard as an explicit regression test (make literal-block-gc-check).
#
# For each profile (built as ./glon MODULES= MEM_PROFILE=<p>):
#   managed_block_gc.glon must read back "glon" 1 (the control);
#   literal_block_gc.glon is the KNOWN, UNRESOLVED defect: it must read back
#   exactly the recorded wrong value "xxxxxxxxyyyyyyyy" 1.  A corrected
#   runtime reads "glon" 1 (or refuses the store): reported as CORRECTED, which
#   also exits non-zero so that the documentation is updated with the fix.
# Any other outcome (a crash, a different wrong value) is a FAIL.
set -u
cd "$(dirname "$0")/../.."
MAKE=${MAKE:-make}
DEFECT='"xxxxxxxxyyyyyyyy" 1'
FIXED='"glon" 1'
status=0
for p in "$@"; do
    $MAKE --no-print-directory glon MODULES= MEM_PROFILE="$p" >/dev/null 2>&1 || { echo "  FAIL: $p: build"; status=1; continue; }
    ctl=$(./glon tests/core-v1/managed_block_gc.glon 2>/dev/null | tail -n 1)
    lit=$(./glon tests/core-v1/literal_block_gc.glon 2>/dev/null | tail -n 1)
    if [ "$ctl" = "$FIXED" ]; then echo "  ok: $p: control (managed block) survives: $ctl"
    else echo "  FAIL: $p: control read '$ctl', expected $FIXED"; status=1; fi
    if [ "$lit" = "$DEFECT" ]; then echo "  known defect: $p: literal block reads $lit (a raw masm store: unchecked by definition; the shared block-set! refuses it, make literal-store-check)"
    elif [ "$lit" = "$FIXED" ]; then echo "  CORRECTED: $p: literal block reads $lit -- update GLON-VIEW-CORE-V1.md and this test"; status=2
    else echo "  FAIL: $p: literal block read '$lit' (neither the recorded defect nor the fix)"; status=1; fi
done
$MAKE --no-print-directory glon MODULES= >/dev/null 2>&1
case $status in
    0) echo "literal-block-gc-check: PASS (reproduced exactly through a raw masm store; the collector is unchanged)" ;;
    2) echo "literal-block-gc-check: CHANGED (the defect no longer reproduces)" ;;
    *) echo "literal-block-gc-check: FAIL" ;;
esac
exit $status
