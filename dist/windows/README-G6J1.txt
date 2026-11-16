# G6j.1 — cocoon collapse correction

Branch: `glon-avp-g6j1`, based on G6j `a713fb1`  
Status: **implemented, packaged and verified; unaccepted pending physical Windows testing**  
Windows executable: `dist/windows/doom.exe`, **8,178,176 bytes**  
SHA-256: `f06f1368457c8b3404fabe0a9988fe1499ca003aa36a1b6b9ca59b280d81c024`  
Launcher: `dist/windows/run-g6j1.bat`

G6j and all earlier builds are preserved. No merge, push, tag, release or deploy.
Frozen S1 is unchanged (`f90496c26dc45c7a387d8fc8639cd2781507d3d2`).

## 1. Defect and source correction

The G6j destroyed frame used the contact sheet's tall **key-state destroyed**
image. That image still had a large upright arc and hanging strands, and the
billboard retained the intact cocoon's height. Scaling alone could not make it a
collapsed object.

The contact sheet's bottom **Destroyed Variants** row was re-inspected and three
low-profile variants were cropped and cleaned. The old tall destroyed frame is
no longer used by the renderer. The new collapse frames are 128×128 with
bottom-centre alignment, transparent background and the same crop/alpha method
as G6j. Some low-alpha floor/debris remains; it is minor and not a rectangular
panel. No unrelated replacement artwork was generated.

## 2. Collapse sequence

Glon lifecycle semantics and the shared `doom/cocoon-step` state machine are
retained. DORMANT/ALERTED/HATCHING/HATCHED/DESTROYED are unchanged.

On the second hit:

- health becomes 0;
- state becomes DESTROYED;
- the hatch timer is cancelled;
- the state timer is set to **30 fixed ticks (0.5 s)** for the collapse;
- the associated scuttler remains state 6 and can never spawn.

The state-4 branch decrements that timer to 0 and then holds. No new top-level
Glon bindings were added; the existing per-cocoon timer cell carries collapse
progress.

Rendering interpolation:

- start height 0.75, final height 0.38 (source sprite is itself low-profile);
- width grows 0.95 → 1.25;
- base stays at floor level with a small depth offset to prevent floor
  z-fighting; the heap reads as floor-contact remains rather than a floating
  object;
- two intermediate texture frames are used (collapse1 early, collapse0 for the
  settled heap), plus vertical/footprint interpolation;
- no per-frame allocation and no new texture uploads.

The first hit leaves the cocoon upright, keeps state ALERTED/HATCHING, reduces
health 2→1 and applies the existing red damage tint. A successful hatch retains
the upright opened shell and releases its scuttler; it is not reused as the
destroyed state.

## 3. Tests

`--cocoon-test` now verifies:

- first hit damages but does not destroy and stays upright;
- second hit enters DESTROYED with a 30-tick collapse;
- collapse reaches mid-height and a final low height (<0.4) with a wider
  footprint (>1.1);
- hatch timer is cancelled and no scuttler spawns;
- destroyed remains cannot resume hatching;
- hatched cocoons stay upright;
- independent cocoon states and the existing G6j lifecycle tests still pass;
- restart restores all three intact cocoons.

Native Windows captures: `intact`, `first-hit`, `collapse-start`,
`collapse-mid`, `collapsed`, and `hatched` in `evidence/g6j1/cocoon/`.

## 4. Regression and measurements

- Full Linux `make test`: **PASSED**, including frozen S1.
- Native Windows title at 640×400/800×600/960×600/1280×720, cocoon lifecycle
  and capture tests, plus the complete G6j/G6i.3 regression suite: **PASSED**
  (projection test was rerun with its test asset present).
- Loader: 25,000 reserved, **20,070 used**, 4,930 free.
- Globals: **270 / 512**; managed-heap high **34,272**.
- Live title Windows: working **98,553,856 B**, peak **99,946,496 B**, private
  **119,468,032 B**.
- Bench: Linux `sim=12.79 ms`, `frame=196.30 ms`; Windows `sim=12.35 ms`,
  `frame=190.43 ms`.

## 5. Scope / limitations

Only cocoon destruction visuals and their bounded collapse state were changed.
Scuttler navigation, player movement, camera/aiming, adult, weapon damage rules,
proximity monitor, level geometry, cocoon positions, activation/countdown, VM
layout and frozen S1 are untouched.

G6j.1 remains **unaccepted** pending physical Windows testing. The extracted
collapse frames retain minor floor/debris artifacts; cocoons remain non-solid
player-collision volumes as in G6j. Scuttler damage attacks and distinctive
sounds remain outstanding.
