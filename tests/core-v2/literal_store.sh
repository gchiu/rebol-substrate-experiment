#!/bin/sh
# tests/core-v2/literal_store.sh - GLON-VIEW-CORE-V2 literal safety of the
# shared block-set! (make literal-store-check), in each profile given:
#   literal_store.glon            the store matrix (managed / literal targets,
#                                 managed / immediate / permanent / closure values)
#   literal_block_gc_shared.glon  the GLON-LANG-1 hazard program with the shared
#                                 word: the store is refused, the literal intact
# (The raw masm store stays unchecked by definition: make literal-block-gc-check.)
set -u
cd "$(dirname "$0")/../.."
MAKE=${MAKE:-make}
status=0
for p in "$@"; do
    $MAKE --no-print-directory glon MODULES= MEM_PROFILE="$p" >/dev/null 2>&1 || { echo "  FAIL: $p build"; status=1; continue; }
    for t in literal_store literal_block_gc_shared; do
        got=$(./glon demo/shop/block.glon tests/core-v2/$t.glon 2>&1 | tail -n 1)
        if [ "$got" = "$(cat tests/core-v2/$t.expect)" ]; then echo "  ok: $p $t: $got"
        else echo "  FAIL: $p $t: '$got' (want $(cat tests/core-v2/$t.expect))"; status=1; fi
    done
done
$MAKE --no-print-directory glon MODULES= >/dev/null 2>&1
[ $status -eq 0 ] && echo "literal-store-check: PASS ($*)" || echo "literal-store-check: FAIL"
exit $status
