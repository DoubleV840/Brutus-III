#ifndef MULTIJOUEUR_SIMULATION_H
#define MULTIJOUEUR_SIMULATION_H

/*
 * Brutus III : rythme des ticks et frontière entre simulation et interface (M6a).
 *
 * - Solo : le nombre de ticks vient de l'horloge et de l'écran (game_speed_get_elapsed_ticks),
 *   comme dans le jeu d'origine.
 * - Multijoueur : le nombre de ticks vient de l'extérieur (le lockstep, M6b), par
 *   brutus_ticks_externes_ajouter(). Une fenêtre ouverte, un glisser ou un défilement ne
 *   changent ni le nombre de ticks ni la partie.
 *
 * Tout passage dans la simulation (ticks, commandes, empreinte, sauvegarde) est encadré par
 * brutus_simulation_debut() / brutus_simulation_fin() : l'aperçu de construction du joueur
 * local est retiré de la carte, et l'annulation utilise le contexte de la simulation (pas
 * celui de l'aperçu). Voir docs/determinisme.md.
 */

/** Entre dans la simulation (imbriquable). */
void brutus_simulation_debut(void);

/** Sort de la simulation : l'aperçu du joueur local est remis sur la carte. */
void brutus_simulation_fin(void);

/** Vrai pendant un passage dans la simulation. */
int brutus_simulation_en_cours(void);

/**
 * Avance la partie de `n` ticks (commandes appliquées avant chaque tick), quel que soit
 * l'écran. Retourne le nombre de ticks faits. En solo, s'arrête comme avant si une fenêtre
 * s'ouvre (message) ; en multijoueur, fait toujours les `n` ticks.
 */
int brutus_avancer_ticks(int n);

/** Multijoueur : ticks autorisés par le lockstep, consommés par game_run(). */
void brutus_ticks_externes_ajouter(int n);
int brutus_ticks_externes_en_attente(void);

/**
 * Resynchronisation (M6b) : l'hôte écrit la sauvegarde (game_file_write_saved_game, avec
 * l'en-tête de partie), puis TOUS les joueurs, hôte compris, la rechargent par cette fonction.
 * Ainsi personne ne garde d'état hors sauvegarde (caches, annulation, aperçu). Retourne 1 si OK.
 */
int brutus_resynchroniser(const char *sauvegarde);

/** Une image du jeu (game_run) : ticks, commandes et messages à afficher. */
void brutus_simulation_image(void);

#endif // MULTIJOUEUR_SIMULATION_H
