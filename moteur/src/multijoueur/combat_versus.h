#ifndef MULTIJOUEUR_COMBAT_VERSUS_H
#define MULTIJOUEUR_COMBAT_VERSUS_H

/*
 * Brutus III (M10) : combat entre les villes du Versus (docs/versus.md, « Combat »).
 *
 * Quand le combat est activé, les militaires d'une ville (soldats des légions, sentinelles des tours,
 * balistes, archers des tours de guet) attaquent ceux de l'autre, au contact et à distance, avec les
 * règles qu'Augustus applique aux envahisseurs ; les soldats arrêtés près des bâtiments de l'autre ville
 * les abîment puis les détruisent, comme les envahisseurs.
 *
 * Les légions sont à la ville de leur fort : chaque ville ne recrute, ne compte, ne voit dans ses
 * listes et ne commande que les siennes.
 *
 * Hors Versus, tout est sans effet (une seule ville : rien ne change, au bit près).
 */

#include "figure/figure.h"
#include "figure/formation.h"

/** Option de la partie (sauvée dans le bloc BR3V) : 1 si le combat est activé. Vrai par défaut. */
int brutus_combat_option(void);
void brutus_combat_definir_option(int actif);
/** Commande `triche` 105 : 0 éteint, 1 allume, autre valeur : bascule. Retourne 1. */
int brutus_combat_triche(int valeur);
/**
 * Commande `triche` 106 (tests) : chaque fort de la ville du joueur reçoit tout de suite ses soldats,
 * qui apparaissent au fort (sans caserne ni armes). Retourne 1.
 */
int brutus_combat_triche_soldats(void);
/** Touche Alt+G (Versus local) : bascule le combat (commande). */
void brutus_combat_touche(void);

/** Vrai en Versus avec le combat activé. */
int brutus_combat_actif(void);

/** Ville d'une formation : celle du fort pour une légion, -1 pour les autres (envahisseurs, troupeaux). */
int brutus_versus_formation_proprietaire(const formation *m);
/** En Versus, vrai si c'est une légion d'une autre ville que la ville active (à sauter). */
int brutus_versus_formation_ignorer(const formation *m);

/** Vrai si ce personnage est une cible militaire pour la ville donnée (combat actif, autre ville). */
int brutus_combat_cible_pour(const figure *f, int ville);
/** Même chose pour la ville active (actions des personnages : la ville du personnage qui agit). */
int brutus_combat_cible(const figure *f);
/** Vrai si a et b sont des militaires de deux villes différentes (corps à corps). */
int brutus_combat_ennemis(const figure *a, const figure *b);
/** Nombre de cibles militaires vivantes pour cette ville (une légion en nettoyage les poursuit). */
int brutus_combat_nombre_cibles(int ville);

/**
 * Soldat arrêté à son étendard : s'il touche un bâtiment de l'autre ville, il l'abîme (comme un
 * envahisseur, tous les 4 ticks) et se tourne vers lui. Retourne 1 s'il frappe.
 */
int brutus_combat_attaquer_batiment(figure *f);

/**
 * Avertissement « La ville rouge attaque vos soldats / vos bâtiments ! », affiché seulement chez le joueur de
 * la ville attaquée, au plus toutes les 15 secondes pour chaque sorte. N'a aucun effet sur la partie.
 */
void brutus_combat_alerter(int victime, int attaquant, int batiments);

/** Bilan : un militaire est mort sous les coups (ou les tirs) d'un personnage (tir : le projectile). */
void brutus_combat_noter_mort(const figure *tueur, const figure *victime);

/**
 * Rejouage : une ligne par ville (« Brutus III : combat ville N : … crees … vivants … detruits … ») :
 * soldats créés par la triche 106, vivants, tués par l'autre ville, bâtiments de la ville détruits par l'autre.
 */
void brutus_combat_bilan(void);

#endif // MULTIJOUEUR_COMBAT_VERSUS_H
