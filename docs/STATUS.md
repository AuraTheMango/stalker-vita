# Status — 0.0.28

Present: initial Sidorovich/Wolf/Allegro/USB route; Cordon–Garbage–Agroprom–Agroprom Underground transitions with confirmation and original loading screens; limited Seriy/Mole encounters; four actors on the Garbage entrance road; Strelok stash discovery, its original USB item and message, and the next objective to find Ghost. Inventory/trading, weapons, audio hooks, HUD/PDA, options and saves remain available.

Incomplete: the Ghost mission itself and the rest of the campaign; most map population, underground mutants/controller and secondary exits; Bes/checkpoints and escort to Bes; looting for the new road encounter; further animation/UI fidelity and consistent performance. The road survivor uses the existing PM world model rather than the exact PB. The stash ladder is a contextual traversal, without a physical climbing animation.

Save format 8 (23232 bytes) supports four maps and reads formats 1–7. Per-map memories retain their compact v5 representation; the new quest item follows global story progress. Settings remain format 3 (60 bytes).

The 0.0.25 stair fix has been confirmed in Vita testing. Version 0.0.26 added the stash route; hardware testing confirmed the ladder in both directions, USB/message, saving and return to Agroprom. Transition cancellation with Circle was also confirmed.

Version 0.0.27 added conservative 3D collision bounds without changing triangle order or stair thresholds. Host comparison on original underground geometry produced identical results for 4000 scenarios with a 4.73x aggregate speedup; the existing 3000-query Cordon comparison remained identical. These are host results, not a Vita FPS measurement. Pause resources and Strelok message pages are prepared during loading.

A short 0.0.27 Vita route measured a cached pause-menu activation of 40 microseconds and an actual save write of about 96 milliseconds. Save-slot browser preparation still took about 6.5 seconds. An earlier 17-second outer frame included time spent choosing a slot and was not the write duration.

Version 0.0.28 shares and prewarms the save/load browser, both sets of labels and four level previews. Each opening still checks the current files; only changed rows/details are regenerated. Host tests pass, including failures and cleanup; hardware timing of this change is pending. Initial loading performs the preparation work.

No original fixtures, captures, dumps, private logs, user saves or private development VPKs are published.
