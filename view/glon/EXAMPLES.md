# Glon/VID Table Examples — GV-2R

**Status:** GV-2R working examples, 2026-11-20.
**Branch:** `glon-view-gv2`.

These are complete runnable Glon applications using the actual implemented
vocabulary. They do not use a numeric scene specification: the window, controls
and table are composed through the generic View words in `view_lib.glon`, and
the table policy is ordinary Glon in `vid_table.glon`.

The table itself is the accepted C table implementation retained as a
performance reference. The host exposes it through Glon words; application code
does not modify C.

## Running the examples

Build the 1M-profile Glon application runner:

```sh
make -C view glon-demo
```

Run an example and write a screenshot:

```sh
SDL_VIDEODRIVER=dummy ./view/build/glon_demo_1m \
  --root . --app view/glon/table1.glon \
  --width 700 --height 300 --screenshot view/evidence/gv2a-table1.png
```

Replace `table1.glon` with `table_demo.glon`, `table3.glon` or
`table_data.glon`. For an interactive window, omit `--screenshot` and use a
normal display; `SDL_VIDEODRIVER=dummy` is only for headless capture.

## Vocabulary used

The generic View words include `view/mknode`, `view/rect`, `view/text`,
`view/flags`, `view/bg`, `view/fg`, `view/font`, `view/draw-clear`,
`view/draw-rect`, `view/draw-text`, `view/focus`, `view/invalidate`,
`view/begin`, `view/end` and `view/capabilities`. Node types are named
constants such as `view/type-window`, `view/type-group`, `view/type-rect`,
`view/type-text` and `view/type-custom`.

The table words include `table/new`, `table/new-auto`, `table/columns`,
`table/rows`, `table/row`, `table/set-cell`, `table/delete`, `table/clear`,
`table/sort`, `table/filter`, `table/select`, `table/focus`, `table/resize`,
`table/readonly`, `table/kind`, `table/renderer`, `table/edit`,
`table/edit-text`, `table/commit`, `table/cancel`, `table/scroll`,
`table/get`, `table/status` and `table/destroy`.

Structured table data uses `table/columns-block` and `table/rows-block`.
Cells are escaped at the Glon boundary, so semicolons, vertical bars, newlines
and Unicode survive the textual host-call transport.

The host-call transport is emitted by `emit-host-call`; its small MASM emit
helpers live in `view_lib.glon`. No table policy is written in MASM.


## Example 1 — Minimal table

```glon

;; view/glon/table1.glon -- minimal Glon/VID table.
;;
;; Run with the generic Glon app runner.  No numeric scene spec is required:
;; the window and table are composed with the generic View vocabulary, and the
;; table policy is ordinary Glon.
[
v/table: 100
v/field-text: ""
v/table-sort-col: -1
v/table-sort-dir: 1
v/table-filter: ""
v/table-select-row: -1
v/status: "Ready"

build-ui: func [] [
    view/mknode 1 0 view/type-window
    view/rect 1 0 0 700 300
    view/flags 1 1 0 0 0

    t1: table/new-auto 1 20 20 660 240
    table/columns t1 "Name:180:0;Age:60:2;City:140:0"
    table/rows t1 "Alice|30|Wellington;Bob|25|Auckland;Cara|41|Christchurch;Dan|37|Dunedin;Eve|29|Hamilton"
    table/select t1 0
]

do-event: [
    either = current-event 'view-build [
        build-ui
    ] [ none ]
    widget/flush
]
]


```


## Example 2 — Interactive membership tables

```glon

;; view/glon/table_demo.glon -- two independent tables, controlled from Glon.
;;
;; The tables are host-backed for large-row performance.  Glon owns the policy:
;; columns, row data, selection, sorting, filtering, editing and formatting.
;; The window and controls are composed with the generic View vocabulary.
[
v/focus-table: 0
v/table1: 0
v/table2: 0
t1: 0
t2: 0
v/focus-row: 0
v/focus-col: 0
v/field-text: ""
v/editing: 0
v/status: "Ready"
v/query: 0
v/table-sort-col: -1
v/table-sort-dir: 1
v/table-filter: ""
v/table-select-row: -1
v/last-button: 0
v/last-button-text: 0
btn1: 0
btn2: 0
btn3: 0
btn4: 0
btn5: 0
btn6: 0
btn7: 0
btn1-text: 0
btn2-text: 0
btn3-text: 0
btn4-text: 0
btn5-text: 0
btn6-text: 0
btn7-text: 0
btn8: 0
btn8-text: 0

button/new: func [x y w h text] [
    id: view/next-id
    bg: + id 100
    tx: + id 200
    view/mknode id 1 view/type-group
    view/rect id x y w h
    view/flags id 1 1 1 1
    view/rect bg id x y w h
    view/flags bg 1 1 0 0
    view/bg bg 70 130 200 255
    view/mknode tx id view/type-text
    view/rect tx 0 0 w h
    view/flags tx 1 1 0 0
    view/text tx text
    view/font tx 13
    v/last-button: id
    v/last-button-text: tx
    none
]

button/new-at: func [id x y w h text] [
    bg: + id 100
    tx: + id 200
    view/mknode id 1 view/type-group
    view/rect id x y w h
    view/flags id 1 1 1 1
    view/rect bg id x y w h
    view/flags bg 1 1 0 0
    view/bg bg 70 130 200 255
    view/mknode tx id view/type-text
    view/rect tx 0 0 w h
    view/flags tx 1 1 0 0
    view/text tx text
    view/font tx 13
    v/last-button: id
    v/last-button-text: tx
    none
]

set-status: func [text] [
    v/status: text
    view/text 40 text
]

split3: func [s] [
    id: parse-int s/slice s 0 4
    row: parse-int s/slice s 5 4
    col: parse-int s/slice s 10 4
    reduce [ id row col ]
]

build-background: func [] [
view/mknode 1 0 view/type-window
view/rect 1 0 0 900 420
view/flags 1 1 0 0 0
view/mknode 40 1 view/type-text
view/rect 40 20 380 860 24
view/flags 40 1 1 0 0
view/text 40 "Ready"
view/bg 40 24 26 30 255
view/fg 40 160 168 178 255
view/font 40 13

;; Two independent host-backed tables.
]

build-table1: func [] [
t1: table/new-auto 1 20 50 420 210
table/columns t1 "Name:160:0;Age:60:2;City:140:0;Donation:90:2"
table/rows t1 "Alice|30|Wellington|10;Bob|25|Auckland|20;Cara|41|Christchurch|30;Dan|37|Dunedin|40;Eve|29|Hamilton|50"
table/select t1 0
table/focus t1 0 0
]

build-table2: func [] [
t2: table/new-auto 1 460 50 420 210
table/columns t2 "Name:160:0;Age:60:2;City:140:0;Donation:90:2"
table/rows t2 "Frank|52|Napier|60;Gina|33|Tauranga|70;Hana|28|Palmerston|80;Ivan|45|Nelson|90;Jade|31|Rotorua|100"
table/select t2 0
table/focus t2 0 0
]
build-controls: func [] [
view/mknode 50 1 view/type-rect
view/rect 50 20 310 300 28
view/flags 50 1 1 1 1
view/bg 50 24 26 30 255
view/fg 50 235 238 242 255
view/mknode 51 50 view/type-text
view/rect 51 6 4 288 20
view/flags 51 1 1 1 0
view/text 51 ""
view/font 51 13
view/mknode 52 1 view/type-text
view/rect 52 340 314 200 20
view/flags 52 1 1 0 0
view/text 52 "Filter or edit value"
view/bg 52 24 26 30 255
view/fg 52 160 168 178 255
view/font 52 12
]

build-buttons: func [] [
button/new-at 600 20 270 100 30 "Add"
btn1: v/last-button
btn1-text: v/last-button-text
button/new-at 610 130 270 100 30 "Delete"
btn2: v/last-button
btn2-text: v/last-button-text
button/new-at 620 240 270 100 30 "Sort"
btn3: v/last-button
btn3-text: v/last-button-text
button/new-at 630 350 270 100 30 "Filter"
btn4: v/last-button
btn4-text: v/last-button-text
button/new-at 640 460 270 100 30 "Edit"
btn5: v/last-button
btn5-text: v/last-button-text
button/new-at 650 570 270 100 30 "Clear"
btn6: v/last-button
btn6-text: v/last-button-text
button/new-at 660 680 270 100 30 "Status"
btn7: v/last-button
btn7-text: v/last-button-text
button/new-at 670 790 270 100 30 "Read"
btn8: v/last-button
btn8-text: v/last-button-text
]

build-ui: func [] [
    build-background
    build-table1
    build-table2
    v/table1: t1
    v/table2: t2
    v/focus-table: t1
    build-controls
    build-buttons
]

table/add-focused: func [] [
    table/row v/focus-table "New|Village|Active|0"
    set-status s/+ "Added row to table " s/from-int v/focus-table
]
table/delete-focused: func [] [
    table/delete v/focus-table v/focus-row
    set-status s/+ "Deleted row " s/from-int v/focus-row
]
table/sort-focused: func [] [
    table/sort v/focus-table 0 1
    set-status "Sorted by column 0"
]
table/filter-focused: func [] [
    table/filter v/focus-table v/field-text
    set-status s/+ "Filter: " v/field-text
]
table/edit-focused: func [] [
    table/edit v/focus-table v/focus-row v/focus-col v/field-text
    v/editing: 1
    set-status "Editing cell"
]
table/clear-focused: func [] [
    table/clear v/focus-table
    set-status "Cleared table"
]
table/status-focused: func [] [
    table/status v/focus-table 1
    set-status "Status requested"
]
table/commit-focused: func [] [
    table/commit v/focus-table
    v/editing: 0
    set-status "Cell committed"
]
table/cancel-focused: func [] [
    table/cancel v/focus-table
    v/editing: 0
    set-status "Edit cancelled"
]

do-event: [
    either = current-event 'view-build [
        build-ui
    ] [ none ]

    either = current-event 'view-click [
        id: parse-int current-value
        either = id btn1 [ table/add-focused ] [ none ]
        either = id btn2 [ table/delete-focused ] [ none ]
        either = id btn3 [ table/sort-focused ] [ none ]
        either = id btn4 [ table/filter-focused ] [ none ]
        either = id btn5 [ table/edit-focused ] [ none ]
        either = id btn6 [ table/clear-focused ] [ none ]
        either = id btn7 [ table/status-focused ] [ none ]
        either = id btn8 [ table/get v/focus-table v/focus-row v/focus-col 1 ] [ none ]
    ] [ none ]

    either = current-event 'table-click [
        parts: split3 current-value
        v/focus-table: block-at parts 0
        v/focus-row: block-at parts 1
        v/focus-col: block-at parts 2
        table/select v/focus-table v/focus-row
        table/focus v/focus-table v/focus-row v/focus-col
        set-status s/+ "Table " s/+ s/from-int v/focus-table s/+ " row " s/from-int v/focus-row
    ] [ none ]

    either = current-event 'view-set-text [
        v/field-text: current-value
        view/text 51 current-value
    ] [ none ]

    either = current-event 'view-char [
        either = v/editing 1 [ table/edit-text v/focus-table current-value ] [ none ]
    ] [ none ]

    either = current-event 'view-key [
        key: parse-int current-value
        either = key 1024 [ v/focus-row: - v/focus-row 1  table/focus v/focus-table v/focus-row v/focus-col ] [ none ]
        either = key 1025 [ v/focus-row: + v/focus-row 1  table/focus v/focus-table v/focus-row v/focus-col ] [ none ]
        either = key 1026 [ v/focus-col: - v/focus-col 1  table/focus v/focus-table v/focus-row v/focus-col ] [ none ]
        either = key 1027 [ v/focus-col: + v/focus-col 1  table/focus v/focus-table v/focus-row v/focus-col ] [ none ]
        either = key 13 [ either = v/editing 1 [ table/commit-focused ] [ table/edit-focused ] ] [ none ]
        either = key 27 [ table/cancel-focused ] [ none ]
        either = key 8 [ either = v/editing 1 [ table/edit-text v/focus-table v/field-text ] [ none ] ] [ none ]
        either = key 65 [ table/add-focused ] [ none ]
        either = key 68 [ table/delete-focused ] [ none ]
        either = key 83 [ table/sort-focused ] [ none ]
        either = key 70 [ table/filter-focused ] [ none ]
    ] [ none ]

    either = current-event 'table-cell [
        v/query: current-value
        set-status s/+ "Query: " current-value
    ] [ none ]

    widget/flush
]
]


```


## Example 3 — Customised table

```glon

;; view/glon/table3.glon -- customised Glon/VID table.
;;
;; The table is host-backed for row storage and rendering.  Glon owns the
;; formatting policy, the checkbox/indicator state, the custom View cell
;; renderer, and the timer-driven cell update.
[
v/table: 100
v/field-text: ""
v/table-sort-col: -1
v/table-sort-dir: 1
v/table-filter: ""
v/table-select-row: -1
v/tick: 0
v/status: "Ready"

build-ui: func [] [
    view/mknode 1 0 view/type-window
    view/rect 1 0 0 760 360
    view/flags 1 1 0 0 0

    t1: table/new-auto 1 20 20 720 300
    table/columns t1 "Item:180:0;Status:120:0;Amount:100:2;Active:80:0;Trend:80:0"
    table/rows t1 "Widget|Active|10|1|up;Gadget|Lapsed|20|0|flat;Doohickey|Active|30|1|up;Thing|Lapsed|40|0|down;Whatsit|Active|50|1|up"
    table/select t1 0

    table/kind t1 0 1 2
    table/kind t1 1 1 2
    table/kind t1 2 1 2
    table/kind t1 3 1 2
    table/kind t1 4 1 2
    table/kind t1 0 3 1
    table/kind t1 1 3 1
    table/kind t1 2 3 1
    table/kind t1 3 3 1
    table/kind t1 4 3 1
    table/readonly t1 0 2 1
    table/readonly t1 1 2 1
    table/readonly t1 2 2 1
    table/readonly t1 3 2 1
    table/readonly t1 4 2 1

    view/mknode 200 1 view/type-custom
    view/rect 200 0 0 16 16
    view/flags 200 0 0 0 0
    view/draw-clear 200
    view/draw-rect 200 0 0 12 12 70 180 110 255
    table/renderer t1 0 4 200
    table/renderer t1 1 4 200
    table/renderer t1 2 4 200
    table/renderer t1 3 4 200
    table/renderer t1 4 4 200
]

do-event: [
    either = current-event 'view-build [
        build-ui
    ] [ none ]

    either = current-event 'view-tick [
        v/tick: + v/tick 1
        table/set-cell t1 0 2 s/+ "Tick " s/from-int v/tick
        v/status: s/+ "Tick " s/from-int v/tick
    ] [ none ]

    widget/flush
]
]


```


## Additional example — Structured data and special characters

```glon

;; view/glon/table_data.glon -- structured table data and special characters.
;;
;; This demonstrates `table/columns-block` and `table/rows-block`.  Cell text
;; may contain semicolons, vertical bars, newlines and Unicode because the Glon
;; vocabulary escapes them before the textual host-call boundary.
[
v/table: 100
v/table1: 0
v/field-text: ""
v/table-sort-col: -1
v/table-sort-dir: 1
v/table-filter: ""
v/table-select-row: -1
v/nl: mk-string [ 10 ]

build-ui: func [] [
    view/mknode 1 0 view/type-window
    view/rect 1 0 0 760 300
    view/flags 1 1 0 0 0

    t1: table/new-auto 1 20 20 720 240
    col1: reduce [ "Name" 160 0 ]
    col2: reduce [ "Note" 320 0 ]
    col3: reduce [ "Unicode" 140 0 ]
    table/columns-block t1 reduce [ col1 col2 col3 ]

    row1: reduce [ "Alice" "semi;colon" "π" ]
    row2: reduce [ "Bob" "pipe|char" "λ" ]
    row3: reduce [
        "Cara"
        s/+ "line" s/+ v/nl "break"
        "漢字"
    ]
    table/rows-block t1 reduce [ row1 row2 row3 ]
    v/table1: t1
]

do-event: [
    either = current-event 'view-build [
        build-ui
    ] [ none ]
    widget/flush
]
]


```


## Application-level code size

| Example | Glon lines |
|---|---:|
| `table1.glon` | 32 |
| `table_demo.glon` | 278 |
| `table3.glon` | 67 |
| `table_data.glon` | 44 |
| `view_lib.glon` (generic View vocabulary) | 155 |
| `vid_table.glon` (table vocabulary) | 126 |

## Configuration and profiles

- The Glon application runner uses the MS-1 **1M** profile
  (`GV_MEM_CELLS=1048576`, `GV_HEAP_BASE=262144`, `GV_MANAGED_CELLS=262144`,
  `GV_WORKLIST=8192`). The larger managed heap is needed because the current
  Glon command vocabulary batches commands through immutable strings; the 64K
  profile exhausts the heap during a multi-table build.
- The C table itself remains 64K-compatible and is measured separately.
- No native controls are used by these examples.

## Missing or unsupported in these examples

- Accessibility is mandatory for 1.0 but is not implemented by the C table or
  the Glon examples.
- Native text controls are not used.
- Column resizing is available through `table/resize`, but pointer-drag resize
  is not demonstrated.
- Inline editing is command-driven; a full inline text-field overlay is not
  implemented.
- Custom cell rendering is generic View draw-list composition; there is no
  per-cell callback in C.
- The table is not a native OS control.
