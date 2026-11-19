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

#endif // MULTIJOUEUR_REJOUAGE_INTERFACE_H
