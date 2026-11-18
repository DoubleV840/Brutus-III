# Couche de commandes (phase 2a)

Code : `moteur/src/multijoueur/commande.c` / `commande.h`.

## Principe

Toute action du joueur qui modifie la partie devient une commande `{type, joueur, tick, paramètres}` :

1. l'interface appelle `brutus_commande_envoyer()` (ou `brutus_commande_1()`…) au lieu de modifier l'état ;
2. la commande est datée du **tick absolu** suivant : `jours écoulés × 50 + tick du jour` (valeurs sauvegardées) ;
3. elle est appliquée **avant** ce tick, dans l'ordre `(tick, joueur, ordre d'arrivée)` (l'hôte, joueur 0, d'abord).

Points d'application :

- `game_tick_run()` (`game/tick.c`) : avant chaque tick de simulation ;
- `game_run()` (`game/game.c`) : à chaque image, avant les ticks. En solo, une commande est donc appliquée dès l'image suivante, même en pause. Comme aucun tick ne passe entre-temps, c'est équivalent à « avant le tick T ». En réseau (2d), seul le premier point d'application servira, piloté par le lockstep.

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

### Ce que le bot exerce vraiment

Le bot de test (`outils/bot-rejouage.mjs`) ne voit pas la partie : beaucoup de ses commandes sont refusées. Bilan du journal de référence (graine 42, 150 000 ticks, lignes `# bilan` de `build-rejouage/empreintes-1.txt`) :

| Commande | Envoyées | Refusées | Remarque |
|---|---|---|---|
| `construire` | 521 | 24 | refus = coordonnées hors carte ; le moteur refuse aussi des poses (terrain, argent) sans les compter |
| `impots`, `salaires`, `fete`, `annuler`, `rotation-vue` | 60, 25, 15, 6, 2 | 0 | |
| `priorite-travail` | 58 | 5 | |
| `stockage-etat`, `-tout`, `-vider`, `-permission` | 74, 13, 14, 22 | 66, 11, 13, 19 | le bot vise souvent un entrepôt démoli |
| `distribution-bien`, `distribution-tout` | 16, 1 | 15, 1 | cibles au hasard, rarement un marché |
| `mise-en-sommeil` | 23 | 18 | |
| `commerce-statut`, `commerce-seuil`, `ressource-reserve`, `ressource-sommeil`, `salaire-gouverneur` | 9, 3, 7, 2, 1 | 0 | |
| `reparer-confirme` | 5 | 0 | zones souvent sans ruine : la réparation ne fait rien |
| `pari-course` | 2 | 1 | |
| `demande-cesar`, `legion-formation` | 2, 4 | 2, 4 | validation seule : pas de demande en cours, pas de légion |
| `legion-rentrer` | 7 | 0 | sans légion, ne fait rien |

**Jamais exercées par un test** : `demolir-confirme`, `don-cesar` (pas d'épargne), `commerce-ouvrir`, `legion-deplacer`, `legion-service`, et toute commande militaire avec une vraie légion (le bot ne construit pas de fort).

## Encore à migrer (l'interface modifie l'état directement : non)

| Action | Fichier : fonction / appel direct |
|---|---|
| Dépôts (charrettes) : ressource, source, destination, condition, seuil | `window/building/depot.c` (écritures `b->data.depot.current_order.*`) ; `window/building_info.c` (condition et seuil) |
| Arrêt / reprise d'un chantier de monument | `window/building_info.c` : `building_monument_toggle_construction_halted` |
| Module de grand temple | `window/building/culture.c` : `building_monument_add_module` |
| Conversion de l'hôtel des monnaies | `window/building/industry.c` : `city_mint_conversion_changed` |
| Mise en réserve d'un producteur | `window/building/distribution.c` : `building_stockpiling_toggle` |
| Routes commerciales d'un dock | `window/building/distribution.c` : `building_dock_set_can_trade_with_route` |
| Politique commerciale terre / mer | `window/building/distribution.c`, `window/advisor/trade.c` : `city_trade_policy_set` |
| Barrage routier : permissions | `window/building/utility.c` : `building_roadblock_accept_all/none/set_permission` |
| Caserne : priorité, livraisons | `window/building/military.c` : `building_barracks_set_priority`, `building_barracks_toggle_delivery` |
| Jeux (Colisée…) | `window/hold_games.c` : `city_games_schedule` |
| Pari de course : sélection en cours dans la fenêtre | `window/race_bet.c` : champs locaux de la fenêtre (le pari lui-même passe par `pari-course`) |
| Don à la ville (épargne du gouverneur) | `window/donate_to_city.c` : `city_emperor_change/set_donation_amount`, `city_emperor_donate_savings_to_city` |
| Envoi des légions à une bataille lointaine | `window/advisor/imperial.c`, `widget/sidebar/extra.c` : `formation_legions_dispatch_to_distant_battle`, `city_military_clear_empire_service_legions` |
| Choix dans les fenêtres (dieu et taille de fête, taille du cadeau) | `window/hold_festival.c` : `city_festival_select_god/size` ; `window/gift_to_emperor.c` : `city_emperor_set_gift_size` (champs de `city_data`, sauvegardés : la commande les réécrit, mais la simple ouverture de la fenêtre les change chez un seul joueur) |
| Messages : lu, supprimé, message courant | `window/message_list.c` : `city_message_mark_read`, `city_message_delete`, `city_message_set_current` (sauvegardés dans la partie) |
| Fin de mission : continuer / arrêter de gouverner | `window/victory_dialog.c` : `city_victory_continue_governing` ; `window/mission_end.c` : `city_victory_stop_governing` |
| Triches (console) | `window/console.c` : `game_cheat_parse_command` |
| Vitesse et pause | `window/city.c`, `widget/city/city.c`, `widget/sidebar/extra.c` : `game_state_toggle_paused`, `setting_increase/decrease_game_speed`. Ce ne sont pas des commandes de simulation : elles règlent **quand** les ticks passent. En réseau, elles deviendront des messages de session (2d). |
| Aperçus qui écrivent dans la carte | aperçu du glisser (`building_construction_update` : marques de construction, annulation provisoire), fantôme de démolition (`widget/city/building_ghost.c` : marques « supprimé », `map_tiles_clear_highway`), pont (`map_bridge_calculate_length_direction` pendant le dessin) |
| Effets du dessin sur l'état | `widget/city/draw.c` : `fumigation_direction`, animations (`map_sprite_animation_set`, images de la carte). Exclus de l'empreinte, mais à sortir de l'état partagé en 2d. |

## Effets d'interface pendant l'exécution (à traiter en M6)

L'exécution d'une commande doit agir sur la partie, pas sur l'écran. Aujourd'hui, certaines ouvrent encore une fenêtre ou un avertissement **chez tous les joueurs** :

| Où | Effet | Conséquence en réseau |
|---|---|---|
| `building/construction_clear.c` : `building_construction_clear_land` (fort, monument, pont) | `window_popup_dialog_show(...)` pendant `construire` (démolition) | le second joueur verrait une confirmation qu'il n'a pas demandée ; sa réponse enverrait un `demolir-confirme` |
| `building/construction_clear.c` : `building_construction_repair_land` | `window_popup_dialog_show_confirmation(...)` pendant `construire` (réparation) | idem, avec `reparer-confirme` |
| `city_warning_show(...)` dans toute la pose (`construction.c`, `construction_building.c`, `building.c`) | avertissements en haut de l'écran | affichage seulement, mais chez les deux joueurs |
| `city/message.c` : `city_message_post` (appelé par la simulation) | choisit entre `show_message_popup` (marque le message lu) et `enqueue_message` selon la fenêtre ouverte (`window_is`) et `CONFIG_UI_MESSAGE_ALERTS` | l'état des messages, sauvegardé et dans l'empreinte, dépend de l'écran de chacun : **fausse désynchronisation probable** |

Correction prévue en M6 : l'exécution note « confirmation nécessaire pour le joueur N » au lieu d'ouvrir la fenêtre, et seul le joueur à l'origine de la commande la voit ; la décision d'afficher un message sort de la simulation.

Non concernés (réglages locaux, pas l'état de la partie) : options et configuration (`window/config.c`), caméra, signets, superpositions, sélection de légion, menus de construction (`building_construction_set_type`, rotation de l'aperçu).
