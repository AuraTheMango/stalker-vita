# Portable development handoff

Snapshot 0.0.28, Vita APP_VER 01.34, title ID STLK00001.

Read README.md, docs/BUILD.md, docs/STATUS.md and docs/CHANGELOG.md first. Build source/trunk/vita/xrGame with VitaSDK. Keep original assets and generated files outside version control.

Source layout: upstream core in source/trunk/xrCore, Vita code in source/trunk/vita/{xrCore,xrRender,xrGame}, tests in source/trunk/vita/tests.

Latest work: reuse/prewarm one VitaSaveUI for startup, pause and death/load menus. Both modes and level previews are prepared once. Files are still checked on each opening; only changed metadata rows/details are rebuilt. Failed allocation preserves the old pair of textures and metadata. PGF initialization is independent of the main menu. Check first/repeated openings, save overwrite, deletion and loading on Vita; hardware timing for 0.0.28 is pending.

Recent work: original Agroprom Underground and Strelok stash USB/message; contextual stash ladder; conservative grouped collision bounds; bounded campaign UI caches; prewarmed pause menu; area confirmation with X/Circle. The stash route, surface return and area cancellation have been exercised on Vita. Ghost mission, mutants and later progression are incomplete.

Persistence: four zones, save v8 (23232 bytes), settings v3 (60 bytes), legacy readers retained. Zone transitions confirm, checkpoint and relaunch to avoid two maps resident. Existing map memories stay v5. The Strelok quest item is derived from global story progress. UI caches contain display metadata, not save contents.

Performance: keep browser preparation, interactive modal time and actual I/O timings separate. The earlier 17-second frame included user time in the slot browser. Short-route Vita observations in docs/STATUS.md do not validate every map or a stable global framerate. Do not describe host speedups as Vita FPS gains.

Run tools/test_host.sh before committing. It uses generated stubs and requires no retail assets. Collision/model/map tests additionally need private fixtures. Keep hardware confirmations separate from host results. The linker fragment vita_sce_gap.ld is required for SCE metadata space. Update this handoff with changes, tests, limitations and next Vita checks. Never add personal paths, secrets, original assets or raw dumps.
