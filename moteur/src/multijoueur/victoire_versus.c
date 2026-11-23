// Brutus III (M11) : fin d'une partie Versus (voir victoire_versus.h et docs/versus.md).
#include "multijoueur/victoire_versus.h"

#include "city/data_private.h"
#include "city/warning.h"
#include "core/encoding.h"
#include "core/log.h"
#include "game/cheats.h"
#include "graphics/window.h"
#include "multijoueur/commande.h"
#include "multijoueur/versus.h"

#include <stdio.h>
#include <string.h>

static struct {
    int duree;        // mois, 0 : sans limite
    int mois_ecoules; // depuis le passage en Versus
    int fin;
    int gagnant;
    uint32_t tick_fin;
    brutus_score_ville scores[BRUTUS_VILLES_MAX];
} etat = { BRUTUS_VICTOIRE_DUREE_DEFAUT, 0, BRUTUS_FIN_AUCUNE, -1, 0, { { 0 } } };

// ------------------------------------------------------------------ options

int brutus_victoire_duree(void)
{
    return etat.duree;
}

void brutus_victoire_definir_duree(int mois)
{
    if (mois < 0) {
        mois = 0;
    }
    if (mois > 65535) {
        mois = 65535;
    }
    etat.duree = mois;
}

int brutus_victoire_mois_ecoules(void)
{
    return etat.mois_ecoules;
}

static int duree_suivante(int duree)
{
    static const int durees[] = { 60, 120, 240, 0 };
    int n = (int) (sizeof(durees) / sizeof(durees[0]));
    for (int i = 0; i < n; i++) {
        if (durees[i] == duree) {
            return durees[(i + 1) % n];
        }
    }
    return durees[0];
}

int brutus_victoire_triche_duree(int valeur)
{
    if (!brutus_versus_actif() || etat.fin != BRUTUS_FIN_AUCUNE) {
        return 0;
    }
    brutus_victoire_definir_duree(valeur >= 0 ? valeur : duree_suivante(etat.duree));
    return 1;
}

static void texte_duree(int mois, char *sortie, size_t taille)
{
    if (!mois) {
        snprintf(sortie, taille, "sans limite");
    } else if (mois % 12 == 0) {
        snprintf(sortie, taille, "%d an%s", mois / 12, mois >= 24 ? "s" : "");
    } else {
        snprintf(sortie, taille, "%d mois", mois);
    }
}

void brutus_victoire_touche(void)
{
    if (!window_is(WINDOW_CITY) || !brutus_versus_actif() || brutus_multijoueur_actif()) {
        return;
    }
    static uint8_t texte[160];
    char ligne[160];
    if (etat.fin != BRUTUS_FIN_AUCUNE) {
        snprintf(ligne, sizeof(ligne), "La partie est finie : la durée ne change plus.");
    } else {
        brutus_commande_2(BRUTUS_CMD_TRICHE, BRUTUS_TRICHE_DUREE, -1);
        // La commande s'applique au tick suivant : on annonce la durée qu'elle donnera
        int duree = duree_suivante(etat.duree);
        char d[40];
        texte_duree(duree, d, sizeof(d));
        if (duree && duree <= etat.mois_ecoules) {
            snprintf(ligne, sizeof(ligne), "Durée de la partie : %s (déjà passée : fin le mois prochain).", d);
        } else {
            snprintf(ligne, sizeof(ligne), "Durée de la partie : %s.", d);
        }
    }
    encoding_from_utf8(ligne, texte, sizeof(texte));
    city_warning_show_custom(texte, NEW_WARNING_SLOT);
}

// ------------------------------------------------------------------ scores

brutus_score_ville brutus_victoire_score(int ville)
{
    brutus_score_ville s = { 0, 0, 0, 0, 0 };
    if (ville < 0 || ville >= BRUTUS_VILLES_MAX) {
        return s;
    }
    const struct city_data_t *v = &brutus_villes_donnees[ville];
    s.population = v->population.population;
    s.tresor = v->finance.treasury;
    s.prosperite = v->ratings.prosperity;
    s.culture = v->ratings.culture;
    s.score = s.population + s.tresor / 10 + 25 * (s.prosperite + s.culture);
    return s;
}

static void finir(int raison, int gagnant)
{
    etat.fin = raison;
    etat.gagnant = gagnant;
    etat.tick_fin = brutus_tick_absolu();
    for (int v = 0; v < BRUTUS_VILLES_MAX; v++) {
        etat.scores[v] = v < brutus_versus_villes() ? brutus_victoire_score(v) : (brutus_score_ville) { 0, 0, 0, 0, 0 };
    }
    log_info("Brutus III (M11) : fin du Versus, raison / gagnant", 0, raison * 10 + gagnant + 1);
}

void brutus_victoire_nouveau_mois(void)
{
    if (!brutus_versus_actif() || etat.fin != BRUTUS_FIN_AUCUNE) {
        return;
    }
    if (etat.mois_ecoules < 65535) {
        etat.mois_ecoules++;
    }
    if (!etat.duree || etat.mois_ecoules < etat.duree) {
        return;
    }
    int gagnant = -1;
    int meilleur = 0;
    for (int v = 0; v < brutus_versus_villes(); v++) {
        int score = brutus_victoire_score(v).score;
        if (v == 0 || score > meilleur) {
            meilleur = score;
            gagnant = v;
        } else if (score == meilleur) {
            gagnant = -1;
        }
    }
    finir(BRUTUS_FIN_ECONOMIQUE, gagnant);
}

void brutus_victoire_senat_detruit(int victime, int attaquant)
{
    if (!brutus_versus_actif() || etat.fin != BRUTUS_FIN_AUCUNE || victime == attaquant ||
        attaquant < 0 || attaquant >= brutus_versus_villes()) {
        return;
    }
    finir(BRUTUS_FIN_MILITAIRE, attaquant);
}

int brutus_victoire_fin(void)
{
    return brutus_versus_actif() ? etat.fin : BRUTUS_FIN_AUCUNE;
}

int brutus_victoire_gagnant(void)
{
    return etat.gagnant;
}

uint32_t brutus_victoire_tick_fin(void)
{
    return etat.tick_fin;
}

const brutus_score_ville *brutus_victoire_scores_fin(int ville)
{
    static const brutus_score_ville vide = { 0, 0, 0, 0, 0 };
    return ville >= 0 && ville < BRUTUS_VILLES_MAX ? &etat.scores[ville] : &vide;
}

// ------------------------------------------------------------------ sauvegarde (bloc BR3V, version 6)

void brutus_victoire_remettre(void)
{
    memset(&etat, 0, sizeof(etat));
    etat.duree = BRUTUS_VICTOIRE_DUREE_DEFAUT;
    etat.gagnant = -1;
}

static void ecrire_32(uint8_t *d, uint32_t v)
{
    d[0] = (uint8_t) v;
    d[1] = (uint8_t) (v >> 8);
    d[2] = (uint8_t) (v >> 16);
    d[3] = (uint8_t) (v >> 24);
}

static uint32_t lire_32(const uint8_t *d)
{
    return d[0] | (d[1] << 8) | (d[2] << 16) | ((uint32_t) d[3] << 24);
}

void brutus_victoire_ecrire(uint8_t *d)
{
    int p = 0;
    d[p++] = (uint8_t) etat.duree;
    d[p++] = (uint8_t) (etat.duree >> 8);
    d[p++] = (uint8_t) etat.mois_ecoules;
    d[p++] = (uint8_t) (etat.mois_ecoules >> 8);
    d[p++] = (uint8_t) etat.fin;
    d[p++] = (uint8_t) (etat.gagnant + 1); // 0 : égalité
    ecrire_32(d + p, etat.tick_fin);
    p += 4;
    for (int v = 0; v < BRUTUS_VILLES_MAX; v++) {
        const brutus_score_ville *s = &etat.scores[v];
        ecrire_32(d + p, (uint32_t) s->population);
        ecrire_32(d + p + 4, (uint32_t) s->tresor);
        ecrire_32(d + p + 8, (uint32_t) s->prosperite);
        ecrire_32(d + p + 12, (uint32_t) s->culture);
        ecrire_32(d + p + 16, (uint32_t) s->score);
        p += 20;
    }
}

void brutus_victoire_lire(const uint8_t *d)
{
    int p = 0;
    etat.duree = d[p] | (d[p + 1] << 8);
    p += 2;
    etat.mois_ecoules = d[p] | (d[p + 1] << 8);
    p += 2;
    etat.fin = d[p] <= BRUTUS_FIN_ECONOMIQUE ? d[p] : BRUTUS_FIN_AUCUNE;
    p++;
    etat.gagnant = (int) d[p++] - 1;
    etat.tick_fin = lire_32(d + p);
    p += 4;
    for (int v = 0; v < BRUTUS_VILLES_MAX; v++) {
        brutus_score_ville *s = &etat.scores[v];
        s->population = (int32_t) lire_32(d + p);
        s->tresor = (int32_t) lire_32(d + p + 4);
        s->prosperite = (int32_t) lire_32(d + p + 8);
        s->culture = (int32_t) lire_32(d + p + 12);
        s->score = (int32_t) lire_32(d + p + 16);
        p += 20;
    }
}

// ------------------------------------------------------------------ bilan (rejouage)

void brutus_victoire_bilan(void)
{
    if (!brutus_versus_actif()) {
        return;
    }
    static const char *raisons[] = { "aucune", "militaire", "economique" };
    printf("Brutus III : victoire : fin %s gagnant %d tick %u duree %d mois %d\n", raisons[etat.fin],
        etat.fin ? etat.gagnant + 1 : 0, (unsigned int) etat.tick_fin, etat.duree, etat.mois_ecoules);
    for (int v = 0; v < brutus_versus_villes(); v++) {
        brutus_score_ville s = etat.fin ? etat.scores[v] : brutus_victoire_score(v);
        printf("Brutus III : score ville %d : population %d tresor %d prosperite %d culture %d score %d\n", v + 1,
            s.population, s.tresor, s.prosperite, s.culture, s.score);
    }
}
