#!/bin/sh
# scripts/glon-stamp.sh FILE TEXT... (GLON-CORE-C2)
# Write TEXT to FILE only when it differs from FILE's current contents, so the
# file's modification time changes exactly when the recorded build
# configuration changes.  Make rules that depend on FILE then rebuild after a
# configuration change and are left alone when nothing changed.
set -eu
f=$1
shift
t="$*"
if [ -f "$f" ] && [ "$(cat "$f")" = "$t" ]; then
    exit 0
fi
mkdir -p "$(dirname "$f")"
printf '%s\n' "$t" > "$f.tmp"
mv "$f.tmp" "$f"
