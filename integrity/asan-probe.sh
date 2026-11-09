#!/bin/sh
# integrity/asan-probe.sh PROBE -- the masm label/reference tables under
# AddressSanitizer (integrity/asan_probe.c), one block per process.
# At capacity a block must assemble; past it, it must be refused as too_large
# with nothing emitted, and in every case with no out-of-bounds access.
# (Known defect K5 until the 2026-11-10 repair: past capacity, ASan reported a
# global-buffer-overflow write.)
probe=$1; fail=0
run() {  # name expect(assembled|refused) args...
    name=$1; want=$2; shift 2
    out=$("$probe" "$@" 2>&1); rc=$?
    if echo "$out" | grep -q "AddressSanitizer"; then
        echo "  FAIL: $name (AddressSanitizer report)"; echo "$out" | grep -m3 "ERROR\|WRITE\|located"; fail=1; return
    fi
    if [ $rc -ne 0 ]; then echo "  FAIL: $name (exit $rc)"; echo "$out" | tail -3; fail=1; return; fi
    case "$want" in
        assembled) echo "$out" | grep -q ": assembled," || { echo "  FAIL: $name"; echo "$out" | tail -2; fail=1; return; } ;;
        refused)   echo "$out" | grep -q "refused (too_large), 0 cells emitted" || { echo "  FAIL: $name"; echo "$out" | tail -2; fail=1; return; } ;;
    esac
    echo "  ok: $name"
}
echo "== M (ASan): masm label/reference tables"
run "M-A1: 64 labels (capacity) assemble, no out-of-bounds access"            assembled 64 0
run "M-A2: 65 labels are refused as too_large, nothing emitted, no overflow"   refused 65 0
run "M-A3: 505 labels (a 510-token block) are refused, no overflow"          refused 505 0
run "M-A4: 128 ZBRANCH references (capacity) assemble"                         assembled 0 128
run "M-A5: 129 ZBRANCH references are refused, nothing emitted, no overflow"   refused 0 129
run "M-A6: 252 ZBRANCH references (a 510-token block) are refused"           refused 0 252
run "M-A7: 128 CALL references (capacity) assemble"                            assembled 0 128 call
run "M-A8: 129 CALL references are refused, nothing emitted, no overflow"      refused 0 129 call
exit $fail
