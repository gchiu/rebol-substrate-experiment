#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
# GLON-VIEW-CORE-V3: the 1M runtime is C2's desktop-1m generated profile
make --no-print-directory -C "$root" build/profile/desktop-1m/glon_profile.h >/dev/null
gcc -std=c17 -O2 -include "$root/build/profile/desktop-1m/glon_profile.h" -I"$root/view/include" -I"$root" \
    -o "$tmp/f1b_proof" "$root/experiments/gui-f1b/f1b_proof.c" \
    "$root/view/src/gv_glon_host.c" "$root/view/src/gv_host.c" "$root/view/src/gv_host_x11.c" "$root/view/src/gv_host_webkit.c" "$root/view/src/gv_view.c" \
    "$root/view/src/gv_table.c" "$root/view/src/gv_backend_headless.c" \
    "$root/r0_s1_g1a_live.c" \
    "$root/r0_s1_g1a.c" "$root/r0_s1_show.c" \
    "$root/r0_s1_session.c" "$root/r0_s1_runtime.c" \
    "$root/build/profile/desktop-1m/s1_profile.c" -lm
SDL_VIDEODRIVER=dummy "$tmp/f1b_proof" "$root" > "$tmp/out" 2>&1
grep -q 'takes=1 chunks=2 errors=0' "$tmp/out"
grep -q 'F1B PROOF PASSED' "$tmp/out"
# Real application batches exceeding 4 KB still produce complete View trees.
SDL_VIDEODRIVER=dummy "$root/view/build/widget_gallery_shot" --gui3 --root "$root" \
    --app view/glon/gui4_scroll_demo.glon --width 700 --height 480 --dump-nodes \
    --screenshot "$tmp/scroll.png" > "$tmp/scroll.out" 2>&1
grep -q '\[f1b\] takes=1 chunks=3 errors=0' "$tmp/scroll.out"
test "$(grep -c '^node ' "$tmp/scroll.out")" -ge 90
SDL_VIDEODRIVER=dummy "$root/view/build/widget_gallery_shot" --root "$root" \
    --app view/glon/widget_gallery.glon --width 1000 --height 700 --dump-nodes \
    --screenshot "$tmp/gallery.png" > "$tmp/gallery.out" 2>&1
# WG-3: the restyled gallery's first build is ~14.6 KB (28 source-line
# nodes), so it now arrives as four sealed 4 KB chunks
# GUI-P2: the responsive gallery adds 12 source lines, the root and preview
# viewports, thumbs and its first layout pass: ~17.2 KB, five sealed chunks
grep -q '\[f1b\] takes=1 chunks=5 errors=0' "$tmp/gallery.out"
test "$(grep -c '^node ' "$tmp/gallery.out")" -ge 120

printf 'GUI-F1B verified: by-reference chunks, ordering, canaries intact\n'
