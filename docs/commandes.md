# Couche de commandes (phases 2a et M6a)

Code : `moteur/src/multijoueur/commande.c` / `commande.h`.

## Principe

Toute action du joueur qui modifie la partie devient une commande `{type, joueur, tick, paramètres}` :

1. l'interface appelle `brutus_commande_envoyer()` (ou `brutus_commande_1()`…) au lieu de modifier l'état ;
2. la commande est datée du **tick absolu** suivant : `jours écoulés × 50 + tick du jour` (valeurs sauvegardées) ;
3. elle est appliquée **avant** ce tick, dans l'ordre `(tick, joueur, ordre d'arrivée)` (l'hôte, joueur 0, d'abord).

Points d'application :

- `game_tick_run()` (`game/tick.c`) : avant chaque tick de simulation ;
- `brutus_simulation_image()` (`multijoueur/simulation.c`, appelé par `game_run()`) : en **solo**, à chaque image, avant les ticks. Une commande est donc appliquée dès l'image suivante, même en pause. Comme aucun tick ne passe entre-temps, c'est équivalent à « avant le tick T ». En **multijoueur** (`brutus_multijoueur_definir(1)`), seul le premier point sert : les commandes ne sont appliquées qu'au début des ticks autorisés par le lockstep (`brutus_ticks_externes_ajouter()`).

### Joueurs

- `brutus_joueur_local_definir()` / `brutus_joueur_local()` : numéro du joueur de cette machine (0 = hôte, par défaut). Il est mis dans chaque commande envoyée par l'interface.
- `brutus_commande_joueur_en_cours()` : joueur de la commande en cours d'exécution, -1 hors commande (ticks, chargement).
- `brutus_interface_pour_moi()` : vrai hors commande, ou si la commande vient du joueur local. Les confirmations (fort, pont, monument, réparation), les avertissements (`city_warning_show*`) et les messages publiés pendant une commande ne s'affichent que chez celui qui l'a envoyée (voir « Effets d'interface pendant l'exécution »).
- Annulation : `annuler` n'annule que la dernière construction du joueur qui l'envoie (`game_undo_brutus_appartient`). Une construction d'un autre joueur remplace la sienne. Sinon la commande est refusée.
- **Conflits (M7, Coop)** : deux ordres incompatibles dans le même tour (même groupe de 5 ticks) sont appliqués dans l'ordre ci-dessus, l'hôte d'abord, chez tous. Si l'ordre du joueur local n'a eu aucun effet (refusé par la validation, ou construction qui n'a rien coûté donc rien posé, la réparation exceptée) alors qu'un autre joueur venait d'agir avec effet dans le même lot, le joueur local voit l'avertissement « L'autre joueur a agi au même moment : ton ordre n'a pas abouti. » (`brutus_coop_signaler_conflit`, `multijoueur/coop.c`), et la page le redit dans le panneau. Seul l'écran change : la partie est la même chez tous. Testé par `outils/test-reseau-memoire.sh` (deux puits sur la même case).

Avant **chaque** commande, le compteur de l'aléatoire de simulation et le repère des caches sont remis à zéro (voir `docs/determinisme.md`). Le résultat ne dépend donc pas du regroupement des commandes (image par image en solo, tick par tick en rejouage).

Les commandes qui visent un bâtiment le désignent par **une tuile** `x y` du bâtiment (coordonnées de carte). L'exécution retrouve le bâtiment principal (`map_building_at` puis `building_main`).

### Validation à l'exécution

Une commande arrive d'un pair : elle est revérifiée avant d'agir. Une commande invalide ne fait rien ; elle est comptée comme **refusée** (`brutus_commandes_total_refusees()`, lignes `# bilan` du rejouage). Ce qui est vérifié :

| Commande | Contrôles |
|---|---|
| `construire` | type dans la plage, et autorisé : `scenario_allowed_building` (le défrichage, la réparation et les maisons sont toujours permis, l'arc de triomphe seulement s'il en reste un à poser, comme `building/menu.c`) ; coordonnées dans la grille ; orientation 0, 2, 4 ou 6. Ensuite, le moteur refuse lui-même ce qui ne se pose pas (terrain, argent). |
| `demolir-confirme`, `reparer-confirme` | zone dans la grille ; `quoi` entre 1 et 3 |
| commandes `stockage-*` | tuile d'un entrepôt ou d'un grenier (`stockage-permission` : tout bâtiment à stockage) ; ressource valide ; permission dans la plage |
| `distribution-bien`, `distribution-tout` | bâtiment distributeur (`building_type_is_distributor`) ou à inventaire (`building_has_supplier_inventory`), comme l'interface |
| `don-cesar` | taille entre `GIFT_MODEST` et `GIFT_LAVISH` (le tableau `gifts[]` de `city/emperor.c` n'est pas borné) et épargne suffisante, pari en cours déduit |
| `demande-cesar` | id dans la plage, demande visible, état « en cours » ou « en retard ». Une demande déjà envoyée est refusée : deux joueurs qui envoient la même demande ne retirent pas les marchandises deux fois. |
| `legion-formation` | légion existante, hors bataille lointaine, disposition entre `FORMATION_COLUMN` et `FORMATION_MOP_UP` |
| `pari-course` | pas de pari en cours, cheval de 1 à 4, montant positif et au plus égal à l'épargne |
| `rotation-vue`, `commerce-statut` | valeur parmi celles de l'interface (0/1/2 ; import ou export) |
| `annuler` | dernière construction annulable faite par le même joueur |
| `message-lu`, `message-supprimer` | message trouvé par son numéro de séquence (les numéros d'emplacement changent quand un message arrive) |
| `depot-ordre` | tuile d'un dépôt ; ressource stockable ; source / destination = 0 ou stockage en service, différent du dépôt, qui accepte la ressource de l'ordre ; condition 0 à 3 ; seuil 0 à 40. Toute commande met le blé si aucune ressource n'est choisie (ce que faisait l'ouverture de la fenêtre), puis retire une source ou une destination qui n'accepte plus la ressource |
| `monument-arret`, `monument-module` | monument ; module 1 ou 2 (choix de la fenêtre) accepté par le moteur (`building_monument_add_module` : monument fini, pas déjà de module) ; le coût (1 000) n'est prélevé que si le module est posé, même en dette (comme l'interface d'origine) |
| `monnaie-conversion` | hôtel des monnaies ; ressource deniers ou or |
| `production-reserve`, `dock-route`, `barrage-permission`, `caserne-*` | bâtiment du bon type (producteur primaire, dock, barrage, caserne) ; route 0 à 30 et permission -2 à 15 (le moteur fait `1 << n` sur un `int` et un `unsigned short`) ; priorité 0 ou 1 |
| `commerce-politique` | terre ou mer ; politique 1 à 3 (coût 500 prélevé à chaque fois, comme l'interface) |
| `jeux` | id 1 à `MAX_GAMES` (vérifié avant la recherche dans `ALL_GAMES`) ; mêmes conditions que la fenêtre (lieu, épargne, marchandises, accès à l'eau), et pas de jeux déjà prévus ou en cours (deux joueurs ne paient pas deux fois) |
| `don-ville` | montant positif, borné par l'épargne (pari en cours déduit) |
| `legions-bataille` | une bataille lointaine est demandée (`city_request_has_troop_request`) |
| `victoire-continuer` | mission gagnée ; 1 à 10 mois |
| `triche` | numéro de la console (hors triches d'affichage) ou 100 argent, 101 victoire, 102 invasion |
| autres | bornes, bâtiment ou légion existants, argent (`fete`), victoire (`salaire-gouverneur`) |

Pas vérifié : les restrictions propres aux tutoriels et les ressources nécessaires d'un bâtiment (`can_get_required_resource` du menu). Le moteur bloque déjà la plupart de ces cas à la pose.

Une file pleine (4 096 commandes en attente) perd la commande : elle est comptée (`brutus_commandes_total_perdues()`) et signalée dans le journal du moteur. Le rejouage applique les commandes prêtes avant d'en ajouter, il n'en perd donc pas.

## Format du journal

Texte, une commande par ligne, `#` en début de ligne = commentaire :

```
# brutus-journal 1
<tick> <joueur> <commande> <paramètre1> <paramètre2> ...
5 0 construire 5 16 20 14 20 0 0 1 0
16999 0 impots 1
```

- `tick` : tick absolu (non décroissant d'une ligne à la suivante) ;
- `joueur` : 0 = hôte ;
- les paramètres sont des entiers, leur nombre est fixé par la commande (tableau ci-dessous).

Lecture : `brutus_commande_lire_ligne()`. Écriture : `brutus_commande_ecrire()`. `brutus_journal_enregistrer(FILE*)` permet d'enregistrer les commandes appliquées (pas encore branché sur une option).

Quand le rejouage part d'une sauvegarde intermédiaire, les commandes du journal datées d'avant le tick de départ sont sautées (elles sont déjà dans la sauvegarde). Le fichier d'empreintes l'indique (`commandes_avant_depart`).

## Commandes en place (passent par la file)

| Commande (journal) | Paramètres | Origine dans l'interface (fichier : fonction) | Exécution |
|---|---|---|---|
| `construire` | type x0 y0 x1 y1 rotation rotation_extra orientation_route orientation_vue | `widget/city/city.c` : `build_end` (fin du glisser, **toutes** les constructions : bâtiments, routes, murs, aqueducs, réservoirs, maisons, jardins, places, ponts, démolition = type 9 `BUILDING_CLEAR_LAND`, réparation = type 210 `BUILDING_REPAIR_LAND`, qui ne fait que mesurer et ouvrir la confirmation) | `building_construction_set_type/start/update/place`, état d'interface mis de côté puis remis. Si la pose a utilisé le dernier arc de triomphe, le type choisi par le joueur local est effacé, comme dans le moteur d'origine. |
| `demolir-confirme` | x0 y0 x1 y1 quoi(1 fort, 2 pont, 3 monument) accepte | `building/construction_clear.c` : `confirm_delete_fort/bridge/monument` | `building_construction_brutus_demolir_confirme` |
| `reparer-confirme` | x0 y0 x1 y1 | `building/construction_clear.c` : `confirm_repair_buildings` (réponse « oui » à la confirmation du coût) | `building_construction_brutus_reparer_confirme` : répare et prélève le coût |
| `pari-course` | cheval(1-4) montant | `window/race_bet.c` : `button_confirm` | `city_race_bet_brutus_parier` (`city/race_bet.c`) |
| `annuler` | — | `window/city.c` : `handle_hotkeys` (h->undo) ; `widget/sidebar/city.c` : `button_undo` | `game_undo_perform` |
| `rotation-vue` | sens (0 gauche, 1 droite, 2 nord) | `window/city.c` : `handle_hotkeys` ; `widget/sidebar/city.c` : `button_rotate`, `button_rotate_north` | `game_orientation_rotate_*` (la rotation modifie la simulation : voir limites) |
| `impots` | variation | `window/advisor/financial.c` : `button_change_taxes` | `city_finance_change_tax_percentage` + estimations |
| `salaires` | variation | `window/advisor/labor.c` : `arrow_button_wages` | `city_labor_change_wages` + estimations |
| `priorite-travail` | catégorie priorité | `window/labor_priority.c` : `button_set_priority` | `city_labor_set_priority` |
| `stockage-etat` | x y ressource inverse partiel | `window/building/distribution.c` : `toggle_resource_state`, `toggle_partial_resource_state` | `building_storage_cycle_(partial_)resource_state` |
| `stockage-tout` | x y accepter | `window/building/distribution.c` : `storage_toggle_all_states` | `building_storage_accept_all/none` |
| `stockage-vider` | x y | `window/building/distribution.c` : `storage_empty_all` | `building_storage_toggle_empty_all` |
| `stockage-permission` | x y permission valeur(-1 bascule, 0, 1) | `window/building/distribution.c` : `storage_toggle_permissions`, `toggle_mantain`, bouton « tout / rien » des permissions | `building_storage_toggle/set_permission` |
| `distribution-bien` | x y ressource | `window/building/distribution.c` : `toggle_resource_state` (marchés, docks, bâtiments à inventaire) | `building_distribution_toggle_good_accepted` |
| `distribution-tout` | x y accepter | `window/building/distribution.c` : `market_orders` | `building_distribution_(un)accept_all_goods` |
| `mise-en-sommeil` | x y | `window/building_info.c` : `button_mothball` ; `window/city.c` : raccourci `mothball_toggle` | `building_mothball_toggle` |
| `fete` | dieu taille | `window/hold_festival.c` : `button_hold_festival` | sélection + `city_festival_schedule` |
| `don-cesar` | taille | `window/gift_to_emperor.c` : `button_send_gift` | `city_emperor_set_gift_size` + `city_emperor_send_gift` |
| `salaire-gouverneur` | rang | `window/set_salary.c` : `button_set_salary` | `city_emperor_set_salary_rank` + recalculs |
| `commerce-ouvrir` | ville | `window/empire.c` : `confirmed_open_trade_by_route` | `empire_city_open_trade` + `building_menu_update` |
| `commerce-statut` | ressource statut | `window/resource_settings.c` : `button_toggle_trade` | `city_resource_cycle_trade_status` |
| `commerce-seuil` | ressource export(1)/import(0) variation | `window/resource_settings.c` : `button_trade_up_down` | `city_resource_change_export/import_over` |
| `ressource-sommeil` | ressource | `window/resource_settings.c` : `button_toggle_industry` | `city_resource_toggle_mothballed` |
| `ressource-reserve` | ressource | `window/resource_settings.c` : `button_toggle_stockpile` ; demandes de César (voir ligne suivante) ; `widget/sidebar/extra.c` (ressources insuffisantes) | `city_resource_toggle_stockpiled` |
| `demande-cesar` | id | `window/advisor/imperial.c` : `confirm_send_goods` ; `widget/sidebar/extra.c` : `confirm_send_goods` ; `window/message_dialog.c` : `button_dispatch_request` | `scenario_request_dispatch` |
| `legion-deplacer` | formation x y | `widget/city/city.c` : `military_map_click` | `formation_legion_move_to` |
| `legion-rentrer` | formation (0 = toutes) | `widget/city/city.c` : `military_map_click` ; `window/advisor/military.c` : `return_legion_to_fort` ; `window/building/military.c` : `button_return_to_fort`, `button_all_legions_return_to_fort` ; `widget/sidebar/military.c` : `button_return_to_fort` | `formation_legion_return_home(_all)` |
| `legion-formation` | formation disposition | `window/building/military.c` : `button_layout` ; `widget/sidebar/military.c` (bouton de disposition) | `formation_legion_change_layout` |
| `legion-service` | formation | `window/advisor/military.c`, `widget/sidebar/military.c` : `button_empire_service` | `formation_toggle_empire_service` + `formation_calculate_figures` |
| `message-lu` | séquence | `city/message.c` : `show_message_popup` (fenêtre de message ouverte, directement ou depuis la file) ; `window/message_list.c` : `button_message` | `city_message_brutus_marquer_lu` |
| `message-supprimer` | séquence | `window/message_list.c` : `button_delete`, `button_delete_all_common`, `button_delete_all_read` (une commande par message) | `city_message_brutus_supprimer` → `city_message_delete` |
| `depot-ordre` | x y champ valeur | `window/building/depot.c` : `set_order_source`, `set_order_destination`, `set_order_resource`, `order_clear_source`, `order_clear_destination` ; `window/building_info.c` : condition et seuil | champ 0 : ressource puis `building_depot_brutus_normaliser_ordre` (`building/storage.c`, ce que faisait l'ouverture de la fenêtre) ; 1, 2 : source, destination (l'autre est effacée si c'est la même) ; 3, 4 : condition, seuil |
| `monument-arret` | x y | `window/building_info.c` : `button_monument_construction` | `building_monument_toggle_construction_halted` |
| `monument-module` | x y module | `window/building/culture.c` : `add_module` | `building_monument_add_module` puis `city_finance_process_construction(1000)` |
| `monnaie-conversion` | x y ressource | `window/building/industry.c` : `city_mint_conversion_changed` | ressource produite + remise à zéro de la production (comme avant) |
| `production-reserve` | x y | `window/building/distribution.c` : `button_stockpiling` | `building_stockpiling_toggle` |
| `dock-route` | x y route accepter | `window/building/distribution.c` : `dock_toggle_route` | `building_dock_set_can_trade_with_route` |
| `commerce-politique` | type politique | `window/advisor/trade.c` : `apply_policy` ; `window/building/distribution.c` : `apply_policy_land`, `apply_policy_sea` | `city_trade_policy_set` + `city_finance_process_sundry(TRADE_POLICY_COST)` |
| `barrage-permission` | x y permission | `window/building/utility.c` : `button_toggle_figure_state`, `button_roadblock_orders` | `building_roadblock_set_permission` (bascule) / `accept_none` (-1) / `accept_all` (-2) |
| `caserne-priorite`, `caserne-livraison` | x y [priorité] | `window/building/military.c` : `button_priority`, `button_delivery` | `building_barracks_set_priority`, `building_barracks_toggle_delivery` |
| `jeux` | id | `window/hold_games.c` : `button_hold_games` (le choix reste dans la fenêtre) | `city_games_brutus_choisir` + `city_games_schedule` |
| `don-ville` | montant | `window/donate_to_city.c` : `button_donate` (le montant reste dans la fenêtre) | `city_emperor_set_donation_amount` + `city_emperor_donate_savings_to_city` |
| `legions-bataille` | annuler_service | `window/advisor/imperial.c` (1), `widget/sidebar/extra.c` (0) : `confirm_send_troops` | `formation_legions_dispatch_to_distant_battle` (+ `city_military_clear_empire_service_legions`) |
| `legions-service-annuler` | — | `widget/sidebar/extra.c` : `button_handle_request` | `city_military_clear_empire_service_legions` |
| `victoire-continuer` | mois | `window/victory_dialog.c` : `button_continue_governing` | `city_victory_continue_governing` + `city_victory_reset` |
| `triche` | numéro a b c d | `window/console.c` → `game_cheat_parse_command` (sauf triches d'affichage : info-bulles, éditeur d'événements) ; raccourcis `game_cheat_money` (100), `game_cheat_victory` (101), `game_cheat_activate` (102) | `game_cheat_brutus_executer` : la triche de la console avec ses 4 entiers, ou argent / victoire / invasion. Les effets durables (légions en plus, sans consommation, sans invasion, marchés pleins) sont écrits dans l'en-tête de partie |

Les fenêtres de fête, de cadeau, de jeux et de don gardent le choix en cours dans la fenêtre (plus dans `city_data`) : leur simple ouverture ne change plus la partie. La commande porte le choix (`fete`, `don-cesar`, `jeux`, `don-ville`).

### Ce que le bot exerce vraiment

Le bot de test (`outils/bot-rejouage.mjs`) ne voit pas la partie : beaucoup de ses commandes sont refusées. Journaux de référence (graine 42, 150 000 ticks, lignes `# bilan` de `build-rejouage/<carte>/empreintes-1.txt`, 19/11/2026) :

| Carte | Commandes | Refusées | Fin de partie |
|---|---|---|---|
| Lugdunum | 869 | 152 | 98 habitants, 143 bâtiments |
| Tarraco | 901 | 179 | 62 habitants, 44 bâtiments |
| Carthago | 861 | 149 | 15 habitants, 36 bâtiments |

**Acceptées au moins une fois** (sur les trois cartes) : `construire`, `annuler`, `rotation-vue`, `impots`, `salaires`, `priorite-travail`, `stockage-*`, `distribution-bien`, `mise-en-sommeil`, `fete`, `don-cesar`, `salaire-gouverneur`, `commerce-statut`, `commerce-seuil`, `ressource-reserve`, `ressource-sommeil`, `reparer-confirme`, `pari-course`, `legion-rentrer`, et parmi les nouvelles : `commerce-politique`, `don-ville`, `legions-service-annuler`, `triche` (argent, marchés pleins, légions sans consommation, invasions coupées), `message-lu`, `message-supprimer`.

**Toujours refusées** (seule la validation est exercée) : `depot-ordre`, `monument-arret`, `monument-module`, `monnaie-conversion`, `production-reserve`, `dock-route`, `barrage-permission`, `caserne-priorite`, `caserne-livraison`, `jeux`, `legions-bataille`, `victoire-continuer`, `demande-cesar`, `legion-formation`, `distribution-tout`. Le bot pose rarement les bâtiments visés (dépôt, caserne, barrage, dock, monument, hôtel des monnaies), il n'a pas de légion et ne gagne pas la mission.

**Jamais exercées par un test** : `demolir-confirme`, `commerce-ouvrir`, `legion-deplacer`, `legion-service`.

## Encore à migrer

Tout ce qui modifiait la partie depuis l'interface passe désormais par une commande. Restent, volontairement :

| Action | Pourquoi ce n'est pas une commande |
|---|---|
| Vitesse et pause (`window/city.c`, `widget/city/city.c`, `widget/sidebar/extra.c` : `game_state_toggle_paused`, `setting_*_game_speed`) | Elles règlent **quand** les ticks passent, pas la partie. En multijoueur, `game_run()` ne les lit plus : le nombre de ticks vient du lockstep. Elles deviendront des messages de session (M6b). La pause déclenchée par la simulation (route de Rome coupée) reste à partager en M6b. |
| Pari de course : cheval et montant en cours dans la fenêtre (`window/race_bet.c`) | Champs de la fenêtre, hors partie. Le pari lui-même passe par `pari-course`. |
| Fin de mission : `city_victory_stop_governing` (`window/mission_end.c`) | Appelé quand le joueur quitte la ville (mission suivante, renvoi) : la partie s'arrête. Une commande ne serait jamais appliquée (le chargement suivant vide la file). |
| `game_undo_disable` depuis l'interface (retour au menu, fin de mission) | Même cas : la partie est quittée. |
| Triches d'affichage (`showtooltip`, `debug.customevents`, `debug.showeditor`) et activation des triches (`data.is_cheating`) | Écran seulement. |
| Effets du dessin (`widget/city/draw.c` : `fumigation_frame`, `fumigation_direction`, animations dans la grille des sprites) | Hors empreinte et jamais lus par la simulation. Le test « interface ouverte » dessine les fenêtres pendant la partie : empreintes inchangées. |
| Recalculs faits par des fenêtres (conseillers, fiche d'entrepôt, demandes de César prêtes, aperçu des parcours) | Gardés en solo (comportement d'origine). En multijoueur ils sont sautés : les fenêtres affichent les valeurs tenues par la simulation (voir `docs/determinisme.md`, « Limites connues »). |

## Effets d'interface pendant l'exécution

L'exécution d'une commande agit sur la partie ; ce qu'elle montre à l'écran ne concerne que le joueur qui l'a envoyée (`brutus_interface_pour_moi()`) :

| Où | Effet | Traitement (M6a) |
|---|---|---|
| `building/construction_clear.c` : `building_construction_clear_land` (fort, monument, pont) | confirmation pendant `construire` (démolition) | la fenêtre ne s'ouvre que chez l'émetteur ; la zone est gardée à part (`confirmation_locale`), une commande d'un autre joueur ne la change pas ; la réponse revient par `demolir-confirme` |
| `building/construction_clear.c` : `building_construction_repair_land` | confirmation du coût pendant `construire` (réparation) | idem, réponse par `reparer-confirme` |
| `city_warning_show*` (pose, triches…) | avertissements en haut de l'écran | seulement chez l'émetteur (`city/warning.c`) |
| `city/message.c` : `city_message_post` (simulation) | fenêtre, file ou alerte selon l'écran et `CONFIG_UI_MESSAGE_ALERTS` | la simulation n'écrit plus que le message (non lu). La décision d'affichage est prise après chaque tick ou commande, chez chacun (`city_message_brutus_afficher`) ; un message dû à une commande n'est montré qu'à son émetteur. « Lu » et « supprimé » passent par `message-lu` / `message-supprimer`. Le tableau des messages reste trié (le nouveau en tête) : les tris faits par la liste des messages ne changent rien. |

Non concernés (réglages locaux, pas l'état de la partie) : caméra, signets, superpositions, sélection de légion, menus de construction (`building_construction_set_type`, rotation de l'aperçu), options d'affichage. Les options qui changent la simulation (`gameplay_*`, difficulté, dieux…) sont dans l'en-tête de partie et verrouillées en multijoueur (`docs/determinisme.md`).
