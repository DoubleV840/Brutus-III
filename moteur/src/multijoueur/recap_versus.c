// Brutus III (M12) : récap d'une partie Versus (voir recap_versus.h et docs/versus.md).
#include "multijoueur/recap_versus.h"

#include "building/building.h"
#include "city/data_private.h"
#include "game/time.h"
#include "multijoueur/commande.h"
#include "multijoueur/empreinte.h"
#include "multijoueur/versus.h"
#include "multijoueur/victoire_versus.h"

#include <stdio.h>
#include <string.h>

#define VERSION_RECAP 1
#define TAILLE_ENTETE 37
#define TAILLE_VILLE 28
#define TAILLE_RELEVE (8 + BRUTUS_VILLES_MAX * TAILLE_VILLE)

typedef struct {
    int32_t population;
    int32_t tresor;
    int32_t score;
    uint8_t prosperite;
    uint8_t culture;
    uint16_t soldats;
    uint32_t construits;
    uint32_t perdus;
    uint32_t tues;
} valeurs_ville;

typedef struct {
    int16_t annee;
    uint8_t mois;
    uint8_t fin;
    uint32_t tick;
    valeurs_ville v[BRUTUS_VILLES_MAX];
} releve;

static struct {
    int nb;
    releve releves[BRUTUS_RECAP_MAX];
    uint32_t construits[BRUTUS_VILLES_MAX];
    uint32_t perdus[BRUTUS_VILLES_MAX];
    uint32_t tues[BRUTUS_VILLES_MAX];
    struct {
        int ville; // -1 : aucun
        int type;
        uint32_t tick;
        int annee;
        int mois;
    } premier;
} recap = { 0, { { 0 } }, { 0 }, { 0 }, { 0 }, { -1, 0, 0, 0, 0 } };

// ------------------------------------------------------------------ lecture

int brutus_recap_nombre(void)
{
    return recap.nb;
}

static const releve *releve_numero(int i)
{
    return i >= 0 && i < recap.nb ? &recap.releves[i] : 0;
}

int32_t brutus_recap_valeur(int i, int ville, brutus_recap_statistique stat)
{
    const releve *r = releve_numero(i);
    if (!r || ville < 0 || ville >= BRUTUS_VILLES_MAX) {
        return 0;
    }
    const valeurs_ville *v = &r->v[ville];
    switch (stat) {
        case BRUTUS_RECAP_POPULATION: return v->population;
        case BRUTUS_RECAP_TRESOR: return v->tresor;
        case BRUTUS_RECAP_PROSPERITE: return v->prosperite;
        case BRUTUS_RECAP_CULTURE: return v->culture;
        case BRUTUS_RECAP_SCORE: return v->score;
        case BRUTUS_RECAP_SOLDATS: return v->soldats;
        case BRUTUS_RECAP_CONSTRUITS: return (int32_t) v->construits;
        case BRUTUS_RECAP_PERDUS: return (int32_t) v->perdus;
        case BRUTUS_RECAP_TUES: return (int32_t) v->tues;
        default: return 0;
    }
}

int brutus_recap_annee(int i)
{
    const releve *r = releve_numero(i);
    return r ? r->annee : 0;
}

int brutus_recap_mois(int i)
{
    const releve *r = releve_numero(i);
    return r ? r->mois : 0;
}

uint32_t brutus_recap_tick(int i)
{
    const releve *r = releve_numero(i);
    return r ? r->tick : 0;
}

int brutus_recap_est_fin(int i)
{
    const releve *r = releve_numero(i);
    return r ? r->fin : 0;
}

int brutus_recap_premier_detruit_ville(void)
{
    return recap.premier.ville;
}

int brutus_recap_premier_detruit_type(void)
{
    return recap.premier.type;
}

uint32_t brutus_recap_premier_detruit_tick(void)
{
    return recap.premier.tick;
}

int brutus_recap_premier_detruit_annee(void)
{
    return recap.premier.annee;
}

int brutus_recap_premier_detruit_mois(void)
{
    return recap.premier.mois;
}

// ------------------------------------------------------------------ simulation

static int en_cours(void)
{
    return brutus_versus_actif() && brutus_victoire_fin() == BRUTUS_FIN_AUCUNE;
}

static int borne_u16(int v)
{
    return v < 0 ? 0 : v > 65535 ? 65535 : v;
}

static int borne_u8(int v)
{
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

void brutus_recap_relever(int fin)
{
    if (!brutus_versus_actif() || recap.nb >= BRUTUS_RECAP_MAX) {
        return;
    }
    releve *r = &recap.releves[recap.nb++];
    memset(r, 0, sizeof(*r));
    r->annee = (int16_t) game_time_year();
    r->mois = (uint8_t) game_time_month();
    r->fin = fin ? 1 : 0;
    r->tick = brutus_tick_absolu();
    for (int v = 0; v < brutus_versus_villes() && v < BRUTUS_VILLES_MAX; v++) {
        brutus_score_ville s = brutus_victoire_score(v);
        valeurs_ville *vv = &r->v[v];
        vv->population = s.population;
        vv->tresor = s.tresor;
        vv->score = s.score;
        vv->prosperite = (uint8_t) borne_u8(s.prosperite);
        vv->culture = (uint8_t) borne_u8(s.culture);
        vv->soldats = (uint16_t) borne_u16(brutus_villes_donnees[v].military.total_soldiers);
        vv->construits = recap.construits[v];
        vv->perdus = recap.perdus[v];
        vv->tues = recap.tues[v];
    }
}

int brutus_recap_compter_poses(void)
{
    if (!en_cours()) {
        return 0;
    }
    int ville = brutus_ville_active();
    int n = 0;
    for (int i = 1; i < building_count(); i++) {
        const building *b = building_get(i);
        if (b->state == BUILDING_STATE_CREATED && !b->prev_part_building_id && b->brutus_proprietaire == ville) {
            n++;
        }
    }
    return n;
}

void brutus_recap_construits(int n)
{
    int ville = brutus_ville_active();
    if (n > 0 && en_cours() && ville >= 0 && ville < BRUTUS_VILLES_MAX) {
        recap.construits[ville] += (uint32_t) n;
    }
}

void brutus_recap_perdu(const struct building *b)
{
    if (!en_cours() || !b || b->id <= 0 || b->prev_part_building_id ||
        (b->state != BUILDING_STATE_IN_USE && b->state != BUILDING_STATE_MOTHBALLED)) {
        return;
    }
    int ville = b->brutus_proprietaire;
    if (ville >= 0 && ville < BRUTUS_VILLES_MAX) {
        recap.perdus[ville]++;
    }
}

void brutus_recap_soldat_tue(int victime)
{
    if (en_cours() && victime >= 0 && victime < BRUTUS_VILLES_MAX) {
        recap.tues[victime]++;
    }
}

void brutus_recap_detruit_par_ennemi(int victime, int type)
{
    if (!en_cours() || recap.premier.ville >= 0 || victime < 0 || victime >= BRUTUS_VILLES_MAX) {
        return;
    }
    recap.premier.ville = victime;
    recap.premier.type = type;
    recap.premier.tick = brutus_tick_absolu();
    recap.premier.annee = game_time_year();
    recap.premier.mois = game_time_month();
}

// ------------------------------------------------------------------ sauvegarde

void brutus_recap_remettre(void)
{
    recap.nb = 0;
    memset(recap.construits, 0, sizeof(recap.construits));
    memset(recap.perdus, 0, sizeof(recap.perdus));
    memset(recap.tues, 0, sizeof(recap.tues));
    memset(&recap.premier, 0, sizeof(recap.premier));
    recap.premier.ville = -1;
}

uint32_t brutus_recap_taille(void)
{
    return TAILLE_ENTETE + (uint32_t) recap.nb * TAILLE_RELEVE;
}

static void e16(uint8_t *d, uint32_t v)
{
    d[0] = (uint8_t) v;
    d[1] = (uint8_t) (v >> 8);
}

static void e32(uint8_t *d, uint32_t v)
{
    d[0] = (uint8_t) v;
    d[1] = (uint8_t) (v >> 8);
    d[2] = (uint8_t) (v >> 16);
    d[3] = (uint8_t) (v >> 24);
}

static uint32_t l16(const uint8_t *d)
{
    return d[0] | (d[1] << 8);
}

static uint32_t l32(const uint8_t *d)
{
    return d[0] | (d[1] << 8) | (d[2] << 16) | ((uint32_t) d[3] << 24);
}

void brutus_recap_ecrire(uint8_t *d)
{
    uint32_t p = 0;
    d[p++] = VERSION_RECAP;
    e16(d + p, (uint32_t) recap.nb);
    p += 2;
    for (int v = 0; v < BRUTUS_VILLES_MAX; v++) {
        e32(d + p, recap.construits[v]);
        e32(d + p + 4, recap.perdus[v]);
        e32(d + p + 8, recap.tues[v]);
        p += 12;
    }
    d[p++] = (uint8_t) (recap.premier.ville + 1);
    e16(d + p, (uint32_t) recap.premier.type);
    e32(d + p + 2, recap.premier.tick);
    e16(d + p + 6, (uint32_t) (uint16_t) (int16_t) recap.premier.annee);
    d[p + 8] = (uint8_t) recap.premier.mois;
    p += 9;
    for (int i = 0; i < recap.nb; i++) {
        const releve *r = &recap.releves[i];
        e16(d + p, (uint32_t) (uint16_t) r->annee);
        d[p + 2] = r->mois;
        d[p + 3] = r->fin;
        e32(d + p + 4, r->tick);
        p += 8;
        for (int v = 0; v < BRUTUS_VILLES_MAX; v++) {
            const valeurs_ville *vv = &r->v[v];
            e32(d + p, (uint32_t) vv->population);
            e32(d + p + 4, (uint32_t) vv->tresor);
            e32(d + p + 8, (uint32_t) vv->score);
            d[p + 12] = vv->prosperite;
            d[p + 13] = vv->culture;
            e16(d + p + 14, vv->soldats);
            e32(d + p + 16, vv->construits);
            e32(d + p + 20, vv->perdus);
            e32(d + p + 24, vv->tues);
            p += TAILLE_VILLE;
        }
    }
}

int brutus_recap_lire(const uint8_t *d, uint32_t taille, uint32_t *lus)
{
    brutus_recap_remettre();
    *lus = 0;
    if (taille < TAILLE_ENTETE || d[0] != VERSION_RECAP) {
        return 0;
    }
    uint32_t p = 1;
    uint32_t nb = l16(d + p);
    p += 2;
    if (nb > BRUTUS_RECAP_MAX || TAILLE_ENTETE + nb * TAILLE_RELEVE > taille) {
        return 0;
    }
    for (int v = 0; v < BRUTUS_VILLES_MAX; v++) {
        recap.construits[v] = l32(d + p);
        recap.perdus[v] = l32(d + p + 4);
        recap.tues[v] = l32(d + p + 8);
        p += 12;
    }
    recap.premier.ville = (int) d[p++] - 1;
    if (recap.premier.ville >= BRUTUS_VILLES_MAX) {
        recap.premier.ville = -1;
    }
    recap.premier.type = (int) l16(d + p);
    recap.premier.tick = l32(d + p + 2);
    recap.premier.annee = (int16_t) l16(d + p + 6);
    recap.premier.mois = d[p + 8];
    p += 9;
    for (uint32_t i = 0; i < nb; i++) {
        releve *r = &recap.releves[i];
        r->annee = (int16_t) l16(d + p);
        r->mois = d[p + 2];
        r->fin = d[p + 3] ? 1 : 0;
        r->tick = l32(d + p + 4);
        p += 8;
        for (int v = 0; v < BRUTUS_VILLES_MAX; v++) {
            valeurs_ville *vv = &r->v[v];
            vv->population = (int32_t) l32(d + p);
            vv->tresor = (int32_t) l32(d + p + 4);
            vv->score = (int32_t) l32(d + p + 8);
            vv->prosperite = d[p + 12];
            vv->culture = d[p + 13];
            vv->soldats = (uint16_t) l16(d + p + 14);
            vv->construits = l32(d + p + 16);
            vv->perdus = l32(d + p + 20);
            vv->tues = l32(d + p + 24);
            p += TAILLE_VILLE;
        }
    }
    recap.nb = (int) nb;
    *lus = p;
    return 1;
}

// ------------------------------------------------------------------ bilan (rejouage)

static uint64_t empreinte(void)
{
    static uint8_t octets[TAILLE_ENTETE + BRUTUS_RECAP_MAX * TAILLE_RELEVE];
    brutus_recap_ecrire(octets);
    return brutus_fnv1a(BRUTUS_FNV_DEPART, octets, brutus_recap_taille());
}

uint32_t brutus_recap_empreinte32(void)
{
    uint64_t h = empreinte();
    return (uint32_t) (h ^ (h >> 32));
}

void brutus_recap_bilan(void)
{
    if (!brutus_versus_actif()) {
        return;
    }
    uint64_t h = empreinte();
    printf("Brutus III : recap : releves %d premier_detruit %d type %d tick %u empreinte %08x%08x\n", recap.nb,
        recap.premier.ville + 1, recap.premier.type, (unsigned int) recap.premier.tick,
        (unsigned int) (h >> 32), (unsigned int) (h & 0xffffffffu));
    for (int i = 0; i < recap.nb; i++) {
        const releve *r = &recap.releves[i];
        printf("Brutus III : recap releve %d : annee %d mois %d tick %u fin %d", i, r->annee, r->mois + 1,
            (unsigned int) r->tick, r->fin);
        for (int v = 0; v < brutus_versus_villes() && v < BRUTUS_VILLES_MAX; v++) {
            const valeurs_ville *vv = &r->v[v];
            printf(" | ville %d population %d tresor %d prosperite %d culture %d score %d soldats %d construits %u "
                "perdus %u tues %u", v + 1, vv->population, vv->tresor, vv->prosperite, vv->culture, vv->score,
                vv->soldats, (unsigned int) vv->construits, (unsigned int) vv->perdus, (unsigned int) vv->tues);
        }
        printf("\n");
    }
}
