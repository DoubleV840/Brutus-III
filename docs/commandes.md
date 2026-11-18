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

Les commandes qui visent un bâtiment le désignent par **une tuile** `x y` du bâtiment (coordonnées de carte). L'exécution retrouve le bâtiment principal (`map_building_at` puis `building_main`). Les paramètres sont toujours revérifiés à l'exécution (bornes, bâtiment existant, type, argent) : une commande invalide ne fait rien.

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

## Commandes en place (passent par la file)

| Commande (journal) | Paramètres | Origine dans l'interface (fichier : fonction) | Exécution |
|---|---|---|---|
| `construire` | type x0 y0 x1 y1 rotation rotation_extra orientation_route orientation_vue | `widget/city/city.c` : `build_end` (fin du glisser, **toutes** les constructions : bâtiments, routes, murs, aqueducs, réservoirs, maisons, jardins, places, ponts, démolition = type 9 `BUILDING_CLEAR_LAND`, réparation) | `building_construction_set_type/start/update/place`, état d'interface mis de côté puis remis |
| `demolir-confirme` | x0 y0 x1 y1 quoi(1 fort, 2 pont, 3 monument) accepte | `building/construction_clear.c` : `confirm_delete_fort/bridge/monument` | `building_construction_brutus_demolir_confirme` |
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

Le bot de test (`outils/bot-rejouage.mjs`) utilise toutes les commandes du tableau sauf `demolir-confirme`, `don-cesar`, `salaire-gouverneur`, `commerce-*`, `ressource-*`, `demande-cesar`, `legion-deplacer`, `legion-formation`, `legion-service` et `distribution-tout`. Celles-ci sont compilées et branchées, mais **pas encore exercées** par un test.

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
| Don à la ville (épargne du gouverneur) | `window/donate_to_city.c` : `city_emperor_change/set_donation_amount`, `city_emperor_donate_savings_to_city` |
| Envoi des légions à une bataille lointaine | `window/advisor/imperial.c`, `widget/sidebar/extra.c` : `formation_legions_dispatch_to_distant_battle`, `city_military_clear_empire_service_legions` |
| Choix dans les fenêtres (dieu et taille de fête, taille du cadeau) | `window/hold_festival.c` : `city_festival_select_god/size` ; `window/gift_to_emperor.c` : `city_emperor_set_gift_size` (champs de `city_data`, sauvegardés : la commande les réécrit, mais la simple ouverture de la fenêtre les change chez un seul joueur) |
| Messages : lu, supprimé, message courant | `window/message_list.c` : `city_message_mark_read`, `city_message_delete`, `city_message_set_current` (sauvegardés dans la partie) |
| Fin de mission : continuer / arrêter de gouverner | `window/victory_dialog.c` : `city_victory_continue_governing` ; `window/mission_end.c` : `city_victory_stop_governing` |
| Triches (console) | `window/console.c` : `game_cheat_parse_command` |
| Vitesse et pause | `window/city.c`, `widget/city/city.c`, `widget/sidebar/extra.c` : `game_state_toggle_paused`, `setting_increase/decrease_game_speed`. Ce ne sont pas des commandes de simulation : elles règlent **quand** les ticks passent. En réseau, elles deviendront des messages de session (2d). |
| Aperçus qui écrivent dans la carte | aperçu du glisser (`building_construction_update` : marques de construction, annulation provisoire), fantôme de démolition (`widget/city/building_ghost.c` : marques « supprimé », `map_tiles_clear_highway`), pont (`map_bridge_calculate_length_direction` pendant le dessin) |
| Effets du dessin sur l'état | `widget/city/draw.c` : `fumigation_direction`, animations (`map_sprite_animation_set`, images de la carte). Exclus de l'empreinte, mais à sortir de l'état partagé en 2d. |

Non concernés (réglages locaux, pas l'état de la partie) : options et configuration (`window/config.c`), caméra, signets, superpositions, sélection de légion, menus de construction (`building_construction_set_type`, rotation de l'aperçu).
