GLON vs ALIEN — G6j Windows candidate
====================================

Launch:

  D:
  cd D:\repos\glon-worktrees\glon-avp-g6j\dist\windows
  run-g6j.bat

Controls:
  W/S/A/D             move/strafe
  Left/Right arrows   turn
  Up arrow            jump
  Down arrow          gradually look down; release holds angle
  R                   smoothly return camera to horizontal
  Space               fire
  Left mouse          fire
  E                   door
  Enter               start from title
  C                   credits from title (does not start gameplay)
  Escape              credits -> title; title/game -> quit
  M / [ / ]           mute / volume

The game starts with three dormant alien cocoons. Approach one to alert it; it
pulses and has an 8-second hatch countdown. Two rifle hits destroy a cocoon and
prevent its scuttler from ever appearing. Surviving cocoons hatch into the
existing scuttlers. The adult alien remains active independently.

This is an unaccepted candidate pending physical Windows testing. Cocoon
sprites are extracted/cleaned from the supplied contact sheet; some floor
debris remains on a few frames. Cocoons are rendered world-space but are not
solid player-collision volumes. See evidence/G6J-COCOON-LIFECYCLE-REPORT.md for
tests, screenshots, measurements, SHA-256 and limitations.
