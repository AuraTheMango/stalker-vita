# Vérifications hôtes Vita

Exécuter depuis la racine du projet avec g++ C++17 et Python 3. Les stubs audio remplacent seulement les appels système Vita ; le mixeur testé est le code réel.

- Comportement, munitions, options et sauvegardes v1/v3 : compiler `gameplay.cpp` avec `-Isource/trunk/vita/xrGame`. Passer un dossier temporaire vide au programme.
- Audio : compiler `audio.cpp` avec `-Isource/trunk/vita/tests/audio_stubs -Isource/trunk/vita/xrGame`.
- Menus : générer les fonctions de dessin depuis le code réel avec `prepare_menu_fixture.py DOSSIER_TEMP`, puis compiler `menus.cpp` avec `-IDOSSIER_TEMP` ; le canevas de contrôle enregistre les rectangles des fonctions de dessin.
- Shaders : `python3 source/trunk/vita/tests/graphics.py`. Requiert EGL Mesa et libGL avec rendu hors écran ; aucun module Python externe. Compile les shaders réels et contrôle le FXAA, le foyer du DOF et l’interpolation des PNJ par lecture des pixels.
- Modèles : préparer les fixtures avec `prepare_npc_fixtures.py DOSSIER_ASSETS DOSSIER_FIXTURES`, puis compiler `npc_models.cpp` et `vita_trader_model.cpp` avec `-Isource/trunk/vita/tests/model_stubs -Isource/trunk/vita/xrGame`. Passer les deux dossiers au programme. Les modèles OGF et `stalker_animation.omf` doivent être extraits des données originales du joueur et ne sont pas fournis. Les matrices du stub reprennent les fonctions des en-têtes mathématiques du projet.
- Activer `-fsanitize=address,undefined -g -O1` pour les vérifications mémoire des tests C++.

La validation matérielle Vita reste nécessaire pour les FPS, le compilateur de shaders embarqué, les transitions de résolution et le MSAA. Le DOF et le MSAA ne sont pas combinés. Les effets sont désactivés par défaut.


Le test `assault_advance.cpp` requiert le `level.cform` original en argument. Compiler ce test avec `vita_cform_ground.cpp` et le chemin d’inclusion xrGame. Il reproduit l’absence de tirs depuis l’ancien point final, vérifie les trois parcours sans franchissement de mur, puis simule leur progression et le combat avec au moins deux tireurs alliés. La recherche est limitée à 2 200 positions, calculées au chargement.

Pour la 0.0.17 :
- `economy.cpp` : compiler avec `-Isource/trunk/vita/xrGame` puis passer un dossier temporaire vide. Il vérifie aussi les migrations v1/v2/v3 et les échanges/butins atomiques.
- `inventory_ui.cpp` : exécuter `python3 source/trunk/vita/tests/prepare_inventory_fixture.py DOSSIER_TEMP` puis compiler avec `-Isource/trunk/vita/xrGame -IDOSSIER_TEMP`. Passer DOSSIER_TEMP au binaire : il y écrit inventory.csv/trade.csv/loot.csv issus des méthodes de production.
- Les fixtures PNJ doivent désormais inclure le clip 78, pour contrôler la dernière pose couchée de norm_death_0.
- `assault_advance.cpp` vérifie aussi que la réserve du bunker est placée sur le sol avec une ligne de vue accessible.

Pour la 0.0.18 :
- Sauvegardes : format 4 de 4224 octets. `economy.cpp` vérifie aussi une ancienne sauvegarde v3 à 12 objets, les nouvelles armes/munitions/accessoires et le cas six PM/140 cartouches (10,86 kg).
- `settings.cpp` vérifie les paramètres OPT1/OPT2, les 12 réglages, les bornes et la corruption.
- `player_weapons.cpp` + `vita_trader_model.cpp` avec model_stubs : argument = dossier meshes/weapons extrait. Vérifie BM16/AK74/RGD5/F1/PM, vrais clips et accessoires, conservation des cartouches, trajectoire/fusée/ligne de vue. Les banques d’une arme principale et d’un type de grenade sont chargées à la demande.
- `prepare_inventory_fixture.py` écrit aussi `vita_dialog_draw.inc`, branche d’écran original de `draw_panel`. Compiler `dialogue_ui.cpp` avec ce dossier en inclusion pour les bornes/UV.
- `menus.cpp` accepte le dossier de sortie et enregistre options-0..3.csv. Le même code de dessin teste les quatre onglets et les contrôles.
- `extract_original.py` lit les archives d’origine depuis le dossier fourni et garde un index JSON. Le décodeur LZH est compilé depuis LzHuf.cpp (génération hôte dans build/review-assets) ; LZO provient de liblzo2.
- `convert_original_audio.py` convertit les sons Vorbis originaux en PCM mono 24 kHz. Bibliothèques de décodage hôte seulement décompressées localement ; aucune dépendance ajoutée à la Vita.
