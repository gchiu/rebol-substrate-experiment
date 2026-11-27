#!/bin/sh
# tests/core-v2/library_audit.sh - GLON-VIEW-CORE-V2 static library audit
# (make library-audit), over the sessions C2 and the shared libraries form:
#   lexaudit.py  function set-words whose binding differs between the old
#                run-time rule and GLON-VOCAB-1 (classes A/B/C/U): must be none;
#   defs.py      names defined by more than one source: only the deliberate
#                overrides listed in ALLOWED.
set -u
cd "$(dirname "$0")/../.."
status=0
B="jupyter/prelude.glon demo/shop/binding.glon demo/shop/strings.glon demo/shop/strings-ext.glon"
SH="demo/shop/case.glon demo/shop/block.glon demo/shop/list.glon demo/shop/strings-text.glon demo/shop/numbers.glon"
FILE="modules/file.glon modules/file-stream.glon"
KAKA="demos/kaka/kaka-lib.glon demos/kaka/kaka.glon demos/kaka/kaka-draw.glon demos/kaka/kaka-rangi.glon demos/kaka/kaka-wave.glon demos/kaka/kaka-selftest.glon demos/kaka/kaka-fighter.glon"
# Deliberate overrides are listed per session (the second argument):
#   io/u32, stream/*    experiments/masm2/u32.glon and masm-vocabulary/
#                       file-stream-masm.glon replace the file module's words
#   json/max-items      examples/https-g3a/server.glon sets the documented limit
#   block-set!          demos/kaka/kaka-lib.glon carries its own copy
STREAM="stream/close-both stream/close-error stream/emit3 stream/find-first stream/finish stream/replace-first stream/step stream/write-all"
session() { # session LABEL ALLOWED FILES...
    label=$1; ALLOWED=$2; shift 2
    la=$(python3 tests/core-v2/lexaudit.py . "$@" 2>&1) || { echo "  FAIL: $label: lexaudit.py failed: $la"; status=1; return; }
    n=$(echo "$la" | sed -n 's/.*divergence candidates: \([0-9]*\).*/\1/p')
    if [ "$n" = 0 ]; then echo "  ok: $label: no lexical-binding divergence ($(echo "$la" | head -n 1))"
    else echo "  FAIL: $label: $la"; status=1; fi
    dd=$(python3 tests/core-v2/defs.py . "$@" 2>&1) || { echo "  FAIL: $label: defs.py failed: $dd"; status=1; return; }
    dup=$(echo "$dd" | sed -n 's/^  \([^ ]*\) .*/\1/p')
    extra=""; for d in $dup; do case " $ALLOWED " in *" $d "*) ;; *) extra="$extra $d" ;; esac; done
    if [ -z "$extra" ]; then echo "  ok: $label: duplicate definitions only the deliberate:${dup:- none}" | tr '\n' ' '; echo
    else echo "  FAIL: $label: names defined twice:$extra"; status=1; fi
}
session "shared libraries" "" $B $SH
session "JSON" "" $B $SH modules/json.glon tests/json/tests.glon
session "file + replace" "" $B $SH $FILE modules/file-atomic.glon modules/file-replace.glon
session "file + MASM find/u32" "io/u32 $STREAM" $B $FILE experiments/masm-vocabulary/find.glon experiments/masm2/u32.glon experiments/masm-vocabulary/file-stream-masm.glon modules/file-atomic.glon modules/file-replace.glon
session "fetch" "" $B $FILE modules/fetch.glon modules/recipe-0004.glon
session "HTTPS G3A" "json/max-items" $B modules/http.glon modules/web.glon modules/json.glon examples/https-g3a/slots.glon examples/https-g3a/reg.glon examples/https-g3a/persist.glon examples/https-g3a/server.glon
session "HTTPS G3" "" $B modules/http.glon examples/https-g3/webparse.glon examples/https-g3/server.glon
session "HTTPS examples 1-4" "" $B modules/http.glon modules/json.glon demo/shop/https-examples/1-hello.glon
session "Kaka" "" jupyter/prelude.glon demo/shop/binding.glon demo/shop/strings.glon $KAKA
session "Kaka + shared libraries" "block-set!" $B $SH $KAKA
[ $status -eq 0 ] && echo "library-audit: PASS" || echo "library-audit: FAIL"
exit $status
