#!/bin/sh
# experiments/glon-iter/measure.sh -- GLON-ITER v001 resource measurements on
# the unchanged 64K runtime. Run by `make measure` (which builds build/measure
# and build/measure-prof first). Prints the figures quoted in GLON-ITER-V001.md.
set -e
R=../..
M=build/measure
MP=build/measure-prof
BASE="$R/jupyter/prelude.glon $R/demo/shop/strings.glon $R/demo/shop/strings-ext.glon"
ITER="$BASE iter.glon iter-lexer.glon"
MASM="$BASE variants/iter-masm.glon iter-lexer.glon"
V004="$R/jupyter/prelude.glon $R/demo/shop/strings.glon ../glon-pratt/s-extra.glon ../glon-pratt/pratt.glon ../glon-pratt/math-grammar.glon"

# fixtures: a 2,048-byte single fragment; 201 fragments (100 "x", 100 "", "y");
# the 518-byte, 74-fragment token stream of lexer-tests.glon (section J)
BIG='pat: "a+1*b-2/"  s8: pat  s16: s/+ s8 s8  s32: s/+ s16 s16  s64: s/+ s32 s32  s128: s/+ s64 s64  s256: s/+ s128 s128  s512: s/+ s256 s256  s1k: s/+ s512 s512  s2k: s/+ s1k s1k  s8: none  s16: none  s32: none  s64: none  s128: none  s256: none  s512: none  s1k: none  big: reduce [ s2k ]'
X='x: "x"  e: ""'
G200="$X g: reduce [ x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e x e \"y\" ]"
TP='tp: "a+1*b-2/a+1*b-2/"  tr0: s/span tp 0 7  tr1: s/span tp 1 8  tr2: s/span tp 2 9  tr3: s/span tp 3 10  tr4: s/span tp 4 11  tr5: s/span tp 5 12  tr6: s/span tp 6 13  tr7: s/span tp 7 14'
J74="$TP jf: reduce [ tr0 tr7 tr6 tr5 tr4 tr3 tr2 tr1 tr0 tr7 tr6 tr5 tr4 tr3 tr2 tr1 tr0 tr7 tr6 tr5 tr4 tr3 tr2 tr1 tr0 tr7 tr6 tr5 tr4 tr3 tr2 tr1 tr0 tr7 tr6 tr5 tr4 tr3 tr2 tr1 tr0 tr7 tr6 tr5 tr4 tr3 tr2 tr1 tr0 tr7 tr6 tr5 tr4 tr3 tr2 tr1 tr0 tr7 tr6 tr5 tr4 tr3 tr2 tr1 tr0 tr7 tr6 tr5 tr4 tr3 tr2 tr1 tr0 tr7 ]"

echo "== 1. bindings, loader-heap cells and masm code cells (adopted: Glon-only fragment search)"
$M budget - - $ITER
echo "== 1b. the measured alternative: masm cross-fragment scanner (variants/iter-masm.glon)"
$M budget - - $MASM
echo "== 1c. reference: the frozen Pratt v004 session"
$M budget - - $V004

echo "== 2. managed-heap cells kept alive"
$M retain "$X fr: reduce [ x ]" 'I: it/bytes fr' $ITER
$M retain "$J74" 'I: it/bytes jf' $ITER
$M retain "$X fr: reduce [ x ]" 'T: lx/tokens it/bytes fr' $ITER
$M retain "$X fr: reduce [ x ]" 'T: lx/tokens it/bytes fr  it/peek T' $ITER

echo "== 3. allocation per operation (profile counters)"
$MP alloc "$BIG I: it/bytes big" 'it/peek I' $ITER
$MP alloc "$BIG I: it/bytes big" 'it/next I' $ITER
$MP alloc "$BIG I: it/bytes big" 'it/pos I' $ITER
$MP alloc "$BIG I: it/bytes big" 'it/peek2 I' $ITER
$MP alloc "$J74 I: it/bytes jf" 'it/next I' $ITER
$MP alloc "$X fr: reduce [ \"aaaab\" ]" 'it/bytes fr' $ITER
$MP alloc "$X fr: reduce [ \"aaaab\" ]" 'it/skip it/bytes fr "a"' $ITER
$MP alloc "$X fr: reduce [ \"aaaab\" ]" 'it/take it/bytes fr "a"' $ITER
$MP alloc "$G200" 'it/bytes g' $ITER
$MP alloc "$G200" 'it/skip it/bytes g "x"' $ITER
$MP alloc "$G200" 'it/take it/bytes g "x"' $ITER
$MP alloc "$G200" 'it/bytes g' $MASM
$MP alloc "$G200" 'it/skip it/bytes g "x"' $MASM
$MP alloc "$G200" 'it/take it/bytes g "x"' $MASM
$MP alloc "$BIG T: lx/tokens it/bytes big" 'it/next T' $ITER
$MP alloc "$BIG T: lx/tokens it/bytes big" 'it/peek T' $ITER
$MP alloc "$J74 T: lx/tokens it/bytes jf" 'it/next T' $ITER
echo "   v004 for comparison: p/token from a position (one token; it re-lexes on every look)"
$MP alloc 'src: "a+1*b-2/"' 'p/token src 2' $V004
$MP alloc 'src: "abc+12.5"' 'p/token src 0' $V004

echo "== 4. return stack: deepest probe recursion under which the step completes"
$M stack "$X fr: reduce [ x ]" 'print 0' $ITER
$M stack "$BIG I: it/bytes big" 'it/next I' $ITER
$M stack "$BIG I: it/bytes reduce [ s2k \"Z\" ]  it/skip I pat" 'it/next I' $ITER
$M stack "$G200 I: it/bytes g" 'it/next I' $ITER
$M stack "$G200" 'it/skip it/bytes g "x"' $ITER
$M stack "$G200" 'it/take it/bytes g "x"' $ITER
$M stack "$G200 I: it/bytes g" 'it/next I' $MASM
$M stack "$G200" 'it/bytes g' $MASM
$M stack "$G200" 'it/skip it/bytes g "x"' $MASM
$M stack "$G200" 'it/take it/bytes g "x"' $MASM
$M stack "$G200" 'it/bytes g' $ITER
$M stack "$BIG T: lx/tokens it/bytes big" 'it/next T' $ITER
$M stack "$J74 T: lx/tokens it/bytes jf" 'it/next T' $ITER
$M stack 'src: "abc+12.5"' 'p/token src 0' $V004

echo "== 5. data stack and code space (deepest probe)"
$M dstack "$G200" 'it/take it/bytes g "x"' $ITER
