// Brutus III : mode rejouage sans affichage (voir rejouage.h)
#include "multijoueur/fin_versus.h"
#include "multijoueur/recap_versus.h"
#include "multijoueur/victoire_versus.h"
#include "multijoueur/rejouage.h"
#include "multijoueur/combat_versus.h"
#include "city/ratings.h"
#include "city/warning.h"
#include "city/view.h"
#include "game/file_editor.h"
#include "game/game.h"
#include "game/tick.h"
#include "map/grid.h"
#include "window/editor/build_menu.h"
#include "multijoueur/carte_versus.h"
#include "multijoueur/generateur_cartes.h"
#include "graphics/screenshot.h"
#include "scenario/request.h"
#include "multijoueur/versus.h"

#include "building/building.h"
#include "city/finance.h"
#include "city/map.h"
#include "city/population.h"
#include "core/config.h"
#include "game/file.h"
#include "game/file_io.h"
#include "game/settings.h"
#include "game/state.h"
#include "game/tick.h"
#include "map/data.h"
#include "map/grid.h"
#include "map/terrain.h"
#include "multijoueur/commande.h"
#include "multijoueur/empreinte.h"
#include "multijoueur/entete.h"
#include "multijoueur/rejouage_interface.h"
#include "multijoueur/simulation.h"
#include "game/game.h"
#include "graphics/window.h"
#include "window/city.h"

#include "SDL.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

enum {
    MODE_AUCUN = 0,
    MODE_REJOUER,
    MODE_CREER_SAUVEGARDE,
    MODE_INFO_CARTE,
    MODE_RESEAU,
    MODE_CREER_CARTE_VERSUS, // M9
    MODE_CAPTURE_EDITEUR // M9 : carte ouverte dans l'éditeur, menu des points, image de l'écran
};

static struct {
    int mode;
    const char *sauvegarde;
    const char *journal;
    const char *empreintes;
    const char *carte;
    const char *sortie;
    const char *sauvegarde_finale;
    long ticks;
    long tous_les;
    const char *detail;
    int multijoueur;
    int interface_test;
    int capture; // M8 : à la fin, image de toute la ville et de la mini-carte (dossier des captures)
    long ticks_par_image;
    long resync_au_tick[64];
    int joueur_local;
    int nb_resync;
    int nb_reglages;
    const char *reglages[32];
    const char *prechargement;
    long ticks_prechargement;
} options = { MODE_AUCUN, 0, 0, 0, 0, 0, 0, -1, 50, 0, 0, 0, 2, { 0 }, 0, 0, { 0 }, 0, 0 };

// Le moteur change de dossier courant au démarrage : les chemins relatifs sont rendus
// absolus tout de suite (par rapport au dossier courant de lancement).
static const char *absolu(const char *chemin)
{
    if (!chemin || chemin[0] == '/' || chemin[0] == '\\' || strcmp(chemin, "-") == 0 ||
        (chemin[0] && chemin[1] == ':')) {
        return chemin;
    }
    char dossier[2048];
    if (!getcwd(dossier, sizeof(dossier))) {
        return chemin;
    }
    size_t taille = strlen(dossier) + strlen(chemin) + 2;
    char *resultat = malloc(taille);
    if (!resultat) {
        return chemin;
    }
    snprintf(resultat, taille, "%s/%s", dossier, chemin);
    return resultat;
}

int brutus_rejouage_extraire_options(int argc, char **argv)
{
    int sortie = 1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--rejouer") == 0 && i + 2 < argc) {
            options.mode = MODE_REJOUER;
            options.sauvegarde = argv[++i];
            options.journal = argv[++i];
        } else if (strcmp(a, "--empreintes") == 0 && i + 1 < argc) {
            options.empreintes = argv[++i];
        } else if (strcmp(a, "--ticks") == 0 && i + 1 < argc) {
            options.ticks = strtol(argv[++i], 0, 10);
        } else if (strcmp(a, "--tous-les") == 0 && i + 1 < argc) {
            options.tous_les = strtol(argv[++i], 0, 10);
        } else if (strcmp(a, "--sauvegarde-finale") == 0 && i + 1 < argc) {
            options.sauvegarde_finale = argv[++i];
        } else if (strcmp(a, "--detail-empreinte") == 0 && i + 1 < argc) {
            options.detail = argv[++i];
        } else if (strcmp(a, "--multijoueur") == 0) {
            options.multijoueur = 1;
        } else if (strcmp(a, "--joueur-local") == 0 && i + 1 < argc) {
            // M12 : rejoue comme le joueur N d'une partie réseau (en Versus, il voit sa ville)
            options.multijoueur = 1;
            options.joueur_local = strtol(argv[++i], 0, 10);
        } else if (strcmp(a, "--capture") == 0) {
            options.capture = 1;
        } else if (strcmp(a, "--interface-test") == 0) {
            options.multijoueur = 1;
            options.interface_test = 1;
        } else if (strcmp(a, "--ticks-par-image") == 0 && i + 1 < argc) {
            options.ticks_par_image = strtol(argv[++i], 0, 10);
        } else if (strcmp(a, "--resync-au-tick") == 0 && i + 1 < argc) {
            // Liste séparée par des virgules : 30000,60000,90000
            const char *p = argv[++i];
            while (*p && options.nb_resync < 64) {
                char *fin;
                long v = strtol(p, &fin, 10);
                if (fin == p) {
                    break;
                }
                options.resync_au_tick[options.nb_resync++] = v;
                p = *fin == ',' ? fin + 1 : fin;
            }
        } else if (strcmp(a, "--prechargement") == 0 && i + 2 < argc) {
            options.prechargement = argv[++i];
            options.ticks_prechargement = strtol(argv[++i], 0, 10);
        } else if (strcmp(a, "--reglage") == 0 && i + 1 < argc) {
            if (options.nb_reglages < 32) {
                options.reglages[options.nb_reglages++] = argv[i + 1];
            }
            i++;
        } else if (strcmp(a, "--creer-sauvegarde") == 0 && i + 2 < argc) {
            options.mode = MODE_CREER_SAUVEGARDE;
            options.carte = argv[++i];
            options.sortie = argv[++i];
        } else if (strcmp(a, "--reseau") == 0 && i + 1 < argc) {
            // M6b : test du client réseau sous Node (voir brutus_rejouage_garder_actif)
            options.mode = MODE_RESEAU;
            options.sauvegarde = argv[++i];
        } else if (strcmp(a, "--capture-editeur") == 0 && i + 1 < argc) {
            options.mode = MODE_CAPTURE_EDITEUR;
            options.carte = argv[++i];
        } else if (strcmp(a, "--creer-carte-versus") == 0 && i + 2 < argc) {
            options.mode = MODE_CREER_CARTE_VERSUS;
            options.carte = argv[++i];
            options.sortie = argv[++i];
        } else if (strcmp(a, "--info-carte") == 0 && i + 2 < argc) {
            options.mode = MODE_INFO_CARTE;
            options.sauvegarde = argv[++i];
            options.sortie = argv[++i];
        } else {
            argv[sortie++] = argv[i];
        }
    }
    if (options.tous_les <= 0) {
        options.tous_les = 50;
    }
    if (options.ticks_par_image <= 0) {
        options.ticks_par_image = 2;
    }
    if (options.mode != MODE_AUCUN) {
        options.sauvegarde = absolu(options.sauvegarde);
        options.journal = absolu(options.journal);
        options.empreintes = absolu(options.empreintes);
        options.sortie = absolu(options.sortie);
        options.sauvegarde_finale = absolu(options.sauvegarde_finale);
        options.detail = absolu(options.detail);
        options.prechargement = absolu(options.prechargement);
        // Sans affichage ni son (sauf si l'appelant a choisi autre chose)
#ifdef _WIN32
        if (!getenv("SDL_VIDEODRIVER")) {
            _putenv_s("SDL_VIDEODRIVER", "dummy");
        }
        if (!getenv("SDL_AUDIODRIVER")) {
            _putenv_s("SDL_AUDIODRIVER", "dummy");
        }
#else
        setenv("SDL_VIDEODRIVER", "dummy", 0);
        setenv("SDL_AUDIODRIVER", "dummy", 0);
#endif
    }
    return sortie;
}

int brutus_rejouage_demande(void)
{
    return options.mode != MODE_AUCUN;
}

// Réglages qui influencent la simulation mais ne sont pas dans la sauvegarde :
// valeurs fixes en rejouage (en multijoueur, ils devront être partagés en début de partie).
static void imposer_reglages(void)
{
    for (int k = 0; k < CONFIG_MAX_ENTRIES; k++) {
        config_set(k, config_get_default_value(k));
    }
    config_set(CONFIG_GP_CH_YEARLY_AUTOSAVE, 0);
    if (setting_monthly_autosave()) {
        setting_toggle_monthly_autosave();
    }
    if (!setting_gods_enabled()) {
        setting_toggle_gods_enabled();
    }
    while (setting_difficulty() > DIFFICULTY_NORMAL) {
        setting_decrease_difficulty();
    }
    while (setting_difficulty() < DIFFICULTY_NORMAL) {
        setting_increase_difficulty();
    }
    // M6a : réglages locaux demandés (--reglage nom=valeur), par exemple pour vérifier que
    // l'en-tête d'une sauvegarde l'emporte sur eux.
    for (int i = 0; i < options.nb_reglages; i++) {
        char nom[128];
        const char *egal = strchr(options.reglages[i], '=');
        size_t n = egal ? (size_t) (egal - options.reglages[i]) : 0;
        if (!egal || n >= sizeof(nom)) {
            fprintf(stderr, "Brutus III : --reglage nom=valeur attendu : %s\n", options.reglages[i]);
            continue;
        }
        memcpy(nom, options.reglages[i], n);
        nom[n] = 0;
        if (!brutus_entete_definir_local(nom, atoi(egal + 1))) {
            fprintf(stderr, "Brutus III : réglage inconnu : %s\n", nom);
        }
    }
}

static int charger_sauvegarde(const char *chemin)
{
    int resultat = game_file_load_saved_game(chemin);
    if (resultat != 1) {
        fprintf(stderr, "Brutus III : impossible de charger la sauvegarde %s (code %d)\n", chemin, resultat);
        return 0;
    }
    game_state_unpause();
    return 1;
}

static int compter_batiments(void)
{
    int n = 0;
    for (unsigned int i = 1; i < (unsigned int) building_count(); i++) {
        if (building_get(i)->state == BUILDING_STATE_IN_USE) {
            n++;
        }
    }
    return n;
}

typedef struct {
    brutus_commande *items;
    int taille;
    int capacite;
} liste_commandes;

static int lire_journal(const char *chemin, liste_commandes *liste)
{
    liste->items = 0;
    liste->taille = 0;
    liste->capacite = 0;
    if (!chemin || strcmp(chemin, "-") == 0) {
        return 1;
    }
    FILE *fp = fopen(chemin, "r");
    if (!fp) {
        fprintf(stderr, "Brutus III : journal introuvable : %s\n", chemin);
        return 0;
    }
    char ligne[1024];
    int numero = 0;
    while (fgets(ligne, sizeof(ligne), fp)) {
        numero++;
        brutus_commande cmd;
        int r = brutus_commande_lire_ligne(ligne, &cmd);
        if (r < 0) {
            fprintf(stderr, "Brutus III : ligne %d du journal illisible : %s", numero, ligne);
            fclose(fp);
            return 0;
        }
        if (r == 0) {
            continue;
        }
        if (liste->taille == liste->capacite) {
            liste->capacite = liste->capacite ? liste->capacite * 2 : 1024;
            liste->items = realloc(liste->items, sizeof(brutus_commande) * liste->capacite);
            if (!liste->items) {
                fclose(fp);
                return 0;
            }
        }
        if (liste->taille > 0 && cmd.tick < liste->items[liste->taille - 1].tick) {
            fprintf(stderr, "Brutus III : ligne %d du journal : ticks non croissants\n", numero);
            fclose(fp);
            return 0;
        }
        liste->items[liste->taille++] = cmd;
    }
    fclose(fp);
    return 1;
}

// Diagnostic (--detail-empreinte) : empreinte de chaque morceau de la sauvegarde, au début et à la fin.
static void ecrire_detail(const char *moment)
{
    if (!options.detail) {
        return;
    }
    FILE *fp = fopen(options.detail, strcmp(moment, "debut") == 0 ? "w" : "a");
    if (!fp) {
        return;
    }
    fprintf(fp, "# %s tick %u\n", moment, (unsigned int) brutus_tick_absolu());
    game_file_io_brutus_empreinte_detail(fp, 0);
    fclose(fp);
    // Contenu brut des morceaux, pour comparer octet par octet (<detail>.<moment>.bin)
    char chemin[4096];
    snprintf(chemin, sizeof(chemin), "%s.%s.bin", options.detail, moment);
    FILE *brut = fopen(chemin, "wb");
    if (brut) {
        game_file_io_brutus_empreinte_detail(0, brut);
        fclose(brut);
    }
}

// Capture de l'écran (sans l'avis « capture enregistrée » de la précédente). Le nom du fichier est
// à la seconde près : on attend un peu pour que la capture suivante ne l'écrase pas.
static void capturer_ecran(void)
{
    city_warning_clear_all();
    window_draw(1);
    graphics_save_screenshot(SCREENSHOT_DISPLAY);
    city_warning_clear_all();
    SDL_Delay(1100);
}

static int rejouer(void)
{
    if (!options.empreintes) {
        fprintf(stderr, "Brutus III : --empreintes <sortie> manquant\n");
        return 3;
    }
    imposer_reglages();
    // Diagnostic (--prechargement <sauvegarde> <ticks>) : une autre partie est chargée et jouée
    // avant, pour vérifier qu'aucun état ne survit au chargement (cas de l'hôte qui recharge).
    if (options.prechargement) {
        if (!charger_sauvegarde(options.prechargement)) {
            return 4;
        }
        for (long t = 0; t < options.ticks_prechargement; t++) {
            brutus_avancer_ticks(1);
        }
    }
    if (!charger_sauvegarde(options.sauvegarde)) {
        return 4;
    }
    liste_commandes journal;
    if (!lire_journal(options.journal, &journal)) {
        return 5;
    }
    FILE *sortie = fopen(options.empreintes, "w");
    if (!sortie) {
        fprintf(stderr, "Brutus III : impossible d'écrire %s\n", options.empreintes);
        return 6;
    }
    uint32_t debut = brutus_tick_absolu();
    // Reprise depuis une sauvegarde intermédiaire (test de rechargement) : les commandes
    // datées d'avant le tick de départ sont déjà dans la sauvegarde, on les saute.
    int suivante = 0;
    while (suivante < journal.taille && journal.items[suivante].tick < debut) {
        suivante++;
    }
    const int avant_depart = suivante;
    long ticks = options.ticks;
    if (ticks < 0) {
        ticks = journal.taille ? (long) (journal.items[journal.taille - 1].tick - debut) + 1 : 0;
        if (ticks < 0) {
            ticks = 0;
        }
    }
    fprintf(sortie, "# brutus-empreintes 1\n");
    fprintf(sortie, "# tick_depart %u ticks %ld tous_les %ld commandes_journal %d commandes_avant_depart %d\n",
        (unsigned int) debut, ticks, options.tous_les, journal.taille, avant_depart);
    fprintf(sortie, "%u %016llx\n", (unsigned int) debut, (unsigned long long) brutus_empreinte_partie());
    ecrire_detail("debut");

    unsigned int nb_empreintes = 1;
    // Le rejouage ne rejoue que le journal : ce que l'interface enverrait (message lu après une
    // fenêtre de message…) est ignoré et compté.
    brutus_commandes_interface_ignorer(1);
    if (options.multijoueur) {
        // M6a : les ticks viennent de l'extérieur (comme le lockstep), image par image (game_run).
        brutus_multijoueur_definir(1);
        brutus_joueur_local_definir(options.joueur_local);
        if (options.interface_test) {
            window_city_show();
        }
    }
    long image = 0;
    for (long t = 0; t < ticks;) {
        uint32_t tick = brutus_tick_absolu();
        long pas = 1;
        if (options.multijoueur) {
            // Jamais au-delà de la prochaine empreinte ni de la fin
            pas = options.ticks_par_image;
            long prochaine = (t / options.tous_les + 1) * options.tous_les;
            if (t + pas > prochaine) {
                pas = prochaine - t;
            }
            if (t + pas > ticks) {
                pas = ticks - t;
            }
        }
        // Commandes du journal datées des ticks de ce pas (appliquées chacune avant son tick)
        while (suivante < journal.taille && journal.items[suivante].tick <= tick + (uint32_t) (pas - 1)) {
            if (brutus_commandes_file_pleine()) {
                if (journal.items[suivante].tick <= tick) {
                    // File pleine : on applique tout de suite ce qui est prêt et on recommence.
                    brutus_commandes_appliquer(tick);
                    continue;
                }
                pas = (long) (journal.items[suivante].tick - tick); // le reste attendra
                break;
            }
            brutus_commande_ajouter(&journal.items[suivante]);
            suivante++;
        }
        if (options.multijoueur) {
            if (options.interface_test) {
                brutus_interface_test_image(image);
            }
            brutus_ticks_externes_ajouter((int) pas);
            while (brutus_ticks_externes_en_attente() > 0) {
                game_run();
            }
            if (options.interface_test) {
                brutus_interface_test_dessiner(image);
            }
            image++;
        } else {
            // Comme une image du jeu en solo : tick, sauvegarde de début de mission, messages
            brutus_avancer_ticks(1);
        }
        long avant = t;
        t += pas;
        // Points de resynchronisation en ticks absolus de la partie (pas depuis le départ)
        int resync = 0;
        uint32_t tick_apres = brutus_tick_absolu();
        for (int r = 0; r < options.nb_resync; r++) {
            if ((long) tick < options.resync_au_tick[r] && (long) tick_apres >= options.resync_au_tick[r]) {
                resync = 1;
            }
        }
        if (resync) {
            // M6a : resynchronisation dans le même programme (cas de l'hôte) : écrire, recharger.
            char chemin[4096];
            snprintf(chemin, sizeof(chemin), "%s.resync-%u.svx", options.empreintes, (unsigned int) brutus_tick_absolu());
            if (!game_file_write_saved_game(chemin) || !brutus_resynchroniser(chemin)) {
                fprintf(stderr, "Brutus III : resynchronisation impossible (%s)\n", chemin);
                fclose(sortie);
                free(journal.items);
                return 8;
            }
            game_state_unpause();
            if (options.interface_test) {
                window_city_show();
            }
            fprintf(sortie, "# resynchronisation tick %u\n", (unsigned int) brutus_tick_absolu());
        }
        if (t / options.tous_les != avant / options.tous_les || t == ticks) {
            fprintf(sortie, "%u %016llx\n", (unsigned int) brutus_tick_absolu(),
                (unsigned long long) brutus_empreinte_partie());
            nb_empreintes++;
            if (nb_empreintes % 20 == 0) {
                printf("tick %u : population %d, trésor %d, %d bâtiments\n", (unsigned int) brutus_tick_absolu(),
                    city_population(), city_finance_treasury(), compter_batiments());
            }
        }
    }
    if (options.interface_test) {
        unsigned int ouvertures, glissers;
        brutus_interface_test_bilan(&ouvertures, &glissers);
        fprintf(sortie, "# interface images %ld fenetres %u glissers %u commandes_interface_ignorees %u\n",
            image, ouvertures, glissers, brutus_commandes_interface_ignorees());
    }
    brutus_commandes_ecrire_bilan(sortie);
    fprintf(sortie, "# fin tick %u empreintes %u commandes_appliquees %u commandes_refusees %u"
        " commandes_perdues %u commandes_restantes %d population %d tresor %d batiments %d\n",
        (unsigned int) brutus_tick_absolu(), nb_empreintes, brutus_commandes_total_appliquees(),
        brutus_commandes_total_refusees(), brutus_commandes_total_perdues(),
        journal.taille - suivante, city_population(), city_finance_treasury(), compter_batiments());
    fclose(sortie);
    ecrire_detail("fin");
    free(journal.items);
    printf("Brutus III : rejouage terminé : %ld ticks, %u empreintes, %u commandes appliquées, "
        "population %d, trésor %d, %d bâtiments\n", ticks, nb_empreintes, brutus_commandes_total_appliquees(),
        city_population(), city_finance_treasury(), compter_batiments());
    // M8 : en Versus, le détail de chaque ville (population, trésor, bâtiments à elle)
    for (int v = 0; brutus_versus_actif() && v < brutus_versus_villes(); v++) {
        brutus_ville_activer(v);
        int a_elle = 0;
        for (int i = 1; i < building_count(); i++) {
            building *b = building_get(i);
            if (b->state == BUILDING_STATE_IN_USE && b->brutus_proprietaire == v) {
                a_elle++;
            }
        }
        int demandes = 0;
        int servies = 0;
        for (unsigned int i = 0; i < scenario_request_count_total(); i++) {
            const scenario_request *r = scenario_request_get((int) i);
            if (r && r->resource && r->visible) {
                demandes++;
            }
            if (r && r->resource && (r->state == REQUEST_STATE_DISPATCHED || r->state == REQUEST_STATE_DISPATCHED_LATE ||
                r->state == REQUEST_STATE_RECEIVED)) {
                servies++;
            }
        }
        printf("Brutus III : ville %d : population %d, trésor %d, %d bâtiments, faveur %d, "
            "demandes de César : %d en cours, %d servies, entrée %d,%d, sortie %d,%d\n",
            v + 1, city_population(), city_finance_treasury(), a_elle, city_rating_favor(), demandes, servies,
            city_map_entry_point()->x, city_map_entry_point()->y, city_map_exit_point()->x, city_map_exit_point()->y);
    }
    brutus_ville_activer(0);
    brutus_combat_bilan(); // M10
    brutus_victoire_bilan(); // M11
    brutus_recap_bilan(); // M12

    if (options.capture) {
        // Vue de la ville du joueur local (ville 1) : les bâtiments de l'autre ville ont sa teinte
        // Les messages en attente s'ouvrent au premier dessin de la ville : on les referme.
        for (int essai = 0; essai < 50 && (essai == 0 || window_get_id() != WINDOW_CITY); essai++) {
            window_city_show();
            window_draw(1);
        }
        graphics_save_screenshot(SCREENSHOT_FULL_CITY);
        graphics_save_screenshot(SCREENSHOT_MINIMAP);
        if (brutus_victoire_fin() != BRUTUS_FIN_AUCUNE) { // M11 : l'écran de fin, sur la ville
            window_brutus_fin_versus_show();
            capturer_ecran();
            // M12 : le récap, onglet de la population avec la bulle de survol au milieu, puis le tableau
            brutus_fin_versus_onglet(BRUTUS_FIN_ONGLET_POPULATION);
            brutus_fin_versus_survol(16 + 16 + 60 + 222, 8 + 106 + 90);
            capturer_ecran();
            brutus_fin_versus_survol(-1, -1);
            brutus_fin_versus_onglet(BRUTUS_FIN_ONGLET_TABLEAU);
            capturer_ecran();
        }
    }

    if (options.sauvegarde_finale && !game_file_write_saved_game(options.sauvegarde_finale)) {
        fprintf(stderr, "Brutus III : impossible d'écrire %s\n", options.sauvegarde_finale);
        return 7;
    }
    return 0;
}

static int creer_sauvegarde(void)
{
    imposer_reglages();
    if (!game_file_start_scenario_by_name((const uint8_t *) options.carte)) {
        fprintf(stderr, "Brutus III : carte introuvable : %s\n", options.carte);
        return 4;
    }
    if (!game_file_write_saved_game(options.sortie)) {
        fprintf(stderr, "Brutus III : impossible d'écrire %s\n", options.sortie);
        return 6;
    }
    printf("Brutus III : sauvegarde de départ écrite : %s\n", options.sortie);
    return 0;
}

// Carte texte pour le bot de test : une ligne par rangée, un caractère par tuile.
static int info_carte(void)
{
    imposer_reglages();
    if (!charger_sauvegarde(options.sauvegarde)) {
        return 4;
    }
    FILE *fp = fopen(options.sortie, "w");
    if (!fp) {
        return 6;
    }
    const map_tile *entree = city_map_entry_point();
    const map_tile *sortie = city_map_exit_point();
    fprintf(fp, "largeur %d\nhauteur %d\ntick %u\nentree %d %d\nsortie %d %d\ntresor %d\n",
        map_data.width, map_data.height, (unsigned int) brutus_tick_absolu(),
        entree->x, entree->y, sortie->x, sortie->y, city_finance_treasury());
    if (brutus_carte_versus_a_points()) { // M9 : carte à deux
        map_point e2 = brutus_carte_versus_entree(1);
        map_point s2 = brutus_carte_versus_sortie(1);
        fprintf(fp, "entree2 %d %d\nsortie2 %d %d\n", e2.x, e2.y, s2.x, s2.y);
    }
    fprintf(fp, "carte\n");
    for (int y = 0; y < map_data.height; y++) {
        for (int x = 0; x < map_data.width; x++) {
            int t = map_terrain_get(map_grid_offset(x, y));
            char c = '.';
            if (t & TERRAIN_WATER) {
                c = 'w';
            } else if (t & TERRAIN_ROAD) {
                c = 'r';
            } else if (t & TERRAIN_BUILDING) {
                c = 'b';
            } else if (t & (TERRAIN_TREE | TERRAIN_SHRUB)) {
                c = 't';
            } else if (t & (TERRAIN_ROCK | TERRAIN_ELEVATION | TERRAIN_ACCESS_RAMP)) {
                c = 'x';
            } else if (t & TERRAIN_NOT_CLEAR) {
                c = 'x';
            } else if (t & TERRAIN_MEADOW) {
                c = 'm';
            }
            fputc(c, fp);
        }
        fputc('\n', fp);
    }
    fclose(fp);
    printf("Brutus III : carte écrite : %s\n", options.sortie);
    return 0;
}

// M6b : charge la sauvegarde de départ puis rend la main sans quitter : JavaScript (le client
// réseau de web/lockstep.js) pilote ensuite le moteur par les fonctions de multijoueur/reseau.h.
static int mode_reseau(void)
{
    imposer_reglages();
    if (!charger_sauvegarde(options.sauvegarde)) {
        return 4;
    }
    window_city_show();
    printf("Brutus III : moteur prêt pour le client réseau (tick %u)\n", (unsigned int) brutus_tick_absolu());
    return 0;
}

int brutus_rejouage_garder_actif(void)
{
    return options.mode == MODE_RESEAU;
}

int brutus_rejouage_executer(void)
{
    switch (options.mode) {
        case MODE_RESEAU:
            return mode_reseau();
        case MODE_REJOUER:
            return rejouer();
        case MODE_CREER_SAUVEGARDE:
            return creer_sauvegarde();
        case MODE_INFO_CARTE:
            return info_carte();
        case MODE_CAPTURE_EDITEUR: {
            // La carte s'ouvre dans l'éditeur, la vue sur l'entrée du joueur 2, le menu des points ouvert
            if (!game_init_editor() || !game_file_editor_load_scenario(options.carte)) {
                fprintf(stderr, "Brutus III : carte introuvable dans l'éditeur : %s\n", options.carte);
                return 4;
            }
            map_point e2 = brutus_carte_versus_entree(1);
            if (e2.x >= 0) {
                city_view_go_to_grid_offset(map_grid_offset(e2.x, e2.y));
            }
            for (int n = 0; n < 4; n++) {
                game_tick_run(); // les drapeaux prennent leur place
            }
            window_editor_build_menu_show(MENU_PEOPLE_POINTS);
            window_draw(1);
            graphics_save_screenshot(SCREENSHOT_DISPLAY);
            return 0;
        }
        case MODE_CREER_CARTE_VERSUS: {
            int modele = brutus_carte_versus_modele(options.carte);
            if (modele < 0) {
                fprintf(stderr, "Brutus III : modèle de carte inconnu : %s (deux-rives, lac, rivage)\n", options.carte);
                return 2;
            }
            return brutus_generer_carte_versus(modele, options.sortie) ? 0 : 6;
        }
        default:
            return 0;
    }
}
