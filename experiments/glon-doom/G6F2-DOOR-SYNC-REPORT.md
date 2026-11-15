# G6f.2 — Door Visibility and Audio Synchronisation

Status: implemented and verified on Linux and native Windows. Physical Windows
playtest remains for the user.

- Branch/worktree: `glon-avp-g6f2` at
  `/mnt/d/repos/glon-worktrees/glon-avp-g6f2`
- G6f.1 baseline: `72769fc` (unchanged)
- Frozen S1: unchanged at
  `f90496c26dc45c7a387d8fc8639cd2781507d3d2`
- Implementation commit: `9635473`
- Documentation/evidence commit: see the commit following `9635473`

## 1. Premature corridor disappearance — root cause

The door has four state values involved in rendering:

- `doom/door-open`: continuous 0..64 travel.
- `door/door-target`: 0 or 64.
- `doom/door-blocked`: binary threshold `door_open < 32`, used for collision/LOS.
- Aperture: `2 * door_open` world units through the two 64-unit leaves.

The CPU renderer in `doom/render-column-block` used the **binary blocked flag**
to decide whether to replace the entire doorway with door colour 4. At
`door_open=28`, `door-blocked` became 1 and the CPU drew a full closed door even
though the physical leaves had a 56-unit gap. The corridor therefore vanished
before the door closed.

The OpenGL `gl_blast_door` path already moves both leaves from `door_open` and
was geometrically progressive. The defect reproduced in the CPU render path,
which is also the fallback path. The correction makes the CPU renderer use the
same aperture geometry rather than the collision threshold.

## 2. Door geometry correction

`doom/render-column-block` now renders a door column only when the ray hits the
solid part of a leaf:

```
leaf north: y <= 320 - door_open
leaf south: y >= 320 + door_open
```

The centre gap `(320-door_open, 320+door_open)` remains corridor until
`door_open` reaches zero. No cosmetic delay and no binary visibility delay was
added. Collision and LOS thresholds remain unchanged.

Door trace before correction (old CPU source):

```
open=32 blocked=0 center=3
open=28 blocked=1 center=4   <-- full door while 56-unit gap remains
open=4  blocked=1 center=4
open=0  blocked=1 center=4
```

After correction:

```
open=32 blocked=0 center=3
open=28 blocked=1 center=3   <-- corridor through 56-unit apex
open=4  blocked=1 center=3   <-- corridor through 8-unit apex
open=0  blocked=1 center=4   <-- door physically closed
```

This was observed from both the room side (`620,320`, angle 0) and the corridor
side (`900,320`, angle 128). Full CPU and GL frame sequences are in evidence.

## 3. Closing audio — root cause

The event itself was not delayed. `sfx_door_event()` detects the target/open
relationship every audio update. At the first closing frame:

```
door-trace frame=0 open=64 target=0 event=SFX_DOOR_CLOSE
```

Subsequent frames return event 0. Opening likewise triggers once at frame 0.
The delay was in the **asset content/time relationship**, not the transition
event:

- Door animation: 16 ticks at 60 Hz = **267 ms**.
- `door_close.wav`: audible from about **2.8 ms**, but its major impact onset is
  at about **680 ms**.
- Playing the clip from its start therefore placed the impact about **413 ms
  after the door was already closed**.

Waveform and timing evidence are in `evidence/g6f2/audio-waveform.txt`.

## 4. Audio correction

The closing clip now starts at a measured **410 ms** playback offset, not at
0. The first 10 ms at that offset still has strong movement audio (peak ~6287),
and the 680 ms impact onset lands **270 ms** after the transition starts,
essentially at the 267 ms fully-closed frame.

- Opening: starts at asset offset 0; first audible at ~10 ms; movement and
  opening event start together.
- Closing: event starts at frame 0; playback starts at 410 ms offset; impact
  lands at 270 ms.
- No repeated trigger: one close event and one open event per transition.
- No audio asset was rewritten or replaced.
- Gunfire, cargo and wall audio are unchanged.

This is a deliberate timing correction, not a blind timestamp shift. A cleaner
future option would be an edited closing asset that contains a short movement
lead and the latch at 267 ms; the current change keeps the existing asset and
starts it at the measured movement/impact region.

## 5. Tests

New `--door-sync-test` is part of `make test`:

- closing from room and corridor side, every frame;
- fully open, partial, nearly closed and fully closed rendering;
- centre corridor remains visible through every nonzero aperture;
- no binary cutoff at `door_blocked=1`;
- close event fires once at transition start;
- open event fires once at transition start;
- repeated calls during the same transition produce no event;
- reset target/open and repeated transition checks.

`--door-trace` logs every frame from both sides. `--cpu-door-sequence` and
`--gl-door-sequence` capture the complete 17-frame closing sequence.

Results:

- Linux full `make test`: **PASSED** RC 0.
- Frozen S1: PASSED.
- G6e combat, G6f death lifecycle and G6f.1 corpse/muzzle tests remain passing.
- Native Windows self-test, combat, door-sync, corpse-stability, audio-e2e,
  WASAPI audio/SFX and batch self-test: **PASSED**.
- Native Windows GL door sequence: PASSED.

Linux executable: **6,224,360 bytes**, SHA-256
`d07c8f95b455b2e0f73b76b234503a15854539f6d5a98975be076ac554dd645f`.

## 6. Visual evidence

- `evidence/g6f2/before-cpu/{room,corridor}-contact.png`: before correction,
  the full yellow door appears while the central gap is still physically open.
- `evidence/g6f2/after-cpu/{room,corridor}-contact.png`: corridor remains
  visible through the shrinking aperture.
- `evidence/g6f2/gl-after/{room,corridor}-contact.png`: native Windows GL
  sequence. GL was already aperture-based.
- `evidence/g6f2/door-trace.log`: per-frame open/blocked/aperture/centre/event.
- `evidence/g6f2/door-timeline.txt`: first movement, event, impact and closed
  frame on a common timeline.
- `evidence/g6f2/audio-waveform.txt`: measured asset onset/impact and offset.

Visual inspection confirms the CPU before/after transition and the GL frames.
The remaining physical limitation is that the CPU door is a per-column
silhouette, not a full 3D leaf; this matches existing CPU renderer capability.

## 7. Loader and architecture

No capacity or memory-map change. The unused, runtime-unreferenced `doom/lines`
legacy table was removed from active Glon loader data to restore headroom for
the aperture code. Actual level geometry is hard-coded in the host and was not
changed. Loader after host blocks: `used=14498 free=302`, globals 186/256.

Glon remains authoritative for `door-open`, `target`, `blocked`, collision and
LOS. The host only renders the aperture and schedules existing audio.

## 8. Renderer/audio limitations

- CPU door rendering is column-based and aperture-based; it approximates the
  moving leaves rather than drawing full 3D leaf edges.
- The closing sample is started 410 ms in. The first audible content is still
  movement, and the impact aligns with closure, but the very beginning of the
  source clip is skipped. An edited asset would be cleaner.
- Opening audio starts at its beginning; its late impact naturally occurs after
  the door is open, but the opening movement audio is synchronised from frame 0.
- Physical speaker latency and Windows perceptual timing remain unverified.

## 9. Native Windows

- Windows executable: **6,592,000 bytes**, SHA-256
  `2b305734c2d7ae58f469dd7e2b802e492aab64bbd27a1014a91ffc5e1114a279`
- Distribution: **22,123,020 bytes**
- Launcher: `dist/windows/run-g6f2.bat`

Captured Windows door sequence:
`evidence/g6f2-win/door/room-contact.png` and `corridor-contact.png`.

## 10. Playtest command

```bat
D:
cd D:\repos\glon-worktrees\glon-avp-g6f2\dist\windows
run-g6f2.bat
```

Watch the door from both sides: the corridor should remain visible through the
aperture until the leaves meet, and the latch/impact should occur as the door
closes.

## 11. Evidence and commits

Implementation: `9635473`.

```
evidence/g6f2/
  before-cpu/ after-cpu/ gl-after/
  door-trace.log / door-timeline.txt / audio-waveform.txt
  linux-full-test.log / loader.txt / windows-build.log / metadata.txt
evidence/g6f2-win/
  self-test.log / combat-test.log / door-sync-test.log
  corpse-stability-test.log / audio-e2e.log
  audio-device-test.log / sfx-device-test.log
  batch-self-test.log / gl-door-sequence.log
  door/room-*.png / corridor-*.png / contact sheets
```

No merge, push, tag or deploy. Previous G6d/G6e/G6f/G6f.1 worktrees remain
unchanged.
