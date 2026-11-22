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
/** Ajoute un joueur du salon (latence en ms, -1 si inconnue). */
BRUTUS_RESEAU_EXPORT void brutus_menu_joueur(int numero, int connecte, int latence);
/** Ajoute une ligne de chat (identifiant croissant ; signal = case ou -1). Vide le chat si id = 0. */
BRUTUS_RESEAU_EXPORT void brutus_menu_chat(int id, int joueur, const char *texte, int signal);

// ---- lu par la page (JS)

/** Action en attente (brutus_menu_action_type), retirée de la file. */
BRUTUS_RESEAU_EXPORT int brutus_menu_action(void);
/** Texte de la dernière action lue (UTF-8). */
BRUTUS_RESEAU_EXPORT const char *brutus_menu_action_texte(void);
/** Paramètre de la dernière action lue. */
BRUTUS_RESEAU_EXPORT int brutus_menu_action_parametre(void);
/** 1 si le clavier est pris par un champ de texte du jeu (la touche G ne doit pas poser de signal). */
BRUTUS_RESEAU_EXPORT int brutus_menu_saisie_en_cours(void);

// ---- moteur

/** Ouvre la fenêtre. `depuis_ville` : 1 si une ville est à l'écran (menu Fichier). */
void window_brutus_multijoueur_show(int depuis_ville);
/** Vrai si la fenêtre est à l'écran, ouverte depuis la ville (la partie continue dessous). */
int brutus_menu_au_dessus_de_la_ville(void);
/** window/main_menu.c : le joueur est revenu au menu principal (abandonne « créer quand la ville sera prête »). */
void brutus_menu_retour_menu_principal(void);

#endif // MULTIJOUEUR_MENU_H
