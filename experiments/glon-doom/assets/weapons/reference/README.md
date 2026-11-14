# Glon AvP — first-person weapon art references

Four generated reference illustrations:
- pulse-rifle-reference.png
- shotgun-reference.png
- pistol-reference.png
- flamethrower-reference.png

These are **full-scene concept illustrations**, NOT transparent weapon sprites or usable RGBA atlases. The background corridor is baked into each image; do not display the entire image as an overlay. Extracting clean weapons and hands will require careful masking/redrawing, transparent edges, and alignment at actual gameplay resolutions. Do not claim they are game-ready.

The pulse rifle's green `60` is **baked into the illustration** and is not an ammunition counter. For an interactive implementation, mask/remove the baked digits and have Glon own ammunition state; the host should draw the live count.

Suggested integration sequence:
1. Inspect the four references and select a consistent lower-screen anchoring and scale.
2. Produce transparent weapon-and-hands assets, verify alpha and silhouette on bright/dark test backgrounds, and avoid baking in the corridor.
3. Implement one weapon visually (pulse rifle) first; validate screen placement and clipping at 640x400, 960x600 and maximised window.
4. Only then add weapon switching and others. Preserve existing corridor/floor/Alien state and frozen S1.
5. Do not invent gameplay mechanics or report working shooting unless implemented and tested.

Source: generated artwork references supplied in the conversation.
