#!/bin/sh
# tests/core-c2/build_deps_check.sh - GLON-CORE-C2 build-dependency regression
# (make build-deps-check).
#
# Each stale-artifact case found in the C2 audit must force exactly the
# rebuild it affects, and an unchanged configuration must rebuild nothing:
#   1  unchanged configuration      -> glon, profile files and objects up to date
#   2  profile contents change      -> profile regenerated, glon relinked, new value used
#   3  profile generator changes    -> profile regenerated, glon relinked
#   4  memory-layout headers change -> runtime object and glon rebuilt
#   5  CivetWeb compiler flags      -> civetweb.o rebuilt
#   6  switching named profiles     -> glon relinked; an already generated
#                                      profile is not regenerated
#   7  other targets                -> .glon-config untouched
# `make -q` answers up-to-date (0) / needs rebuilding (1) without building.
# Only a temporary profile (profiles/zz-buildcheck.profile) is created; tracked
# files are only touched (their contents never change).
set -u
cd "$(dirname "$0")/../.."
MAKE=${MAKE:-make}
fails=0
ok()   { echo "  ok: $1"; }
bad()  { echo "  FAIL: $1"; fails=$((fails + 1)); }
q()    { $MAKE -q --no-print-directory "$@" >/dev/null 2>&1; echo $?; }   # 0 up to date, 1 stale
expect() { # expect LABEL WANT GOT
    if [ "$3" = "$2" ]; then ok "$1"; else bad "$1 (make -q said $3, expected $2)"; fi; }
build() { $MAKE --no-print-directory "$@" 2>&1; }
later() { sleep 1; }                  # make compares timestamps: keep them apart
G="glon MODULES="
TMPP=profiles/zz-buildcheck.profile

# ---- 1. unchanged configuration ------------------------------------------
build $G MEM_PROFILE=large >/dev/null || { echo "initial large build failed"; exit 1; }
expect "1 unchanged large build: glon up to date" 0 "$(q $G MEM_PROFILE=large)"
expect "1 unchanged large build: profile header up to date" 0 "$(q build/profile/large/glon_profile.h)"
out=$(build $G MEM_PROFILE=large)
case "$out" in *"-o glon"*|*glon-profile.sh*) bad "1 rerun relinked or regenerated: $out" ;; *) ok "1 rerunning the same command builds nothing" ;; esac

# ---- 2. profile contents change ------------------------------------------
sed 's/^NAME=large$/NAME=zz-buildcheck/' profiles/large.profile > "$TMPP"
build $G MEM_PROFILE=zz-buildcheck >/dev/null || bad "2 temporary profile build failed"
expect "2 temporary profile built: up to date" 0 "$(q $G MEM_PROFILE=zz-buildcheck)"
later
sed -i 's/^LOADER_CELLS=200000$/LOADER_CELLS=200016/' "$TMPP"
expect "2 profile contents changed: glon stale" 1 "$(q $G MEM_PROFILE=zz-buildcheck)"
expect "2 profile contents changed: header stale" 1 "$(q build/profile/zz-buildcheck/glon_profile.h)"
out=$(build $G MEM_PROFILE=zz-buildcheck)
case "$out" in *glon-profile.sh*) ok "2 rebuild regenerated the profile" ;; *) bad "2 rebuild did not regenerate" ;; esac
grep -q '^#define R0S1_HEAP_LIMIT 1321360L$' build/profile/zz-buildcheck/glon_profile.h \
  && grep -q '^#define MEM_CELLS 1333120$' build/profile/zz-buildcheck/s1_profile.h \
  && ok "2 header and S1 copy both carry the new layout (loader end 1321360, MEM_CELLS 1333120)" \
  || bad "2 regenerated files do not carry the new layout"
expect "2 after rebuild: up to date" 0 "$(q $G MEM_PROFILE=zz-buildcheck)"
rm -f "$TMPP" build/profile/zz-buildcheck/glon_profile.h build/profile/zz-buildcheck/s1_profile.c \
      build/profile/zz-buildcheck/s1_profile.h build/profile/zz-buildcheck/layout.txt
rmdir build/profile/zz-buildcheck 2>/dev/null || true

# ---- 3. generator changes -------------------------------------------------
build $G MEM_PROFILE=large >/dev/null
later; touch scripts/glon-profile.sh
expect "3 generator changed: profile header stale" 1 "$(q build/profile/large/glon_profile.h)"
expect "3 generator changed: glon stale" 1 "$(q $G MEM_PROFILE=large)"
build $G MEM_PROFILE=large >/dev/null
expect "3 after rebuild: up to date" 0 "$(q $G MEM_PROFILE=large)"

# ---- 4. memory-layout headers change --------------------------------------
build r0_s1_runtime.o >/dev/null
for h in r0_s1_layout.h m1_layout.h r0_s1.h; do
    expect "4 before touching $h: r0_s1_runtime.o up to date" 0 "$(q r0_s1_runtime.o)"
    later; touch "$h"
    expect "4 $h changed: r0_s1_runtime.o stale" 1 "$(q r0_s1_runtime.o)"
    expect "4 $h changed: glon stale" 1 "$(q $G MEM_PROFILE=large)"
    build r0_s1_runtime.o >/dev/null; build $G MEM_PROFILE=large >/dev/null
done

# ---- 5. CivetWeb compiler flags -------------------------------------------
build third_party/civetweb/civetweb.o >/dev/null || bad "5 civetweb build failed"
expect "5 civetweb.o up to date" 0 "$(q third_party/civetweb/civetweb.o)"
later
expect "5 OPENSSL_CFLAGS changed: civetweb.o stale" 1 "$(q third_party/civetweb/civetweb.o OPENSSL_CFLAGS=-DGLON_BUILDCHECK)"
later
build third_party/civetweb/civetweb.o >/dev/null        # flags restored: rebuild once
expect "5 flags restored and rebuilt: up to date" 0 "$(q third_party/civetweb/civetweb.o)"

# ---- 6. switching named profiles ------------------------------------------
build $G MEM_PROFILE=large >/dev/null
expect "6 switching large -> server: glon stale" 1 "$(q $G MEM_PROFILE=server)"
build $G MEM_PROFILE=server >/dev/null
expect "6 server built: up to date" 0 "$(q $G MEM_PROFILE=server)"
expect "6 switching back server -> large: glon stale" 1 "$(q $G MEM_PROFILE=large)"
out=$(build $G MEM_PROFILE=large)
case "$out" in
    *glon-profile.sh*) bad "6 switching back regenerated an up-to-date profile" ;;
    *"-o glon"*) ok "6 switching back relinked without regenerating the large profile" ;;
    *) bad "6 switching back did not relink" ;;
esac

# ---- 7. other targets leave .glon-config alone ----------------------------
before=$(stat -c %Y .glon-config)
later; build profile-list >/dev/null; build r0_s1_runtime.o >/dev/null
after=$(stat -c %Y .glon-config)
[ "$before" = "$after" ] && ok "7 other targets did not rewrite .glon-config" || bad "7 .glon-config rewritten by another target"
expect "7 glon still up to date afterwards" 0 "$(q $G MEM_PROFILE=large)"

if [ "$fails" -eq 0 ]; then echo "build-deps-check: PASS"; else echo "build-deps-check: FAIL ($fails)"; exit 1; fi
