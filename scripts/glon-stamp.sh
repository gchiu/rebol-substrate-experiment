#!/bin/sh
# scripts/glon-stamp.sh FILE TEXT... (GLON-CORE-C2)
# Write TEXT to FILE only when it differs from FILE's current contents, so the
# file's modification time changes exactly when the recorded build
# configuration changes.  Make rules that depend on FILE then rebuild after a
# configuration change and are left alone when nothing changed.
#
# scripts/glon-stamp.sh --differs FILE TEXT... (GLON-CORE-CI-1A)
# Print "differs" when FILE is missing or its contents are not TEXT; print
# nothing otherwise; never write.  FILE is a build record written after a
# successful build, so this compares the configuration an output was really
# built with against the requested one by content, independently of file
# timestamps and their resolution.
set -eu
if [ "$1" = "--differs" ]; then
    shift
    f=$1
    shift
    t="$*"
    if [ -f "$f" ] && [ "$(cat "$f")" = "$t" ]; then
        exit 0
    fi
    echo differs
    exit 0
fi
f=$1
shift
t="$*"
if [ -f "$f" ] && [ "$(cat "$f")" = "$t" ]; then
    exit 0
fi
mkdir -p "$(dirname "$f")"
printf '%s\n' "$t" > "$f.tmp"
mv "$f.tmp" "$f"
