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
#include "multijoueur/combat_versus.h"
#include "multijoueur/coop.h"
#include "multijoueur/recap_versus.h"
#include "multijoueur/versus.h"
#include "multijoueur/victoire_versus.h"
#include "game/cheats.h"
#include "game/state.h"
#include "scenario/property.h"
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

enum { MODE_COOP = 0, MODE_VERSUS = 1 };

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
    // M12 : choix de l'hôte (mode, options du Versus) et partie Versus à lancer au premier tour du lockstep
    int mode;   // MODE_COOP ou MODE_VERSUS
    int combat; // option du Versus
    int duree;  // mois (0 : sans limite)
    struct {
        int voulu;
        int combat;
        int duree;
        uint32_t tick_depot;
        int depose;
    } versus;
    uint8_t info[TEXTE_MAX]; // poussé par JS : mode, carte et options de la partie (donnés par l'hôte)
    // fenêtre
    int depuis_ville;
    unsigned int focus;
    int saisie; // 0 aucune, 1 code, 2 chat, 3 pseudo
    int saisie_choisie; // M12 : à l'accueil, le champ choisi d'un clic (1 code, 3 pseudo ; 0 : pas encore)
    char pseudos[BRUTUS_JOUEURS_MAX][64]; // M12 : pseudo de chaque joueur du salon (UTF-8, donné par le relais)
} etat = { .combat = 1, .duree = BRUTUS_VICTOIRE_DUREE_DEFAUT };

static uint8_t saisie_code[CODE_MAX];
static uint8_t saisie_chat[200];
static uint8_t saisie_pseudo[24]; // M12 : pseudo du joueur local (encodage du jeu)
static input_box boite_code = { 224, 316, 10, 2, FONT_NORMAL_WHITE, 0, saisie_code, 7 };
static input_box boite_chat = { 72, 340, 22, 2, FONT_NORMAL_WHITE, 1, saisie_chat, 120 };
static input_box boite_pseudo = { 160, 64, 14, 2, FONT_NORMAL_WHITE, 1, saisie_pseudo, 21 };

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
        copier_utf8(!brutus_versus_actif() ? "Partie en cours : vous gérez la même ville." :
            brutus_ville_vue() == 0 ? "Partie Versus en cours : vous jouez la ville bleue." :
            "Partie Versus en cours : vous jouez la ville rouge.", etat.texte, sizeof(etat.texte));
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

BRUTUS_RESEAU_EXPORT void brutus_menu_info(const char *info)
{
    copier_utf8(info, etat.info, sizeof(etat.info));
    window_invalidate();
}

BRUTUS_RESEAU_EXPORT void brutus_menu_joueurs_vider(void)
{
    etat.nb_joueurs = 0;
    memset(etat.pseudos, 0, sizeof(etat.pseudos));
}

// ------------------------------------------------------------------ M12 : pseudos

BRUTUS_RESEAU_EXPORT void brutus_menu_joueur_pseudo(int numero, const char *pseudo)
{
    if (numero >= 0 && numero < BRUTUS_JOUEURS_MAX) {
        snprintf(etat.pseudos[numero], sizeof(etat.pseudos[numero]), "%s", pseudo ? pseudo : "");
    }
}

BRUTUS_RESEAU_EXPORT void brutus_menu_pseudo_definir(const char *pseudo)
{
    copier_utf8(pseudo, saisie_pseudo, sizeof(saisie_pseudo));
    if (etat.saisie == 3) {
        input_box_refresh_text(&boite_pseudo);
    }
}

BRUTUS_RESEAU_EXPORT const char *brutus_menu_pseudo(void)
{
    static char utf8[64];
    encoding_to_utf8(saisie_pseudo, utf8, sizeof(utf8), 0);
    return utf8;
}

BRUTUS_RESEAU_EXPORT const char *brutus_menu_nom(int joueur)
{
    static char noms[4][64];
    static int prochain;
    if (joueur >= 0 && joueur < BRUTUS_JOUEURS_MAX && etat.pseudos[joueur][0]) {
        return etat.pseudos[joueur];
    }
    char *n = noms[prochain];
    prochain = (prochain + 1) % 4;
    snprintf(n, 64, "Joueur %d", joueur + 1);
    return n;
}

const char *brutus_versus_pseudo_ville(int ville)
{
    // En Versus réseau, la ville n est au joueur n ; en local, pas de pseudo (on dit « la ville bleue »).
    if (!brutus_versus_actif() || !brutus_reseau_actif() || ville < 0 || ville >= BRUTUS_JOUEURS_MAX) {
        return 0;
    }
    return etat.pseudos[ville][0] ? etat.pseudos[ville] : 0;
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
        snprintf(ligne, sizeof(ligne), "%s : %s", brutus_menu_nom(joueur), texte ? texte : "");
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

static void texte_duree(int mois, char *sortie, size_t taille)
{
    if (!mois) {
        snprintf(sortie, taille, "sans limite de durée");
    } else if (mois % 12 == 0) {
        snprintf(sortie, taille, "%d an%s", mois / 12, mois >= 24 ? "s" : "");
    } else {
        snprintf(sortie, taille, "%d mois", mois);
    }
}

// M12 : création d'une partie. Le texte de l'action est la ligne d'information que le relais montrera à tous
// (mode, carte, options). En Versus, la partie passera en Versus au premier tour du lockstep
// (brutus_menu_versus_lancer) ; en attendant l'autre joueur, l'hôte est en pause.
static void demander_creation(void)
{
    int versus = etat.mode == MODE_VERSUS || brutus_versus_actif();
    etat.versus.voulu = versus;
    etat.versus.combat = etat.combat;
    etat.versus.duree = etat.duree;
    etat.versus.depose = 0;
    etat.versus.tick_depot = 0;
    char carte[120];
    carte[0] = 0;
    if (scenario_name()[0]) {
        encoding_to_utf8(scenario_name(), carte, sizeof(carte), 0);
    }
    char info[TEXTE_MAX];
    if (versus) {
        char d[40];
        texte_duree(etat.duree, d, sizeof(d));
        snprintf(info, sizeof(info), "Versus%s%s%s, combat %s, %s", carte[0] ? ", carte « " : "", carte,
            carte[0] ? " »" : "", etat.combat ? "activé" : "désactivé", d);
        game_state_pause(); // pas d'avance gratuite : la partie démarre pour tous à l'arrivée de l'autre
    } else {
        snprintf(info, sizeof(info), "Coop%s%s%s", carte[0] ? ", carte « " : "", carte, carte[0] ? " »" : "");
    }
    etat.action = BRUTUS_MENU_CREER;
    etat.action_parametre = versus ? 2 : 2; // joueurs du salon (Versus : 2, une ville chacun)
    snprintf(etat.action_texte, sizeof(etat.action_texte), "%s", info);
}

void brutus_menu_versus_lancer(void)
{
    // Hôte seulement, en partie réseau : les commandes partent avec le prochain tour, et tous les joueurs
    // les appliquent au même tick (comme Alt+W, Alt+G et Alt+T en local).
    if (!etat.versus.voulu || !brutus_reseau_actif() || brutus_joueur_local() != 0) {
        return;
    }
    if (brutus_victoire_fin() != BRUTUS_FIN_AUCUNE ||
        (brutus_versus_actif() && brutus_combat_option() == etat.versus.combat &&
        brutus_victoire_duree() == etat.versus.duree)) {
        etat.versus.voulu = 0; // fait
        return;
    }
    uint32_t tick = brutus_tick_absolu();
    if (etat.versus.depose && tick < etat.versus.tick_depot + 400) {
        return; // déjà envoyées ; renvoyées si une resynchronisation les a perdues
    }
    if (!brutus_versus_actif()) {
        brutus_commande_2(BRUTUS_CMD_TRICHE, BRUTUS_TRICHE_VERSUS, 0);
    }
    brutus_commande_2(BRUTUS_CMD_TRICHE, BRUTUS_TRICHE_COMBAT, etat.versus.combat ? 1 : 0);
    brutus_commande_2(BRUTUS_CMD_TRICHE, BRUTUS_TRICHE_DUREE, etat.versus.duree);
    etat.versus.depose = 1;
    etat.versus.tick_depot = tick;
}

BRUTUS_RESEAU_EXPORT void brutus_menu_definir_partie(int versus, int combat, int duree)
{
    etat.mode = versus ? MODE_VERSUS : MODE_COOP;
    etat.combat = combat ? 1 : 0;
    etat.duree = duree < 0 ? 0 : duree;
}

BRUTUS_RESEAU_EXPORT int brutus_menu_creer(void)
{
    demander_creation();
    return etat.action_parametre;
}

BRUTUS_RESEAU_EXPORT int brutus_menu_versus_etat(int quoi)
{
    switch (quoi) {
        case 0: return brutus_versus_villes();
        case 1: return brutus_ville_vue();
        case 2: return brutus_combat_option();
        case 3: return brutus_victoire_duree();
        case 4: return brutus_victoire_fin();
        case 5: return brutus_victoire_gagnant();
        case 6: return (int) brutus_victoire_tick_fin();
        case 7: return brutus_recap_nombre();
        case 8: return (int) brutus_recap_empreinte32();
        case 9: return brutus_victoire_batiments_restants(0, 0);
        case 10: return brutus_victoire_batiments_restants(1, 0);
        default: return 0;
    }
}

int brutus_menu_mode_versus_choisi(void)
{
    return etat.creer_quand_ville && etat.mode == MODE_VERSUS;
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
        demander_creation();
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
    B_VOIR,
    B_MODE_COOP,
    B_MODE_VERSUS,
    B_COMBAT,
    B_DUREE
};

static void clic(const generic_button *b);

// Boutons visibles de l'accueil, refaits à chaque image (ils dépendent du mode choisi).
static generic_button boutons_accueil[10];
static unsigned int nb_accueil;

static generic_button boutons_session[2 + BRUTUS_JOUEURS_MAX + CHAT_LIGNES + 3];
static unsigned int nb_session;

static void arreter_saisie(void)
{
    if (etat.saisie == 1) {
        input_box_stop(&boite_code);
    } else if (etat.saisie == 2) {
        input_box_stop(&boite_chat);
    } else if (etat.saisie == 3) {
        input_box_stop(&boite_pseudo);
    }
    etat.saisie = 0;
}

static void choisir_saisie(void)
{
    int voulue = 0;
    if (etat.disponible) {
        // Accueil : le pseudo d'abord s'il est vide, sinon le code ; un clic choisit l'un ou l'autre.
        voulue = en_session() ? 2 : etat.saisie_choisie ? etat.saisie_choisie : saisie_pseudo[0] ? 1 : 3;
    }
    if (voulue == etat.saisie) {
        return;
    }
    arreter_saisie();
    if (voulue == 1) {
        input_box_start(&boite_code);
    } else if (voulue == 2) {
        input_box_start(&boite_chat);
    } else if (voulue == 3) {
        input_box_start(&boite_pseudo);
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

static int mode_impose(void)
{
    // Une ville déjà en Versus (Versus local) ne peut devenir qu'une partie Versus.
    return etat.depuis_ville && brutus_versus_actif();
}

static int versus_choisi(void)
{
    return etat.mode == MODE_VERSUS || mode_impose();
}

static void ajouter_accueil(int x, int y, int w, int h, int parametre)
{
    generic_button *b = &boutons_accueil[nb_accueil++];
    b->x = x;
    b->y = y;
    b->width = w;
    b->height = h;
    b->left_click_handler = clic;
    b->right_click_handler = 0;
    b->parameter1 = parametre;
    b->parameter2 = 0;
}

static void preparer_boutons_accueil(void)
{
    nb_accueil = 0;
    if (etat.disponible) {
        ajouter_accueil(X0 + 96, 122, 120, 24, B_MODE_COOP);
        ajouter_accueil(X0 + 224, 122, 120, 24, B_MODE_VERSUS);
        if (versus_choisi()) {
            ajouter_accueil(X0 + 96, 182, 150, 24, B_COMBAT);
            ajouter_accueil(X0 + 352, 182, 150, 24, B_DUREE);
        }
        if (etat.depuis_ville) {
            ajouter_accueil(X0 + 24, 214, 240, 26, B_CREER_ICI);
        } else {
            ajouter_accueil(X0 + 24, 214, 240, 26, B_NOUVELLE);
            ajouter_accueil(X0 + 280, 214, 240, 26, B_CHARGER);
        }
        ajouter_accueil(X0 + 352, 316, 168, 26, B_REJOINDRE);
    }
    ajouter_accueil(X0 + LARGEUR - 184, BAS, 160, 26, B_RETOUR);
}

static const char *libelle_accueil(int p, char *tampon, size_t taille)
{
    switch (p) {
        case B_MODE_COOP: return "Coop";
        case B_MODE_VERSUS: return "Versus";
        case B_COMBAT: return etat.combat ? "Activé" : "Désactivé";
        case B_DUREE: texte_duree(etat.duree, tampon, taille); return etat.duree ? tampon : "Sans limite";
        case B_CREER_ICI: return "Créer avec cette ville";
        case B_NOUVELLE: return "Nouvelle partie";
        case B_CHARGER: return "Charger une sauvegarde";
        case B_REJOINDRE: return "Rejoindre";
        default: return "Retour";
    }
}

static void dessiner_boutons_accueil(void)
{
    for (unsigned int i = 0; i < nb_accueil; i++) {
        const generic_button *b = &boutons_accueil[i];
        char tampon[40];
        const char *libelle = libelle_accueil(b->parameter1, tampon, sizeof(tampon));
        int choisi = (b->parameter1 == B_MODE_COOP && !versus_choisi()) ||
            (b->parameter1 == B_MODE_VERSUS && versus_choisi());
        if (choisi) {
            // Mode choisi : bouton enfoncé, comme un sélecteur à deux positions
            button_border_draw(b->x, b->y, b->width, b->height, 1);
            graphics_fill_rect(b->x + 3, b->y + 3, b->width - 6, b->height - 6, 0xff5a4a32);
            text_draw_centered(t(libelle), b->x, b->y + 6, b->width, FONT_NORMAL_WHITE, 0);
        } else if (b->parameter1 == B_MODE_COOP && mode_impose()) {
            button_border_draw(b->x, b->y, b->width, b->height, 0);
            text_draw_centered(t(libelle), b->x, b->y + 6, b->width, FONT_NORMAL_PLAIN, COLOR_FONT_LIGHT_GRAY);
        } else {
            button_border_draw(b->x, b->y, b->width, b->height, etat.focus == i + 1);
            text_draw_centered(t(libelle), b->x, b->y + (b->height == 24 ? 6 : 7), b->width, FONT_NORMAL_BLACK, 0);
        }
    }
}

// Deux champs dans la même fenêtre : seul celui qui reçoit le clavier passe par input_box_draw (curseur,
// défilement du texte) ; l'autre est dessiné en entier, sans curseur.
static void dessiner_boite(const input_box *b, int active)
{
    if (active) {
        input_box_draw(b);
        return;
    }
    inner_panel_draw(b->x, b->y, b->width_blocks, b->height_blocks);
    text_draw_ellipsized(b->text, b->x + 16, b->y + 10, b->width_blocks * 16 - 32, b->font, 0);
}

static void dessiner_indisponible(void)
{
    text_draw_multiline(t(
        "Le multijoueur se joue dans la version navigateur de Brutus III, une fois le serveur de parties "
        "en ligne (bientôt sur brutus.ivwi.fr). En attendant, il fonctionne en local : voir le README, "
        "section « Multijoueur en local »."), X0 + 24, 90, LARGEUR - 48, 0, FONT_NORMAL_BLACK, 0);
    dessiner_boutons_accueil();
}

static void dessiner_accueil(void)
{
    text_draw(t("Pseudo :"), X0 + 24, 74, FONT_NORMAL_BLACK, 0);
    dessiner_boite(&boite_code, etat.saisie == 1);
    dessiner_boite(&boite_pseudo, etat.saisie == 3);
    text_draw_multiline(t("Ton nom pour les autres joueurs (20 caractères)."), X0 + 352, 68, LARGEUR - 376, 0,
        FONT_SMALL_PLAIN, COLOR_BLACK);
    text_draw_multiline(t("Chacun a besoin de ses propres fichiers du jeu et garde son onglet visible (un onglet masqué "
        "ralentit tout le monde)."), X0 + 24, 398, LARGEUR - 220, 0, FONT_SMALL_PLAIN, COLOR_BLACK);

    text_draw(t("Créer une partie"), X0 + 24, 102, FONT_NORMAL_BLACK, 0);
    text_draw(t("Mode :"), X0 + 24, 128, FONT_NORMAL_BLACK, 0);
    const char *aide;
    if (mode_impose()) {
        aide = "Cette ville est déjà en Versus : la partie sera un Versus, chacun sa ville (2 joueurs).";
    } else if (versus_choisi()) {
        aide = "Versus : chacun sa ville sur la même carte (2 joueurs). Victoire : détruire la ville de l'autre, "
            "ou le meilleur score à la fin de la durée.";
    } else {
        aide = "Coop : vous gérez la même ville à plusieurs, avec les mêmes pouvoirs.";
    }
    text_draw_multiline(t(aide), X0 + 24, 152, LARGEUR - 48, 0, FONT_SMALL_PLAIN, COLOR_BLACK);
    if (versus_choisi()) {
        text_draw(t("Combat :"), X0 + 24, 188, FONT_NORMAL_BLACK, 0);
        text_draw(t("Durée :"), X0 + 284, 188, FONT_NORMAL_BLACK, 0);
    }
    if (etat.creer_quand_ville) {
        text_draw(t("La partie multijoueur sera créée dès que la ville sera à l'écran."), X0 + 24, 246, FONT_SMALL_PLAIN, COLOR_BLACK);
    } else if (!etat.depuis_ville && versus_choisi()) {
        text_draw(t("Nouvelle partie : les cartes à deux joueurs sont en tête de liste."), X0 + 24, 246, FONT_SMALL_PLAIN, COLOR_BLACK);
    } else if (etat.depuis_ville) {
        text_draw(t("La ville à l'écran devient la partie multijoueur. Tu recevras un code à donner à l'autre joueur."),
            X0 + 24, 246, FONT_SMALL_PLAIN, COLOR_BLACK);
    }

    text_draw(t("Rejoindre une partie"), X0 + 24, 268, FONT_NORMAL_BLACK, 0);
    text_draw_multiline(t("Entre le code de 6 caractères donné par l'autre joueur. Tu peux rester au menu : tu recevras sa ville."),
        X0 + 24, 288, LARGEUR - 48, 0, FONT_SMALL_PLAIN, COLOR_BLACK);
    text_draw(t("Code :"), X0 + 112, 325, FONT_NORMAL_BLACK, 0);

    if (etat.texte[0]) {
        text_draw_multiline(etat.texte, X0 + 24, 360, LARGEUR - 48, 0, FONT_NORMAL_PLAIN,
            etat.phase == BRUTUS_MENU_ERREUR ? COLOR_FONT_RED : COLOR_BLACK);
    }
    dessiner_boutons_accueil();
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
    if (etat.info[0]) {
        text_draw_ellipsized(etat.info, X0 + 250, 72, LARGEUR - 274, FONT_SMALL_PLAIN, COLOR_BLACK);
    } else if (etat.nb_joueurs < 2 && etat.code[0]) {
        text_draw(t("Donne ce code à l'autre joueur."), X0 + 300, 70, FONT_SMALL_PLAIN, COLOR_BLACK);
    }
    text_draw_multiline(etat.texte[0] ? etat.texte : t(" "), X0 + 24, 96, LARGEUR - 48, 0, FONT_NORMAL_BLACK, 0);

    // Joueurs du salon
    inner_panel_draw(X0 + 16, 122, (LARGEUR - 32) / 16, 6);
    for (int i = 0; i < etat.nb_joueurs; i++) {
        const joueur_salon *j = &etat.joueurs[i];
        int y = 130 + i * 22;
        graphics_fill_rect(X0 + 28, y + 2, 12, 12, brutus_coop_couleur(j->numero));
        const char *qui = brutus_menu_nom(j->numero);
        const char *moi = j->numero == etat.joueur_local ? " (toi)" : "";
        const char *hote = j->numero == 0 ? ", hôte" : "";
        if (!j->connecte) {
            snprintf(ligne, sizeof(ligne), "%s%s%s : déconnecté", qui, moi, hote);
        } else if (j->latence >= 0) {
            snprintf(ligne, sizeof(ligne), "%s%s%s : %d ms", qui, moi, hote, j->latence);
        } else {
            snprintf(ligne, sizeof(ligne), "%s%s%s", qui, moi, hote);
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
            snprintf(ligne, sizeof(ligne), "%s :", brutus_menu_nom(l->joueur));
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
    text_draw_centered(t("Multijoueur"), X0, Y0 + 12, LARGEUR, FONT_LARGE_BLACK, 0);
    if (!etat.disponible) {
        preparer_boutons_accueil();
        dessiner_indisponible();
    } else if (en_session()) {
        preparer_boutons_session();
        dessiner_session();
    } else {
        preparer_boutons_accueil();
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

static int duree_suivante(int duree)
{
    static const int durees[] = { 60, 120, 240, 0 };
    for (int i = 0; i < 4; i++) {
        if (durees[i] == duree) {
            return durees[(i + 1) % 4];
        }
    }
    return durees[0];
}

static void clic(const generic_button *b)
{
    switch (b->parameter1) {
        case B_RETOUR:
            fermer();
            break;
        case B_MODE_COOP:
            if (!mode_impose()) {
                etat.mode = MODE_COOP;
            }
            break;
        case B_MODE_VERSUS:
            etat.mode = MODE_VERSUS;
            break;
        case B_COMBAT:
            etat.combat = !etat.combat;
            break;
        case B_DUREE:
            etat.duree = duree_suivante(etat.duree);
            break;
        case B_CREER_ICI:
            demander_creation();
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
    window_invalidate();
}

static void handle_input(const mouse *m, const hotkeys *h)
{
    const mouse *m_dialog = mouse_in_dialog(m);
    if (!etat.disponible) {
        preparer_boutons_accueil();
        if (generic_buttons_handle_mouse(m_dialog, 0, 0, boutons_accueil, nb_accueil, &etat.focus)) {
            return;
        }
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
        preparer_boutons_accueil();
        if (input_box_handle_mouse(m_dialog, &boite_pseudo)) {
            etat.saisie_choisie = 3;
            return;
        }
        if (input_box_handle_mouse(m_dialog, &boite_code)) {
            etat.saisie_choisie = 1;
            return;
        }
        if (generic_buttons_handle_mouse(m_dialog, 0, 0, boutons_accueil, nb_accueil, &etat.focus)) {
            return;
        }
        if (input_box_is_accepted()) {
            if (etat.saisie == 3) {
                etat.saisie_choisie = 1; // Entrée dans le pseudo : on passe au code
            } else {
                rejoindre();
            }
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

// ------------------------------------------------------------------ M12 : cartes à deux en tête de liste

#include "core/dir.h"
#include "multijoueur/carte_versus.h"

#include <stdlib.h>

void brutus_menu_trier_cartes(dir_listing *cartes)
{
    if (!cartes || !brutus_menu_mode_versus_choisi() || cartes->num_files < 2) {
        return;
    }
    // Tri stable : les cartes à deux (entrée du joueur 2) d'abord, chaque groupe dans l'ordre d'origine
    dir_entry *tri = malloc(sizeof(dir_entry) * (size_t) cartes->num_files);
    char *a_deux = malloc((size_t) cartes->num_files);
    if (!tri || !a_deux) {
        free(tri);
        free(a_deux);
        return;
    }
    for (int i = 0; i < cartes->num_files; i++) {
        a_deux[i] = (char) brutus_carte_versus_fichier_a_deux(
            dir_get_file_at_location(cartes->files[i].name, PATH_LOCATION_SCENARIO));
    }
    int n = 0;
    for (int passe = 1; passe >= 0; passe--) {
        for (int i = 0; i < cartes->num_files; i++) {
            if (a_deux[i] == passe) {
                tri[n++] = cartes->files[i];
            }
        }
    }
    memcpy(cartes->files, tri, sizeof(dir_entry) * (size_t) cartes->num_files);
    free(tri);
    free(a_deux);
}
