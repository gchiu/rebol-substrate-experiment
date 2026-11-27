#!/bin/sh
# tests/core-v1/mem_diff.sh - GLON-VIEW-CORE-V1 differential checks of the
# runtime optimisations ported from glon-gui-f2 (make mem-diff-test).
#
#   GLON-MEM-2/3  mem2_alloc_diff: free-block hint vs -DR0S1_ALLOC_SCAN_FROM_BASE
#   GLON-MEM-5    mem5_closure_diff: mark-aware closure check vs
#                 -DR0S1_GC_CLOSURE_WALK_ALWAYS; GC interior tests in both
#   INTENT-7      both workloads: hashed set-word search vs -DR0S1_SET_LINEAR
#                 (only when those builds exist)
# First fit and the collector are deterministic, so each pair must print the
# same heap layouts, live counts and checksums per chunk.  The halt dump's IP
# differs with code size, so [dump] lines are not compared.
# Usage: sh tests/core-v1/mem_diff.sh BUILD_DIR PROFILE...
b=$1; shift
status=0
cmp_pair() { # cmp_pair LABEL NEW OLD
    "$b/$2" > "$b/$2.txt" 2>&1; rn=$?; "$b/$3" > "$b/$3.txt" 2>&1; ro=$?
    n=$(grep -c layout "$b/$2.txt")
    grep -v '^\[dump\]' "$b/$2.txt" > "$b/$2.cmp"; grep -v '^\[dump\]' "$b/$3.txt" > "$b/$3.cmp"
    if [ "$n" -gt 0 ] && cmp -s "$b/$2.cmp" "$b/$3.cmp"; then
        echo "  ok: $1: identical ($n heap layouts; exit $rn / $ro)"
    else
        echo "  FAIL: $1: DIFFERENT or empty ($n layouts; exit $rn / $ro)"; diff "$b/$3.cmp" "$b/$2.cmp" | head -6; status=1
    fi
    [ $rn -eq 0 ] && [ $ro -eq 0 ] || { echo "  FAIL: $1: nonzero exit"; status=1; }
}
for p in "$@"; do
    cmp_pair "$p MEM-2/3 hint vs scan from base" mem2-$p mem2-$p-base
    cmp_pair "$p MEM-5 closure check vs walk always" mem5-$p mem5-$p-walk
    for v in "" -walk; do
        "$b/interior-$p$v" > "$b/interior-$p$v.txt" 2>&1; rc=$?
        k=$(grep -c 'ok:' "$b/interior-$p$v.txt"); f=$(grep -c FAIL "$b/interior-$p$v.txt")
        if [ $rc -eq 0 ] && [ "$f" -eq 0 ]; then echo "  ok: $p GC interior$v: $k ok"; else echo "  FAIL: $p GC interior$v: exit $rc, $f FAIL"; status=1; fi
    done
    if [ -x "$b/mem2-$p-setlin" ]; then
        cmp_pair "$p INTENT-7 allocator workload: hashed set vs linear" mem2-$p mem2-$p-setlin
        cmp_pair "$p INTENT-7 closure workload: hashed set vs linear" mem5-$p mem5-$p-setlin
    fi
done
[ $status -eq 0 ] && echo "mem-diff-test: PASS ($*)" || echo "mem-diff-test: FAIL"
exit $status
