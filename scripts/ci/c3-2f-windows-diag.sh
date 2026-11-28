#!/usr/bin/env bash
# scripts/ci/c3-2f-windows-diag.sh - TEMPORARY (GLON-C3-2F) Windows diagnosis of
# the task-runtime startup failure (run 46153543854: exit 127, no output).
#
# Diagnostic only: never fails (exit 0), changes no source, and writes
# everything under $1 (the uploaded artifact directory).  It inspects the
# task-runtime executables built by `make task-runtime-test`, runs them
# directly and through scripts/run-with-limit.sh, checks their DLL imports
# with cygcheck/objdump/ldd, records the MSYS2/MinGW environment, and compares
# with executables from the same toolchain that are known to run (lib1, the
# server-https site-capacity test).
set -u
OUT="${1:-ci-logs/c3-2f-diag}"
mkdir -p "$OUT"
LOG="$OUT/diag.txt"
: > "$LOG"
sec() { printf '\n===== %s =====\n' "$*" >> "$LOG"; }
run() { printf '$ %s\n' "$*" >> "$LOG"; ( "$@" ) >> "$LOG" 2>&1; printf '[exit %s]\n' "$?" >> "$LOG"; }

sec "environment"
run uname -a
printf 'MSYSTEM=%s\nMSYSTEM_PREFIX=%s\nCC=%s\nCFLAGS=%s\n' "${MSYSTEM:-}" "${MSYSTEM_PREFIX:-}" "${CC:-}" "${CFLAGS:-}" >> "$LOG"
printf 'PATH=%s\n' "$PATH" >> "$LOG"
run sh -c 'command -v cc gcc make sh cygcheck objdump ldd timeout'
run cc --version
run cc -dumpmachine
run cygcheck --version

sec "build/core-c3 contents"
run ls -la build/core-c3/
run sh -c 'ls -la build/core-c3/*.exe build/core-c3/task-* 2>&1'

sec "the build commands (make -n)"
run make --no-print-directory -n task-runtime-one MEM_PROFILE=server-https

# a working comparison from the same toolchain and profile machinery
sec "build comparison executables"
run make --no-print-directory lib1-one MEM_PROFILE=desktop-1m
run make --no-print-directory site-capacity-one MEM_PROFILE=server-https

inspect() {   # path-without-extension label
    local p="$1" exe
    exe="$p"; [ -f "$p.exe" ] && exe="$p.exe"
    sec "inspect $2: $exe"
    if [ ! -f "$exe" ]; then echo "MISSING: neither $p nor $p.exe exists" >> "$LOG"; return; fi
    run ls -la "$exe"
    run sh -c "wc -c < '$exe'"
    run sh -c "objdump -f '$exe' | head -20"
    run sh -c "objdump -p '$exe' | grep -iE 'DLL Name|^Characteristics|Subsystem|SizeOfStackReserve|SizeOfHeapReserve|SizeOfImage'"
    run sh -c "objdump -h '$exe' | grep -E ' \\.(text|data|bss|rdata|tls) '"
    run cygcheck "$exe"
    run ldd "$exe"
    sec "run $2 directly (foreground, root .)"
    printf '$ %s .\n' "$exe" >> "$LOG"
    timeout 900 "$exe" . > "$OUT/$2-direct.out" 2>&1
    rc=$?
    printf '[exit %s] output bytes %s\n' "$rc" "$(wc -c < "$OUT/$2-direct.out")" >> "$LOG"
    head -c 4000 "$OUT/$2-direct.out" >> "$LOG"; echo >> "$LOG"
    tail -n 5 "$OUT/$2-direct.out" >> "$LOG"
    sec "run $2 via cmd.exe (Windows loader without the MSYS exec path)"
    printf '$ cmd //c %s .\n' "$(cygpath -w "$exe")" >> "$LOG"
    timeout 900 cmd //c "$(cygpath -w "$exe")" . > "$OUT/$2-cmd.out" 2>&1
    printf '[exit %s] output bytes %s\n' "$?" "$(wc -c < "$OUT/$2-cmd.out")" >> "$LOG"
    tail -n 5 "$OUT/$2-cmd.out" >> "$LOG"
    sec "run $2 via scripts/run-with-limit.sh (the test launcher)"
    printf '$ sh scripts/run-with-limit.sh 900 %s .\n' "$p" >> "$LOG"
    sh scripts/run-with-limit.sh 900 "$p" . > "$OUT/$2-launcher.out" 2>&1
    printf '[exit %s] output bytes %s\n' "$?" "$(wc -c < "$OUT/$2-launcher.out")" >> "$LOG"
    tail -n 5 "$OUT/$2-launcher.out" >> "$LOG"
}
inspect build/core-c3/task-server-https task-server-https
inspect build/core-c3/task-server-https-noroot task-server-https-noroot
inspect build/core-v2/lib1-desktop-1m lib1-desktop-1m
inspect build/site-capacity-server-https site-capacity-server-https

sec "done"
exit 0
