#!/bin/sh
# desktop/make-big.sh -- generate the bulk-streaming test fixture.
# ~2 MB of ordinary text ending in a marker, so the proof can confirm the
# whole file arrived. This is test data, not part of the application.
set -e
out="$(dirname "$0")/big.txt"
awk 'BEGIN {
    for (i = 0; i < 33000; i++)
        printf "line %06d: the native host streams these bytes without Glon\n", i;
    printf "END-OF-BIG-FILE\n";
}' > "$out"
wc -c < "$out"
