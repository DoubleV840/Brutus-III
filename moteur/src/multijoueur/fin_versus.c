// Brutus III (M11) : écran de fin du Versus (voir fin_versus.h).
#include "multijoueur/fin_versus.h"

#include "building/construction.h"
#include "core/encoding.h"
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
#include "multijoueur/reseau.h"
#include "multijoueur/versus.h"
#include "multijoueur/victoire_versus.h"
#include "sound/speech.h"
#include "window/city.h"
#include "window/main_menu.h"

#include <stdio.h>

#define X0 48
#define Y0 56
#define LARGEUR 544
#define HAUTEUR 368
#define TEXTE_MAX 300

static void bouton_continuer(const generic_button *b);
static void bouton_menu(const generic_button *b);

static generic_button boutons[] = {
    {X0 + 32, Y0 + 320, 228, 26, bouton_continuer},
    {X0 + LARGEUR - 260, Y0 + 320, 228, 26, bouton_menu},
};

static struct {
    unsigned int focus;
    int affichee;
    uint32_t tick_affiche;
} etat;

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

static void dessiner_ligne(int y, const char *libelle, int v1, int v2, font_t police)
{
    char n[32];
    text_draw(t(libelle), X0 + 40, y, police, 0);
    snprintf(n, sizeof(n), "%d", v1);
    text_draw_centered(t(n), X0 + 236, y, 140, police, 0);
    snprintf(n, sizeof(n), "%d", v2);
    text_draw_centered(t(n), X0 + 376, y, 140, police, 0);
}

static void draw_foreground(void)
{
    graphics_in_dialog();
    outer_panel_draw(X0, Y0, LARGEUR / 16, HAUTEUR / 16);

    int moi = brutus_ville_vue();
    int fin = brutus_victoire_fin();
    int gagnant = brutus_victoire_gagnant();
    const char *titre = gagnant < 0 ? "Égalité" : gagnant == moi ? "Victoire !" : "Défaite";
    text_draw_centered(t(titre), X0, Y0 + 16, LARGEUR, FONT_LARGE_BLACK, 0);

    char ligne[TEXTE_MAX];
    if (fin == BRUTUS_FIN_MILITAIRE) {
        if (gagnant == moi) {
            snprintf(ligne, sizeof(ligne), "Vos soldats ont détruit le Sénat de la ville %s.",
                nom_ville(1 - moi));
        } else {
            snprintf(ligne, sizeof(ligne), "Les soldats de la ville %s ont détruit votre Sénat.",
                nom_ville(gagnant));
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
            snprintf(ligne, sizeof(ligne), "Fin du temps imparti (%s) : la ville %s a la meilleure économie.", duree,
                nom_ville(gagnant));
        }
    }
    text_draw_multiline(t(ligne), X0 + 32, Y0 + 58, LARGEUR - 64, 0, FONT_NORMAL_BLACK, 0);

    // Tableau des scores (au moment de la fin)
    inner_panel_draw(X0 + 24, Y0 + 100, (LARGEUR - 48) / 16, 10);
    const brutus_score_ville *s1 = brutus_victoire_scores_fin(0);
    const brutus_score_ville *s2 = brutus_victoire_scores_fin(1);
    char tete[2][60];
    for (int v = 0; v < 2; v++) {
        snprintf(tete[v], sizeof(tete[v]), "Ville %s%s", nom_ville(v), v == moi ? " (vous)" : "");
    }
    text_draw_centered(t(tete[0]), X0 + 236, Y0 + 112, 140, FONT_NORMAL_PLAIN, COLOR_FONT_BLUE);
    text_draw_centered(t(tete[1]), X0 + 376, Y0 + 112, 140, FONT_NORMAL_PLAIN, COLOR_FONT_RED);
    dessiner_ligne(Y0 + 138, "Population", s1->population, s2->population, FONT_NORMAL_WHITE);
    dessiner_ligne(Y0 + 160, "Trésor", s1->tresor, s2->tresor, FONT_NORMAL_WHITE);
    dessiner_ligne(Y0 + 182, "Prospérité", s1->prosperite, s2->prosperite, FONT_NORMAL_WHITE);
    dessiner_ligne(Y0 + 204, "Culture", s1->culture, s2->culture, FONT_NORMAL_WHITE);
    dessiner_ligne(Y0 + 236, "Score", s1->score, s2->score, FONT_NORMAL_WHITE);

    text_draw_multiline(t("Score = population + trésor / 10 + 25 x (prospérité + culture)"),
        X0 + 32, Y0 + 278, LARGEUR - 64, 0, FONT_SMALL_PLAIN, COLOR_BLACK);

    button_border_draw(boutons[0].x, boutons[0].y, boutons[0].width, boutons[0].height, etat.focus == 1);
    text_draw_centered(t("Continuer à jouer"), boutons[0].x, boutons[0].y + 7, boutons[0].width,
        FONT_NORMAL_BLACK, 0);
    button_border_draw(boutons[1].x, boutons[1].y, boutons[1].width, boutons[1].height, etat.focus == 2);
    text_draw_centered(t("Menu principal"), boutons[1].x, boutons[1].y + 7, boutons[1].width,
        FONT_NORMAL_BLACK, 0);
    graphics_reset_dialog();
}

static void handle_input(const mouse *m, const hotkeys *h)
{
    if (generic_buttons_handle_mouse(mouse_in_dialog(m), 0, 0, boutons, 2, &etat.focus)) {
        return;
    }
    if (input_go_back_requested(m, h)) {
        window_city_show();
    }
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

void window_brutus_fin_versus_show(void)
{
    if (brutus_victoire_fin() == BRUTUS_FIN_AUCUNE) {
        return;
    }
    window_type fenetre = {
        WINDOW_BRUTUS_FIN_VERSUS,
        window_draw_underlying_window,
        draw_foreground,
        handle_input
    };
    etat.focus = 0;
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
