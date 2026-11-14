# G5r — pulse-rifle reference integration

Branch/worktree: `glon-avp-g5r`, `/tmp/opencode/glon-avp-g5r`  
Starting commit: `a13144028abc11e51e053bae50a4fe07f8d094a8`  
Commit: `da3ef2578310b62f34b041f348ba1f2c3ade95a7`

Separate worktree from G5p. G5n/G5p were not modified. No push/merge/tag/deploy.
No corridor, floor, Alien AI, controls, motion tracker or frozen S1 changes.

## 1. References inspected

`glon-avp-weapons-reference-pack.zip` contains four 1672×941 full-scene
illustrations. They are opaque corridor scenes, not transparent weapon atlases:

- pulse rifle: lower centre-right rifle, one visible hand, baked green `60`
- shotgun: pump-action, lower right, one visible hand
- pistol: compact pistol, centre, gloved hand
- flamethrower: large tank/nozzle, lower right, orange pilot light

The supplied README explicitly warns that the corridor is baked in and that the
`60` is not an ammunition counter. Only the pulse rifle was converted and
integrated. The other three were not falsely presented as ready assets.

## 2. Transparent pulse-rifle asset

`tools/convert_g5r_weapon.py`:

- crops the lower weapon/hand from the pulse-rifle reference;
- uses a hand-traced outer silhouette and a separate carry-handle opening so the
  corridor is excluded;
- supersamples the mask and erodes the edge to reduce background fringes;
- blanks the baked `60` display and leaves a dark screen/bezel;
- emits `assets/weapons/pulse-rifle-hands.png` (768×406 RGBA) and
  `weapon_sprites.h`.

Alpha checks:

- checker: `/tmp/g5r-pulse-transparent-checker5.png`
- light background: `/tmp/g5r-pulse-on-light.png`
- dark background: `/tmp/g5r-pulse-on-dark.png`

The silhouette is a manual reference-derived mask, not a production alpha asset.
Only one hand is visible in the reference. A second support hand, recoil poses,
muzzle flash and reload frames do not exist here.

## 3. Glon owns ammunition

- `doom.glon` owns `doom/ammo`, initialized to **24**.
- The status block prints it as an additional field.
- `Status.ammo` reads it every frame; `get_status()` now captures 18 values.
- The regression suite checks `Glon-owned ammunition == 24`.
- OpenGL blanks the illustrated digits and draws the live `st->ammo` value with
  green seven-segment quads in the display panel.

No firing, reload, ammo consumption or weapon-switching mechanics were invented.
The illustrated `60` is not used. The displayed `24` comes from Glon state.

## 4. Rendering and placement

`gl_scene()` draws the weapon after the world/Alien and before the tracker, in a
screen-space orthographic pass:

- width is 60% of the drawable width, aspect preserved;
- anchored to the bottom-right at 99% width / 102% height;
- alpha blended, depth test disabled for the overlay;
- live count placed from output-space panel bounds (444,194)–(519,251);
- resizes from `gl_w`/`gl_h` each frame.

The static weapon does not alter controls or CPU-renderer behaviour. Software
fallback still has no first-person weapon.

## 5. Verification

Screenshots:

- 640×400: `/tmp/g5r-final-640/01-entrance.png`
- 960×600: `/tmp/g5r-final-960/01-entrance.png`,
  `/tmp/g5r-final-960/15-alien-attack.png`
- 1920×1080: `/tmp/g5r-final-max/01-entrance.png`,
  `/tmp/g5r-final-max/15-alien-attack.png`
- resolution comparison: `/tmp/g5r-weapon-resolutions.png`
- gameplay pair: `/tmp/g5r-gameplay.png`

Performance (`--gl-bench 120 960 600`): **67.3 FPS**, sim 6.79 ms, submit
0.75 ms, total 14.87 ms. Loader 53,337 cells, heap 33,440. Executable
5,500,512 bytes; generated weapon header 3,743,322 bytes.

`make test` passes: self-test, projection, input, tracker, resize, stationary,
flat/object tests and trace. Frozen S1 passes at
`f90496c26dc45c7a387d8fc8639cd2781507d3d2`.

Remaining weapon work: convert shotgun/pistol/flamethrower with separate
per-weapon silhouettes and hands; implement switching, firing/reload and
ammunition consumption only after those mechanics are explicitly designed and
tested. Do not describe the current static pulse rifle as a complete weapon
system.
