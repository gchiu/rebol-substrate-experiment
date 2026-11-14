# Glon AvP Alien artwork — visual reference, not game-ready frames

This image is a **conceptual animation reference sheet**. Despite the labels within the illustration, it is NOT a validated 128x128 RGBA transparent sprite atlas. It contains background, labels, variable spacing, and possibly inconsistent proportions. Do not split it into a fixed grid or claim its frames are ready for use.

## Desired visual identity
Dark glossy biomechanical predator; elongated eyeless dome; lowered asymmetric clawed arms; hunched stance; digitigrade legs; articulated tail; menacing silhouette. Avoid the earlier flower/insect appearance.

## Engineering handoff
1. Inspect current Alien texture loader, billboard dimensions, frame selection, facing conventions, animation timing and occlusion.
2. Retain existing Alien state/AI/collision/timing and frozen S1 VM.
3. Treat this sheet as art direction only. For real integration request/produce **individual transparent, tightly aligned** frames with consistent pivot/foot baseline, verified against a checkerboard and in-game at near/mid/far distances.
4. Do not pretend this sheet's dark background is alpha; do not apply crude global dark-colour removal to the entire sheet without visual validation.
5. Do not alter the corridor/floor/lighting or introduce new gameplay to compensate for missing assets.
6. Report which assets remain missing, screenshots and measured performance.
