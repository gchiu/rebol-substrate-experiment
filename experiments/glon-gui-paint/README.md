# Glon Paint G2.2

A Photoshop-inspired desktop paint pad. SDL2 owns the window, events,
renderer and PNG export; Nuklear supplies the immediate-mode interface; Glon
owns the painting state and drawing decisions; SDL2_image decodes imported
PNG/JPEG files.

## Layout

- Menu bar: File, Edit, View, Help.
- Left toolbar: Brush, Eraser, and a visibly disabled Rectangle.
- Centre workspace: the fixed 800x600 document, aspect-fit and centred with
  margins; it scales with the window.
- Right inspector: four colour swatches, brush-size slider, and tool
  properties.
- Layers panel: one read-only drawing layer above the immutable image
  background.
- Status bar: image size and current tool.

Unavailable operations (Export, grid, additional layers and Rectangle) are
visibly disabled rather than fake live controls.

## Build and launch

On Debian/Ubuntu:

```sh
sudo apt install build-essential libsdl2-dev libsdl2-image-dev libpng-dev
```

Nuklear is vendored in `third_party/`; no system Nuklear package is needed.
SDL2_image supplies PNG/JPEG decoding (through its libpng/libjpeg backends).

From the repository root:

```sh
cd experiments/glon-gui-paint
make
./build/glon-paint
```

The default library root is `../..` when launched from this directory. Set
`PAINT_ROOT=/path/to/checkout` to launch from elsewhere.

## Controls

| Input | Action |
| --- | --- |
| Left-drag on image | draw with the current tool |
| Toolbar | select Brush or Eraser |
| Colour swatches | select Red, Blue, Green or Yellow |
| Brush size slider | set 1-64 px |
| `1`-`4` | colour shortcuts |
| `+` / `-` | brush-size shortcuts |
| `B` / `E` | Brush / Eraser shortcuts |
| `C` | clear the drawing layer |
| `S` | save the composite as `glon-paint.png` |
| `Esc` | quit |
| `Ctrl+Z` | undo the last complete stroke or clear |
| `Ctrl+Y` / `Ctrl+Shift+Z` | redo |
| Menu File | New / Clear, Open PNG/JPEG, Save PNG, Quit |
| Menu Edit / View / Help | Clear, Undo, Redo, reset window size, About |

The document is always 800x600 internally. Window resizing preserves its aspect
ratio, centres it, and maps mouse coordinates through its current destination
rectangle. Clicks on the menu, toolbar, inspector, layers and status panels
never reach the canvas.

## Image background

File → Open shows a Nuklear path field; this WSLg setup has no native file
dialog dependency. PNG and JPEG files are decoded with SDL2_image. The image is
aspect-fit over white, centred, and stored as an immutable background beneath
the drawing layer. The document remains 800x600.

Opening another image resets the drawing layer and undo/redo history. If the
current drawing is unsaved, a confirmation is shown first. An invalid image
shows an error and leaves the current document untouched. The eraser writes
transparency into the drawing layer, revealing the background; Clear Canvas
clears only the drawing layer. Save PNG exports the visible composite.

## Glon command interface

Glon returns one newline-terminated command per event. The host only executes
these commands:

```text
L x1 y1 x2 y2 size r g b   draw an opaque line on the drawing layer
E x1 y1 x2 y2 size r g b   erase to transparency on the drawing layer
C                          clear the drawing layer (background remains)
T title                    set the window title
I color size tool drawing  update the UI mirror/status
S path.png                 export the 800x600 composite
Q                          quit
```

Mouse down/move/up invoke `paint/down`, `paint/move` and `paint/up`; those Glon
words emit `L` or `E` through the same `paint/line` path. Inspector controls
invoke `paint/set-color`, `paint/set-size` and `paint/set-tool`. Future Glon
scripts must use those same words and command interface; there is no C drawing
language and no script editor or replay engine yet.

## Checks

```sh
# 31 Glon paint-logic tests
./glon jupyter/prelude.glon demo/shop/strings.glon \
  demo/shop/strings-ext.glon experiments/glon-gui-paint/paint.glon \
  experiments/glon-gui-paint/tests/paint-tests.glon

# SDL/Nuklear UI, image, undo, resize, PNG and 10,000-move loader tests
make test
```

The image test uses committed PNG/JPEG/alpha/invalid fixtures under
`tests/fixtures/`. `make test` also runs a coalescing check that feeds 1,000
motion events in one batch and reports raw motions versus Glon move calls and
frame renders. This is a synthetic input-to-present measurement, not a claim
about a specific display's refresh rate.

The 10,000-move result reports loader usage and bridge elapsed time. Mouse
motion is coalesced to one `paint/move` per event batch so the final position
still draws a continuous line; the frame is presented once per batch.

## Undo model

Undo/Redo uses a bounded snapshot history, not recorded Glon commands. Each
completed brush/eraser stroke stores the 800x600 drawing layer immediately
before the stroke; Clear Canvas stores the pre-clear drawing layer. The limit
is 16 undo entries and 16 redo entries. New drawing or clear invalidates redo
history. Exact command recording/replay remains for the scripting milestone;
these snapshots must not be described as a Glon command log.

## Limitations

- File Open uses a Nuklear path field, not a native file chooser.
- Imported images are immutable backgrounds, not editable layers.
- Single drawing layer only; no layer creation or compositing.
- Rectangle, custom Export and grid are disabled.
- No PDF decoding, script editor, recorder or replay engine.
- Nuklear is vendored in `third_party/` (public-domain Nuklear v4.13.4 and its
  SDL2 renderer backend).
