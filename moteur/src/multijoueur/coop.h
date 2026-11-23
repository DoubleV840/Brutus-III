#ifndef MULTIJOUEUR_COOP_H
#define MULTIJOUEUR_COOP_H

/*
 * Brutus III (M7) : mode Coop, ce que chaque joueur voit des autres.
 *
 * Rien ici n'entre dans la simulation, la sauvegarde ou l'empreinte : ce sont des données
 * d'écran, échangées par le client JavaScript (web/lockstep.js) hors du lockstep :
 *  - la vue de chaque autre joueur (caméra, taille de la vue, curseur), message PRESENCE ;
 *  - les signaux posés sur la carte (touche G), message SIGNAL ;
 *  - les ordres du joueur local qui ont échoué parce qu'un autre joueur a agi juste avant
 *    dans le même tour (conflit), signalés par un avertissement et comptés pour JS.
 * Les données passent par le tampon de multijoueur/reseau.h. Voir docs/protocole.md.
 */

#include "graphics/color.h"
#include "multijoueur/commande.h"
#include "multijoueur/reseau.h"

#include <stdint.h>

/** Taille en octets d'une présence (6 x i32 : caméra x, y, largeur, hauteur, orientation, curseur). */
#define BRUTUS_COOP_PRESENCE 24

/** Couleur d'un joueur (opaque, pour la mini-carte). */
color_t brutus_coop_couleur(int joueur);

/** Écrit la présence du joueur local dans le tampon. Retourne sa taille, ou 0 hors de la ville. */
BRUTUS_RESEAU_EXPORT int brutus_coop_ma_presence(void);
/** Présence d'un autre joueur, lue dans le tampon (`taille` octets). */
BRUTUS_RESEAU_EXPORT void brutus_coop_presence(int joueur, int taille);
/** Oublie la présence d'un joueur (parti), ou de tous si joueur < 0. Efface aussi les signaux si < 0. */
BRUTUS_RESEAU_EXPORT void brutus_coop_oublier(int joueur);
/** Case de la carte sous la souris (grid offset), ou -1. */
BRUTUS_RESEAU_EXPORT int brutus_coop_curseur(void);
/** Affiche un signal d'un joueur sur une case. Retourne 1 si la case est valide. */
BRUTUS_RESEAU_EXPORT int brutus_coop_signal(int joueur, int grid_offset);
/** Centre la vue locale sur une case. */
BRUTUS_RESEAU_EXPORT void brutus_coop_aller(int grid_offset);
/** Nombre d'ordres du joueur local refusés pour conflit depuis le dernier appel. */
BRUTUS_RESEAU_EXPORT int brutus_coop_conflits(void);

/** Tests : la n-ième case libre (3 x 3 cases dégagées autour), ou -1 ; x et y (i32) dans le tampon. */
BRUTUS_RESEAU_EXPORT int brutus_coop_test_case_libre(int n);
/** Tests (Coop jusqu'à 4) : vrai si la vue du joueur `joueur` est connue (cadre sur la mini-carte). */
BRUTUS_RESEAU_EXPORT int brutus_coop_test_presence(int joueur);

/** multijoueur/commande.c : un ordre du joueur local a échoué après celui d'un autre joueur. */
void brutus_coop_signaler_conflit(void);

/** widget/city/draw.c : curseurs des autres joueurs et signaux, sur la ville. */
void brutus_coop_dessiner_ville(float scale);

/**
 * widget/minimap.c : cadre de la vue d'un autre joueur, en coordonnées de vue (comme la caméra).
 * Retourne 0 s'il n'y a rien à dessiner pour ce joueur.
 */
int brutus_coop_vue_minimap(int joueur, int *x, int *y, int *largeur, int *hauteur, color_t *couleur);
/** widget/minimap.c : signal `i` (0 à BRUTUS_COOP_SIGNAUX - 1), en coordonnées de vue. */
int brutus_coop_signal_minimap(int i, int *x, int *y, color_t *couleur);
#define BRUTUS_COOP_SIGNAUX 8

#endif // MULTIJOUEUR_COOP_H
