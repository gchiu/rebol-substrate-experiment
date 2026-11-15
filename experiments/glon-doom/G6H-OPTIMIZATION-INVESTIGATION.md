# G6h — deferred loader optimization investigation

This is a measurement/investigation note, not an applied optimization. The
playable build was deliberately completed first.

## Largest contributors

- **The fixed evaluator code is not the G6h growth.** `code=12,454` cells before
  and after G6h. Frozen S1/evaluator semantics and emitted runtime code did not
  grow. The growth is in the **loader heap** used for parsed game data, closures,
  globals and host blocks.
- **Game/entity data is the dominant increase.** Loader use after the game grew
  from 13,438 to 16,651 cells: **+3,213 cells** for +6,657 source bytes in
  `doom.glon`. This is the scuttler constants, 15 state globals, shared
  getter/setter closures, shared AI/update, target consideration, damage helper
  and status values. There is no single function of comparable size; the cost
  is many small closures/globals plus parser metadata.
- **Host prebuilt blocks are the next contributor.** Loader use after host
  blocks grew from 14,738 to 17,986 cells, so host/test blocks added **+1,335
  cells** total (G6g had 852, G6h 1,335; **+483**). These include the normal
  collision/LOS/tick/shot blocks and test-isolation scenes.
- **Sprite header is binary/rodata, not Glon loader data.** The 49 64x32 RGBA
  frames occupy 401,408 bytes of texture data and dominate the executable-size
  increase; they do not consume Glon loader cells.
- **Globals grew by 50 bindings** (201/256 to 251/512). The 512-binding global
  context and gated hash-stride change were required for the new entity plus
  existing objects mode.

## Opportunities

1. Replace the ten scalar getter/setter closures with a packed entity record or
   a data-driven table. This is the clearest loader/global reduction, but needs
   mutation support that does not compromise the frozen semantics.
2. Merge the adult and scuttler damage/consideration paths into one generic
   damage helper, removing duplicated arithmetic/metadata.
3. Gate test-only scene/isolation blocks out of the normal build or parse them
   lazily. Host blocks are ~1.3k cells; the lazy tick experiment previously
   recovered ~448 cells.
4. Pack scuttler fields (position/health/state/timer) into fewer integers or a
   managed block; keep the bounded three-entity indexing explicit.
5. Remove/avoid the 15-value status expansion in release mode, or emit it only
   for tests, as host status is a test/diagnostic contract.

## Can the enlarged arena be reduced?

Yes, but not back to 14,800. Current measured use after host blocks is **17,986
cells** of the authorized 25,000. Removing lazy/test blocks and consolidating
entity logic could plausibly bring the requirement toward **~20,000 cells** with
modest spare capacity. That is a separate approved change; the playable G6h build
keeps 25,000 cells and 7,014 free cells. Reverting to 14,800 would require a
substantial redesign, not a small optimization.

## Performance note

Tick-only `--bench 60` on the software test path: G6g 2.74 ms/tick, G6h
8.43 ms/tick; full frame 178.27 vs 212.42 ms. The new entity AI dominates the
simulation component. No optimization or performance claim is made; this is
deferred.
