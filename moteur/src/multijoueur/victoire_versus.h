#ifndef MULTIJOUEUR_VICTOIRE_VERSUS_H
#define MULTIJOUEUR_VICTOIRE_VERSUS_H

/*
 * Brutus III (M11) : fin d'une partie Versus (docs/versus.md, « Victoire »).
 *
 *  - Victoire militaire : la ville dont le Sénat est détruit par les soldats de l'autre ville a perdu.
 *  - Victoire économique : à la fin de la durée choisie (en mois depuis le passage en Versus), la ville
 *    au meilleur score gagne (égalité possible) :
 *        score = population + trésor / 10 + 25 × (prospérité + culture).
 *  - La fin est décidée par la simulation (déterministe, dans l'empreinte et le bloc BR3V) ; l'écran de fin
 *    est ouvert par l'interface, hors simulation, chez chaque joueur (window/… : multijoueur/fin_versus.c).
 *  - En Versus, la victoire du scénario d'Augustus (city_victory_check) ne s'applique pas.
 *
 * Hors Versus, tout est sans effet.
 */

#include <stdint.h>

#define BRUTUS_VICTOIRE_DUREE_DEFAUT 120 // 10 ans
#define BRUTUS_VICTOIRE_TAILLE 50 // octets dans le bloc BR3V (version 6)

enum {
    BRUTUS_FIN_AUCUNE = 0,
    BRUTUS_FIN_MILITAIRE = 1,
    BRUTUS_FIN_ECONOMIQUE = 2
};

typedef struct {
    int32_t population;
    int32_t tresor;
    int32_t prosperite;
    int32_t culture;
    int32_t score;
} brutus_score_ville;

/** Durée de la partie, en mois depuis le passage en Versus (0 : sans limite). */
int brutus_victoire_duree(void);
void brutus_victoire_definir_duree(int mois);
/** Mois écoulés depuis le passage en Versus (s'arrête à la fin de la partie). */
int brutus_victoire_mois_ecoules(void);
/**
 * Commande `triche` 107 : valeur >= 0 : durée en mois ; valeur < 0 : durée suivante
 * (5 ans, 10 ans, 20 ans, sans limite). Retourne 1 (0 si la partie est finie).
 */
int brutus_victoire_triche_duree(int valeur);
/** Touche Alt+T (Versus local) : durée suivante (commande). */
void brutus_victoire_touche(void);

/** Score d'une ville en ce moment (données de la ville, sans changer la ville active). */
brutus_score_ville brutus_victoire_score(int ville);

/** Simulation, chaque mois (game/tick.c, après les notes) : fin de la durée → victoire économique. */
void brutus_victoire_nouveau_mois(void);
/** Combat : le Sénat de la ville `victime` vient d'être détruit par un soldat de la ville `attaquant`. */
void brutus_victoire_senat_detruit(int victime, int attaquant);

/** Fin de la partie : BRUTUS_FIN_AUCUNE tant qu'elle n'est pas décidée. */
int brutus_victoire_fin(void);
/** Ville gagnante (-1 : égalité ; sans objet sans fin). */
int brutus_victoire_gagnant(void);
/** Tick absolu de la fin. */
uint32_t brutus_victoire_tick_fin(void);
/** Scores des villes au moment de la fin. */
const brutus_score_ville *brutus_victoire_scores_fin(int ville);

/** Nouvelle partie ou retour à une ville : options par défaut, aucune fin. */
void brutus_victoire_remettre(void);
/** Bloc BR3V (version 6) : BRUTUS_VICTOIRE_TAILLE octets. */
void brutus_victoire_ecrire(uint8_t *d);
void brutus_victoire_lire(const uint8_t *d);

/** Rejouage : « Brutus III : victoire : … ». */
void brutus_victoire_bilan(void);

#endif // MULTIJOUEUR_VICTOIRE_VERSUS_H
