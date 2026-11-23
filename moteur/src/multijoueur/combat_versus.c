// Brutus III (M10) : combat entre les villes du Versus (voir combat_versus.h et docs/versus.md).
#include "multijoueur/combat_versus.h"

#include "building/building.h"
#include "building/destruction.h"
#include "core/encoding.h"
#include "figure/movement.h"
#include "game/cheats.h"
#include "game/time.h"
#include "graphics/window.h"
#include "city/warning.h"
#include "map/building.h"
#include "map/grid.h"
#include "map/routing_terrain.h"
#include "map/terrain.h"
#include "multijoueur/commande.h"
#include "multijoueur/versus.h"

#include <stdio.h>

static int option = 1;

// Bilan des tests (rejouage) : pas dans la sauvegarde ni dans l'empreinte
static struct {
    unsigned int soldats_crees[BRUTUS_VILLES_MAX];
    unsigned int batiments_detruits[BRUTUS_VILLES_MAX];
    unsigned int soldats_tues[BRUTUS_VILLES_MAX]; // par l'autre ville
} bilan;

int brutus_combat_option(void)
{
    return option;
}

void brutus_combat_definir_option(int actif)
{
    option = actif ? 1 : 0;
}

int brutus_combat_triche(int valeur)
{
    if (valeur == 0 || valeur == 1) {
        option = valeur;
    } else {
        option = !option;
    }
    return 1;
}

int brutus_combat_actif(void)
{
    return option && brutus_versus_actif();
}

// ------------------------------------------------------------------ légions par ville

int brutus_versus_formation_proprietaire(const formation *m)
{
    if (!m || !m->is_legion || m->building_id <= 0) {
        return -1;
    }
    return building_get(m->building_id)->brutus_proprietaire;
}

int brutus_versus_formation_ignorer(const formation *m)
{
    if (!brutus_versus_actif()) {
        return 0;
    }
    int ville = brutus_versus_formation_proprietaire(m);
    return ville >= 0 && ville != brutus_ville_active();
}

// ------------------------------------------------------------------ militaires

// Militaires qu'on peut viser : soldats des légions et sentinelles des murs (les balistes et les archers
// des tours de guet restent sur leur tour, comme pour les envahisseurs).
static int est_cible_militaire(const figure *f)
{
    return figure_is_legion(f) || f->type == FIGURE_TOWER_SENTRY;
}

static int est_combattant(const figure *f)
{
    return est_cible_militaire(f) || f->type == FIGURE_BALLISTA || f->type == FIGURE_WATCHTOWER_ARCHER;
}

int brutus_combat_cible_pour(const figure *f, int ville)
{
    if (!brutus_combat_actif() || f->state != FIGURE_STATE_ALIVE ||
        f->action_state == FIGURE_ACTION_149_CORPSE || f->is_ghost) {
        return 0;
    }
    return est_cible_militaire(f) && f->brutus_proprietaire != ville;
}

int brutus_combat_cible(const figure *f)
{
    return brutus_combat_cible_pour(f, brutus_ville_active());
}

int brutus_combat_ennemis(const figure *a, const figure *b)
{
    if (!brutus_combat_actif()) {
        return 0;
    }
    return a->brutus_proprietaire != b->brutus_proprietaire && est_combattant(a) && est_cible_militaire(b);
}

int brutus_combat_nombre_cibles(int ville)
{
    if (!brutus_combat_actif()) {
        return 0;
    }
    int total = 0;
    for (unsigned int i = 1; i < figure_count(); i++) {
        if (brutus_combat_cible_pour(figure_get(i), ville)) {
            total++;
        }
    }
    return total;
}

void brutus_combat_noter_mort(const figure *tueur, const figure *victime)
{
    if (brutus_versus_actif() && tueur->brutus_proprietaire != victime->brutus_proprietaire &&
        est_cible_militaire(victime) && victime->brutus_proprietaire < BRUTUS_VILLES_MAX) {
        bilan.soldats_tues[victime->brutus_proprietaire]++;
    }
}

// ------------------------------------------------------------------ bâtiments

// Points de vie d'une case, comme pour les envahisseurs (figure/movement.c, advance_route_tile)
static int degats_max(int grid_offset, const building *b)
{
    switch (map_routing_get_destroyable(grid_offset)) {
        case DESTROYABLE_BUILDING:
            switch (b->type) {
                case BUILDING_PALISADE:
                case BUILDING_PALISADE_GATE:
                case BUILDING_WATCHTOWER:
                    return PALISADE_HP;
                default:
                    return BUILDING_HP;
            }
        case DESTROYABLE_AQUEDUCT_GARDEN:
            return BUILDING_HP;
        case DESTROYABLE_WALL:
            return WALL_HP;
        case DESTROYABLE_GATEHOUSE:
            return GATEHOUSE_HP;
        default:
            return 0;
    }
}

// Les villages indigènes ne sont à aucune ville (leur propriétaire 0 n'est qu'une valeur par défaut)
static int est_indigene(building_type type)
{
    switch (type) {
        case BUILDING_NATIVE_HUT:
        case BUILDING_NATIVE_MEETING:
        case BUILDING_NATIVE_CROPS:
        case BUILDING_NATIVE_HUT_ALT:
        case BUILDING_NATIVE_WATCHTOWER:
        case BUILDING_NATIVE_MONUMENT:
        case BUILDING_NATIVE_DECORATION:
        case BUILDING_NATIVE_WELL:
        case BUILDING_NATIVE_LARGE_HUT_ALT:
        case BUILDING_NATIVE_HUT_ALT_2:
        case BUILDING_NATIVE_LARGE_HUT_ALT_2:
        case BUILDING_NATIVE_PALISADE:
            return 1;
        default:
            return 0;
    }
}

int brutus_combat_attaquer_batiment(figure *f)
{
    if (!brutus_combat_actif() || f->action_state == FIGURE_ACTION_150_ATTACK) {
        return 0;
    }
    for (int dir = 0; dir < 8; dir++) {
        int grid_offset = f->grid_offset + map_grid_direction_delta(dir);
        int building_id = map_building_at(grid_offset);
        if (building_id <= 0 || !map_routing_is_destroyable(grid_offset)) {
            continue;
        }
        building *b = building_get(building_id);
        if (b->state != BUILDING_STATE_IN_USE && b->state != BUILDING_STATE_MOTHBALLED) {
            continue;
        }
        if (b->brutus_proprietaire == f->brutus_proprietaire || est_indigene(b->type)) {
            continue;
        }
        if (map_terrain_is(grid_offset, TERRAIN_ACCESS_RAMP | TERRAIN_RUBBLE)) {
            continue;
        }
        int max = degats_max(grid_offset, b);
        if (max <= 0) {
            continue;
        }
        f->attack_direction = dir;
        if (!(game_time_tick() & 3)) {
            // Ce que la destruction change (note de paix, ruines…) concerne la ville du bâtiment
            int ville = brutus_ville_active();
            int victime = b->brutus_proprietaire;
            brutus_ville_activer(victime);
            building_destroy_increase_enemy_damage(grid_offset, max);
            brutus_ville_activer(ville);
            if (map_building_at(grid_offset) != building_id ||
                (b->state != BUILDING_STATE_IN_USE && b->state != BUILDING_STATE_MOTHBALLED)) {
                if (victime < BRUTUS_VILLES_MAX) {
                    bilan.batiments_detruits[victime]++;
                }
            }
        }
        return 1;
    }
    return 0;
}

// ------------------------------------------------------------------ test : soldats tout de suite

int brutus_combat_triche_soldats(void)
{
    for (int i = 1; i < formation_count(); i++) {
        formation *m = formation_get(i);
        if (!m->in_use || !m->is_legion || m->in_distant_battle || brutus_versus_formation_ignorer(m)) {
            continue;
        }
        building *fort = building_get(m->building_id);
        for (int n = m->num_figures; n < MAX_FORMATION_FIGURES; n++) {
            figure *f = figure_create(m->figure_type, fort->x, fort->y, DIR_0_TOP);
            f->formation_id = m->id;
            f->formation_at_rest = 1;
            f->action_state = FIGURE_ACTION_81_SOLDIER_GOING_TO_FORT;
            if (f->brutus_proprietaire < BRUTUS_VILLES_MAX) {
                bilan.soldats_crees[f->brutus_proprietaire]++;
            }
        }
        m->legion_recruit_type = LEGION_RECRUIT_NONE;
        formation_calculate_figures();
    }
    return 1;
}

// ------------------------------------------------------------------ Versus local (touche Alt+G)

void brutus_combat_touche(void)
{
    if (!window_is(WINDOW_CITY) || !brutus_versus_actif() || brutus_multijoueur_actif()) {
        return;
    }
    brutus_commande_2(BRUTUS_CMD_TRICHE, BRUTUS_TRICHE_COMBAT, -1);
    static uint8_t texte[120];
    // La commande s'applique au tick suivant : on annonce l'état qu'elle donnera
    encoding_from_utf8(option ? "Combat entre les villes : désactivé." : "Combat entre les villes : activé.",
        texte, sizeof(texte));
    city_warning_show_custom(texte, NEW_WARNING_SLOT);
}

// ------------------------------------------------------------------ bilan (rejouage)

void brutus_combat_bilan(void)
{
    if (!brutus_versus_actif()) {
        return;
    }
    for (int v = 0; v < brutus_versus_villes(); v++) {
        unsigned int vivants = 0;
        for (unsigned int i = 1; i < figure_count(); i++) {
            figure *f = figure_get(i);
            if (f->state == FIGURE_STATE_ALIVE && f->action_state != FIGURE_ACTION_149_CORPSE &&
                figure_is_legion(f) && f->brutus_proprietaire == v) {
                vivants++;
            }
        }
        printf("Brutus III : combat ville %d : %s, crees %u vivants %u tues %u detruits %u\n", v + 1,
            option ? "activé" : "désactivé", bilan.soldats_crees[v], vivants, bilan.soldats_tues[v],
            bilan.batiments_detruits[v]);
    }
}
