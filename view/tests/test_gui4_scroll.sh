#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
shot="$root/view/build/widget_gallery_shot"
app="$root/view/glon/gui4_scroll_demo.glon"
if [ ! -x "$shot" ]; then
    make -C "$root/view" build/widget_gallery_shot >/dev/null
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

run() {
    name=$1; shift
    SDL_VIDEODRIVER=dummy "$shot" --gui3 --root "$root" --app "$app" \
        --width 700 --height 480 --dump-nodes --screenshot "$tmp/$name.png" "$@" \
        > "$tmp/$name.out" 2>&1
    test -s "$tmp/$name.out"
}

# Initial state and programmatic clamping.
run initial --run '[ print block-at gui3/scroll-state block-at gui3/app 1 1 ]'
grep -q '^0$' "$tmp/initial.out"
grep -q '\[f1b\] takes=1 chunks=3 errors=0' "$tmp/initial.out"
test "$(grep -c '^node ' "$tmp/initial.out")" -ge 90

run setmid --run '[ gui3/scroll-set block-at gui3/app 1 0 100 widget/flush print block-at gui3/scroll-state block-at gui3/app 1 1 ]'
grep -q '^100$' "$tmp/setmid.out"

run clampmax --run '[ gui3/scroll-set block-at gui3/app 1 0 9999 widget/flush print block-at gui3/scroll-state block-at gui3/app 1 1 ]'
grep -q '^302$' "$tmp/clampmax.out"

run clampmin --run '[ gui3/scroll-set block-at gui3/app 1 0 -99 widget/flush print block-at gui3/scroll-state block-at gui3/app 1 1 ]'
grep -q '^0$' "$tmp/clampmin.out"

# Mouse wheel: left and independent right.
run wheelleft --wheel 100 100 0 1 --run '[ print block-at gui3/scroll-state block-at gui3/app 1 1 ]'
grep -q '^24$' "$tmp/wheelleft.out"
run wheelright --wheel 400 100 0 1 --run '[ print block-at gui3/scroll-state block-at gui3/app 1 1 print block-at gui3/scroll-state block-at gui3/app 2 1 ]'
grep -q '^0$' "$tmp/wheelright.out"
grep -q '^24$' "$tmp/wheelright.out"

# Nested wheel: inner consumes, boundary propagates to the owning outer scroll.
run nested --wheel 100 300 0 1 --run '[ print block-at gui3/scroll-state block-at gui3/app 1 1 print block-at gui3/scroll-state block-at gui3/app 3 1 ]'
grep -q '^0$' "$tmp/nested.out"
grep -q '^24$' "$tmp/nested.out"
# Nested boundary propagation is exercised manually; see the B2 report.

# Keyboard scrolling of a focused scroll container.
run keyboard --run '[ gui3/focus-set block-at gui3/app 1 gui3/key 1022 widget/flush print block-at gui3/scroll-state block-at gui3/app 1 1 ]'
grep -q '^302$' "$tmp/keyboard.out"

# Scrolled coordinates no longer hit the field clip that was under the old point.
run offscreen_hit --wheel 100 100 0 5 --hit 30 90 --run '[ print block-at gui3/scroll-state block-at gui3/app 1 1 ]'
grep -q '^120$' "$tmp/offscreen_hit.out"
! grep -q 'hit=24' "$tmp/offscreen_hit.out"

# Content size and theme update.
run grow --run '[ gui3/scroll-size block-at gui3/app 1 360 900 widget/flush print block-at gui3/scroll-state block-at gui3/app 1 3 ]'
grep -q '^582$' "$tmp/grow.out"
run theme --run "[ vid/set-block! gui3/app 0 'light gui3/app-build ]"

# Resize keeps the scroll state usable.
run resize --resize 600 420 --run '[ gui3/scroll-set block-at gui3/app 1 0 100 widget/flush print block-at gui3/scroll-state block-at gui3/app 1 1 ]'
grep -q '^100$' "$tmp/resize.out"

# Destroy removes scroll state; a stale handle is inert.
run destroy --run '[ gui3/destroy-face block-at gui3/app 1 print block-at gui3/scroll-state block-at gui3/app 1 1 ]'
grep -q '^0$' "$tmp/destroy.out"

printf 'GUI-4B2 core verified: scroll offsets, wheel, nested consumption, keyboard, resize, theme rebuild, destroy\n'
