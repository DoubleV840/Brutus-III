#ifndef MULTIJOUEUR_EMPREINTE_H
#define MULTIJOUEUR_EMPREINTE_H

/*
 * Brutus III : empreinte (hash) de l'état de la partie, phase 2b.
 * On hache l'état tel qu'il est écrit dans une sauvegarde (tampons de game/file_io.c),
 * sans compression, en excluant ce qui ne relève pas de la simulation.
 * Voir docs/determinisme.md.
 */

#include <stddef.h>
#include <stdint.h>

#define BRUTUS_FNV_DEPART 0xcbf29ce484222325ULL

/** FNV-1a 64 bits, à enchaîner (départ : BRUTUS_FNV_DEPART). */
uint64_t brutus_fnv1a(uint64_t h, const void *donnees, size_t taille);

/** Vrai pendant le calcul d'une empreinte (les champs visuels sont alors écrits à zéro). */
int brutus_empreinte_en_cours(void);
void brutus_empreinte_marquer(int en_cours);

/** Empreinte de l'état courant de la partie. */
uint64_t brutus_empreinte_partie(void);

#endif // MULTIJOUEUR_EMPREINTE_H
