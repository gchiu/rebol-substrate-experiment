#!/usr/bin/env bash
# glon-smoke.sh - prove the native glon artifact runs outside the source tree.
#
# Copies the built glon binary and its glon-lib/ directory to a temporary
# directory, runs a program that exercises arithmetic, persistent definitions
# and the STRING! library, then checks parse, SIN! and machine-halt reporting.
# Requires `make glon` first (the glon-smoke Makefile target does that).

set -eu

ROOT="$(cd "$(dirname "$0")" && pwd)"
DIST="$(mktemp -d "${TMPDIR:-/tmp}/glon-smoke.XXXXXX")"
trap 'rm -rf "$DIST"' EXIT

cp "$ROOT/glon" "$DIST/glon"
cp -r "$ROOT/glon-lib" "$DIST/glon-lib"

# arithmetic + persistent definition + STRING! library (prelude + strings)
cat > "$DIST/program.glon" <<'GLON'
;; arithmetic, a definition, and the STRING! library
x: 21
values [* x 2  s/+ "glo" "n"]
GLON
(cd "$DIST" && ./glon program.glon > out.txt 2> err.txt)
grep -q '^42 "glon"$' "$DIST/out.txt"

# parse error: nonzero exit, clear parse-error report on stderr
printf 'y: [ oops\n' > "$DIST/bad-parse.glon"
if (cd "$DIST" && ./glon bad-parse.glon > /dev/null 2> err.txt); then
    echo "glon-smoke: expected a parse error" >&2; exit 1
fi
grep -q 'parse error' "$DIST/err.txt"

# uncaught SIN!: nonzero exit, SIN! report on stderr
printf "raise create-sin 'demo 'oops 7\n" > "$DIST/sin.glon"
if (cd "$DIST" && ./glon sin.glon > /dev/null 2> err.txt); then
    echo "glon-smoke: expected an uncaught SIN!" >&2; exit 1
fi
grep -q 'SIN!' "$DIST/err.txt"

# machine-level fail-stop: nonzero exit, halt report on stderr
printf 'undefined-word\n' > "$DIST/halt.glon"
if (cd "$DIST" && ./glon halt.glon > /dev/null 2> err.txt); then
    echo "glon-smoke: expected a machine halt" >&2; exit 1
fi
grep -q 'halted' "$DIST/err.txt"

echo "glon-smoke: PASS (arithmetic/definitions/STRING! + parse/SIN!/halt)"
