#ifndef MULTIJOUEUR_RECAP_VERSUS_H
#define MULTIJOUEUR_RECAP_VERSUS_H

/*
 * Brutus III (M12) : récap d'une partie Versus (docs/versus.md, « Récap de fin de partie »).
 *
 * Pendant la partie, la simulation relève pour chaque ville, au passage en Versus, puis chaque mois et
 * au moment de la fin : population, trésor, prospérité, culture, score, soldats, bâtiments construits et
 * perdus (cumulés), soldats tués (cumulés, ceux de la ville tués par l'autre). Elle note aussi le premier
 * bâtiment détruit par l'autre ville.
 *
 * Ces relevés sont dans le bloc BR3V (version 7) et dans l'empreinte : tous les joueurs ont les mêmes,
 * et l'écran de fin (multijoueur/fin_versus.c) montre le même récap chez chacun.
 * Les relevés s'arrêtent à la fin de la partie. Hors Versus, tout est sans effet.
 */

#include <stdint.h>

/** Relevés gardés au plus (le premier, puis un par mois : 100 ans ; au-delà, on n'en ajoute plus). */
#define BRUTUS_RECAP_MAX 1202

typedef enum {
    BRUTUS_RECAP_POPULATION = 0,
    BRUTUS_RECAP_TRESOR,
    BRUTUS_RECAP_PROSPERITE,
    BRUTUS_RECAP_CULTURE,
    BRUTUS_RECAP_SCORE,
    BRUTUS_RECAP_SOLDATS,
    BRUTUS_RECAP_CONSTRUITS,
    BRUTUS_RECAP_PERDUS,
    BRUTUS_RECAP_TUES,
    BRUTUS_RECAP_STATISTIQUES
} brutus_recap_statistique;

/** Nombre de relevés. */
int brutus_recap_nombre(void);
/** Valeur d'une statistique pour une ville au relevé `i` (0 si hors limites). */
int32_t brutus_recap_valeur(int i, int ville, brutus_recap_statistique stat);
/** Date du relevé `i` : année (négative : av. J.-C.) et mois (0 à 11). */
int brutus_recap_annee(int i);
int brutus_recap_mois(int i);
/** Tick absolu du relevé `i`. */
uint32_t brutus_recap_tick(int i);
/** Vrai si le relevé `i` est celui de la fin de la partie. */
int brutus_recap_est_fin(int i);

/** Premier bâtiment détruit par l'autre ville : ville (-1 : aucun), type, tick, date. */
int brutus_recap_premier_detruit_ville(void);
int brutus_recap_premier_detruit_type(void);
uint32_t brutus_recap_premier_detruit_tick(void);
int brutus_recap_premier_detruit_annee(void);
int brutus_recap_premier_detruit_mois(void);

// ---- simulation

/** Passage en Versus, chaque mois, fin de la partie : un relevé pour chaque ville. */
void brutus_recap_relever(int fin);
/**
 * multijoueur/commande.c (construire) : bâtiments tout juste posés par la ville active (0 hors Versus).
 * Appelé avant et après la pose ; la différence est passée à brutus_recap_construits.
 */
int brutus_recap_compter_poses(void);
/** Une commande de construction vient de poser `n` bâtiments pour la ville active. */
void brutus_recap_construits(int n);
struct building;
/**
 * building/destruction.c : ce bâtiment va être perdu (incendie, effondrement, tremblement de terre, peste,
 * émeute, ennemi). Compté pour sa ville s'il était en service ; une partie d'un grand bâtiment ne compte pas.
 */
void brutus_recap_perdu(const struct building *b);
/** Un militaire de la ville `victime` a été tué par l'autre ville. */
void brutus_recap_soldat_tue(int victime);
/** Un bâtiment (type) de la ville `victime` a été détruit par les soldats de l'autre ville. */
void brutus_recap_detruit_par_ennemi(int victime, int type);

// ---- sauvegarde (bloc BR3V, version 7), à la suite du bloc de la victoire

void brutus_recap_remettre(void);
/** Taille en octets de ce qu'écrit brutus_recap_ecrire. */
uint32_t brutus_recap_taille(void);
void brutus_recap_ecrire(uint8_t *d);
/** Retourne 1 si la lecture est valide (`taille` octets disponibles). */
int brutus_recap_lire(const uint8_t *d, uint32_t taille, uint32_t *lus);

/** Empreinte du récap (32 bits), pour comparer celui de deux joueurs (tests). */
uint32_t brutus_recap_empreinte32(void);

/** Rejouage : « Brutus III : recap : … ». */
void brutus_recap_bilan(void);

#endif // MULTIJOUEUR_RECAP_VERSUS_H
