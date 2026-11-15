GLON vs ALIEN — G6i.1 Windows candidate
======================================

Launch from Windows:

  D:
  cd D:\repos\glon-worktrees\glon-avp-g6i1\dist\windows
  run-g6i1.bat

Controls:
  W/S/A/D             move forward/back/strafe
  Left/Right arrows   turn
  Up arrow            jump
  Down arrow          look down (held; releases smoothly to level)
  Space               fire rifle
  Left mouse          fire rifle
  E                   open nearby door
  Enter               start from title
  C                   credits from title
  Escape              credits -> title; title/game -> quit
  M / [ / ]           mute / volume

Title screen shows live VM capacity, loader reserved/occupied/free, globals,
managed-heap high-water, and live Windows working/peak/private memory. VM cells
are 8 bytes on x64: 65,536 cells = 524,288 bytes. Loader/global/heap regions are
parts of the same VM address space and must not be added together.

G6i.1 is an unaccepted candidate pending physical Windows acceptance. It has no
new enemies, acid, attack damage or sound system. Distinctive scuttler
movement/proximity sounds remain outstanding. Packaging was cross-compiled;
see the G6I1 report and evidence for tests, screenshots, SHA-256 and known
limitations.
