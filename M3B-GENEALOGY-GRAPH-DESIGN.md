# M3B-GENEALOGY-GRAPH-DESIGN.md — a genealogy graph on a simple drawing surface

**Status:** accepted (with one improvement) and implemented. The improvement:
display labels are `block!` character-code values (not `word!` symbol ids, which
are C-side interner metadata invisible to GLON), and the surface stream gains a
`rect` opcode and a `text x y n c1 … cn` opcode that carries the character
codes. M3B demonstrates that M3A's frozen extensible datatypes are useful for a
real graph-shaped domain — a small genealogy graph rendered as a generic
surface-command stream — without any new datatype, evaluator, parser,
collector, S1, or frozen-HOST machinery.

---

## 0. Scope

A tiny fixed genealogy graph (3 people, 3 relationships) is modelled with
ordinary M3A datatypes and rendered as static shapes (lines + boxes + labels)
onto a surface. No GUI toolkit, no layout manager, no windows, no widgets, no
automatic layout. Positions are explicit `VECTOR` values.

---

## 1. Exact datatype definitions (current M3A facilities)

All definitions are ordinary `make datatype! [ … ]` forms. They are **totally
ordered** (each field type is already defined above it), so the M3A
eager-resolution rule is satisfied — no forward/recursive references are used:

```
vector!:       make datatype! [ x: integer!  y: integer! ]
person!:       make datatype! [ id: integer!  name: block! ]
relationship!: make datatype! [ kind: word!  from: person!  to: person! ]
graph-node!:   make datatype! [ person: person!  position: vector! ]
graph-edge!:   make datatype! [ relationship: relationship!
                                from-node:    graph-node!
                                to-node:      graph-node! ]
```

Display labels are **`block!` values of character codes** (deliberately
temporary presentation text; M3B does not add `string!`). A label is
conceptually:

```
Alice   -> [65 108 105 99 101]
Bob     -> [66 111 98]
Charlie -> [67 104 97 114 108 105 101]
```

WORD spellings live in the C-side interner and are not visible to GLON, so a
`word!` name could not be rendered; the `block!` character-code form makes the
label GLON-visible without broadening M3B into a string implementation.

Construction uses the M3A `make` surface (get-words for reference values, plain
words for `word!`/`kind!` fields, blocks for `block!` fields):

```
alice:   make person! [0 [65 108 105 99 101]]
bob:     make person! [1 [66 111 98]]
charlie: make person! [2 [67 104 97 114 108 105 101]]

spouse:  make relationship! [spouse :alice :bob]
parent1: make relationship! [parent :alice :charlie]
parent2: make relationship! [parent :bob   :charlie]

pos-a: make vector! [100 100]
pos-b: make vector! [300 100]
pos-c: make vector! [200 250]

node-a: make graph-node! [:alice   :pos-a]
node-b: make graph-node! [:bob     :pos-b]
node-c: make graph-node! [:charlie :pos-c]

edge-ab: make graph-edge! [:spouse  :node-a :node-b]
edge-ac: make graph-edge! [:parent1 :node-a :node-c]
edge-bc: make graph-edge! [:parent2 :node-b :node-c]
```

Field access is `field v 'name`; nesting composes: `field field node 'position
'x`, `field field edge 'relationship 'from 'id`, etc.

---

## 2. Exact object / reference graph

```
PERSON(alice)  ←—from—  RELATIONSHIP(spouse)  —to—→  PERSON(bob)
     ↑                                                       ↑
     | person                                                | person
GRAPH-NODE(node-a)                                      GRAPH-NODE(node-b)
     | position                                             | position
VECTOR(pos-a [100 100])                                VECTOR(pos-b [300 100])
     ↑ from-node                                   to-node ↑
     └———————— GRAPH-EDGE(edge-ab, relationship=spouse) ——————┘

and similarly parent1/parent2 → node-c (VECTOR pos-c [200 250]).
```

Every object is a `T_USER` managed value; every reference is a `T_USER` field
cell. The collector sees one flat graph of tagged values and traces it through
the single generic `trace_user` path — it has no knowledge of any of these
datatypes.

---

## 3. Why VECTOR belongs in GRAPH-NODE, not PERSON

Position is **presentation state**, not domain fact. The same `PERSON` value can
appear at different coordinates in different diagrams (see tests F/G). Putting
`x`/`y` inside `PERSON` would (a) conflate identity with placement and (b) make
the same person unmovable across diagrams without mutating shared domain data.

So:

- `PERSON` / `RELATIONSHIP` = domain facts (who, and how related).
- `GRAPH-NODE` / `GRAPH-EDGE` = one particular diagram's presentation.

`GRAPH-NODE` composes domain (`person: person!`) with presentation
(`position: vector!`); `GRAPH-EDGE` composes relationship + two nodes. This
is the whole point of the demonstration.

---

## 4. Proposed static example dataset

People: `Alice` (id 0), `Bob` (id 1), `Charlie` (id 2). Names are `block!`
values of character codes (spellings are not GLON values; STRING! is
deliberately not added).

Relationships: `spouse` (Alice–Bob), `parent` (Alice–Charlie), `parent`
(Bob–Charlie).

Positions: Alice `[100 100]`, Bob `[300 100]`, Charlie `[200 250]` — explicitly
supplied, giving the visible "Alice — Bob, both down to Charlie" shape.

---

## 5. GC root / lifetime expectations

Root set: the words bound in the global context (the datatype descriptors, the
`person`/`relationship`/`graph-node`/`graph-edge` values, and the vectors). All
other references are `T_USER` field cells traced by the generic M3 GC path.

- A live diagram keeps its persons, relationships, nodes, edges and vectors
  alive transitively.
- Dropping the diagram's node/edge roots while the persons are still bound keeps
  the persons alive (they are referenced by those roots).
- Dropping every root makes the whole graph reclaimable in one collection.

The M3B tests (A–H) encode exactly these expectations.

---

## 6. Proposed rendering abstraction

Three tiers, keeping the GLON side identical regardless of the concrete
renderer:

```
GLON: traverse graph, read positions, decide what to emit
        ↓
generic surface command stream (integers over the frozen `print`)
        ↓
platform renderer (SVG text / JS canvas) decodes and draws
```

The generic surface protocol is a stream of integers (one `print` per integer):

| opcode | meaning | following integers |
|---|---|---|
| 0 | clear | — |
| 1 | line x1 y1 x2 y2 | x1 y1 x2 y2 |
| 2 | text x y n c1 … cn | x y n, then n character codes |
| 3 | rect x y w h | x y w h (optional but useful for nodes) |
| 4 | present | — |

GLON does **not** render SVG. It emits this generic surface-command stream. A
small host-side decoder consumes the stream and produces SVG for the first
proof; that decoder must know only the generic drawing operations and must know
nothing about `VECTOR`/`PERSON`/`RELATIONSHIP`/`GRAPH-NODE`/`GRAPH-EDGE` or
genealogy semantics. The same GLON-side stream can later be consumed by Canvas
or a native pane without changing the graph model.

The GLON side reads coordinates from the graph values (it never hardcodes the
geometry) and emits the commands; e.g. a `draw-edge` function reads
`field field field e 'from-node 'position 'x` and the corresponding `to-node`
coordinates, then prints `1 x1 y1 x2 y2`. The host never sees
PERSON/RELATIONSHIP/positions-as-meaning — it sees only opcodes and integers.

---

## 7. Does rendering require new HOST operations?

**No.** The frozen HOST boundary already provides `HOST_PRINT` ("%ld\n"), which
is reachable from GLON through the existing `print` native and (for RAW) the
`PRINT`/`HOST <id>` mnemonics. The drawing-command stream is just a sequence of
`print` calls. No new HOST service is added; `s1.c`/`s1.h` remain byte-identical
to `s1-frozen-v1`.

The only other output service (`HOST_PUTCHAR`) is also already present if a
future text path is wanted, but it is not needed for this design.

Note on labels: `name` is a `block!` of character codes, so the label text is
GLON-visible and is emitted through the `text` command's `n c1 … cn` fields.
The host-side decoder maps those character codes to a text run. STRING!/
spelling machinery is out of scope and would be a future intrinsic tag, not M3B
work.

---

## 8. Can a file/SVG/canvas target avoid native windowing?

Yes. The first proof renders to **SVG text** with no native windowing:

- **Native:** a tiny C driver runs the GLON rendering program, captures the
  `HOST_PRINT` stream, and writes an SVG file (lines + text) from it — or
  simply asserts the stream is the expected golden command sequence.
- **WASM:** the existing `host_print` import already delivers each `print`
  line; a ~30-line addition to `standalone/glon.js` (or a sibling) decodes the
  opcodes into `<line>`/`<text>` DOM/SVG elements or a `<canvas>` 2D context.

The GLON-side graph/rendering architecture is identical in both cases, so it
would carry over unchanged to a real pane later. No native window system is
built.

---

## 9. Exact tests A–H

New `r0_s1_m3b_tests.c`, reusing the M3A datatype-library preamble plus the
five `make datatype!` definitions. Each test is a fresh `m3_run` (as in M3A).

- **A — VECTOR nested in GRAPH-NODE survives GC:**
  build `node-a: make graph-node! [:alice :pos-a]`; `collect`;
  `field field node-a 'position 'x` → 100.
- **B — PERSON referenced through GRAPH-NODE survives GC:**
  build `node-a`; `collect`; `field field node-a 'person 'id` → 0.
- **C — RELATIONSHIP references two PERSON values and survives GC:**
  build `spouse`; `collect`; `field field spouse 'from 'id` → 0 and
  `field field spouse 'to 'id` → 1.
- **D — GRAPH-EDGE references RELATIONSHIP + two GRAPH-NODEs and survives GC:**
  build `edge-ab`; `collect`; `field field edge-ab 'relationship 'from 'id` → 0
  and `field field field edge-ab 'to-node 'position 'x` → 300.
- **E — drop all roots; collect; whole graph reclaimable:**
  build the full graph; `alice: none bob: none … node-c: none edge-ab: none …`;
  `collect`; assert `free_cells` grew by ≥ the graph's cells (and no user
  object is live).
- **F — two diagrams share PERSONs with different VECTORs:**
  `n1: make graph-node! [:alice :pos-a]`, `n2: make graph-node! [:alice :pos-b]`;
  `collect`; `field field n1 'position 'x` → 100 and `field field n2 'position
  'x` → 300, while both `field field n1 'person 'id` and `… n2 …` → 0.
- **G — reclaiming one diagram keeps a shared PERSON rooted by the other:**
  drop `n1` (and its edges); `collect`; `field field n2 'person 'id` → 0 (Alice
  still alive); assert the dropped node's cells were reclaimed.
- **H — M3A A–Q unchanged:** the full native suite (including `r0_s1_m3_tests`
  A–Q) still passes with 0 failures.

---

## 10. Confirmation that frozen layers stay frozen

- **S1:** no new primitive; `./check-frozen-s1.sh` unchanged (s1.c/s1.h/tests.c/
  adversarial.c/claims.c byte-identical to `s1-frozen-v1`).
- **Evaluator / parser:** no new native id, no new parse form, no change to any
  `emit_*`/`parse_*` function.
- **M3 datatype machinery:** no new value tag, no new `GC_KIND_*`, no
  datatype-specific collector logic, no `PERSON`/`RELATIONSHIP` special case.
  The frozen `glon-m3a-datatypes-v1` datatype library is used as-is.
- **HOST:** no new HOST service; rendering rides the existing `print`/`HOST_PRINT`.

If any of these were found to be insufficient, the design would STOP and report
the blocker rather than silently extending M3A.

---

## Recommendation

**B — render to SVG/file as the first proof**, using the existing frozen
`print` (`HOST_PRINT`) numeric path as a generic drawing-command stream.

Reasons:

- **A (existing surface)** is not viable: the only existing "surface" is the
  DOM text element (`host_set_text`); it cannot draw lines/shapes.
- **C (new HOST ABI)** is both unnecessary and (for the native host) impossible:
  `s1.c` is frozen, and `HOST_PRINT` already carries the integers a surface
  needs. Reusing it keeps the GLON side identical natively and in WASM and adds
  no frozen-file change.
- **B** needs no native windowing, keeps the host datatype-agnostic, and leaves
  the GLON graph/rendering architecture unchanged for a later real pane.

The rendering host learns only `{clear, line, text, present}` with integer
arguments — never PERSON, RELATIONSHIP, genealogy, traversal, or layout.
