# Glon Paint (G1 native host)

Glon owns the paint decisions: colour, size, brush/eraser mode, whether an
event draws, clear, save and quit. `paint-host.c` only creates SDL2 resources,
translates SDL input into calls, and executes the command strings Glon returns.

## Build

On Debian/Ubuntu:

```sh
sudo apt install build-essential libsdl2-dev libpng-dev
make
```

Run from this directory (the default library root is `../..`):

```sh
./build/glon-paint
```

`PAINT_ROOT=/path/to/checkout` can point at the repository root. The native
`glon` CLI must be available there for the headless tests.

## Controls

| Input | Action |
| --- | --- |
| Left-drag | draw with the current brush |
| `1`–`4` | red, blue, green, yellow |
| `+` / `-` | increase/decrease brush size |
| `B` | brush |
| `E` | eraser |
| `C` | clear canvas |
| `S` | save `glon-paint.png` |
| `Esc` | quit |
| Close window | quit |
| Resize window | scale the 800×600 canvas to the window; mouse coordinates follow the scale |

## Checks

Headless Glon logic tests:

```sh
./glon jupyter/prelude.glon demo/shop/strings.glon \
  demo/shop/strings-ext.glon experiments/glon-gui-paint/paint.glon \
  experiments/glon-gui-paint/tests/paint-tests.glon
```

SDL event/PNG smoke test, resize/coordinate-mapping regression, and a
deterministic 10,000-move event-bridge stress test:

```sh
make test
# or directly:
./build/glon-paint --resize-test
./build/glon-paint --move-stress 10000
```

The stress mode checks that transient per-event parse blocks do not grow the
permanent loader heap and then saves a PNG. The resize test checks mapping and
black-pixel-free redraw at several window sizes, including repeated resizes.

The host has no SDL HOST primitives in frozen S1; this session-call bridge is the
host limitation documented by the experiment.
