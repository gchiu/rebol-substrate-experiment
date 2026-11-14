# G5q Alien reference conversion

Source: `alien-animation-reference.png` (1536×1024 concept reference sheet).
The sheet has a transparent background but contains labels, an information box,
variable spacing and some merged silhouettes. It is not a validated fixed-grid
sprite atlas.

Selected poses were cropped from isolated alpha regions, excluding the labels and
info box, then normalized by `tools/convert_g5q_alien.py`:

- 192×144 RGBA output canvas
- one common scale (0.8780) for all selected poses
- common bottom-foot baseline
- alpha-weighted foot pivot centered horizontally

Frames:

- `idle.png`
- `walk1.png`
- `walk2.png`
- `attack.png`
- `side.png`

These replace the previous five procedural render textures only. The full sheet
sequences (idle 6, walk 8, turn 6, hit 4, death 8) are not integrated because the
existing Alien render state/timing was preserved. Hit, turn and death still need
individually authored, pivot-validated frames if those states are added later.
