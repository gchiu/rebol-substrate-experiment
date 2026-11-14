# G5x2 door-texture evidence

Matched before/after captures for the G5x2 blast-door leaf upgrade. See
`../../G5X2-DOOR-TEXTURE-REPORT.md` for the analysis.

- `before/` — G5x assets, commit `174b9eb`: 960 and 1920 door states, 24-unit
  close-up, oblique view, test log, executable size/hash and benchmark log.
- `after/` — G5x2 assets, commit `24c5860`: the same poses and resolutions,
  final test log, executable size/hash and the randomized old-vs-new benchmark.
- `compare-*.png` — labeled before/after montages.

Regenerate with (from `experiments/glon-doom`):

```sh
DOOM_ROOT=../.. ./build/doom --gl-door-shots DIR 960 600
DOOM_ROOT=../.. ./build/doom --gl-door-shots DIR 1920 1080
DOOM_ROOT=../.. ./build/doom --g5u-nav DIR 1920 1080
```

The `--g5u-nav` `02-route-at-closed-door` frame is the 24-unit room-side
close-up. Raw render captures are `.ppm`; the committed files are lossless PNG
conversions of those frames.
