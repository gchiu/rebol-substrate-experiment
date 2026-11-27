#!/bin/sh
# tests/core-v2/s_find_check.sh - GLON-VIEW-CORE-V2: the two string searches
# (make s-find-check).
#   s/index-of text pattern   C2 experiments/masm-vocabulary/find.glon (was
#                             s/find): first offset, or -1
#   s/find s i p              demo/shop/strings-text.glon (GLON-VOCAB-1 s/):
#                             first index >= i, or the length
# 1. find.glon's conformance suite (cases.py: Glon reference, MASM and C over
#    221 cases, input immutability, SIN! 'type) on s/index-of;
# 2. both searches in one session, in either load order, with their own
#    results (desktop-1m: the shared libraries exceed the 64K loader).
set -u
cd "$(dirname "$0")/../.."
MAKE=${MAKE:-make}
status=0
mkdir -p build/core-v2
${CC:-cc} -std=c17 -O2 -w experiments/masm-vocabulary/c_find.c -o build/core-v2/c_find || exit 1
$MAKE --no-print-directory glon MODULES= >/dev/null 2>&1 || exit 1
if python3 experiments/masm-vocabulary/cases.py --glon ./glon --c build/core-v2/c_find > build/core-v2/s-find-cases.txt 2>&1; then
    echo "  ok: s/index-of conformance: $(tail -n 1 build/core-v2/s-find-cases.txt)"
else echo "  FAIL: s/index-of conformance"; tail -n 5 build/core-v2/s-find-cases.txt; status=1; fi
$MAKE --no-print-directory glon MODULES= MEM_PROFILE=desktop-1m >/dev/null 2>&1 || exit 1
prog=build/core-v2/s-find-both.glon
printf 'values [ s/index-of "the web is wide" "web"  s/index-of "abc" "z"  s/find "the web is wide" 0 "web"  s/find "abc" 0 "z"  s/find "abcabc" 2 "bc" ]\n' > $prog
want='4 -1 4 3 4'
SH="demo/shop/block.glon demo/shop/list.glon demo/shop/strings-text.glon"
F=experiments/masm-vocabulary/find.glon
for order in "$SH $F" "$F $SH"; do
    got=$(./glon glon-lib/strings-ext.glon $order $prog 2>&1 | tail -n 1)
    if [ "$got" = "$want" ]; then echo "  ok: both searches, load order [$order]: $got"
    else echo "  FAIL: both searches, load order [$order]: '$got' (want $want)"; status=1; fi
done
$MAKE --no-print-directory glon MODULES= >/dev/null 2>&1
[ $status -eq 0 ] && echo "s-find-check: PASS" || echo "s-find-check: FAIL"
exit $status
