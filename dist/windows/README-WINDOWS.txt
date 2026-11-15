Glon AvP — G6f.2 Windows build
============================

Launch from Windows (D: drive):

  D:
  cd D:\repos\glon-worktrees\glon-avp-g6f2\dist\windows
  run-g6f2.bat

Controls: W/A/S/D move, arrow keys turn, E door, F or left mouse rifle
fire, M mute, [ / ] volume, Esc quit.

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
