#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
shot="$root/view/build/widget_gallery_shot"
if [ ! -x "$shot" ]; then
    make -C "$root/view" build/widget_gallery_shot >/dev/null
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

run_shot() {
    SDL_VIDEODRIVER=dummy "$shot" --root "$root" \
        --app view/glon/widget_gallery.glon --width 1200 --height 700 "$@"
}

run_shot --click-text Activate --dump-nodes --screenshot "$tmp/button.png" > "$tmp/button.out" 2>&1
grep -q 'text=Button activated' "$tmp/button.out"

run_shot --click-text-list Radio,Household --dump-nodes --screenshot "$tmp/radio.png" > "$tmp/radio.out" 2>&1
grep -q 'text=Radio: Household' "$tmp/radio.out"

run_shot --click-text-list Field,Type\ here --type Hi --dump-nodes --screenshot "$tmp/field.png" > "$tmp/field.out" 2>&1
grep -q 'text=Field: Hi' "$tmp/field.out"
grep -q 'text=Hi' "$tmp/field.out"

run_shot --click-text-list Vocabulary,Call\ Status --dump-nodes --screenshot "$tmp/vocab.png" > "$tmp/vocab.out" 2>&1
grep -q 'text=Vocabulary dispatch' "$tmp/vocab.out"

run_shot --click-text Theme --screenshot "$tmp/light.png" > /dev/null 2>&1

run_shot --screenshot "$tmp/dark.png" > /dev/null 2>&1
test -s "$tmp/dark.png"
test -s "$tmp/light.png"
if cmp -s "$tmp/dark.png" "$tmp/light.png"; then
    echo "dark and light screenshots are identical" >&2
    exit 1
fi

printf 'WG-1 verified: button, radio, field, V-1 dispatch, theme, dark/light screenshots\n'
printf 'WG-1 known limitation: checkbox and table row click dispatch need another pass\n'