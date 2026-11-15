Glon AvP — G6e Windows build
============================

Launch from Windows (D: drive):

  D:
  cd D:\repos\glon-worktrees\glon-avp-g6e\dist\windows
  run-g6e.bat

Controls: W/A/S/D move, arrow keys turn, E door, F or left mouse rifle
fire, M mute, [ / ] volume, Esc quit.

Audio uses SDL2's native WASAPI backend. The game does not use PulseAudio or
WSLg. SDL2.dll is required; Windows UCRT and OPENGL32 are system components.
The ambient and event recordings retain their Freesound/CC attributions in
assets-audio-SOURCE.txt and assets-audio-sfx-SOURCE.txt. SDL2 is under its
zlib licence (SDL2-LICENSE.txt).

G6e adds the first combat loop: F or left mouse fires one accepted shot per
press, uses the existing 24-round magazine, damages the alien, shows hit and
death states, and stops the alien at zero health. This build is cross-compiled;
see the G6e report for verification and limitations.
