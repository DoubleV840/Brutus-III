#ifndef MULTIJOUEUR_CARTE_VERSUS_H
#define MULTIJOUEUR_CARTE_VERSUS_H

/*
 * Brutus III (M9) : cartes à deux, un point d'entrée et un point de sortie par joueur.
 *
 * Le joueur 1 (ville 0) garde les points du scénario d'Augustus. Les points des autres joueurs sont
 * à nous : dans la carte, ils vont dans un bloc « BR3C » écrit après les données d'Augustus (un
 * Augustus d'origine lit la carte sans le voir) ; dans une partie, ils sont dans le bloc Versus
 * (multijoueur/versus.c). Quand la partie passe en Versus, la ville n prend les points du joueur n
 * (accès à Rome, arrivée des migrants et des marchands, départ des émigrants).
 *
 * Carte sans ces points (toutes les cartes d'origine) : la ville 2 reçoit les points de la ville 1,
 * comme en M8.
 */

#include "core/buffer.h"
#include "map/point.h"

#include <stdio.h>

/** Vrai si la carte a des points pour le joueur 2 (au moins une entrée). */
int brutus_carte_versus_a_points(void);

/** Points du joueur de cette ville (ville 0 : ceux du scénario). {-1, -1} si aucun. */
map_point brutus_carte_versus_entree(int ville);
map_point brutus_carte_versus_sortie(int ville);

/** Sortie du joueur telle que posée ({-1, -1} si aucune) : drapeau de l'éditeur. */
map_point brutus_carte_versus_valeur_sortie(int ville);

/** Éditeur et générateur de cartes : points du joueur 2 et suivants (ville >= 1). */
void brutus_carte_versus_definir_entree(int ville, int x, int y);
void brutus_carte_versus_definir_sortie(int ville, int x, int y);

/** Nouvelle carte, chargement d'une carte ou d'une partie : plus de points à nous. */
void brutus_carte_versus_effacer(void);

/** Donne à chaque ville les points de son joueur (city_data.map), s'il en a. */
void brutus_carte_versus_appliquer_aux_villes(void);

// ---- fichier de carte (bloc « BR3C » à la fin)
void brutus_carte_versus_ecrire_fichier(FILE *fp);
void brutus_carte_versus_lire_fichier(FILE *fp);
void brutus_carte_versus_lire_tampon(buffer *buf);
/** Chargement d'une carte (pas d'une simple fiche) : les points lus deviennent ceux de la partie. */
void brutus_carte_versus_adopter_lu(void);

// ---- partie (dans le bloc Versus) : taille fixe
#define BRUTUS_CARTE_VERSUS_TAILLE 17
#include <stdint.h>
void brutus_carte_versus_ecrire(buffer *b);
void brutus_carte_versus_lire(buffer *b);

#endif // MULTIJOUEUR_CARTE_VERSUS_H
