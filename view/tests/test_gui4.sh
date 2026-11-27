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

run initial --screenshot "$tmp/initial.png"
grep -q 'text=Ada' "$tmp/initial.out"

# Caret movement is insertion-point editing, not append-only editing.
run left_insert --key-list 1073741904 --char-after X --screenshot "$tmp/left_insert.png"
grep -q 'text=AdXa' "$tmp/left_insert.out"
! grep -q 'text=AdaX' "$tmp/left_insert.out"

run home_delete --key-list 1073741898,127 --screenshot "$tmp/home_delete.png"
grep -q 'text=da' "$tmp/home_delete.out"

run end_backspace --key-list 1073741901,8 --screenshot "$tmp/end_backspace.png"
grep -q 'text=Ad' "$tmp/end_backspace.out"

run end_delete --key-list 127 --screenshot "$tmp/end_delete.png"
grep -q 'text=Ada' "$tmp/end_delete.out"

# The visible caret follows the edit position.
run caret_left --key-list 1073741904 --screenshot "$tmp/caret_left.png"
grep -q 'rect=14,0 1x17' "$tmp/caret_left.out"

run home_insert --key-list 1073741898 --char-after Z --screenshot "$tmp/home_insert.png"
grep -q 'text=ZAda' "$tmp/home_insert.out"

# Editing must not rebuild the form; only the text changes.
test "$(grep -c '^node ' "$tmp/initial.out")" = "$(grep -c '^node ' "$tmp/left_insert.out")"

# Caret/edit state survives GC.
run gc_edit --key-list 1073741904 --char-after X --gc --screenshot "$tmp/gc_edit.png"
grep -q 'text=AdXa' "$tmp/gc_edit.out"

# Arrow keys must not edit a non-field control after focus moves.
run tab_arrow --key-list 9,1073741904 --screenshot "$tmp/tab_arrow.png"
grep -q 'text=Ada' "$tmp/tab_arrow.out"


# GUI-4B1: selection, clipboard, Unicode-safe caret movement and clipping.
run shift_select --key-mod-list '1073741904:1,1073741904:1' --screenshot "$tmp/shift_select.png"
grep -q 'rect=7,0 14x17' "$tmp/shift_select.out"
grep -q 'text=Ada' "$tmp/shift_select.out"

run shift_replace --key-mod-list '1073741904:1,1073741904:1' --char-after X --screenshot "$tmp/shift_replace.png"
grep -q 'text=AX' "$tmp/shift_replace.out"

run copy_selection --key-mod-list '1073741904:1,99:64' --dump-clipboard --screenshot "$tmp/copy_selection.png"
grep -q 'clipboard=a' "$tmp/copy_selection.out"

run shift_backspace --key-mod-list '1073741904:1,8:0' --screenshot "$tmp/shift_backspace.png"
grep -q 'text=Ad' "$tmp/shift_backspace.out"

run cut_all --key-mod-list '97:64,120:64' --dump-clipboard --screenshot "$tmp/cut_all.png"
grep -q 'clipboard=Ada' "$tmp/cut_all.out"
! grep -q 'text=Ada' "$tmp/cut_all.out"

run paste_all --clipboard-set XYZ --key-mod-list '97:64,118:64' --screenshot "$tmp/paste_all.png"
grep -q 'text=XYZ' "$tmp/paste_all.out"

run paste_replaces --clipboard-set Q --key-mod-list '1073741904:1,118:64' --screenshot "$tmp/paste_replaces.png"
grep -q 'text=AdQ' "$tmp/paste_replaces.out"

run empty_clipboard --key-mod-list '97:64,118:64' --dump-clipboard --screenshot "$tmp/empty_clipboard.png"
grep -q 'clipboard=<empty>' "$tmp/empty_clipboard.out"
grep -q 'text=Ada' "$tmp/empty_clipboard.out"

run unicode --char 中 --key-list 1073741904 --char-after 文 --screenshot "$tmp/unicode.png"
grep -q 'text=Ada文中' "$tmp/unicode.out"

run long_text --char 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ --screenshot "$tmp/long_text.png"
grep -q 'text=Ada0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ' "$tmp/long_text.out"
grep -Eq 'rect=-[0-9]+,0 148x17' "$tmp/long_text.out"
grep -q 'rect=147,0 1x17' "$tmp/long_text.out"

run long_home --char 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ --key-list 1073741898 --screenshot "$tmp/long_home.png"
grep -q 'node 11 type=4 parent=10 rect=0,0 148x17' "$tmp/long_home.out"
grep -q 'rect=0,0 1x17' "$tmp/long_home.out"

run gc_selection --key-mod-list '1073741904:1,1073741904:1' --gc --screenshot "$tmp/gc_selection.png"
grep -q 'rect=7,0 14x17' "$tmp/gc_selection.out"


# Copy selected Unicode text without splitting it.
run unicode_copy --char 中 --key-mod-list '97:64,99:64' --dump-clipboard --screenshot "$tmp/unicode_copy.png"
grep -q 'clipboard=Ada中' "$tmp/unicode_copy.out"

# Theme rebuild cancels selection but keeps field text.
run theme_selection --key-mod-list '1073741904:1,1073741904:1' \
    --pointer-list 'down:300,166;up:300,166' --screenshot "$tmp/theme_selection.png"
grep -q 'text=Ada' "$tmp/theme_selection.out"
grep -q 'text=Light theme' "$tmp/theme_selection.out"
! grep -q 'rect=7,0 14x17' "$tmp/theme_selection.out"

# Disabled fields reject clipboard and editing keys.
life=$root/view/glon/gui3_lifecycle.glon
SDL_VIDEODRIVER=dummy "$shot" --gui3 --root "$root" --app "$life" \
    --width 400 --height 300 --dump-nodes --dump-clipboard \
    --key-mod-list '97:64,99:64' --char X --screenshot "$tmp/disabled_clip.png" \
    > "$tmp/disabled_clip.out" 2>&1
grep -q 'clipboard=<empty>' "$tmp/disabled_clip.out"
grep -q 'text=x' "$tmp/disabled_clip.out"
! grep -q 'text=xX' "$tmp/disabled_clip.out"

# Theme switching must not lose the edit caret or the field text.
run theme_edit --pointer-list 'down:300,166;up:300,166' \
    --key-list 1073741904 --char-after Q --screenshot "$tmp/theme_edit.png"
grep -q 'text=AdQa' "$tmp/theme_edit.out"
grep -q 'text=Light theme' "$tmp/theme_edit.out"

printf 'GUI-4B1 verified: selection, clipboard, Unicode caret movement, field clipping and scrolling\n'
