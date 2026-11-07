#!/bin/sh
# experiments/glon-links-native/test.sh -- offline tests of glon-web (run by
# `make test`). Pages are read through file:// URLs; no network is used.
W=build/glon-web
P=file://$(pwd)
[ -x $W ] || { echo "build glon-web first (make)"; exit 2; }
[ -f build/pages/gen-10000.html ] || python3 gen_pages.py build/pages 100 1000 10000
fails=0
ok()   { echo "ok   $1"; }
bad()  { echo "FAIL $1"; fails=$((fails + 1)); }
# expect NAME OUTPUT-PATTERN COMMAND...: the command exits 0 and prints the pattern
expect() {
    name=$1; pat=$2; shift 2
    out=$("$@" 2>&1); rc=$?
    if [ $rc -eq 0 ] && printf '%s\n' "$out" | grep -q -- "$pat"; then ok "$name"; else bad "$name"; printf '%s\n' "$out" | tail -5; fi
}

# 1. every link of each page equals libxml2's own [href, text content], byte for byte
expect "test page: 11 links, nested text, entities, Unicode, empty links" \
       "PASS: Glon printed 11 links; libxml2's own list has 11" $W --check links.glon $P/tests/test-page.html
for n in 100 1000 10000; do
    expect "gen-$n: 10 links" "PASS: Glon printed 10 links; libxml2's own list has 10" $W --check links.glon $P/build/pages/gen-$n.html
done
expect "a page with two navs: 104 links" "PASS: Glon printed 104 links; libxml2's own list has 104" $W --check links.glon $P/tests/close-page.html

# 2. the readable output: nested elements give combined text
expect "nested <em> in a link" "/docs/primer	Glon primer" $W links.glon $P/tests/test-page.html
expect "nested <strong><span>" "/docs/laws	alpha laws" $W links.glon $P/tests/test-page.html
expect "collect mode gives the same links" "/docs/laws	alpha laws" $W --collect links.glon $P/tests/test-page.html

# 3. it/close: skipping <nav> subtrees, in the batch and at the host
expect "close inside a batch (test page): 9 links" "# 9 links" $W links.glon tests/skip-nav.glon $P/tests/test-page.html
expect "close across batches (a 100-link nav): 3 links" "# 3 links, 24 events" $W links.glon tests/skip-nav.glon $P/tests/close-page.html

# 4. handles: a forged/stale handle is refused; nothing is left open
expect "a stale handle is refused" "refused: stale or unknown handle" $W --measure links.glon tests/forged-handle.glon $P/tests/test-page.html
expect "the document is released" "leaked 0; handles rejected 1" $W --measure links.glon tests/forged-handle.glon $P/tests/test-page.html
expect "released after a normal walk" "documents open at most 1, leaked 0; handles rejected 0" $W --measure links.glon $P/tests/test-page.html

# 5. errors reach Glon as web-error events
expect "a missing file" "error: " $W links.glon $P/tests/no-such-page.html
expect "a refused scheme (ftp)" "error: " $W links.glon ftp://127.0.0.1/x.html

echo
if [ $fails -eq 0 ]; then echo "glon-links-native: all tests passed"; else echo "glon-links-native: $fails FAILED"; exit 1; fi
