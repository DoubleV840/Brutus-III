// Brutus III : pont entre le moteur et le client réseau JavaScript (voir reseau.h)
#include "multijoueur/reseau.h"

#include "core/log.h"
#include "game/file.h"
#include "game/settings.h"
#include "game/state.h"
#include "graphics/window.h"
#include "multijoueur/commande.h"
#include "multijoueur/empreinte.h"
#include "multijoueur/simulation.h"
#include "window/city.h"

#include <string.h>

#define SORTIE_MAX 32768

static struct {
    int actif;
    int pause_partagee;
    int demande_vitesse;
    int demande_pause;
    uint64_t derniere_empreinte;
    int taille_sortie;
    uint8_t sortie[SORTIE_MAX];
    uint8_t tampon[BRUTUS_RESEAU_TAMPON];
} etat;

int brutus_reseau_actif(void)
{
    return etat.actif;
}

BRUTUS_RESEAU_EXPORT uint8_t *brutus_reseau_tampon(void)
{
    return etat.tampon;
}

BRUTUS_RESEAU_EXPORT int brutus_reseau_activer(int joueur)
{
    if (joueur < 0 || joueur >= BRUTUS_JOUEURS_MAX) {
        return 0;
    }
    etat.actif = 1;
    etat.taille_sortie = 0;
    etat.demande_vitesse = 0;
    etat.demande_pause = 0;
    brutus_multijoueur_definir(1);
    brutus_joueur_local_definir(joueur);
    return 1;
}

BRUTUS_RESEAU_EXPORT void brutus_reseau_desactiver(void)
{
    etat.actif = 0;
    etat.taille_sortie = 0;
    etat.pause_partagee = 0;
    brutus_multijoueur_definir(0);
    brutus_joueur_local_definir(0);
    game_state_unpause();
}

int brutus_reseau_deposer(int type, int nb_params, const int32_t *params)
{
    int attendus = brutus_commande_nb_params((brutus_commande_type) type);
    if (!brutus_commande_nom((brutus_commande_type) type) || 2 + attendus * 4 > SORTIE_MAX - etat.taille_sortie) {
        log_error("Brutus III : commande de l'interface perdue (boîte de sortie pleine)", 0, 0);
        return 0;
    }
    uint8_t *p = etat.sortie + etat.taille_sortie;
    p[0] = (uint8_t) type;
    p[1] = (uint8_t) attendus;
    p += 2;
    for (int i = 0; i < attendus; i++) {
        int32_t v = i < nb_params ? params[i] : 0; // paramètres manquants : 0, comme dans la file locale
        uint32_t u = (uint32_t) v;
        p[0] = (uint8_t) (u & 0xff);
        p[1] = (uint8_t) ((u >> 8) & 0xff);
        p[2] = (uint8_t) ((u >> 16) & 0xff);
        p[3] = (uint8_t) ((u >> 24) & 0xff);
        p += 4;
    }
    etat.taille_sortie = (int) (p - etat.sortie);
    return 1;
}

void brutus_reseau_demander_vitesse(int delta)
{
    if (delta > 0) {
        etat.demande_vitesse++;
    } else if (delta < 0) {
        etat.demande_vitesse--;
    }
}

void brutus_reseau_demander_pause(void)
{
    etat.demande_pause++;
}

BRUTUS_RESEAU_EXPORT int brutus_reseau_sortie(void)
{
    int taille = etat.taille_sortie;
    if (taille > BRUTUS_RESEAU_TAMPON) {
        taille = BRUTUS_RESEAU_TAMPON;
    }
    memcpy(etat.tampon, etat.sortie, (size_t) taille);
    etat.taille_sortie = 0;
    return taille;
}

static int lire_i32(const uint8_t *p, int32_t *v)
{
    uint32_t u = (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
    *v = (int32_t) u;
    return 1;
}

BRUTUS_RESEAU_EXPORT int brutus_reseau_entree(int joueur, uint32_t tick, int taille)
{
    if (joueur < 0 || joueur >= BRUTUS_JOUEURS_MAX || taille < 0 || taille > BRUTUS_RESEAU_TAMPON) {
        return 0;
    }
    const uint8_t *d = etat.tampon;
    // 1re passe : tout le tour est valide, ou il est rejeté en entier (jamais de lecture hors du tampon).
    int pos = 0;
    while (pos < taille) {
        if (pos + 2 > taille) {
            return 0;
        }
        brutus_commande_type type = (brutus_commande_type) d[pos];
        int nb = d[pos + 1];
        if (!brutus_commande_nom(type) || nb != brutus_commande_nb_params(type) ||
            pos + 2 + nb * 4 > taille) {
            return 0;
        }
        pos += 2 + nb * 4;
    }
    // 2e passe : mise en file (joueur et tick de l'en-tête du message).
    pos = 0;
    while (pos < taille) {
        brutus_commande cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = (brutus_commande_type) d[pos];
        cmd.nb_params = d[pos + 1];
        cmd.joueur = joueur;
        cmd.tick = tick;
        for (int i = 0; i < cmd.nb_params; i++) {
            lire_i32(d + pos + 2 + i * 4, &cmd.p[i]);
        }
        brutus_commande_ajouter(&cmd);
        pos += 2 + cmd.nb_params * 4;
    }
    return 1;
}

static void appliquer_pause_partagee(void)
{
    // La pause de l'interface et celle de maintenance.c sont locales : seule la pause partagée compte.
    if (etat.pause_partagee) {
        game_state_pause();
    } else {
        game_state_unpause();
    }
}

BRUTUS_RESEAU_EXPORT int brutus_reseau_avancer(int n)
{
    int faits = brutus_avancer_ticks(n);
    appliquer_pause_partagee();
    return faits;
}

BRUTUS_RESEAU_EXPORT uint32_t brutus_reseau_tick(void)
{
    return brutus_tick_absolu();
}

BRUTUS_RESEAU_EXPORT uint32_t brutus_reseau_empreinte(void)
{
    etat.derniere_empreinte = brutus_empreinte_partie();
    return (uint32_t) (etat.derniere_empreinte & 0xffffffffu);
}

BRUTUS_RESEAU_EXPORT uint32_t brutus_reseau_empreinte_haute(void)
{
    return (uint32_t) (etat.derniere_empreinte >> 32);
}

BRUTUS_RESEAU_EXPORT int brutus_reseau_sauvegarder(const char *chemin)
{
    // La file est vide aux frontières de groupe (JS n'y met les commandes qu'au moment de jouer).
    brutus_simulation_debut();
    int ok = game_file_write_saved_game(chemin);
    brutus_simulation_fin();
    return ok ? 1 : 0;
}

BRUTUS_RESEAU_EXPORT int brutus_reseau_charger(const char *chemin)
{
    if (!brutus_resynchroniser(chemin)) {
        return 0;
    }
    appliquer_pause_partagee();
    window_city_show();
    return 1;
}

BRUTUS_RESEAU_EXPORT int brutus_reseau_jeu_en_cours(void)
{
    window_id id = window_get_id();
    return id >= WINDOW_CITY && id <= WINDOW_TRADE_LEDGER;
}

BRUTUS_RESEAU_EXPORT int brutus_reseau_demande_vitesse(void)
{
    int d = etat.demande_vitesse;
    etat.demande_vitesse = 0;
    return d;
}

BRUTUS_RESEAU_EXPORT int brutus_reseau_demande_pause(void)
{
    int d = etat.demande_pause;
    etat.demande_pause = 0;
    return d;
}

BRUTUS_RESEAU_EXPORT void brutus_reseau_vitesse_appliquer(int pourcent, int pause)
{
    setting_set_game_speed(pourcent);
    etat.pause_partagee = pause ? 1 : 0;
    appliquer_pause_partagee();
}

BRUTUS_RESEAU_EXPORT int brutus_reseau_vitesse(void)
{
    return setting_game_speed();
}

BRUTUS_RESEAU_EXPORT int brutus_reseau_en_pause(void)
{
    return etat.pause_partagee;
}

BRUTUS_RESEAU_EXPORT uint32_t brutus_reseau_commandes_appliquees(void)
{
    return brutus_commandes_total_appliquees();
}

BRUTUS_RESEAU_EXPORT uint32_t brutus_reseau_commandes_refusees(void)
{
    return brutus_commandes_total_refusees();
}

// Pour le bot de test : fait ce que font les touches de vitesse et de pause de l'interface.
BRUTUS_RESEAU_EXPORT void brutus_reseau_test_vitesse(int delta)
{
    brutus_reseau_demander_vitesse(delta);
}

BRUTUS_RESEAU_EXPORT void brutus_reseau_test_pause(void)
{
    brutus_reseau_demander_pause();
}

// Pour le bot de test (pas d'interface sous Node) : numéro d'une commande d'après son nom du journal.
BRUTUS_RESEAU_EXPORT int brutus_reseau_type_par_nom(const char *nom)
{
    for (int t = 1; t < BRUTUS_CMD_NOMBRE; t++) {
        const char *n = brutus_commande_nom((brutus_commande_type) t);
        if (n && strcmp(n, nom) == 0) {
            return t;
        }
    }
    return 0;
}

// Pour le bot de test : fait ce que fait l'interface (brutus_commande_envoyer). Les paramètres sont
// dans le tampon (nb x i32 petit-boutiste). La commande part dans la boîte de sortie.
BRUTUS_RESEAU_EXPORT int brutus_reseau_injecter(int type, int nb_params)
{
    if (nb_params < 0 || nb_params > BRUTUS_COMMANDE_MAX_PARAMS || !brutus_commande_nom((brutus_commande_type) type)) {
        return 0;
    }
    int32_t p[BRUTUS_COMMANDE_MAX_PARAMS] = { 0 };
    for (int i = 0; i < nb_params; i++) {
        lire_i32(etat.tampon + i * 4, &p[i]);
    }
    brutus_commande_envoyer((brutus_commande_type) type, nb_params, p);
    return 1;
}
