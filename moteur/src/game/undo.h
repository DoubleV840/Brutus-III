#ifndef GAME_UNDO_H
#define GAME_UNDO_H

#include "building/building.h"

int game_can_undo(void);

void game_undo_disable(void);

void game_undo_add_building(building *b);

void game_undo_adjust_building(building *b);

void game_undo_record_building_type(building *b);

void game_undo_restore_building_types(void);

int game_undo_contains_building(int building_id);

void game_undo_restore_building_state(void);

void game_undo_restore_map(int include_properties);

int game_undo_start_build(building_type type);

void game_undo_finish_build(int cost);

void game_undo_perform(void);

void game_undo_reduce_time_available(void);

/*
 * Brutus III : contexte d'annulation de la simulation (commandes, ticks) séparé de celui de
 * l'aperçu du joueur local. Appelés par multijoueur/simulation.c.
 */
void game_undo_brutus_entrer_simulation(void);
void game_undo_brutus_sortir_simulation(void);

/** Vrai si la dernière construction annulable de la simulation vient de ce joueur. */
int game_undo_brutus_appartient(int joueur);

/** Partie chargée ou démarrée : rien à annuler, copies de secours lues dans la sauvegarde. */
void game_undo_brutus_partie_chargee(void);

#endif // GAME_UNDO_H
