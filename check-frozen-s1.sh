#!/usr/bin/env bash
# check-frozen-s1.sh - mechanically enforce the frozen S1 substrate.
#
# Verifies that the tag s1-frozen-v1 points at the approved freeze commit and
# that the frozen substrate files have not changed since that commit.
#
# Exits 0 (with a success message) iff both hold; otherwise exits 1 with a
# clear error.

set -u

EXPECTED="f90496c26dc45c7a387d8fc8639cd2781507d3d2"
FROZEN_FILES=(
    s1.c
    s1.h
    tests.c
    adversarial.c
    claims.c
)

fail() {
    echo "ERROR: $1" >&2
    exit 1
}

# 1. The tag must resolve to the exact approved freeze commit.
if ! resolved="$(git rev-parse 's1-frozen-v1^{commit}' 2>/dev/null)"; then
    fail "tag 's1-frozen-v1' does not exist or cannot be resolved"
fi
if [ "$resolved" != "$EXPECTED" ]; then
    fail "tag 's1-frozen-v1' resolves to $resolved, expected $EXPECTED"
fi

# 2. The frozen files must be byte-for-byte unchanged since the tag.
if ! git diff --quiet "$EXPECTED" -- "${FROZEN_FILES[@]}"; then
    echo "ERROR: frozen S1 files differ from tag 's1-frozen-v1' ($EXPECTED):" >&2
    git diff --stat "$EXPECTED" -- "${FROZEN_FILES[@]}" >&2
    fail "frozen substrate modified"
fi

echo "OK: S1 substrate frozen at $EXPECTED; all frozen files unchanged."
