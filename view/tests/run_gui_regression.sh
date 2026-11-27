#!/bin/sh
# run_gui_regression.sh -- the Glon GUI regression set, from a clean checkout.
#
#   view/tests/run_gui_regression.sh [--x11] [--record [FRAMEDIR]] [LOGDIR]
#
# Builds what the suites need (the root `glon` runner and glon-lib, and a
# forced rebuild of the View test binaries), then runs every GUI suite with
# its own log in LOGDIR (default view/build/regression, which git ignores)
# and prints a summary.  Exits 1 if any suite fails.
#
# By default all SDL tests use SDL's dummy video driver: nothing is shown and
# no real display is touched.  --x11 also runs the GUI-P2 and WG runner
# suites with SDL_VIDEODRIVER=x11: they open real windows on $DISPLAY and push
# events into the test program's own SDL queue (no XTest, pointer warping or
# system-wide input), but they do appear on that desktop, so it is opt-in.
#
# --record writes every frame the SDL backend presents (GV_RECORD_DIR, one
# folder per suite under FRAMEDIR, default LOGDIR/frames) and then encodes
# LOGDIR/regression.mp4 with ffmpeg: one segment per suite that drew frames,
# padded to 1920x1080 and labelled with the suite name.  Headless suites
# draw no frames.  This records what the GUI drew, not the desktop.
set -u

x11=0
record=0
frames=""
while [ $# -gt 0 ]; do
    case "$1" in
        --x11) x11=1; shift ;;
        --record) record=1; shift
                  case "${1:-}" in /*|./*|../*) frames=$1; shift ;; esac ;;
        *) break ;;
    esac
done
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
logs=${1:-"$root/view/build/regression"}
mkdir -p "$logs"
summary="$logs/SUMMARY.txt"
[ -n "$frames" ] || frames="$logs/frames"
if [ $record -eq 1 ]; then rm -rf "$frames"; mkdir -p "$frames"; fi
: > "$summary"
failed=0
seq=0

run() {
    name=$1; shift
    t0=$(date +%s)
    if [ $record -eq 1 ] && [ "$name" != "${name#build}" ]; then
        ( cd "$root" && "$@" ) > "$logs/$name.log" 2>&1
    elif [ $record -eq 1 ]; then
        seq=$((seq + 1))                  # folders sort in run order
        dir="$frames/$(printf '%02d' $seq)-$name"
        mkdir -p "$dir"
        ( cd "$root" && GV_RECORD_DIR="$dir" "$@" ) > "$logs/$name.log" 2>&1
    else
        ( cd "$root" && "$@" ) > "$logs/$name.log" 2>&1
    fi
    rc=$?
    t1=$(date +%s)
    printf '%-22s %s %4ds\n' "$name" "$([ $rc -eq 0 ] && echo PASS || echo "FAIL($rc)")" $((t1 - t0)) | tee -a "$summary"
    [ $rc -eq 0 ] || failed=1
}

# prerequisites
run build-glon    make glon
run build-view    make -C view -B build/test_backend build/test_view build/test_glon build/test_table \
                      build/test_table_host_1m build/test_table_data build/test_vid_widgets \
                      build/widget_gallery_shot build/test_gui_f2 build/test_gui4b2c build/test_wg_runner \
                      build/test_vg_runner build/test_vid_load_64k build/test_global_capacity_64k \
                      build/test_global_capacity_256k build/test_global_capacity_1m \
                      build/test_global_capacity_4m build/glon_demo_1m build/bench_gui_p2 \
                      build/bench_gui_p2_prof build/test_gui_p2

# the frozen substrate and the GUI suites (SDL dummy driver)
run frozen-s1         ./check-frozen-s1.sh
run make-test         make -C view test
run gui-core          make -C view test-gui-core
run widget-gallery    make -C view test-widget-gallery
run gui3              make -C view test-gui3
run gui4              make -C view test-gui4
run gui4-scroll       make -C view test-gui4-scroll
run gui-f1b           make -C view test-gui-f1b
run gui-f2            make -C view test-gui-f2
run gui4b2c           make -C view test-gui4b2c
run wg-runner         make -C view test-wg-runner
run vg-runner         make -C view test-vg-runner
run gui-p2            make -C view test-gui-p2
run intent            make -C view test-intent
run vocab-v1          sh view/tests/test_vocab_v1.sh
run vid-load-64k      make -C view test-vid-load-64k
run capacity-profiles make -C view test-global-capacity-profiles

if [ $x11 -eq 1 ]; then
    run gui-p2-x11    env SDL_VIDEODRIVER=x11 make -C view test-gui-p2
    run wg-runner-x11 env SDL_VIDEODRIVER=x11 make -C view test-wg-runner
    run foreign-x11   make -C view test-foreign-x11
fi

# the video: one labelled segment per suite that drew frames, then joined
if [ $record -eq 1 ]; then
    font=/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
    list="$frames/segments.txt"
    : > "$list"
    for d in "$frames"/*/; do
        name=$(basename "$d" | sed 's/^[0-9]*-//')
        n=$(ls "$d" 2>/dev/null | grep -c '\.png$')
        [ "$n" -gt 0 ] || continue
        ffmpeg -v error -y -framerate 15 -pattern_type glob -i "$d/*.png" \
            -vf "scale=1920:1080:force_original_aspect_ratio=decrease,pad=1920:1080:(ow-iw)/2:(oh-ih)/2:color=0x101214,drawtext=fontfile=$font:text='$name  ($n frames)':x=24:y=1080-44:fontsize=28:fontcolor=white:box=1:boxcolor=black@0.6" \
            -c:v libx264 -pix_fmt yuv420p -r 15 "${d%/}.mp4" &&
            echo "file '${d%/}.mp4'" >> "$list"
        printf '%-22s %6d frames\n' "$name" "$n" | tee -a "$summary"
    done
    if [ -s "$list" ] && ffmpeg -v error -y -f concat -safe 0 -i "$list" -c copy "$logs/regression.mp4"; then
        echo "video: $logs/regression.mp4" | tee -a "$summary"
    else
        echo "video: FAILED" | tee -a "$summary"; failed=1
    fi
fi

if [ $failed -eq 0 ]; then echo "GUI regression: all passed (logs in $logs)"; else echo "GUI regression: FAILURES (logs in $logs)"; fi
exit $failed
