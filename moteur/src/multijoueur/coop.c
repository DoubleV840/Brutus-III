// Brutus III (M7) : mode Coop, ce que chaque joueur voit des autres (voir coop.h).
#include "multijoueur/coop.h"

#include "city/view.h"
#include "city/warning.h"
#include "core/encoding.h"
#include "core/image.h"
#include "core/image_group.h"
#include "core/time.h"
#include "graphics/image.h"
#include "map/grid.h"
#include "map/terrain.h"
#include "multijoueur/commande.h"
#include "widget/city/city.h"

#include <string.h>

#define DUREE_SIGNAL 8000

typedef struct {
    int connue;
    int camera_x;
    int camera_y;
    int largeur;
    int hauteur;
    int orientation;
    int curseur;
} presence;

typedef struct {
    int actif;
    int joueur;
    int grid_offset;
    time_millis debut;
    // Position de vue, recalculée si l'orientation change (city_view_grid_offset_to_xy_view est lent).
    int vue_x;
    int vue_y;
    int vue_orientation;
} signal_carte;

static struct {
    presence autres[BRUTUS_JOUEURS_MAX];
    signal_carte signaux[BRUTUS_COOP_SIGNAUX];
    int prochain_signal;
    int conflits;
    float scale;
    time_millis maintenant; // heure de l'image en cours (dessin de la ville)
} etat;

static const color_t COULEURS[BRUTUS_JOUEURS_MAX] = { 0xff3399ff, 0xffff6633, 0xff33dd55, 0xffbb55ff };

color_t brutus_coop_couleur(int joueur)
{
    return joueur >= 0 && joueur < BRUTUS_JOUEURS_MAX ? COULEURS[joueur] : COLOR_WHITE;
}

static color_t avec_alpha(color_t c, uint32_t alpha)
{
    return (c & 0x00ffffff) | (alpha << 24);
}

static int32_t lire_i32(const uint8_t *p)
{
    int32_t v;
    memcpy(&v, p, 4);
    return v;
}

static void ecrire_i32(uint8_t *p, int32_t v)
{
    memcpy(p, &v, 4);
}

BRUTUS_RESEAU_EXPORT int brutus_coop_ma_presence(void)
{
    if (!brutus_reseau_jeu_en_cours()) {
        return 0;
    }
    int x, y, w, h;
    city_view_get_camera(&x, &y);
    city_view_get_viewport_size_tiles(&w, &h);
    uint8_t *t = brutus_reseau_tampon();
    ecrire_i32(t, x);
    ecrire_i32(t + 4, y);
    ecrire_i32(t + 8, w);
    ecrire_i32(t + 12, h);
    ecrire_i32(t + 16, city_view_orientation());
    ecrire_i32(t + 20, brutus_coop_curseur());
    return BRUTUS_COOP_PRESENCE;
}

BRUTUS_RESEAU_EXPORT void brutus_coop_presence(int joueur, int taille)
{
    if (joueur < 0 || joueur >= BRUTUS_JOUEURS_MAX || joueur == brutus_joueur_local() ||
        taille < BRUTUS_COOP_PRESENCE) {
        return;
    }
    const uint8_t *t = brutus_reseau_tampon();
    presence *p = &etat.autres[joueur];
    p->camera_x = lire_i32(t);
    p->camera_y = lire_i32(t + 4);
    p->largeur = lire_i32(t + 8);
    p->hauteur = lire_i32(t + 12);
    p->orientation = lire_i32(t + 16);
    p->curseur = lire_i32(t + 20);
    // Valeurs d'un autre client : bornées, pour ne jamais dessiner n'importe où.
    p->connue = p->largeur > 0 && p->largeur <= 2 * GRID_SIZE && p->hauteur > 0 && p->hauteur <= 4 * GRID_SIZE;
    if (!map_grid_is_valid_offset(p->curseur)) {
        p->curseur = -1;
    }
}

BRUTUS_RESEAU_EXPORT void brutus_coop_oublier(int joueur)
{
    if (joueur < 0) {
        memset(&etat, 0, sizeof(etat));
        return;
    }
    if (joueur < BRUTUS_JOUEURS_MAX) {
        memset(&etat.autres[joueur], 0, sizeof(presence));
    }
}

BRUTUS_RESEAU_EXPORT int brutus_coop_curseur(void)
{
    if (!brutus_reseau_jeu_en_cours()) {
        return -1;
    }
    int g = widget_city_current_grid_offset();
    return map_grid_is_valid_offset(g) ? g : -1;
}

BRUTUS_RESEAU_EXPORT int brutus_coop_signal(int joueur, int grid_offset)
{
    if (joueur < 0 || joueur >= BRUTUS_JOUEURS_MAX || !map_grid_is_valid_offset(grid_offset)) {
        return 0;
    }
    signal_carte *s = &etat.signaux[etat.prochain_signal];
    etat.prochain_signal = (etat.prochain_signal + 1) % BRUTUS_COOP_SIGNAUX;
    s->actif = 1;
    s->joueur = joueur;
    s->grid_offset = grid_offset;
    s->debut = time_get_millis();
    s->vue_orientation = -1;
    return 1;
}

BRUTUS_RESEAU_EXPORT void brutus_coop_aller(int grid_offset)
{
    if (map_grid_is_valid_offset(grid_offset) && brutus_reseau_jeu_en_cours()) {
        city_view_go_to_grid_offset(grid_offset);
    }
}

BRUTUS_RESEAU_EXPORT int brutus_coop_conflits(void)
{
    int n = etat.conflits;
    etat.conflits = 0;
    return n;
}

void brutus_coop_signaler_conflit(void)
{
    static uint8_t texte[200];
    if (!texte[0]) {
        encoding_from_utf8("Un autre joueur a agi au même moment : ton ordre n'a pas abouti.", texte, sizeof(texte));
    }
    etat.conflits++;
    city_warning_show_custom(texte, NEW_WARNING_SLOT);
}

BRUTUS_RESEAU_EXPORT int brutus_coop_test_presence(int joueur)
{
    // Vrai si la vue de ce joueur est connue (cadre de couleur sur la mini-carte, curseur dans la ville).
    return joueur >= 0 && joueur < BRUTUS_JOUEURS_MAX && joueur != brutus_joueur_local() && etat.autres[joueur].connue;
}

BRUTUS_RESEAU_EXPORT int brutus_coop_test_case_libre(int n)
{
    for (int y = 2; y < GRID_SIZE - 2; y += 4) {
        for (int x = 2; x < GRID_SIZE - 2; x += 4) {
            if (!map_grid_is_inside(x - 1, y - 1, 3)) {
                continue;
            }
            int libre = 1;
            for (int dy = -1; dy <= 1 && libre; dy++) {
                for (int dx = -1; dx <= 1 && libre; dx++) {
                    libre = !map_terrain_is(map_grid_offset(x + dx, y + dy), TERRAIN_NOT_CLEAR);
                }
            }
            if (libre && n-- <= 0) {
                ecrire_i32(brutus_reseau_tampon(), x); // coordonnées de la case, pour une commande
                ecrire_i32(brutus_reseau_tampon() + 4, y);
                return map_grid_offset(x, y);
            }
        }
    }
    return -1;
}

// ------------------------------------------------------------------ dessin

static int signal_en_cours(signal_carte *s, time_millis maintenant)
{
    if (s->actif && maintenant - s->debut > DUREE_SIGNAL) {
        s->actif = 0;
    }
    return s->actif;
}

static void dessiner_case(int x, int y, int grid_offset)
{
    time_millis maintenant = etat.maintenant;
    int local = brutus_joueur_local();
    for (int j = 0; j < BRUTUS_JOUEURS_MAX; j++) {
        if (j != local && etat.autres[j].connue && etat.autres[j].curseur == grid_offset) {
            image_draw(image_group(GROUP_TERRAIN_FLAT_TILE), x, y, avec_alpha(COULEURS[j], 0x99), etat.scale);
        }
    }
    int gx = map_grid_offset_to_x(grid_offset);
    int gy = map_grid_offset_to_y(grid_offset);
    for (int i = 0; i < BRUTUS_COOP_SIGNAUX; i++) {
        signal_carte *s = &etat.signaux[i];
        if (!signal_en_cours(s, maintenant)) {
            continue;
        }
        int dx = gx - map_grid_offset_to_x(s->grid_offset);
        int dy = gy - map_grid_offset_to_y(s->grid_offset);
        if (dx < -1 || dx > 1 || dy < -1 || dy > 1) {
            continue;
        }
        // Clignote (deux fois par seconde) ; la case du centre reste plus marquée.
        int phase = ((maintenant - s->debut) / 250) % 2;
        uint32_t alpha = (dx == 0 && dy == 0) ? 0xcc : (phase ? 0x88 : 0x33);
        image_draw(image_group(GROUP_TERRAIN_FLAT_TILE), x, y, avec_alpha(COULEURS[s->joueur], alpha), etat.scale);
    }
}

void brutus_coop_dessiner_ville(float scale)
{
    if (!brutus_reseau_actif()) {
        return;
    }
    int quelque_chose = 0;
    time_millis maintenant = time_get_millis();
    for (int j = 0; j < BRUTUS_JOUEURS_MAX; j++) {
        quelque_chose |= j != brutus_joueur_local() && etat.autres[j].connue && etat.autres[j].curseur >= 0;
    }
    for (int i = 0; i < BRUTUS_COOP_SIGNAUX; i++) {
        quelque_chose |= signal_en_cours(&etat.signaux[i], maintenant);
    }
    if (!quelque_chose) {
        return;
    }
    etat.scale = scale;
    etat.maintenant = maintenant;
    city_view_foreach_valid_map_tile(dessiner_case);
}

int brutus_coop_vue_minimap(int joueur, int *x, int *y, int *largeur, int *hauteur, color_t *couleur)
{
    if (!brutus_reseau_actif() || joueur < 0 || joueur >= BRUTUS_JOUEURS_MAX || joueur == brutus_joueur_local()) {
        return 0;
    }
    const presence *p = &etat.autres[joueur];
    // L'orientation est partagée (commande rotation-vue) : une orientation différente ne dure qu'un
    // instant (rotation pas encore appliquée chez l'un des deux) ; on ne dessine pas un cadre faux.
    if (!p->connue || p->orientation != city_view_orientation()) {
        return 0;
    }
    *x = p->camera_x;
    *y = p->camera_y;
    *largeur = p->largeur;
    *hauteur = p->hauteur;
    *couleur = COULEURS[joueur];
    return 1;
}

int brutus_coop_signal_minimap(int i, int *x, int *y, color_t *couleur)
{
    if (!brutus_reseau_actif() || i < 0 || i >= BRUTUS_COOP_SIGNAUX) {
        return 0;
    }
    signal_carte *s = &etat.signaux[i];
    time_millis maintenant = time_get_millis();
    if (!signal_en_cours(s, maintenant)) {
        return 0;
    }
    int orientation = city_view_orientation();
    if (s->vue_orientation != orientation) {
        city_view_grid_offset_to_xy_view(s->grid_offset, &s->vue_x, &s->vue_y);
        s->vue_orientation = orientation;
    }
    if (!s->vue_x && !s->vue_y) {
        return 0; // case introuvable dans la vue (0, 0 : valeur par défaut de la recherche)
    }
    if (((maintenant - s->debut) / 250) % 2) {
        return 0; // clignote
    }
    *x = s->vue_x;
    *y = s->vue_y;
    *couleur = COULEURS[s->joueur];
    return 1;
}
