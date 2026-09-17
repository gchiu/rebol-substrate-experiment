# M3B-RESULTS.md — genealogy graph rendered as a generic surface stream

M3B is implemented on the frozen R0/S1 substrate using the M3A datatype
mechanism unchanged. A tiny fixed family (Alice/Bob/Charlie) is modelled with
five ordinary M3A datatypes, rendered as a generic surface-command stream over
the frozen `print` (`HOST_PRINT`) path, and decoded to SVG by a small host-side
decoder that knows nothing about genealogy.

## 1. Complete native assertion count

- **Assertions:** **244 `ok:`** (up from 225 at M3A), **0 failures**, exit 0.
- M3B adds 19 assertions (A–G + R + S) on top of the unchanged M3A/M2/M1/…
  suites.

## 2. M3B A–H results

All pass:

- **A — VECTOR nested in GRAPH-NODE survives GC:** `node-a.position.x == 100`.
- **B — PERSON reachable only through GRAPH-NODE survives GC:**
  `node-a.person.id == 0` after the separate `alice`/`pos-a` roots are dropped.
- **C — RELATIONSHIP keeps both PERSON values alive:** `from.id == 0`,
  `to.id == 1` after dropping the person roots.
- **D — GRAPH-EDGE keeps RELATIONSHIP + two GRAPH-NODEs alive:**
  `edge.relationship.from.id == 0`, `edge.to-node.position.x == 300` after
  dropping every non-edge root.
- **E — dropping all roots reclaims the entire graph:** ≥ 15×32 cells freed
  (3 persons + 3 relationships + 3 nodes + 3 edges + 3 vectors).
- **F — two diagrams share a PERSON with different VECTORs:**
  `n1.position.x == 100`, `n2.position.x == 300`, both nodes’ `person.id` equal.
- **G — dropping one diagram preserves a shared PERSON:**
  `n2.person.id == 0` survives; the dropped node + its private vector reclaimed
  (≥ 64 cells).
- **H — M3A A–Q and earlier suites unchanged:** full native suite still exits 0.

## 3. Frozen-S1 result

`./check-frozen-s1.sh` → **OK** (`f90496c26dc45c7a387d8fc8639cd2781507d3d2`);
`s1.c`/`s1.h`/`tests.c`/`adversarial.c`/`claims.c` byte-identical to
`s1-frozen-v1`.

## 4. M3A regression result

The full M3A suite (A–Q) is unchanged and still passes; no `emit_*`/`parse_*`
function, native id, value tag, GC kind, or HOST service was added or changed.
`git diff` on the runtime/host/evaluator files is empty.

## 5. Files changed

Tracked (modified):

- `Makefile` — add `r0_s1_m3b_tests.o`.
- `main.c` — run `r0_s1_m3b_tests`.

New (untracked):

- `r0_s1_m3b_tests.c` — M3B acceptance suite (A–G), the render/decoder test (R),
  and the datatype-leakage audit (S).
- `examples/m3b-genealogy.svg` — generated demo artifact.
- `M3B-GENEALOGY-GRAPH-DESIGN.md` — design record (updated for the `block!`
  label + revised stream).

Pre-existing unrelated untracked files left untouched.

No commit or tag was made; `git add .` was not run.

## 6. Drawing-stream example

The render program emits this exact 59-integer stream (one integer per frozen
`print`):

```
0
1 130 115 330 115
1 130 115 230 265
1 330 115 230 265
3 100 100 60 30
2 108 121 5 65 108 105 99 101
3 300 100 60 30
2 308 121 3 66 111 98
3 200 250 60 30
2 208 271 7 67 104 97 114 108 105 101
4
```

i.e. `clear`, three `line`s (spouse + two parent edges, centre-to-centre), three
`rect`/`text` pairs (nodes + labels), then `present`. The `text` commands carry
the character codes of “Alice”, “Bob”, “Charlie”.

## 7. Generated SVG path

`examples/m3b-genealogy.svg` — decoded by the host-side decoder and verified to
contain `Alice`, `Bob`, `Charlie`, three `<line>` elements, and the node
`<rect>`/`<text>` elements.

## 8. Architectural assumptions

Two findings, neither of which required touching the frozen layers:

1. **Label spelling.** As the review anticipated, WORD spellings are C-side
   interner metadata and are invisible to GLON. M3B therefore models labels as
   `block!` character-code values (`person!: make datatype! [id: integer!
   name: block!]`) and the `text` opcode carries `n c1 … cn`. No `string!` was
   added.

2. **A latent R0 evaluator bug (workaround, not fix).** The first M3B render
   code factored `node-x`/`node-y`/`node-name` as ordinary closures. A closure
   invocation nested inside *another* closure’s argument list
   (`draw-rect node-x n …`) mis-evaluates: `emit_invoke_closure` uses `RV_ARITY`
   as its argument-loop bound, but `RV_ARITY` is clobbered by the nested
   closure invocation and restored only after the loop, so the outer closure
   stops early and the arguments shift (first arg becomes 0). This is a
   pre-existing evaluator bug, not an M3B regression — it is reproducible with
   pure closures and natives (`f: func [a b] […]`, `f + g 100 30 115`).

   M3B works around it without modifying the evaluator: field access is inlined
   (`field field n 'position 'x`) instead of factored into closure helpers, so
   every closure argument is a word/literal/native/RAW call (invoke_raw does
   not clobber `RV_ARITY`). The frozen evaluator/parser remain byte-for-byte
   unchanged. The bug itself is recorded here for a future hardening pass.

   (The render layer also needed two one-liner RAW helpers — `tag-int` and
   `div16` — to cross the raw↔tagged integer boundary for the `text` count and
   the character-code loop; these are presentation tooling in `r0_s1_m3b_tests.c`,
   not datatype or runtime changes.)
