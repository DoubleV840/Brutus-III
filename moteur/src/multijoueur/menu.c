// Brutus III : fenêtre « Multijoueur » dans le jeu (voir menu.h).
#include "multijoueur/menu.h"

#include "city/warning.h"
#include "core/encoding.h"
#include "core/string.h"
#include "graphics/button.h"
#include "graphics/generic_button.h"
#include "graphics/graphics.h"
#include "graphics/panel.h"
#include "graphics/text.h"
#include "graphics/window.h"
#include "input/input.h"
#include "input/keyboard.h"
#include "multijoueur/coop.h"
#include "multijoueur/simulation.h"
#include "widget/input_box.h"
#include "window/cck_selection.h"
#include "window/city.h"
#include "window/file_dialog.h"

#include <stdio.h>
#include <string.h>

#define CHAT_LIGNES 7
#define CHAT_GARDE 20
#define TEXTE_MAX 400
#define CODE_MAX 8

typedef struct {
    int numero;
    int connecte;
    int latence;
} joueur_salon;

typedef struct {
    int id;
    int joueur;
    int signal;
    uint8_t texte[TEXTE_MAX];
} ligne_chat;

static struct {
    // poussé par JS
    int disponible;
    int phase;
    uint8_t code[CODE_MAX * 2];
    uint8_t texte[TEXTE_MAX];
    uint8_t avis[TEXTE_MAX];
    int joueur_local;
    int hote;
    joueur_salon joueurs[BRUTUS_JOUEURS_MAX];
    int nb_joueurs;
    ligne_chat chat[CHAT_GARDE];
    int nb_chat;
    int dernier_chat;
    // action en attente pour JS
    int action;
    int action_parametre;
    char action_texte[TEXTE_MAX];
    int action_lue_parametre;
    char action_lue_texte[TEXTE_MAX];
    // « créer la partie quand la ville sera à l'écran » (choisie depuis le menu principal)
    int creer_quand_ville;
    // fenêtre
    int depuis_ville;
    unsigned int focus;
    int saisie; // 0 aucune, 1 code, 2 chat
} etat;

static uint8_t saisie_code[CODE_MAX];
static uint8_t saisie_chat[200];
static input_box boite_code = { 224, 316, 10, 2, FONT_NORMAL_WHITE, 0, saisie_code, 7 };
static input_box boite_chat = { 72, 340, 22, 2, FONT_NORMAL_WHITE, 1, saisie_chat, 120 };

// ------------------------------------------------------------------ textes

// Convertit un texte UTF-8 dans l'encodage du jeu (tampons tournants : plusieurs textes par image).
static const uint8_t *t(const char *utf8)
{
    static uint8_t tampons[12][TEXTE_MAX];
    static int prochain;
    uint8_t *sortie = tampons[prochain];
    prochain = (prochain + 1) % 12;
    encoding_from_utf8(utf8, sortie, TEXTE_MAX);
    return sortie;
}

static void copier_utf8(const char *utf8, uint8_t *sortie, int taille)
{
    if (!utf8) {
        sortie[0] = 0;
        return;
    }
    encoding_from_utf8(utf8, sortie, taille);
}

static int ouverte(void)
{
    return window_is(WINDOW_BRUTUS_MULTIJOUEUR);
}

static int en_session(void)
{
    return etat.phase != BRUTUS_MENU_INACTIF && etat.phase != BRUTUS_MENU_ERREUR;
}

// ------------------------------------------------------------------ poussé par JS

BRUTUS_RESEAU_EXPORT void brutus_menu_disponible(int oui)
{
    etat.disponible = oui ? 1 : 0;
    window_invalidate();
}

BRUTUS_RESEAU_EXPORT void brutus_menu_etat(int phase, const char *code, const char *texte, const char *avis,
    int joueur_local, int hote)
{
    int ancienne = etat.phase;
    uint8_t ancien_avis[TEXTE_MAX];
    memcpy(ancien_avis, etat.avis, sizeof(ancien_avis));
    etat.phase = phase;
    copier_utf8(code, etat.code, sizeof(etat.code));
    copier_utf8(texte, etat.texte, sizeof(etat.texte));
    copier_utf8(avis, etat.avis, sizeof(etat.avis));
    if (!etat.texte[0] && phase == BRUTUS_MENU_JEU) {
        copier_utf8("Partie en cours : vous gérez la même ville.", etat.texte, sizeof(etat.texte));
    }
    // Un avis (menu refusé, divergences à répétition) doit se voir même fenêtre fermée.
    if (etat.avis[0] && !string_equals(etat.avis, ancien_avis) && !ouverte() && window_is(WINDOW_CITY)) {
        city_warning_show_custom(etat.avis, NEW_WARNING_SLOT);
    }
    // Revenu au menu principal pendant une partie à deux : la fenêtre propose d'y revenir.
    if (phase == BRUTUS_MENU_HORS_VILLE && ancienne != BRUTUS_MENU_HORS_VILLE && window_is(WINDOW_MAIN_MENU)) {
        window_brutus_multijoueur_show(0);
    }
    etat.joueur_local = joueur_local;
    etat.hote = hote;
    window_invalidate();
}

BRUTUS_RESEAU_EXPORT void brutus_menu_joueurs_vider(void)
{
    etat.nb_joueurs = 0;
}

BRUTUS_RESEAU_EXPORT void brutus_menu_joueur(int numero, int connecte, int latence)
{
    if (etat.nb_joueurs < BRUTUS_JOUEURS_MAX) {
        joueur_salon *j = &etat.joueurs[etat.nb_joueurs++];
        j->numero = numero;
        j->connecte = connecte;
        j->latence = latence;
    }
}

BRUTUS_RESEAU_EXPORT void brutus_menu_chat(int id, int joueur, const char *texte, int signal)
{
    if (id <= 0) {
        etat.nb_chat = 0;
        etat.dernier_chat = 0;
        return;
    }
    if (id <= etat.dernier_chat) {
        return;
    }
    etat.dernier_chat = id;
    if (etat.nb_chat == CHAT_GARDE) {
        memmove(&etat.chat[0], &etat.chat[1], sizeof(ligne_chat) * (CHAT_GARDE - 1));
        etat.nb_chat--;
    }
    ligne_chat *l = &etat.chat[etat.nb_chat++];
    l->id = id;
    l->joueur = joueur;
    l->signal = signal;
    copier_utf8(texte, l->texte, sizeof(l->texte));
    // Fenêtre fermée : le message de l'autre joueur s'affiche en haut de la ville.
    if (joueur != etat.joueur_local && !ouverte() && window_is(WINDOW_CITY)) {
        char ligne[TEXTE_MAX + 32];
        snprintf(ligne, sizeof(ligne), "Joueur %d : %s", joueur + 1, texte ? texte : "");
        city_warning_show_custom(t(ligne), NEW_WARNING_SLOT);
    }
    window_invalidate();
}

// ------------------------------------------------------------------ lu par JS

static void demander(int action, int parametre, const uint8_t *texte)
{
    etat.action = action;
    etat.action_parametre = parametre;
    etat.action_texte[0] = 0;
    if (texte) {
        encoding_to_utf8(texte, etat.action_texte, sizeof(etat.action_texte), 0);
    }
}

static void arreter_saisie(void);

BRUTUS_RESEAU_EXPORT int brutus_menu_action(void)
{
    // La fenêtre a été remplacée (ville reçue, chargement) : le clavier ne doit pas rester pris.
    if (etat.saisie && !ouverte()) {
        arreter_saisie();
    }
    // Partie choisie au menu principal : on crée le salon dès que la ville est à l'écran, et la
    // fenêtre s'ouvre pour montrer le code.
    if (!etat.action && etat.creer_quand_ville && window_is(WINDOW_CITY)) {
        etat.creer_quand_ville = 0;
        demander(BRUTUS_MENU_CREER, 0, 0);
        window_brutus_multijoueur_show(1);
    }
    int action = etat.action;
    etat.action_lue_parametre = etat.action_parametre;
    memcpy(etat.action_lue_texte, etat.action_texte, sizeof(etat.action_texte));
    etat.action = BRUTUS_MENU_AUCUNE;
    return action;
}

BRUTUS_RESEAU_EXPORT const char *brutus_menu_action_texte(void)
{
    return etat.action_lue_texte;
}

BRUTUS_RESEAU_EXPORT int brutus_menu_action_parametre(void)
{
    return etat.action_lue_parametre;
}

BRUTUS_RESEAU_EXPORT int brutus_menu_saisie_en_cours(void)
{
    return keyboard_is_capturing();
}

int brutus_menu_au_dessus_de_la_ville(void)
{
    return ouverte() && etat.depuis_ville;
}

void brutus_menu_retour_menu_principal(void)
{
    etat.creer_quand_ville = 0;
}

// ------------------------------------------------------------------ fenêtre

#define X0 48
#define Y0 24
#define LARGEUR 544
#define HAUTEUR 432
#define BAS (Y0 + HAUTEUR - 44)

enum {
    B_RETOUR = 1,
    B_CREER_ICI,
    B_NOUVELLE,
    B_CHARGER,
    B_REJOINDRE,
    B_ENVOYER,
    B_QUITTER,
    B_REVENIR,
    B_RETIRER,
    B_VOIR
};

static void clic(const generic_button *b);

// Boutons des deux vues (les coordonnées des boutons « Retirer » et « Voir » sont fixées au dessin).
static generic_button boutons_accueil[] = {
    {X0 + 24, 196, 240, 26, clic, 0, B_CREER_ICI, 0},
    {X0 + 24, 196, 240, 26, clic, 0, B_NOUVELLE, 0},
    {X0 + 280, 196, 240, 26, clic, 0, B_CHARGER, 0},
    {X0 + 352, 316, 168, 26, clic, 0, B_REJOINDRE, 0},
    {X0 + LARGEUR - 184, BAS, 160, 26, clic, 0, B_RETOUR, 0},
};
#define NB_ACCUEIL 5

static generic_button boutons_session[2 + BRUTUS_JOUEURS_MAX + CHAT_LIGNES + 3];
static unsigned int nb_session;

static void arreter_saisie(void)
{
    if (etat.saisie == 1) {
        input_box_stop(&boite_code);
    } else if (etat.saisie == 2) {
        input_box_stop(&boite_chat);
    }
    etat.saisie = 0;
}

static void choisir_saisie(void)
{
    int voulue = 0;
    if (etat.disponible) {
        voulue = en_session() ? 2 : 1;
    }
    if (voulue == etat.saisie) {
        return;
    }
    arreter_saisie();
    if (voulue == 1) {
        input_box_start(&boite_code);
    } else if (voulue == 2) {
        input_box_start(&boite_chat);
    }
    etat.saisie = voulue;
}

static void fermer(void)
{
    arreter_saisie();
    window_go_back();
}

static void bouton(const generic_button *b, const char *libelle, int focus)
{
    button_border_draw(b->x, b->y, b->width, b->height, focus);
    text_draw_centered(t(libelle), b->x, b->y + 7, b->width, FONT_NORMAL_BLACK, 0);
}

static void dessiner_indisponible(void)
{
    text_draw_multiline(t(
        "Le multijoueur se joue dans la version navigateur de Brutus III, une fois le serveur de parties "
        "en ligne (bientôt sur brutus.ivwi.fr). En attendant, il fonctionne en local : voir le README, "
        "section « Multijoueur en local »."), X0 + 24, 90, LARGEUR - 48, 0, FONT_NORMAL_BLACK, 0);
    bouton(&boutons_accueil[4], "Retour", etat.focus == 5);
}

static void dessiner_accueil(void)
{
    text_draw_multiline(t(
        "Jouez à plusieurs sur la même ville, avec les mêmes pouvoirs. Chacun a besoin de ses propres fichiers "
        "du jeu et garde son onglet visible (un onglet masqué ralentit les deux joueurs)."), X0 + 24, 70, LARGEUR - 48, 0, FONT_NORMAL_BLACK, 0);

    text_draw(t("Créer une partie"), X0 + 24, 140, FONT_NORMAL_BLACK, 0);
    if (etat.depuis_ville) {
        text_draw_multiline(t("La ville à l'écran devient la partie multijoueur. Tu recevras un code à donner à l'autre joueur."),
            X0 + 24, 160, LARGEUR - 48, 0, FONT_SMALL_PLAIN, COLOR_BLACK);
        bouton(&boutons_accueil[0], "Créer avec cette ville", etat.focus == 1);
    } else {
        text_draw_multiline(t("Choisis une carte ou une sauvegarde : la partie multijoueur démarre quand la ville est à l'écran."),
            X0 + 24, 160, LARGEUR - 48, 0, FONT_SMALL_PLAIN, COLOR_BLACK);
        bouton(&boutons_accueil[1], "Nouvelle partie", etat.focus == 2);
        bouton(&boutons_accueil[2], "Charger une sauvegarde", etat.focus == 3);
    }
    if (etat.creer_quand_ville) {
        text_draw(t("La partie multijoueur sera créée dès que la ville sera à l'écran."), X0 + 24, 228, FONT_SMALL_PLAIN, COLOR_BLACK);
    }

    text_draw(t("Rejoindre une partie"), X0 + 24, 264, FONT_NORMAL_BLACK, 0);
    text_draw_multiline(t("Entre le code de 6 caractères donné par l'autre joueur. Tu peux rester au menu : tu recevras sa ville."),
        X0 + 24, 284, LARGEUR - 48, 0, FONT_SMALL_PLAIN, COLOR_BLACK);
    text_draw(t("Code :"), X0 + 112, 325, FONT_NORMAL_BLACK, 0);
    input_box_draw(&boite_code);
    bouton(&boutons_accueil[3], "Rejoindre", etat.focus == 4);

    if (etat.texte[0]) {
        text_draw_multiline(etat.texte, X0 + 24, 360, LARGEUR - 48, 0, FONT_NORMAL_PLAIN,
            etat.phase == BRUTUS_MENU_ERREUR ? COLOR_FONT_RED : COLOR_BLACK);
    }
    bouton(&boutons_accueil[4], "Retour", etat.focus == 5);
}

static void ajouter_session(int x, int y, int w, int parametre, int parametre2)
{
    generic_button *b = &boutons_session[nb_session++];
    b->x = x;
    b->y = y;
    b->width = w;
    b->height = parametre == B_RETIRER || parametre == B_VOIR ? 20 : 26;
    b->left_click_handler = clic;
    b->right_click_handler = 0;
    b->parameter1 = parametre;
    b->parameter2 = parametre2;
}

static void preparer_boutons_session(void)
{
    nb_session = 0;
    for (int i = 0; i < etat.nb_joueurs; i++) {
        if (etat.hote && etat.joueurs[i].numero != etat.joueur_local) {
            ajouter_session(X0 + LARGEUR - 120, 130 + i * 22, 88, B_RETIRER, etat.joueurs[i].numero);
        }
    }
    int premier = etat.nb_chat > CHAT_LIGNES ? etat.nb_chat - CHAT_LIGNES : 0;
    for (int i = premier; i < etat.nb_chat; i++) {
        if (etat.chat[i].signal >= 0) {
            ajouter_session(X0 + LARGEUR - 88, 222 + (i - premier) * 16 - 3, 56, B_VOIR, etat.chat[i].signal);
        }
    }
    ajouter_session(X0 + LARGEUR - 152, 340, 120, B_ENVOYER, 0);
    if (etat.phase == BRUTUS_MENU_HORS_VILLE) {
        ajouter_session(X0 + 24, BAS, 168, B_REVENIR, 0);
    }
    ajouter_session(X0 + 200, BAS, 160, B_QUITTER, 0);
    ajouter_session(X0 + LARGEUR - 176, BAS, 152, B_RETOUR, 0);
}

static const char *libelle_session(int p)
{
    switch (p) {
        case B_RETIRER: return "Retirer";
        case B_VOIR: return "Voir";
        case B_ENVOYER: return "Envoyer";
        case B_REVENIR: return "Revenir dans la partie";
        case B_QUITTER: return "Quitter la partie";
        default: return etat.depuis_ville ? "Retour au jeu" : "Retour";
    }
}

static void dessiner_session(void)
{
    char ligne[TEXTE_MAX];
    snprintf(ligne, sizeof(ligne), "Code de la partie : ");
    int x = text_draw(t(ligne), X0 + 24, 70, FONT_NORMAL_BLACK, 0);
    text_draw(etat.code[0] ? etat.code : t("..."), X0 + 24 + x, 66, FONT_LARGE_BLACK, 0);
    if (etat.nb_joueurs < 2 && etat.code[0]) {
        text_draw(t("Donne ce code à l'autre joueur."), X0 + 300, 70, FONT_SMALL_PLAIN, COLOR_BLACK);
    }
    text_draw_multiline(etat.texte[0] ? etat.texte : t(" "), X0 + 24, 96, LARGEUR - 48, 0, FONT_NORMAL_BLACK, 0);

    // Joueurs du salon
    inner_panel_draw(X0 + 16, 122, (LARGEUR - 32) / 16, 6);
    for (int i = 0; i < etat.nb_joueurs; i++) {
        const joueur_salon *j = &etat.joueurs[i];
        int y = 130 + i * 22;
        graphics_fill_rect(X0 + 28, y + 2, 12, 12, brutus_coop_couleur(j->numero));
        if (!j->connecte) {
            snprintf(ligne, sizeof(ligne), "Joueur %d%s%s : déconnecté", j->numero + 1,
                j->numero == etat.joueur_local ? " (toi)" : "", j->numero == 0 ? ", hôte" : "");
        } else if (j->latence >= 0) {
            snprintf(ligne, sizeof(ligne), "Joueur %d%s%s : %d ms", j->numero + 1,
                j->numero == etat.joueur_local ? " (toi)" : "", j->numero == 0 ? ", hôte" : "", j->latence);
        } else {
            snprintf(ligne, sizeof(ligne), "Joueur %d%s%s", j->numero + 1,
                j->numero == etat.joueur_local ? " (toi)" : "", j->numero == 0 ? ", hôte" : "");
        }
        text_draw(t(ligne), X0 + 48, y + 3, FONT_NORMAL_WHITE, 0);
    }

    // Chat
    text_draw(t("Messages (touche G dans la ville : signaler un endroit)"), X0 + 24, 200, FONT_SMALL_PLAIN, COLOR_BLACK);
    inner_panel_draw(X0 + 16, 214, (LARGEUR - 32) / 16, 7);
    int premier = etat.nb_chat > CHAT_LIGNES ? etat.nb_chat - CHAT_LIGNES : 0;
    for (int i = premier; i < etat.nb_chat; i++) {
        const ligne_chat *l = &etat.chat[i];
        int y = 222 + (i - premier) * 16;
        if (l->joueur == etat.joueur_local) {
            snprintf(ligne, sizeof(ligne), "Toi :");
        } else {
            snprintf(ligne, sizeof(ligne), "Joueur %d :", l->joueur + 1);
        }
        int w = text_draw(t(ligne), X0 + 28, y, FONT_NORMAL_PLAIN, brutus_coop_couleur(l->joueur));
        text_draw_ellipsized(l->texte, X0 + 32 + w, y, LARGEUR - 72 - w - (l->signal >= 0 ? 64 : 0),
            FONT_NORMAL_WHITE, 0);
    }
    if (!etat.nb_chat) {
        text_draw(t("Aucun message pour l'instant."), X0 + 28, 222, FONT_NORMAL_PLAIN, COLOR_FONT_LIGHT_GRAY);
    }
    input_box_draw(&boite_chat);

    if (etat.avis[0]) {
        text_draw_multiline(etat.avis, X0 + 24, 376, LARGEUR - 48, 0, FONT_NORMAL_PLAIN, COLOR_FONT_RED);
    }

    for (unsigned int i = 0; i < nb_session; i++) {
        const generic_button *b = &boutons_session[i];
        if (b->parameter1 == B_RETIRER || b->parameter1 == B_VOIR) {
            button_border_draw(b->x, b->y, b->width, b->height, etat.focus == i + 1);
            text_draw_centered(t(libelle_session(b->parameter1)), b->x, b->y + 5, b->width, FONT_SMALL_PLAIN,
                COLOR_WHITE);
        } else {
            bouton(b, libelle_session(b->parameter1), etat.focus == i + 1);
        }
    }
}

static void draw_foreground(void)
{
    choisir_saisie();
    graphics_in_dialog();
    outer_panel_draw(X0, Y0, LARGEUR / 16, HAUTEUR / 16);
    text_draw_centered(t("Multijoueur"), X0, Y0 + 16, LARGEUR, FONT_LARGE_BLACK, 0);
    if (!etat.disponible) {
        dessiner_indisponible();
    } else if (en_session()) {
        preparer_boutons_session();
        dessiner_session();
    } else {
        dessiner_accueil();
    }
    graphics_reset_dialog();
}

static void envoyer_chat(void)
{
    if (saisie_chat[0]) {
        demander(BRUTUS_MENU_CHAT, 0, saisie_chat);
        saisie_chat[0] = 0;
        input_box_refresh_text(&boite_chat);
    }
}

static void rejoindre(void)
{
    // Le code est en majuscules côté relais (sans 0/O/1/I/L) : on accepte la saisie en minuscules.
    for (int i = 0; saisie_code[i]; i++) {
        if (saisie_code[i] >= 'a' && saisie_code[i] <= 'z') {
            saisie_code[i] = (uint8_t) (saisie_code[i] - 'a' + 'A');
        }
    }
    if (string_length(saisie_code) > 0) {
        demander(BRUTUS_MENU_REJOINDRE, 0, saisie_code);
    }
}

static void clic(const generic_button *b)
{
    switch (b->parameter1) {
        case B_RETOUR:
            fermer();
            break;
        case B_CREER_ICI:
            demander(BRUTUS_MENU_CREER, 0, 0);
            break;
        case B_NOUVELLE:
            etat.creer_quand_ville = 1;
            arreter_saisie();
            window_cck_selection_show();
            break;
        case B_CHARGER:
            etat.creer_quand_ville = 1;
            arreter_saisie();
            window_file_dialog_show(FILE_TYPE_SAVED_GAME, FILE_DIALOG_LOAD);
            break;
        case B_REJOINDRE:
            rejoindre();
            break;
        case B_ENVOYER:
            envoyer_chat();
            break;
        case B_QUITTER:
            demander(BRUTUS_MENU_QUITTER, 0, 0);
            break;
        case B_REVENIR:
            demander(BRUTUS_MENU_REVENIR, 0, 0);
            fermer();
            break;
        case B_RETIRER:
            demander(BRUTUS_MENU_EXPULSER, b->parameter2, 0);
            break;
        case B_VOIR:
            demander(BRUTUS_MENU_VOIR, b->parameter2, 0);
            if (etat.depuis_ville) {
                fermer();
            }
            break;
    }
}

static void handle_input(const mouse *m, const hotkeys *h)
{
    const mouse *m_dialog = mouse_in_dialog(m);
    if (!etat.disponible) {
        if (generic_buttons_handle_mouse(m_dialog, 0, 0, &boutons_accueil[4], 1, &etat.focus)) {
            if (etat.focus) {
                etat.focus = 5;
            }
            return;
        }
        etat.focus = 0;
    } else if (en_session()) {
        if (input_box_handle_mouse(m_dialog, &boite_chat) ||
            generic_buttons_handle_mouse(m_dialog, 0, 0, boutons_session, nb_session, &etat.focus)) {
            return;
        }
        if (input_box_is_accepted()) {
            envoyer_chat();
            return;
        }
    } else {
        generic_button *visibles = etat.depuis_ville ? &boutons_accueil[0] : &boutons_accueil[1];
        // Accueil : « créer ici » (en ville) ou « nouvelle / charger » (au menu), puis rejoindre et retour.
        unsigned int focus = 0;
        int traite = input_box_handle_mouse(m_dialog, &boite_code);
        if (!traite) {
            if (etat.depuis_ville) {
                traite = generic_buttons_handle_mouse(m_dialog, 0, 0, visibles, 1, &focus);
                etat.focus = focus ? 1 : 0;
            } else {
                traite = generic_buttons_handle_mouse(m_dialog, 0, 0, visibles, 2, &focus);
                etat.focus = focus ? focus + 1 : 0;
            }
            if (!traite) {
                focus = 0;
                traite = generic_buttons_handle_mouse(m_dialog, 0, 0, &boutons_accueil[3], 2, &focus);
                if (focus) {
                    etat.focus = focus + 3;
                }
            }
        }
        if (traite) {
            return;
        }
        if (input_box_is_accepted()) {
            rejoindre();
            return;
        }
    }
    if (input_go_back_requested(m, h)) {
        fermer();
    }
}

void window_brutus_multijoueur_show(int depuis_ville)
{
    if (ouverte()) {
        etat.depuis_ville = depuis_ville;
        return;
    }
    window_type fenetre = {
        WINDOW_BRUTUS_MULTIJOUEUR,
        window_draw_underlying_window,
        draw_foreground,
        handle_input
    };
    etat.depuis_ville = depuis_ville;
    etat.focus = 0;
    etat.saisie = 0;
    window_show(&fenetre);
}
