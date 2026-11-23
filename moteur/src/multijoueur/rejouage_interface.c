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

// ------------------------------------------------------------------ tous les bâtiments (--test-batiments)
//
// Chaque type du menu de construction est choisi comme le ferait le joueur, en multijoueur (ticks imposés
// de l'extérieur) : survol de plusieurs cases (terre, rive, eau, emplacements de pont), clic, glisser,
// relâchement, puis quelques images pour que la commande soit jouée. La souris passe par input/mouse.c et
// window_draw, comme dans le jeu. Une image trop lente ou un blocage (le script de test coupe au bout d'un
// délai) désigne le type fautif : son nom est écrit avant chaque essai.

#include "building/menu.h"
#include "city/finance.h"
#include "city/view.h"
#include "core/time.h"
#include "game/game.h"
#include "input/mouse.h"
#include "map/bridge.h"
#include "map/terrain.h"
#include "multijoueur/commande.h"
#include "multijoueur/simulation.h"

#include <stdio.h>

#define CASES_MAX 6
#define IMAGE_LENTE_MS 3000

static struct {
    FILE *sortie;
    int lentes;
    int fenetres;
    time_millis pire;
    int pire_type;
} tb;

static void une_image(int type)
{
    time_millis debut = time_get_millis();
    window_draw(0);
    brutus_ticks_externes_ajouter(1);
    while (brutus_ticks_externes_en_attente() > 0) {
        game_run();
    }
    time_millis duree = time_get_millis() - debut;
    if (duree > tb.pire) {
        tb.pire = duree;
        tb.pire_type = type;
    }
    if (duree > IMAGE_LENTE_MS) {
        tb.lentes++;
        fprintf(tb.sortie, "# LENT type %d : image de %u ms\n", type, (unsigned int) duree);
        fflush(tb.sortie);
    }
}

static int choisir_cases(int *cases)
{
    int n = 0;
    // Emplacements de pont valides (deux au plus, éloignés), puis une case d'eau quelconque et une rive.
    int ponts = 0, eau = 0, rive = 0;
    for (int y = 2; y < map_data.height - 2 && n < CASES_MAX - 3; y++) {
        for (int x = 2; x < map_data.width - 2 && n < CASES_MAX - 3; x++) {
            int offset = map_grid_offset(x, y);
            if (!map_terrain_is(offset, TERRAIN_WATER)) {
                continue;
            }
            int longueur, direction;
            static grid_slice bloquees; // statique : 105 Ko
            bloquees.size = 0;
            if (ponts < 2 && map_bridge_calculate_length_direction(x, y, &longueur, &direction, &bloquees)) {
                if (!ponts || abs(map_grid_offset_to_x(cases[n - 1]) - x) + abs(map_grid_offset_to_y(cases[n - 1]) - y) > 20) {
                    cases[n++] = offset;
                    ponts++;
                }
            } else if (!eau && map_terrain_count_directly_adjacent_with_type(offset, TERRAIN_WATER) == 4) {
                cases[n++] = offset;
                eau = 1;
            } else if (!rive && map_terrain_count_directly_adjacent_with_type(offset, TERRAIN_WATER) == 2) {
                cases[n++] = offset;
                rive = 1;
            }
        }
    }
    map_bridge_reset_building_length();
    fprintf(tb.sortie, "# cases : %d emplacements de pont, eau %d, rive %d\n", ponts, eau, rive);
    // Terre, au hasard (graine fixe)
    while (n < CASES_MAX) {
        tuile_au_hasard(10);
        int offset = map_grid_offset(etat.x, etat.y);
        if (!map_terrain_is(offset, TERRAIN_WATER)) {
            cases[n++] = offset;
        }
    }
    return n;
}

static void essayer(int type, int offset)
{
    int vx, vy, largeur, hauteur;
    city_view_go_to_grid_offset(offset);
    city_view_get_viewport(&vx, &vy, &largeur, &hauteur);
    int cx = vx + largeur / 2, cy = vy + hauteur / 2;
    // Pixel de l'écran qui tombe sur la case (la caméra est bornée au bord de la carte : pas toujours au centre)
    int trouve = 0;
    for (int py = vy + 2; py < vy + hauteur - 2 && !trouve; py += 3) {
        for (int px = vx + 2; px < vx + largeur - 2 && !trouve; px += 6) {
            view_tile vt;
            if (city_view_pixels_to_view_tile(px, py, &vt) && city_view_tile_to_grid_offset(&vt) == offset) {
                cx = px;
                cy = py;
                trouve = 1;
            }
        }
    }
    if (!trouve) {
        fprintf(tb.sortie, "#   case %d hors de l'écran\n", offset);
        return;
    }
    if (building_construction_type() != type) {
        building_construction_set_type(type, 0);
    }
    mouse_set_position(cx, cy);
    int tresor = city_finance_treasury();
    une_image(type); // survol : fantôme
    mouse_set_left_down(1); // clic simple
    une_image(type);
    mouse_set_left_down(0);
    une_image(type);
    une_image(type);
    int clic = tresor - city_finance_treasury();
    if (!window_is(WINDOW_CITY)) {
        tb.fenetres++;
        fprintf(tb.sortie, "#   case %d : fenêtre %d ouverte après le clic, refermée\n", offset, window_get_id());
        window_city_show();
        une_image(type);
    }
    if (building_construction_type() != type) {
        building_construction_set_type(type, 0);
    }
    tresor = city_finance_treasury();
    mouse_set_left_down(1); // glisser
    une_image(type);
    int glisse = building_construction_in_progress();
    mouse_set_position(cx + 58, cy + 30); // glisser de deux cases
    une_image(type);
    mouse_set_left_down(0);
    une_image(type); // relâchement
    une_image(type); // commande jouée
    fprintf(tb.sortie, "#   case %d (%d,%d) : clic coût %d ; glisser %d coût %d\n", offset, map_grid_offset_to_x(offset),
        map_grid_offset_to_y(offset), clic, glisse, tresor - city_finance_treasury());
    if (!window_is(WINDOW_CITY)) {
        tb.fenetres++;
        fprintf(tb.sortie, "# type %d case %d : fenêtre %d ouverte, refermée\n", type, offset, window_get_id());
        window_city_show();
        une_image(type);
    }
}

int brutus_interface_test_batiments(FILE *sortie)
{
    tb.sortie = sortie;
    window_city_show();
    building_menu_enable_all();
    city_finance_treasury_add(1000000);
    int cases[CASES_MAX];
    int nb_cases = choisir_cases(cases);
    int vus[BUILDING_TYPE_MAX] = { 0 };
    int testes = 0;
    for (int sub = 0; sub < BUILD_MENU_MAX; sub++) {
        int nb = building_menu_count_all_items(sub);
        for (int item = 0; item < nb; item++) {
            building_type type = building_menu_type(sub, item);
            if (type <= BUILDING_NONE || type >= BUILDING_TYPE_MAX || vus[type] ||
                building_menu_get_submenu_for_type(type)) {
                continue; // vide, déjà vu, ou bouton qui ouvre un sous-menu
            }
            vus[type] = 1;
            // Diagnostic : BRUTUS_TYPES="26,27" limite le test à ces types
            const char *filtre = getenv("BRUTUS_TYPES");
            if (filtre) {
                char cle[16];
                snprintf(cle, sizeof(cle), ",%d,", type);
                char liste[256];
                snprintf(liste, sizeof(liste), ",%s,", filtre);
                if (!strstr(liste, cle)) {
                    continue;
                }
            }
            fprintf(stderr, "Brutus III : test des bâtiments : type %d (menu %d)\n", type, sub);
            fprintf(sortie, "# type %d menu %d\n", type, sub);
            fflush(sortie);
            for (int c = 0; c < nb_cases; c++) {
                essayer(type, cases[c]);
            }
            building_construction_clear_type();
            une_image(type);
            testes++;
        }
    }
    fprintf(sortie, "# fin test-batiments types %d cases %d lentes %d fenetres %d pire %u ms (type %d)"
        " commandes_appliquees %u commandes_refusees %u\n", testes, nb_cases, tb.lentes, tb.fenetres,
        (unsigned int) tb.pire, tb.pire_type, brutus_commandes_total_appliquees(), brutus_commandes_total_refusees());
    fflush(sortie);
    return tb.lentes == 0;
}
