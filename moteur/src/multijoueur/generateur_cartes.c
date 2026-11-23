// Brutus III (M9) : cartes Versus créées par programme (voir generateur_cartes.h).
#include "multijoueur/generateur_cartes.h"

#include "core/dir.h"
#include "core/encoding.h"
#include "core/file.h"
#include "core/log.h"
#include "editor/editor.h"
#include "game/file_editor.h"
#include "game/game.h"
#include "map/data.h"
#include "map/grid.h"
#include "map/image_context.h"
#include "map/routing_terrain.h"
#include "map/terrain.h"
#include "map/tiles.h"
#include "multijoueur/carte_versus.h"
#include "scenario/editor.h"
#include "scenario/editor_map.h"
#include "scenario/property.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TAILLE_INDEX 3 // 100 x 100 (scenario/editor.c, MAP_SIZES)
#define PI 3.14159265358979

typedef enum {
    T_HERBE,
    T_EAU,
    T_ARBRE,
    T_BUISSON,
    T_ROCHER,
    T_PRE,
    T_ROUTE
} type_case;

typedef struct {
    int x, y;
} point;

// Ce qui décrit une carte (le joueur 2 est le symétrique du joueur 1)
typedef struct {
    const char *nom_court;
    const char *fichier;
    const char *description;
    int miroir_x; // 1 : symétrie gauche-droite ; 0 : symétrie par rapport au centre
    point entree1;
    point coude1; // la route de Rome du joueur 1 : entrée → coude → sortie (deux segments droits)
    point sortie1;
    point centre1; // le cœur de la ville 1 : terrain dégagé autour
    int rayon_degage;
    point peche[2]; // points de pêche du joueur 1 (le symétrique va au joueur 2)
    point troupeau[2];
    int a_fleuve; // entrée et sortie des navires
    point fleuve_entree;
    point fleuve_sortie;
} modele_carte;

static const modele_carte MODELES[BRUTUS_CARTE_NOMBRE] = {
    {
        "deux-rives", "Brutus - Deux rives.mapx",
        "Versus : deux rives d'un fleuve, un gué au centre.",
        0,
        { 0, 70 }, { 28, 70 }, { 28, 99 }, { 18, 84 }, 13,
        { { 30, 26 }, { 18, 15 } },
        { { 6, 45 }, { 44, 92 } },
        0, { 0, 0 }, { 0, 0 }
    },
    {
        "lac", "Brutus - Lac central.mapx",
        "Versus : une plaine, un lac au centre, des forêts entre les deux villes.",
        0,
        { 0, 30 }, { 25, 30 }, { 25, 99 }, { 16, 52 }, 14,
        { { 42, 50 }, { 44, 42 } },
        { { 8, 90 }, { 40, 12 } },
        0, { 0, 0 }, { 0, 0 }
    },
    {
        "rivage", "Brutus - Rivage.mapx",
        "Versus : une côte au sud, ouverte aux navires, des collines entre les deux villes.",
        1,
        { 0, 58 }, { 26, 58 }, { 26, 0 }, { 20, 64 }, 14,
        { { 18, 90 }, { 34, 92 } },
        { { 6, 12 }, { 38, 20 } },
        1, { 0, 95 }, { 99, 95 }
    },
};

static struct {
    int largeur;
    int hauteur;
    const modele_carte *m;
    uint8_t cases[100 * 100];
} gen;

// ------------------------------------------------------------------ symétrie et bruit

static point symetrique(point p)
{
    point s;
    s.x = gen.largeur - 1 - p.x;
    s.y = gen.m->miroir_x ? p.y : gen.hauteur - 1 - p.y;
    return s;
}

static uint32_t hacher(uint32_t x, uint32_t y, uint32_t graine)
{
    uint32_t h = x * 374761393u + y * 668265263u + graine * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// Bruit lisse (valeurs sur une grille de pas `pas`, interpolées), entre 0 et 1
static double bruit_brut(double x, double y, int pas, uint32_t graine)
{
    int gx = (int) floor(x / pas);
    int gy = (int) floor(y / pas);
    double fx = x / pas - gx;
    double fy = y / pas - gy;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    double v00 = (hacher(gx + 50, gy + 50, graine) & 0xffff) / 65535.0;
    double v10 = (hacher(gx + 51, gy + 50, graine) & 0xffff) / 65535.0;
    double v01 = (hacher(gx + 50, gy + 51, graine) & 0xffff) / 65535.0;
    double v11 = (hacher(gx + 51, gy + 51, graine) & 0xffff) / 65535.0;
    double a = v00 + (v10 - v00) * fx;
    double b = v01 + (v11 - v01) * fx;
    return a + (b - a) * fy;
}

// Bruit symétrique : même valeur sur une case et sur sa symétrique
static double bruit(int x, int y, int pas, uint32_t graine)
{
    point p = { x, y };
    point s = symetrique(p);
    double v = 0.6 * bruit_brut(x, y, pas, graine) + 0.4 * bruit_brut(x, y, pas / 2 + 1, graine + 7);
    double w = 0.6 * bruit_brut(s.x, s.y, pas, graine) + 0.4 * bruit_brut(s.x, s.y, pas / 2 + 1, graine + 7);
    return (v + w) / 2;
}

static double distance(int x, int y, point p)
{
    double dx = x - p.x;
    double dy = y - p.y;
    return sqrt(dx * dx + dy * dy);
}

// Distance au plus proche des deux centres de ville
static double distance_ville(int x, int y)
{
    double a = distance(x, y, gen.m->centre1);
    double b = distance(x, y, symetrique(gen.m->centre1));
    return a < b ? a : b;
}

// ------------------------------------------------------------------ terrain de chaque modèle

static int eau_deux_rives(int x, int y)
{
    double d = x - y;
    double c = x + y - (gen.largeur - 1);
    double meandre = 6 * sin(c * 2 * PI / 70);
    return fabs(d - meandre) <= 2.4 && fabs(c) > 4; // gué de 9 cases au centre
}

static int rive_deux_rives(int x, int y)
{
    double d = x - y;
    double c = x + y - (gen.largeur - 1);
    double meandre = 6 * sin(c * 2 * PI / 70);
    return fabs(d - meandre) <= 7;
}

static int eau_lac(int x, int y)
{
    double cx = (gen.largeur - 1) / 2.0;
    double cy = (gen.hauteur - 1) / 2.0;
    double dx = x - cx;
    double dy = y - cy;
    double r = sqrt(dx * dx + dy * dy);
    double angle = atan2(dy, dx);
    return r <= 10 + 2 * sin(2 * angle); // sin(2a) : même valeur à l'opposé, le lac reste symétrique
}

static int eau_cote(int x, int y)
{
    double vague = 2 * cos((x - (gen.largeur - 1) / 2.0) * 2 * PI / 30);
    return y >= 82 + vague;
}

static int eau(int x, int y)
{
    switch ((int) (gen.m - MODELES)) {
        case BRUTUS_CARTE_DEUX_RIVES: return eau_deux_rives(x, y);
        case BRUTUS_CARTE_LAC: return eau_lac(x, y);
        default: return eau_cote(x, y);
    }
}

// Bande de collines (rochers) et de forêts entre les deux villes, avec des passages
static int crete(int x, int y, int *rocher)
{
    *rocher = 0;
    int modele = (int) (gen.m - MODELES);
    if (modele == BRUTUS_CARTE_COTE) {
        double milieu = (gen.largeur - 1) / 2.0;
        double ecart = fabs(x - milieu);
        int passage = (y >= 30 && y <= 35) || (y >= 58 && y <= 63);
        if (ecart <= 5 && !passage && y < 80) {
            *rocher = ecart <= 2 && bruit(x, y, 6, 11) > 0.35;
            return 1;
        }
    } else if (modele == BRUTUS_CARTE_LAC) {
        // forêt en arc autour du lac, ouverte vers chaque ville
        double cx = (gen.largeur - 1) / 2.0;
        double cy = (gen.hauteur - 1) / 2.0;
        double r = sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
        if (r >= 15 && r <= 22 && fabs(x - cx) < 14) {
            *rocher = r >= 20 && bruit(x, y, 5, 12) > 0.62;
            return 1;
        }
    }
    return 0;
}

static int sur_route(int x, int y)
{
    for (int joueur = 0; joueur < 2; joueur++) {
        point e = gen.m->entree1, c = gen.m->coude1, s = gen.m->sortie1;
        if (joueur) {
            e = symetrique(e);
            c = symetrique(c);
            s = symetrique(s);
        }
        // segment entrée → coude, puis coude → sortie (chacun horizontal ou vertical)
        point a[2] = { e, c };
        point b[2] = { c, s };
        for (int k = 0; k < 2; k++) {
            int x0 = a[k].x < b[k].x ? a[k].x : b[k].x, x1 = a[k].x < b[k].x ? b[k].x : a[k].x;
            int y0 = a[k].y < b[k].y ? a[k].y : b[k].y, y1 = a[k].y < b[k].y ? b[k].y : a[k].y;
            if (x >= x0 && x <= x1 && y >= y0 && y <= y1) {
                return 1;
            }
        }
    }
    return 0;
}

static type_case choisir(int x, int y)
{
    if (sur_route(x, y)) {
        return T_ROUTE;
    }
    if (eau(x, y)) {
        return T_EAU;
    }
    double dv = distance_ville(x, y);
    int rocher = 0;
    if (crete(x, y, &rocher) && dv > gen.m->rayon_degage) {
        return rocher ? T_ROCHER : T_ARBRE;
    }
    int modele = (int) (gen.m - MODELES);
    if (dv <= gen.m->rayon_degage) {
        // le cœur de la ville : dégagé, quelques prés pour les fermes
        return bruit(x, y, 7, 3) > 0.7 ? T_PRE : T_HERBE;
    }
    if (modele == BRUTUS_CARTE_DEUX_RIVES && rive_deux_rives(x, y)) {
        return bruit(x, y, 6, 4) > 0.35 ? T_PRE : T_HERBE;
    }
    if (modele == BRUTUS_CARTE_COTE && y >= 72) {
        return bruit(x, y, 6, 4) > 0.45 ? T_PRE : T_HERBE; // bord de mer fertile
    }
    double foret = bruit(x, y, 10, 1);
    double pierre = bruit(x, y, 7, 2);
    // carrières et mines : petits massifs, entre 18 et 35 cases du centre de la ville
    if (pierre > 0.72 && dv > 17 && dv < 36) {
        return T_ROCHER;
    }
    if (foret > 0.6) {
        return T_ARBRE;
    }
    if (foret > 0.54) {
        return T_BUISSON;
    }
    if (bruit(x, y, 9, 5) > 0.68) {
        return T_PRE;
    }
    return T_HERBE;
}

// ------------------------------------------------------------------ écriture dans la carte

static int bits_terrain(type_case t)
{
    switch (t) {
        case T_EAU: return TERRAIN_WATER;
        case T_ARBRE: return TERRAIN_TREE;
        case T_BUISSON: return TERRAIN_SHRUB;
        case T_ROCHER: return TERRAIN_ROCK;
        case T_PRE: return TERRAIN_MEADOW;
        case T_ROUTE: return TERRAIN_ROAD;
        default: return TERRAIN_CLEAR;
    }
}

static void rafraichir_images(void)
{
    int xm = gen.largeur - 1;
    int ym = gen.hauteur - 1;
    map_image_context_reset_water();
    map_tiles_update_all_elevation_editor();
    map_tiles_update_all_water();
    map_tiles_update_all_marshland();
    map_tiles_update_all_beach();
    map_tiles_update_all_rocks();
    map_tiles_update_region_trees(0, 0, xm, ym);
    map_tiles_update_region_shrub(0, 0, xm, ym);
    map_tiles_update_all_empty_land();
    map_tiles_update_all_meadow();
    map_tiles_update_all_roads();
    map_routing_update_all();
}

static void poser_points(void)
{
    const modele_carte *m = gen.m;
    scenario_editor_set_entry_point(m->entree1.x, m->entree1.y);
    scenario_editor_set_exit_point(m->sortie1.x, m->sortie1.y);
    point e2 = symetrique(m->entree1);
    point s2 = symetrique(m->sortie1);
    brutus_carte_versus_definir_entree(1, e2.x, e2.y);
    brutus_carte_versus_definir_sortie(1, s2.x, s2.y);
    for (int i = 0; i < 2; i++) {
        point p = m->peche[i];
        point q = symetrique(p);
        scenario_editor_set_fishing_point(2 * i, p.x, p.y);
        scenario_editor_set_fishing_point(2 * i + 1, q.x, q.y);
        p = m->troupeau[i];
        q = symetrique(p);
        scenario_editor_set_herd_point(2 * i, p.x, p.y);
        scenario_editor_set_herd_point(2 * i + 1, q.x, q.y);
    }
    if (m->a_fleuve) {
        scenario_editor_set_river_entry_point(m->fleuve_entree.x, m->fleuve_entree.y);
        scenario_editor_set_river_exit_point(m->fleuve_sortie.x, m->fleuve_sortie.y);
    }
}

// Les points de pêche doivent être dans l'eau, les troupeaux sur la terre : on vérifie
static int verifier(void)
{
    int ok = 1;
    for (int i = 0; i < 2; i++) {
        point p = gen.m->peche[i];
        if (gen.cases[p.y * gen.largeur + p.x] != T_EAU) {
            log_error("Brutus III (M9) : point de pêche hors de l'eau, n°", 0, i);
            ok = 0;
        }
        p = gen.m->troupeau[i];
        if (gen.cases[p.y * gen.largeur + p.x] == T_EAU) {
            log_error("Brutus III (M9) : troupeau dans l'eau, n°", 0, i);
            ok = 0;
        }
    }
    // symétrie parfaite
    for (int y = 0; y < gen.hauteur; y++) {
        for (int x = 0; x < gen.largeur; x++) {
            point p = { x, y };
            point s = symetrique(p);
            if (gen.cases[y * gen.largeur + x] != gen.cases[s.y * gen.largeur + s.x]) {
                log_error("Brutus III (M9) : carte non symétrique en x =", 0, x);
                return 0;
            }
        }
    }
    return ok;
}

const char *brutus_carte_versus_nom_fichier(int modele)
{
    return modele >= 0 && modele < BRUTUS_CARTE_NOMBRE ? MODELES[modele].fichier : 0;
}

int brutus_carte_versus_modele(const char *nom)
{
    for (int i = 0; i < BRUTUS_CARTE_NOMBRE; i++) {
        if (strcmp(nom, MODELES[i].nom_court) == 0) {
            return i;
        }
    }
    if (nom[0] >= '0' && nom[0] < '0' + BRUTUS_CARTE_NOMBRE && !nom[1]) {
        return nom[0] - '0';
    }
    return -1;
}

int brutus_generer_carte_versus(int modele, const char *fichier)
{
    if (modele < 0 || modele >= BRUTUS_CARTE_NOMBRE) {
        return 0;
    }
    if (!game_init_editor()) {
        log_error("Brutus III (M9) : éditeur indisponible", 0, 0);
        return 0;
    }
    game_file_editor_create_scenario(TAILLE_INDEX);
    gen.m = &MODELES[modele];
    gen.largeur = map_data.width;
    gen.hauteur = map_data.height;
    if (gen.largeur != 100 || gen.hauteur != 100) {
        log_error("Brutus III (M9) : taille de carte inattendue", 0, gen.largeur);
        return 0;
    }
    for (int y = 0; y < gen.hauteur; y++) {
        for (int x = 0; x < gen.largeur; x++) {
            type_case t = choisir(x, y);
            gen.cases[y * gen.largeur + x] = (uint8_t) t;
            map_terrain_set(map_grid_offset(x, y), bits_terrain(t));
        }
    }
    if (!verifier()) {
        return 0;
    }
    rafraichir_images();
    poser_points();

    static uint8_t texte[200];
    encoding_from_utf8(gen.m->description, texte, sizeof(texte));
    scenario_editor_update_brief_description(texte);
    scenario_editor_set_initial_funds(4000);
    scenario_editor_set_rescue_loan(2000);
    scenario_editor_set_start_year(-350);
    if (!scenario_is_open_play()) {
        scenario_editor_toggle_open_play(); // Versus : la victoire est celle du mode (M11), pas du scénario
    }
    if (!game_file_editor_write_scenario(fichier)) {
        log_error("Brutus III (M9) : écriture impossible", fichier, 0);
        return 0;
    }
    printf("Brutus III : carte Versus « %s » écrite : %s\n", gen.m->nom_court, fichier);
    return 1;
}

void brutus_cartes_versus_installer(void)
{
    for (int i = 0; i < BRUTUS_CARTE_NOMBRE; i++) {
        const char *nom = MODELES[i].fichier;
        if (dir_get_file_at_location(nom, PATH_LOCATION_SCENARIO)) {
            continue; // déjà là (peut-être retouchée dans l'éditeur : on n'y touche pas)
        }
        char asset[256];
        snprintf(asset, sizeof(asset), "Brutus/cartes/%s", nom);
        FILE *source = file_open_asset(asset, "rb");
        if (!source) {
            log_error("Brutus III (M9) : carte Versus absente des assets :", nom, 0);
            continue;
        }
        char destination[FILE_NAME_MAX];
        snprintf(destination, sizeof(destination), "%s", dir_append_location(nom, PATH_LOCATION_SCENARIO));
        FILE *sortie = file_open(destination, "wb");
        if (sortie) {
            uint8_t tampon[16384];
            size_t lus;
            while ((lus = fread(tampon, 1, sizeof(tampon), source)) > 0) {
                fwrite(tampon, 1, lus, sortie);
            }
            file_close(sortie);
            log_info("Brutus III (M9) : carte Versus installée :", destination, 0);
        }
        file_close(source);
    }
}
