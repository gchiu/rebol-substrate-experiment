#!/bin/sh
# scripts/run-with-limit.sh SECONDS COMMAND [ARG...] (GLON-CORE-CI-1C)
#
# Run COMMAND with a wall-clock limit using only POSIX sh, sleep and kill, so
# test recipes do not depend on GNU coreutils `timeout` (absent on macOS).
# Exit status: COMMAND's own status when it finishes within the limit; 124
# (as GNU timeout) when the limit expires, after which COMMAND is sent TERM
# and, if it is still running 2 seconds later, KILL.
#
# The watchdog polls once per second and exits as soon as COMMAND has ended,
# so a command that finishes early is not held for the full limit.  COMMAND
# runs as an asynchronous list: its standard input is /dev/null.
set -u
if [ $# -lt 2 ]; then
    echo "usage: run-with-limit.sh SECONDS COMMAND [ARG...]" >&2
    exit 2
fi
limit=$1
shift
case "$limit" in
    ''|*[!0-9]*) echo "run-with-limit.sh: SECONDS must be a whole number: $limit" >&2; exit 2 ;;
esac

"$@" &
cmd=$!
(
    i=0
    while [ "$i" -lt "$limit" ]; do
        sleep 1
        kill -0 "$cmd" 2>/dev/null || exit 0
        i=$((i + 1))
    done
    kill -0 "$cmd" 2>/dev/null || exit 0
    echo "run-with-limit.sh: '$1' exceeded ${limit}s; terminated" >&2
    kill -TERM "$cmd" 2>/dev/null
    sleep 2
    kill -KILL "$cmd" 2>/dev/null
    exit 124
) &
dog=$!
wait "$cmd"
rc=$?
wait "$dog"
if [ $? -eq 124 ]; then
    exit 124
fi
exit "$rc"
