#!/bin/sh
# GLON-MEM-3 (A): the view Makefile rebuilds what a runtime change affects,
# and nothing for an unrelated change.  Nothing is modified: the targets are
# first brought up to date, then `make -n -W FILE` asks what make WOULD rebuild
# if FILE were newer.
#   sh tests/build_deps_test.sh   (from view/; make test-build-deps)
cd "$(dirname "$0")/.." || exit 2
targets="build/glon_demo_1m build/glon_demo_4m build/test_gui_p2 build/test_gui_p2_4m build/glon_demo build/test_glon"
make -s $targets >/dev/null 2>&1 || { echo "FAIL: could not build $targets"; exit 1; }
fail=0
# would_build FILE TARGET: 1 when make -n -W FILE TARGET runs a compile
would_build() {
    make -n -W "$1" "$2" 2>/dev/null | grep -q -- "-o $2 "
}
check() {   # expect(yes|no) FILE TARGET
    if would_build "$2" "$3"; then got=yes; else got=no; fi
    if [ "$got" = "$1" ]; then echo "ok: $3 rebuilt after $2: $got"
    else echo "FAIL: $3 rebuilt after $2: $got (wanted $1)"; fail=1; fi
}
# the runtime sources and headers of the 1M/4M builds (GLON-VIEW-CORE-V3: the
# root sources with C2's generated profile; s1.c through the profile's S1 copy)
for f in ../r0_s1_runtime.c ../s1.c ../r0_s1_session.c ../r0_s1_g1a.h ../r0_s1.h ../r0_s1_layout.h ../m1_layout.h ../scripts/glon-profile.sh; do
    check yes "$f" build/glon_demo_1m
    check yes "$f" build/glon_demo_4m
done
check yes ../r0_s1_runtime.c build/test_gui_p2
check yes ../r0_s1_runtime.c build/test_gui_p2_4m
# a desktop profile change rebuilds that profile's programs (conservatively,
# every profiled program: the runtime dependency list names all three profiles)
check yes ../profiles/desktop-1m.profile build/glon_demo_1m
check yes ../profiles/desktop-4m.profile build/glon_demo_4m
# the 64K tools compiled straight from the root sources: their headers too
check yes ../r0_s1.h build/test_glon
check yes ../s1.h build/test_glon
# unrelated files: nothing to rebuild
check no ../README.md build/glon_demo_1m
check no ../GLON-MEM-2.md build/glon_demo_4m
check no glon/intent_layout_demo.glon build/glon_demo_1m
check no ../r0_s1_debug_tests.c build/glon_demo_4m
if [ $fail -eq 0 ]; then echo "BUILD_DEPS PASS"; else echo "BUILD_DEPS FAIL"; fi
exit $fail
