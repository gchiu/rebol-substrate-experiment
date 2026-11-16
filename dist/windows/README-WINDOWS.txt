GLON vs ALIEN — G6j.2 Windows candidate
======================================

Launch:

  D:
  cd D:\repos\glon-worktrees\glon-avp-g6j2\dist\windows
  run-g6j2.bat

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

Three dormant cocoons replace the initial scuttlers. Approach a cocoon to alert
it; it pulses and has an 8-second hatch countdown. Two rifle hits destroy it and
prevent its scuttler from ever appearing. A destroyed cocoon collapses over
about 0.5 seconds into a low floor-level organic heap. A successful hatch leaves
an upright, relatively short, broken-open empty shell with a dark hollow and
releases its scuttler.

G6j.2 also moves the c1 cocoon beside the FLOW A / Engineering Division wall far
enough south that depth testing no longer clips it.

Unaccepted candidate pending physical Windows testing. Some extracted cocoon
frames retain minor floor/debris; cocoons are not solid player-collision volumes.
No new enemies, acid, player damage, weapons or audio assets. See
evidence/G6J2-WALL-AND-HATCH-REPORT.md for captures, tests, measurements,
SHA-256 and limitations.
