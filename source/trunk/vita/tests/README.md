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


## Planque de Strelok — 0.0.26
- Sauvegardes courantes : v8, 23232 octets, quatre cartes ; lecture des anciens formats conservée.
- Test privé avec données originales locales : compiler strelok.cpp avec vita_cform_ground.cpp et vita_trader_model.cpp, inclure model_stubs et xrGame. Arguments : dossier des ressources extraites, dossier contenant une vraie sauvegarde v7 de la Décharge (story3), dossier temporaire de sortie.
- Vérifie appuis de transition/échelle, visibilité de la clé, modèle d'origine, message FR/EN, migration v7, découverte/récupération, unicité/non-vente, poids100g et persistance sur aller-retour. N'exporter aucune ressource originale ni sauvegarde privée.
- Les tests sans assets gameplay/inventory_ui/dialogue_ui/menus/campaign_interaction sont adaptés à24 objets et au panneau d'informations sans portrait.


## Réduction des pauses — 0.0.27
- prepare_campaign_cache_fixture.py extrait les vraies méthodes de cache de pages et les blocs de préparation/activation du menu. Compiler campaign_ui_cache.cpp et pause_cache.cpp avec le dossier généré et xrGame ; ASan/UBSan recommandés.
- Le test des pages couvre préchauffage, changement de page sans font/upload, éviction LRU bornée, échec allocation et libération unique. Le test pause vérifie une préparation puis huit activations/fermetures sans relecture et avec musique coupée hors menu.
- collision_storage.cpp : comparer les3000résultats au témoin .26/.25 (première ligne mémoire différente). La limite de32MiB reste vérifiée sur le CFORM Cordon original.
- Escaliers, campagne et strelok.cpp doivent rester passants. Bounds3D conservatrices par groupes contigus, aucun changement des seuils de marche ni du format sauvegarde8.
- Nouveau journal : debug_pause_menu prepared/opened, debug_strelok ui_prepared_pages, debug_save_browser prepare_us/modal_us, debug_save_io capture_us/io_us. Le temps passé à choisir un emplacement n'est pas la durée d'une écriture.
- Le script vita_sce_gap.ld maintient un espace hors du segment exécutable pour les métadonnées SCE générées par vita-elf-create. Il doit accompagner CMake lors de l'export des sources.


## Confirmation de zone — ajout à 0.0.27
- prepare_zone_confirmation_fixture.py extrait les branches réelles d'entrée/transition et le dessin du pop-up. Compiler zone_confirmation.cpp et zone_confirmation_ui.cpp avec le dossier généré et xrGame.
- Vérifie qu'ouvrir/annuler ne lance pas de changement ni de checkpoint, qu'une confirmation appelle le chargement après validation, les boutons maintenus, priorité de l'annulation, verrous de mission, passage invalidé, six sorties et les limites/UV de la fenêtre et de son repli sans cadre.
- Au point de passage : Carré ouvre, Croix confirme, Rond annule. Le monde attend la réponse ; les délais de combat sont décalés comme pour le menu pause.


## Navigateur de sauvegardes — 0.0.28
- prepare_save_browser_fixture.py extrait les vraies méthodes VitaSaveUI et le helper de préparation. Compiler save_browser.cpp avec xrGame et le dossier généré ; ASan/UBSan recommandés.
- Vérifie préparation/réouvertures sans recharger les images ni créer de font/texture, bascule Sauvegarder/Charger, liste revérifiée à chaque ouverture, reconstruction de l'emplacement modifié seulement, suppression/corruption/autosauvegarde, échec d'allocation atomique, PGF absent/échec puis récupération, aperçus,144dessins et nettoyage des alias sans double libération.
- Le test gameplay existant exerce les lectures/écritures et migrations du backend, inchangé. Les métadonnées d'affichage sont comparées champ par champ ; le contenu d'une sauvegarde n'est jamais chargé depuis le cache UI.
