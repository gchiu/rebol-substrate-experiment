# Glon/VID GV-2B Examples

**Status:** GV-2B working examples, 2026-11-20.
**Branch:** `glon-view-gv2`.

These are complete runnable Glon applications using the actual implemented VID
vocabulary. Run them with the 1M-profile Glon application runner and `--vid`:

```sh
make -C view glon-demo
SDL_VIDEODRIVER=dummy ./view/build/glon_demo_1m \
  --root . --app view/glon/examples/example1.glon --vid \
  --width 800 --height 500 --screenshot view/evidence/gv2b-example1.png
```

For an interactive window, omit `--screenshot` and use a normal display.


## Example 1 — Minimal window

`view/glon/examples/example1.glon`

```glon

;; Example 1 -- minimal VID window with a label and button.
[
do-event: [
    either = current-event 'view-build [
        vid/window theme/dark 640 240
        vid/label theme/dark 1 20 20 300 24 "Minimal VID window"
        vid/button theme/dark 1 20 60 120 30 "Press"
    ] [ none ]
    widget/flush
]
]


```


## Example 2 — Form with automatic layout

`view/glon/examples/example2.glon`

```glon

;; Example 2 -- conventional form with automatic vertical layout.
[
v/field-text: ""
v/name-text: ""
v/city-text: ""

do-event: [
    either = current-event 'view-build [
        vid/window theme/dark 700 320
        p: vid/panel theme/dark 1 20 20 660 280 view/layout-v 0 8 12 []
        pid: block-at p 1
        vid/label theme/dark pid 0 0 200 24 "Name"
        vid/field theme/dark pid 0 0 300 28 "" 0
        vid/label theme/dark pid 0 0 200 24 "City"
        vid/field theme/dark pid 0 0 300 28 "" 0
        vid/button theme/dark pid 0 0 120 30 "Save"
    ] [ none ]

    either = current-event 'view-set-text [
        v/name-text: current-value
    ] [ none ]

    widget/flush
]
]


```


## Example 3 — Table with structured block data

`view/glon/examples/example3.glon`

```glon

;; Example 3 -- table with structured block data and Unicode.
[
v/field-text: ""
v/table-id: 0
v/table-sort-col: -1
v/table-sort-dir: 1
v/table-filter: ""
v/table-select-row: -1
row1: reduce [ "Alice" "阿明" "Wellington" "10" ]
row2: reduce [ "Bob" "小波" "Auckland" "20" ]
row3: reduce [ "Cara" "小美" "Christchurch" "30" ]

do-event: [
    either = current-event 'view-build [
        vid/window theme/dark 800 400
        p: vid/panel theme/dark 1 20 20 760 360 0 0 0 0 []
        pid: block-at p 1
        rows: reduce [ row1 row2 row3 ]
        t: vid/table theme/dark pid 20 20 720 300
        v/table-id: block-at t 1
            "Name:180:0;Chinese:120:0;City:160:0;Donation:100:2"
            table/rows-encode rows
    ] [ none ]
    widget/flush
]
]


```


## Example 4 — Recursive form clearing

`view/glon/examples/example4.glon`

```glon

;; Example 4 -- recursive clearing of nested form fields.
[
v/field-text: ""
v/name-text: ""
v/city-text: ""

do-event: [
    either = current-event 'view-build [
        vid/window theme/dark 760 360
        outer: vid/panel theme/dark 1 20 20 720 300 0 0 0 0 []
        oid: block-at outer 1
        vid/label theme/dark oid 20 20 200 24 "Outer form"
        inner: vid/panel theme/dark oid 20 60 680 160 0 0 0 0 []
        iid: block-at inner 1
        vid/label theme/dark iid 10 10 200 24 "Name"
        name: vid/field theme/dark iid 10 40 300 28 "" 0
        vid/label theme/dark iid 330 10 200 24 "City"
        city: vid/field theme/dark iid 330 40 300 28 "" 0
        clear: vid/button theme/dark oid 20 240 120 30 "Clear"
        v/clear-node: block-at clear 1
        v/name-node: block-at name 2
        v/city-node: block-at city 2
        v/outer: outer
    ] [ none ]

    either = current-event 'view-set-text [
        v/name-text: current-value
        view/text v/name-node current-value
    ] [ none ]

    either = current-event 'view-click [
        id: parse-int current-value
        either = id v/clear-node [
            vid/clear-fields v/outer
            v/name-text: ""
            v/city-text: ""
            view/text v/name-node ""
            view/text v/city-node ""
        ] [ none ]
    ] [ none ]

    widget/flush
]
]


```


## Example 5 — Integrated membership application

`view/glon/membership.glon`

```glon

;; view/glon/membership.glon -- synthetic membership administration.
;;
;; Entire application in ordinary Glon.  The table is the host-backed C table;
;; all policy, form state and event decisions are Glon.
[
v/field-text: ""
v/table-id: 0
v/table-sort-col: -1
v/table-sort-dir: 1
v/table-filter: ""
v/table-select-row: -1
v/form-id: 0
v/status-node: 0
v/filter-node: 0
v/fields: []
v/focus-row: 0
v/focus-col: 0
v/status: "Ready"
v/filter-text: ""
v/tick: 0
v/editing: 0
v/form-children: 0
v/main-children: 0
main: 0
form: 0
f1: 0
f2: 0
f3: 0
f4: 0
f5: 0
f6: 0
f7: 0
f8: 0
b1: 0
b2: 0
b3: 0
b4: 0
b5: 0
b6: 0

set-status: func [text] [
    v/status: text
    view/text v/status-node text
]

field-at: func [i] [ block-at v/fields i ]
field-set: func [i text] [
    f: field-at i
    vid/field/set-text f text
]

build-ui: func [] [
    vid/window theme/dark 1000 650
    main: vid/panel theme/dark 1 0 0 1000 650 0 0 0 0 []
    main-id: block-at main 1

    filter: vid/field theme/dark main-id 20 20 300 28 "" 0
    v/filter-node: block-at filter 2

    t: vid/table theme/dark main-id 20 60 960 260
        "Name:140:0;Chinese:100:0;Address:200:0;City:100:0;Paternal:100:0;Maternal:100:0;Status:80:0;Donation:80:2"
        "Alice|阿明|123 Queen St|Wellington|Village A|Village B|Active|10;Bob|小波|456 Lambton Quay|Auckland|Village C|Village D|Lapsed|20;Cara|小美|789 Cuba St|Christchurch|Village E|Village F|Active|30;Dan|大伟|12 Willis St|Dunedin|Village G|Village H|Lapsed|40;Eve|小怡|34 Courtenay Pl|Hamilton|Village I|Village J|Active|50"
    v/table-id: block-at t 1

    form: vid/panel theme/dark main-id 20 340 960 160 0 0 0 0 []
    form-id: block-at form 1
    v/form-id: form-id

    f1: vid/field theme/dark form-id 10 10 220 28 "" 0
    f2: vid/field theme/dark form-id 240 10 220 28 "" 0
    f3: vid/field theme/dark form-id 470 10 220 28 "" 0
    f4: vid/field theme/dark form-id 700 10 220 28 "" 0
    f5: vid/field theme/dark form-id 10 50 220 28 "" 0
    f6: vid/field theme/dark form-id 240 50 220 28 "" 0
    f7: vid/field theme/dark form-id 470 50 220 28 "" 0
    f8: vid/field theme/dark form-id 700 50 220 28 "" 0
    v/fields: reduce [ f1 f2 f3 f4 f5 f6 f7 f8 ]

    b1: vid/button theme/dark form-id 10 100 100 30 "Add"
    b2: vid/button theme/dark form-id 120 100 100 30 "Save"
    b3: vid/button theme/dark form-id 230 100 100 30 "Delete"
    b4: vid/button theme/dark form-id 340 100 100 30 "Clear"
    b5: vid/button theme/dark form-id 450 100 100 30 "Sort"
    b6: vid/button theme/dark form-id 560 100 100 30 "Filter"

    status: vid/label theme/dark main-id 20 610 960 24 "Ready"
    v/status-node: block-at status 1

    v/form-children: reduce [ f1 f2 f3 f4 f5 f6 f7 f8 b1 b2 b3 b4 b5 b6 ]
    form: reduce [ vid/kind-panel form-id v/form-children ]
    v/main-children: reduce [ filter t form status ]
    main: reduce [ vid/kind-panel main-id v/main-children ]
]

table/populate-fields: func [cells] [
    field-set 0 block-at cells 0
    field-set 1 block-at cells 1
    field-set 2 block-at cells 2
    field-set 3 block-at cells 3
    field-set 4 block-at cells 4
    field-set 5 block-at cells 5
    field-set 6 block-at cells 6
    field-set 7 block-at cells 7
]

table/add-focused: func [] [
    table/row v/table-id "New|New Member|Village|0|Active|0"
    set-status "Added member"
]

table/save-focused: func [] [
    cells: s/+ vid/field/get-text f1 s/+ "|" s/+ vid/field/get-text f2 s/+ "|" s/+ vid/field/get-text f3 s/+ "|" s/+ vid/field/get-text f4 s/+ "|" s/+ vid/field/get-text f5 s/+ "|" s/+ vid/field/get-text f6 s/+ "|" s/+ vid/field/get-text f7 s/+ "|" vid/field/get-text f8
    table/set-row v/table-id v/focus-row cells
    set-status "Saved member"
]

do-event: [
    either = current-event 'view-build [
        build-ui
    ] [ none ]

    either = current-event 'view-click [
        id: parse-int current-value
        either = id block-at b1 0 [ table/add-focused ] [ none ]
        either = id block-at b2 0 [ table/save-focused ] [ none ]
        either = id block-at b3 0 [ table/delete v/table-id v/focus-row  set-status "Deleted member" ] [ none ]
        either = id block-at b4 1 [ vid/clear-fields form  set-status "Form cleared" ] [ none ]
        either = id block-at b5 0 [ table/sort v/table-id 0 1  set-status "Sorted" ] [ none ]
        either = id block-at b6 0 [ table/filter v/table-id v/filter-text  set-status "Filtered" ] [ none ]
    ] [ none ]

    either = current-event 'table-click [
        parts: split3 current-value
        v/focus-row: block-at parts 1
        v/focus-col: block-at parts 2
        table/focus v/table-id v/focus-row v/focus-col
        table/get v/table-id v/focus-row 0 1
        table/get v/table-id v/focus-row 1 2
        table/get v/table-id v/focus-row 2 3
        table/get v/table-id v/focus-row 3 4
        table/get v/table-id v/focus-row 4 5
        table/get v/table-id v/focus-row 5 6
        table/get v/table-id v/focus-row 6 7
        table/get v/table-id v/focus-row 7 8
        set-status "Member selected"
    ] [ none ]

    either = current-event 'view-set-text [
        v/filter-text: current-value
        vid/field/set-text v/filter-node current-value
    ] [ none ]

    either = current-event 'view-key [
        key: parse-int current-value
        either = key 1024 [ v/focus-row: - v/focus-row 1  table/focus v/table-id v/focus-row v/focus-col ] [ none ]
        either = key 1025 [ v/focus-row: + v/focus-row 1  table/focus v/table-id v/focus-row v/focus-col ] [ none ]
        either = key 1026 [ v/focus-col: - v/focus-col 1  table/focus v/table-id v/focus-row v/focus-col ] [ none ]
        either = key 1027 [ v/focus-col: + v/focus-col 1  table/focus v/table-id v/focus-row v/focus-col ] [ none ]
    ] [ none ]

    either = current-event 'table-cell [
        q: parse-int s/slice current-value 0 1
        text: s/slice current-value 2 - s/length current-value 2
        field-set - q 1 text
        set-status s/+ "Cell " s/from-int q s/+ ": " text
    ] [ none ]

    either = current-event 'view-tick [
        v/tick: + v/tick 1
        set-status s/+ "Ready; tick " s/from-int v/tick
    ] [ none ]

    widget/flush
]
]


```


## Example 6 — Custom View in a VID form

`view/glon/examples/example6.glon`

```glon

;; Example 6 -- custom View component embedded in a VID form.
[
v/field-text: ""
do-event: [
    either = current-event 'view-build [
        vid/window theme/dark 700 300
        p: vid/panel theme/dark 1 20 20 660 260 0 0 0 0 []
        pid: block-at p 1
        vid/label theme/dark pid 20 20 300 24 "Custom View in a VID form"
        vid/button theme/dark pid 20 60 120 30 "Press"
        c: view/next-id
        view/mknode c pid view/type-custom
        view/rect c 20 110 160 80
        view/flags c 1 1 0 0
        view/draw-clear c
        view/draw-rect c 0 0 160 80 42 48 58 255
        view/draw-rect c 8 8 40 64 70 130 200 255
        view/draw-text c 58 28 14 235 238 242 255 "Custom"
    ] [ none ]
    widget/flush
]
]


```


## State examples

Two additional runnable examples render the widget state matrix:

```sh
SDL_VIDEODRIVER=dummy ./view/build/glon_demo_1m --root . \
  --app view/glon/examples/states.glon --vid \
  --width 1000 --height 300 --screenshot view/evidence/gv2b-states.png

SDL_VIDEODRIVER=dummy ./view/build/glon_demo_1m --root . \
  --app view/glon/examples/states-light.glon --vid \
  --width 1000 --height 300 --screenshot view/evidence/gv2b-states-light.png
```

They show normal, hover, pressed, focused and disabled buttons; focused and
disabled fields; checked and unchecked checkboxes; selected and unselected
radio buttons; and both dark and light themes.

## Application-level source size

| Example | Glon lines |
|---|---:|

| `example1.glon` | 11 |

| `example2.glon` | 25 |

| `example3.glon` | 26 |

| `example4.glon` | 44 |

| `membership.glon` | 175 |

| `example6.glon` | 22 |


## Configuration

- Glon app runner: MS-1 **1M** profile.
- `--vid` loads `view_lib.glon`, `vid_table.glon`, `vid_table_data.glon`,
  `theme_lib.glon`, `vid_widgets.glon`, `vid_fields.glon`, `vid_forms.glon`.
- No numeric scene specification is required for these examples.
- No native controls are used.

## Missing or unsupported

- Accessibility, native text controls, full IME, general reactive dependencies.
- Pointer-drag resize, full inline text-field overlay.
- Production data sources and 100k live updates.
