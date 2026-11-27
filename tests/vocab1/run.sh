#!/bin/sh
# tests/vocab1/run.sh -- GLON-VOCAB-1 regression: vocabularies and lexical
# binding through the native runner (each FILE argument is its own source).
# Usage: tests/vocab1/run.sh [GLON]   (default ./glon, from the repository root)
G=${1:-./glon}
D=tests/vocab1
fail=0
T=$(mktemp -d "${TMPDIR:-/tmp}/vocab1.XXXXXX")
check() {   # name expected-file files...
    name=$1; exp=$2; shift 2
    out=$("$G" "$@" 2>/dev/null | tail -1)
    if [ "$out" = "$(cat "$exp")" ]; then echo "vocab1 $name: ok"; else echo "vocab1 $name: FAIL (got '$out', wanted '$(cat "$exp")')"; fail=1; fi
}
known() {   # name expected-file files... : a recorded defect; reported, never fails the run
    name=$1; exp=$2; shift 2
    out=$("$G" "$@" 2>/dev/null | tail -1)
    if [ "$out" = "$(cat "$exp")" ]; then echo "vocab1 known-$name: NOW PASSES (update GLON-VIEW-C1-CONTRACT.md and make it a check)";
    else echo "vocab1 known-$name: still the recorded defect, got '$out' (GLON-VIEW-C1-CONTRACT.md)"; fi
}
check_err() {   # name expected-stderr-file file
    name=$1; exp=$2; shift 2
    if "$G" "$@" >/dev/null 2>"$T/err"; then echo "vocab1 $name: FAIL (loaded)"; fail=1;
    elif grep -qF "$(cat "$exp")" "$T/err"; then echo "vocab1 $name: ok (refused)"; else echo "vocab1 $name: FAIL ($(head -1 "$T/err"))"; fail=1; fi
}
check nested $D/nested.expect $D/nested.glon
check numbers $D/numbers.expect demo/shop/numbers.glon $D/numbers.glon
check reuse $D/reuse.expect $D/reuse.glon
check library-locals $D/app-k.expect $D/lib-k.glon $D/app-k.glon
check outer-mutation $D/outer.expect $D/outer.glon
check computed-set $D/computed-set.expect $D/computed-set.glon
check computed-spec $D/computed-spec.expect $D/computed-spec.glon
check reload $D/reload.expect $D/reload.glon $D/reload.glon
check load-order $D/order-b.expect $D/order-a.glon $D/order-b.glon
check later-global $D/later-global2.expect $D/later-global.glon $D/later-global2.glon
# GLON-VIEW-PREP-1: the string and number vocabularies against application
# globals named like their members; locals per function and activation;
# nested shadowing; explicit outer assignment across levels
check strings-vs-app $D/s-app.expect demo/shop/strings-ext.glon demo/shop/block.glon demo/shop/list.glon demo/shop/strings-text.glon $D/s-app.glon
check numbers-earlier-globals $D/n-post.expect $D/n-pre.glon demo/shop/numbers.glon $D/n-post.glon
check locals $D/locals-app.expect $D/locals-lib.glon $D/locals-app.glon
check nested-shadow $D/shadow.expect $D/shadow.glon
check outer-levels $D/outer2.expect $D/outer2.glon
known forward-relative $D/forward-relative.expect $D/forward-relative.glon
known forward-qualified $D/forward-qualified.expect $D/forward-qualified.glon
# the literal-block GC hazard (GLON-LANG-1 2.4), unresolved, and its control
check managed-block-gc $D/managed-block-gc.expect $D/managed-block-gc.glon
known literal-block-gc $D/literal-block-gc.expect $D/literal-block-gc.glon
for e in unknown define redefine dup global place setparam; do check_err "error-$e" $D/err-$e.experr $D/err-$e.glon; done
exit $fail
