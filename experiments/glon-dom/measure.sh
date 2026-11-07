#!/bin/sh
# experiments/glon-dom/measure.sh -- GLON-DOM v001 resource measurements on the
# unchanged 64K runtime. Run by `make measure` (which builds build/glon,
# build/measure and build/measure-prof first; measure.c is GLON-ITER v001's, only
# read). Prints the figures quoted in GLON-DOM-V001.md.
set -e
R=../..
M=build/measure
MP=build/measure-prof
BASE="$R/jupyter/prelude.glon $R/demo/shop/strings.glon $R/demo/shop/strings-ext.glon ../glon-iter/iter.glon dom.glon"
FN="$BASE variants/dom-fn.glon"
RW="$BASE variants/rw.glon"

# fixtures: CHAIN n is a literal n-level chain (element-only, in the loader heap);
# WIDE is one element with 50 empty children; TXT one element with 50 text children
chain() {
    i=0; s="deep: "
    while [ $i -lt $(($1 - 1)) ]; do s="$s[ n$((i % 60)) none [ "; i=$((i + 1)); done
    s="$s[ z none none ] "
    i=0; while [ $i -lt $(($1 - 1)) ]; do s="$s] ] "; i=$((i + 1)); done
    echo "$s"
}
rep() { i=0; s=""; while [ $i -lt $2 ]; do s="$s$1 "; i=$((i + 1)); done; echo "$s"; }
TREE='tree: [ document none [ [ a none [ [ b none none ] [ c none [] ] [ d none [ [ e none none ] ] ] [ f none none ] ] ] ] ]'
WIDE="wide: [ w none [ $(rep '[ e none none ]' 50) ] ]"
TXT="x: \"t\" txt: reduce [ 'p none reduce [ $(rep x 50) ] ]"
C60=$(chain 60)
DOWN='down: func [n] [ either = n 0 [ 0 ] [ it/next D  down - n 1 ] ]'

echo "== 1. bindings, loader-heap cells and masm code cells"
$M budget - - $BASE
echo "== 1b. with the measured alternative and the recursive baseline"
$M budget - - $BASE variants/dom-fn.glon variants/rw.glon

echo "== 2. managed-heap cells kept alive"
$M retain "$TREE" 'D: dom/walk tree' $BASE
$M retain "$TREE" 'D: dom/walk tree  it/peek D' $BASE
$M retain "$C60 $DOWN D: dom/walk deep" 'down 59' $BASE
$M retain "$C60" 'S: df/start deep' $FN
$M retain "$C60 sd: func [s n] [ either = n 0 [ s ] [ sd block-at df/step s 3 - n 1 ] ]" 'S: sd df/start deep 59' $FN

echo "== 3. allocation per operation (profile counters)"
$MP alloc "$WIDE D: dom/walk wide it/next D" 'it/next D' $BASE
$MP alloc "$TXT D: dom/walk txt it/next D" 'it/next D' $BASE
$MP alloc "$C60 D: dom/walk deep" 'it/next D' $BASE
$MP alloc "$C60 $DOWN D: dom/walk deep down 60" 'it/next D' $BASE
$MP alloc "$WIDE D: dom/walk wide it/next D" 'it/peek D' $BASE
$MP alloc "$WIDE D: dom/walk wide it/next D" 'it/pos D' $BASE
$MP alloc "$C60 $DOWN D: dom/walk deep down 30" 'it/close D' $BASE
# D is predeclared: a set-word with no existing binding would make D a local of
# each nested measuring call, keeping 40 walkers alive at once
$MP alloc "$WIDE D: none" 'D: dom/walk wide' $BASE
echo "   the explicit-state alternative at the same positions (empty, text, open, close)"
$MP alloc "$WIDE S: block-at df/step df/start wide 3" 'S: block-at df/step S 3' $FN
$MP alloc "$TXT S: block-at df/step df/start txt 3" 'S: block-at df/step S 3' $FN
$MP alloc "$C60 S: df/start deep" 'S: block-at df/step S 3' $FN
$MP alloc "$C60 S: df/start deep d2: func [n] [ either = n 0 [ 0 ] [ S: block-at df/step S 3  d2 - n 1 ] ] d2 60" 'S: block-at df/step S 3' $FN

echo "== 4. return stack: deepest probe recursion under which the step completes"
$M stack "x: 0" 'print 0' $BASE
$M stack "$C60 D: dom/walk deep" 'it/next D' $BASE
$M stack "$C60 $DOWN D: dom/walk deep down 59" 'it/next D' $BASE
$M stack "$C60 $DOWN D: dom/walk deep down 60" 'it/next D' $BASE
$M stack "$C60 $DOWN D: dom/walk deep down 59" 'it/peek D' $BASE
$M stack "$C60 $DOWN D: dom/walk deep down 59" 'it/close D' $BASE
$M stack "$C60 S: df/start deep" 'df/step S' $FN
echo "   the recursive baseline, by nesting depth (-1: fails even with no probe)"
for n in 1 5 10 20 30 40 50; do echo "   depth $n"; $M stack "$(chain $n)" 'rw/node deep' $RW; done

echo "== 5. data stack and code space (deepest probe)"
$M dstack "$C60 $DOWN D: dom/walk deep down 59" 'it/next D' $BASE

echo "== 6. the deepest walk (CLI, the test helpers loaded; events counted to the end)"
for n in 150 160 170; do
    printf 'do [ %s none ]\nprint t/counted dom/walk deep 1024\n' "$(chain $n)" > build/depth.glon
    echo "   depth $n: $(build/glon $R/demo/shop/strings-ext.glon ../glon-iter/iter.glon dom.glon t-common.glon build/depth.glon 2>&1 | grep -v '^#\[func\]$\|^\[dump\]' | tr '\n' ' ')"
done
