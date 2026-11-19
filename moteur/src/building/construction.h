#ifndef BUILDING_CONSTRUCTION_H
#define BUILDING_CONSTRUCTION_H

#include "building/type.h"
#include "figure/type.h"
#include "map/grid.h"

typedef enum {
    AUTO_CYCLE_GROUP_TEMPLES,
    AUTO_CYCLE_GROUP_GARDENS,
    AUTO_CYCLE_GROUP_MAX
} auto_cycle_group;

void building_construction_reset_cycle_steps(void);

int building_construction_cycle_forward(void);

int building_construction_cycle_back(void);

int building_construction_type_can_cycle(building_type type);

int building_construction_type_num_cycles(building_type type);

int building_construction_type_cycle_steps(building_type type);

int building_construction_type_cycled_steps(building_type type);

void building_construction_set_cost(int cost);

void building_construction_set_type(building_type type, int setup_rotation);

void building_construction_clear_type(void);

int building_construction_is_auto_cycling(auto_cycle_group group);

void building_construction_toggle_auto_cycle(auto_cycle_group group);

int building_construction_can_rotate(void);

building_type building_construction_type(void);

int building_construction_cost(void);

int building_construction_size(int *x, int *y);

int building_construction_in_progress(void);

void building_construction_start(int x, int y, int grid_offset);

int building_construction_is_updatable(void);

void building_construction_cancel(void);

void building_construction_update(int x, int y, int grid_offset);

figure_type building_construction_nearby_enemy_type(grid_slice *slice);

void building_construction_offset_start_from_orientation(int *x, int *y, int size);

void building_construction_place(void);

int building_construction_can_place_on_terrain(int x, int y, int *warning_id);

void building_construction_record_view_position(int view_x, int view_y, int grid_offset);
void building_construction_get_view_position(int *view_x, int *view_y);
int building_construction_get_start_grid_offset(void);

void building_construction_reset_draw_as_constructing(void);
int building_construction_draw_as_constructing(void);
/** @brief to place a single wall tile at the given grid offset. since walls are being moved to building category,
* Every tile should be handled separately with individual building IDs
*/
int building_construction_place_wall(int grid_offset);

/* Brutus III : accroches de la couche de commandes (multijoueur/commande.c). */
/** Coordonnées brutes (avant décalage d'orientation) du début et de la fin du glisser en cours. */
void building_construction_brutus_lire_brut(int *x_start, int *y_start, int *x_end, int *y_end);
/** Met de côté / remet l'état d'interface de la construction (type choisi, glisser, rotation). */
void building_construction_brutus_sauver(void);
void building_construction_brutus_restaurer(void);
/** Retire l'aperçu du glisser en cours de la carte (comme une annulation), sans changer le type choisi. */
void building_construction_brutus_retirer_apercu(void);
/** Effet d'interface de la pose (rotation automatique des cycles de bâtiments), côté joueur local. */
void building_construction_brutus_apres_placement_interface(void);
/** Recrée l'aperçu du glisser après restauration (si un glisser était en cours). */
void building_construction_brutus_relancer_apercu(void);

/** Brutus III : retire l'aperçu du joueur local de la carte (avant la simulation). */
void building_construction_brutus_suspendre_apercu(void);

/** Brutus III : remet l'aperçu retiré par building_construction_brutus_suspendre_apercu(). */
void building_construction_brutus_reprendre_apercu(void);

#endif // BUILDING_CONSTRUCTION_H


