#!/usr/bin/env bash
# scripts/ci/c2p-windows.sh - Windows MinGW-w64 (UCRT64) C2P checks.
#
# Runs the available executable smoke tests on Windows and separately records
# compile-only builds of the optional file/HTTP/SQLite/task configuration.
# The job timeout bounds the whole job; this script does not run `make test`
# (its NaSch simulator is Linux-bounded in c2p-linux.sh).
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

LOG_DIR="${CI_LOG_DIR:-ci-logs/c2p-windows}"
mkdir -p "$LOG_DIR"
SUMMARY="$LOG_DIR/summary.md"
FAIL=0
TOTAL=0
: > "$SUMMARY"
printf '| Step | Result | Kind | Log |\n|---|---|---|---|\n' >> "$SUMMARY"

glon_bin() {
    if [ -f ./glon.exe ]; then echo ./glon.exe; elif [ -x ./glon ]; then echo ./glon; else echo ./glon; fi
}

run_step() {
    name="$1"; kind="$2"; shift 2
    TOTAL=$((TOTAL + 1))
    echo "::group::$name"
    ( "$@" ) 2>&1 | tee "$LOG_DIR/$name.log"
    rc=${PIPESTATUS[0]}
    echo "::endgroup::"
    if [ "$rc" -eq 0 ]; then
        echo "PASS $name"
        printf '| %s | PASS | %s | `%s/%s.log` |\n' "$name" "$kind" "$LOG_DIR" "$name" >> "$SUMMARY"
    else
        echo "FAIL $name (exit $rc)"
        FAIL=$((FAIL + 1))
        printf '| %s | **FAIL** (exit %s) | %s | `%s/%s.log` |\n' "$name" "$rc" "$kind" "$LOG_DIR" "$name" >> "$SUMMARY"
    fi
}

conformance() {
    out="$("$(glon_bin)" glon-lib/strings-ext.glon ci/conformance.glon)"
    printf '%s\n' "$out"
    printf '%s\n' "$out" | grep -qx 'C15=576460752303423487'
    printf '%s\n' "$out" | grep -qx 'C16=-576460752303423488'
}

run_step frozen execute bash check-frozen-s1.sh
run_step build-minimal execute make glon MODULES=
run_step smoke-minimal execute "$(glon_bin)" examples/hello-d1.glon
run_step conformance execute conformance
m1_bin() {
    if [ -f ./m1-smoke.exe ]; then echo ./m1-smoke.exe; else echo ./m1-smoke; fi
}
run_step m1-smoke execute make m1-smoke
run_step m1-smoke-run execute "$(m1_bin)"
run_step json-j1 execute make json-j1-test
run_step io-file-windows execute make io-file-windows-test

# Compile-only checks for the restored optional modules.  They are not started
# as servers on Windows in this initial C2P configuration.
run_step build-file-http-sqlite compile-only make glon MODULES=file,http,sqlite
run_step build-task-server compile-only make glon MODULES=file,http,sqlite,task MEM_PROFILE=server-https

cat "$SUMMARY"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    cat "$SUMMARY" >> "$GITHUB_STEP_SUMMARY"
fi
printf '\nC2P_WINDOWS result steps=%s failures=%s\n' "$TOTAL" "$FAIL"
[ "$FAIL" -eq 0 ]
