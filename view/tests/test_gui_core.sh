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
    SDL_VIDEODRIVER=dummy "$shot" --gui --root "$root" \
        --app view/glon/gui_demo.glon --width 700 --height 500 "$@"
}

run_shot --dump-nodes --click-x 100 --click-y 37 --screenshot "$tmp/alpha.png" > "$tmp/alpha.out" 2>&1
grep -q 'text=Button activated' "$tmp/alpha.out"
grep -q '\[globals\] [0-9][0-9]*/4096' "$tmp/alpha.out"

run_shot --dump-nodes --click-x 100 --click-y 87 --screenshot "$tmp/beta.png" > "$tmp/beta.out" 2>&1
grep -q 'text=Beta disabled' "$tmp/beta.out"

run_shot --dump-nodes --click-list 100,87,100,87 --screenshot "$tmp/beta2.png" > "$tmp/beta2.out" 2>&1
grep -q 'text=Beta enabled' "$tmp/beta2.out"

run_shot --dump-nodes --click-x 100 --click-y 187 --screenshot "$tmp/recreate.png" > "$tmp/recreate.out" 2>&1
grep -q 'text=Alpha recreated' "$tmp/recreate.out"

run_shot --dump-nodes --click-x 100 --click-y 237 --screenshot "$tmp/light.png" > "$tmp/light.out" 2>&1
grep -q 'text=Light theme' "$tmp/light.out"

run_shot --dump-nodes --click-list 100,37,100,87,100,187,100,237,100,237 \
    --screenshot "$tmp/sequence.png" > "$tmp/sequence.out" 2>&1
grep -q 'text=Alpha recreated' "$tmp/sequence.out"
grep -q 'text=Light theme' "$tmp/sequence.out"
grep -q 'text=Dark theme' "$tmp/sequence.out"

run_shot --dump-nodes --click-list 100,187,100,37 --screenshot "$tmp/recreate-alpha.png" > "$tmp/recreate-alpha.out" 2>&1
grep -q 'text=Button activated' "$tmp/recreate-alpha.out"

run_shot --dump-nodes --click-list 100,237,100,37 --screenshot "$tmp/theme-alpha.png" > "$tmp/theme-alpha.out" 2>&1
grep -q 'text=Button activated' "$tmp/theme-alpha.out"

for png in "$tmp/alpha.png" "$tmp/beta.png" "$tmp/beta2.png" "$tmp/recreate.png" "$tmp/light.png" "$tmp/sequence.png"; do
    test -s "$png"
done
if cmp -s "$tmp/alpha.png" "$tmp/light.png"; then
    echo "dark and light GUI screenshots are identical" >&2
    exit 1
fi

printf 'GUI-2 verified: alpha, beta toggle, recreate, theme rebuild, post-rebuild dispatch, 1M profile\n'
