# Glon/View GV-1

Glon/View is the retained composition and presentation layer for Glon.
Glon/VID is the conventional form vocabulary built on top of it.

## Status

**M1–M3 complete on Linux.** The View core, the SDL2 + Cairo backend, and a
Glon-driven demonstration build and pass. The Win32 native-control probe is
implemented and compile-checked, but Windows was not available in this
environment; no native-control interoperability claim is made. See
`WINDOWS-NATIVE-PROBE.md`.

## Layout

```text
include/gv_backend.h       backend-neutral interface
include/gv_view.h         retained scene, layout, transforms, hit testing
include/gv_glon_host.h    Glon host/spec interpreter
include/gv_table.h        GV-2A table model and rendering
src/gv_backend_headless.c deterministic headless backend
src/gv_backend_sdl.c      SDL2 + Cairo backend
src/gv_view.c             View core
src/gv_glon_host.c        declarative spec interpreter + Glon bindings
src/gv_table.c            table rows, sort/filter, selection, virtual rendering
src/gv_native_win32.c     Win32 EDIT/Rich Edit 4.1 probe (Windows)
glon/demo.glon            Glon state and event behaviour
glon/demo.spec            declarative flat scene spec (VID 1.0 minimal)
tests/test_backend.c      backend tests
tests/test_view.c         View core tests
tests/test_table.c        GV-2A table unit tests
tests/test_glon.c         Glon/VID integration tests
tests/table_demo.c        GV-2A table demonstration
tests/bench_table.c       300/10k/100k table scalability run
tests/glon_demo.c         interactive Glon-driven demo
evidence/                 generated screenshots and hashes
```

## Build and test

```sh
make -C view test
make -C view test-view-asan
make -C view test-table
make -C view test-table-asan
make -C view table-demo
make -C view bench-table
make -C view glon-demo
```

Equivalent top-level targets:

```sh
make view-test
make view-demo
make view-probe
```

`make -C view glon-demo` runs the Glon application with the SDL dummy video
driver and writes `evidence/gv2a-glon.png` (GV-2A table demonstration). The
historical GV-1 screenshot remains `evidence/gv1-glon.png`. For a visible
window:

```sh
make -C view demo-window
```

## Architecture in GV-1

- **Declarative scene spec.** `glon/demo.spec` is a flat integer record stream
  interpreted by the host. It is data, not C logic. The host creates the
  retained View scene from it.
- **Glon owns behaviour.** `glon/demo.glon` owns state, event decisions and
  validation. The host only binds Glon state to node properties.
- **Backend-neutral.** The spec uses logical pixels, node ids, RGBA colours,
  fonts and abstract properties. No SDL/Cairo/Win32 types appear in Glon.
- **Text buffer.** The host supplies a generic text-buffer mechanism for the
  field (insert/delete), like a native text control. Glon owns the field value
  and all validation; the backend does not interpret application semantics.
- **Native controls are GV-3.** The Win32 probe is compile-checked only.

## Results

```text
$ make -C view test
PASS: GV-1 backend spike tests
PASS: GV-1 View core tests
PASS: GV-2A table unit tests
PASS: GV-1 Glon/VID integration tests

$ make -C view test-table-asan
PASS: GV-2A table unit tests   (ASan + UBSan)

$ make -C view test-view-asan
PASS: GV-1 View core tests   (ASan + UBSan, no findings)

$ make -C view glon-demo
wrote evidence/gv2a-glon.png
```

GV-1 historical baseline measurements (Linux, SDL dummy video driver, five
runs per binary):

| binary | text | data | bss | file bytes | RSS median | wall median |
|---|---:|---:|---:|---:|---:|---:|
| `test_glon` | 127,621 | 2,272 | 693,960 | 693,560 | 1,900 KB | 41.5 ms |
| `glon_demo` | 134,845 | 2,680 | 693,960 | 746,080 | 13,132 KB | 46.8 ms |

The large `bss` is the 65,536-cell Glon runtime plus host statics. The
`test_glon` RSS is the relevant headless Glon figure; `glon_demo` adds SDL2,
Cairo and the display/font stack.

`gv1-glon.png` sha256 (GV-1 historical):
`de1222672a157c4a45d7f879594b78639296002380ea4885bdd53b79b553e9d7`.
GV-2A screenshot: `gv2a-glon.png`, sha256
`a4b89cd49a4522b34fb6818781f4dee42b939952fd275a4ce6c289b9fcdb3562`.

## Demonstrated behaviour

- window with vertical/horizontal layout;
- label, editable field, button, checkbox, status label;
- button click updates Glon state and the status label;
- checkbox toggles Glon state and the check box colour;
- editable field updates Glon state and the field text;
- resize reflows the View scene;
- custom View canvas embedded in a VID panel;
- VID button inside a scaled custom View scene;
- clean shutdown.

## Known limitations

- Native controls are **not verified**; Windows was unavailable. TSF/UIA/
  spell-check/dictation compatibility is not claimed.
- The GV-1 VID vocabulary is a flat declarative spec, not the higher-level
  `vid/` word set planned for GV-2.
- Text metrics are approximate; complex-script shaping and IME are GV-3.
- The field text buffer is a host mechanism; Glon owns the value and validation.
- No tables, trees, menus, toolbars, tabs, clipboard, accessibility or ZOE.
- The backend interface remains preliminary.

## Boundary

GV-1 implements the View/VID split, the retained composition model, the
declarative scene spec, the host/Glon bindings and the minimal demonstration.
It does not implement the full widget inventory, ZOE, clinical viewers or
native controls. Those are acceptance targets and later milestones.

See `../GLON-VIEW-ARCHITECTURE.md`, `../GLON-VIEW-COMPLETENESS.md`,
`../GLON-VIEW-BACKEND-SELECTION.md` and `../GLON-VIEW-GV1.md`.
## GV-1A experiments

GV-1A adds the vocabulary/ownership assessment without changing the GV-1
runtime or the GV-1 branch.

```sh
make -C view test          # GV-1 regression
make -C view test-address  # object addressing, stale ids, create/destroy
make -C view test-vocab    # vocabulary registry experiment (uses ../glon)
make -C view measure       # heap, GC, node size, layout latency
make -C view bench-text    # text-buffer allocation/copy measurement
```

Verified GV-1A results:

- `test-address`: PASS; 1,000 create/destroy cycles, capacity 7, stale ids invalid.
- `test-vocab`: PASS; string dispatch 16, field text/length, word function call,
  block-function limitation, old-reference preservation.
- `measure`: 17 nodes, `sizeof(gv_node)=264`, node array 4,488 bytes; managed
  heap 7,232 cells; forced GC live 608 cells / 19 objects; layout+display list
  ~1.8–2.4 µs/op.
- `bench-text`: 50 chars succeeds (heap high 36,512, 84 µs/char); 1,000 chars
  fails at char 86 (heap high 39,968, GC live 704) — managed-heap fragmentation.
- `test-vocab` uses the existing Glon CLI (`../glon`); build it with
  `make glon` at the repository root.

See `../GLON-VIEW-GV1A.md`, `../GLON-VOCABULARY-COMPOSITION.md` and
`../GLON-VIEW-VALUE-OWNERSHIP.md`.

## GV-1B memory and text assessment

```sh
make -C view bench-text          # 1,000 chars by default
make -C view bench-text-big      # 11,232-cell managed-heap test build
make -C view test-vocab          # vocabulary + UTF-8 byte checks
make -C view test-address        # addressing, stale ids, create/destroy
make -C view measure             # heap, GC, node size, layout latency
```

Verified findings:

- The GV-1A ~86-character failure was **loader-heap exhaustion** from per-char
  host parsing, not managed-heap fragmentation. Restoring `M[GC_LOADER_HP]`
  around host parses fixes it; 1,000 characters succeed in the original 64K
  configuration.
- Managed-heap limits after the fix: 7,232 cells ~1,571 chars; 11,232-cell
  test build ~2,259 chars; 5,000-char and 10,000-char workloads fail in both.
- 1,000 chars: original 64K succeeds (heap high 39,968, GC 240, live 1,616);
  11,232 build succeeds (heap high 43,904, GC 136, live 1,616).
- 5,000 chars: fails at 1,571 (original) / 2,259 (11,232); heap high 39,968 /
  43,904; GC 937 / 1,350; live 3,840 / 5,216.
- 1,000-char peak RSS: 2,024 KB.
- Context literal `[ ctx: context [ n: 0 ] print 1 ]` fails in both 64K and
  11,232 builds; not a constrained-memory effect.
- UTF-8 is byte-oriented: `s/length "πππ"` = 6.

See `../GLON-VIEW-GV1B.md`, `../GLON-MEMORY-SCALABILITY.md`,
`../GLON-TEXT-EDITING-ASSESSMENT.md` and
`../GLON-HOST-RESOURCE-OWNERSHIP.md`.

## GV-2A table increment

```sh
make -C view test-table     # table data model, sort/filter, selection, render
make -C view table-demo     # Glon host + 300-row table demonstration
make -C view bench-table    # 300 / 10,000 / 100,000 rows
```

The table is a host C data model rendered through a generic `GV_NODE_CUSTOM`
node. Glon owns the policy words (`v/table-sort-col`, `v/table-sort-dir`,
`v/table-filter`, `v/table-select-row`); the host applies them and renders only
the visible row window. Selection is stored by stable row id.

Verified on Linux, 2026-11-20:

- `test_table`: 300-row unit test, 1,000 create/destroy cycles, render and
  selection checks; `test-table-asan` passes under ASan/UBSan.
- `test_glon`: table discovery, table commands, status and `view-tick` update.
- `bench_table`: 100,000 rows; 126 draw ops at 800×600; 24 visible rows;
  create 84.984 ms, sort 36.598 ms, filter 48.149 ms, rerender 44.415 ms;
  peak RSS 22,920 KB.
- `evidence/gv2a-glon.png` sha256
  `a4b89cd49a4522b34fb6818781f4dee42b939952fd275a4ce6c289b9fcdb3562`.

Remaining 1.0-M table work: keyboard/focus policy, column resize, inline edit,
custom cell rendering, accessibility, golden image and incremental updates.
See `../GLON-VID-TABLE.md` and `../GLON-VIEW-COMPLETENESS.md`.

Timers and reactive behaviour are mandatory 1.0 capabilities. GV-2A implements
only a small timer-driven table/status path through `gv_glon_host_tick()` and
the `view-tick` event; it does not implement a general reactive engine.

## GV-2R Glon table vocabulary

```sh
make -C view test-table
make -C view test-table-host
make -C view test-table-data
make -C view table1-demo
make -C view table-glon-demo
make -C view table3-demo
make -C view table-golden
```

The Glon application runner uses the MS-1 **1M** memory profile:

```sh
SDL_VIDEODRIVER=dummy ./view/build/glon_demo_1m \
  --root . --app view/glon/table1.glon \
  --width 700 --height 300 --screenshot view/evidence/gv2a-table1.png
```

Complete runnable applications:

- `view/glon/table1.glon` — minimal three-column, five-row table;
- `view/glon/table_demo.glon` — two independent interactive membership tables;
- `view/glon/table3.glon` — custom cell kinds, checkbox, custom View renderer,
  read-only amount and timer updates.

The generic View vocabulary is in `view/glon/view_lib.glon`; the table policy
vocabulary is in `view/glon/vid_table.glon`. The C table remains the row-store
and visible-window renderer. See `../GLON-VID-TABLE.md` and
`view/glon/EXAMPLES.md`.

## GV-2R structured table data

```sh
make -C view test-table-data
make -C view table-glon-demo
```

`view/glon/table_data.glon` demonstrates `table/columns-block` and
`table/rows-block` with semicolons, vertical bars, newlines and Unicode in cell
data. The Glon vocabulary escapes special bytes before the textual host-call
boundary; the host decodes them.

The generic View escape hatch remains in `view/glon/view_lib.glon`. The C table
is retained as the row-store and visible-window renderer.

## GV-2B VID widgets and membership application

```sh
make -C view test-vid-widgets
make -C view test-vid-widgets-4m
make -C view table-glon-demo
make -C view table-demo
```

Run a VID application with the 1M-profile runner:

```sh
SDL_VIDEODRIVER=dummy ./view/build/glon_demo_1m \
  --root . --app view/glon/membership.glon --vid \
  --width 1000 --height 650 --screenshot view/evidence/gv2b-membership.png
```

VID constructors live in `view/glon/vid_widgets.glon`,
`view/glon/vid_fields.glon`, `view/glon/vid_forms.glon` and
`view/glon/vid_table.glon`. Shared theme data lives in
`view/glon/theme_lib.glon`. Examples are in `view/glon/examples/` and
`view/glon/EXAMPLES-GV2B.md`.

The state matrix for both themes is generated with:

```sh
SDL_VIDEODRIVER=dummy ./view/build/glon_demo_1m --root . \
  --app view/glon/examples/states.glon --vid \
  --width 1000 --height 300 --screenshot view/evidence/gv2b-states.png
SDL_VIDEODRIVER=dummy ./view/build/glon_demo_1m --root . \
  --app view/glon/examples/states-light.glon --vid \
  --width 1000 --height 300 --screenshot view/evidence/gv2b-states-light.png
```

The SDL2/Cairo backend uses Pango for text rendering and font fallback. Chinese
text uses the installed Droid Sans Fallback font; accented Latin and Greek use
DejaVu Sans.

Golden membership screenshot:

```sh
make -C view membership-golden
```

`view/evidence/gv2b-membership.sha256` is the recorded SHA-256 for
`view/evidence/gv2b-membership.png`.
