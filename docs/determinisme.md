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
| Vitesse du jeu | `game_speed_get_elapsed_ticks()` relie le nombre de ticks au temps réel. | La simulation ne dépend que du nombre de ticks. Le rejouage avance les ticks sans horloge. En réseau, c'est le lockstep qui décidera du nombre de ticks (2d). | `game/speed.c` (inchangé) |
| Pause déclenchée par la simulation | Route de Rome coupée, ville enclavée : `game_state_pause()`. | Déterministe (les deux joueurs pausent au même tick). Le rejouage l'ignore. En réseau, elle devra devenir une pause partagée (2d). | `building/maintenance.c` (inchangé) |
| Mémoire non initialisée | Les tampons de sauvegarde « dynamiques » sont alloués par `malloc` puis remplis avec des `buffer_skip` (octets sautés non écrits). | Pendant l'écriture d'une sauvegarde ou d'une empreinte, `buffer_init()` remet les tampons neufs à zéro. Bâtiments, figures et tableaux étaient déjà remis à zéro (`memset`). | `core/buffer.c`, `game/file_io.c` |
| Ordre d'itération | Tableaux indexés et listes chaînées par type. Pas de table de hachage ni de tri sur des pointeurs dans la simulation (`qsort` seulement dans l'interface, l'éditeur et les encodages). | Rien à corriger. La file de commandes est triée sur des clés uniques (tick, joueur, numéro d'arrivée). | `multijoueur/commande.c` |
| Flottants | Peu nombreux dans la simulation : `log10` (commerce), `pow` (formules d'événements), `sqrt`, `ceil`, pourcentages en `double`. Pas de `-ffast-math`. | Ajout de `-ffp-contract=off` (pas de fusion FMA en natif). Entre deux navigateurs, le même wasm (libm incluse) donne les mêmes résultats. Entre natif et wasm, `log10` et `pow` viennent de deux libm différentes : un écart d'un ulp reste théoriquement possible (non observé). | `moteur/CMakeLists.txt` |
| Orientation de la vue | La rotation de la carte modifie la simulation (images et routage des murs, reroutage des sentinelles et des chevaux, indigènes), et la pose dépend de l'orientation. | Rotation = commande `rotation-vue`. L'orientation est un paramètre de `construire`, imposée le temps de la pose. Elle reste dans l'empreinte. | `city/view.c`, `multijoueur/commande.c` |
| Pont | La longueur du pont était calculée pendant le dessin du fantôme. | Recalculée à l'exécution de la commande. | `multijoueur/commande.c` |
| Réglages hors sauvegarde | Options de jeu `CONFIG_GP_*`, difficulté, dieux activés, sauvegardes automatiques : propres à chaque joueur mais elles changent la simulation. | En rejouage : valeurs par défaut, difficulté normale, dieux activés, pas de sauvegarde automatique. **En réseau, il faudra les partager au début de la partie (2d/2e).** | `multijoueur/rejouage.c` |

## Empreinte

`brutus_empreinte_partie()` → `game_file_io_brutus_empreinte()` (`game/file_io.c`). L'état est sérialisé exactement comme pour une sauvegarde (`savegame_save_to_state`), sans compression ni fichier. Puis on calcule un **FNV-1a 64 bits** sur chaque morceau, dans l'ordre : taille sur 4 octets, puis contenu.

Exclus (hors simulation) :

- morceaux entiers : caméra (`city_view_camera`), signets (`bookmarks`), copie de secours de l'annulation (`sprite_backup_grid`), sons de la ville (`city_sounds`, jamais écrit) ;
- bits « en construction » et « supprimé / à réparer » de la grille des propriétés (aperçus du joueur) ;
- grille des sprites, sauf sur les tuiles de pont (animations écrites par le dessin) ;
- `fumigation_frame` / `fumigation_direction` des bâtiments (écrits par le dessin).

Gardés, à surveiller en 2d : messages (lu / courant), sélections des fenêtres de fête et de cadeau dans `city_data`, orientation de la vue.

Coût : une sérialisation complète par empreinte. Pour 301 empreintes sur 150 000 ticks, le rejouage complet prend 5 à 7 s.

## Rejouage sans affichage

```
augustus --rejouer <sauvegarde> <journal|-> --empreintes <sortie> [--ticks N] [--tous-les N]
         [--sauvegarde-finale <fichier>] <dossier du jeu>
augustus --creer-sauvegarde <nom de carte> <sortie.svx> <dossier du jeu>
augustus --info-carte <sauvegarde> <carte.txt> <dossier du jeu>
```

Les pilotes SDL `dummy` sont imposés par le programme lui-même dans ces modes. Les chemins relatifs sont résolus par rapport au dossier de lancement. Si SDL n'a ni vidéo ni pilote `dummy` (wasm sous Node), le moteur démarre SDL sans vidéo et utilise un rendu logiciel en mémoire. La boucle avance les ticks au plus vite, sans horloge. Avant chaque tick, elle met en file les commandes du journal datées de ce tick ou d'avant.

Sortie : `<tick> <empreinte hex>` toutes les N lignes, plus une ligne finale `# fin …` (population, trésor, bâtiments). Deux fichiers identiques = même partie.

## Tests et résultats (19/11/2026, Mac du fondateur, Emscripten 7.0.0, Node 26)

```
bash outils/test-rejouage.sh          # natif : 2 lancements séparés, même journal
bash outils/test-rejouage-wasm.sh     # wasm sous Node : 2 lancements + comparaison natif ↔ wasm
```

Carte « Lugdunum » (fichiers du jeu, non publiés), bot `outils/bot-rejouage.mjs` avec la graine 42. **150 000 ticks**, soit 20 min à la vitesse 200 % (8 ms par tick) ou 3 000 jours de jeu (environ 15,6 ans).

| Comparaison | Ticks | Empreintes (1 / 500 ticks) | Commandes appliquées | Résultat |
|---|---|---|---|---|
| natif ↔ natif | 150 000 | 301 | 870 | identiques |
| wasm ↔ wasm (Node) | 150 000 | 301 | 870 | identiques |
| natif ↔ wasm | 150 000 | 301 | 870 | identiques |
| contrôle négatif : 1 commande `impots` retirée (tick 16 999) | 150 000 | 301 | 869 | différentes dès le tick 17 000 (268 empreintes sur 301 diffèrent) |

Répartition du journal : 503 `construire` (routes, maisons, services, fermes, entrepôts, greniers, aqueducs, réservoirs, murs, démolitions), 79 `impots`, 35 `salaires`, 60 `priorite-travail`, 71 `stockage-etat`, 17 `stockage-tout`, 17 `stockage-permission`, 10 `stockage-vider`, 24 `distribution-bien`, 25 `mise-en-sommeil`, 5 `fete`, 10 `annuler`, 12 `legion-rentrer`, 2 `rotation-vue`. Ville en fin de test : 61 habitants, trésor −1 501, 102 bâtiments. La population a oscillé (432 au tick 9 500, 497 au tick 29 500) : le bot démolit beaucoup.

## Limites connues

- La variante wasm testée tourne sous Node, pas dans un navigateur. Elle est compilée depuis le même code et les mêmes options, mais ce n'est pas le fichier publié (pas de `SINGLE_FILE`, NODEFS). Un test dans deux onglets reste à faire (M6).
- Natif ↔ wasm est identique sur ce test, mais ce n'est pas garanti dans tous les cas (`log10` et `pow` de libm différentes). Ce n'est pas bloquant : les deux joueurs utilisent le wasm.
- Réglages non sauvegardés (options de jeu, difficulté, dieux) : à synchroniser au début d'une partie en réseau.
- L'aperçu du glisser et le fantôme de démolition écrivent dans la carte partagée (marques et annulation provisoire). Ils sont exclus de l'empreinte, mais ils restent une source de désynchronisation en coop tant qu'ils ne sont pas isolés (M6/M7).
- L'annulation (`annuler`) repose sur la copie de secours de la dernière construction locale. Elle n'est pas sauvegardée et elle est commune à tous les joueurs : à revoir pour la coop.
- Le chemin de l'interface (clic → commande → pose) n'a pas été testé à la main dans cette mission : le test passe par le journal.
- Le bot ne voit pas la partie. Une partie de ses commandes échoue (terrain, argent), ce qui reste déterministe.
