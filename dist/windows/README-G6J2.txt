GLON vs ALIEN — G6j.2 Windows candidate
======================================

Launch:

  D:
  cd D:\repos\glon-worktrees\glon-avp-g6j2\dist\windows
  run-g6j2.bat

This candidate contains both G6j.2 corrections:

1. Cocoon c1 beside the FLOW A / Engineering Division north wall has been moved
   50 world units south so its billboard clears the wall with depth testing on.
   c0 and c2 are unchanged.

2. The HATCHED state now uses the complete empty-shell artwork from the supplied
   cocoon contact sheet. It reads as a broken-open, upright, relatively short
   shell with a dark hollow interior and irregular rim, not a standing alien.
   DESTROYED remains the G6j.1 low floor-level collapse.

Controls:
  W/S/A/D             move/strafe
  Left/Right arrows   turn
  Up arrow            jump
  Down arrow          gradually look down; release holds angle
  R                   smoothly return camera to horizontal
  Space / left mouse  fire
  E                   door
  Enter               start from title
  C                   credits from title (does not start gameplay)
  Escape              credits -> title; title/game -> quit

Preserved: independent cocoon records, 8-second activation, hatching and
scuttler release, two-hit destruction, G6j.1 collapse, spawn coordinates, depth
testing and frozen S1.

Unaccepted candidate pending physical Windows testing. Cocoons are not solid
player-collision volumes. Some extracted artwork retains minor floor/debris.
No new enemies, acid, player damage, weapons, audio assets or gameplay features.
See evidence/G6J2-WALL-AND-HATCH-REPORT.md for captures, tests, measurements,
SHA-256 and limitations.
