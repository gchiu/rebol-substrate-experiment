#!/bin/sh
# experiments/glon-dom-links/measure.sh -- what the link extractor costs on the
# unchanged 64K runtime, on top of the DOM iterator. Run by `make measure`.
set -e
R=../..
M=build/measure
MP=build/measure-prof
WALK="$R/jupyter/prelude.glon $R/demo/shop/strings.glon $R/demo/shop/strings-ext.glon ../glon-iter/iter.glon ../glon-dom/dom.glon"
LINKS="$WALK list-block.glon links.glon"
rep() { i=0; s=""; while [ $i -lt $2 ]; do s="$s$1 "; i=$((i + 1)); done; echo "$s"; }
L='l: reduce [ "li" none reduce [ reduce [ "a" reduce [ "href" "/n" ] reduce [ "n" ] ] ] ]'
UL40="$L ul: reduce [ \"ul\" none reduce [ $(rep l 40) ] ]"
RESET='lk/found: none lk/count: 0 lk/anchor: none lk/text: none lk/done: 0'

echo "== 1. bindings, loader-heap cells and masm code cells"
$M budget - - $LINKS page.glon

echo "== 2. managed heap"
echo "   the example page's data, and the libraries alone:"
$M retain 'x: 0' 'P: 0' $LINKS page.glon
$M retain 'x: 0' 'P: 0' $LINKS
echo "   kept alive WHILE extracting (pairs + chain), for 10 and 40 links:"
$M retain "$L ul: reduce [ \"ul\" none reduce [ $(rep l 10) ] ] $RESET" 'lk/run dom/walk ul 16' $LINKS
$M retain "$UL40 $RESET" 'lk/run dom/walk ul 16' $LINKS
echo "   kept alive by the RESULT (the chain released), for the page and 40 links:"
$M retain 'R: none' 'R: lk/extract page' $LINKS page.glon
$M retain "$UL40 R: none" 'R: lk/extract ul' $LINKS

echo "== 3. allocation per extraction (profile counters)"
K=10 $MP alloc 'R: none' 'R: lk/extract page' $LINKS page.glon
K=10 $MP alloc "$UL40 R: none" 'R: lk/extract ul' $LINKS
echo "   the bare walk of the page, for comparison (26 events)"
K=10 $MP alloc 'cnt: func [w n] [ either it/end? it/peek w [ none ] [ either = n 1 [ it/next w ] [ cnt w / n 2  cnt w - n / n 2 ] ] ]' 'cnt dom/walk page 32' $LINKS page.glon

echo "== 4. return stack: deepest probe recursion under which the step completes (84 with no step)"
$M stack 'x: 0' 'print 0' $LINKS page.glon
$M stack 'R: none' 'R: lk/extract page' $LINKS page.glon
$M stack "$UL40 R: none" 'R: lk/extract ul' $LINKS

echo "== 5. the most links one extraction can return (CLI)"
for pg in "" page.glon; do
    lo=1; hi=150
    while [ $lo -lt $hi ]; do
        mid=$(( (lo + hi + 1) / 2 ))
        printf '%s\nul: reduce [ "ul" none reduce [ %s ] ]\nprint block-len lk/extract ul\n' "$L" "$(rep l $mid)" > build/max.glon
        if build/glon $R/demo/shop/strings-ext.glon ../glon-iter/iter.glon ../glon-dom/dom.glon list-block.glon links.glon $pg build/max.glon 2>/dev/null | grep -qx "$mid"; then lo=$mid; else hi=$((mid - 1)); fi
    done
    echo "   with ${pg:-no page} loaded: $lo links"
done
