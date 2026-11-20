# Status — 0.0.25

Present: initial Sidorovich/Wolf/Allegro/USB route; Cordon–Garbage–Agroprom transitions and loading screens; limited Seriy/Mole encounters; four actors on the Garbage entrance road; inventory/trade, weapons, audio hooks, HUD/PDA, menus/options and saves.

Incomplete: full campaign, underground/later maps, most map population, Bes/checkpoints, escort to Bes, looting for the new road encounter, further animation/UI fidelity and consistent performance. The road survivor uses the existing PM world model rather than the exact PB.

Save format 7 (18600 bytes) reads formats 1–6. Settings format 3 (60 bytes) reads earlier versions.

The 0.0.25 stair fix uses continuous footprint support, bounded substeps and headroom checks, with a cheap flat-ground path. Host tests passed for synthetic full/open stairs at several angles/speeds, original Agroprom and Cordon bunker stairs, wall/door/headroom blocking and save support. Existing height/slide/ray collision results remained identical for 3000 comparison queries.

Hardware confirmation of this stair fix is pending. Host timing is not an FPS measurement. Earlier hardware testing reached Agroprom with map/detail layers available; texture cache pressure remains a separate follow-up.

No original fixtures, captures, dumps, private logs or user saves are published.
