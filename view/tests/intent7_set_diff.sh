#!/bin/sh
# INTENT-7: the hashed set-word search (the default) against the linear scan
# (-DR0S1_SET_LINEAR).  The search finds the same binding, so the GLON-MEM-5
# closure workload and the GLON-MEM-2 allocator workload -- both full of
# set-words in functions -- must print the same lines: heap layouts, live
# counts, closure checksums.  Usage: sh tests/intent7_set_diff.sh BUILD_DIR
b="$1"
status=0
for t in mem5_closure_diff mem2_alloc_diff; do
    for p in 64k 256k 1m 4m; do
        "$b/${t}_$p" > "$b/i7set-$t-$p.txt" 2>&1; rc_new=$?
        "$b/${t}_${p}_setlin" > "$b/i7set-$t-$p-lin.txt" 2>&1; rc_lin=$?
        n=$(grep -c layout "$b/i7set-$t-$p.txt")
        grep -v '^\[dump\]' "$b/i7set-$t-$p.txt" > "$b/i7set-$t-$p.cmp"
        grep -v '^\[dump\]' "$b/i7set-$t-$p-lin.txt" > "$b/i7set-$t-$p-lin.cmp"
        if cmp -s "$b/i7set-$t-$p.cmp" "$b/i7set-$t-$p-lin.cmp" && [ "$n" -gt 0 ]; then
            echo "$t $p: identical ($n heap layouts compared; exit new $rc_new, linear $rc_lin)"
        else
            echo "$t $p: DIFFERENT (exit new $rc_new, linear $rc_lin)"
            diff "$b/i7set-$t-$p-lin.cmp" "$b/i7set-$t-$p.cmp" | head -6
            status=1
        fi
        [ $rc_new -eq 0 ] && [ $rc_lin -eq 0 ] || status=1
    done
done
exit $status
