// Brutus III : interface scriptée pendant un rejouage multijoueur (test « conseiller ouvert », M6a).
//
// Le rejouage avance image par image (game_run), avec des ticks imposés de l'extérieur comme le
// fera le lockstep. Pendant ce temps, ce script ouvre des fenêtres (chaque conseiller, fiche
// d'un bâtiment, liste des messages), glisse des routes, aqueducs, murs, maisons et zones de
// démolition, et montre le fantôme de démolition. Les commandes que l'interface enverrait
// (message lu…) sont ignorées : la partie doit rester exactement celle du journal.
#include "multijoueur/rejouage_interface.h"

#include "building/building.h"
#include "building/construction.h"
#include "city/constants.h"
#include "graphics/window.h"
#include "map/grid.h"
#include "map/data.h"
#include "widget/city/building_ghost.h"
#include "window/advisors.h"
#include "window/building_info.h"
#include "window/city.h"
#include "window/message_list.h"
#include "window/hold_festival.h"
#include "window/gift_to_emperor.h"
#include "window/hold_games.h"
#include "window/donate_to_city.h"

#include <stdlib.h>
#include <string.h>

#define IMAGES_PAR_PHASE 25
#define NB_PHASES 8
#define MAX_DESSINS 40

static struct {
    uint32_t alea;
    int phase;
    int glisser;
    int x;
    int y;
    int pas;
    unsigned int ouvertures;
    unsigned int glissers;
    unsigned int dessins;
} etat = { 12345u };

static uint32_t suivant(void)
{
    // Générateur local (n'utilise pas l'aléatoire de la partie)
    etat.alea = etat.alea * 1103515245u + 12345u;
    return etat.alea >> 8;
}

static void tuile_au_hasard(int marge)
{
    int largeur = map_data.width > 2 * marge ? map_data.width - 2 * marge : 1;
    int hauteur = map_data.height > 2 * marge ? map_data.height - 2 * marge : 1;
    etat.x = marge + (int) (suivant() % (uint32_t) largeur);
    etat.y = marge + (int) (suivant() % (uint32_t) hauteur);
}

static void arreter_glisser(void)
{
    if (building_construction_in_progress()) {
        building_construction_cancel();
    }
    building_construction_clear_type();
    etat.glisser = 0;
}

static void commencer_glisser(building_type type)
{
    building_construction_set_type(type, 0);
    tuile_au_hasard(10);
    building_construction_start(etat.x, etat.y, map_grid_offset(etat.x, etat.y));
    etat.glisser = building_construction_in_progress();
    etat.pas = 0;
    if (etat.glisser) {
        etat.glissers++;
    }
}

static void prolonger_glisser(void)
{
    if (!etat.glisser || !building_construction_in_progress()) {
        return;
    }
    etat.pas++;
    int x = etat.x + (etat.pas % 9);
    int y = etat.y + (etat.pas % 4);
    if (x >= map_data.width) {
        x = map_data.width - 1;
    }
    if (y >= map_data.height) {
        y = map_data.height - 1;
    }
    building_construction_update(x, y, map_grid_offset(x, y));
}

static void ouvrir_fiche_batiment(void)
{
    int nb = building_count();
    if (nb <= 1) {
        return;
    }
    int depart = 1 + (int) (suivant() % (uint32_t) (nb - 1));
    for (int i = 0; i < nb - 1; i++) {
        int id = 1 + (depart - 1 + i) % (nb - 1);
        building *b = building_get(id);
        if (b->state == BUILDING_STATE_IN_USE && b->grid_offset) {
            window_building_info_show(b->grid_offset);
            etat.ouvertures++;
            return;
        }
    }
}

// Diagnostic : BRUTUS_PHASES="13" limite le script aux phases 1 et 3.
static int phase_permise(int phase)
{
    static const char *phases = 0;
    static int lu = 0;
    if (!lu) {
        phases = getenv("BRUTUS_PHASES");
        lu = 1;
    }
    return !phases || strchr(phases, '0' + phase) != 0;
}

void brutus_interface_test_image(long image)
{
    if (image % IMAGES_PAR_PHASE == 0) {
        arreter_glisser();
        window_city_show();
        etat.phase = (int) ((image / IMAGES_PAR_PHASE) % NB_PHASES);
        if (!phase_permise(etat.phase)) {
            etat.phase = -1;
            return;
        }
        long cycle = image / (IMAGES_PAR_PHASE * NB_PHASES);
        switch (etat.phase) {
            case 0:
                // Un conseiller différent à chaque cycle
                window_advisors_show_advisor((advisor_type) (1 + cycle % (ADVISOR_MAX - 1)));
                etat.ouvertures++;
                break;
            case 1:
                ouvrir_fiche_batiment();
                break;
            case 2:
                // Liste des messages et fenêtres qui choisissaient dans la partie (fête, cadeau,
                // jeux, don) : leur ouverture ne doit plus rien changer.
                switch (cycle % 5) {
                    case 0: window_message_list_show(); break;
                    case 1: window_hold_festival_show(); break;
                    case 2: window_gift_to_emperor_show(); break;
                    case 3: window_hold_games_show(1); break;
                    default: window_donate_to_city_show(); break;
                }
                etat.ouvertures++;
                break;
            case 3:
                commencer_glisser(BUILDING_ROAD);
                break;
            case 4:
                commencer_glisser(BUILDING_CLEAR_LAND);
                break;
            case 5:
                // Fantôme de démolition (survol, sans glisser)
                building_construction_set_type(BUILDING_CLEAR_LAND, 0);
                tuile_au_hasard(5);
                break;
            case 6: {
                static const building_type types[] = {
                    BUILDING_AQUEDUCT, BUILDING_WALL, BUILDING_HOUSE_VACANT_LOT, BUILDING_GARDENS,
                    BUILDING_PLAZA, BUILDING_REPAIR_LAND
                };
                commencer_glisser(types[cycle % (sizeof(types) / sizeof(types[0]))]);
                break;
            }
            default:
                // Conseiller ouvert sur toute la phase, plus un glisser en cours en dessous
                commencer_glisser(BUILDING_ROAD);
                window_advisors_show_advisor((advisor_type) (1 + (cycle + 5) % (ADVISOR_MAX - 1)));
                etat.ouvertures++;
                break;
        }
    }
    if (image % 3 == 0) {
        prolonger_glisser();
    }
    if (etat.phase == 5) {
        map_tile tuile;
        tuile.x = etat.x + (int) (image % 3);
        tuile.y = etat.y;
        tuile.grid_offset = map_grid_offset(tuile.x, tuile.y);
        city_building_ghost_mark_deleting(&tuile);
    }
}

void brutus_interface_test_dessiner(long image)
{
    // Dessin complet de la fenêtre ouverte (certains conseillers recalculent en dessinant).
    // Le rendu logiciel est lent : deux dessins par phase suffisent.
    // Le rendu logiciel sans écran est très lent : on dessine une fois les fenêtres des
    // premières phases (chaque conseiller, fiche, liste des messages), puis plus.
    if (image % IMAGES_PAR_PHASE == 1 && etat.dessins < MAX_DESSINS &&
        (etat.phase == 0 || etat.phase == 1 || etat.phase == 2 || etat.phase == 7)) {
        window_draw(1);
        etat.dessins++;
    }
}

void brutus_interface_test_bilan(unsigned int *ouvertures, unsigned int *glissers)
{
    *ouvertures = etat.ouvertures;
    *glissers = etat.glissers;
}
