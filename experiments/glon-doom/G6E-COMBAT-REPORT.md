# G6e — First Player-versus-Alien Combat Loop

Status: implemented and tested on Linux and native Windows; physical Windows
playtest remains for the user.

- Branch/worktree: `glon-avp-g6e` at
  `/mnt/d/repos/glon-worktrees/glon-avp-g6e`
- G6d baseline: `ab9544e` (unchanged)
- Implementation commit: `246e69b`
- Frozen S1: unchanged at
  `f90496c26dc45c7a387d8fc8639cd2781507d3d2`

## 1. Minimal design

Combat rules live in `doom.glon`. The host only:

- maps the existing F/left-mouse input to a fire tick;
- presents the existing Alien sprite in hit/dead states;
- plays the existing rifle SFX when Glon ammunition decreases.

No combat decision, damage, hit test or death rule exists in C. No new weapon,
AI, animation, texture or level geometry was added.

`doom/tick` gained a seventh `fire` argument. A fire tick is a normal fixed tick
with no movement input and `fire=1`; the host builds it once as
`[ doom/tick 0 0 0 0 0 0 1 ]`. Firing is therefore deterministic and uses the
same Glon tick path as movement.

## 2. Glon combat rules

The rifle is semi-automatic for this smallest loop: each F keydown or
left-mouse-down attempts one shot. Holding the key does not auto-repeat.

A shot is accepted only when `doom/ammo > 0`:

- decrement ammunition;
- compute target distance and forward depth;
- require the target in front;
- require distance² < `640²` (rifle range 640);
- require the target centre to lie within the alien radius of the aim ray
  (`cross² < alien-radius² * 64²`);
- require `doom/los` to report no wall or closed-door obstruction.

An accepted shot always spends ammunition and emits the rifle shot event, even
if it misses.

On a hit:

- damage is 25;
- health clamps to zero;
- living hit sets state `3` and a two-tick hit-reaction counter;
- zero health sets state `4` (dead), clears detection and terminal counters.

`doom/reset` restores health 100, ammo 24, living state 0, detection and the
hit-reaction counter.

## 3. Deterministic state transitions

- Hit: state `3` for two fixed updates, then state `1` if detected (otherwise
  state `0`). The transition is tested as `3 -> 3 -> 1`.
- Dead: state `4`; `doom/tick` skips the living Alien update, so it cannot
  move or attack. It remains state `4` through further ticks until `doom/reset`.
- Reset: health 100, state 0, ammo 24.

## 4. Rifle SFX follows accepted shots

The previous host `sfx_rifle_event()` 110 ms cadence was removed. This was an
independent host firing clock and could play a shot Glon had rejected.

The existing host/Glon status mechanism now carries the authoritative result:
`Status.ammo`. `audio_sfx_update()` remembers the previous ammo value and starts
the rifle sound only when ammo decreases. Since only `doom/tick` decrements
ammo, SFX exactly follows accepted shots:

- accepted shot (hit or miss): ammo decreases, rifle SFX plays;
- empty magazine: ammo does not change, no rifle SFX;
- reset: ammo increases, no rifle SFX.

This uses the existing status capture; frozen S1 and VM semantics are untouched.

## 5. Hit/death presentation

No art was added.

- CPU renderer: state `3` draws the existing Alien sprite with a red hit flash;
  state `4` draws the same sprite dimmed to 30% brightness.
- OpenGL renderer: state `3` uses a red tint; state `4` uses a 35% grey tint.
- Both use the existing idle Alien sprite for these states. The alien still
  stops movement/attack because that is the Glon state rule.

## 6. Loader-capacity accommodation

G6d was already close to the loader limit. The combat source fits without
raising capacity, but two host-side block-construction changes were required:

1. **Normalized movement masks.** Blocks are built only for canonical movement
   combinations. Pressing opposite keys (W+S, A+D, left+right) already cancels
   movement mathematically; normalizing those pairs before block lookup changes
   no movement result and reduces tick blocks from 64 to 27.
2. **Minimal test builds.** `--combat-test`, `--audio-e2e` and
   `--objects-test` do not build unused render-column blocks. This is test
   harness memory management only.

No memory-map or capacity limit changed. Normal loader usage after host blocks:
`used=14386 free=414` with 187/256 global bindings. The unused legacy level
arrays remain present.

## 7. Tests

`--combat-test` is part of `make test` and covers:

- accepted shot: ammo 23, health 75, state 3;
- hit reaction holds one update, then expires alive at state 1;
- aim miss (80 units side offset): no damage, ammo spent;
- range miss (800 units): no damage, ammo spent;
- closed-door obstruction: no damage, ammo spent;
- four hits: ammo 20, health 0, state 4;
- death terminal through 120 ticks and near the player: no attack/reset;
- empty magazine: no ammo change, no damage;
- reset: health 100, state 0, ammo 24;
- deterministic replay of health/state/ammo;
- visible CPU hit and death presentation hashes differ from living sprite.

`--audio-e2e` additionally verifies:

- one accepted Glon shot requests the rifle SFX and has PCM;
- an empty shot does not start rifle SFX;
- door, wall and cargo SFX still pass.

Linux full suite: **PASSED** (`make test`, RC 0), including all existing
texture, audio, traversal, projection, object, raw-wall, entrance and frozen
S1 checks. Frozen S1 passes.

Linux executable: **6,207,376 bytes**, SHA-256
`739082436f890aba99aa8f5f8ea50990d008aa6c58a3869950b56e65ba909157`.

## 8. Native Windows rebuild

Built with the G6d standalone Zig/SDL2 toolchain, SDL2 2.30.9 WASAPI.

- Windows executable: **6,555,136 bytes**, SHA-256
  `5ee157b94ff5e92255c50d5b37509a14ddba94e438ee8daa9489a743d2103013`
- Distribution: **22,085,319 bytes**
- Launcher: `dist/windows/run-g6e.bat`
- Platform: PE32+ x86-64, native OpenGL/AMD and WASAPI

Native Windows results:

- `--self-test`: PASSED;
- `--combat-test`: PASSED (all hit/miss/damage/death/reset cases and visible
  hit/death hashes);
- `--audio-e2e`: PASSED;
- `--audio-device-test`: WASAPI, 44100 Hz/2ch/256 samples;
- `--sfx-device-test`: six effects through WASAPI;
- `run-g6e.bat --self-test`: PASSED.

The build script now derives the SDL2 licence path from `SDL2_MINGW`, so the
distribution can be rebuilt using the G6d `.deps` directory.

## 9. Architectural limitations

- The rifle is semi-automatic in this milestone; there is no held-fire or
  weapon cadence beyond one shot per press.
- Hit detection is 2D point/ray-circle with `doom/los` to the alien centre.
  There is no partial cover, vertical aim, head/body area or per-pixel hit.
- Hit and death visuals reuse the existing sprite with tint/dim; no animation,
  corpse art or gore was added.
- The alien remains a set of Glon globals, consistent with the existing
  conceptual tuple; no new VM primitive or entity system was introduced.
- No player health/damage was added; the task scope is killing the alien.
- SFX follows ammo decrease rather than a separate shot counter. This is
  correct while only accepted shots can decrease ammo; the tests cover both
  accepted and empty cases.
- Physical Windows input, speaker playback and subjective hit feedback are
  not agent-verifiable.

## 10. Physical Windows playtest

```bat
D:
cd D:\repos\glon-worktrees\glon-avp-g6e\dist\windows
run-g6e.bat
```

Controls: W/A/S/D move, arrows turn, E door, F or left mouse fire, M mute,
`[`/`]` volume, Esc quit.

Check specifically:

1. four rifle hits kill the alien;
2. hit flash appears on non-lethal hits;
3. dead alien remains dim/down and never attacks;
4. rifle SFX plays only for accepted shots and stops at empty ammo;
5. reset returns the alien and 24-round magazine.

## 11. Evidence

```
evidence/g6e/
  linux-full-test.log
  combat-test.log
  audio-e2e.log
  loader.txt
  windows-build.log

evidence/g6e-win/
  self-test.log
  combat-test.log
  audio-e2e.log
  audio-device-test.log
  sfx-device-test.log
  batch-self-test.log
```

Implementation commit: `246e69b`.

No merge, push, tag or deploy. G6d baseline worktree remains untouched.
