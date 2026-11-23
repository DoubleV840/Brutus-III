// Brutus III (M9) : points d'entrée et de sortie de chaque joueur (voir carte_versus.h).
#include "multijoueur/carte_versus.h"

#include "city/map.h"
#include "core/file.h"
#include "core/log.h"
#include "multijoueur/versus.h"
#include "scenario/map.h"

#include <string.h>

#define VERSION_CARTE 1

static const char MARQUE[4] = { 'B', 'R', '3', 'C' };

static struct {
    map_point entree[BRUTUS_VILLES_MAX];
    map_point sortie[BRUTUS_VILLES_MAX];
} points;

static void effacer_points(void)
{
    for (int v = 0; v < BRUTUS_VILLES_MAX; v++) {
        points.entree[v].x = points.entree[v].y = -1;
        points.sortie[v].x = points.sortie[v].y = -1;
    }
}

static int initialise;

// Points lus dans le dernier fichier de carte ouvert (aussi pour une simple fiche d'information) :
// ils ne deviennent ceux de la partie qu'au vrai chargement (brutus_carte_versus_adopter_lu).
static struct {
    uint8_t octets[BRUTUS_CARTE_VERSUS_TAILLE];
    uint32_t taille;
} lu;

static void initialiser(void)
{
    if (!initialise) {
        effacer_points();
        initialise = 1;
    }
}

static int valide(map_point p)
{
    return p.x >= 0 && p.y >= 0;
}

int brutus_carte_versus_a_points(void)
{
    initialiser();
    for (int v = 1; v < BRUTUS_VILLES_MAX; v++) {
        if (valide(points.entree[v])) {
            return 1;
        }
    }
    return 0;
}

map_point brutus_carte_versus_entree(int ville)
{
    initialiser();
    if (ville <= 0) {
        return scenario_map_entry();
    }
    if (ville >= BRUTUS_VILLES_MAX) {
        map_point aucun = { -1, -1 };
        return aucun;
    }
    return points.entree[ville];
}

map_point brutus_carte_versus_sortie(int ville)
{
    initialiser();
    if (ville <= 0) {
        return scenario_map_exit();
    }
    if (ville >= BRUTUS_VILLES_MAX) {
        map_point aucun = { -1, -1 };
        return aucun;
    }
    // Sortie absente : la même que l'entrée (comme scenario_map_init_entry_exit)
    return valide(points.sortie[ville]) ? points.sortie[ville] : points.entree[ville];
}

map_point brutus_carte_versus_valeur_sortie(int ville)
{
    initialiser();
    if (ville < 1 || ville >= BRUTUS_VILLES_MAX) {
        map_point aucun = { -1, -1 };
        return aucun;
    }
    return points.sortie[ville];
}

void brutus_carte_versus_definir_entree(int ville, int x, int y)
{
    initialiser();
    if (ville >= 1 && ville < BRUTUS_VILLES_MAX) {
        points.entree[ville].x = x;
        points.entree[ville].y = y;
    }
}

void brutus_carte_versus_definir_sortie(int ville, int x, int y)
{
    initialiser();
    if (ville >= 1 && ville < BRUTUS_VILLES_MAX) {
        points.sortie[ville].x = x;
        points.sortie[ville].y = y;
    }
}

void brutus_carte_versus_effacer(void)
{
    initialise = 1;
    effacer_points();
}

void brutus_carte_versus_appliquer_aux_villes(void)
{
    int avant = brutus_ville_active();
    for (int v = 1; v < brutus_versus_villes(); v++) {
        map_point e = brutus_carte_versus_entree(v);
        if (!valide(e)) {
            continue; // carte sans points à nous : la ville garde ceux de la ville 1
        }
        map_point s = brutus_carte_versus_sortie(v);
        brutus_ville_activer(v);
        city_map_set_entry_point(e.x, e.y);
        city_map_set_exit_point(s.x, s.y);
    }
    brutus_ville_activer(avant);
}

// ------------------------------------------------------------------ données (partie et carte)

void brutus_carte_versus_ecrire(buffer *b)
{
    initialiser();
    buffer_write_u8(b, VERSION_CARTE);
    // Joueur 2 seulement (BRUTUS_VILLES_MAX = 2) : 4 entiers sur 32 bits
    buffer_write_i32(b, points.entree[1].x);
    buffer_write_i32(b, points.entree[1].y);
    buffer_write_i32(b, points.sortie[1].x);
    buffer_write_i32(b, points.sortie[1].y);
}

void brutus_carte_versus_lire(buffer *b)
{
    effacer_points();
    initialise = 1;
    if (buffer_read_u8(b) != VERSION_CARTE) {
        return;
    }
    points.entree[1].x = buffer_read_i32(b);
    points.entree[1].y = buffer_read_i32(b);
    points.sortie[1].x = buffer_read_i32(b);
    points.sortie[1].y = buffer_read_i32(b);
    if (b->overflow) {
        effacer_points();
    }
}

// ------------------------------------------------------------------ fichier de carte

void brutus_carte_versus_ecrire_fichier(FILE *fp)
{
    if (!brutus_carte_versus_a_points()) {
        return; // carte ordinaire : le fichier ne change pas
    }
    uint8_t d[BRUTUS_CARTE_VERSUS_TAILLE];
    buffer b;
    buffer_init(&b, d, sizeof(d));
    brutus_carte_versus_ecrire(&b);
    uint8_t taille[4] = { (uint8_t) b.index, 0, 0, 0 };
    fwrite(MARQUE, 1, sizeof(MARQUE), fp);
    fwrite(taille, 1, 4, fp);
    fwrite(d, 1, b.index, fp);
}

static void lire_octets(const uint8_t *d, size_t reste)
{
    lu.taille = 0;
    if (reste < 8 || memcmp(d, MARQUE, 4) != 0) {
        return;
    }
    uint32_t taille = d[4] | (d[5] << 8) | (d[6] << 16) | ((uint32_t) d[7] << 24);
    if (taille > reste - 8 || taille > sizeof(lu.octets)) {
        log_error("Brutus III (M9) : bloc de carte Versus illisible", 0, 0);
        return;
    }
    memcpy(lu.octets, d + 8, taille);
    lu.taille = taille;
}

int brutus_carte_versus_fichier_a_deux(const char *chemin)
{
    // Le bloc BR3C est tout à la fin du fichier : marque, taille, puis BRUTUS_CARTE_VERSUS_TAILLE octets
    FILE *fp = chemin ? file_open(chemin, "rb") : 0;
    if (!fp) {
        return 0;
    }
    uint8_t d[8 + BRUTUS_CARTE_VERSUS_TAILLE];
    int ok = fseek(fp, -(long) sizeof(d), SEEK_END) == 0 && fread(d, 1, sizeof(d), fp) == sizeof(d);
    file_close(fp);
    if (!ok || memcmp(d, MARQUE, 4) != 0 || d[4] != BRUTUS_CARTE_VERSUS_TAILLE || d[8] != VERSION_CARTE) {
        return 0;
    }
    buffer b;
    buffer_init(&b, d + 9, 8);
    int x = buffer_read_i32(&b);
    int y = buffer_read_i32(&b);
    return x >= 0 && y >= 0;
}

void brutus_carte_versus_lire_fichier(FILE *fp)
{
    uint8_t d[8 + 64];
    size_t lus = fread(d, 1, sizeof(d), fp);
    lire_octets(d, lus);
}

void brutus_carte_versus_lire_tampon(buffer *buf)
{
    size_t reste = buf->size > buf->index ? buf->size - buf->index : 0;
    lire_octets(buf->data + buf->index, reste);
}

void brutus_carte_versus_adopter_lu(void)
{
    if (!lu.taille) {
        brutus_carte_versus_effacer();
        return;
    }
    buffer b;
    buffer_init(&b, lu.octets, lu.taille);
    brutus_carte_versus_lire(&b);
    if (brutus_carte_versus_a_points()) {
        log_info("Brutus III (M9) : carte Versus, points du joueur 2", 0, 0);
    }
}
