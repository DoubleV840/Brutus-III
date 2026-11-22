// Brutus III : rythme des ticks et frontière simulation / interface (voir simulation.h)
#include "multijoueur/simulation.h"
#include "multijoueur/versus.h"

#include "building/construction.h"
#include "city/message.h"
#include "game/file.h"
#include "game/speed.h"
#include "game/tick.h"
#include "game/undo.h"
#include "graphics/window.h"
#include "multijoueur/commande.h"

// Au plus tant de ticks par image en multijoueur (rattrapage étalé sur plusieurs images).
#define MAX_TICKS_EXTERNES_PAR_IMAGE 50

static struct {
    int profondeur;
    int ticks_externes;
} etat;

void brutus_simulation_debut(void)
{
    if (etat.profondeur++ > 0) {
        return;
    }
    // L'aperçu du glisser et le fantôme de démolition sont retirés de la carte partagée,
    // avec le contexte d'annulation de l'aperçu, avant de basculer sur celui de la simulation.
    building_construction_brutus_suspendre_apercu();
    game_undo_brutus_entrer_simulation();
    brutus_versus_entrer_simulation(); // M8 : la simulation part de la ville 0
}

void brutus_simulation_fin(void)
{
    if (etat.profondeur <= 0) {
        etat.profondeur = 0;
        return;
    }
    if (--etat.profondeur > 0) {
        return;
    }
    game_undo_brutus_sortir_simulation();
    building_construction_brutus_reprendre_apercu();
    brutus_versus_sortir_simulation(); // M8 : l'interface revient à la ville vue
}

int brutus_simulation_en_cours(void)
{
    return etat.profondeur > 0;
}

int brutus_avancer_ticks(int n)
{
    if (n <= 0) {
        return 0;
    }
    int multijoueur = brutus_multijoueur_actif();
    int faits = 0;
    brutus_simulation_debut();
    for (int i = 0; i < n; i++) {
        game_tick_run();
        faits++;
        game_file_write_mission_saved_game();
        // Décision d'affichage des nouveaux messages, chez chacun, juste après le tick.
        city_message_brutus_afficher();
        // Solo : comme avant, une fenêtre qui s'ouvre (message) arrête les ticks de cette image.
        // Multijoueur : le nombre de ticks est imposé par le lockstep.
        if (!multijoueur && window_is_invalid()) {
            break;
        }
    }
    brutus_simulation_fin();
    return faits;
}

void brutus_ticks_externes_ajouter(int n)
{
    if (n > 0) {
        etat.ticks_externes += n;
    }
}

int brutus_ticks_externes_en_attente(void)
{
    return etat.ticks_externes;
}

void brutus_simulation_image(void)
{
    // Messages publiés hors tick et hors commande (interface, chargement) : affichés maintenant.
    city_message_brutus_afficher();
    if (brutus_multijoueur_actif()) {
        // Le lockstep décide : ni l'écran, ni la vitesse locale, ni la pause locale.
        int n = etat.ticks_externes;
        if (n > MAX_TICKS_EXTERNES_PAR_IMAGE) {
            n = MAX_TICKS_EXTERNES_PAR_IMAGE;
        }
        etat.ticks_externes -= n;
        brutus_avancer_ticks(n);
        return;
    }
    // Solo : les commandes de l'interface sont appliquées dès l'image suivante, avant le prochain
    // tick (même en pause). Elles portent le numéro de ce tick. Puis les ticks selon l'horloge et
    // l'écran, comme dans le jeu d'origine (calculés après les commandes, qui peuvent ouvrir une
    // fenêtre). Un seul passage dans la simulation par image, et aucun s'il n'y a rien à faire.
    int en_attente = brutus_commandes_en_attente();
    int n = 0;
    if (!en_attente) {
        n = game_speed_get_elapsed_ticks();
        if (!n) {
            return;
        }
    }
    brutus_simulation_debut();
    if (en_attente) {
        brutus_commandes_appliquer(brutus_tick_absolu());
        city_message_brutus_afficher();
        n = game_speed_get_elapsed_ticks();
    }
    brutus_avancer_ticks(n);
    brutus_simulation_fin();
}

int brutus_resynchroniser(const char *sauvegarde)
{
    // Le glisser en cours du joueur local est abandonné : la carte change sous lui.
    if (building_construction_in_progress()) {
        building_construction_cancel();
    }
    if (game_file_load_saved_game(sauvegarde) != 1) {
        return 0;
    }
    etat.ticks_externes = 0;
    return 1;
}
