// Brutus III (M8) : deux villes sur la même carte (voir versus.h et docs/versus.md).
#include "multijoueur/versus.h"

#include "building/building.h"
#include "city/data.h"
#include "city/data_private.h"
#include "city/message.h"
#include "core/log.h"
#include "figure/figure.h"
#include "multijoueur/empreinte.h"
#include "multijoueur/simulation.h"
#include "scenario/request.h"
#include "widget/minimap.h"

#include <stdlib.h>
#include <string.h>

#define VERSION_BLOC 3 // 2 : ville de chaque message ; 3 : demandes de César de chaque ville
#define TAILLE_VILLE_MAX 65536

static const char MARQUE[4] = { 'B', 'R', '3', 'V' };

static struct {
    int villes;
    int active;
    int vue;
    int commune; // vrai entre deux passes de ville : ce qui arrive concerne toutes les villes
    struct {
        int lu;
        uint8_t *octets;
        uint32_t taille;
    } bloc;
} etat = { 1, 0, 0, 0, { 0, 0, 0 } };

// ------------------------------------------------------------------ villes

int brutus_versus_villes(void)
{
    return etat.villes;
}

int brutus_versus_actif(void)
{
    return etat.villes > 1;
}

int brutus_ville_active(void)
{
    return etat.active;
}

void brutus_ville_activer(int ville)
{
    if (ville < 0 || ville >= etat.villes) {
        ville = 0;
    }
    etat.active = ville;
    etat.commune = 0;
    brutus_ville_courante = &brutus_villes_donnees[ville];
}

void brutus_ville_activer_commune(void)
{
    brutus_ville_activer(0);
    etat.commune = 1;
}

int brutus_versus_ville_concernee(void)
{
    return etat.villes > 1 && !etat.commune ? etat.active : -1;
}

int brutus_ville_vue(void)
{
    return etat.vue;
}

void brutus_ville_vue_changer(int ville)
{
    if (ville < 0 || ville >= etat.villes) {
        return;
    }
    etat.vue = ville;
    if (!brutus_simulation_en_cours()) {
        brutus_ville_activer(ville);
    }
    widget_minimap_invalidate(); // les couleurs des propriétaires changent
}

void brutus_versus_entrer_simulation(void)
{
    brutus_ville_activer_commune();
}

void brutus_versus_sortir_simulation(void)
{
    brutus_ville_activer(etat.vue);
}

int brutus_versus_ville_du_joueur(int joueur)
{
    return etat.villes > 1 && joueur >= 0 && joueur < etat.villes ? joueur : 0;
}

int brutus_versus_proprietaire_nouveau(void)
{
    return etat.active;
}

int brutus_versus_ignorer(int proprietaire)
{
    return etat.villes > 1 && proprietaire != etat.active;
}

uint32_t brutus_versus_couleur(int ville)
{
    static const uint32_t couleurs[BRUTUS_VILLES_MAX] = { 0xff3399ff, 0xffdd2222 };
    return ville >= 0 && ville < BRUTUS_VILLES_MAX ? couleurs[ville] : 0xffffffff;
}

int brutus_versus_a_autre_ville(int proprietaire)
{
    return etat.villes > 1 && proprietaire != etat.vue;
}

uint32_t brutus_versus_teinte(int ville)
{
    // Masque multiplicatif, assez clair pour garder le dessin lisible
    static const uint32_t teintes[BRUTUS_VILLES_MAX] = { 0xff8cc0ff, 0xffff9c8c };
    return ville >= 0 && ville < BRUTUS_VILLES_MAX ? teintes[ville] : 0xffffffff;
}

static void remettre_une_ville(void)
{
    etat.villes = 1;
    etat.vue = 0;
    brutus_ville_activer(0);
    memset(&brutus_villes_donnees[1], 0, sizeof(brutus_villes_donnees[1]));
    scenario_request_brutus_oublier(1);
}

int brutus_versus_activer(void)
{
    if (etat.villes > 1) {
        return 0;
    }
    int avant = etat.active;
    etat.villes = 2;
    // La ville 1 démarre vide : données neuves, trésor de départ du scénario, mêmes points
    // d'entrée et de sortie que la ville 0 (M9 donnera un point de départ par joueur).
    brutus_ville_activer(1);
    city_data_init();
    city_data_init_scenario();
    brutus_villes_donnees[1].map = brutus_villes_donnees[0].map;
    scenario_request_brutus_copier(1); // mêmes demandes de César, mais chacune les siennes
    brutus_ville_activer(avant);
    log_info("Brutus III (M8) : partie en Versus, deux villes", 0, 0);
    return 1;
}

// ------------------------------------------------------------------ bloc de sauvegarde

static void ecrire_u32(uint8_t *d, uint32_t v)
{
    d[0] = (uint8_t) v;
    d[1] = (uint8_t) (v >> 8);
    d[2] = (uint8_t) (v >> 16);
    d[3] = (uint8_t) (v >> 24);
}

static uint32_t lire_u32(const uint8_t *d)
{
    return d[0] | (d[1] << 8) | (d[2] << 16) | ((uint32_t) d[3] << 24);
}

// Compose le bloc (sans la marque ni la taille). Retourne sa taille ; *sortie à libérer.
static uint32_t composer(uint8_t **sortie)
{
    unsigned int nb_batiments = (unsigned int) building_count();
    unsigned int nb_figures = figure_count();
    unsigned int nb_messages = (unsigned int) city_message_brutus_cases();
    uint32_t taille = 2 + 4 + nb_batiments + 4 + nb_figures + 4 + nb_messages;
    taille += (uint32_t) (etat.villes - 1) * (4 + TAILLE_VILLE_MAX);
    int active_avant = etat.active;
    for (int v = 1; v < etat.villes; v++) {
        brutus_ville_activer(v);
        taille += scenario_request_brutus_taille();
    }
    brutus_ville_activer(active_avant);
    uint8_t *d = calloc(1, taille);
    if (!d) {
        *sortie = 0;
        return 0;
    }
    uint32_t p = 0;
    d[p++] = VERSION_BLOC;
    d[p++] = (uint8_t) etat.villes;
    ecrire_u32(d + p, nb_batiments);
    p += 4;
    for (unsigned int i = 0; i < nb_batiments; i++) {
        d[p++] = building_get(i)->brutus_proprietaire;
    }
    ecrire_u32(d + p, nb_figures);
    p += 4;
    for (unsigned int i = 0; i < nb_figures; i++) {
        d[p++] = figure_get(i)->brutus_proprietaire;
    }
    ecrire_u32(d + p, nb_messages);
    p += 4;
    for (unsigned int i = 0; i < nb_messages; i++) {
        d[p++] = (uint8_t) (city_message_brutus_ville_case((int) i) + 1); // 0 : toutes les villes
    }
    int avant = etat.active;
    for (int v = 1; v < etat.villes; v++) {
        buffer b;
        buffer_init(&b, d + p + 4, TAILLE_VILLE_MAX);
        brutus_ville_activer(v);
        city_data_brutus_ecrire(&b);
        ecrire_u32(d + p, (uint32_t) b.index);
        p += 4 + (uint32_t) b.index;
        buffer_init(&b, d + p, scenario_request_brutus_taille());
        scenario_request_brutus_ecrire(&b);
        p += (uint32_t) b.index;
    }
    brutus_ville_activer(avant);
    *sortie = d;
    return p;
}

void brutus_versus_ecrire_fichier(FILE *fp)
{
    if (etat.villes <= 1) {
        return; // solo et Coop : le fichier ne change pas
    }
    uint8_t *d;
    uint32_t taille = composer(&d);
    if (!d) {
        return;
    }
    uint8_t taille_le[4];
    ecrire_u32(taille_le, taille);
    fwrite(MARQUE, 1, sizeof(MARQUE), fp);
    fwrite(taille_le, 1, 4, fp);
    fwrite(d, 1, taille, fp);
    free(d);
}

static void oublier_bloc(void)
{
    free(etat.bloc.octets);
    etat.bloc.octets = 0;
    etat.bloc.taille = 0;
    etat.bloc.lu = 0;
}

static void garder_bloc(const uint8_t *octets, uint32_t taille)
{
    oublier_bloc();
    etat.bloc.octets = malloc(taille ? taille : 1);
    if (!etat.bloc.octets) {
        return;
    }
    memcpy(etat.bloc.octets, octets, taille);
    etat.bloc.taille = taille;
    etat.bloc.lu = 1;
}

void brutus_versus_lire_fichier(FILE *fp)
{
    oublier_bloc();
    uint8_t entete[8];
    long position = ftell(fp);
    if (fread(entete, 1, 8, fp) != 8 || memcmp(entete, MARQUE, 4) != 0) {
        fseek(fp, position, SEEK_SET);
        return;
    }
    uint32_t taille = lire_u32(entete + 4);
    if (taille > 64u * 1024u * 1024u) {
        return;
    }
    uint8_t *d = malloc(taille ? taille : 1);
    if (!d) {
        return;
    }
    if (fread(d, 1, taille, fp) == taille) {
        garder_bloc(d, taille);
    }
    free(d);
}

void brutus_versus_lire_tampon(buffer *buf)
{
    oublier_bloc();
    size_t reste = buf->size > buf->index ? buf->size - buf->index : 0;
    const uint8_t *d = buf->data + buf->index;
    if (reste < 8 || memcmp(d, MARQUE, 4) != 0) {
        return;
    }
    uint32_t taille = lire_u32(d + 4);
    if (8 + (size_t) taille > reste) {
        return;
    }
    garder_bloc(d + 8, taille);
    buf->index += 8 + taille;
}

void brutus_versus_appliquer_lu(void)
{
    if (!etat.bloc.lu) {
        remettre_une_ville();
        return;
    }
    const uint8_t *d = etat.bloc.octets;
    uint32_t taille = etat.bloc.taille;
    uint32_t p = 0;
    int version = taille >= 1 ? d[0] : 0;
    int ok = taille >= 2 && version >= 1 && version <= VERSION_BLOC && d[1] >= 1 && d[1] <= BRUTUS_VILLES_MAX;
    int villes = ok ? d[1] : 1;
    p = 2;
    unsigned int nb_batiments = 0;
    unsigned int nb_figures = 0;
    if (ok && p + 4 <= taille) {
        nb_batiments = lire_u32(d + p);
        p += 4;
        ok = p + nb_batiments <= taille && nb_batiments == (unsigned int) building_count();
    }
    if (ok) {
        for (unsigned int i = 0; i < nb_batiments; i++) {
            building_get(i)->brutus_proprietaire = d[p + i];
        }
        p += nb_batiments;
        ok = p + 4 <= taille;
    }
    if (ok) {
        nb_figures = lire_u32(d + p);
        p += 4;
        ok = p + nb_figures <= taille && nb_figures == figure_count();
    }
    if (ok) {
        for (unsigned int i = 0; i < nb_figures; i++) {
            figure_get(i)->brutus_proprietaire = d[p + i];
        }
        p += nb_figures;
    }
    if (ok && version >= 2) {
        ok = p + 4 <= taille;
        unsigned int nb_messages = ok ? lire_u32(d + p) : 0;
        p += 4;
        ok = ok && p + nb_messages <= taille && nb_messages == (unsigned int) city_message_brutus_cases();
        for (unsigned int i = 0; ok && i < nb_messages; i++) {
            city_message_brutus_ville_case_definir((int) i, (int) d[p + i] - 1);
        }
        p += nb_messages;
    }
    if (!ok) {
        log_error("Brutus III (M8) : bloc Versus illisible, partie chargée avec une seule ville", 0, 0);
        oublier_bloc();
        remettre_une_ville();
        return;
    }
    etat.villes = villes;
    etat.vue = 0;
    for (int v = 1; v < villes && ok; v++) {
        if (p + 4 > taille) {
            ok = 0;
            break;
        }
        uint32_t t = lire_u32(d + p);
        p += 4;
        if (p + t > taille) {
            ok = 0;
            break;
        }
        buffer b;
        buffer_init(&b, (uint8_t *) d + p, t);
        brutus_ville_activer(v);
        city_data_brutus_lire(&b);
        brutus_villes_donnees[v].map = brutus_villes_donnees[0].map;
        p += t;
        if (version >= 3) {
            buffer_init(&b, (uint8_t *) d + p, taille - p);
            ok = scenario_request_brutus_lire(&b) && !b.overflow;
            p += (uint32_t) b.index;
        } else {
            scenario_request_brutus_copier(v); // ancien bloc : les demandes de la ville 1
        }
    }
    brutus_ville_activer(0);
    oublier_bloc();
    building_brutus_reconstruire_listes(); // les listes par type et par ville suivent les propriétaires
    if (!ok) {
        log_error("Brutus III (M8) : données de ville Versus illisibles", 0, 0);
        remettre_une_ville();
    }
}

void brutus_versus_partie_neuve(void)
{
    oublier_bloc();
    remettre_une_ville();
}

uint64_t brutus_versus_empreinte(uint64_t h)
{
    if (etat.villes <= 1) {
        return h; // solo et Coop : empreinte inchangée
    }
    uint8_t *d;
    uint32_t taille = composer(&d);
    if (!d) {
        return h;
    }
    h = brutus_fnv1a(h, MARQUE, sizeof(MARQUE));
    h = brutus_fnv1a(h, d, taille);
    free(d);
    return h;
}

// ------------------------------------------------------------------ Versus local (touche Alt+W)

#include "city/warning.h"
#include "core/encoding.h"
#include "game/cheats.h"
#include "graphics/window.h"
#include "multijoueur/commande.h"

#include <stdio.h>

void brutus_versus_touche(void)
{
    if (!window_is(WINDOW_CITY)) {
        return;
    }
    static uint8_t texte[200];
    if (brutus_multijoueur_actif()) {
        encoding_from_utf8("Versus en réseau : pas encore (M12).", texte, sizeof(texte));
        city_warning_show_custom(texte, NEW_WARNING_SLOT);
        return;
    }
    if (!brutus_versus_actif()) {
        brutus_commande_1(BRUTUS_CMD_TRICHE, BRUTUS_TRICHE_VERSUS);
        encoding_from_utf8("Versus local : deux villes. Alt+W pour jouer l'autre ville.", texte, sizeof(texte));
        city_warning_show_custom(texte, NEW_WARNING_SLOT);
        return;
    }
    int ville = (etat.vue + 1) % etat.villes;
    brutus_ville_vue_changer(ville);
    brutus_joueur_local_definir(ville);
    char ligne[120];
    snprintf(ligne, sizeof(ligne), "Tu joues maintenant pour la ville %d (%s).", ville + 1, ville ? "rouge" : "bleue");
    encoding_from_utf8(ligne, texte, sizeof(texte));
    city_warning_show_custom(texte, NEW_WARNING_SLOT);
}
