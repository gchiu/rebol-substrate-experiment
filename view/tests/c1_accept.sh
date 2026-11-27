#!/bin/sh
# view/tests/c1_accept.sh -- GLON-VIEW-PREP-1: the View acceptance suite for
# C1 integration (make -C view test-c1-accept).  It asks one question: does
# the combined core still run View, Intent and their applications?  It uses
# only the targets, binaries and Glon sources that exist on this branch; it
# assumes nothing about C1's own API.  See GLON-VIEW-C1-CONTRACT.md 6.
#
# Every step runs under a time limit, its exit status is recorded (never an
# echo's) in $LOG/summary.txt, and the run exits non-zero if any required
# step fails.  Everything runs under the SDL dummy driver: no window or input
# reaches a desktop.
#
# Optional sections (off by default):
#   C1_ACCEPT_X11=1       the real-engine tests on an X11 display (WSLg or X11;
#                         DISPLAY must be set): foreign X11 editor, WebKitGTK
#   C1_ACCEPT_VARIANTS=1  this branch's runtime-variant differential tests
#                         (allocator hint, closure check, set-word search):
#                         they build the runtime with -D switches of this
#                         branch, which a C1 core need not have
#
# Usage: sh tests/c1_accept.sh        (from view/, or through make)
cd "$(dirname "$0")/.." || exit 2
LOG=${C1_ACCEPT_LOG:-build/c1-accept}
mkdir -p "$LOG" || exit 2
: > "$LOG/summary.txt"
fail=0
MAKE=${MAKE:-make}
step() {   # name seconds command...
    name=$1; limit=$2; shift 2
    t0=$(date +%s)
    timeout "$limit" "$@" > "$LOG/$name.log" 2>&1
    rc=$?
    t1=$(date +%s)
    if [ $rc -eq 124 ]; then what="TIMEOUT"; elif [ $rc -eq 0 ]; then what="ok"; else what="FAIL"; fi
    printf '%-26s exit %3d  %5ds  %s\n' "$name" "$rc" "$((t1 - t0))" "$what" | tee -a "$LOG/summary.txt"
    [ $rc -eq 0 ] || fail=1
}
# each case its own process of BINARY; stops at the first failure
cases() { echo "for c in $2; do ./build/$1 .. \$c || exit 1; done"; }
MEMBERS="intent6-load intent6-select intent6-search intent6-new-edit intent6-validation intent6-unsaved intent6-delete intent6-reload intent6-keys intent6-resize"

echo "GLON-VIEW-PREP-1 C1 acceptance, $(date -u +%Y-%m-%dT%H:%MZ), $(git rev-parse --short HEAD 2>/dev/null)" | tee -a "$LOG/summary.txt"

# 0. builds (the 1M/4M binaries track the runtime sources: GLON-MEM-3 A)
step build                 1800 $MAKE build/test_gui_p2 build/test_gui_p2_1m build/test_gui_p2_4m
# 1. frozen S1, and the root suites: S1, the R0-S1 runtime and collector,
#    the primer doc-tests, GLON-VOCAB-1 and GLON-VIEW-PREP-1 vocabulary checks
step frozen-s1              120 sh -c 'cd .. && ./check-frozen-s1.sh'
step root-test             1800 $MAKE -C .. test
step build-deps             300 $MAKE test-build-deps
# 2. the shared libraries: strings, lists, numbers (64K, 1M, 4M)
step lib1                   900 $MAKE test-lib1
# 3. View, machine VID and Intent
step view-test              900 $MAKE test
step gui-p2                1200 $MAKE test-gui-p2
step gui-f1b                600 $MAKE test-gui-f1b
step intent                1800 $MAKE test-intent
# 4. components, layouts in one window, node lifecycle, native windows
step inst1                  900 $MAKE test-inst1
step inst2                  900 $MAKE test-inst2
step inst3                  900 $MAKE test-inst3
step win3                  1800 $MAKE test-win3
step c1-accept-4m           600 sh -c "$(cases test_gui_p2_4m 'c1-accept c1-no-windows')"
step c1-accept-1m           600 sh -c "$(cases test_gui_p2_1m 'c1-accept c1-no-windows')"
# 5. the membership application (4M; 1M; with the window library loaded)
step members-4m            1800 $MAKE test-members
step members-1m            1800 $MAKE test-members-1m
step intent7-4m             900 sh -c "$(cases test_gui_p2_4m 'intent7-search intent7-ops intent7-reload')"
step members-winlib-4m     1800 env GV_TEST_WINDOWS_LIB=1 sh -c "$(cases test_gui_p2_4m "$MEMBERS")"
# 6. capacity: function sites and globals per profile, the 64K VID profile
step site-capacity          900 $MAKE test-site-capacity-profiles
step global-capacity        900 $MAKE test-global-capacity-profiles
step vid-load-64k           300 $MAKE test-vid-load-64k

if [ "${C1_ACCEPT_X11:-0}" = 1 ]; then
    if [ -z "$DISPLAY" ]; then
        printf '%-26s skipped: DISPLAY not set\n' x11 | tee -a "$LOG/summary.txt"; fail=1
    else
        step foreign-x11        600 $MAKE test-foreign-x11
        step browser-x11        600 $MAKE test-browser-x11
        step intent3-browser    600 $MAKE test-intent3-browser-x11
    fi
fi
if [ "${C1_ACCEPT_VARIANTS:-0}" = 1 ]; then
    step mem2-alloc-diff   1800 $MAKE mem2-alloc-diff
    step mem5-closure-diff 1800 $MAKE mem5-closure-diff
    step intent7-set-diff  1800 $MAKE intent7-set-diff
fi

if [ $fail -eq 0 ]; then echo "C1 ACCEPTANCE: PASS" | tee -a "$LOG/summary.txt"
else echo "C1 ACCEPTANCE: FAIL (see $LOG/*.log)" | tee -a "$LOG/summary.txt"; fi
exit $fail
