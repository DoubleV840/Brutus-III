# Déterminisme (phase 2b)

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
| Vitesse du jeu | `game_speed_get_elapsed_ticks()` relie le nombre de ticks au temps réel. Il ne donne **aucun tick** si une fenêtre autre que la ville est ouverte (conseiller, info d'un bâtiment…), pendant un glisser ou un défilement de la carte. | La simulation ne dépend que du nombre de ticks. Le rejouage avance les ticks sans horloge. **Pas encore traité** : en lockstep, un joueur qui ouvre un conseiller bloquerait l'autre. En M6, le nombre de ticks doit être décidé par le lockstep, pas par l'écran de chacun. | `game/speed.c` (inchangé) |
| Pause déclenchée par la simulation | Route de Rome coupée, ville enclavée : `game_state_pause()`. | Déterministe (les deux joueurs pausent au même tick). Le rejouage l'ignore. En réseau, elle devra devenir une pause partagée (2d). | `building/maintenance.c` (inchangé) |
| Mémoire non initialisée | Les tampons de sauvegarde « dynamiques » sont alloués par `malloc` puis remplis avec des `buffer_skip` (octets sautés non écrits). | Pendant l'écriture d'une sauvegarde ou d'une empreinte, `buffer_init()` remet les tampons neufs à zéro. Bâtiments, figures et tableaux étaient déjà remis à zéro (`memset`). | `core/buffer.c`, `game/file_io.c` |
| Ordre d'itération | Tableaux indexés et listes chaînées par type. Pas de table de hachage ni de tri sur des pointeurs dans la simulation (`qsort` seulement dans l'interface, l'éditeur et les encodages). | Rien à corriger. La file de commandes est triée sur des clés uniques (tick, joueur, numéro d'arrivée). | `multijoueur/commande.c` |
| Flottants | Peu nombreux dans la simulation : `log10` (commerce), `pow` (formules d'événements), `sqrt`, `ceil`, pourcentages en `double`. Pas de `-ffast-math`. | Ajout de `-ffp-contract=off` (pas de fusion FMA en natif). Entre deux navigateurs, le même wasm (libm incluse) donne les mêmes résultats. Entre natif et wasm, `log10` et `pow` viennent de deux libm différentes : un écart d'un ulp reste théoriquement possible (non observé). | `moteur/CMakeLists.txt` |
| Orientation de la vue | La rotation de la carte modifie la simulation (images et routage des murs, reroutage des sentinelles et des chevaux, indigènes), et la pose dépend de l'orientation. | Rotation = commande `rotation-vue`. L'orientation est un paramètre de `construire`, imposée le temps de la pose. Elle reste dans l'empreinte. | `city/view.c`, `multijoueur/commande.c` |
| Pont | La longueur du pont était calculée pendant le dessin du fantôme. | Recalculée à l'exécution de la commande. | `multijoueur/commande.c` |
| Formules et textes de scénario (bug d'Augustus) | La sauvegarde annonçait une formule et un texte de plus qu'elle n'en écrivait (l'emplacement 0, réservé, était compté). Chaque chargement ajoutait une entrée vide : partie rechargée ≠ partie continue, et la sauvegarde grossissait. | Le nombre écrit ne compte plus l'emplacement 0. Les anciennes sauvegardes se chargent comme avant (une entrée vide en trop, une seule fois). | `scenario/event/controller.c` |
| Terre indigène au chargement | Au chargement, la remise à jour des bâtiments efface une partie des marques de terre indigène, puis `map_natives_check_land(0)` les recalculait avec la colère du moment. Si la colère d'un village était passée à 100 depuis le dernier tick 6, la partie rechargée différait de la partie continue (jusqu'au tick 6 suivant). | Les marques sauvegardées sont gardées avant et remises après, au lieu du recalcul. Elles sont toujours recalculées au tick 6 de chaque jour. | `game/file.c`, `map/property.c` |
| Réglages hors sauvegarde | Options de jeu `CONFIG_GP_*`, difficulté, dieux activés, sauvegardes automatiques : propres à chaque joueur mais elles changent la simulation. | En rejouage : valeurs par défaut, difficulté normale, dieux activés, pas de sauvegarde automatique. **En réseau, il faudra les partager au début de la partie (2d/2e).** | `multijoueur/rejouage.c` |

## Empreinte

`brutus_empreinte_partie()` → `game_file_io_brutus_empreinte()` (`game/file_io.c`). L'état est sérialisé exactement comme pour une sauvegarde (`savegame_save_to_state`), sans compression ni fichier. Puis on calcule un **FNV-1a 64 bits** sur chaque morceau, dans l'ordre : taille sur 4 octets, puis contenu.

Exclus (hors simulation) :

- morceaux entiers, hors simulation : caméra (`city_view_camera`), signets (`bookmarks`), copie de secours de l'annulation (`sprite_backup_grid`), sons de la ville (`city_sounds`, jamais écrit), défilement et objet choisi de la carte de l'empire (`empire` : `scroll_x`, `scroll_y`, `selected_object`), réglage du graphique de population (`city_graph_order`) ;
- morceaux entiers, sans effet sur la suite : listes de travail des bâtiments (`building_list_small`, `building_list_large` : remplies juste avant usage et vidées au chargement, `building/list.c`), compteurs statistiques du routage (`routing_counters` : jamais relus, `map/routing.c`) ;
- bits « en construction » et « supprimé / à réparer » de la grille des propriétés (aperçus du joueur) ;
- grille des sprites, sauf sur les tuiles de pont (animations écrites par le dessin) ;
- `fumigation_frame` / `fumigation_direction` des bâtiments (écrits par le dessin).

Gardés, à surveiller en M6 (ils dépendent de l'écran de chaque joueur et feraient une **fausse désynchronisation**) :

- messages : `city_message_post` (`city/message.c`) choisit selon la fenêtre ouverte et `CONFIG_UI_MESSAGE_ALERTS` entre une fenêtre (message marqué lu) et la file ; la liste des messages permet aussi de marquer lu, supprimer, choisir le message courant (voir `docs/commandes.md`) ;
- sélections des fenêtres de fête et de cadeau dans `city_data` (la simple ouverture de la fenêtre les change) ;
- orientation de la vue (dans la simulation, donc gardée ; elle passe par `rotation-vue`).

Diagnostic : `--detail-empreinte <fichier>` (en plus de `--rejouer`) écrit l'empreinte de chaque morceau au début et à la fin (`morceau <n> champ <rang dans savegame_state> taille … empreinte`), plus leur contenu brut dans `<fichier>.debut.bin` et `<fichier>.fin.bin`. C'est ce qui a permis de trouver les deux bugs de chargement ci-dessus.

Coût : une sérialisation complète par empreinte. Pour 301 empreintes sur 150 000 ticks, le rejouage complet prend 5 à 7 s.

## Rejouage sans affichage

```
augustus --rejouer <sauvegarde> <journal|-> --empreintes <sortie> [--ticks N] [--tous-les N]
         [--sauvegarde-finale <fichier>] [--detail-empreinte <fichier>] <dossier du jeu>
augustus --creer-sauvegarde <nom de carte> <sortie.svx> <dossier du jeu>
augustus --info-carte <sauvegarde> <carte.txt> <dossier du jeu>
```

Les pilotes SDL `dummy` sont imposés par le programme lui-même dans ces modes. Les chemins relatifs sont résolus par rapport au dossier de lancement. Si SDL n'a ni vidéo ni pilote `dummy` (wasm sous Node), le moteur démarre SDL sans vidéo et utilise un rendu logiciel en mémoire. La boucle avance les ticks au plus vite, sans horloge. Avant chaque tick, elle met en file les commandes du journal datées de ce tick ou d'avant.

Sortie : `<tick> <empreinte hex>` tous les N ticks, puis une ligne `# bilan <commande> appliquees A refusees R` par type de commande et une ligne finale `# fin …` (commandes appliquées, refusées, perdues, restantes ; population, trésor, bâtiments). Deux fichiers identiques = même partie. `outils/verifier-fin-rejouage.mjs` vérifie que toutes les commandes du journal ont été appliquées, et compare un rejouage repris d'une sauvegarde à un rejouage continu (`--comparer`).

## Tests et résultats (19/11/2026, Mac du fondateur, Emscripten 7.0.0, Node 26)

```
bash outils/test-rejouage.sh          # natif : 2 lancements séparés, même journal, puis rechargement
bash outils/test-rejouage-wasm.sh     # wasm sous Node : 2 lancements + comparaison natif ↔ wasm
```

`test-rejouage.sh` échoue si une commande du journal n'a pas été appliquée (ligne `# fin`) : deux rejouages identiques mais vides ne passent plus. La sauvegarde de départ est refaite si le moteur est plus récent qu'elle. Sur Linux, le binaire `build-natif/augustus` est pris si le paquet macOS n'existe pas. Sans les fichiers du jeu (CI), le test est sauté.

**Test de rechargement** (cas de la resynchronisation) : rejoue la moitié du parcours, écrit une sauvegarde, la recharge dans un 3ᵉ lancement, continue avec le même journal, et compare chaque empreinte de la seconde moitié à celle du rejouage continu.

Carte « Lugdunum » (fichiers du jeu, non publiés), bot `outils/bot-rejouage.mjs` avec la graine 42. **150 000 ticks**, soit 20 min à la vitesse 200 % (8 ms par tick) ou 3 000 jours de jeu (environ 15,6 ans).

| Comparaison | Ticks | Empreintes (1 / 500 ticks) | Commandes appliquées | Résultat |
|---|---|---|---|---|
| natif ↔ natif | 150 000 | 301 | 892 / 892 (179 refusées) | identiques |
| rechargement au tick 75 000 ↔ continu (natif) | 75 000 + 75 000 | 151 comparées | 427 / 427 après le départ | identiques |
| wasm ↔ wasm (Node) | 150 000 | 301 | 892 / 892 | identiques |
| natif ↔ wasm | 150 000 | 301 | 892 | identiques |
| contrôle négatif : 1 commande `impots` retirée (tick 21 411) | 150 000 | 301 | 891 | différentes dès l'empreinte suivante (tick 21 500), 261 sur 301 diffèrent |

Avant les deux corrections de chargement (formules, terre indigène), le test de rechargement échouait dès la première empreinte (tick 75 000), alors que la ville finale était la même.

Répartition du journal et refus : voir `docs/commandes.md`, « Ce que le bot exerce vraiment ». Ville en fin de test : 86 habitants, trésor −2 961, 79 bâtiments. La population oscille beaucoup (392 au tick 9 500, 100 au tick 19 500, 557 au tick 69 500, 620 au tick 109 500) : le bot démolit beaucoup et ne gère pas la ville.

## Limites connues

- La variante wasm testée tourne sous Node, pas dans un navigateur. Elle est compilée depuis le même code et les mêmes options, mais ce n'est pas le fichier publié (pas de `SINGLE_FILE`, NODEFS). Un test dans deux onglets reste à faire (M6).
- Natif ↔ wasm est identique sur ce test, mais ce n'est pas garanti dans tous les cas (`log10` et `pow` de libm différentes). Ce n'est pas bloquant : les deux joueurs utilisent le wasm.
- Réglages non sauvegardés (options de jeu, difficulté, dieux) : à synchroniser au début d'une partie en réseau.
- Resynchronisation (M6) : **les deux joueurs rechargent la même sauvegarde**, l'hôte compris, et les réglages de partie sont renvoyés avec elle. Le test de rechargement montre qu'une partie rechargée continue comme la partie d'origine, mais sur un seul point (tick 75 000) et une seule carte.
- Le rythme des ticks dépend encore de l'écran (fenêtre ouverte, glisser) : voir « Vitesse du jeu » ci-dessus.
- Messages et fenêtres ouvertes par l'exécution d'une commande : voir `docs/commandes.md`, « Effets d'interface pendant l'exécution ».
- L'aperçu du glisser et le fantôme de démolition écrivent dans la carte partagée (marques et annulation provisoire). Ils sont exclus de l'empreinte, mais ils restent une source de désynchronisation en coop tant qu'ils ne sont pas isolés (M6/M7).
- L'annulation (`annuler`) repose sur la copie de secours de la dernière construction locale. Elle n'est pas sauvegardée et elle est commune à tous les joueurs : à revoir pour la coop.
- Le chemin de l'interface (clic → commande → pose) n'a pas été testé à la main dans cette mission : le test passe par le journal.
- Le bot ne voit pas la partie. Une partie de ses commandes échoue (terrain, argent), ce qui reste déterministe.
