Glon AvP — G6g Windows build
============================

Launch from Windows (D: drive):

  D:
  cd D:\repos\glon-worktrees\glon-avp-g6f4\dist\windows
  run-g6g.bat

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
