Glon AvP — G6h.2 Windows build
============================

Launch from Windows (D: drive):

  D:
  cd D:\repos\glon-worktrees\glon-avp-g6f4\dist\windows
  run-g6h2.bat

Controls: W/A/S/D move, arrow keys turn, E door, F or left mouse rifle
fire, Space jump, M mute, [ / ] volume, Esc quit.

Audio uses SDL2's native WASAPI backend. The game does not use PulseAudio or
WSLg. SDL2.dll is required; Windows UCRT and OPENGL32 are system components.
The ambient and event recordings retain their Freesound/CC attributions in
assets-audio-SOURCE.txt and assets-audio-sfx-SOURCE.txt. SDL2 is under its
zlib licence (SDL2-LICENSE.txt).

G6e combat is retained. G6f adds a deterministic death collapse: four hits
kill, the alien tips to the floor over 18 fixed ticks, and remains as a
persistent low-collision corpse. The corpse cannot move, attack or take damage;
reset restores the living alien. G6f.2 keeps the corridor visible through the shrinking door aperture and
starts the impacted closing sample at the transition start so its latch aligns
with the closed door. This build is cross-compiled; see the G6f.2 report.

G6f.3 keeps the G6f.2 door aperture and closing-audio corrections unchanged.
It moves the OpenGL muzzle flash from the retired hard-coded hand/forearm
anchor to the visible barrel exit in weapon-sprite space. Recoil and viewport
resizing use the same weapon transform, so the flash follows the barrel.
This build is awaiting physical Windows verification; it is not yet an
accepted baseline.

G6f.4 corrects alien collision ownership: the living/dying alien is solid to
player movement, the terminal corpse remains rendered but has no collider, and
walls/doors keep the existing check-all path. This build is awaiting physical
Windows verification; it is not yet an accepted baseline.

G6g adds a deterministic Spacebar jump owned by Glon. Jumping preserves normal
horizontal and air control, clears the low corpse only when the player's feet
are above its collision top, and cannot bypass walls, closed doors, living or
dying aliens. Camera rise/fall is presentation-only in OpenGL.
This build is awaiting physical Windows verification; it is not yet an accepted
baseline.

G6h adds three independent scuttlers (rug babies) alongside the adult
Xenomorph, with independent health/AI/death and directional 64x32 sprites.
Scuttlers are low obstacles; jump over them when feet clear their 14-world-unit
top. The loader arena is expanded for this game build to 25,000 cells using the
unused M1 task arena; frozen S1 is unchanged. This build is awaiting physical
Windows verification and is not an accepted baseline.

G6h.1 fixes the first physical visibility failure: scuttlers now appear on the
actual Windows renderer at the start (one in the starting room) and in the
corridor. The correction is LOS-gated rendering, draw-after-weapon ordering,
reachable spawn positions and a larger low-sprite scale. Frozen S1 is unchanged.
Scuttler rendering remains a screen-space overlay, so world-depth occlusion is
approximate. This build awaits physical verification and is not accepted.

G6h.2 replaces the screen-space scuttler overlay with camera-facing world-space billboards: floor-anchored, depth-tested against walls/doors/props, and behind the foreground weapon. Visual height is 30 world units while collision stays low. Small-enemy aim tolerance is bounded and LOS-gated. The proximity monitor now derives all four contacts from Glon multi-entity state; it shows living scuttlers in addition to the adult and removes contacts on death. This build awaits physical verification and is not accepted.
