// Brutus III : mode rejouage sans affichage (voir rejouage.h)
#include "multijoueur/rejouage.h"

#include "building/building.h"
#include "city/finance.h"
#include "city/map.h"
#include "city/population.h"
#include "core/config.h"
#include "game/file.h"
#include "game/settings.h"
#include "game/state.h"
#include "game/tick.h"
#include "map/data.h"
#include "map/grid.h"
#include "map/terrain.h"
#include "multijoueur/commande.h"
#include "multijoueur/empreinte.h"

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
    MODE_INFO_CARTE
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
} options = { MODE_AUCUN, 0, 0, 0, 0, 0, 0, -1, 50 };

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
        } else if (strcmp(a, "--creer-sauvegarde") == 0 && i + 2 < argc) {
            options.mode = MODE_CREER_SAUVEGARDE;
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
    if (options.mode != MODE_AUCUN) {
        options.sauvegarde = absolu(options.sauvegarde);
        options.journal = absolu(options.journal);
        options.empreintes = absolu(options.empreintes);
        options.sortie = absolu(options.sortie);
        options.sauvegarde_finale = absolu(options.sauvegarde_finale);
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

static int rejouer(void)
{
    if (!options.empreintes) {
        fprintf(stderr, "Brutus III : --empreintes <sortie> manquant\n");
        return 3;
    }
    imposer_reglages();
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
    long ticks = options.ticks;
    if (ticks < 0) {
        ticks = journal.taille ? (long) (journal.items[journal.taille - 1].tick - debut) + 1 : 0;
        if (ticks < 0) {
            ticks = 0;
        }
    }
    fprintf(sortie, "# brutus-empreintes 1\n");
    fprintf(sortie, "# tick_depart %u ticks %ld tous_les %ld commandes_journal %d\n",
        (unsigned int) debut, ticks, options.tous_les, journal.taille);
    fprintf(sortie, "%u %016llx\n", (unsigned int) debut, (unsigned long long) brutus_empreinte_partie());

    int suivante = 0;
    unsigned int nb_empreintes = 1;
    for (long t = 0; t < ticks; t++) {
        uint32_t tick = brutus_tick_absolu();
        while (suivante < journal.taille && journal.items[suivante].tick <= tick) {
            if (!brutus_commande_ajouter(&journal.items[suivante])) {
                // File pleine : on applique tout de suite ce qui est prêt et on recommence.
                brutus_commandes_appliquer(tick);
                continue;
            }
            suivante++;
        }
        game_tick_run();
        if ((t + 1) % options.tous_les == 0 || t + 1 == ticks) {
            fprintf(sortie, "%u %016llx\n", (unsigned int) brutus_tick_absolu(),
                (unsigned long long) brutus_empreinte_partie());
            nb_empreintes++;
            if (nb_empreintes % 20 == 0) {
                printf("tick %u : population %d, trésor %d, %d bâtiments\n", (unsigned int) brutus_tick_absolu(),
                    city_population(), city_finance_treasury(), compter_batiments());
            }
        }
    }
    fprintf(sortie, "# fin tick %u empreintes %u commandes_appliquees %u commandes_restantes %d"
        " population %d tresor %d batiments %d\n",
        (unsigned int) brutus_tick_absolu(), nb_empreintes, brutus_commandes_total_appliquees(),
        journal.taille - suivante, city_population(), city_finance_treasury(), compter_batiments());
    fclose(sortie);
    free(journal.items);
    printf("Brutus III : rejouage terminé : %ld ticks, %u empreintes, %u commandes appliquées, "
        "population %d, trésor %d, %d bâtiments\n", ticks, nb_empreintes, brutus_commandes_total_appliquees(),
        city_population(), city_finance_treasury(), compter_batiments());

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

int brutus_rejouage_executer(void)
{
    switch (options.mode) {
        case MODE_REJOUER:
            return rejouer();
        case MODE_CREER_SAUVEGARDE:
            return creer_sauvegarde();
        case MODE_INFO_CARTE:
            return info_carte();
        default:
            return 0;
    }
}
