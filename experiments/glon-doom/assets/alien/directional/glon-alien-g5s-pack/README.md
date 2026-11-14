# Glon AvP — G5s Alien directional artwork candidates

Two 1536×1024 RGBA sheets with transparent backgrounds, showing frontal approach, three-quarter, side run, rear retreat and forward attack poses. **These are source reference sheets, not validated animation atlases.** They are not 512×512 independent sprites; individual characters occupy much smaller regions. Do not upscale cropped frames and claim native high resolution. No firing/weapon/corridor changes are requested.

## DS integration requirements

1. Work on a new branch/worktree from the intended combined Alien/weapon baseline; verify the G5r rifle and G5q Alien histories before merging, do not silently overwrite either.
2. Inspect the actual alpha channel and find frame bounds; reject overlapping silhouettes, cropped tails, or poses with inconsistent anatomy. Never use fixed-grid crops without visual checks.
3. Extract candidate frontal, three-quarter, side, rear and attack frames with transparent backgrounds, common ground pivots and documented scale. Record source-sheet rectangle and effective pixel dimensions for each frame.
4. Do not invent missing animation frames or assert smooth gait if available images do not support it. Report the need for individually generated higher-resolution frames if the close-up remains fuzzy.
5. Select sprites using actual relative facing direction and motion, preserve Glon AI/collision and frozen S1, preserve existing corridor/floor and rifle.
6. Capture real in-game screenshots/video of head-on approach, diagonal movement, lateral crossing, retreat and attack at both near and far distances; report frame rate and tests. Commit locally only, no push/merge/tag/deploy.

## Quality caveat

The source sheets are 1536×1024 overall, but **each pose has much lower effective resolution**. A larger ZIP file does not itself fix close-range blur. For a genuinely sharp close-up, request separately rendered single-pose 768–1024 px sprites with consistent anatomy and alpha, then build the animation from those.
