#ifndef MULTIJOUEUR_FIN_VERSUS_H
#define MULTIJOUEUR_FIN_VERSUS_H

/*
 * Brutus III (M11) : écran de fin du Versus (gagnant, raison, détail des scores des villes).
 * Interface seulement : la fin elle-même est décidée par la simulation (victoire_versus.h).
 */

/**
 * Appelé à chaque image, hors simulation : si la partie vient de finir et que la ville est à l'écran,
 * ouvre l'écran de fin (une fois par fin).
 */
void brutus_fin_versus_verifier(void);

/** Ouvre l'écran de fin (sans effet si la partie n'est pas finie). */
void window_brutus_fin_versus_show(void);

#endif // MULTIJOUEUR_FIN_VERSUS_H
