# G6i — boot screen, credits and memory initialization

Worktree: `/mnt/d/repos/glon-worktrees/glon-avp-g6i`  
Branch: `glon-avp-g6i`  
Base: G6h.2 `4b942b3`  
Implementation: `af0eea2dbafa535ceed72cc3bfcd62aa38af1fbd`

Status: **IMPLEMENTED AND AUTOMATED-VERIFIED; PHYSICAL WINDOWS ACCEPTANCE
PENDING.** G6i is unaccepted and has no tag. G6h.2 remains physically
unaccepted.

## 1. Startup sequence

Normal Windows launch now enters a TITLE/BOOT state before gameplay:

1. SDL/OpenGL/audio and the Glon runtime are initialized.
2. The game is loaded and host blocks are parsed so loader/global values are
   real; no tick occurs.
3. Title, memory, graphics/audio status and credits are drawn.
4. Enter resets gameplay, applies the existing spawn orientation and starts the
   fixed-tick loop.
5. Escape quits cleanly; enemy AI cannot advance on the title because no
   `tick`/`tick_jump` call is made.

Automated smoke modes (`--smoke-frames`, portal smoke) intentionally bypass the
title so existing headless graphics checks keep their fixed frame semantics.

## 2. Memory display

The title renders values collected from the running build, not constants:

| Item | Displayed/measured |
|---|---:|
| VM address space (capacity) | 65,536 cells |
| Loader reserved | 25,000 cells |
| Loader occupied (current launch) | 17,830 cells |
| Loader available | 7,170 cells |
| Global bindings | 274 of 512 |
| Managed heap high-water | 34,240 cells (labelled OBSERVED) |
| Graphics | OpenGL ready, current resolution |
| Audio | READY / DISABLED / UNAVAILABLE from actual initialization |

The 65,536 figure is labelled as VM address-space/capacity, not as physically
allocated RAM. Loader reserved/occupied/free are separate. No memory
architecture was changed for presentation.

## 3. Credits

Editable data lives in `credits.h` as a simple string array. Current page:

- Concept and direction: Graham Chiu
- Execution language: Glon
- AI-assisted development: DeepSeek / OpenCode
- Additional engineering: ChatGPT
- Graphics: project-supplied art; Python/Pillow conversion tools
- Audio: Freesound contributors Tommaso Motteran, kentspublicdomain,
  Mastersoundboy2005, Heretic4522, F.M.Audio and Rickplayer; full licences and
  modifications remain in the existing `SOURCE.txt` files.
- Runtime: SDL2 / OpenGL.

No third-party authorship, endorsement or licence claim is invented.

## 4. Presentation

- Dark terminal background, green/amber 5x7 pixel type, restrained scanlines.
- Large title, boot/BIOS line, memory block, credits and start/quit prompt.
- Uses the existing bitmap font and no new external art.
- No boot sound was added: there was no existing suitable boot cue, so none was
  invented. Audio readiness is displayed.
- No unskippable delay; Enter responds as soon as the title is initialized.
- Captured at 960x600 and 1280x720.

## 5. Verification

`--title-test` verifies with actual GL framebuffer captures:

- initial state is TITLE; title image is produced before gameplay;
- every memory value equals the live `M[GC_LOADER_HP]`, global usage,
  `R0_s1_heap_high()` etc.;
- credits data is present and rendered;
- three title renders do not change scuttler positions/AI state;
- Enter transitions to PLAYING; Escape transitions to QUIT;
- restart/reset still works;
- four title/start/reset cycles leave loader high-water unchanged (no loader
  leak);
- title and post-Enter gameplay screenshots saved.

Actual Windows title-test passed at 960x600 and 1280x720; the post-Enter
gameplay regression (`--gl-g6h2-test`) also passed. Linux full `make test`
passed all prior regressions, title test, tracker, world/depth and frozen S1.
Native Windows self, multi, tracker, combat, door, corpse, muzzle, audio, jump,
alien collision and collision-probe passed. Packaged `run-g6i` title and self
tests passed.

Screenshots: `evidence/g6i/live-960/01-title.png`,
`02-gameplay.png`, `evidence/g6i/live-1280/01-title.png`, plus
`evidence/g6i/gameplay-check/`.

## 6. Measurements

- Loader reserved 25,000; occupied 17,830; free 7,170.
- Global bindings 274/512.
- Managed heap high 34,240.
- VM capacity 65,536.
- Tick-only `--bench 60`: **10.77 ms/tick**; full frame 185.52 ms.
- Windows exe: **7,012,864 bytes**, SHA-256
  `f44db46773e42ca4dc277bcefbf3bbb1cb15088c46aeb3b3447a7eba4af97eb7`.
- Windows distribution: **22,556,127 bytes**.
- Launcher: `dist/windows/run-g6i.bat`.

## 7. Gameplay preservation and outstanding issues

G6h.2 scuttler AI, combat, movement, world rendering, collider, aim tolerance
and four-contact HUD are unchanged. The following physical-playtest issues are
recorded for a later milestone and were **not silently fixed**:

- starting-room scuttler approaches fast enough to move beside/under the player;
- the player cannot look down to see very close low enemies;
- scuttlers have no distinctive movement/proximity sound;
- scuttler attack is not yet damaging.

## 8. Preserved builds

G6h (`e6f68555...`), G6h.1 (`8b150d82...`) and G6h.2 (`6b5e7823...`) are
preserved in their worktrees. G6i is a separate branch/distribution. No merge,
push, tag, release or deployment.
