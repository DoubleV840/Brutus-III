#ifndef MULTIJOUEUR_RESEAU_H
#define MULTIJOUEUR_RESEAU_H

/*
 * Brutus III : pont entre le moteur et le client réseau JavaScript (M6b, web/lockstep.js).
 *
 * Le lockstep (qui attend quels tours, quand avancer) est écrit en JavaScript. Le moteur
 * n'expose que des fonctions simples, appelées par ccall :
 *  - les commandes de l'interface ne vont plus dans la file locale : elles sont déposées dans une
 *    boîte de sortie, que JS vide et envoie en TOUR (elles reviennent par le relais, comme celles
 *    des autres, puis entrent dans la file par brutus_reseau_entree) ;
 *  - JS fait avancer la simulation par groupes de ticks (brutus_reseau_avancer) ;
 *  - vitesse et pause sont partagées : l'interface ne les change plus, elle enregistre une demande ;
 *  - sauvegarde et rechargement (resynchronisation) passent par un fichier.
 *
 * Les données échangées passent par un tampon statique de BRUTUS_RESEAU_TAMPON octets
 * (brutus_reseau_tampon()), sans malloc côté JS. Voir docs/protocole.md.
 */

#include <stdint.h>

#define BRUTUS_RESEAU_TAMPON 65536

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define BRUTUS_RESEAU_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define BRUTUS_RESEAU_EXPORT
#endif

/** Vrai si le client réseau est actif (les commandes de l'interface partent sur le réseau). */
int brutus_reseau_actif(void);

/** Boîte de sortie : dépose une commande de l'interface (joueur local). Retourne 1 si acceptée. */
int brutus_reseau_deposer(int type, int nb_params, const int32_t *params);

/** Interface : demande de vitesse (+1 / -1) ou de pause (bascule), enregistrée pour JS. */
void brutus_reseau_demander_vitesse(int delta);
void brutus_reseau_demander_pause(void);

BRUTUS_RESEAU_EXPORT uint8_t *brutus_reseau_tampon(void);
BRUTUS_RESEAU_EXPORT int brutus_reseau_activer(int joueur);
BRUTUS_RESEAU_EXPORT void brutus_reseau_desactiver(void);
/** Vide la boîte de sortie dans le tampon (u8 type, u8 nb, nb x i32 par commande). Retourne la taille. */
BRUTUS_RESEAU_EXPORT int brutus_reseau_sortie(void);
/** Ajoute les commandes du tampon (`taille` octets) à la file, datées de `tick`. 0 si le tour est invalide. */
BRUTUS_RESEAU_EXPORT int brutus_reseau_entree(int joueur, uint32_t tick, int taille);
/** Avance de `n` ticks, puis réapplique la pause partagée. Retourne le nombre de ticks faits. */
BRUTUS_RESEAU_EXPORT int brutus_reseau_avancer(int n);
BRUTUS_RESEAU_EXPORT uint32_t brutus_reseau_tick(void);
/** Calcule l'empreinte ; retourne les 32 bits bas, brutus_reseau_empreinte_haute() les 32 bits hauts. */
BRUTUS_RESEAU_EXPORT uint32_t brutus_reseau_empreinte(void);
BRUTUS_RESEAU_EXPORT uint32_t brutus_reseau_empreinte_haute(void);
BRUTUS_RESEAU_EXPORT int brutus_reseau_sauvegarder(const char *chemin);
BRUTUS_RESEAU_EXPORT int brutus_reseau_charger(const char *chemin);
/** Vrai si une partie est en cours (fenêtre de la ville ou de ses dialogues). */
BRUTUS_RESEAU_EXPORT int brutus_reseau_jeu_en_cours(void);
BRUTUS_RESEAU_EXPORT int brutus_reseau_demande_vitesse(void);
BRUTUS_RESEAU_EXPORT int brutus_reseau_demande_pause(void);
BRUTUS_RESEAU_EXPORT void brutus_reseau_vitesse_appliquer(int pourcent, int pause);
BRUTUS_RESEAU_EXPORT int brutus_reseau_vitesse(void);
BRUTUS_RESEAU_EXPORT int brutus_reseau_en_pause(void);
BRUTUS_RESEAU_EXPORT uint32_t brutus_reseau_commandes_appliquees(void);
BRUTUS_RESEAU_EXPORT uint32_t brutus_reseau_commandes_refusees(void);
/** Tests : numéro d'une commande d'après son nom du journal (0 si inconnue). */
BRUTUS_RESEAU_EXPORT int brutus_reseau_type_par_nom(const char *nom);
/** Tests : agit comme l'interface (paramètres dans le tampon, nb x i32). La commande part sur le réseau. */
BRUTUS_RESEAU_EXPORT int brutus_reseau_injecter(int type, int nb_params);
/** Tests : agit comme les touches de vitesse (delta +1 ou -1) et de pause de l'interface. */
BRUTUS_RESEAU_EXPORT void brutus_reseau_test_vitesse(int delta);
BRUTUS_RESEAU_EXPORT void brutus_reseau_test_pause(void);

#endif // MULTIJOUEUR_RESEAU_H
