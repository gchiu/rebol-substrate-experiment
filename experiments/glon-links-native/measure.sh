#!/bin/sh
# experiments/glon-links-native/measure.sh -- resource measurements (make measure):
# the host-backed walk (streaming and collecting links) against the materialised
# Glon DOM, on pages of about 100, 1,000 and 10,000 nodes, and the cost of
# keeping links. Native, the 65,536-cell machine, -O1.
W=build/glon-web
P=file://$(pwd)
show() { grep '^# \(materialise\|measure: \(Glon\|time\|host\|events\)\)' | sed 's/^# measure: /   /; s/^# materialise: /   materialised: /'; }

for n in 100 1000 10000; do
    echo "== gen-$n.html (about $n nodes, 10 links, a 20-level chain)"
    echo "-- host-backed, streaming"; $W --measure links.glon $P/build/pages/gen-$n.html | show
    echo "-- host-backed, collecting the links"; $W --measure --collect links.glon $P/build/pages/gen-$n.html | show
    echo "-- materialised: the whole document as Glon blocks, then lk/extract"; $W --materialise links.glon $P/build/pages/gen-$n.html 2>/dev/null | show
done

echo "== the cost of KEEPING links: tests/close-page.html (104 links, 829 events)"
echo "-- streaming"; $W --measure links.glon $P/tests/close-page.html | show
echo "-- collecting"; $W --measure --collect links.glon $P/tests/close-page.html | show; echo "   (exit status $?)"
echo "== tests/small-page.html (the hand-built example of ../glon-dom-links: 10 elements, 3 links)"
echo "-- host-backed, streaming"; $W --measure links.glon $P/tests/small-page.html | show
echo "-- materialised"; $W --materialise links.glon $P/tests/small-page.html 2>/dev/null | show
echo "== the test page (97 nodes, 11 links)"
echo "-- host-backed, streaming"; $W --measure links.glon $P/tests/test-page.html | show
echo "-- materialised"; $W --materialise links.glon $P/tests/test-page.html 2>/dev/null | show
