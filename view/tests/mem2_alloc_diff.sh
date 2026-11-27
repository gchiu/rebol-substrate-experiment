#!/bin/sh
# GLON-MEM-2/3: run the builds of mem2_alloc_diff for every profile -- the
# current hint, the old scan from the base (-DR0S1_ALLOC_SCAN_FROM_BASE) and,
# when built, the GLON-MEM-2 hint (-DR0S1_ALLOC_HINT_MEM2).  Their outputs
# (heap layout hashes per chunk) must be identical: the same first-fit choices.
# Usage: sh tests/mem2_alloc_diff.sh BUILD_DIR
b="$1"
status=0
for p in 64k 256k 1m 4m; do
    "$b/mem2_alloc_diff_$p" > "$b/mem2-$p.txt" 2>&1
    rc_hint=$?
    n=$(grep -c layout "$b/mem2-$p.txt")
    # the halt dump's IP differs with the code size: compare the heap lines
    grep -v '^\[dump\]' "$b/mem2-$p.txt" > "$b/mem2-$p.cmp"
    for v in base mem2; do
        [ -x "$b/mem2_alloc_diff_${p}_$v" ] || continue
        "$b/mem2_alloc_diff_${p}_$v" > "$b/mem2-$p-$v.txt" 2>&1
        rc_v=$?
        grep -v '^\[dump\]' "$b/mem2-$p-$v.txt" > "$b/mem2-$p-$v.cmp"
        if cmp -s "$b/mem2-$p.cmp" "$b/mem2-$p-$v.cmp"; then
            echo "$p: identical to $v ($n heap layouts compared; exit hint $rc_hint, $v $rc_v)"
        else
            echo "$p: DIFFERENT from $v (exit hint $rc_hint, $v $rc_v)"
            diff "$b/mem2-$p-$v.cmp" "$b/mem2-$p.cmp" | head -8
            status=1
        fi
    done
    [ $rc_hint -eq 0 ] || status=1
done
exit $status
