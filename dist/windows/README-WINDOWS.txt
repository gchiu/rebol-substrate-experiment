GLON vs ALIEN — G6i.3 Windows candidate
======================================

Launch:

  D:
  cd D:\repos\glon-worktrees\glon-avp-g6i3\dist\windows
  run-g6i3.bat

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

Pitch starts at 0, rises at 30°/s while Down is held, clamps at 75°, and R
returns it at 300°/s without resetting the game. No camera roll.

Scuttlers use persistent per-enemy detour waypoints around the actual barrels
and cabinets, then resume direct pursuit; closed doors remain solid.

Unaccepted candidate pending physical Windows testing. Scuttler damage attacks
and distinctive movement/proximity sounds remain outstanding. No new enemies,
acid, player damage, weapons, audio assets or asset optimizations. See
evidence/G6I3-PRECISION-AIM-NAV-REPORT.md for tests, screenshots, measurements,
SHA-256 and limitations.
