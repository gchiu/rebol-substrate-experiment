# G5q — Alien orientation and locomotion correction

Starting commit: `a13144028abc11e51e053bae50a4fe07f8d094a8`  
Worktree: `/tmp/opencode/glon-avp-g5p`  
Commit: `25162cfcabe4da3b24ec0b179f1bfbc15d7cbe6b`

## Diagnosis

The Alien's world-space movement is correct. `doom.glon` tracks the player and
moves independently along X and Y with the existing speed and collision rules.
The visual failure is in sprite selection plus the supplied artwork.

All five converted poses are **right-facing side profiles**:

- `idle.png` — side profile
- `walk1.png` — side profile
- `walk2.png` — side profile
- `attack.png` — side profile, mouth open
- `side.png` — side profile

Frame contact sheet: `/tmp/g5q-frames.png`

`alien_frame_for()` selects:

- attack state → `alien_attack`
- walk state with large lateral offset (`|tside| > 600`) → `alien_side`
- otherwise walk → alternating `walk1`/`walk2`
- idle with large lateral offset → `alien_side`
- otherwise idle → `alien_idle`

When the Alien approaches directly, `tside` is near zero, so the code correctly
chooses `walk1`/`walk2`—but those frames themselves are side profiles. The result
is the reported sideways shuffle. There is no frontal, three-quarter, or rear
artwork to select. Rotating a side profile would violate the task constraints and
would not create valid anatomy, so no such transform was introduced.

## Locomotion

`walk1` and `walk2` do contain different leg positions, but both are lateral
strides. They cannot convey a forward loping gait toward the camera. Translating
or scaling one frame would only make the side-shuffle more obvious.

## Additional artwork required

To meet the acceptance criterion, the Alien needs a new, aligned set:

1. Frontal/three-quarter approach idle and walk cycle (at least two walk frames
   with alternating legs, head/torso momentum, arms and tail).
2. Frontal or three-quarter attack frame(s) for close-range state.
3. Rear/three-quarter walk cycle if retreating movement must be represented.
4. Left/right crossing variants, or an explicitly approved horizontal-mirror
   convention for side frames.
5. Consistent foot baseline and pivot metadata for every frame.

## Verification and scope

No corridor, floor, lighting, weapon, VM or collision changes were made. The
existing regression tests still pass, but automated tests cannot satisfy the
acceptance criterion because the required directional artwork is absent.

Existing side-profile evidence:

- `/tmp/g5q-final/11-alien-tracker.png`
- `/tmp/g5q-final/15-alien-attack.png`
- `/tmp/g5q-before-after.png`

The Alien still visibly side-shuffles when approaching. This report documents the
blocking artwork gap rather than claiming a successful orientation fix.
