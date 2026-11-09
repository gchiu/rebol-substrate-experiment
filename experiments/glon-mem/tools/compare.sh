#!/bin/sh
# tools/compare.sh -- the same per-definition accounting and per-operation
# timing for two library versions, on one harness (tools/profile.c, -O1).
#   sh tools/compare.sh variants/mem-v001.glon mem.glon
#   ALLOC=1 sh tools/compare.sh ...   managed cells allocated per operation
#                                     instead of time (the runtime's -DR0_S1_PROFILE)
set -e
cd "$(dirname "$0")/.."
mkdir -p build
PROF=build/profile
if [ "$ALLOC" = 1 ]; then PROF=build/profile-alloc; DEFS=-DR0_S1_PROFILE; fi
cc -std=c17 -O1 $DEFS -I../.. -o $PROF tools/profile.c ../../r0_s1_runtime.c ../../s1.c
for lib in "$@"; do
  d=build/defs-$(basename "$lib" .glon)
  rm -rf "$d"; python3 -I tools/split.py "$lib" "$d" > /dev/null
  n=$(ls "$d" | wc -l)
  echo "=== $lib"
  $PROF ../.. "$d" "$n" \
    "!r: mem/alloc 4000  q: mem/alloc 4000  s300: mem/to-string r 0 300" \
    "mem/byte r 5" "mem/set-byte r 5 7" "mem/cell r 5" "mem/set-cell r 5 -5" "mem/size r" \
    "s/at s300 5" \
    "K=30:mem/move r 0 q 0 256" "K=30:mem/move r 0 r 1 256" "K=30:mem/to-string r 0 256" "K=30:mem/from-string q 0 s300" \
    "K=10:mem/move r 0 q 0 3000" "K=10:mem/move r 0 r 1 3000" \
    | grep -v '^[0-9][0-9]  '
done
