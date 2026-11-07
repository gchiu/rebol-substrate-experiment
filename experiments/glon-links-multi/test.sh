#!/bin/sh
# experiments/glon-links-multi/test.sh -- two independent extractors, interleaved
# batch by batch in one Glon machine (run by `make test`). Offline: file:// pages.
N=../glon-links-native
W=$N/build/glon-web
[ -x $W ] || { echo "build $N first (make -C $N)"; exit 2; }
B=file://$(pwd); P=file://$(cd $N && pwd)
mkdir -p build
fails=0
single() { $W --raw $N/links.glon "$1" > "build/exp-$2.out" 2>&1 && \
           $W --check $N/links.glon "$1" | grep -q '^PASS'; }
# run NAME MIN-SWITCHES [stream.glon] URL-A URL-B
run() {
    name=$1; min=$2; shift 2; extra=""
    if [ "$1" = stream ]; then extra=stream.glon; shift; fi
    single "$1" a && single "$2" b || { echo "FAIL $name: single-extractor reference"; fails=$((fails + 1)); return; }
    $W --measure multi.glon $extra "$1 $2" > build/multi.out 2>&1; rc=$?
    echo "$name"
    if [ $rc -eq 0 ] && python3 check.py build/multi.out build/exp-a.out build/exp-b.out $min \
       && grep -q 'documents open at most 2, leaked 0' build/multi.out; then
        echo "ok   $name"; grep '^# measure: Glon\|^# measure: events' build/multi.out | sed 's/^# measure:/    /'
    else echo "FAIL $name (exit $rc)"; tail -3 build/multi.out; fails=$((fails + 1)); fi
}
run "1. kept results: small page (A) and doc-b (B)" 2 $P/tests/small-page.html $B/doc-b.html
run "2. kept results, the other way round: doc-a (A) and small page (B)" 2 $B/doc-a.html $P/tests/small-page.html
run "3. two long documents, streamed: doc-a and doc-b" 40 stream $B/doc-a.html $B/doc-b.html
run "4. the SAME document twice, streamed: each job finds it once" 40 stream $B/doc-b.html $B/doc-b.html
run "5. the test page (entities, Unicode, nested markup) and doc-a, streamed" 10 stream $P/tests/test-page.html $B/doc-a.html
echo
if [ $fails -eq 0 ]; then echo "glon-links-multi: all tests passed"; else echo "glon-links-multi: $fails FAILED"; exit 1; fi
