# Changes

## 0.0.28 — save/load browser reuse
- Share the browser between the startup menu, pause menu and loading after death.
- Prepare both modes, static labels, artwork and level previews during loading.
- Recheck files at each opening and rebuild only changed display rows.
- Preserve previous metadata/textures if an allocation fails; retain the existing save-file reader and writer.
- Add production-method host checks for repeated openings, updated/deleted/corrupt slots, allocation and font-module failures, previews and cleanup.
- Save format 8 remains compatible. Vita timing is pending.

## 0.0.27 — pauses and area confirmation
- Add a destination dialog before area travel: Square opens, X confirms, Circle cancels.
- Freeze movement/combat during the choice and require button release before confirmation.
- Add conservative bounds over contiguous collision groups; retain exact triangle order and stair behavior.
- Prewarm/reuse the pause menu and Strelok message pages with bounded caches.
- Separate browser interaction time from actual save I/O measurements.
- Reserve linker space for generated SCE metadata with vita_sce_gap.ld.

## 0.0.26 — Strelok's stash
- Add original Agroprom Underground, its factory entrance/return loading transition and six bandit placements.
- Track stash discovery and collection of the original gunslinger USB item/message.
- Add contextual ladder access and the next Find Ghost objective; the Ghost mission and underground mutants remain incomplete.
- Extend saves to v8/four maps while preserving legacy readers.
