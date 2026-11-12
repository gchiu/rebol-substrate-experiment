#!/bin/sh
# B13 profile selector: profile.sh s1-7|s1-12 [glon args...]
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case "${1:-}" in
  s1-7) shift; exec "$HERE/build/glon-s1-7" "$@" ;;
  s1-12) shift; exec "$HERE/build/glon-s1-12" "$@" ;;
  *) echo "usage: $0 s1-7|s1-12 [glon args...]" >&2; exit 2 ;;
esac
