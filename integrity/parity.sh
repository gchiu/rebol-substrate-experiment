#!/bin/sh
# integrity/parity.sh MODE [WASM NODE] -- the Glon-level machine-integrity
# files, each in a fresh session, compared with integrity/expected/NAME.out.
#   native     runs ./glon (MODE native: from the repository root)
#   wasm       runs WASM under NODE with integrity/run-wasm.js
# Only lines starting "parity:" are compared, because the two hosts report
# outcomes differently (./glon prints the file's last value and its outcome;
# glon_load's return code does not distinguish a halted run, see
# MACHINE-INTEGRITY-AUDIT.md). The files that fail on purpose print a line
# before the failure point and one after it that must not appear.
mode=$1; wasm=$2; node=$3; fail=0
LIBS="jupyter/prelude.glon demo/shop/strings.glon demo/shop/strings-ext.glon"
for name in binary-strings gc-values stack-ok stack-exhaust stack-underflow masm-unwrapped masm-limits masm-refused masm-invalid; do
    if [ "$mode" = native ]; then
        out=$(./glon demo/shop/strings-ext.glon integrity/$name.glon 2>/dev/null | grep '^parity:')
    else
        out=$("$node" integrity/run-wasm.js "$wasm" $LIBS integrity/$name.glon 2>/dev/null | grep '^parity:')
    fi
    if [ "$out" = "$(cat integrity/expected/$name.out)" ]; then
        echo "  ok: $mode $name: output as expected ($(echo "$out" | grep -c .) lines)"
    else
        echo "  FAIL: $mode $name"; echo "--- expected"; cat integrity/expected/$name.out; echo "--- got"; echo "$out"; fail=1
    fi
done
exit $fail
