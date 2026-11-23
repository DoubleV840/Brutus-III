// Brutus III (M11, M12) : écran de fin du Versus et récap de la partie (voir fin_versus.h).
#include "multijoueur/fin_versus.h"

#include "building/construction.h"
#include "core/encoding.h"
#include "core/lang.h"
#include "game/state.h"
#include "game/undo.h"
#include "graphics/button.h"
#include "graphics/color.h"
#include "graphics/generic_button.h"
#include "graphics/graphics.h"
#include "graphics/panel.h"
#include "graphics/text.h"
#include "graphics/window.h"
#include "input/input.h"
#include "multijoueur/combat_versus.h"
#include "multijoueur/menu.h"
#include "multijoueur/recap_versus.h"
#include "multijoueur/reseau.h"
#include "multijoueur/versus.h"
#include "multijoueur/victoire_versus.h"
#include "sound/speech.h"
#include "window/city.h"
#include "window/main_menu.h"

#include <stdio.h>
#include <string.h>

// Fenêtre : 608 x 464 dans l'écran minimal de 640 x 480
#define X0 16
#define Y0 8
#define LARGEUR 608
#define HAUTEUR 464
#define TEXTE_MAX 300

// Onglets
#define ONGLET_Y (Y0 + 82)
#define ONGLET_X (X0 + 10)
#define ONGLETS_L (LARGEUR - 20)
#define ONGLET_H 20

// Cadre du contenu (courbes ou tableau)
#define CADRE_X (X0 + 16)
#define CADRE_Y (Y0 + 106)
#define CADRE_L 576
#define CADRE_H 212

// Zone des courbes, dans le cadre
#define TRACE_X (CADRE_X + 60)
#define TRACE_Y (CADRE_Y + 30)
#define TRACE_L 444
#define TRACE_H 158

#define FAITS_Y (Y0 + 324)
#define BOUTONS_Y (Y0 + 426)

// Couleurs du récap (vérifiées : écart suffisant pour les daltonismes, contraste sur le fond clair)
#define FOND_COURBES 0xfff4ecd8
#define BORD_COURBES 0xff8a7a5a
#define GRILLE 0xffd9ccae
#define ENCRE 0xff2b2418
#define ENCRE_DOUCE 0xff6b5d44
static const color_t COULEUR_VILLE[2] = { 0xff1f6fd6, 0xffd42a1c };

static void bouton_continuer(const generic_button *b);
static void bouton_menu(const generic_button *b);
static void bouton_onglet(const generic_button *b);

static generic_button boutons[2 + BRUTUS_FIN_ONGLETS] = {
    {X0 + 32, BOUTONS_Y, 228, 26, bouton_continuer},
    {X0 + LARGEUR - 260, BOUTONS_Y, 228, 26, bouton_menu},
};

static struct {
    unsigned int focus;
    int affichee;
    uint32_t tick_affiche;
    int onglet;
    int souris_x;
    int souris_y;
} etat;

typedef struct {
    const char *onglet;
    const char *titre;
    brutus_recap_statistique stat;
} description_onglet;

static const description_onglet ONGLETS[BRUTUS_FIN_ONGLETS] = {
    { "Score", "Score", BRUTUS_RECAP_SCORE },
    { "Population", "Population (habitants)", BRUTUS_RECAP_POPULATION },
    { "Trésor", "Trésor (deniers)", BRUTUS_RECAP_TRESOR },
    { "Prospérité", "Prospérité (note de 0 à 100)", BRUTUS_RECAP_PROSPERITE },
    { "Culture", "Culture (note de 0 à 100)", BRUTUS_RECAP_CULTURE },
    { "Soldats", "Soldats des légions", BRUTUS_RECAP_SOLDATS },
    { "Construits", "Bâtiments construits (total)", BRUTUS_RECAP_CONSTRUITS },
    { "Perdus", "Bâtiments perdus (total)", BRUTUS_RECAP_PERDUS },
    { "Tués", "Soldats tués par l'autre ville (total)", BRUTUS_RECAP_TUES },
    { "Tableau", "", BRUTUS_RECAP_SCORE },
};

// Convertit un texte UTF-8 dans l'encodage du jeu (tampons tournants : plusieurs textes par image).
static const uint8_t *t(const char *utf8)
{
    static uint8_t tampons[16][TEXTE_MAX];
    static int prochain;
    uint8_t *sortie = tampons[prochain];
    prochain = (prochain + 1) % 16;
    encoding_from_utf8(utf8, sortie, TEXTE_MAX);
    return sortie;
}

static const char *nom_ville(int ville)
{
    return ville == 0 ? "bleue" : "rouge";
}

// M12 : en réseau, les villes portent le pseudo de leur joueur (« la ville de Léa »), sinon leur couleur.
static const char *tampon_nom(void)
{
    static char tampons[8][96];
    static int prochain;
    prochain = (prochain + 1) % 8;
    return tampons[prochain];
}

static const char *la_ville(int ville)
{
    char *n = (char *) tampon_nom();
    const char *pseudo = brutus_versus_pseudo_ville(ville);
    if (pseudo) {
        snprintf(n, 96, "la ville de %s", pseudo);
    } else {
        snprintf(n, 96, "la ville %s", nom_ville(ville));
    }
    return n;
}

static const char *etiquette(int ville)
{
    char *n = (char *) tampon_nom();
    const char *pseudo = brutus_versus_pseudo_ville(ville);
    if (pseudo) {
        snprintf(n, 96, "%s", pseudo);
    } else {
        snprintf(n, 96, "Ville %s", nom_ville(ville));
    }
    return n;
}

static const char *nom_court(int ville)
{
    const char *pseudo = brutus_versus_pseudo_ville(ville);
    return pseudo ? pseudo : nom_ville(ville);
}

// 12345 -> « 12 345 »
static void nombre(int valeur, char *sortie, size_t taille)
{
    char chiffres[16];
    unsigned int v = valeur < 0 ? (unsigned int) -(long long) valeur : (unsigned int) valeur;
    snprintf(chiffres, sizeof(chiffres), "%u", v);
    int n = (int) strlen(chiffres);
    size_t p = 0;
    if (valeur < 0 && p + 1 < taille) {
        sortie[p++] = '-';
    }
    for (int i = 0; i < n && p + 2 < taille; i++) {
        if (i > 0 && (n - i) % 3 == 0) {
            sortie[p++] = ' ';
        }
        sortie[p++] = chiffres[i];
    }
    sortie[p] = 0;
}

static void date(int annee, int mois, char *sortie, size_t taille)
{
    static const char *noms[12] = { "janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août",
        "septembre", "octobre", "novembre", "décembre" };
    const char *m = mois >= 0 && mois < 12 ? noms[mois] : "?";
    if (annee < 0) {
        snprintf(sortie, taille, "%s %d av. J.-C.", m, -annee);
    } else {
        snprintf(sortie, taille, "%s %d apr. J.-C.", m, annee);
    }
}

static void date_releve(int i, char *sortie, size_t taille)
{
    date(brutus_recap_annee(i), brutus_recap_mois(i), sortie, taille);
}

// Date courte des faits marquants (« juil. 350 ») : l'ère est donnée une fois, avec la durée.
static void date_courte(int annee, int mois, char *sortie, size_t taille)
{
    static const char *noms[12] = { "janv.", "févr.", "mars", "avr.", "mai", "juin", "juil.", "août",
        "sept.", "oct.", "nov.", "déc." };
    snprintf(sortie, taille, "%s %d", mois >= 0 && mois < 12 ? noms[mois] : "?", annee < 0 ? -annee : annee);
}

static void date_courte_releve(int i, char *sortie, size_t taille)
{
    date_courte(brutus_recap_annee(i), brutus_recap_mois(i), sortie, taille);
}

// ------------------------------------------------------------------ faits marquants

#define TICKS_PAR_MOIS 800 // 16 jours de 50 ticks

static int dernier(void)
{
    return brutus_recap_nombre() - 1;
}

static void fait_duree(char *s, size_t taille)
{
    int n = brutus_recap_nombre();
    if (n < 1) {
        snprintf(s, taille, "inconnue (partie commencée avant le récap)");
        return;
    }
    uint32_t ticks = brutus_victoire_tick_fin() > brutus_recap_tick(0) ? brutus_victoire_tick_fin() - brutus_recap_tick(0) : 0;
    int mois = (int) ((ticks + TICKS_PAR_MOIS / 2) / TICKS_PAR_MOIS);
    int ans = mois / 12;
    mois %= 12;
    char d[80];
    if (!ans && !mois) {
        snprintf(d, sizeof(d), "moins d'un mois");
    } else if (!ans) {
        snprintf(d, sizeof(d), "%d mois", mois);
    } else if (!mois) {
        snprintf(d, sizeof(d), "%d an%s", ans, ans > 1 ? "s" : "");
    } else {
        snprintf(d, sizeof(d), "%d an%s et %d mois", ans, ans > 1 ? "s" : "", mois);
    }
    char de[40], a[40];
    date_releve(0, de, sizeof(de));
    date_releve(dernier(), a, sizeof(a));
    if (brutus_recap_annee(0) < 0 && brutus_recap_annee(dernier()) < 0) {
        char *ere = strstr(de, " av. J.-C.");
        if (ere) {
            *ere = 0; // « de janvier 350 à janvier 349 av. J.-C. »
        }
    }
    snprintf(s, taille, "%s, de %s à %s", d, de, a);
}

static void fait_pic_population(char *s, size_t taille)
{
    int n = brutus_recap_nombre();
    char parties[2][60];
    for (int v = 0; v < 2; v++) {
        int meilleur = -1;
        int quand = 0;
        for (int i = 0; i < n; i++) {
            int p = brutus_recap_valeur(i, v, BRUTUS_RECAP_POPULATION);
            if (p > meilleur) {
                meilleur = p;
                quand = i;
            }
        }
        char nb[24], d[40];
        nombre(meilleur < 0 ? 0 : meilleur, nb, sizeof(nb));
        date_courte_releve(quand, d, sizeof(d));
        snprintf(parties[v], sizeof(parties[v]), "%s %s en %s", nom_court(v), nb, d);
    }
    snprintf(s, taille, "%s ; %s", parties[0], parties[1]);
}

static void fait_bataille(char *s, size_t taille)
{
    int n = brutus_recap_nombre();
    int meilleur = 0;
    int quand = -1;
    int pertes[2] = { 0, 0 };
    for (int i = 1; i < n; i++) {
        int p0 = brutus_recap_valeur(i, 0, BRUTUS_RECAP_TUES) - brutus_recap_valeur(i - 1, 0, BRUTUS_RECAP_TUES);
        int p1 = brutus_recap_valeur(i, 1, BRUTUS_RECAP_TUES) - brutus_recap_valeur(i - 1, 1, BRUTUS_RECAP_TUES);
        if (p0 + p1 > meilleur) {
            meilleur = p0 + p1;
            quand = i - 1; // le relevé i est fait au début du mois suivant : la bataille a eu lieu pendant le mois i - 1
            pertes[0] = p0;
            pertes[1] = p1;
        }
    }
    if (quand < 0) {
        snprintf(s, taille, brutus_combat_option() ? "aucune, pas un soldat tué entre les deux villes"
            : "aucune (combat désactivé)");
        return;
    }
    char d[40];
    date_courte_releve(quand, d, sizeof(d));
    snprintf(s, taille, "%d soldat%s tué%s en %s (%d bleu%s, %d rouge%s)", meilleur,
        meilleur > 1 ? "s" : "", meilleur > 1 ? "s" : "", d, pertes[0], pertes[0] > 1 ? "s" : "", pertes[1],
        pertes[1] > 1 ? "s" : "");
}

static void fait_premier_detruit(char *s, size_t taille)
{
    int ville = brutus_recap_premier_detruit_ville();
    if (ville < 0) {
        snprintf(s, taille, "aucun, aucune ville n'a perdu de bâtiment au combat");
        return;
    }
    char nom[80];
    const uint8_t *nom_jeu = lang_get_building_type_string(brutus_recap_premier_detruit_type());
    if (nom_jeu && nom_jeu[0]) {
        encoding_to_utf8(nom_jeu, nom, sizeof(nom), 0);
        if (nom[0] >= 'A' && nom[0] <= 'Z' && (nom[1] < 'A' || nom[1] > 'Z')) {
            nom[0] = (char) (nom[0] - 'A' + 'a'); // « Puits » -> « puits » (pas les sigles)
        }
    } else {
        snprintf(nom, sizeof(nom), "un bâtiment");
    }
    char d[40];
    date_courte(brutus_recap_premier_detruit_annee(), brutus_recap_premier_detruit_mois(), d, sizeof(d));
    snprintf(s, taille, "%s de %s, en %s", nom, la_ville(ville), d);
}

static int en_tete(int i)
{
    int a = brutus_recap_valeur(i, 0, BRUTUS_RECAP_SCORE);
    int b = brutus_recap_valeur(i, 1, BRUTUS_RECAP_SCORE);
    return a > b ? 0 : b > a ? 1 : -1;
}

static void fait_bascule(char *s, size_t taille)
{
    int n = brutus_recap_nombre();
    int tete = -1;
    int bascule = -1;
    int changements = 0;
    for (int i = 0; i < n; i++) {
        int e = en_tete(i);
        if (e < 0) {
            continue; // égalité : la tête ne change pas
        }
        if (tete >= 0 && e != tete) {
            bascule = i;
            changements++;
        }
        tete = e;
    }
    if (tete < 0) {
        snprintf(s, taille, "jamais : égalité tout au long de la partie");
    } else if (bascule < 0) {
        snprintf(s, taille, "jamais : %s a mené du début à la fin", la_ville(tete));
    } else {
        char d[40];
        date_courte_releve(bascule, d, sizeof(d));
        snprintf(s, taille, "en %s, %s passe devant %s", d, la_ville(tete),
            changements > 1 ? "pour de bon" : "et y reste");
    }
}

// ------------------------------------------------------------------ courbes

static int pas_rond(int etendue)
{
    // Pas des graduations : 1, 2 ou 5 x 10^k, environ 4 intervalles
    int brut = etendue / 4;
    if (brut < 1) {
        return 1;
    }
    int puissance = 1;
    while (puissance <= brut / 10) {
        puissance *= 10;
    }
    int m = brut / puissance;
    int pas = m < 2 ? 2 : m < 5 ? 5 : 10;
    return pas * puissance;
}

static int plancher(int v, int pas)
{
    return v >= 0 ? (v / pas) * pas : -(((-v) + pas - 1) / pas) * pas;
}

static int plafond(int v, int pas)
{
    return v >= 0 ? ((v + pas - 1) / pas) * pas : -(((-v) / pas) * pas);
}

typedef struct {
    int bas;
    int haut;
    uint32_t tick0;
    uint32_t duree;
} echelle;

static int x_releve(const echelle *e, int i)
{
    if (!e->duree) {
        return TRACE_X;
    }
    return TRACE_X + (int) ((uint64_t) (brutus_recap_tick(i) - e->tick0) * (TRACE_L - 1) / e->duree);
}

static int y_valeur(const echelle *e, int v)
{
    return TRACE_Y + TRACE_H - 1 - (int) ((int64_t) (v - e->bas) * (TRACE_H - 1) / (e->haut - e->bas));
}

static void trait_epais(int x1, int y1, int x2, int y2, color_t c)
{
    graphics_draw_line(x1, x2, y1, y2, c);
    graphics_draw_line(x1, x2, y1 + 1, y2 + 1, c);
}

static int releve_sous_souris(const echelle *e)
{
    int n = brutus_recap_nombre();
    if (etat.souris_x < TRACE_X - 4 || etat.souris_x > TRACE_X + TRACE_L + 4 ||
        etat.souris_y < TRACE_Y || etat.souris_y > TRACE_Y + TRACE_H) {
        return -1;
    }
    int meilleur = -1;
    int distance = 1 << 30;
    for (int i = 0; i < n; i++) {
        int d = x_releve(e, i) - etat.souris_x;
        d = d < 0 ? -d : d;
        if (d < distance) {
            distance = d;
            meilleur = i;
        }
    }
    return meilleur;
}

static void dessiner_legende(int moi)
{
    int x = CADRE_X + CADRE_L - 12;
    for (int v = 1; v >= 0; v--) {
        char l[80];
        snprintf(l, sizeof(l), "%s%s", etiquette(v), v == moi ? " (vous)" : "");
        int w = text_get_width(t(l), FONT_SMALL_PLAIN);
        x -= w;
        text_draw(t(l), x, CADRE_Y + 9, FONT_SMALL_PLAIN, ENCRE);
        x -= 14;
        graphics_fill_rect(x, CADRE_Y + 9, 10, 10, COULEUR_VILLE[v]);
        x -= 16;
    }
}

static void dessiner_courbes(int moi)
{
    const description_onglet *o = &ONGLETS[etat.onglet];
    text_draw(t(o->titre), CADRE_X + 10, CADRE_Y + 8, FONT_NORMAL_BLACK, 0);
    dessiner_legende(moi);

    int n = brutus_recap_nombre();
    if (n < 2) {
        text_draw_centered(t("Pas assez de relevés pour tracer une courbe (un relevé par mois)."),
            CADRE_X, CADRE_Y + 100, CADRE_L, FONT_NORMAL_BLACK, 0);
        return;
    }
    // Échelle verticale : de 0 (ou du minimum s'il est négatif) au maximum, graduations rondes
    int mini = 0, maxi = 0;
    for (int i = 0; i < n; i++) {
        for (int v = 0; v < 2; v++) {
            int x = brutus_recap_valeur(i, v, o->stat);
            if (x < mini) {
                mini = x;
            }
            if (x > maxi) {
                maxi = x;
            }
        }
    }
    if (o->stat == BRUTUS_RECAP_PROSPERITE || o->stat == BRUTUS_RECAP_CULTURE) {
        maxi = 100;
    }
    if (maxi == mini) {
        maxi = mini + 4;
    }
    int pas = pas_rond(maxi - mini);
    echelle e;
    e.bas = plancher(mini, pas);
    e.haut = plafond(maxi, pas);
    if (e.haut == e.bas) {
        e.haut = e.bas + pas;
    }
    e.tick0 = brutus_recap_tick(0);
    e.duree = brutus_recap_tick(n - 1) - e.tick0;

    // Grille et graduations (discrètes)
    char nb[24];
    for (int g = e.bas; g <= e.haut; g += pas) {
        int y = y_valeur(&e, g);
        graphics_draw_line(TRACE_X, TRACE_X + TRACE_L - 1, y, y, g == 0 ? BORD_COURBES : GRILLE);
        nombre(g, nb, sizeof(nb));
        text_draw_right_aligned(t(nb), CADRE_X + 4, y - 4, TRACE_X - CADRE_X - 10, FONT_SMALL_PLAIN, ENCRE_DOUCE);
    }
    // Dates : début, fin, et le milieu s'il y a la place
    char d[40];
    date_releve(0, d, sizeof(d));
    text_draw(t(d), TRACE_X, TRACE_Y + TRACE_H + 6, FONT_SMALL_PLAIN, ENCRE_DOUCE);
    date_releve(n - 1, d, sizeof(d));
    text_draw_right_aligned(t(d), TRACE_X, TRACE_Y + TRACE_H + 6, TRACE_L, FONT_SMALL_PLAIN, ENCRE_DOUCE);
    if (n >= 5) {
        date_releve(n / 2, d, sizeof(d));
        int xm = x_releve(&e, n / 2);
        graphics_draw_line(xm, xm, TRACE_Y + TRACE_H, TRACE_Y + TRACE_H + 3, BORD_COURBES);
        text_draw_centered(t(d), xm - 60, TRACE_Y + TRACE_H + 6, 120, FONT_SMALL_PLAIN, ENCRE_DOUCE);
    }
    graphics_draw_line(TRACE_X, TRACE_X, TRACE_Y, TRACE_Y + TRACE_H - 1, BORD_COURBES);

    // Les deux courbes (la ville du joueur dessinée en dernier, au-dessus)
    for (int k = 0; k < 2; k++) {
        int v = k == 0 ? 1 - moi : moi;
        for (int i = 1; i < n; i++) {
            trait_epais(x_releve(&e, i - 1), y_valeur(&e, brutus_recap_valeur(i - 1, v, o->stat)) - 1,
                x_releve(&e, i), y_valeur(&e, brutus_recap_valeur(i, v, o->stat)) - 1, COULEUR_VILLE[v]);
        }
    }

    // Valeurs à la fin, à droite des courbes (texte à l'encre, repère de couleur)
    int y_fin[2];
    for (int v = 0; v < 2; v++) {
        y_fin[v] = y_valeur(&e, brutus_recap_valeur(n - 1, v, o->stat)) - 5;
    }
    if (y_fin[0] > y_fin[1] - 12 && y_fin[0] < y_fin[1] + 12) { // trop proches : on les écarte
        int milieu = (y_fin[0] + y_fin[1]) / 2;
        int haut = brutus_recap_valeur(n - 1, 0, o->stat) >= brutus_recap_valeur(n - 1, 1, o->stat) ? 0 : 1;
        y_fin[haut] = milieu - 6;
        y_fin[1 - haut] = milieu + 6;
    }
    for (int v = 0; v < 2; v++) {
        graphics_fill_rect(TRACE_X + TRACE_L + 6, y_fin[v] + 2, 6, 6, COULEUR_VILLE[v]);
        nombre(brutus_recap_valeur(n - 1, v, o->stat), nb, sizeof(nb));
        text_draw(t(nb), TRACE_X + TRACE_L + 15, y_fin[v], FONT_SMALL_PLAIN, ENCRE);
    }

    // Survol : repère vertical, points et bulle avec la date et les deux valeurs
    int i = releve_sous_souris(&e);
    if (i < 0) {
        return;
    }
    int x = x_releve(&e, i);
    graphics_draw_line(x, x, TRACE_Y, TRACE_Y + TRACE_H - 1, ENCRE_DOUCE);
    for (int v = 0; v < 2; v++) {
        int y = y_valeur(&e, brutus_recap_valeur(i, v, o->stat));
        graphics_fill_rect(x - 4, y - 4, 8, 8, FOND_COURBES);
        graphics_fill_rect(x - 3, y - 3, 6, 6, COULEUR_VILLE[v]);
    }
    char lignes[3][100];
    date_releve(i, d, sizeof(d));
    snprintf(lignes[0], sizeof(lignes[0]), "%s%s", d, brutus_recap_est_fin(i) ? " (fin)" : "");
    for (int v = 0; v < 2; v++) {
        nombre(brutus_recap_valeur(i, v, o->stat), nb, sizeof(nb));
        snprintf(lignes[v + 1], sizeof(lignes[v + 1]), "%s : %s", etiquette(v), nb);
    }
    int largeur = 0;
    for (int l = 0; l < 3; l++) {
        int w = text_get_width(t(lignes[l]), FONT_SMALL_PLAIN) + (l ? 14 : 0);
        largeur = w > largeur ? w : largeur;
    }
    largeur += 14;
    int bx = x + 10 + largeur > TRACE_X + TRACE_L ? x - 10 - largeur : x + 10;
    int by = TRACE_Y + 4;
    graphics_fill_rect(bx, by, largeur, 44, 0xfffffdf6);
    graphics_draw_rect(bx, by, largeur, 44, BORD_COURBES);
    text_draw(t(lignes[0]), bx + 7, by + 5, FONT_SMALL_PLAIN, ENCRE);
    for (int v = 0; v < 2; v++) {
        graphics_fill_rect(bx + 7, by + 19 + v * 12, 8, 8, COULEUR_VILLE[v]);
        text_draw(t(lignes[v + 1]), bx + 21, by + 18 + v * 12, FONT_SMALL_PLAIN, ENCRE);
    }
}

// ------------------------------------------------------------------ tableau

static void ligne_tableau(int y, const char *libelle, int v1, int v2, int gras)
{
    char n[24];
    font_t police = gras ? FONT_NORMAL_BLACK : FONT_NORMAL_PLAIN;
    color_t c = gras ? 0 : ENCRE;
    text_draw(t(libelle), CADRE_X + 40, y, police, c);
    nombre(v1, n, sizeof(n));
    text_draw_right_aligned(t(n), CADRE_X + 250, y, 120, police, c);
    nombre(v2, n, sizeof(n));
    text_draw_right_aligned(t(n), CADRE_X + 410, y, 120, police, c);
}

static void dessiner_tableau(int moi)
{
    // Scores au moment de la fin ; le reste vient du dernier relevé du récap
    const brutus_score_ville *s1 = brutus_victoire_scores_fin(0);
    const brutus_score_ville *s2 = brutus_victoire_scores_fin(1);
    for (int v = 0; v < 2; v++) {
        char tete[80];
        snprintf(tete, sizeof(tete), "%s%s", etiquette(v), v == moi ? " (vous)" : "");
        int x = CADRE_X + 250 + v * 160;
        int w = text_get_width(t(tete), FONT_NORMAL_BLACK);
        graphics_fill_rect(x + 120 - w - 14, CADRE_Y + 13, 10, 10, COULEUR_VILLE[v]);
        text_draw(t(tete), x + 120 - w, CADRE_Y + 12, FONT_NORMAL_BLACK, 0);
    }
    graphics_draw_line(CADRE_X + 30, CADRE_X + CADRE_L - 30, CADRE_Y + 30, CADRE_Y + 30, BORD_COURBES);
    int y = CADRE_Y + 36;
    ligne_tableau(y, "Population", s1->population, s2->population, 0);
    ligne_tableau(y += 16, "Trésor", s1->tresor, s2->tresor, 0);
    ligne_tableau(y += 16, "Prospérité", s1->prosperite, s2->prosperite, 0);
    ligne_tableau(y += 16, "Culture", s1->culture, s2->culture, 0);
    ligne_tableau(y += 18, "Score", s1->score, s2->score, 1);
    int d = dernier();
    if (d >= 0) {
        y += 24;
        ligne_tableau(y, "Soldats", brutus_recap_valeur(d, 0, BRUTUS_RECAP_SOLDATS),
            brutus_recap_valeur(d, 1, BRUTUS_RECAP_SOLDATS), 0);
        ligne_tableau(y += 16, "Bâtiments construits", brutus_recap_valeur(d, 0, BRUTUS_RECAP_CONSTRUITS),
            brutus_recap_valeur(d, 1, BRUTUS_RECAP_CONSTRUITS), 0);
        ligne_tableau(y += 16, "Bâtiments perdus", brutus_recap_valeur(d, 0, BRUTUS_RECAP_PERDUS),
            brutus_recap_valeur(d, 1, BRUTUS_RECAP_PERDUS), 0);
        ligne_tableau(y += 16, "Soldats tués", brutus_recap_valeur(d, 0, BRUTUS_RECAP_TUES),
            brutus_recap_valeur(d, 1, BRUTUS_RECAP_TUES), 0);
    }
    text_draw(t("Score = population + trésor / 10 + 25 x (prospérité + culture)"), CADRE_X + 40,
        CADRE_Y + CADRE_H - 14, FONT_SMALL_PLAIN, ENCRE_DOUCE);
}

// ------------------------------------------------------------------ fenêtre

static void dessiner_onglets(void)
{
    for (int i = 0; i < BRUTUS_FIN_ONGLETS; i++) {
        const generic_button *b = &boutons[2 + i];
        int choisi = i == etat.onglet;
        if (choisi) {
            graphics_fill_rect(b->x, b->y, b->width, b->height + 4, FOND_COURBES);
            graphics_draw_rect(b->x, b->y, b->width, b->height + 4, BORD_COURBES);
        } else {
            button_border_draw(b->x, b->y, b->width, b->height, etat.focus == (unsigned int) (3 + i));
        }
        text_draw_centered(t(ONGLETS[i].onglet), b->x, b->y + 6, b->width, FONT_SMALL_PLAIN,
            choisi ? ENCRE : COLOR_BLACK);
    }
}

static void draw_foreground(void)
{
    graphics_in_dialog();
    outer_panel_draw(X0, Y0, LARGEUR / 16, HAUTEUR / 16);

    int moi = brutus_ville_vue();
    int fin = brutus_victoire_fin();
    int gagnant = brutus_victoire_gagnant();
    const char *titre = gagnant < 0 ? "Égalité" : gagnant == moi ? "Victoire !" : "Défaite";
    text_draw_centered(t(titre), X0, Y0 + 14, LARGEUR, FONT_LARGE_BLACK, 0);

    char ligne[TEXTE_MAX];
    if (fin == BRUTUS_FIN_MILITAIRE) {
        if (gagnant < 0) {
            snprintf(ligne, sizeof(ligne), "Les deux villes ont été entièrement détruites.");
        } else if (gagnant == moi) {
            snprintf(ligne, sizeof(ligne), "Vos soldats ont entièrement détruit %s.", la_ville(1 - moi));
        } else {
            snprintf(ligne, sizeof(ligne), "Les soldats de %s ont entièrement détruit votre ville.",
                la_ville(gagnant));
        }
    } else {
        int mois = brutus_victoire_duree();
        char duree[40];
        if (mois % 12 == 0) {
            snprintf(duree, sizeof(duree), "%d an%s", mois / 12, mois >= 24 ? "s" : "");
        } else {
            snprintf(duree, sizeof(duree), "%d mois", mois);
        }
        if (gagnant < 0) {
            snprintf(ligne, sizeof(ligne), "Fin du temps imparti (%s) : les deux villes ont le même score.", duree);
        } else {
            snprintf(ligne, sizeof(ligne), "Fin du temps imparti (%s) : %s a la meilleure économie.", duree,
                la_ville(gagnant));
        }
    }
    text_draw_multiline(t(ligne), X0 + 32, Y0 + 48, LARGEUR - 64, 0, FONT_NORMAL_BLACK, 0);

    dessiner_onglets();
    graphics_fill_rect(CADRE_X, CADRE_Y, CADRE_L, CADRE_H, FOND_COURBES);
    graphics_draw_rect(CADRE_X, CADRE_Y, CADRE_L, CADRE_H, BORD_COURBES);
    // l'onglet choisi s'ouvre sur le cadre
    const generic_button *o = &boutons[2 + etat.onglet];
    graphics_draw_line(o->x + 1, o->x + o->width - 2, CADRE_Y, CADRE_Y, FOND_COURBES);
    if (etat.onglet == BRUTUS_FIN_ONGLET_TABLEAU) {
        dessiner_tableau(moi);
    } else {
        dessiner_courbes(moi);
    }

    // Faits marquants : libellé, puis la valeur
    text_draw(t("Faits marquants"), X0 + 24, FAITS_Y, FONT_NORMAL_BLACK, 0);
    static const char *libelles[5] = { "Durée", "Pic de population", "Plus grosse bataille",
        "Premier bâtiment détruit", "Bascule du score" };
    void (*faits[5])(char *, size_t) = { fait_duree, fait_pic_population, fait_bataille, fait_premier_detruit,
        fait_bascule };
    for (int i = 0; i < 5; i++) {
        int y = FAITS_Y + 19 + i * 15;
        text_draw(t(libelles[i]), X0 + 32, y, FONT_SMALL_PLAIN, ENCRE_DOUCE);
        faits[i](ligne, sizeof(ligne));
        text_draw_ellipsized(t(ligne), X0 + 200, y, LARGEUR - 224, FONT_NORMAL_PLAIN, ENCRE);
    }

    for (int i = 0; i < 2; i++) {
        button_border_draw(boutons[i].x, boutons[i].y, boutons[i].width, boutons[i].height, etat.focus == (unsigned int) (i + 1));
        text_draw_centered(t(i ? "Menu principal" : "Continuer à jouer"), boutons[i].x, boutons[i].y + 7,
            boutons[i].width, FONT_NORMAL_BLACK, 0);
    }
    graphics_reset_dialog();
}

static void handle_input(const mouse *m, const hotkeys *h)
{
    const mouse *md = mouse_in_dialog(m);
    if (md->x != etat.souris_x || md->y != etat.souris_y) {
        etat.souris_x = md->x;
        etat.souris_y = md->y;
        window_invalidate();
    }
    if (generic_buttons_handle_mouse(md, 0, 0, boutons, 2 + BRUTUS_FIN_ONGLETS, &etat.focus)) {
        return;
    }
    if (input_go_back_requested(m, h)) {
        window_city_show();
    }
}

static void bouton_onglet(const generic_button *b)
{
    brutus_fin_versus_onglet(b->parameter1);
}

static void bouton_continuer(const generic_button *b)
{
    window_city_show();
}

static void bouton_menu(const generic_button *b)
{
    if (brutus_reseau_menu_refuse()) { // en réseau, on quitte la partie par la page
        window_city_show();
        return;
    }
    building_construction_clear_type();
    game_undo_disable();
    game_state_reset_overlay();
    window_main_menu_show(1);
}

void brutus_fin_versus_onglet(int onglet)
{
    if (onglet >= 0 && onglet < BRUTUS_FIN_ONGLETS) {
        etat.onglet = onglet;
        window_invalidate();
    }
}

void brutus_fin_versus_survol(int x, int y)
{
    etat.souris_x = x;
    etat.souris_y = y;
    window_invalidate();
}

void window_brutus_fin_versus_show(void)
{
    if (brutus_victoire_fin() == BRUTUS_FIN_AUCUNE) {
        return;
    }
    // Largeur des onglets : celle du libellé, puis la place qui reste partagée également
    int largeurs[BRUTUS_FIN_ONGLETS];
    int total = 0;
    for (int i = 0; i < BRUTUS_FIN_ONGLETS; i++) {
        largeurs[i] = text_get_width(t(ONGLETS[i].onglet), FONT_SMALL_PLAIN) + 8;
        total += largeurs[i];
    }
    int reste = ONGLETS_L - total - (BRUTUS_FIN_ONGLETS - 1);
    int x = ONGLET_X;
    for (int i = 0; i < BRUTUS_FIN_ONGLETS; i++) {
        generic_button *b = &boutons[2 + i];
        int ajout = reste > 0 ? reste / BRUTUS_FIN_ONGLETS + (i < reste % BRUTUS_FIN_ONGLETS) : 0;
        b->x = x;
        b->y = ONGLET_Y;
        b->width = largeurs[i] + ajout;
        b->height = ONGLET_H;
        b->left_click_handler = bouton_onglet;
        b->parameter1 = i;
        x += b->width + 1;
    }
    window_type fenetre = {
        WINDOW_BRUTUS_FIN_VERSUS,
        window_draw_underlying_window,
        draw_foreground,
        handle_input
    };
    etat.focus = 0;
    etat.onglet = BRUTUS_FIN_ONGLET_SCORE;
    etat.souris_x = -1;
    etat.souris_y = -1;
    window_show(&fenetre);
}

void brutus_fin_versus_verifier(void)
{
    if (brutus_victoire_fin() == BRUTUS_FIN_AUCUNE) {
        return;
    }
    uint32_t tick = brutus_victoire_tick_fin();
    if (etat.affichee && etat.tick_affiche == tick) {
        return;
    }
    if (!window_is(WINDOW_CITY)) {
        return; // un message ou une fenêtre est ouvert : l'écran de fin viendra après
    }
    etat.affichee = 1;
    etat.tick_affiche = tick;
    building_construction_clear_type();
    if (brutus_victoire_gagnant() == brutus_ville_vue()) {
        sound_speech_play_file("wavs/fanfare_nu2.wav");
    }
    window_brutus_fin_versus_show();
}
