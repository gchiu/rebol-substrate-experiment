GLON vs ALIEN — G6i.2 Windows candidate
======================================

Launch:

  D:
  cd D:\repos\glon-worktrees\glon-avp-g6i2\dist\windows
  run-g6i2.bat

Controls:
  W/S/A/D             move/strafe
  Left/Right arrows   turn
  Up arrow            jump
  Down arrow          look down (held; smooth return)
  Space               fire
  Left mouse          fire
  E                   door
  Enter               start from title
  C                   credits from title (does not start gameplay)
  Escape              credits -> title; title/game -> quit
  M / [ / ]           mute / volume

Camera pitch is a true downward pitch with no roll; pitch is clamped at 75°
and reset on restart. Player movement slides on walls, retreats from cabinets
and corners, and diagonal speed is normalized. Scuttlers use local obstacle
avoidance around barrels/cabinets; closed doors remain solid.

This is an unaccepted candidate pending physical Windows testing. No new
enemies, acid, damage, weapons, audio assets or asset optimizations. Scuttler
damage attacks and distinctive movement/proximity sounds remain outstanding.
See evidence/G6I2-CAMERA-COLLISION-NAV-REPORT.md for tests, screenshots,
measurements, SHA-256 and limitations.
