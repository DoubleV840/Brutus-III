#ifndef MULTIJOUEUR_VERSUS_H
#define MULTIJOUEUR_VERSUS_H

/*
 * Brutus III (M8) : deux villes sur la même carte (mode Versus), d'abord en local.
 *
 * Principe (docs/versus.md) :
 *  - chaque bâtiment et chaque personnage a un propriétaire (numéro de ville, 0 par défaut) ;
 *  - les données de ville d'Augustus (`city_data`, city/data_private.h) existent une fois par ville :
 *    `city_data` désigne la ville ACTIVE, que la simulation change pour faire travailler chaque ville
 *    à son tour ; hors simulation, c'est la ville VUE par le joueur local (interface) ;
 *  - le terrain, les routes, les personnages et la carte restent communs.
 *
 * Tant que la partie n'a qu'une ville (solo, Coop), rien ne change : ville active = 0, propriétaires 0,
 * aucune passe en plus, aucun octet de plus dans la sauvegarde ni dans l'empreinte.
 * Une partie passe en Versus par la commande `triche` 103 (local) : la ville 1 démarre vide, avec
 * le trésor de départ du scénario. Le joueur n joue pour la ville n.
 */

#include "core/buffer.h"

#include <stdint.h>
#include <stdio.h>

#define BRUTUS_VILLES_MAX 2

/** Nombre de villes de la partie (1, ou 2 en Versus). */
int brutus_versus_villes(void);
int brutus_versus_actif(void);

/** Passe la partie en Versus (deux villes). Retourne 0 si c'est déjà le cas. Commande `triche` 103. */
int brutus_versus_activer(void);

/** Ville dont `city_data` contient les données en ce moment. */
int brutus_ville_active(void);
/** Change la ville active (simulation). Sans effet hors Versus (toujours 0). */
void brutus_ville_activer(int ville);

/**
 * Fin d'une passe de ville : ville 0 active, et ce qui se fait ensuite est commun à toutes les villes
 * (événements du scénario, invasions…). Voir brutus_versus_ville_concernee().
 */
void brutus_ville_activer_commune(void);
/**
 * Ville concernée par ce qui se passe en ce moment (message, avertissement) : la ville active pendant
 * une passe de ville, un personnage, un bâtiment ou une commande ; -1 si c'est commun (ou hors Versus).
 */
int brutus_versus_ville_concernee(void);

/** Ville du joueur local (interface, hors simulation). */
int brutus_ville_vue(void);
/** Change la ville vue (touche de test du Versus local : on joue pour l'autre ville). */
void brutus_ville_vue_changer(int ville);

/** multijoueur/simulation.c : entrée et sortie de la simulation (ville 0 dedans, ville vue dehors). */
void brutus_versus_entrer_simulation(void);
void brutus_versus_sortir_simulation(void);

/** Ville du joueur qui envoie une commande (Versus : joueur n = ville n ; sinon 0). */
int brutus_versus_ville_du_joueur(int joueur);

/** Propriétaire donné aux bâtiments et personnages créés en ce moment (= la ville active). */
int brutus_versus_proprietaire_nouveau(void);

/**
 * Vrai si, en Versus, ce propriétaire n'est pas la ville active : les boucles d'une ville sautent les
 * bâtiments et personnages des autres (`if (brutus_versus_ignorer(b->brutus_proprietaire)) continue;`).
 */
int brutus_versus_ignorer(int proprietaire);

/**
 * Touche Alt+W (Versus local, en ville) : passe la partie en Versus (commande), puis à chaque appui
 * fait jouer le joueur local pour l'autre ville. Refusé en partie réseau (M12).
 */
void brutus_versus_touche(void);

/** Couleur d'un propriétaire (affichage). */
uint32_t brutus_versus_couleur(int ville);
/**
 * Affichage (vue de la ville, mini-carte) : vrai si ce bâtiment ou ce personnage est à une autre
 * ville que celle du joueur local (toujours faux hors Versus).
 */
int brutus_versus_a_autre_ville(int proprietaire);
/** Masque de couleur des bâtiments d'une autre ville dans la vue de la ville (teinte de leur couleur). */
uint32_t brutus_versus_teinte(int ville);

// ---- sauvegarde, chargement, empreinte (bloc « BR3V » après l'en-tête ; absent avec une seule ville)

void brutus_versus_ecrire_fichier(FILE *fp);
void brutus_versus_lire_fichier(FILE *fp);
void brutus_versus_lire_tampon(buffer *buf);
/** Après la lecture de l'état : applique le bloc lu (propriétaires, villes en plus), ou une seule ville. */
void brutus_versus_appliquer_lu(void);
/** Nouvelle partie (carte, campagne) : une seule ville. */
void brutus_versus_partie_neuve(void);
uint64_t brutus_versus_empreinte(uint64_t h);

#endif // MULTIJOUEUR_VERSUS_H
