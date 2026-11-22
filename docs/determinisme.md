# Déterminisme (phases 2b et M6a)

## Audit

Séparation retenue : **simulation** = ce qui est appelé depuis `game_tick_run()` et les commandes ; **interface / son / visuel** = le reste (dessin, fenêtres, météo affichée, nuages, musique).

| Point | Constat | Correction | Fichier |
|---|---|---|---|
| `random_from_stdlib()` / `random_between_from_stdlib()` / `random_fractional_from_stdlib()` | `srand(time())` + `rand()` : dépend de l'heure et de la libc. Utilisé par la simulation (industrie, grèves, criminels, artistes, invasions, demandes de César, événements de scénario, formules, paris de course, icônes d'empire). | Générateur déterministe : `splitmix64((iv1:iv2) + compteur)`. `iv1/iv2` sont sauvegardés ; le compteur est remis à zéro à chaque tick, avant chaque commande et au chargement. Même code sur toutes les plateformes, `RAND_MAX` fixé à 2^31-1. | `core/random.c` |
| Aléatoire visuel | Nuages, météo (particules, choix de la pluie), fichier test du gestionnaire de fichiers. | Passés sur `random_ui_*` (ancien comportement), qui ne touche pas la simulation. La météo n'influence pas la simulation. | `graphics/clouds.c`, `graphics/weather.c`, `platform/file_manager.c` |
| `rand()` direct | Apparition des plébéiens et des chiens. | `random_from_stdlib()`. | `building/figure.c` |
| `rand()` restant | Musique (`sound/music.c`), une fenêtre (`window/building/utility.c`) | Laissés : son / interface. | — |
| Réserve aléatoire (`pool`) et valeurs dérivées | Non sauvegardées : un programme qui charge une sauvegarde n'avait pas le même état que celui qui l'a écrite (âges des immigrants). | Recalculées depuis `iv1/iv2` au chargement, sans faire avancer le générateur. | `core/random.c` (`random_load_state`) |
| Horloge réelle dans la simulation | Caches valables « pendant la milliseconde courante » : évolution des maisons (`last_update`), maison libre la plus proche pour les migrants, état des combats pour le routage. En jeu accéléré, plusieurs ticks tombent dans la même milliseconde : le résultat dépendait de la vitesse de la machine. | Remplacé par `brutus_repere_simulation()`, qui change au début de chaque tick et avant chaque commande. | `building/house_evolution.c`, `figuretype/migrant.c`, `map/routing.c` |
| Horloge réelle restante | `figuretype/native.c` (délai d'un son), animations, info-bulles, défilement, fenêtres. | Laissée : son / interface. | — |
| Vitesse du jeu | `game_speed_get_elapsed_ticks()` relie le nombre de ticks au temps réel. Il ne donne **aucun tick** si une fenêtre autre que la ville est ouverte (conseiller, info d'un bâtiment…), pendant un glisser ou un défilement de la carte. | **M6a** : en solo, inchangé. En multijoueur, `game_run()` → `brutus_simulation_image()` ne l'appelle plus : les ticks viennent de l'extérieur (`brutus_ticks_externes_ajouter`, le futur lockstep) et sont tous joués (`brutus_avancer_ticks(n)`, 50 au plus par image). Le rejouage passe aussi par `brutus_avancer_ticks`. Test « interface ouverte » (voir plus bas). | `multijoueur/simulation.c`, `game/game.c` |
| Pause déclenchée par la simulation | Route de Rome coupée, ville enclavée : `game_state_pause()`. | Déterministe (les deux joueurs pausent au même tick). Le rejouage l'ignore. En réseau, elle devra devenir une pause partagée (2d). | `building/maintenance.c` (inchangé) |
| Mémoire non initialisée | Les tampons de sauvegarde « dynamiques » sont alloués par `malloc` puis remplis avec des `buffer_skip` (octets sautés non écrits). | Pendant l'écriture d'une sauvegarde ou d'une empreinte, `buffer_init()` remet les tampons neufs à zéro. Bâtiments, figures et tableaux étaient déjà remis à zéro (`memset`). | `core/buffer.c`, `game/file_io.c` |
| Ordre d'itération | Tableaux indexés et listes chaînées par type. Pas de table de hachage ni de tri sur des pointeurs dans la simulation (`qsort` seulement dans l'interface, l'éditeur et les encodages). | Rien à corriger. La file de commandes est triée sur des clés uniques (tick, joueur, numéro d'arrivée). | `multijoueur/commande.c` |
| Flottants | Peu nombreux dans la simulation : `log10` (commerce), `pow` (formules d'événements), `sqrt`, `ceil`, pourcentages en `double`. Pas de `-ffast-math`. | Ajout de `-ffp-contract=off` (pas de fusion FMA en natif). Entre deux navigateurs, le même wasm (libm incluse) donne les mêmes résultats. Entre natif et wasm, `log10` et `pow` viennent de deux libm différentes : un écart d'un ulp reste théoriquement possible (non observé). | `moteur/CMakeLists.txt` |
| Orientation de la vue | La rotation de la carte modifie la simulation (images et routage des murs, reroutage des sentinelles et des chevaux, indigènes), et la pose dépend de l'orientation. | Rotation = commande `rotation-vue`. L'orientation est un paramètre de `construire`, imposée le temps de la pose. Elle reste dans l'empreinte. | `city/view.c`, `multijoueur/commande.c` |
| Pont | La longueur du pont était calculée pendant le dessin du fantôme. | Recalculée à l'exécution de la commande. | `multijoueur/commande.c` |
| Formules et textes de scénario (bug d'Augustus) | La sauvegarde annonçait une formule et un texte de plus qu'elle n'en écrivait (l'emplacement 0, réservé, était compté). Chaque chargement ajoutait une entrée vide : partie rechargée ≠ partie continue, et la sauvegarde grossissait. | Le nombre écrit ne compte plus l'emplacement 0. Les anciennes sauvegardes se chargent comme avant (une entrée vide en trop, une seule fois). | `scenario/event/controller.c` |
| Terre indigène au chargement | Au chargement, la remise à jour des bâtiments efface une partie des marques de terre indigène, puis `map_natives_check_land(0)` les recalculait avec la colère du moment. Si la colère d'un village était passée à 100 depuis le dernier tick 6, la partie rechargée différait de la partie continue (jusqu'au tick 6 suivant). | Les marques sauvegardées sont gardées avant et remises après, au lieu du recalcul. Elles sont toujours recalculées au tick 6 de chaque jour. | `game/file.c`, `map/property.c` |
| Réglages hors sauvegarde | Options de jeu `CONFIG_GP_*`, difficulté, dieux activés, sauvegardes automatiques, suppression des vieux messages : propres à chaque joueur mais elles changent la simulation. | **M6a** : en-tête de partie (`multijoueur/entete.c`) : bloc texte `nom=valeur` écrit après les morceaux de la sauvegarde (marque `BRUTUS3-ENTETE1` ; format et version d'Augustus inchangés, Augustus lit toujours ces sauvegardes). Au chargement, il remplace les réglages locaux **avant** la lecture de l'état ; une option absente prend sa valeur par défaut. Les réglages locaux sont gardés à part, remis pour une partie sans en-tête, et ce sont eux qui vont dans `augustus.ini` / `c3.inf`. En multijoueur, ces réglages sont verrouillés (`config_set`, `setting_*` refusent). L'en-tête compte dans l'empreinte. Le rejouage impose toujours des valeurs par défaut pour une sauvegarde sans en-tête. | `multijoueur/entete.c`, `core/config.c`, `game/settings.c`, `game/file_io.c` |
| Messages (M6a) | `city_message_post` choisissait fenêtre / file / alerte selon l'écran et `CONFIG_UI_MESSAGE_ALERTS`, et marquait lu à l'ouverture ; la liste des messages triait et supprimait directement. | La simulation n'écrit que le message (non lu), en tête d'un tableau toujours trié. Décision d'affichage chez chacun après le tick ou la commande, et au début de chaque image pour les messages publiés ailleurs (`city_message_brutus_afficher`). Lu / supprimé par commandes ; la liste des messages se refait avant chaque dessin. `city_message_set_current` (interface) ne change plus la partie. | `city/message.c`, `window/message_list.c` |
| Aperçus de construction (M6a) | Le glisser écrit dans la carte (terrain, images, routage, marques) et dans le contexte d'annulation ; le fantôme de démolition pose des marques « supprimé ». | Avant toute simulation (tick, commande, empreinte, sauvegarde : `brutus_simulation_debut`), l'aperçu est retiré (terrain, toutes les images, grilles de routage, marques), puis remis après (`brutus_simulation_fin`) ; un seul passage par image, aucun s'il n'y a ni commande ni tick. Annuler un glisser (clic droit) ou le finir remet aussi les images et le routage. L'annulation a deux contextes : celui de l'aperçu (local) et celui de la simulation (copies de secours des grilles et habitants à rendre gardés à part, propriétaire = joueur qui a construit). | `building/construction.c`, `game/undo.c`, `multijoueur/simulation.c` |
| Fenêtres qui recalculent la partie (M6a) | Ouvrir les conseillers (main-d'œuvre, finances, couverture, notes…), dessiner certains conseillers (dieux, culture, cadeaux, logement, religion), la fiche d'un entrepôt (recomptage), les demandes de César (« marchandises prêtes ») et l'aperçu des parcours (figure d'essai dans la grille des figures) modifiaient l'état. | Solo : inchangé. Multijoueur : sautés (valeurs de la simulation affichées). Champs d'affichage écrits par les fiches et jamais lus par la simulation, mis hors empreinte : phrases des figures, texte « pourquoi la maison n'évolue pas ». Fenêtres de fête, cadeau, jeux, don : choix gardé dans la fenêtre. | `window/advisors.c`, `window/advisor/*.c`, `window/building/distribution.c`, `city/request.c`, `figure/roamer_preview.c`, `figure/figure.c`, `building/state.c` |
| État hors sauvegarde qui survit au chargement (M6a) | Un programme qui recharge (l'hôte) gardait des valeurs qu'un programme neuf (l'invité) n'a pas : compteur des mendiants, direction du feu, message d'obstruction, point de départ de la répartition de l'eau (variable statique d'une fonction), compteur du tremblement de terre personnalisé (idem), effets durables des triches. | Remis à zéro dans `brutus_partie_chargee()` (chargement et nouvelle partie) ; les effets des triches sont écrits dans l'en-tête de partie (`brutus_triche_*`). Trouvés avec le test hôte / invité et l'option `--prechargement`. | `building/figure.c`, `building/maintenance.c`, `city/labor.c`, `scenario/earthquake.c`, `game/cheats.c`, `multijoueur/entete.c` |
| Chemins des figures (M6a) | La table des chemins garde parfois des emplacements libres en fin de tableau (chemin non trouvé) que le chargement retire : mêmes chemins, octets sauvegardés différents. | La sauvegarde n'écrit que jusqu'au dernier chemin utilisé (le tableau n'est pas modifié). | `figure/route.c` |

## Empreinte

`brutus_empreinte_partie()` → `game_file_io_brutus_empreinte()` (`game/file_io.c`). L'état est sérialisé exactement comme pour une sauvegarde (`savegame_save_to_state`), sans compression ni fichier. Puis on calcule un **FNV-1a 64 bits** sur chaque morceau, dans l'ordre : taille sur 4 octets, puis contenu.

Exclus (hors simulation) :

- morceaux entiers, hors simulation : caméra (`city_view_camera`), signets (`bookmarks`), copie de secours de l'annulation (`sprite_backup_grid`), sons de la ville (`city_sounds`, jamais écrit), défilement et objet choisi de la carte de l'empire (`empire` : `scroll_x`, `scroll_y`, `selected_object`), réglage du graphique de population (`city_graph_order`) ;
- morceaux entiers, sans effet sur la suite : listes de travail des bâtiments (`building_list_small`, `building_list_large` : remplies juste avant usage et vidées au chargement, `building/list.c`), compteurs statistiques du routage (`routing_counters` : jamais relus, `map/routing.c`) ;
- bits « en construction » et « supprimé / à réparer » de la grille des propriétés (aperçus du joueur) ;
- grille des sprites, sauf sur les tuiles de pont (animations écrites par le dessin) ;
- `fumigation_frame` / `fumigation_direction` des bâtiments (écrits par le dessin).

Exclus aussi depuis M6a : phrases des figures (`phrase_id`, `phrase_sequence_*`) et texte d'évolution des maisons (`evolve_text_id`) : écrits par les fiches ouvertes, jamais lus par la simulation. L'**en-tête de partie** est ajouté au calcul (après les morceaux). L'empreinte, comme la sauvegarde, est calculée sans l'aperçu du joueur local.

Toujours gardés : l'orientation de la vue (dans la simulation ; elle passe par `rotation-vue`) et le choix courant des fêtes / cadeaux / jeux dans `city_data` (seule une commande les écrit désormais).

Diagnostic : `--detail-empreinte <fichier>` (en plus de `--rejouer`) écrit l'empreinte de chaque morceau au début et à la fin (`morceau <n> champ <rang dans savegame_state> taille … empreinte`), plus leur contenu brut dans `<fichier>.debut.bin` et `<fichier>.fin.bin`. C'est ce qui a permis de trouver les deux bugs de chargement ci-dessus.

Coût : une sérialisation complète par empreinte. Pour 301 empreintes sur 150 000 ticks, le rejouage complet prend 5 à 7 s.

## Rejouage sans affichage

```
augustus --rejouer <sauvegarde> <journal|-> --empreintes <sortie> [--ticks N] [--tous-les N]
         [--sauvegarde-finale <fichier>] [--detail-empreinte <fichier>]
         [--multijoueur] [--interface-test] [--ticks-par-image N]
         [--resync-au-tick T1,T2,...] [--reglage nom=valeur]... [--prechargement <sauvegarde> <ticks>]
         <dossier du jeu>
augustus --creer-sauvegarde <nom de carte> <sortie.svx> <dossier du jeu>
augustus --info-carte <sauvegarde> <carte.txt> <dossier du jeu>
```

Les pilotes SDL `dummy` sont imposés par le programme lui-même dans ces modes. Les chemins relatifs sont résolus par rapport au dossier de lancement. Si SDL n'a ni vidéo ni pilote `dummy` (wasm sous Node), le moteur démarre SDL sans vidéo et utilise un rendu logiciel en mémoire. La boucle avance les ticks au plus vite, sans horloge. Avant chaque tick, elle met en file les commandes du journal datées de ce tick ou d'avant.

Options M6a :

- `--multijoueur` : ticks imposés de l'extérieur, image par image (`game_run`), comme le fera le lockstep (2 ticks par image par défaut, `--ticks-par-image`).
- `--interface-test` (implique `--multijoueur`) : interface scriptée (`multijoueur/rejouage_interface.c`) : chaque conseiller, fiches de bâtiments, liste des messages, fenêtres de fête / cadeau / jeux / don, glissers (routes, démolition, aqueducs, murs, maisons, jardins, places, réparation), fantôme de démolition, conseiller ouvert sur un glisser ; quelques dessins complets. Les commandes que l'interface enverrait sont ignorées et comptées (ligne `# interface`). Diagnostic : `BRUTUS_PHASES=13` limite le script à certaines phases.
- `--resync-au-tick` (ticks absolus) : resynchronisation dans le même programme (cas de l'hôte) : sauvegarde `<empreintes>.resync-<tick>.svx` puis `brutus_resynchroniser()`.
- `--reglage nom=valeur` : réglage local (`gameplay_*`, `brutus_difficulte`, `brutus_dieux`, `brutus_sauvegarde_mensuelle`), pour vérifier que l'en-tête d'une sauvegarde l'emporte.
- `--prechargement <sauvegarde> <ticks>` : diagnostic : charge et joue une autre partie avant, pour trouver un état qui survit au chargement.
- Le rejouage ignore toujours les commandes de l'interface (il ne rejoue que le journal).

Sortie : `<tick> <empreinte hex>` tous les N ticks, puis une ligne `# bilan <commande> appliquees A refusees R` par type de commande et une ligne finale `# fin …` (commandes appliquées, refusées, perdues, restantes ; population, trésor, bâtiments). Deux fichiers identiques = même partie. `outils/verifier-fin-rejouage.mjs` vérifie que toutes les commandes du journal ont été appliquées, et compare un rejouage repris d'une sauvegarde à un rejouage continu (`--comparer`).

## Tests et résultats (19/11/2026, Mac du fondateur, Emscripten 7.0.0, Node 26)

```
bash outils/test-rejouage.sh          # natif, 3 cartes (≈ 2 min par carte)
bash outils/test-rejouage-wasm.sh     # wasm sous Node, mêmes cartes, après le test natif
```

Pour chaque carte de `BRUTUS_CARTES` (défaut : Lugdunum, Tarraco, Carthago ; fichiers du jeu, non publiés), bot `outils/bot-rejouage.mjs`, graine 42, **150 000 ticks** (20 min à 200 %, environ 15,6 ans de jeu), une empreinte tous les 500 ticks (301). Tout est dans `build-rejouage/<carte>/` (ignoré par git). Sans les fichiers du jeu (CI), les tests sont sautés.

| Vérification | Lugdunum | Tarraco | Carthago |
|---|---|---|---|
| natif ↔ natif (2 lancements) | identiques | identiques | identiques |
| interface ouverte, multijoueur (≈ 1 500 fenêtres, ≈ 1 150 glissers) ↔ rejouage simple | identiques | identiques | identiques |
| annulation : joueur 0 sur la construction du joueur 1 refusée et sans effet ; joueur 1 accepté | OK | OK | OK |
| invité chargé au tick 50 000 ↔ hôte qui recharge à 50 000 et 100 000 (201 empreintes) | identiques | identiques | identiques |
| invité chargé au tick 100 000 ↔ hôte (101 empreintes) | identiques | identiques | identiques |
| wasm ↔ wasm, interface wasm ↔ wasm, natif ↔ wasm | identiques | identiques | identiques |
| invité wasm chargé sur la sauvegarde de l'hôte natif (tick 50 000) | identiques | identiques | identiques |
| pour information : partie rechargée ↔ partie continue | diffère dès le tick 50 000 | idem | idem |

Durées (natif) : 6 à 8 s par rejouage, 50 à 60 s pour l'interface ouverte (le rendu logiciel des dessins coûte cher). Wasm sous Node : 5 à 8 s, 8 à 16 s avec l'interface.

**Rechargement.** Le test M4 comparait la partie rechargée à la partie continue sur un seul point (tick 75 000). Avec plus de points, ce n'est pas toujours vrai : le chargement d'Augustus refait des calculs que la partie continue ne fait pas au même moment (accès routier des entrepôts, distances…). C'est pour cela qu'à la resynchronisation **tous les joueurs, hôte compris, rechargent** la sauvegarde (`brutus_resynchroniser`). Le test exigé est donc : un hôte qui recharge dans son programme et un invité qui charge la même sauvegarde dans un programme neuf ont la même partie. Ce test a trouvé six états hors sauvegarde (voir l'audit) ; `--prechargement` permet d'en chercher d'autres.

**En-tête de partie.** Une sauvegarde créée avec `--reglage brutus_difficulte=4 --reglage brutus_dieux=0`, rejouée avec des réglages locaux différents, donne les mêmes empreintes qu'avec les réglages par défaut, et d'autres empreintes qu'une sauvegarde sans ces réglages (vérifié à la main, Lugdunum, 3 000 ticks).

Contrôle négatif fait en M4 (non refait ici) : une commande `impots` retirée du journal → empreintes différentes dès l'empreinte suivante.

## Test réseau (M6b, 22/11/2026)

`bash outils/test-reseau-memoire.sh [ticks]` : deux instances wasm du moteur (`--reseau`, variante Node en `MODULARIZE`, option CMake `BRUTUS_RESEAU_NODE`) dans un même processus Node, chacune pilotée par le vrai client `web/lockstep.js`, reliées par un relais en mémoire qui reprend les règles de `serveur-multi/src/index.js` (en-tête de 8 octets, destinataires obligatoires, jetons, remplacement de connexion, `erreur`, ping/pong, latence de 0 à 2 tours de boucle). Chaque client injecte son journal du bot (graines 42 et 43). Rapport : `build-reseau/rapport-memoire.txt`.

Scénario déclenché par la progression : coupure propre de l'invité, coupure silencieuse (le relais jette tout sans fermer), resynchronisation forcée (invité puis hôte), pause partagée, vitesses simultanées, coupure de l'hôte, **panne injectée** (impôts +3 sur l'invité seul : l'empreinte doit diverger, être détectée une fois par l'hôte et réparée par la resynchronisation), rechargement de la page de l'hôte (nouvelle instance du moteur, partie récupérée auprès de l'invité).

Résultat, 225 000 ticks (30 minutes de jeu à 200 %), carte Lugdunum : 4 495 empreintes comparables (une tous les 50 ticks) **identiques**, zéro divergence réelle, comparaison finale directe identique, panne détectée une fois, scénario complet. **Aucune variable statique oubliée ni source de non-déterminisme nouvelle n'est apparue** : aucune modification du moteur n'a été nécessaire pour la synchronisation. Les chargements du test sont ceux de `brutus_resynchroniser` (tout le monde recharge, hôte compris).

Limites : le test n'exerce que les commandes du bot (joueur 0 de chaque client) sur Lugdunum ; une panne volontaire ne prouve pas l'absence d'état caché, seulement que la détection fonctionne. Avec `BRUTUS_RELAIS`, le même scénario passe par un vrai relais (`outils/test-reseau-30min.sh`) : voir `docs/protocole.md` pour son état.

## Divergences trouvées à la relecture QA (M6b) : causes corrigées

Deux familles de divergence que la resynchronisation aurait cachées (le client recharge l'état de l'hôte à la première empreinte différente, et le symptôme disparaît). Elles sont corrigées **à la cause**, dans le moteur :

- **Menus « Charger », « Recommencer », « Quitter la mission »** : ils changent la ville d'un seul joueur sans commande (`game_file_load_saved_game`, `game_file_start_scenario_by_name`). En partie à deux ils sont refusés (`brutus_reseau_menu_refuse()` : `window/file_dialog.c`, `window/city.c`, `window/pause_menu.c`, `widget/top_menu.c`, commentés « Brutus III (M6b) »). Les deux seuls chemins restants (menu principal, choix de mission) appellent `brutus_reseau_ville_quittee()` : le client passe en phase `hors-ville` et n'avance plus (voir `docs/protocole.md`). Victoire et fin de mission : M11. On n'utilise pas `jeuEnCours` pour cela : il ne couvre que `WINDOW_CITY` à `WINDOW_TRADE_LEDGER` et figerait les deux joueurs dès l'ouverture de la fenêtre d'options.
- **Vitesse changée en local** : retour à la vitesse par défaut après la première invasion (`city/message.c`) et fenêtre d'options (`window/config.c`). Ignorés en réseau ; de plus le client remet la vitesse locale à la vitesse commune avant chaque groupe (`verifierVitesse`, compteur `vitessesCorrigees`).

**Règle** : en réseau, tout appel direct à `setting_set_game_speed`, `game_file_load_saved_game` ou `game_file_start_scenario_by_name` contourne le client et fait diverger. Toute nouvelle entrée de menu qui en appelle un doit être gardée.

**Détection** : le moteur écrit le détail de la dernière empreinte par morceau dans `/tmp/brutus-empreinte-detail.txt` (`empreinte.c`, lu par `moteur.detailEmpreinte()`). À la première divergence le client le journalise ; à 3 divergences en 10 minutes il affiche une alerte (`etat().alerte`). Le test exige zéro divergence avant la panne injectée, une détection, et une réparation dans une nouvelle époque.

**Cas complémentaires du test** (un critère chacun dans le rapport) : menus refusés, vitesse locale corrigée, invité hors ville, hôte hors ville (récupération de sa partie), chargement refusé chez l'invité (redemande une fois), envoi refusé par le relais (nouvel essai sans recharger seul).

**Non testé** : latence de plusieurs centaines de ms ; route de Rome coupée (pause déclenchée par la simulation) ; deux onglets avec le même jeton ; arrivée d'un joueur pendant une resynchronisation ou au milieu des morceaux de sauvegarde ; onglet masqué dans un vrai navigateur.

**Constantes** : TOUR par groupe de 5 ticks (5 divise 50, un jour de jeu), délai d'entrée D = 2 groupes, empreinte tous les 50 groupes (250 ticks, hôte seul), ping 20 s, mort 45 s, attente 10 s, réessai de sauvegarde 5 s, récupération hôte 60 s. Le test rapide les raccourcit ; `--production` les garde (passe d'au moins 20 minutes réelles).

## Limites connues

- Navigateur réel : les variantes wasm testées tournent sous Node (même code, mêmes options, mais pas le fichier publié `SINGLE_FILE`). Le test réseau de M6b (deux moteurs wasm, client `web/lockstep.js`, relais en mémoire puis vrai relais en local) n'est pas un test à deux onglets dans un navigateur : voir « Test réseau (M6b) » plus bas.
- Natif ↔ wasm est identique sur les trois cartes, mais pas garanti dans tous les cas (`log10` et `pow` de libm différentes). Non bloquant : les deux joueurs utilisent le wasm.
- **État hors sauvegarde** : six cas trouvés et remis à zéro au chargement. D'autres variables statiques de la simulation peuvent exister ; le test hôte / invité ne les voit que si elles changent la partie sur ces trois cartes. À chaque nouvelle fonction du moteur, vérifier qu'un état qui change la partie est soit sauvegardé, soit remis dans `brutus_partie_chargee()`.
- Partie rechargée ≠ partie continue (voir plus haut) : sans conséquence si tout le monde recharge. Une sauvegarde automatique ne doit donc pas servir de point de resynchronisation sans être rechargée par tous.
- Vitesse et pause sont partagées par le lockstep depuis M6b (messages VITESSE, voir `docs/protocole.md`). En multijoueur, `game_run()` ignore la vitesse et la pause locales. Une pause déclenchée par la simulation elle-même (route de Rome coupée) n'a pas été essayée dans le test réseau.
- Multijoueur : quelques valeurs des conseillers (explications des notes, cause de l'absence d'immigration, demandes culturelles, estimation des impôts, stocks des entrepôts) ne sont plus recalculées à l'ouverture : elles ne bougent que quand la simulation les recalcule (certaines jamais). L'annonce « marchandises prêtes » de César n'arrive qu'au contrôle mensuel. L'aperçu des parcours est coupé. À reprendre en M7 (Coop) si gênant.
- Les aperçus sont retirés et remis à chaque passage dans la simulation : en multijoueur, pendant un glisser, l'aperçu est recalculé à chaque image où des ticks passent.
- `brutus_resynchroniser` abandonne le glisser en cours du joueur local.
- Le bot n'exerce que la validation de `depot-ordre`, `monument-*`, `monnaie-conversion`, `production-reserve`, `dock-route`, `barrage-permission`, `caserne-*`, `jeux`, `legions-bataille`, `victoire-continuer` (voir `docs/commandes.md`). Les chemins de l'interface vers ces commandes n'ont pas été testés à la main.
- Solo : le chemin de l'interface (clic → commande → pose, sauvegarde et rechargement) a été testé à la main par le fondateur dans le navigateur et fonctionne. Les tests automatiques, eux, passent par le journal et l'interface scriptée. Les chemins de l'interface propres au multijoueur (page « Jouer à deux ») n'ont pas été essayés dans un navigateur.
- Effets durables des triches : dans l'en-tête de partie (pas dans le format d'Augustus) : une sauvegarde Brutus III les garde, une sauvegarde d'Augustus les remet à zéro.
- Ouvrir la fenêtre d'un dépôt ne remet plus son ordre en état (ressource par défaut, source ou destination invalides) : c'est fait par la commande `depot-ordre` suivante. La simulation ignore déjà un stockage inactif.
- Messages : le numéro de séquence est sauvegardé sur 16 bits ; après 32 767 messages, l'ordre du tableau pourrait différer entre une partie continue et une partie rechargée (sans effet si tout le monde recharge).
