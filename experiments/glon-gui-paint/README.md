# Glon Paint G3b.2

A Photoshop-inspired desktop paint pad. SDL2 owns the window, events,
renderer and PNG export; Nuklear supplies the immediate-mode interface; Glon
owns the painting state and drawing decisions; SDL2_image decodes imported
PNG/JPEG files.

## Layout

- Menu bar: File, Edit, View, Help.
- Left toolbar: Brush, Eraser, Calligraphy, Line, Rectangle, Ellipse and Bucket.
- Centre workspace: the fixed 800x600 document, aspect-fit and centred with
  margins; it scales with the window.
- Right inspector: four colour swatches, brush-size slider, calligraphy
  diameter/pressure/opacity sliders, and tool properties.
- Layers panel: one read-only drawing layer above the immutable image
  background.
- Status bar: image size and current tool.

Unavailable operations (Export, grid and additional layers) are visibly
disabled rather than fake live controls.

## Build and launch

On Debian/Ubuntu:

```sh
sudo apt install build-essential libsdl2-dev libsdl2-image-dev libpng-dev libgtk-3-dev \
  libdecor-0-dev libcairo2-dev libpango1.0-dev libwayland-dev
```

Nuklear is vendored in `third_party/`; no system Nuklear package is needed.
SDL2_image supplies PNG/JPEG decoding (through its libpng/libjpeg backends).

Native File Dialog Extended (NFD) is vendored in `third_party/nfd/` and built
with its GTK3 Linux backend when `pkg-config gtk+-3.0` is available. Install
`libgtk-3-dev` for the graphical requester. If GTK3 development files are
absent, or GTK initialisation fails at runtime, the application keeps the
Nuklear path-entry fallback.

Under WSLg, SDL's X11/Xwayland backend leaves the Windows pointer hidden over
the application even though SDL and XFixes report a visible cursor; a minimal
SDL X11 window reproduces it. Paint therefore prefers SDL's Wayland backend
when WSLg advertises Wayland, while still honouring an explicit
`SDL_VIDEODRIVER`. GTK/NFD is then matched to the same backend. The X11 path
remains available by explicitly setting `SDL_VIDEODRIVER=x11`.
`./build/glon-paint --cursor-test` checks SDL cursor state and backend
matching.

WSLg's Wayland compositor does not advertise
`zxdg_decoration_manager_v1`, so SDL cannot use server-side decorations there.
Paint must keep its client-side libdecor title bar and resize borders. Stock
libdecor 0.1.0 hides its shadow/border subsurface when maximising; WSLg leaves
those old pixels composited inside the enlarged window, producing the ghost
white/document rectangle with a grey shadow.

Paint builds the vendored, patched libdecor 0.1.0 cairo plugin from
`third_party/libdecor-cairo/` and points `LIBDECOR_PLUGIN_DIR` at it on WSLg
Wayland. The patch keeps the border surface mapped but transparent while the
window is maximized, instead of unmapping it. The title bar, buttons, drag,
resize borders, maximize and restore all remain active, and the stale shadow
cannot be left behind. This is selected only when `libdecor-0.so.0` has the
0.1.0 ABI; an explicit `LIBDECOR_PLUGIN_DIR` or
`SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR=0` still wins. If the patched plugin cannot be
built, Paint falls back to an undecorated window and prints a warning rather
than reintroducing the ghost. Building the plugin needs `libdecor-0-dev`,
`libcairo2-dev`, `libpango1.0-dev` and `libwayland-dev`
(`cairo`, `pangocairo`, `wayland-cursor` via `pkg-config`).
`./build/glon-paint --cursor-test` reports the selected decoration backend and
fails on WSLg Wayland if the patched plugin is not active.

Check whether the built binary has native dialogs:

```sh
./build/glon-paint --nfd-probe
```

`nfd-probe: ready` means the native GTK requester is compiled in. If it says
`built without GTK/NFD`, install `libgtk-3-dev`, run `make clean`, and rebuild;
otherwise Paint intentionally uses the Nuklear path fallback. The Makefile also
prints a warning when NFD is being omitted.

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
| Toolbar | select Brush, Eraser or Calligraphy |
| Colour swatches | select Red, Blue, Green or Yellow |
| Brush size slider | set ordinary Brush/Eraser size 1-64 px |
| Calligraphy sliders | max diameter 1-64, pressure 0-255, ink opacity 0-255 |
| `1`-`4` | colour shortcuts |
| `+` / `-` | brush-size shortcuts |
| `B` / `E` | Brush / Eraser shortcuts |
| `L` / `R` / `O` / `G` | Line / Rectangle / Ellipse / Bucket shortcuts |
| `C` | clear the drawing layer |
| `S` | save the composite as `glon-paint.png` |
| `Esc` | quit |
| `Ctrl+Z` | undo the last complete stroke or clear |
| `Ctrl+Y` / `Ctrl+Shift+Z` | redo |
| Menu File | New / Clear, Open Image, Open Script, Run Script, Save PNG, Quit |
| Menu Edit / View / Help | Clear, Undo, Redo, reset window size, About |
| Inspector Replay | Play, Pause, Step, Back, Stop, End; 1/5/20/60 ops/s |
| Inspector Smooth | Smooth strokes On/Off, Brush tip On/Off, stroke duration 0.15/0.3/0.6/1.2 s |
| Toolbar Line / Rectangle / Ellipse | click-drag geometric tools; Shift constrains line angle, square and circle |
| Inspector Fill shapes | outline (Off) or filled (On) rectangles/ellipses |
| Inspector Bucket tolerance | 0 = exact RGBA match; 1-255 = max per-channel/alpha difference |
| Esc / right-click / focus loss | cancel an in-progress geometric preview without committing |

The document is always 800x600 internally. Window resizing preserves its aspect
ratio, centres it, and maps mouse coordinates through its current destination
rectangle. Clicks on the menu, toolbar, inspector, layers and status panels
never reach the canvas.

## Image background

File → Open Image uses the native NFD/GTK file chooser with a PNG/JPEG filter.
The chooser supports normal folder navigation, spaces in filenames, and
WSL-mounted Windows drives such as `/mnt/c` and `/mnt/d`. It starts in the last
successfully used image directory when available. If the native backend is not
available or fails, the existing Nuklear path field is used instead.

PNG and JPEG files are decoded with SDL2_image. The image is
aspect-fit over white, centred, and stored as an immutable background beneath
the drawing layer. The document remains 800x600.

Opening another image resets the drawing layer and undo/redo history. If the
current drawing is unsaved, a confirmation is shown first. An invalid image
shows an error and leaves the current document untouched. The eraser writes
transparency into the drawing layer, revealing the background; Clear Canvas
clears only the drawing layer. Save PNG exports the visible composite.

## Scripts

File → Open Script uses the same native NFD/GTK requester with a `.glon`
filter, starting in the last used script directory. If native selection is
unavailable it falls back to a Nuklear path field. Loading reads and parses the
source with the existing Glon parser; it does not execute it or change the
canvas. File → Run Script (or the Run Script button in the inspector) executes
the loaded source in the persistent Glon session.

A script starts with the current drawing layer; the bundled examples explicitly
call `paint/draw-clear` first. Running the same script twice repeats its
drawing. A successful script is one undoable operation. Parse/runtime/limit
failures show an error, apply no drawing commands, and leave the current canvas
and undo/redo history unchanged. Loading a script does not execute it. If a
replay or stroke animation is active, a successful load first cancels replay and
restores the pre-playback drawing, then installs the new script; otherwise
loading leaves the canvas unchanged. A failed load leaves the current script,
canvas and history usable. Clear and Open Script are therefore never blocked by
Pause, Complete or End.

The exact script vocabulary is:

```text
paint/set-color n                  select colour 1-4, or 5 = black (script-only)
paint/set-size n                   set brush size 1-64
paint/draw-line x1 y1 x2 y2        opaque line using current colour/size
paint/draw-rect x y w h           rectangle outline
paint/draw-clear                   clear the drawing layer
paint/erase-line x1 y1 x2 y2      erase to transparency using current size
```

Coordinates are always in the fixed 800x600 canvas system. Scripts do not see
SDL or Nuklear. Scripts are validated against the existing parser, but arity and
undefined-word errors are runtime errors. The source limit is 256 KiB and the
drawing-command limit is 4,096 operations per run. The loader frontier is saved
and restored around validation and execution; no permanent loader growth is
expected for repeated runs of the same script.

Examples:

```text
examples/rectangle.glon
examples/landscape.glon
examples/calligraphy.glon
examples/yong.glon
examples/mandala.glon
examples/replay-demo.glon
```

To try the acceptance path: File → Open Script, choose
`examples/landscape.glon` in the native chooser (or enter it in the path
fallback), then Run Script. Paint over the result and use File → Save PNG to
export the composite.

## Calligraphy

The Calligraphy tool uses a circular pressure tip. Its defaults are maximum
diameter 24 px, pressure 180 and ink opacity 255. Pressure 0 deposits no ink;
pressure 255 uses the full maximum diameter. The brush diameter is:

```text
diameter = (maximum_diameter * pressure + 127) / 255
```

Integer arithmetic is used throughout. Segment endpoints are projected onto a
pixel-centre grid in doubled integer coordinates; the nearest-point diameter is
interpolated linearly and rounded to nearest, and pixel coverage is a hard
circular test. Coverage composites at the requested opacity using **max-alpha**
rather than repeated source-over, so a joined segment does not darken an
existing join. The drawing layer remains transparent outside the ink, and
imported backgrounds are never modified.

The mouse supplies position only. The Pressure slider supplies pressure for new
segments; changing it between mouse moves changes subsequent segment widths.
A click without movement emits a zero-length pressure segment (a round dot).
Existing Brush and Eraser output and the window-to-canvas mapping are unchanged.
One complete calligraphy mouse stroke is one Undo entry (snapshot history is
retained).

Script vocabulary:

```text
paint/set-calligraphy-diameter n   set maximum diameter 1-64
paint/set-pressure p               set pressure 0-255
paint/set-ink-opacity a            set ink opacity 0-255
paint/set-tool 3                   select Calligraphy
paint/draw-pressure-line x1 y1 p1 x2 y2 p2
                                     draw a pressure-interpolated segment
```

`paint/draw-pressure-line` emits the host command:

```text
P x1 y1 p1 x2 y2 p2 max_diameter opacity r g b
```

See `examples/calligraphy.glon` for thin-to-thick, thick-to-thin, connected
curves, a dot and a taper to zero pressure. Rendering is CPU/integer based and
does not depend on SDL frame timing. The command sequence is deterministic and
is suitable for the later recording/replay milestones; recording, playback,
stylus hardware, bristles, texture, diffusion and character recognition are not
implemented here.

## Geometric tools

Line, Rectangle and Ellipse are click-drag tools. The document overlays a live
preview while dragging, clipped to the 800x600 canvas; the preview is
presentation only. Releasing the mouse commits the gesture as ordinary `L`
commands through the normal command dispatcher. Shift snaps a line to
horizontal/vertical/45 degrees, a rectangle to a square and an ellipse to a
circle. Reversed drags are normalised. Zero-size rectangles/ellipses emit no
commands; a zero-length line emits a dot. Esc, right-click and window focus loss
cancel the preview. Each completed gesture is one undoable document action.

Outline shapes use the current brush size; filled shapes use one-pixel
horizontal scanlines because there is no fill primitive. Ellipse outlines are
72-segment integer polygons; filled ellipses use one `L` command per canvas row.
The generated command stream is only `L` commands; it is standard, deterministic
and replayable. Measured costs for representative gestures:

```text
line                 1 command    ~31 bytes
200x150 rect outline 4 commands   ~120 bytes
200x150 rect filled  151 commands ~4.5 KiB
200x200 ellipse out  72 commands  ~2.2 KiB
200x200 ellipse fill 201 commands ~6.2 KiB
```

The ellipse is a polygonal approximation, not a true vector rasteriser; large
outlines show slight faceting. Filled shapes can expand to hundreds of commands,
so replaying them at 5 ops/s is slow. The preview approximates outline width
with a one-pixel guide and uses translucent fill.

## Bucket

The Bucket tool performs an iterative scanline flood-fill on the drawing layer.
Clicking a pixel fills its contiguous region with the selected palette colour.
Tolerance 0 requires an exact ARGB match; higher values accept pixels whose
maximum per-channel difference, including alpha, is within the tolerance. This
suits antialiased boundaries at the cost of potentially including boundary
pixels.

The fill is computed into a temporary canvas snapshot using an explicit
scanline stack, not recursion. Each contiguous horizontal span becomes one
ordinary `L` command with size 1. Those commands are then executed through the
normal dispatcher, so the committed pixels are reproducible, saveable and
replayable; the temporary mask is never uploaded directly. Exact matching leaves
the outline untouched, including irregular freehand outlines. Regions open to
the canvas edge fill until the edge or an opaque boundary. A target already
matching the selected colour is a no-op and adds no undo entry. Each fill is one
undo/redo action.

The fill is bounded to the existing 4,096-command / 1 MiB command budget. A
pathological mask needing more spans is rejected without changing the canvas.
Measured on WSLg: 200x150 rectangle interior 145 commands / 4,350 B; full
800x600 600 commands / 16,580 B. Flood + command application took about
12-38 ms for the 200x150 region and 15-26 ms for the full canvas across runs;
literal-command replay took about 8-13 ms. These are CPU/flood and
command-application timings, not a display FPS claim.

## Mandala demo

`examples/mandala.glon` is a self-contained 12-fold sunburst mandala built only
from ordinary `paint/draw-line`, `paint/draw-pressure-line`, palette and size
commands. It has a 16-sided outer frame, red radial rays, six tapered blue
pressure petals, a yellow inner ring and a green/black centre. The script is
1,947 bytes on disk (1,770 after comments) and produces 45 replay commands (one Clear, 38 lines, six pressure
segments), 1,344 command bytes.

`./build/glon-paint --mandala-demo` renders it immediately, saves
`glon-mandala-final.png`, captures replay prefixes at 25/50/75%, completes a
full smooth replay, compares it to the immediate render by `memcmp`, checks
Pause/Step/Back/Stop/End and repeated Load→Clear→Load, and prints timings.
Measured on WSLg: load about 0.1 ms, immediate script execution 400-465 ms,
replay preparation 310-356 ms and full smooth replay from the pre-playback
canvas 305-343 ms.
The final and replay images are pixel-identical; the stage frames show the
progressive build.

Paint now streams drawing commands through an experimental host-backed sink:
`paint/script-emit` writes each existing command representation byte-for-byte
through the frozen `HOST_PUTCHAR` output path, and the host captures it directly
into the bounded C command buffer. The old managed `paint/script-buf` sink is
retained for the standalone Glon CLI and non-streaming tests. Streaming removes
the old growing-string failure (60 calls OK, 61 failed): 1,000 short drawing
calls now complete with the same command bytes. The host sink is bounded to
1 MiB and the Glon operation counter to 4,096 commands; overflow invalidates the
whole stream and leaves the document unchanged. A residual limit remains around
1,350 calls because each command's temporary Glon strings still fragment the
non-compacting managed heap; 1,400 calls fail and the UI recovers cleanly.

## Writing 永

`examples/yong.glon` writes 永 with the existing pressure calligraphy commands.
It follows the conventional five-stroke order (dot; horizontal; vertical hook;
left sweep; right sweep). The Eight Principles are distributed as brush
behaviour inside those five strokes: 側 (dot), 勒 (horizontal), 努 (vertical),
趯 (hook), 策 (rising entry), 掠 (left sweep), 啄 (short right entry) and 磔
(right sweep).

The script uses palette colour 5, a script-only black (`0,0,0`); the interactive
UI still exposes the original four colour swatches. It sets maximum calligraphy
diameter 54 and ink opacity 255, then emits 18 connected pressure segments
within an approximately 380x400 px character box. It contains one Clear plus 18
`P` commands (19 replay commands) and stays well below the command, byte and
loader limits.

To watch it: File → Open Script → `examples/yong.glon`, enable Smooth strokes,
then Play. The five strokes develop in writing order; Pause freezes a partial
segment, Step/Back navigate command boundaries, and End commits only completed
segments. The final frame is RGBA-identical to immediate Run Script execution.

The circular brush gives hard round caps and geometric joins. The dot is
slightly blob-like, the hook termination is small, and the right 啄/磔 turn is
sharper than a brush turn. The character is recognisable, but this is not an
authentic brush rendering; no new renderer or stroke primitive is used.

## Replay

The REPLAY section prepares the loaded Glon script without touching the visible
drawing layer. Preparation executes the ordinary Glon pipeline, reads its
bounded command sink, validates every command, and stores the ordered stream in
a 4,096-command / 1 MiB playback buffer. Playback dispatches those stored
commands through the normal host drawing dispatcher; Glon is not rerun per
frame. Smooth strokes are a presentation of stored `P` commands, not a second
renderer or a reveal mask.

Controls:

- **Play** prepares if needed, then advances at the selected speed.
- **Pause** stops dispatch while preserving the current canvas and pending
  commands, including a partially animated stroke.
- **Back** reverses to the preceding completed command boundary. If a stroke is
  partially animated, its temporary progress is discarded first.
- **Step** completes the current partially animated command, or executes exactly
  one pending command and pauses when no command is partial.
- **Stop** cancels an active playback and restores the drawing layer captured
  before playback.
- **End** commits the currently visible completed-command prefix as one document
  action and leaves replay navigation. A partially animated command is
  discarded, never committed.
- **Smooth strokes**: animate pressure-aware `P` commands; default On.
- **Brush tip**: draw the optional approximate-diameter ring; default On.
- **Stroke duration**: 0.15, 0.3, 0.6 or 1.2 seconds; default 0.6.
- **Speed**: 1, 5, 20 or 60 operations/second; default 5.

### Smooth pressure strokes

When Smooth strokes is on, a `P` command is presented over its stroke duration:

1. The canvas immediately before the command is read once into a reusable
   pre-command buffer.
2. Each animation frame copies that buffer into one reusable work buffer and
   renders a partial segment from the command start to an integer-interpolated
   endpoint and pressure. The partial segment uses the same
   `raster_pressure_line` function as authoritative drawing.
3. Only the work buffer is uploaded to the canvas texture: one texture transfer
   per frame, with no GPU readback and no per-frame full snapshot.
4. On completion the pre-command buffer is restored and the original full `P`
   command is dispatched once through the normal command path. The final pixels
   therefore come from the authoritative full command, not accumulated partial
   alpha compositing.

Progress is an integer permille (`0..999`) derived from monotonic elapsed time
and the selected stroke duration. Endpoint and pressure interpolation use
round-to-nearest with ties away from zero. A pump advances at most 250 ms of
animation catch-up, so a scheduler stall cannot jump the whole stroke. Pause
freezes progress; resume continues from the same progress. Window resizing does
not reset progress or alter canvas coordinates. Stroke duration affects
presentation only; playback speed schedules command starts only; neither changes
the final image. If the selected duration is longer than the playback interval,
the animation duration dominates for `P` commands. Changing the duration applies
to strokes started after the change; turning Smooth strokes off during a partial
stroke completes that command authoritatively.

The replay counter displays completed commands plus `+1` while one command is
partially animated (for example `Playing 3+1/12`). The brush-tip ring is drawn
directly to the window after the canvas, using the aspect-fit mapping; it never
touches the drawing layer, composite, exported PNG or undo history.

Measured on the WSLg test build over 200 partial frames of the first
`calligraphy.glon` stroke: average 5.4–7.3 ms per frame, worst 8.4–12.5 ms,
exactly one texture upload per frame, no per-frame readback, and 3.84 MB extra
working memory (pre-command plus work buffer). These are CPU raster/upload
timings, not a display refresh-rate claim. The cost scales with the segment's
destination rectangle and brush diameter.

While replay navigation is available, `Ctrl+Z` moves back one replay command and
`Ctrl+Y`/`Ctrl+Shift+Z` moves forward one command. These are replay commands, not
document Undo entries. The inspector labels this explicitly. Once **End** (or
Stop) leaves replay navigation, ordinary document Undo/Redo is available again.

Backward navigation deterministically restores the saved pre-playback layer and
replays the command prefix; it never attempts to invert alpha compositing. This
handles Clear, eraser, zero-pressure and overlapping translucent calligraphy
commands correctly. Completion does **not** implicitly commit document history;
Back/Step remain available at Complete until End or Stop. End commits exactly
the completed prefix as one document action and invalidates redo only when that
prefix genuinely changes the canvas; a partial stroke is discarded. Stop
restores the pre-playback canvas
without adding history and preserves existing document redo history. A second
Play restarts from the same pre-playback state rather than accumulating pixels.

Replay states:

```text
Ready -> Playing -> Paused -> Complete -> Ended
             \         /
              Stop -> Ready
Preparation failure -> Error
```

`Ended` is a terminal display state after End; the next Play/Step prepares a
fresh replay from the then-current document. `Playing/Paused/Complete` own a
temporary drawing layer, not document history. Only End transfers that drawing
into document history.

The inspector displays Ready / Playing / Paused / Complete / Error, current and
total command counts, and the current speed. The inspector scrolls when the
window is too short; the canvas remains an 800x600 aspect-correct document.

Preparation and playback are atomic with respect to failure and Stop. While
replay navigation is active, painting, script execution, image replacement and
document Undo/Redo are disabled; Save remains available. Clear and Open Script
remain available as lifecycle actions: each cancels replay, restores the
pre-playback drawing and resets animation state before clearing or loading.
Window resizing does not affect playback or canvas coordinates.

Timing uses monotonic `SDL_GetTicks64`. Non-animated command scheduling applies
at most two commands per pump; animated progress is bounded to 250 ms of
catch-up per pump. Geometry and final pixels depend only on the stored command
order. Authoritative full-command dispatch still reads and updates a full
800x600 texture once per pressure command (earlier measured roughly 35 ms per
command on WSLg); partial animation frames avoid that readback. Therefore 60/s
is a nominal scheduling rate and may be limited by rendering throughput.

`examples/replay-demo.glon` is a short replay demonstration containing a clear,
an ordinary line, an eraser command and a pressure calligraphy taper. The
calligraphy example and replay demo both execute through the ordinary Glon
script pipeline. Replay tests compare complete RGBA snapshots at intermediate
prefixes after repeated Back/Step navigation, as well as immediate-vs-replay
final output.

## Glon command interface

Glon returns one newline-terminated command per event. The host only executes
these commands:

```text
L x1 y1 x2 y2 size r g b   draw an opaque line on the drawing layer
E x1 y1 x2 y2 size r g b   erase to transparency on the drawing layer
P x1 y1 p1 x2 y2 p2 max opacity r g b
                           pressure-aware calligraphy segment
C                          clear the drawing layer (background remains)
T title                    set the window title
I color size tool drawing calldia pressure opacity
                           update the UI mirror/status
S path.png                 export the 800x600 composite
Q                          quit
```

Mouse down/move/up invoke `paint/down`, `paint/move` and `paint/up`; those Glon
words emit `L` or `E` through the same `paint/line` path. Inspector controls
invoke `paint/set-color`, `paint/set-size` and `paint/set-tool`. Future Glon
scripts must use those same words and command interface; there is no C drawing
language and no script editor. Replay is host-side playback of the bounded
command sink, not a second Glon execution.

## Script resource diagnostics

For headless measurements without SDL, `--stack-probe` runs a generated script
in the normal persistent session and prints outcome, data/return stack depths,
managed heap usage, free extents, loader use and command-buffer size:

```sh
./build/glon-paint --stack-probe draw 60
./build/glon-paint --stack-probe draw 61
./build/glon-paint --stack-probe drawlong 40
./build/glon-paint --stack-probe nop4 200
./build/glon-paint --stack-probe draw 200 1   # reset the sink every call
```

The failure at `draw 61` is an allocator OOM/halt inside `r_alloc`, not a
data-stack or return-stack overflow. Controls (`arith`, `setsize`, `nop4`) and
sink-reset runs complete at 200 calls because they do not accumulate a large
managed string.

The streaming adapter has its own probes:

```sh
./build/glon-paint --stream-probe 1000       # 1,000 calls through the host sink
./build/glon-paint --stream-probe 100 1000   # force a small C sink; overflow/recovery
./build/glon-paint --stream-compare examples/mandala.glon
./build/glon-paint --stream-test              # 101-command app/replay regression
```

`--stream-probe` verified 40, 60, 61, 100, 500 and 1,000 calls all complete;
1,350 is the observed maximum and 1,400 fails on residual per-command managed
strings. `--stream-compare` matches the old and new streams byte-for-byte for
calligraphy, 永, mandala, rectangle, landscape and replay-demo. Forced sink
overflow reports `overflow=1`, discards the stream and succeeds on a following
short stream.

## Checks

```sh
# 31 Glon paint-logic tests
./glon jupyter/prelude.glon demo/shop/strings.glon \
  demo/shop/strings-ext.glon experiments/glon-gui-paint/paint.glon \
  experiments/glon-gui-paint/tests/paint-tests.glon

# SDL/Nuklear UI, image, script lifecycle, geometric/bucket tools, mandala,
# calligraphy, replay, smooth animation, 永, undo, resize, PNG and 10,000-move tests
make test

# Write start/25/50/75%/final PNG frames for the first stroke of a script and
# verify the final full replay equals immediate script execution.
./build/glon-paint --smooth-demo
./build/glon-paint --yong-demo

# Write one frame after each completed 永 stroke.
./build/glon-paint --yong-frames

# Render the mandala, save replay stages and verify immediate/replay equality.
./build/glon-paint --mandala-demo
```

The calligraphy and replay tests compare complete 800x600 drawing-layer
snapshots with `memcmp` for repeated, immediate-vs-replay and intermediate
replay-prefix command sequences.
The resize test verifies the rendered output framebuffer after each resize, not
only the logical canvas: the painted pixel, workspace margin, document shadow
and document outline must all land on the current destination rectangle.
On WSLg Wayland, `--cursor-test` also prints and checks the selected decoration
backend. The manual acceptance path is to drag the title bar, drag a side or
bottom resize border, use the maximize and restore buttons, and repeat those
cycles; the painted document must scale correctly and no stale shadow may
reappear.
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

- Native file dialogs require GTK3 development files at build time; without
  them the Nuklear path-entry fallback is used.
- Imported images are immutable backgrounds, not editable layers.
- Single drawing layer only; no layer creation or compositing.
- Custom Export and grid are disabled.
- Ellipses are 72-segment polygons; filled shapes emit one L command per row
  (up to 600) rather than a new fill primitive.
- Bucket fills with more than 4,096 horizontal spans or 1 MiB of commands are
  rejected rather than partially applied. Tolerance may include boundary pixels
  and can visually merge a close-coloured outline.
- The host streaming sink removes the old managed growing-string limit, but
  per-command temporary Glon strings still fragment the non-compacting managed
  heap at roughly 1,350 drawing calls; 1,400 fails. The host C sink is bounded
  at 1 MiB and the Glon operation counter at 4,096 commands.
- No PDF decoding or script editor; replay is stored-command playback, not
  recording.
- Smooth animation applies only to pressure-aware `P` commands; ordinary lines,
  erasers and clears remain whole-command steps. It is a partial presentation of
  the existing CPU calligraphy renderer, not a vector brush or bristle model.
- Scripts are ordinary Glon source, not a drawing DSL; replay Step governs stored
  drawing commands, not Glon source evaluation.
- Nuklear is vendored in `third_party/` (public-domain Nuklear v4.13.4 and its
  SDL2 renderer backend).
- Native File Dialog Extended is vendored in `third_party/nfd/` under the
  Zlib license.
