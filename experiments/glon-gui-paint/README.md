# Glon Paint G2

A Photoshop-inspired desktop paint pad. SDL2 owns the window, events,
renderer and PNG export; Nuklear supplies the immediate-mode interface; Glon
owns the painting state and drawing decisions.

## Layout

- Menu bar: File, Edit, View, Help.
- Left toolbar: Brush, Eraser, and a visibly disabled Rectangle.
- Centre workspace: the fixed 800x600 image, aspect-fit and centred with
  margins; it scales with the window.
- Right inspector: four colour swatches, brush-size slider, and tool
  properties.
- Layers panel: one read-only working canvas layer.
- Status bar: image size and current tool.

Unavailable operations (Open, Export, Undo, Redo, grid, additional layers and
Rectangle) are visibly disabled rather than fake live controls.

## Build and launch

On Debian/Ubuntu:

```sh
sudo apt install build-essential libsdl2-dev libpng-dev
```

Nuklear is vendored in `third_party/`; no system Nuklear package is needed.
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
| `C` | clear canvas |
| `S` | save `glon-paint.png` |
| `Esc` | quit |
| `Ctrl+Z` | undo the last complete stroke or clear |
| `Ctrl+Y` / `Ctrl+Shift+Z` | redo |
| Menu File | New / Clear, Save PNG, Quit |
| Menu Edit / View / Help | Clear, Undo, Redo, reset window size, About |

The image is always 800x600 internally. Window resizing preserves its aspect
ratio, centres it, and maps mouse coordinates through its current destination
rectangle. Clicks on the menu, toolbar, inspector, layers and status panels
never reach the canvas.

## Glon command interface

Glon returns one newline-terminated command per event. The host only executes
these commands:

```text
L x1 y1 x2 y2 size r g b   draw a line on the 800x600 canvas
C                          clear the canvas
T title                    set the window title
I color size tool drawing  update the UI mirror/status
S path.png                 export the 800x600 canvas
Q                          quit
```

Mouse down/move/up invoke `paint/down`, `paint/move` and `paint/up`; those Glon
words emit `L` through the same `paint/line` path. Inspector controls invoke
`paint/set-color`, `paint/set-size` and `paint/set-tool`. Future Glon scripts
must use those same words and command interface; there is no C drawing language
and no script editor or replay engine yet.

## Checks

```sh
# 31 Glon paint-logic tests
./glon jupyter/prelude.glon demo/shop/strings.glon \
  demo/shop/strings-ext.glon experiments/glon-gui-paint/paint.glon \
  experiments/glon-gui-paint/tests/paint-tests.glon

# SDL/Nuklear UI, resize/mapping, PNG and 10,000-move loader tests
make test
```

`make test` also runs a coalescing check that feeds 1,000 motion events in one
event batch and reports the raw motion count versus the number of Glon move
calls and frame renders. This is a synthetic input-to-present measurement, not
a claim about a specific display's refresh rate.

The 10,000-move result reports loader usage and bridge elapsed time. Mouse
motion in the interactive loop is coalesced to one `paint/move` per event
batch so the final position still draws a continuous line; the frame is
presented once per batch, not once per raw motion event.

## Undo model

Undo/Redo uses a bounded snapshot history, not recorded Glon commands. Each
completed brush/eraser stroke stores the 800x600 canvas immediately before the
stroke; Clear Canvas stores the pre-clear canvas. The limit is 16 undo entries
and 16 redo entries. New drawing or clear invalidates redo history. Exact
command recording/replay remains for the scripting milestone; these snapshots
must not be described as a Glon command log.

## Limitations

- Single working canvas layer only; no layer creation or compositing.
- Rectangle is disabled.
- Open, custom export, undo/redo and grid are disabled.
- No script editor, recorder or replay engine.
- Nuklear is vendored in `third_party/` (public-domain Nuklear v4.13.4 and its
  SDL2 renderer backend).
