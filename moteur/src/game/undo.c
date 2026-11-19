#include "undo.h"

#include "building/connectable.h"
#include "building/construction.h"
#include "building/house.h"
#include "building/image.h"
#include "building/industry.h"
#include "building/menu.h"
#include "building/monument.h"
#include "building/properties.h"
#include "building/storage.h"
#include "building/warehouse.h"
#include "building/storage.h"
#include "city/buildings.h"
#include "city/finance.h"
#include "city/population.h"
#include "core/calc.h"
#include "core/image.h"
#include "figure/roamer_preview.h"
#include "game/resource.h"
#include "graphics/window.h"
#include "map/aqueduct.h"
#include "map/building.h"
#include "map/building_tiles.h"
#include "map/grid.h"
#include "map/image.h"
#include "map/property.h"
#include "map/routing_terrain.h"
#include "map/sprite.h"
#include "map/terrain.h"
#include "multijoueur/commande.h" // Brutus III
#include "scenario/earthquake.h"

#include <stdlib.h> // Brutus III
#include <string.h>

#define MAX_UNDO_BUILDINGS 50
#define MAX_UNDO_TYPE_CHANGES 50

typedef struct {
    int available;
    int ready;
    int timeout_ticks;
    int building_cost;
    int num_buildings;
    building_type type;
    building buildings[MAX_UNDO_BUILDINGS];
    struct {
        int num;
        struct {
            unsigned int building_id;
            building_type original_type;
        } items[MAX_UNDO_TYPE_CHANGES];
    } type_changes;
    int joueur; // Brutus III : joueur qui a fait la construction
} undo_state;

// Brutus III : deux contextes d'annulation. Celui de la simulation (commandes, ticks) ne
// change que par les commandes, donc pareil chez tous les joueurs ; celui de l'aperçu sert
// au glisser du joueur local (building_construction_start/update) et n'a aucun effet sur la
// partie. « data » est le contexte chargé, « autre_contexte » celui qui est mis de côté.
// Les copies de secours des grilles (map_*_backup) et les habitants à rendre sont gardés à
// part pour la simulation et remis dans les modules quand la simulation reprend.
static undo_state data;
static undo_state autre_contexte;
static int contexte_simulation_charge;

#define MAX_SECOURS 12

static struct {
    int nb;
    void *module[MAX_SECOURS];
    size_t taille[MAX_SECOURS];
    void *copie[MAX_SECOURS];
    int modules_simulation; // 1 : les modules contiennent les copies de secours de la simulation
    int en_simulation;
} secours = { .modules_simulation = 1 };

// Met dans « data » le contexte de la simulation (1) ou celui de l'aperçu (0).
static void charger_contexte(int simulation)
{
    static undo_state echange;
    if (contexte_simulation_charge == simulation) {
        return;
    }
    echange = data;
    data = autre_contexte;
    autre_contexte = echange;
    contexte_simulation_charge = simulation;
}

static void assurer_contexte(void);

static void ajouter_secours(void *module, size_t taille)
{
    if (module && taille && secours.nb < MAX_SECOURS) {
        secours.module[secours.nb] = module;
        secours.taille[secours.nb] = taille;
        secours.copie[secours.nb] = calloc(1, taille);
        secours.nb++;
    }
}

static void preparer_secours(void)
{
    if (secours.nb) {
        return;
    }
    size_t taille;
    void *(*modules[])(int, size_t *) = {
        map_image_brutus_secours, map_terrain_brutus_secours, map_aqueduct_brutus_secours,
        map_property_brutus_secours, map_sprite_brutus_secours, map_building_brutus_secours
    };
    for (unsigned int m = 0; m < sizeof(modules) / sizeof(modules[0]); m++) {
        void *p;
        for (int i = 0; (p = modules[m](i, &taille)) != 0; i++) {
            ajouter_secours(p, taille);
        }
    }
    int *ages = city_population_brutus_annulation(&taille);
    ajouter_secours(ages, taille);
}

// Avant que l'aperçu (interface) n'écrive dans les copies de secours des modules : celles
// de la simulation sont mises de côté.
static void liberer_modules_pour_apercu(void)
{
    if (secours.en_simulation || !secours.modules_simulation) {
        return;
    }
    preparer_secours();
    for (int i = 0; i < secours.nb; i++) {
        if (secours.copie[i]) {
            memcpy(secours.copie[i], secours.module[i], secours.taille[i]);
        }
    }
    secours.modules_simulation = 0;
}

static void assurer_contexte(void)
{
    charger_contexte(secours.en_simulation > 0);
}

void game_undo_brutus_entrer_simulation(void)
{
    if (secours.en_simulation++ > 0) {
        return;
    }
    if (!secours.modules_simulation) {
        preparer_secours();
        for (int i = 0; i < secours.nb; i++) {
            if (secours.copie[i]) {
                memcpy(secours.module[i], secours.copie[i], secours.taille[i]);
            }
        }
        secours.modules_simulation = 1;
    }
    charger_contexte(1);
}

void game_undo_brutus_sortir_simulation(void)
{
    if (secours.en_simulation <= 0) {
        secours.en_simulation = 0;
        return;
    }
    if (--secours.en_simulation > 0) {
        return;
    }
    // Les modules et « data » gardent le contexte de la simulation tant que l'aperçu n'en a pas
    // besoin (charger_contexte, liberer_modules_pour_apercu).
}

static const undo_state *etat_simulation(void)
{
    return contexte_simulation_charge ? &data : &autre_contexte;
}

int game_undo_brutus_appartient(int joueur)
{
    return etat_simulation()->joueur == joueur;
}

void game_undo_brutus_partie_chargee(void)
{
    // Les copies de secours lues dans la sauvegarde (aqueducs, sprites) sont dans les modules.
    memset(&data, 0, sizeof(data));
    memset(&autre_contexte, 0, sizeof(autre_contexte));
    secours.modules_simulation = 1;
}

static int contexte_peut_annuler(void)
{
    return data.ready && data.available;
}

int game_can_undo(void)
{
    // Brutus III : l'interface (bouton, raccourci) regarde la dernière construction de la
    // simulation, et seulement si elle vient du joueur local.
    if (!secours.en_simulation) {
        const undo_state *sim = etat_simulation();
        return sim->ready && sim->available && sim->joueur == brutus_joueur_local();
    }
    assurer_contexte();
    return contexte_peut_annuler();
}

void game_undo_disable(void)
{
    if (!secours.en_simulation) {
        // Brutus III : appelé par l'interface (retour au menu, fin de mission) : partie quittée,
        // les deux contextes sont fermés.
        autre_contexte.available = 0;
    } else {
        assurer_contexte(); // Brutus III
    }
    data.available = 0;
}

void game_undo_add_building(building *b)
{
    assurer_contexte(); // Brutus III
    if (b->id <= 0) {
        return;
    }
    data.num_buildings = 0;
    int is_on_list = 0;
    for (int i = 0; i < MAX_UNDO_BUILDINGS; i++) {
        if (data.buildings[i].id) {
            data.num_buildings++;
        }
        if (data.buildings[i].id == b->id) {
            is_on_list = 1;
        }
    }
    if (!is_on_list) {
        for (int i = 0; i < MAX_UNDO_BUILDINGS; i++) {
            if (!data.buildings[i].id) {
                data.num_buildings++;
                memcpy(&data.buildings[i], b, sizeof(building));
                return;
            }
        }
        data.available = 0;
    }
}

void game_undo_adjust_building(building *b)
{
    assurer_contexte(); // Brutus III
    for (int i = 0; i < MAX_UNDO_BUILDINGS; i++) {
        if (data.buildings[i].id == b->id) {
            // found! update the building now
            memcpy(&data.buildings[i], b, sizeof(building));
        }
    }
}

int game_undo_contains_building(int building_id)
{
    assurer_contexte(); // Brutus III
    if (building_id <= 0 || !contexte_peut_annuler()) { // Brutus III : contexte courant
        return 0;
    }
    if (data.num_buildings <= 0) {
        return 0;
    }
    for (int i = 0; i < MAX_UNDO_BUILDINGS; i++) {
        if (data.buildings[i].id == (unsigned int) building_id) {
            return 1;
        }
    }
    return 0;
}

static void clear_buildings(void)
{
    data.num_buildings = 0;
    memset(data.buildings, 0, MAX_UNDO_BUILDINGS * sizeof(building));
    data.type_changes.num = 0;
    city_population_reset_undo();
}

void game_undo_record_building_type(building *b)
{
    assurer_contexte(); // Brutus III
    if (!b || b->id <= 0 || data.type_changes.num >= MAX_UNDO_TYPE_CHANGES) {
        return;
    }
    // Only record the first snapshot (before any change this build action)
    for (int i = 0; i < data.type_changes.num; i++) {
        if (data.type_changes.items[i].building_id == b->id) {
            return;
        }
    }
    data.type_changes.items[data.type_changes.num].building_id = b->id;
    data.type_changes.items[data.type_changes.num].original_type = b->type;
    data.type_changes.num++;
}

void game_undo_restore_building_types(void)
{
    assurer_contexte(); // Brutus III
    for (int i = 0; i < data.type_changes.num; i++) {
        building *b = building_get(data.type_changes.items[i].building_id);
        if (b && b->id > 0) {
            building_change_type(b, data.type_changes.items[i].original_type);
        }
    }
    data.type_changes.num = 0;
}

int game_undo_start_build(building_type type)
{
    assurer_contexte(); // Brutus III
    liberer_modules_pour_apercu(); // Brutus III : hors simulation, c'est l'aperçu
    data.joueur = secours.en_simulation ?
        (brutus_commande_joueur_en_cours() >= 0 ? brutus_commande_joueur_en_cours() : 0) : brutus_joueur_local();
    data.ready = 0;
    data.available = 1;
    data.timeout_ticks = 0;
    data.building_cost = 0;
    data.type = type;
    clear_buildings();
    for (int i = 1; i < building_count(); i++) {
        building *b = building_get(i);
        if (b->state == BUILDING_STATE_UNDO) {
            data.available = 0;
            return 0;
        }
        if (b->state == BUILDING_STATE_DELETED_BY_PLAYER) {
            data.available = 0;
        }
    }

    map_image_backup();
    map_terrain_backup();
    map_aqueduct_backup();
    map_property_backup();
    map_sprite_backup();
    map_building_backup();

    return 1;
}

void game_undo_restore_building_state(void)
{
    assurer_contexte(); // Brutus III
    for (int i = 0; i < data.num_buildings; i++) {
        if (data.buildings[i].id) {
            building *b = building_get(data.buildings[i].id);
            if (b->state == BUILDING_STATE_DELETED_BY_PLAYER) {
                b->state = BUILDING_STATE_IN_USE;
            }
            b->is_deleted = 0;
        }
    }
    clear_buildings();
}

static void restore_map_images(void)
{
    int map_width, map_height;
    map_grid_size(&map_width, &map_height);
    for (int y = 0; y < map_height; y++) {
        for (int x = 0; x < map_width; x++) {
            int grid_offset = map_grid_offset(x, y);
            if (!map_building_at(grid_offset) || map_terrain_is(grid_offset, TERRAIN_AQUEDUCT)) {
                map_image_restore_at(grid_offset);
            }
        }
    }
}

void game_undo_restore_map(int include_properties)
{
    map_terrain_restore();
    map_aqueduct_restore();
    map_building_restore();
    if (include_properties) {
        map_property_restore();
    }
    restore_map_images();
}

void game_undo_finish_build(int cost)
{
    assurer_contexte(); // Brutus III
    data.ready = 1;
    data.timeout_ticks = 500;
    data.building_cost = cost;
    window_invalidate();
}

static void add_building_to_terrain(building *b)
{
    if (b->id <= 0) {
        return;
    }
    if (building_is_farm(b->type)) {
        map_building_tiles_add_farm(b->id, b->x, b->y, building_image_get_base_farm_crop(b->type),
            calc_percentage(b->data.industry.progress, building_industry_get_max_progress(b)));
    } else {
        int size = building_properties_for_type(b->type)->size;
        if (building_is_house(b->type) && b->house_is_merged) {
            size = 2;
        }
        map_building_tiles_add(b->id, b->x, b->y, size, 0, 0);
        if (b->type == BUILDING_WHARF) {
            b->data.industry.fishing_boat_id = 0;
        }
    }
    b->state = BUILDING_STATE_IN_USE;
}

void game_undo_perform(void)
{
    assurer_contexte(); // Brutus III
    if (!contexte_peut_annuler()) { // Brutus III
        return;
    }
    data.available = 0;
    city_finance_process_construction(-data.building_cost);
    if (data.type == BUILDING_CLEAR_LAND) {
        for (int i = 0; i < data.num_buildings; i++) {
            if (data.buildings[i].id) {
                if (building_properties_for_type(data.buildings[i].type)->shared) {
                    building *original = building_get(data.buildings[i].id);
                    original->subtype.instances = data.buildings[i].subtype.instances;
                    continue;
                }
                building *b = building_restore_from_undo(&data.buildings[i]);
                switch (b->type) {
                    default:
                        break;
                    case BUILDING_WAREHOUSE:
                    case BUILDING_GRANARY:
                        if (!building_storage_restore(b->storage_id)) {
                            building_storage_reset_building_ids();
                        }
                        break;
                    case BUILDING_TRIUMPHAL_ARCH:
                        city_buildings_build_triumphal_arch();
                        building_menu_update();
                        if (building_construction_type() == BUILDING_TRIUMPHAL_ARCH &&
                            !building_menu_is_enabled(BUILDING_TRIUMPHAL_ARCH)) {
                            building_construction_clear_type();
                        }
                        break;
                }
                if (building_is_house(b->type)) {
                    building_house_restore_population_after_undo(b);
                }
                add_building_to_terrain(b);
            }
        }
        map_terrain_restore();
        map_aqueduct_restore();
        map_sprite_restore();
        map_image_restore();
        map_property_restore();
        map_building_restore();
        map_property_clear_constructing_and_deleted();
    } else {
        int restore_map_buildings = 0;
        for (int i = 0; i < data.num_buildings; i++) {
            if (data.buildings[i].id && building_properties_for_type(data.buildings[i].type)->shared) {
                building *original = building_get(data.buildings[i].id);
                original->subtype.instances = data.buildings[i].subtype.instances;
                restore_map_buildings = 1;
            }
        }
        if (restore_map_buildings) {
            map_building_restore();
        }
        if (data.type == BUILDING_AQUEDUCT || data.type == BUILDING_ROAD ||
            data.type == BUILDING_WALL || data.type == BUILDING_HIGHWAY) {
            map_terrain_restore();
            map_aqueduct_restore();
            restore_map_images();
            game_undo_restore_building_types();
            building_connectable_update_connections();
        } else if (data.type == BUILDING_LOW_BRIDGE || data.type == BUILDING_SHIP_BRIDGE) {
            map_terrain_restore();
            map_building_restore();
            map_sprite_restore();
            restore_map_images();
        } else if (data.type == BUILDING_PLAZA || data.type == BUILDING_GARDENS ||
            data.type == BUILDING_OVERGROWN_GARDENS) {
            map_terrain_restore();
            map_aqueduct_restore();
            map_property_restore();
            restore_map_images();
        } else if (data.num_buildings) {
            if (data.type == BUILDING_DRAGGABLE_RESERVOIR) {
                map_terrain_restore();
                map_aqueduct_restore();
                restore_map_images();
            }
            for (int i = 0; i < data.num_buildings; i++) {
                if (data.buildings[i].id && !building_properties_for_type(data.buildings[i].type)->shared) {
                    building_get(data.buildings[i].id)->state = BUILDING_STATE_UNDO;
                }
            }
            building_update_state();
        }
    }
    map_routing_update_land();
    map_routing_update_walls();
    figure_roamer_preview_reset(building_construction_type());
    data.num_buildings = 0;
}

void game_undo_reduce_time_available(void)
{
    assurer_contexte(); // Brutus III
    if (!contexte_peut_annuler()) { // Brutus III
        return;
    }
    if (data.timeout_ticks <= 0 || scenario_earthquake_is_in_progress()) {
        data.available = 0;
        clear_buildings();
        window_invalidate();
        return;
    }
    data.timeout_ticks--;
    switch (data.type) {
        case BUILDING_CLEAR_LAND:
        case BUILDING_AQUEDUCT:
        case BUILDING_ROAD:
        case BUILDING_HIGHWAY:
        case BUILDING_WALL:
        case BUILDING_LOW_BRIDGE:
        case BUILDING_SHIP_BRIDGE:
        case BUILDING_PLAZA:
        case BUILDING_GARDENS:
        case BUILDING_OVERGROWN_GARDENS:
            return;
        default: break;
    }
    if (data.num_buildings <= 0) {
        data.available = 0;
        window_invalidate();
        return;
    }
    if (data.type == BUILDING_HOUSE_VACANT_LOT) {
        for (int i = 0; i < data.num_buildings; i++) {
            if (data.buildings[i].id && building_get(data.buildings[i].id)->house_population) {
                // no undo on a new house where people moved in
                data.available = 0;
                window_invalidate();
                return;
            }
        }
    }
    for (int i = 0; i < data.num_buildings; i++) {
        if (data.buildings[i].id) {
            building *b = building_get(data.buildings[i].id);
            if (b->state == BUILDING_STATE_UNDO ||
                b->state == BUILDING_STATE_RUBBLE ||
                b->state == BUILDING_STATE_DELETED_BY_GAME) {
                data.available = 0;
                window_invalidate();
                return;
            }
            if (b->type != data.buildings[i].type || b->grid_offset != data.buildings[i].grid_offset) {
                data.available = 0;
                window_invalidate();
                return;
            }
        }
    }
}
