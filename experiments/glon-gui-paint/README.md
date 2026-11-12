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

## Checks

Headless Glon logic tests:

```sh
./glon jupyter/prelude.glon demo/shop/strings.glon \
  demo/shop/strings-ext.glon experiments/glon-gui-paint/paint.glon \
  experiments/glon-gui-paint/tests/paint-tests.glon
```

SDL event/PNG smoke test (uses an injected SDL event queue, then checks the PNG):

```sh
make test
```

The host has no SDL HOST primitives in frozen S1; this session-call bridge is the
host limitation documented by the experiment.
