#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
ln -s "$root/glon-lib" "$tmp/glon-lib"

run_demo() {
    runner=$1
    out=$2
    "$runner" glon-lib/strings-ext.glon view/glon/view_lib.glon \
        view/glon/vocab_lib.glon view/glon/vocab_v1_demo.glon > "$out"
    grep -q 'VOCAB_V1_PASS' "$out"
}

run_bench() {
    runner=$1
    out=$2
    "$runner" glon-lib/strings-ext.glon view/glon/vocab_lib.glon \
        view/glon/vocab_v1_bench.glon > "$out"
    grep -q 'VOCAB_V1_BENCH_PASS' "$out"
}

run_measure() {
    runner=$1
    vocab=$2
    out=$3
    cap=$4
    "$runner" view/glon/vocab_v1_empty_loop.glon \
        view/glon/vocab_v1_lookup_loop.glon "$vocab" > "$out"
    grep -q "loaded globals=116/$cap" "$out"
    grep -q "demo globals=138/$cap" "$out"
}

run_demo ./glon "$tmp/demo-64k.out"

for p in 256k 1m 4m; do
    case $p in
        256k) prof=desktop-256k; cap=1024 ;;
        1m) prof=desktop-1m; cap=4096 ;;
        4m) prof=desktop-4m; cap=16384 ;;
    esac
    # GLON-VIEW-CORE-V3: C2's generated desktop profiles
    make --no-print-directory build/profile/$prof/glon_profile.h >/dev/null
    flags="-include build/profile/$prof/glon_profile.h"
    s1src=build/profile/$prof/s1_profile.c

    gcc -std=c17 -Wall -Wextra -O2 -g -I. $flags \
        -o "$tmp/glon-$p" glon.c \
        r0_s1_session.c r0_s1_show.c \
        r0_s1_runtime.c $s1src

    gcc -std=c17 -Wall -Wextra -O2 -g -I. $flags \
        -o "$tmp/measure-$p" view/tests/measure_vocab_v1.c \
        r0_s1_session.c r0_s1_show.c \
        r0_s1_runtime.c $s1src

    run_demo "$tmp/glon-$p" "$tmp/demo-$p.out"
    run_bench "$tmp/glon-$p" "$tmp/bench-$p.out"
    run_measure "$tmp/measure-$p" view/glon/vocab_v1_small_timing.glon "$tmp/small-$p.out" "$cap"
    run_measure "$tmp/measure-$p" view/glon/vocab_v1_bench_timing.glon "$tmp/large-$p.out" "$cap"
    printf '%s: demo, bench, 4-op and 32-op global counts PASS\n' "$p"
done

printf 'VOCAB_V1_TESTS PASS\n'