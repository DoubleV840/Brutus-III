#ifndef MULTIJOUEUR_COMMANDE_H
#define MULTIJOUEUR_COMMANDE_H

/*
 * Brutus III : couche de commandes (phase 2a).
 *
 * Toute action du joueur qui modifie la partie devient une commande
 * { type, joueur, tick, paramètres }. Elle est mise en file puis appliquée
 * par la simulation avant le tick indiqué. Le solo passe par le même chemin.
 * Voir docs/commandes.md (inventaire et format du journal).
 */

#include <stdint.h>
#include <stdio.h>

#define BRUTUS_COMMANDE_MAX_PARAMS 10

typedef enum {
    BRUTUS_CMD_AUCUNE = 0,
    BRUTUS_CMD_CONSTRUIRE,          // type x0 y0 x1 y1 rotation rotation_extra orientation_route orientation_vue
    BRUTUS_CMD_DEMOLIR_CONFIRME,    // x0 y0 x1 y1 quoi(1 fort, 2 pont, 3 monument) accepte(0/1)
    BRUTUS_CMD_ANNULER,             // (aucun) : annuler la dernière construction
    BRUTUS_CMD_ROTATION_VUE,        // sens (0 gauche, 1 droite, 2 nord)
    BRUTUS_CMD_IMPOTS,              // variation en points
    BRUTUS_CMD_SALAIRES,            // variation
    BRUTUS_CMD_PRIORITE_TRAVAIL,    // categorie priorite
    BRUTUS_CMD_STOCKAGE_ETAT,       // x y ressource sens_inverse(0/1) partiel(0/1)
    BRUTUS_CMD_STOCKAGE_TOUT,       // x y accepter(1)/refuser(0)
    BRUTUS_CMD_STOCKAGE_VIDER,      // x y
    BRUTUS_CMD_STOCKAGE_PERMISSION, // x y permission valeur(-1 bascule, 0, 1)
    BRUTUS_CMD_DISTRIBUTION_BIEN,   // x y ressource
    BRUTUS_CMD_DISTRIBUTION_TOUT,   // x y accepter(1)/refuser(0)
    BRUTUS_CMD_MISE_EN_SOMMEIL,     // x y
    BRUTUS_CMD_FETE,                // dieu taille
    BRUTUS_CMD_DON_CESAR,           // taille
    BRUTUS_CMD_SALAIRE_GOUVERNEUR,  // rang
    BRUTUS_CMD_COMMERCE_OUVRIR,     // ville
    BRUTUS_CMD_COMMERCE_STATUT,     // ressource statut
    BRUTUS_CMD_COMMERCE_SEUIL,      // ressource export(1)/import(0) variation
    BRUTUS_CMD_RESSOURCE_SOMMEIL,   // ressource
    BRUTUS_CMD_RESSOURCE_RESERVE,   // ressource
    BRUTUS_CMD_DEMANDE_CESAR,       // id
    BRUTUS_CMD_LEGION_DEPLACER,     // formation x y
    BRUTUS_CMD_LEGION_RENTRER,      // formation
    BRUTUS_CMD_LEGION_FORMATION,    // formation disposition
    BRUTUS_CMD_LEGION_SERVICE,      // formation
    BRUTUS_CMD_NOMBRE
} brutus_commande_type;

typedef struct {
    brutus_commande_type type;
    int joueur;
    uint32_t tick;
    int nb_params;
    int32_t p[BRUTUS_COMMANDE_MAX_PARAMS];
} brutus_commande;

/** Tick absolu de la partie (jours écoulés x 50 + tick du jour), lu dans l'état sauvegardé. */
uint32_t brutus_tick_absolu(void);

/**
 * Repère des caches internes de la simulation (remplace time_get_millis()) :
 * change au début de chaque tick et avant chaque commande. Jamais sauvegardé.
 */
uint32_t brutus_repere_simulation(void);

/** Début d'un tick de simulation (game_tick_run). */
void brutus_debut_tick(void);

/** Une partie vient d'être chargée ou démarrée : file vidée, compteurs remis à zéro. */
void brutus_partie_chargee(void);

/** Nom de la commande dans le journal (« construire »…), ou 0. */
const char *brutus_commande_nom(brutus_commande_type type);

/** Nombre de paramètres attendus pour ce type. */
int brutus_commande_nb_params(brutus_commande_type type);

/**
 * Point d'entrée de l'interface : met la commande en file pour le prochain tick
 * (joueur local). Les paramètres manquants valent 0.
 */
void brutus_commande_envoyer(brutus_commande_type type, int nb_params, const int32_t *params);

/** Raccourcis */
void brutus_commande_0(brutus_commande_type type);
void brutus_commande_1(brutus_commande_type type, int32_t a);
void brutus_commande_2(brutus_commande_type type, int32_t a, int32_t b);
void brutus_commande_3(brutus_commande_type type, int32_t a, int32_t b, int32_t c);
void brutus_commande_4(brutus_commande_type type, int32_t a, int32_t b, int32_t c, int32_t d);
void brutus_commande_5(brutus_commande_type type, int32_t a, int32_t b, int32_t c, int32_t d, int32_t e);

/** Met en file une commande déjà datée (rejouage, réseau plus tard). */
int brutus_commande_ajouter(const brutus_commande *cmd);

/**
 * Applique, dans l'ordre (tick, joueur, ordre d'arrivée), toutes les commandes
 * en file dont le tick est <= `tick`. Appelé avant chaque tick de simulation.
 * Retourne le nombre de commandes appliquées.
 */
int brutus_commandes_appliquer(uint32_t tick);

/** Vide la file (chargement d'une partie). */
void brutus_commandes_vider(void);

/** Nombre de commandes appliquées depuis le lancement. */
unsigned int brutus_commandes_total_appliquees(void);

/** Construction depuis l'interface (fin du glisser) : remplace building_construction_place(). */
void brutus_commande_construire_depuis_interface(void);

/** Écrit / lit une commande au format du journal (une ligne). */
void brutus_commande_ecrire(FILE *fp, const brutus_commande *cmd);
int brutus_commande_lire_ligne(const char *ligne, brutus_commande *cmd);

/** Enregistrement facultatif des commandes appliquées dans un journal (0 pour arrêter). */
void brutus_journal_enregistrer(FILE *fp);

#endif // MULTIJOUEUR_COMMANDE_H
