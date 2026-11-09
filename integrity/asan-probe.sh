#!/bin/sh
# integrity/asan-probe.sh PROBE -- run the masm label/reference table probe
# (integrity/asan_probe.c, an AddressSanitizer build) at and past each table's
# capacity. At capacity the block must assemble cleanly. Past it, the invariant
# is that the block is refused without an out-of-bounds write; today ASan
# reports a global-buffer-overflow (known defect K5: expected failure). If a
# repair makes the refusal clean, the check reports XPASS and fails, so that
# it is promoted to an ordinary check deliberately.
probe=$1; fail=0
run() {  # labels refs expect(ok|xfail) name
    out=$("$probe" "$1" "$2" 2>&1); rc=$?
    overflow=0; echo "$out" | grep -q "global-buffer-overflow" && overflow=1
    if [ "$3" = ok ]; then
        if [ $rc -eq 0 ] && [ $overflow -eq 0 ]; then echo "  ok: $4"
        else echo "  FAIL: $4"; echo "$out" | head -5; fail=1; fi
    else
        if [ $overflow -eq 1 ]; then echo "  xfail (known defect): $4"
        elif [ $rc -eq 0 ]; then echo "  XPASS (known defect now fixed: promote this check): $4"; fail=1
        else echo "  FAIL (other error): $4"; echo "$out" | head -5; fail=1; fi
    fi
}
echo "== K5: masm label/reference tables (AddressSanitizer)"
run 64 0 ok    "K5: a block with 64 labels (the table's capacity) assembles with no out-of-bounds access"
run 0 128 ok   "K5: a block with 128 references (the table's capacity) assembles with no out-of-bounds access"
run 65 0 xfail "K5: a block with 65 labels is refused without writing past raw_labels[64]"
run 0 129 xfail "K5: a block with 129 references is refused without writing past raw_refs[128]"
exit $fail
