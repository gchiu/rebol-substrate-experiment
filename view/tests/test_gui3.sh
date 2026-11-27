#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
shot="$root/view/build/widget_gallery_shot"
if [ ! -x "$shot" ]; then
    make -C "$root/view" build/widget_gallery_shot >/dev/null
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

run() {
    name=$1; shift
    SDL_VIDEODRIVER=dummy "$shot" --gui3 --root "$root" \
        --app view/glon/gui3_demo.glon --width 800 --height 500 \
        --dump-nodes "$@" > "$tmp/$name.out" 2>&1
    test -s "$tmp/$name.out"
}

# Keyboard editing and no full-form reconstruction.
run initial --screenshot "$tmp/initial.png"
run char --char X --screenshot "$tmp/char.png"
grep -q 'text=AdaX' "$tmp/char.out"
grep -q 'text=Ready' "$tmp/char.out"
test "$(grep -c '^node ' "$tmp/initial.out")" = "$(grep -c '^node ' "$tmp/char.out")"

run back --char X --key-list 8 --screenshot "$tmp/back.png"
grep -q 'text=Ada' "$tmp/back.out"
! grep -q 'text=AdaX' "$tmp/back.out"

# Focus traversal.
run tab --key-list 9 --dump-focus --screenshot "$tmp/tab.png"
grep -q 'focus view=' "$tmp/tab.out"
! grep -q 'focus view=0' "$tmp/tab.out"
run stab --key-list 1024 --dump-focus --screenshot "$tmp/stab.png"
! grep -q 'focus view=0' "$tmp/stab.out"

# Pointer activation, outside release and cancellation.
run ok --pointer-down-x 50 166 --pointer-up-x 50 166 --screenshot "$tmp/ok.png"
grep -q 'text=OK: Ada' "$tmp/ok.out"
run outside --pointer-down-x 50 166 --pointer-move-x 180 166 --pointer-up-x 180 166 --screenshot "$tmp/outside.png"
grep -q 'text=Pointer released outside' "$tmp/outside.out"
run cancel --pointer-down-x 50 166 --pointer-cancel-x 50 166 --screenshot "$tmp/cancel.png"
grep -q 'text=Pointer cancelled' "$tmp/cancel.out"

# Checkbox indicator and label.
run check_indicator --pointer-down-x 60 98 --pointer-up-x 60 98 --screenshot "$tmp/check_indicator.png"
grep -q 'text=✓' "$tmp/check_indicator.out"
grep -q 'text=Subscribe' "$tmp/check_indicator.out"
run check_label --click-text Subscribe --screenshot "$tmp/check_label.png"
grep -q 'text=✓' "$tmp/check_label.out"
grep -q 'text=Subscribe' "$tmp/check_label.out"

# Radio selection and mutual exclusion.
run radio --click-list 260,70,260,98 --screenshot "$tmp/radio.png"
test "$(grep -c 'text=●' "$tmp/radio.out")" -eq 1
grep -q 'text=One' "$tmp/radio.out"
grep -q 'text=Two' "$tmp/radio.out"

# Theme must preserve field, checkbox, radio and focus.
run theme_check --pointer-list 'down:60,98;up:60,98;down:300,166;up:300,166' --screenshot "$tmp/theme_check.png"
grep -q 'text=Light theme' "$tmp/theme_check.out"
test "$(grep -c 'text=✓' "$tmp/theme_check.out")" -eq 1
run theme_radio --pointer-list 'down:260,98;up:260,98;down:300,166;up:300,166' --screenshot "$tmp/theme_radio.png"
grep -q 'text=Light theme' "$tmp/theme_radio.out"
test "$(grep -c 'text=●' "$tmp/theme_radio.out")" -eq 1
run theme_repeat --pointer-list 'down:60,98;up:60,98;down:300,166;up:300,166;down:300,166;up:300,166' --screenshot "$tmp/theme_repeat.png"
grep -q 'text=Dark theme' "$tmp/theme_repeat.out"
test "$(grep -c 'text=✓' "$tmp/theme_repeat.out")" -eq 1
run theme_field --pointer-list 'down:300,166;up:300,166' --click-list 100,75 --char X --screenshot "$tmp/theme_field.png"
grep -q 'text=AdaX' "$tmp/theme_field.out"
grep -q 'text=Light theme' "$tmp/theme_field.out"

# Rebuild after theme leaves the new controls live.
run theme_ok --pointer-list 'down:300,166;up:300,166;down:50,166;up:50,166' --screenshot "$tmp/theme_ok.png"
grep -q 'text=OK: Ada' "$tmp/theme_ok.out"

# Resize and GC survival.
run resize --resize 640 420 --screenshot "$tmp/resize.png"
grep -q 'text=Resized 640 420' "$tmp/resize.out"
grep -q 'node 0 type=1 parent=0 rect=0,0 640x420' "$tmp/resize.out"
run gc --char X --gc --screenshot "$tmp/gc.png"
grep -q 'text=AdaX' "$tmp/gc.out"
grep -q 'text=Ready' "$tmp/gc.out"

printf 'GUI-3 verified: editing, focus, pointer, selection, theme preservation, resize, GC\n'

# Disabled controls and re-enabling.
life=$root/view/glon/gui3_lifecycle.glon
run_life() {
    name=$1; shift
    SDL_VIDEODRIVER=dummy "$shot" --gui3 --root "$root" --app "$life" \
        --width 400 --height 300 --dump-nodes --dump-focus "$@" > "$tmp/$name.out" 2>&1
    test -s "$tmp/$name.out"
}

run_life disabled --pointer-list 'down:20,10;up:20,10;down:20,35;up:20,35;down:20,60;up:20,60;down:20,125;up:20,125' --char X --screenshot "$tmp/disabled.png"
grep -q 'text=x' "$tmp/disabled.out"
! grep -q 'text=✓' "$tmp/disabled.out"
! grep -q 'text=●' "$tmp/disabled.out"
grep -q 'focus view=0' "$tmp/disabled.out"

run_life disabled_tab --key-list 9 --dump-focus --screenshot "$tmp/disabled_tab.png"
! grep -q 'focus view=0' "$tmp/disabled_tab.out"

run_life destroy --pointer-list 'down:20,90;up:20,90' --screenshot "$tmp/destroy.png"
grep -q 'text=Destroyed' "$tmp/destroy.out"
! grep -q 'text=Victim' "$tmp/destroy.out"

reen=$root/view/glon/gui3_reenable.glon
SDL_VIDEODRIVER=dummy "$shot" --gui3 --root "$root" --app "$reen" \
    --width 400 --height 300 --pointer-list 'down:20,35;up:20,35;down:20,10;up:20,10' \
    --dump-nodes --screenshot "$tmp/reenable.png" > "$tmp/reenable.out" 2>&1
grep -q 'text=Enabled' "$tmp/reenable.out"
grep -q 'text=✓' "$tmp/reenable.out"
grep -q 'text=Toggle' "$tmp/reenable.out"
