#!/bin/sh
# tests/core-v2/modules_check.sh - GLON-VIEW-CORE-V2: C2's modules under
# GLON-VOCAB-1 (make modules-check).
# 1. modules/strings.glon (served to the browser as /strings.glon) is the
#    shared s vocabulary: identical to demo/shop/strings.glon, and loads and
#    works on its own;
# 2. every C2 module stack loads in a bare session without a refusal, in the
#    profile it is built for (file: constrained; HTTP: server);
# 3. s/replace-first (modules/file-replace.glon, an s member) keeps its results.
set -u
cd "$(dirname "$0")/../.."
MAKE=${MAKE:-make}
status=0
ok() { echo "  ok: $1"; }
bad() { echo "  FAIL: $1"; status=1; }
cmp -s modules/strings.glon demo/shop/strings.glon && ok "modules/strings.glon == demo/shop/strings.glon" \
    || bad "modules/strings.glon differs from demo/shop/strings.glon"
for p in constrained server; do $MAKE --no-print-directory libload-one MEM_PROFILE=$p >/dev/null 2>&1 || { bad "libload $p build"; exit 1; }; done
L=build/core-v2
printf 'values [ s/length s/+ "glo" "n"  s/= "a" "a"  judge [ s/length 5 ] ]\n' > $L/strings-mod.glon
got=$($L/libload-constrained -q jupyter/prelude.glon modules/strings.glon $L/strings-mod.glon 2>&1 | tail -n 1)
case "$got" in "RESULT 4 1 "*"type"*) ok "modules/strings.glon alone: $got" ;; *) bad "modules/strings.glon alone: '$got'" ;; esac
B="jupyter/prelude.glon demo/shop/binding.glon demo/shop/strings.glon demo/shop/strings-ext.glon"
stack() { # stack PROFILE LABEL FILES...
    p=$1; label=$2; shift 2
    if out=$($L/libload-$p -q $B "$@" 2>&1); then ok "$label loads ($p)"; else bad "$label ($p): $(echo "$out" | head -n 3)"; fi; }
stack constrained "file stack" modules/file.glon modules/file-stream.glon modules/file-atomic.glon modules/file-replace.glon
stack constrained "file stack + masm find/u32" modules/file.glon modules/file-stream.glon experiments/masm-vocabulary/find.glon experiments/masm2/u32.glon experiments/masm-vocabulary/file-stream-masm.glon modules/file-atomic.glon modules/file-replace.glon
stack constrained "fetch + recipe-0004" modules/file.glon modules/file-stream.glon modules/fetch.glon modules/recipe-0004.glon
stack server "http + web + json" modules/http.glon modules/web.glon modules/json.glon
stack server "https-g3a register (slots, reg)" modules/http.glon modules/web.glon modules/json.glon examples/https-g3a/slots.glon examples/https-g3a/reg.glon
stack server "https-g3 server" modules/http.glon examples/https-g3/webparse.glon
printf 'values [ s/replace-first "the web is wide" "web" "rebol"  s/replace-first "web web" "web" "x"  s/replace-first "none here" "web" "x" ]\n' > $L/replace.glon
got=$($L/libload-constrained -q $B modules/file.glon modules/file-stream.glon modules/file-replace.glon $L/replace.glon 2>&1 | tail -n 1)
[ "$got" = 'RESULT "the rebol is wide" "x web" "none here"' ] && ok "s/replace-first: $got" || bad "s/replace-first: '$got'"
[ $status -eq 0 ] && echo "modules-check: PASS" || echo "modules-check: FAIL"
exit $status
