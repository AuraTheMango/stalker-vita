# S.T.A.L.K.E.R. Vita

Unofficial, work-in-progress PlayStation Vita port of S.T.A.L.K.E.R.: Shadow of Chernobyl.

[Documentation française](README.fr.md)

**Current source snapshot: 0.0.28. This is an incomplete prototype, not a full-game release.**
The current route covers parts of the Cordon, Garbage, Agroprom and Agroprom Underground, including Strelok's stash, with a limited set of encounters. The performance target is 30 FPS; performance and stability still depend on location and Vita hardware testing.

## Included
- Vita rendering, collision/movement, controls and audio code.
- Inventory, trading, dialogue, HUD, options, saves and initial mission progression.
- Upstream engine source subset required by this Vita build.
- Host regression tests and build instructions.

This repository contains **no retail game archives, textures, models, recordings or extracted game audio**. It is not affiliated with or endorsed by GSC Game World. You need your own PC game data to run it.

## Build and test
See [BUILD.md](docs/BUILD.md) for dependencies and build commands. The verification build uses silent audio and generated placeholder LiveArea artwork, not the original game assets.

    bash tools/test_host.sh

These checks need no retail assets. Collision/model/map tests additionally require private files from your own installation. Vita testing remains necessary for rendering, audio, memory usage and FPS.

## Documentation
- [Controls](docs/CONTROLS.md)
- [Status and limitations](docs/STATUS.md)
- [Changes through 0.0.28](docs/CHANGELOG.md)
- [Portable development handoff](HANDOFF_WSL.md)
- [Host tests](source/trunk/vita/tests/README.md)

## Credits and licensing
Based on [VideogameSources/STALKER-Shadow-of-Chernobyl](https://github.com/VideogameSources/STALKER-Shadow-of-Chernobyl), baseline commit d7b23596. Original GSC notices and redistribution conditions are preserved in [LICENSE](LICENSE) and [source/License.txt](source/License.txt). Per-file third-party notices also apply; see [NOTICE.md](NOTICE.md).

Existing licenses are retained. This project does not apply a new blanket MIT or GPL license to the engine or game assets.
