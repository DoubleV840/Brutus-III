#ifndef MULTIJOUEUR_MENU_H
#define MULTIJOUEUR_MENU_H

/*
 * Brutus III : fenêtre « Multijoueur » dans le jeu (menu principal et menu Fichier).
 *
 * La fenêtre ne parle pas au réseau. Le client JavaScript (web/lockstep.js, piloté par la page)
 * lui pousse son état (phase, code, joueurs, latences, chat, avis) et vient chercher, à chaque
 * pompe, l'action demandée par le joueur (créer, rejoindre, quitter, revenir, chat, retirer, voir).
 * Sans client (version native, ou relais pas encore en ligne), la fenêtre explique où jouer.
 * Rien ici n'entre dans la simulation, la sauvegarde ou l'empreinte.
 */

#include "multijoueur/reseau.h"

/** Phases du client (web/lockstep.js, etat().phase), dans cet ordre. */
typedef enum {
    BRUTUS_MENU_INACTIF = 0,
    BRUTUS_MENU_CONNEXION,
    BRUTUS_MENU_SALON,
    BRUTUS_MENU_RESYNC,
    BRUTUS_MENU_JEU,
    BRUTUS_MENU_ATTENTE,
    BRUTUS_MENU_DECONNECTE,
    BRUTUS_MENU_RECUPERATION,
    BRUTUS_MENU_HORS_VILLE,
    BRUTUS_MENU_ERREUR
} brutus_menu_phase;

/** Actions demandées au client par la fenêtre. */
typedef enum {
    BRUTUS_MENU_AUCUNE = 0,
    BRUTUS_MENU_CREER,
    BRUTUS_MENU_REJOINDRE, // texte : le code
    BRUTUS_MENU_QUITTER,
    BRUTUS_MENU_REVENIR,
    BRUTUS_MENU_CHAT,      // texte : le message (UTF-8)
    BRUTUS_MENU_EXPULSER,  // paramètre : le joueur
    BRUTUS_MENU_VOIR       // paramètre : la case (grid offset) d'un signal
} brutus_menu_action_type;

// ---- poussé par la page (JS)

/** Le client réseau est là (1) ou non (0 : version native, ou relais pas encore en ligne). */
BRUTUS_RESEAU_EXPORT void brutus_menu_disponible(int oui);
/**
 * État du client. Les textes sont en UTF-8 (chaîne vide si rien). `hote` : 1 si le joueur local est l'hôte.
 */
BRUTUS_RESEAU_EXPORT void brutus_menu_etat(int phase, const char *code, const char *texte, const char *avis,
    int joueur_local, int hote);
/** Vide la liste des joueurs du salon. */
BRUTUS_RESEAU_EXPORT void brutus_menu_joueurs_vider(void);
/** M12 : ligne d'information de la partie, donnée par l'hôte et transmise par le relais (mode, carte, options). */
BRUTUS_RESEAU_EXPORT void brutus_menu_info(const char *info);
/** M12 : pseudo d'un joueur du salon (UTF-8, chaîne vide si aucun), après brutus_menu_joueur. */
BRUTUS_RESEAU_EXPORT void brutus_menu_joueur_pseudo(int numero, const char *pseudo);
/** M12 : pseudo du joueur local, retenu par la page (mis dans le champ « Pseudo »). */
BRUTUS_RESEAU_EXPORT void brutus_menu_pseudo_definir(const char *pseudo);
/** Ajoute un joueur du salon (latence en ms, -1 si inconnue). */
BRUTUS_RESEAU_EXPORT void brutus_menu_joueur(int numero, int connecte, int latence);
/** Ajoute une ligne de chat (identifiant croissant ; signal = case ou -1). Vide le chat si id = 0. */
BRUTUS_RESEAU_EXPORT void brutus_menu_chat(int id, int joueur, const char *texte, int signal);

// ---- lu par la page (JS)

/** Action en attente (brutus_menu_action_type), retirée de la file. */
BRUTUS_RESEAU_EXPORT int brutus_menu_action(void);
/** M12 : pseudo saisi par le joueur local (UTF-8), envoyé au relais en créant ou en rejoignant. */
BRUTUS_RESEAU_EXPORT const char *brutus_menu_pseudo(void);
/** M12 : nom d'un joueur (UTF-8) : son pseudo, sinon « Joueur N ». */
BRUTUS_RESEAU_EXPORT const char *brutus_menu_nom(int joueur);
/** M12 : pseudo du joueur d'une ville en Versus réseau (UTF-8), ou 0 (Versus local, pas de pseudo). */
const char *brutus_versus_pseudo_ville(int ville);
/** Texte de la dernière action lue (UTF-8). */
BRUTUS_RESEAU_EXPORT const char *brutus_menu_action_texte(void);
/** Paramètre de la dernière action lue. */
BRUTUS_RESEAU_EXPORT int brutus_menu_action_parametre(void);
/** 1 si le clavier est pris par un champ de texte du jeu (la touche G ne doit pas poser de signal). */
BRUTUS_RESEAU_EXPORT int brutus_menu_saisie_en_cours(void);

/**
 * M12 (tests, et ce que fait le menu) : choix de l'hôte pour la prochaine partie créée : Versus (1) ou Coop (0),
 * combat (0 ou 1), durée en mois (0 : sans limite).
 */
BRUTUS_RESEAU_EXPORT void brutus_menu_definir_partie(int versus, int combat, int duree);
/**
 * Coop jusqu'à 4 (tests, et ce que fait le bouton « Places ») : nombre de joueurs du salon en Coop, borné à
 * 2…BRUTUS_JOUEURS_MAX. Le Versus reste à 2 (une ville par joueur).
 */
BRUTUS_RESEAU_EXPORT void brutus_menu_definir_places(int places);
/**
 * M12 (tests) : fait comme le bouton « Créer avec cette ville » : prépare la partie choisie et met l'action
 * CREER en file (son texte est la ligne d'information). Retourne le nombre de joueurs du salon.
 */
BRUTUS_RESEAU_EXPORT int brutus_menu_creer(void);
/**
 * M12 (tests) : état du Versus : 0 villes, 1 ville vue, 2 combat, 3 durée (mois), 4 fin, 5 gagnant, 6 tick de la
 * fin, 7 relevés du récap, 8 empreinte du récap, 9 et 10 bâtiments restants des villes 1 et 2.
 */
BRUTUS_RESEAU_EXPORT int brutus_menu_versus_etat(int quoi);

// ---- moteur

/**
 * M12 : appelé à chaque avance du lockstep (multijoueur/reseau.c) : chez l'hôte d'une partie créée en Versus,
 * envoie les commandes qui passent la partie en Versus avec ses options (une fois, renvoyées si elles se perdent).
 */
void brutus_menu_versus_lancer(void);
/** M12 : vrai si l'hôte va créer une partie Versus depuis le menu principal (cartes à deux en tête de liste). */
int brutus_menu_mode_versus_choisi(void);
#include "core/dir.h"
/** M12 : window/cck_selection.c : met les cartes à deux en tête de liste quand l'hôte crée un Versus. */
void brutus_menu_trier_cartes(dir_listing *cartes);

/** Ouvre la fenêtre. `depuis_ville` : 1 si une ville est à l'écran (menu Fichier). */
void window_brutus_multijoueur_show(int depuis_ville);
/** Vrai si la fenêtre est à l'écran, ouverte depuis la ville (la partie continue dessous). */
int brutus_menu_au_dessus_de_la_ville(void);
/** window/main_menu.c : le joueur est revenu au menu principal (abandonne « créer quand la ville sera prête »). */
void brutus_menu_retour_menu_principal(void);

#endif // MULTIJOUEUR_MENU_H
