#!/bin/sh
# GV-1A vocabulary experiment using the existing Glon CLI.
set -eu

VIEW=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
ROOT=$(CDPATH= cd -- "$VIEW/.." && pwd)
GLON="$ROOT/glon"
PRELUDE="$ROOT/jupyter/prelude.glon"
STRINGS="$ROOT/demo/shop/strings.glon"
EXT="$ROOT/demo/shop/strings-ext.glon"
VOCAB="$VIEW/glon/vocab.glon"
TMP=${TMPDIR:-/tmp}/gv-vocab-test.$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT

fail=0
check() {
    name=$1; expected=$2; expr=$3
    printf '[ %s ]\n' "$expr" > "$TMP/case.glon"
    out=$("$GLON" "$PRELUDE" "$STRINGS" "$EXT" "$VOCAB" "$TMP/case.glon" 2>/dev/null || true)
    if printf '%s\n' "$out" | grep -q -- "$expected"; then
        printf 'PASS %s\n' "$name"
    else
        printf 'FAIL %s expected=%s out=%s\n' "$name" "$expected" "$out"
        fail=1
    fi
}

check string_length 16 "r: dispatch string-vocab 'length \"123 Lambton Quay\" print r"
check field_text abc "v/field-text: \"abc\" s/print dispatch field-vocab 'text 0"
check field_length 3 "v/field-text: \"abc\" r: dispatch field-vocab 'length 0 print r"
check word_function_call 3 "b: reduce [ s/length ] f: get block-at b 1 print f \"abc\""
check block_function_call 0 "b: reduce [ :s/length ] f: block-at b 1 print f \"abc\""
check old_reference 1 "a: \"123\" b: a a: \"125\" print s/= b \"123\""
check utf8_bytes 6 "print s/length \"πππ\""

if [ "$fail" -ne 0 ]; then
    echo "FAILED: GV-1A vocabulary experiment"
    exit 1
fi
echo "PASS: GV-1A vocabulary experiment"
