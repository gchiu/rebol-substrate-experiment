#!/bin/sh
# tests/core-c2/config_rebuild_check.sh - GLON-CORE-CI-1A regression
# (make config-rebuild-check).
#
# A configuration change (MODULES, MEM_PROFILE, CivetWeb flags) must rebuild
# the affected output even when the rewritten configuration stamp has the same
# timestamp as the output -- what a one-second-resolution make sees when the
# stamp is written in the same second as the previous link (the CI-1 macOS
# https-g3a failure: the http-only constrained-notask glon was kept and the
# G3A file options printed usage).  An unchanged configuration must still
# rebuild nothing.  The tie is made exactly with `touch -r`, so the test does
# not depend on the host's timestamp resolution or on sleeping.
#
# The resulting executable is checked, not only make's output:
#   modules  its usage text lists --io-root (file) and --http-cert (http);
#   profile  a literal with 700 distinct words fits the server profile's
#            1,024 symbols and stops with symbol_table_full under 512.
# Needs the OpenSSL development files (as the HTTPS steps do).
set -u
cd "$(dirname "$0")/../.."
MAKE=${MAKE:-make}
fails=0
ok()  { echo "  ok: $1"; }
bad() { echo "  FAIL: $1"; fails=$((fails + 1)); }
build() { $MAKE --no-print-directory "$@" 2>&1; }
q() { $MAKE -q --no-print-directory "$@" >/dev/null 2>&1; echo $?; }   # 0 up to date, 1 stale
bin() { if [ -f glon.exe ]; then echo glon.exe; else echo glon; fi; }
T=build/config-rebuild
mkdir -p "$T"

# Give every file newer than REF the timestamp of REF (a tie in any
# resolution).  REF is the output; the candidates are its configuration stamp,
# generated profile files and staged libraries.
tie() {
    ref=$1; shift
    find "$@" -type f -newer "$ref" 2>/dev/null | while IFS= read -r f; do touch -r "$ref" "$f"; done
}
modules() {   # "file http" / "file" / "http" / "none", from the binary's usage text
    u=$(./$(bin) --config-rebuild-check-unknown-option 2>&1 < /dev/null)
    m=""
    case "$u" in *--io-root*) m="file" ;; esac
    case "$u" in *--http-cert*) m="${m:+$m }http" ;; esac
    echo "${m:-none}"
}
# 700 distinct words (7 blocks of 100: the parser bounds one block's cells)
{
    printf 'probe: [\n'
    i=0
    while [ $i -lt 7 ]; do
        printf '  ['
        j=0
        while [ $j -lt 100 ]; do printf ' crc%d' $((i * 100 + j)); j=$((j + 1)); done
        printf ' ]\n'
        i=$((i + 1))
    done
    printf ']\n'
} > "$T/symbols.glon"
symbols() {   # "fits" when 700 more symbols fit (SYM_CAP 1,024), else "full"
    out=$(./$(bin) "$T/symbols.glon" 2>&1 < /dev/null)
    case "$out" in *symbol_table_full*) echo full ;; *) echo fits ;; esac
}
switch() {    # switch LABEL WANT-MODULES WANT-SYMBOLS MAKEARGS... (after a tie)
    label=$1; wm=$2; ws=$3; shift 3
    $MAKE -q --no-print-directory glon "$@" >/dev/null 2>&1   # rewrites .glon-config now
    tie "$(bin)" .glon-config build/profile glon-lib
    out=$(build glon "$@")
    case "$out" in *"-o glon"*) ok "$label: relinked after a same-timestamp stamp" ;;
        *) bad "$label: not relinked after a same-timestamp stamp: $(echo "$out" | tail -n 1)" ;; esac
    got=$(modules); [ "$got" = "$wm" ] && ok "$label: executable modules = $got" || bad "$label: executable modules = $got (want $wm)"
    got=$(symbols); [ "$got" = "$ws" ] && ok "$label: executable symbol capacity: 700 more $got" || bad "$label: 700 more symbols $got (want $ws)"
}

# ---- 1. the CI-1 sequence: http/constrained-notask -> file,http/server ---
build build/profile/server/glon_profile.h MEM_PROFILE=server >/dev/null || { echo "server profile generation failed"; exit 1; }
# In CI the module libraries are already staged by earlier steps; a library
# copied during the switch would itself force the relink and hide the defect.
build glon-lib/file.glon glon-lib/file-stream.glon glon-lib/http.glon >/dev/null || { echo "library staging failed"; exit 1; }
build glon MODULES=http MEM_PROFILE=constrained-notask >/dev/null || { echo "http/constrained-notask build failed"; exit 1; }
[ "$(modules)" = "http" ] && ok "1 start: http/constrained-notask executable" || bad "1 start: modules $(modules)"
switch "1 file,http/server" "file http" fits MODULES=file,http MEM_PROFILE=server

# ---- 2. unchanged configuration: nothing rebuilt, even with a tie --------
out=$(build glon MODULES=file,http MEM_PROFILE=server)
case "$out" in *"-o glon"*) bad "2 unchanged configuration relinked" ;; *) ok "2 unchanged configuration builds nothing" ;; esac
tie "$(bin)" .glon-config build/profile glon-lib
[ "$(q glon MODULES=file,http MEM_PROFILE=server)" = 0 ] && ok "2 unchanged configuration with a tie: up to date" || bad "2 unchanged configuration with a tie: stale"

# ---- 3. removing modules and returning to the 64K reference profile ------
switch "3 none/constrained" none full MODULES=

# ---- 4. a profile-only change (same modules) -----------------------------
switch "4 none/server" none fits MODULES= MEM_PROFILE=server

# ---- 5. CivetWeb compiler flags ------------------------------------------
# The probe flag is appended to the caller's OPENSSL_CFLAGS, never substituted
# for it: macOS CI exports Homebrew's OpenSSL include directory there and
# civetweb.c does not compile without it (GLON-CORE-CI-1C).  The caller's
# setting is restored by building with no command-line OPENSSL_CFLAGS at all,
# so the restored build and the final up-to-date check use exactly the
# environment's state (set, empty or unset).  A failed compile is a failure.
build third_party/civetweb/civetweb.o >/dev/null || bad "5 civetweb build failed"
X=-DGLON_CONFIG_REBUILD_CHECK
base=${OPENSSL_CFLAGS-}
probe="${base:+$base }$X"
for step in probe restore; do
    if [ "$step" = probe ]; then
        label="5 OPENSSL_CFLAGS+=$X"
        set -- OPENSSL_CFLAGS="$probe"
    else
        label="5 OPENSSL_CFLAGS restored"
        set --
    fi
    $MAKE -q --no-print-directory third_party/civetweb/civetweb.o "$@" >/dev/null 2>&1
    tie third_party/civetweb/civetweb.o build/civetweb.flags
    out=$(build third_party/civetweb/civetweb.o "$@")
    rc=$?
    if [ "$rc" -ne 0 ]; then
        bad "$label: civetweb.o compilation failed (make exit $rc): $(echo "$out" | tail -n 3 | tr '\n' ' ')"
        continue
    fi
    case "$out" in *"-c third_party/civetweb/civetweb.c"*) ok "$label: civetweb.o recompiled after a same-timestamp stamp" ;;
        *) bad "$label: civetweb.o not recompiled after a same-timestamp stamp" ;; esac
    rec=$(cat build/civetweb.built 2>/dev/null)
    if [ -z "$rec" ]; then bad "$label: no build record (build/civetweb.built)"; continue; fi
    case "$step:$rec" in
        probe:*"$X"*) ;;
        probe:*) bad "$label: build record lacks $X"; continue ;;
        restore:*"$X"*) bad "$label: build record still has $X"; continue ;;
    esac
    if [ -n "$base" ]; then
        case "$rec" in *"$base"*) ;; *) bad "$label: build record lacks the caller's OPENSSL_CFLAGS ($base)"; continue ;; esac
    fi
    ok "$label: build record has the expected flags"
done
[ "$(q third_party/civetweb/civetweb.o)" = 0 ] && ok "5 caller's flags restored: up to date" || bad "5 caller's flags restored: still stale"

build glon MODULES= >/dev/null   # leave the default build in place
if [ "$fails" -eq 0 ]; then echo "config-rebuild-check: PASS"; else echo "config-rebuild-check: FAIL ($fails)"; exit 1; fi
