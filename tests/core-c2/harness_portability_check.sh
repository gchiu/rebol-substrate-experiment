#!/bin/sh
# tests/core-c2/harness_portability_check.sh - GLON-CORE-CI-1C regression
# (make harness-portability-check).
#
# The two test-harness defects of the CI-1B macOS runs:
#   A/B  lib1-test called GNU `timeout`, which macOS does not have
#        ("timeout: command not found", exit 127).  It now uses
#        scripts/run-with-limit.sh (POSIX sh, sleep, kill), still 900 s.
#   C    config_rebuild_check.sh case 5 replaced the caller's OPENSSL_CFLAGS
#        (macOS CI: Homebrew's include directory, which civetweb.c needs) and
#        ignored a failed compile.
# C simulates the macOS condition on any host: OPENSSL_CFLAGS gains a marker
# flag and CC is a wrapper that refuses to compile civetweb.c without it, as a
# missing <openssl/ssl.h> would.  The caller's own OPENSSL_CFLAGS is kept.
# Needs the OpenSSL development files (as config-rebuild-check does).
set -u
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
MAKE=${MAKE:-make}
fails=0
ok()  { echo "  ok: $1"; }
bad() { echo "  FAIL: $1"; fails=$((fails + 1)); }
T=build/harness-portability
mkdir -p "$T"
RL="sh scripts/run-with-limit.sh"

# ---- A. scripts/run-with-limit.sh ------------------------------------------
out=$($RL 5 sh -c 'echo to-stdout; echo to-stderr >&2; exit 3' 2>&1); rc=$?
[ "$rc" = 3 ] && ok "A command status passed through (3)" || bad "A command status: got $rc, want 3"
case "$out" in *to-stdout*to-stderr*|*to-stderr*to-stdout*) ok "A command output passed through" ;;
    *) bad "A command output lost: $out" ;; esac

t0=$(date +%s); $RL 900 true; rc=$?; t1=$(date +%s)
[ "$rc" = 0 ] && ok "A quick command: status 0" || bad "A quick command: status $rc"
[ $((t1 - t0)) -le 5 ] && ok "A quick command under a 900 s limit returned in $((t1 - t0)) s" \
    || bad "A quick command under a 900 s limit took $((t1 - t0)) s (watchdog held the call)"

t0=$(date +%s); $RL 2 sh -c "echo \$\$ > $T/pid; exec sleep 30" 2>/dev/null; rc=$?; t1=$(date +%s)
[ "$rc" = 124 ] && ok "A overrun: status 124" || bad "A overrun: status $rc, want 124"
[ $((t1 - t0)) -le 10 ] && ok "A overrun stopped after $((t1 - t0)) s (limit 2 s)" || bad "A overrun took $((t1 - t0)) s"
kill -0 "$(cat $T/pid)" 2>/dev/null && bad "A overrun: command still running" || ok "A overrun: command terminated"

t0=$(date +%s); $RL 2 sh -c "trap '' TERM; echo \$\$ > $T/pid; exec sleep 30" 2>/dev/null; rc=$?; t1=$(date +%s)
[ "$rc" = 124 ] && ok "A TERM ignored: status 124" || bad "A TERM ignored: status $rc, want 124"
[ $((t1 - t0)) -le 12 ] && ok "A TERM ignored: killed after $((t1 - t0)) s" || bad "A TERM ignored: took $((t1 - t0)) s"
kill -0 "$(cat $T/pid)" 2>/dev/null && bad "A TERM ignored: command still running" || ok "A TERM ignored: command killed"

$RL 2>/dev/null; [ $? = 2 ] && ok "A usage error: status 2" || bad "A usage error: status not 2"
$RL x true 2>/dev/null; [ $? = 2 ] && ok "A non-numeric limit: status 2" || bad "A non-numeric limit: status not 2"

# ---- B. lib1-test without GNU timeout ----------------------------------------
grep -nE '(^|[^-[:alnum:]_])timeout [0-9]' Makefile > "$T/makefile-timeout.txt"
[ -s "$T/makefile-timeout.txt" ] && bad "B Makefile recipes still call timeout: $(cat $T/makefile-timeout.txt)" \
    || ok "B no Makefile recipe calls timeout"
grep -q 'run-with-limit.sh 900 ./build/core-v2/lib1-' Makefile && ok "B lib1-test keeps its 900 s limit" \
    || bad "B lib1-test does not run under run-with-limit.sh 900"
mkdir -p "$T/no-timeout"
printf '#!/bin/sh\necho "timeout: command not found (harness-portability stub)" >&2\nexit 127\n' > "$T/no-timeout/timeout"
chmod +x "$T/no-timeout/timeout"
out=$(PATH="$ROOT/$T/no-timeout:$PATH" $MAKE --no-print-directory lib1-test LIB1_PROFILES=constrained 2>&1); rc=$?
case "$out" in *"harness-portability stub"*) bad "B lib1-test called timeout" ;; esac
[ "$rc" = 0 ] && ok "B lib1-test passes with timeout unavailable" || bad "B lib1-test with timeout unavailable: exit $rc: $(echo "$out" | tail -n 3)"

# ---- C. config_rebuild_check.sh under a required OPENSSL_CFLAGS --------------
REAL_CC=${CC:-cc}
mkcc() {   # mkcc NAME CASE-PATTERN-THAT-FAILS (civetweb.c compiles only)
    cat > "$T/$1" <<EOF
#!/bin/sh
case " \$* " in
  *civetweb.c*)
    case " \$* " in
      $2) echo "$1: simulated missing <openssl/ssl.h>" >&2; exit 1 ;;
    esac ;;
esac
exec $REAL_CC "\$@"
EOF
    chmod +x "$T/$1"
}
M=-DGLON_HARNESS_OPENSSL_ENV
mkcc cc-require-env "*\" $M \"*) ;; *"
mkcc cc-reject-probe "*\" -DGLON_CONFIG_REBUILD_CHECK \"*"
ENV_FLAGS="${OPENSSL_CFLAGS:+$OPENSSL_CFLAGS }$M"

out=$(OPENSSL_CFLAGS="$ENV_FLAGS" CC="$ROOT/$T/cc-require-env" sh tests/core-c2/config_rebuild_check.sh 2>&1); rc=$?
echo "$out" > "$T/config-rebuild-env.log"
[ "$rc" = 0 ] && ok "C config-rebuild-check passes when civetweb.c needs the caller's OPENSSL_CFLAGS" \
    || bad "C config-rebuild-check with a required OPENSSL_CFLAGS: exit $rc: $(echo "$out" | grep FAIL | head -n 3)"

out=$(OPENSSL_CFLAGS="$ENV_FLAGS" CC="$ROOT/$T/cc-require-env" $MAKE -q --no-print-directory third_party/civetweb/civetweb.o >/dev/null 2>&1; echo $?)
[ "$out" = 0 ] && ok "C caller's OPENSSL_CFLAGS restored: civetweb.o up to date" || bad "C caller's OPENSSL_CFLAGS not restored (make -q $out)"

out=$(CC="$ROOT/$T/cc-reject-probe" sh tests/core-c2/config_rebuild_check.sh 2>&1); rc=$?
echo "$out" > "$T/config-rebuild-reject.log"
[ "$rc" != 0 ] && ok "C failed probe compile fails config-rebuild-check (exit $rc)" || bad "C failed probe compile: config-rebuild-check passed"
case "$out" in *"civetweb.o compilation failed"*) ok "C failed probe compile reported explicitly" ;;
    *) bad "C failed probe compile not reported" ;; esac

# leave the host's default build in place
$MAKE --no-print-directory third_party/civetweb/civetweb.o >/dev/null 2>&1 || bad "restoring civetweb.o failed"
$MAKE --no-print-directory glon MODULES= >/dev/null 2>&1 || bad "restoring glon failed"

if [ "$fails" -eq 0 ]; then echo "harness-portability-check: PASS"; else echo "harness-portability-check: FAIL ($fails)"; exit 1; fi
