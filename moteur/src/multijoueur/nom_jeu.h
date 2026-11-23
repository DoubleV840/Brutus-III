#ifndef MULTIJOUEUR_NOM_JEU_H
#define MULTIJOUEUR_NOM_JEU_H

// Brutus III : le nom du jeu affiché au joueur (titre de la fenêtre, bannière, textes), à la place
// d'« Augustus » et de « Caesar III ». Le moteur reste crédité (menu principal, page web).
#define BRUTUS_NOM_JEU "Brutus III"

#include <stdint.h>

/**
 * Remplace « Augustus » (le nom du jeu) par « Brutus III » dans un texte UTF-8 des traductions.
 * « augustus-log.txt » (minuscule) et les mots déclinés (« Augustusa ») ne sont pas touchés.
 * Renvoie le texte d'origine s'il n'y a rien à changer, sinon un tampon interne (valable jusqu'à
 * l'appel suivant).
 */
const char *brutus_nom_jeu_traduire(const char *utf8);

/**
 * Remplace sur place « Caesar III » par « Brutus III » (même longueur) dans les textes des
 * fichiers du jeu original (c3.eng, c3_mm.eng…), une fois chargés en mémoire.
 */
void brutus_nom_jeu_textes_originaux(uint8_t *donnees, int taille);

#endif // MULTIJOUEUR_NOM_JEU_H
