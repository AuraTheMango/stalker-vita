# Portable development handoff

Snapshot 0.0.25, Vita APP_VER 01.31, title ID STLK00001.

Read README.md, docs/BUILD.md and docs/STATUS.md first. Build source/trunk/vita/xrGame with VitaSDK. Keep original assets and generated files outside version control.

Source layout: upstream core in source/trunk/xrCore, Vita code in source/trunk/vita/{xrCore,xrRender,xrGame}, tests in source/trunk/vita/tests.

Latest work: VitaCformGround::walk_move improves stair climbing with disk support, bounded movement and headroom checks. Save application accepts support near a stair edge. Hardware confirmation of 0.0.25 is pending; check ascent/descent, saving on steps and physics time in boot.log. Earlier testing reached Agroprom; texture cache evictions/refusals need separate follow-up.

Persistence: three zones, save v7 (18600 bytes), settings v3 (60 bytes), legacy readers retained. Zone transitions checkpoint and relaunch to avoid two maps resident. Garbage road actor health/positions persist across saves and transitions.

Run tools/test_host.sh before committing. Retail map/model tests need private fixtures. Keep hardware confirmations separate from host results. Update this handoff with changes, tests, limitations and next Vita checks. Never add personal paths, secrets, original assets or raw dumps.
