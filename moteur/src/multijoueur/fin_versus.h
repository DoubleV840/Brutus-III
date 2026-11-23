#ifndef MULTIJOUEUR_FIN_VERSUS_H
#define MULTIJOUEUR_FIN_VERSUS_H

/*
 * Brutus III (M11, M12) : écran de fin du Versus : gagnant, raison, récap de la partie (courbes qui
 * comparent les deux villes, un onglet par statistique, tableau des scores) et faits marquants.
 * Interface seulement : la fin et les relevés du récap sont faits par la simulation
 * (victoire_versus.h, recap_versus.h), identiques chez tous les joueurs.
 */

/** Onglets de l'écran de fin (le premier est celui affiché à l'ouverture). */
enum {
    BRUTUS_FIN_ONGLET_SCORE = 0,
    BRUTUS_FIN_ONGLET_POPULATION,
    BRUTUS_FIN_ONGLET_TRESOR,
    BRUTUS_FIN_ONGLET_PROSPERITE,
    BRUTUS_FIN_ONGLET_CULTURE,
    BRUTUS_FIN_ONGLET_SOLDATS,
    BRUTUS_FIN_ONGLET_CONSTRUITS,
    BRUTUS_FIN_ONGLET_PERDUS,
    BRUTUS_FIN_ONGLET_TUES,
    BRUTUS_FIN_ONGLET_TABLEAU,
    BRUTUS_FIN_ONGLETS
};

/**
 * Appelé à chaque image, hors simulation : si la partie vient de finir et que la ville est à l'écran,
 * ouvre l'écran de fin (une fois par fin).
 */
void brutus_fin_versus_verifier(void);

/** Ouvre l'écran de fin (sans effet si la partie n'est pas finie), sur l'onglet du score. */
void window_brutus_fin_versus_show(void);

/** Change l'onglet affiché (captures du rejouage). */
void brutus_fin_versus_onglet(int onglet);
/** Place la souris (coordonnées de l'écran de fin) pour montrer la bulle de survol (captures). */
void brutus_fin_versus_survol(int x, int y);

#endif // MULTIJOUEUR_FIN_VERSUS_H
