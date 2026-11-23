#ifndef MULTIJOUEUR_REJOUAGE_INTERFACE_H
#define MULTIJOUEUR_REJOUAGE_INTERFACE_H

/*
 * Brutus III : interface scriptée pendant un rejouage multijoueur (option --interface-test).
 * Voir rejouage_interface.c et outils/test-rejouage.sh (test « conseiller ouvert »).
 */

/** Avant les ticks de l'image `image` : ouvre / ferme des fenêtres, glisse, survole. */
void brutus_interface_test_image(long image);

/** Après les ticks de l'image `image` : dessine parfois la fenêtre ouverte. */
void brutus_interface_test_dessiner(long image);

/** Nombre de fenêtres ouvertes et de glissers commencés. */
void brutus_interface_test_bilan(unsigned int *ouvertures, unsigned int *glissers);

#include <stdio.h>

/** --test-batiments : chaque type du menu de construction, survolé, cliqué, glissé. 1 si aucune image lente. */
int brutus_interface_test_batiments(FILE *sortie);

#endif // MULTIJOUEUR_REJOUAGE_INTERFACE_H
