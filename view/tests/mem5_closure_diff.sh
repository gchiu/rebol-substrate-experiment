#!/bin/sh
# GLON-MEM-5: run the mark-aware and the kind-only (_walk) builds of
# mem5_closure_diff, and of the GC interior tests, for every profile.  The
# differential outputs (heap layouts, live counts, closure checksums per chunk)
# must be identical; the interior tests must pass in both.
# Usage: sh tests/mem5_closure_diff.sh BUILD_DIR
b="$1"
status=0
for p in 64k 256k 1m 4m; do
    "$b/mem5_closure_diff_$p" > "$b/mem5-$p.txt" 2>&1; rc_new=$?
    "$b/mem5_closure_diff_${p}_walk" > "$b/mem5-$p-walk.txt" 2>&1; rc_old=$?
    n=$(grep -c layout "$b/mem5-$p.txt")
    grep -v '^\[dump\]' "$b/mem5-$p.txt" > "$b/mem5-$p.cmp"
    grep -v '^\[dump\]' "$b/mem5-$p-walk.txt" > "$b/mem5-$p-walk.cmp"
    if cmp -s "$b/mem5-$p.cmp" "$b/mem5-$p-walk.cmp"; then
        echo "$p: identical ($n heap layouts, live counts and checksums compared; exit new $rc_new, old $rc_old)"
    else
        echo "$p: DIFFERENT (exit new $rc_new, old $rc_old)"
        diff "$b/mem5-$p-walk.cmp" "$b/mem5-$p.cmp" | head -8
        status=1
    fi
    [ $rc_new -eq 0 ] && [ $rc_old -eq 0 ] || status=1
    for v in "" _walk; do
        "$b/mem5_interior_$p$v" > "$b/mem5-interior-$p$v.txt" 2>&1; rc=$?
        echo "$p interior${v:+ (old check)}: exit $rc, $(grep -c 'ok:' "$b/mem5-interior-$p$v.txt") ok, $(grep -c 'FAIL' "$b/mem5-interior-$p$v.txt") FAIL"
        [ $rc -eq 0 ] || status=1
    done
done
exit $status
