#!/bin/sh
# apps/glon-fetch/run-glon-fetch.sh -- one-click launcher for Glon Fetch (POSIX).
# Run this file to start the application. No command line needed.
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
cd "$root" || { echo "could not find the repository root"; exit 1; }
if [ ! -x ./glon-desktop ]; then
    echo "glon-desktop was not found. Build it first with:"
    echo "    make glon-desktop desktop-wasm"
    exit 1
fi
echo "Launching Glon Fetch... (close to stop)"
exec ./glon-desktop --install org.glon.fetch "$@"
