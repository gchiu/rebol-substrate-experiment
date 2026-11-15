# G6f.2 — Windows Acceptance Record

Status: **ACCEPTED — known-good Windows baseline**

- Build: G6f.2
- Implementation commit: `9635473`
- Evidence/documentation commit: `47e6264`
- Branch: `glon-avp-g6f2`
- Frozen S1: `f90496c26dc45c7a387d8fc8639cd2781507d3d2` (unchanged)
- Windows executable: `dist/windows/doom.exe`
- Windows executable size: 6,592,000 bytes
- Windows executable SHA-256:
  `2b305734c2d7ae58f469dd7e2b802e492aab64bbd27a1014a91ffc5e1114a279`
- Windows distribution size: 22,123,020 bytes
- Windows launcher: `dist/windows/run-g6f2.bat`

## Verification

Automated verification passed as reported:

- Linux full `make test` and frozen S1 check passed.
- Native Windows self-test, combat, door-sync, corpse-stability, audio-e2e,
  WASAPI audio-device and SFX-device tests passed.
- Native Windows GL door-sequence and corpse/muzzle regression captures
  passed.
- The door-sync test and traces confirmed corridor visibility through every
  nonzero aperture and one close/open event per transition.
- The close clip starts at the measured 410 ms offset, aligning its impact with
  the 267 ms closed state.

Physical Windows verification: **user-confirmed, 15 November 2026.**

Accepted corrections:

1. continuous door visibility through the physically unobstructed aperture;
2. synchronized closing audio accompanying the closing movement and latch.

This acceptance covers those two corrections specifically. It does **not** claim
comprehensive physical verification of every gameplay feature. Automated native
Windows tests are not a substitute for human playtesting.

## Preserved limitations

- Physical Windows playtesting remains the final acceptance for features other
  than the two accepted door corrections; no comprehensive physical report
  exists for combat, corpse, muzzle flash, long sessions or all machines.
- CPU corpse rendering is a centered/flattened silhouette; GL corpse rendering
  uses two low fixed world planes, not a 3D prone body.
- CPU door rendering is a per-column aperture approximation.
- The close audio starts 410 ms into the existing asset; initial asset content
  is skipped. An edited asset would be cleaner.
- Loader headroom is constrained; G6f.2 removed unused runtime `doom/lines`
  legacy data, not level geometry or gameplay rules.
- GL vs CPU can behave differently; physical Windows acceptance is for the
  accepted build and machine tested.

## Authority

Git and the committed reports/source remain authoritative. This record marks a
known-good baseline; it does not replace source control.
