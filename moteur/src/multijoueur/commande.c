// Brutus III : couche de commandes (phase 2a). Voir commande.h et docs/commandes.md.
#include "multijoueur/commande.h"
#include "multijoueur/versus.h"
#include "multijoueur/combat_versus.h" // Brutus III (M10)
#include "multijoueur/coop.h" // M7 : conflits

#include "building/building.h"
#include "game/cheats.h"
#include "city/trade_policy.h"
#include "city/military.h"
#include "city/games.h"
#include "building/roadblock.h"
#include "building/monument.h"
#include "building/industry.h"
#include "building/dock.h"
#include "building/barracks.h"
#include "building/construction.h"
#include "building/construction_clear.h"
#include "building/distribution.h"
#include "building/figure.h"
#include "building/maintenance.h"
#include "building/menu.h"
#include "building/rotation.h"
#include "building/storage.h"
#include "city/buildings.h"
#include "city/message.h"
#include "city/emperor.h"
#include "city/festival.h"
#include "city/finance.h"
#include "city/gods.h"
#include "city/labor.h"
#include "city/race_bet.h"
#include "city/request.h"
#include "city/ratings.h"
#include "city/victory.h"
#include "city/resource.h"
#include "city/view.h"
#include "core/log.h"
#include "core/random.h"
#include "empire/city.h"
#include "figure/formation.h"
#include "figure/formation_legion.h"
#include "game/orientation.h"
#include "game/resource.h"
#include "game/time.h"
#include "game/undo.h"
#include "map/bridge.h"
#include "map/building.h"
#include "map/grid.h"
#include "multijoueur/reseau.h"
#include "multijoueur/simulation.h"
#include "scenario/allowed_building.h"
#include "scenario/earthquake.h"
#include "scenario/request.h"

#include <stdlib.h>
#include <string.h>

#define FILE_MAX 4096

// Coût d'un module de grand temple (MODULE_COST de window/building/culture.c)
#define BRUTUS_COUT_MODULE 1000

static struct {
    brutus_commande file[FILE_MAX];
    unsigned int sequence[FILE_MAX]; // ordre d'arrivée, pour un tri stable
    int taille;
    unsigned int prochaine_sequence;
    unsigned int total_appliquees;
    unsigned int total_refusees;
    unsigned int total_perdues;
    unsigned int appliquees_par_type[BRUTUS_CMD_NOMBRE];
    unsigned int refusees_par_type[BRUTUS_CMD_NOMBRE];
    uint32_t repere;
    FILE *journal;
    int joueur_local;
    int multijoueur;
    int joueur_en_cours; // -1 hors commande
    int ignorer_interface;
    unsigned int interface_ignorees;
} etat = { .repere = 1, .joueur_en_cours = -1 };

static const struct {
    const char *nom;
    int nb_params;
} DESCRIPTION[BRUTUS_CMD_NOMBRE] = {
    [BRUTUS_CMD_AUCUNE] = { "aucune", 0 },
    [BRUTUS_CMD_CONSTRUIRE] = { "construire", 9 },
    [BRUTUS_CMD_DEMOLIR_CONFIRME] = { "demolir-confirme", 6 },
    [BRUTUS_CMD_ANNULER] = { "annuler", 0 },
    [BRUTUS_CMD_ROTATION_VUE] = { "rotation-vue", 1 },
    [BRUTUS_CMD_IMPOTS] = { "impots", 1 },
    [BRUTUS_CMD_SALAIRES] = { "salaires", 1 },
    [BRUTUS_CMD_PRIORITE_TRAVAIL] = { "priorite-travail", 2 },
    [BRUTUS_CMD_STOCKAGE_ETAT] = { "stockage-etat", 5 },
    [BRUTUS_CMD_STOCKAGE_TOUT] = { "stockage-tout", 3 },
    [BRUTUS_CMD_STOCKAGE_VIDER] = { "stockage-vider", 2 },
    [BRUTUS_CMD_STOCKAGE_PERMISSION] = { "stockage-permission", 4 },
    [BRUTUS_CMD_DISTRIBUTION_BIEN] = { "distribution-bien", 3 },
    [BRUTUS_CMD_DISTRIBUTION_TOUT] = { "distribution-tout", 3 },
    [BRUTUS_CMD_MISE_EN_SOMMEIL] = { "mise-en-sommeil", 2 },
    [BRUTUS_CMD_FETE] = { "fete", 2 },
    [BRUTUS_CMD_DON_CESAR] = { "don-cesar", 1 },
    [BRUTUS_CMD_SALAIRE_GOUVERNEUR] = { "salaire-gouverneur", 1 },
    [BRUTUS_CMD_COMMERCE_OUVRIR] = { "commerce-ouvrir", 1 },
    [BRUTUS_CMD_COMMERCE_STATUT] = { "commerce-statut", 2 },
    [BRUTUS_CMD_COMMERCE_SEUIL] = { "commerce-seuil", 3 },
    [BRUTUS_CMD_RESSOURCE_SOMMEIL] = { "ressource-sommeil", 1 },
    [BRUTUS_CMD_RESSOURCE_RESERVE] = { "ressource-reserve", 1 },
    [BRUTUS_CMD_DEMANDE_CESAR] = { "demande-cesar", 1 },
    [BRUTUS_CMD_LEGION_DEPLACER] = { "legion-deplacer", 3 },
    [BRUTUS_CMD_LEGION_RENTRER] = { "legion-rentrer", 1 },
    [BRUTUS_CMD_LEGION_FORMATION] = { "legion-formation", 2 },
    [BRUTUS_CMD_LEGION_SERVICE] = { "legion-service", 1 },
    [BRUTUS_CMD_REPARER_CONFIRME] = { "reparer-confirme", 4 },
    [BRUTUS_CMD_PARI_COURSE] = { "pari-course", 2 },
    [BRUTUS_CMD_MESSAGE_LU] = { "message-lu", 1 },
    [BRUTUS_CMD_MESSAGE_SUPPRIMER] = { "message-supprimer", 1 },
    [BRUTUS_CMD_DEPOT_ORDRE] = { "depot-ordre", 4 },
    [BRUTUS_CMD_MONUMENT_ARRET] = { "monument-arret", 2 },
    [BRUTUS_CMD_MONUMENT_MODULE] = { "monument-module", 3 },
    [BRUTUS_CMD_MONNAIE_CONVERSION] = { "monnaie-conversion", 3 },
    [BRUTUS_CMD_PRODUCTION_RESERVE] = { "production-reserve", 2 },
    [BRUTUS_CMD_DOCK_ROUTE] = { "dock-route", 4 },
    [BRUTUS_CMD_COMMERCE_POLITIQUE] = { "commerce-politique", 2 },
    [BRUTUS_CMD_BARRAGE_PERMISSION] = { "barrage-permission", 3 },
    [BRUTUS_CMD_CASERNE_PRIORITE] = { "caserne-priorite", 3 },
    [BRUTUS_CMD_CASERNE_LIVRAISON] = { "caserne-livraison", 2 },
    [BRUTUS_CMD_JEUX] = { "jeux", 1 },
    [BRUTUS_CMD_DON_VILLE] = { "don-ville", 1 },
    [BRUTUS_CMD_LEGIONS_BATAILLE] = { "legions-bataille", 1 },
    [BRUTUS_CMD_LEGIONS_SERVICE_ANNULER] = { "legions-service-annuler", 0 },
    [BRUTUS_CMD_VICTOIRE_CONTINUER] = { "victoire-continuer", 1 },
    [BRUTUS_CMD_TRICHE] = { "triche", 5 },
};

void brutus_multijoueur_definir(int actif)
{
    etat.multijoueur = actif != 0;
}

int brutus_multijoueur_actif(void)
{
    return etat.multijoueur;
}

void brutus_joueur_local_definir(int joueur)
{
    if (joueur >= 0 && joueur < BRUTUS_JOUEURS_MAX) {
        etat.joueur_local = joueur;
    }
}

int brutus_joueur_local(void)
{
    return etat.joueur_local;
}

int brutus_commande_joueur_en_cours(void)
{
    return etat.joueur_en_cours;
}

int brutus_interface_pour_moi(void)
{
    // M8 (Versus) : ce qui concerne une autre ville que la nôtre ne s'affiche pas chez nous
    int ville = brutus_versus_ville_concernee();
    if (ville >= 0 && ville != brutus_ville_vue()) {
        return 0;
    }
    return etat.joueur_en_cours < 0 || etat.joueur_en_cours == etat.joueur_local;
}

uint32_t brutus_tick_absolu(void)
{
    return (uint32_t) game_time_brutus_total_days() * GAME_TIME_TICKS_PER_DAY + (uint32_t) game_time_tick();
}

uint32_t brutus_repere_simulation(void)
{
    return etat.repere;
}

void brutus_debut_tick(void)
{
    etat.repere++;
}

void brutus_partie_chargee(void)
{
    brutus_commandes_vider();
    game_undo_brutus_partie_chargee();
    city_message_brutus_partie_chargee();
    building_figure_brutus_partie_chargee();
    building_maintenance_brutus_partie_chargee();
    city_labor_brutus_partie_chargee();
    scenario_earthquake_brutus_partie_chargee();
    etat.repere++;
    random_brutus_reinitialiser_compteur();
}

const char *brutus_commande_nom(brutus_commande_type type)
{
    if (type <= BRUTUS_CMD_AUCUNE || type >= BRUTUS_CMD_NOMBRE) {
        return 0;
    }
    return DESCRIPTION[type].nom;
}

int brutus_commande_nb_params(brutus_commande_type type)
{
    if (type <= BRUTUS_CMD_AUCUNE || type >= BRUTUS_CMD_NOMBRE) {
        return 0;
    }
    return DESCRIPTION[type].nb_params;
}

int brutus_commande_ajouter(const brutus_commande *cmd)
{
    if (!brutus_commande_nom(cmd->type)) {
        return 0;
    }
    if (etat.taille >= FILE_MAX) {
        etat.total_perdues++;
        log_error("Brutus III : file de commandes pleine, commande perdue :", brutus_commande_nom(cmd->type), 0);
        return 0;
    }
    etat.file[etat.taille] = *cmd;
    etat.sequence[etat.taille] = etat.prochaine_sequence++;
    etat.taille++;
    return 1;
}

int brutus_commandes_en_attente(void)
{
    return etat.taille;
}

int brutus_commandes_file_pleine(void)
{
    return etat.taille >= FILE_MAX;
}

void brutus_commandes_interface_ignorer(int ignorer)
{
    etat.ignorer_interface = ignorer != 0;
}

unsigned int brutus_commandes_interface_ignorees(void)
{
    return etat.interface_ignorees;
}

void brutus_commande_envoyer(brutus_commande_type type, int nb_params, const int32_t *params)
{
    if (etat.ignorer_interface) {
        etat.interface_ignorees++;
        return;
    }
    // Brutus III (M6b) : avec le client réseau, la commande part dans la boîte de sortie (TOUR) ;
    // elle reviendra par le relais, comme celles des autres joueurs, avec son tick d'application.
    if (brutus_reseau_actif()) {
        brutus_reseau_deposer((int) type, nb_params, params);
        return;
    }
    brutus_commande cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = type;
    cmd.joueur = etat.joueur_local;
    cmd.tick = brutus_tick_absolu();
    cmd.nb_params = brutus_commande_nb_params(type);
    for (int i = 0; i < nb_params && i < BRUTUS_COMMANDE_MAX_PARAMS; i++) {
        cmd.p[i] = params[i];
    }
    brutus_commande_ajouter(&cmd);
}

void brutus_commande_0(brutus_commande_type type)
{
    brutus_commande_envoyer(type, 0, 0);
}

void brutus_commande_1(brutus_commande_type type, int32_t a)
{
    int32_t p[1] = { a };
    brutus_commande_envoyer(type, 1, p);
}

void brutus_commande_2(brutus_commande_type type, int32_t a, int32_t b)
{
    int32_t p[2] = { a, b };
    brutus_commande_envoyer(type, 2, p);
}

void brutus_commande_3(brutus_commande_type type, int32_t a, int32_t b, int32_t c)
{
    int32_t p[3] = { a, b, c };
    brutus_commande_envoyer(type, 3, p);
}

void brutus_commande_4(brutus_commande_type type, int32_t a, int32_t b, int32_t c, int32_t d)
{
    int32_t p[4] = { a, b, c, d };
    brutus_commande_envoyer(type, 4, p);
}

void brutus_commande_5(brutus_commande_type type, int32_t a, int32_t b, int32_t c, int32_t d, int32_t e)
{
    int32_t p[5] = { a, b, c, d, e };
    brutus_commande_envoyer(type, 5, p);
}

void brutus_commandes_vider(void)
{
    etat.taille = 0;
}

unsigned int brutus_commandes_total_appliquees(void)
{
    return etat.total_appliquees;
}

unsigned int brutus_commandes_total_refusees(void)
{
    return etat.total_refusees;
}

unsigned int brutus_commandes_total_perdues(void)
{
    return etat.total_perdues;
}

void brutus_commandes_ecrire_bilan(FILE *fp)
{
    for (int t = BRUTUS_CMD_AUCUNE + 1; t < BRUTUS_CMD_NOMBRE; t++) {
        if (etat.appliquees_par_type[t]) {
            fprintf(fp, "# bilan %s appliquees %u refusees %u\n", DESCRIPTION[t].nom,
                etat.appliquees_par_type[t], etat.refusees_par_type[t]);
        }
    }
}

void brutus_journal_enregistrer(FILE *fp)
{
    etat.journal = fp;
}

// ---------------------------------------------------------------------------
// Exécution

static building *batiment_en(int x, int y)
{
    if (x < 0 || y < 0 || x >= GRID_SIZE || y >= GRID_SIZE) {
        return 0;
    }
    unsigned int id = map_building_at(map_grid_offset(x, y));
    if (!id) {
        return 0;
    }
    building *b = building_main(building_get(id));
    if (!b || b->state == BUILDING_STATE_UNUSED || b->state == BUILDING_STATE_DELETED_BY_GAME ||
        b->state == BUILDING_STATE_DELETED_BY_PLAYER) {
        return 0;
    }
    return b;
}

static building *stockage_en(int x, int y)
{
    building *b = batiment_en(x, y);
    if (!b || b->storage_id <= 0 || (b->type != BUILDING_WAREHOUSE && b->type != BUILDING_GRANARY)) {
        return 0;
    }
    return b;
}

// Même condition que l'interface (window/building/distribution.c, toggle_resource_state)
static building *distributeur_en(int x, int y)
{
    building *b = batiment_en(x, y);
    if (!b || !(building_type_is_distributor(b->type) || building_has_supplier_inventory(b->type))) {
        return 0;
    }
    return b;
}

static int ressource_valide(int r)
{
    return r >= RESOURCE_MIN && r < RESOURCE_MAX;
}

static formation *legion(int id)
{
    if (id <= 0 || id >= formation_count()) {
        return 0;
    }
    formation *m = formation_get(id);
    if (!m->in_use || !m->is_legion || brutus_versus_formation_ignorer(m)) {
        return 0; // M10 : en Versus, chacun ne commande que ses légions
    }
    return m;
}

static int construction_autorisee(building_type type)
{
    if (type <= BUILDING_NONE || type >= BUILDING_TYPE_MAX) {
        return 0;
    }
    // Toujours au menu (voir building/menu.c, enable_house et enable_clear)
    if (type == BUILDING_CLEAR_LAND || type == BUILDING_REPAIR_LAND ||
        (type >= BUILDING_HOUSE_VACANT_LOT && type <= BUILDING_HOUSE_LUXURY_PALACE)) {
        return 1;
    }
    // Les arcs gagnés restent disponibles même si le scénario les interdit (menu.c, enable_normal)
    if (type == BUILDING_TRIUMPHAL_ARCH) {
        return city_buildings_triumphal_arch_available();
    }
    return scenario_allowed_building(type);
}

static int executer_construire(const brutus_commande *c)
{
    building_type type = c->p[0];
    int x0 = c->p[1], y0 = c->p[2], x1 = c->p[3], y1 = c->p[4];
    if (!construction_autorisee(type) ||
        x0 < 0 || y0 < 0 || x1 < 0 || y1 < 0 || x0 >= GRID_SIZE || y0 >= GRID_SIZE ||
        x1 >= GRID_SIZE || y1 >= GRID_SIZE ||
        c->p[8] < 0 || c->p[8] > 6 || (c->p[8] & 1)) {
        return 0;
    }
    // L'état d'interface du joueur local (type choisi, glisser en cours) est mis de côté.
    int apercu = building_construction_in_progress();
    building_construction_brutus_sauver();
    if (apercu) {
        building_construction_brutus_retirer_apercu();
    }
    int orientation_avant = city_view_orientation();
    city_view_brutus_forcer_orientation(c->p[8]);

    building_construction_set_type(type, 0);
    building_rotation_brutus_ecrire(c->p[5], c->p[6], c->p[7]);
    building_construction_start(x0, y0, map_grid_offset(x0, y0));
    if (building_construction_in_progress()) {
        building_construction_update(x1, y1, map_grid_offset(x1, y1));
        if (type == BUILDING_LOW_BRIDGE || type == BUILDING_SHIP_BRIDGE) {
            // Le moteur calcule le pont pendant le dessin du fantôme : on le recalcule ici.
            int longueur, direction;
            grid_slice bloquees;
            bloquees.size = 0;
            map_bridge_calculate_length_direction(x1, y1, &longueur, &direction, &bloquees);
        }
        building_construction_place();
    }

    city_view_brutus_forcer_orientation(orientation_avant);
    building_construction_brutus_restaurer();
    // Le type choisi par le joueur local a pu devenir indisponible (dernier arc de triomphe posé :
    // le moteur d'origine désélectionne alors le type, voir construction_building.c).
    if (building_construction_type() == BUILDING_TRIUMPHAL_ARCH && !city_buildings_triumphal_arch_available()) {
        building_construction_clear_type();
        apercu = 0;
    }
    if (apercu) {
        building_construction_brutus_relancer_apercu();
    }
    return 1;
}

static int zone_valide(const int32_t *p)
{
    return p[0] >= 0 && p[1] >= 0 && p[2] >= 0 && p[3] >= 0 &&
        p[0] < GRID_SIZE && p[1] < GRID_SIZE && p[2] < GRID_SIZE && p[3] < GRID_SIZE;
}

static int demande_envoyable(int id)
{
    if (id < 0 || (unsigned int) id >= scenario_request_count_total()) {
        return 0;
    }
    const scenario_request *r = scenario_request_get(id);
    // Même état que scenario_request_can_comply() : une demande déjà envoyée est refusée
    // (deux joueurs qui envoient la même demande ne retirent pas les marchandises deux fois).
    return r && r->resource != RESOURCE_NONE && r->visible &&
        (r->state == REQUEST_STATE_NORMAL || r->state == REQUEST_STATE_OVERDUE);
}

// --- M6a : commandes restantes (docs/commandes.md) ---

static int executer_depot_ordre(const int32_t *p)
{
    building *b = batiment_en(p[0], p[1]);
    if (!b || b->type != BUILDING_DEPOT) {
        return 0;
    }
    order *o = &b->data.depot.current_order;
    // Comme l'ouverture de la fenêtre du dépôt le faisait : ressource par défaut (blé)
    if (!o->resource_type) {
        o->resource_type = RESOURCE_MIN_FOOD;
    }
    switch (p[2]) {
        case 0: // ressource : comme set_order_resource + calculate_available_storages de l'interface
            if (!ressource_valide(p[3]) || !resource_is_storable(p[3])) {
                return 0;
            }
            o->resource_type = p[3];
            break;
        case 1:
        case 2:
            // 0 = aucun ; sinon un stockage en service qui accepte la ressource de l'ordre
            if (p[3] < 0 || (unsigned int) p[3] == b->id ||
                (p[3] && !building_depot_brutus_stockage_valide(p[3], o->resource_type))) {
                return 0;
            }
            if (p[2] == 1) {
                o->src_storage_id = p[3];
                if (p[3] && o->dst_storage_id == (unsigned int) p[3]) {
                    o->dst_storage_id = 0;
                }
            } else {
                o->dst_storage_id = p[3];
                if (p[3] && o->src_storage_id == (unsigned int) p[3]) {
                    o->src_storage_id = 0;
                }
            }
            break;
        case 3:
            if (p[3] < 0 || p[3] > 3) {
                return 0;
            }
            o->condition.condition_type = p[3];
            break;
        case 4:
            if (p[3] < 0 || p[3] > 40) {
                return 0;
            }
            o->condition.threshold = p[3];
            break;
        default:
            return 0;
    }
    // Source ou destination qui n'acceptent plus la ressource retirées (ce que faisait aussi
    // l'ouverture de la fenêtre)
    building_depot_brutus_normaliser_ordre(b);
    return 1;
}

static int executer_monnaie(const int32_t *p)
{
    building *b = batiment_en(p[0], p[1]);
    if (!b || b->type != BUILDING_CITY_MINT || (p[2] != RESOURCE_DENARII && p[2] != RESOURCE_GOLD)) {
        return 0;
    }
    if (b->output_resource_id == p[2]) {
        return 1; // déjà
    }
    // Comme city_mint_conversion_changed (window/building/industry.c)
    b->output_resource_id = p[2];
    b->data.industry.progress = 0;
    b->data.industry.age_months = 0;
    b->data.industry.average_production_per_month = 0;
    b->data.industry.production_current_month = 0;
    return 1;
}

static int executer_jeux(int id)
{
    if (!city_games_brutus_possible(id)) {
        return 0;
    }
    city_games_brutus_choisir(id);
    city_games_schedule(id);
    return 1;
}

// Retourne 1 si la commande a été exécutée, 0 si elle a été refusée (paramètres invalides).
static int executer(const brutus_commande *c)
{
    const int32_t *p = c->p;
    building *b;
    formation *m;
    switch (c->type) {
        case BRUTUS_CMD_CONSTRUIRE:
            return executer_construire(c);
        case BRUTUS_CMD_DEMOLIR_CONFIRME:
            if (!zone_valide(p) || p[4] < 1 || p[4] > 3) {
                return 0;
            }
            building_construction_brutus_demolir_confirme(p[0], p[1], p[2], p[3], p[4], p[5]);
            break;
        case BRUTUS_CMD_REPARER_CONFIRME:
            if (!zone_valide(p)) {
                return 0;
            }
            building_construction_brutus_reparer_confirme(p[0], p[1], p[2], p[3]);
            break;
        case BRUTUS_CMD_PARI_COURSE:
            return city_race_bet_brutus_parier(p[0], p[1]);
        case BRUTUS_CMD_ANNULER:
            // Chaque joueur n'annule que sa propre dernière construction (game/undo.c).
            if (!game_undo_brutus_appartient(c->joueur)) {
                return 0;
            }
            game_undo_perform();
            break;
        case BRUTUS_CMD_DEPOT_ORDRE:
            return executer_depot_ordre(p);
        case BRUTUS_CMD_MONUMENT_ARRET:
            if (!(b = batiment_en(p[0], p[1])) || !building_monument_is_monument(b)) {
                return 0;
            }
            building_monument_toggle_construction_halted(b);
            break;
        case BRUTUS_CMD_MONUMENT_MODULE:
            // Comme add_module (window/building/culture.c) : coût puis module, si le moteur l'accepte
            // Modules proposés par la fenêtre : 1 ou 2 (window/building/culture.c). Pas de refus
            // pour dette : l'interface d'origine ne vérifiait pas l'argent.
            if (!(b = batiment_en(p[0], p[1])) || (p[2] != 1 && p[2] != 2)) {
                return 0;
            }
            if (!building_monument_add_module(b, p[2])) {
                return 0;
            }
            city_finance_process_construction(BRUTUS_COUT_MODULE);
            break;
        case BRUTUS_CMD_MONNAIE_CONVERSION:
            return executer_monnaie(p);
        case BRUTUS_CMD_PRODUCTION_RESERVE:
            if (!(b = batiment_en(p[0], p[1])) || !building_is_primary_product_producer(b->type)) {
                return 0;
            }
            building_stockpiling_toggle(b);
            break;
        case BRUTUS_CMD_DOCK_ROUTE:
            // route 0 à 30 : building_dock_set_can_trade_with_route fait 1 << route sur un int
            if (!(b = batiment_en(p[0], p[1])) || b->type != BUILDING_DOCK || p[2] < 0 || p[2] > 30) {
                return 0;
            }
            building_dock_set_can_trade_with_route(p[2], b->id, p[3] != 0);
            break;
        case BRUTUS_CMD_COMMERCE_POLITIQUE:
            // Comme l'interface (window/advisor/trade.c) : politique puis coût
            if ((p[0] != LAND_TRADE_POLICY && p[0] != SEA_TRADE_POLICY) || p[1] <= NO_POLICY || p[1] > TRADE_POLICY_3) {
                return 0;
            }
            city_trade_policy_set(p[0], p[1]);
            city_finance_process_sundry(TRADE_POLICY_COST);
            break;
        case BRUTUS_CMD_BARRAGE_PERMISSION:
            // permission -2 à 15 : bits d'un unsigned short (building/roadblock.c)
            if (!(b = batiment_en(p[0], p[1])) || !building_type_is_roadblock(b->type) || p[2] < -2 || p[2] > 15) {
                return 0;
            }
            if (p[2] == -1) {
                building_roadblock_accept_none(b);
            } else if (p[2] == -2) {
                building_roadblock_accept_all(b);
            } else {
                building_roadblock_set_permission(p[2], b);
            }
            break;
        case BRUTUS_CMD_CASERNE_PRIORITE:
            if (!(b = batiment_en(p[0], p[1])) || b->type != BUILDING_BARRACKS || p[2] < 0 || p[2] > 1) {
                return 0;
            }
            building_barracks_set_priority(b, p[2]);
            break;
        case BRUTUS_CMD_CASERNE_LIVRAISON:
            if (!(b = batiment_en(p[0], p[1])) || b->type != BUILDING_BARRACKS) {
                return 0;
            }
            building_barracks_toggle_delivery(b);
            break;
        case BRUTUS_CMD_JEUX:
            return executer_jeux(p[0]);
        case BRUTUS_CMD_DON_VILLE:
            if (p[0] <= 0) {
                return 0;
            }
            city_emperor_set_donation_amount(p[0]); // borné par l'épargne (pari en cours déduit)
            city_emperor_donate_savings_to_city();
            break;
        case BRUTUS_CMD_LEGIONS_BATAILLE:
            // Bataille lointaine demandée par César (sinon l'interface ne propose rien)
            if (!city_request_has_troop_request()) {
                return 0;
            }
            formation_legions_dispatch_to_distant_battle();
            if (p[0]) {
                city_military_clear_empire_service_legions();
            }
            break;
        case BRUTUS_CMD_LEGIONS_SERVICE_ANNULER:
            city_military_clear_empire_service_legions();
            break;
        case BRUTUS_CMD_VICTOIRE_CONTINUER:
            if (!city_victory_has_won() || p[0] <= 0 || p[0] > 10) {
                return 0;
            }
            city_victory_continue_governing(p[0]);
            city_victory_reset();
            break;
        case BRUTUS_CMD_TRICHE:
            return game_cheat_brutus_executer(p[0], p + 1);
        case BRUTUS_CMD_MESSAGE_LU:
            return city_message_brutus_marquer_lu(p[0]);
        case BRUTUS_CMD_MESSAGE_SUPPRIMER:
            return city_message_brutus_supprimer(p[0]);
        case BRUTUS_CMD_ROTATION_VUE:
            if (p[0] == 0) {
                game_orientation_rotate_left();
            } else if (p[0] == 1) {
                game_orientation_rotate_right();
            } else if (p[0] == 2) {
                game_orientation_rotate_north();
            } else {
                return 0;
            }
            break;
        case BRUTUS_CMD_IMPOTS:
            city_finance_change_tax_percentage(p[0]);
            city_finance_estimate_taxes();
            city_finance_calculate_totals();
            break;
        case BRUTUS_CMD_SALAIRES:
            city_labor_change_wages(p[0]);
            city_finance_estimate_wages();
            city_finance_calculate_totals();
            break;
        case BRUTUS_CMD_PRIORITE_TRAVAIL:
            if (!(p[0] >= 0 && p[0] < 10 && p[1] >= 0 && p[1] <= city_labor_max_selectable_priority(p[0]))) {
                return 0;
            }
            city_labor_set_priority(p[0], p[1]);
            break;
        case BRUTUS_CMD_STOCKAGE_ETAT:
            if (!(b = stockage_en(p[0], p[1])) || !ressource_valide(p[2])) {
                return 0;
            }
            if (p[4]) {
                building_storage_cycle_partial_resource_state(b->storage_id, p[2], p[3] != 0);
            } else {
                building_storage_cycle_resource_state(b->storage_id, p[2], p[3] != 0);
            }
            break;
        case BRUTUS_CMD_STOCKAGE_TOUT:
            if (!(b = stockage_en(p[0], p[1]))) {
                return 0;
            }
            if (p[2]) {
                building_storage_accept_all(b->storage_id);
            } else {
                building_storage_accept_none(b->storage_id);
            }
            break;
        case BRUTUS_CMD_STOCKAGE_VIDER:
            if (!(b = stockage_en(p[0], p[1]))) {
                return 0;
            }
            building_storage_toggle_empty_all(b->storage_id);
            break;
        case BRUTUS_CMD_STOCKAGE_PERMISSION:
            if (!(b = batiment_en(p[0], p[1])) || b->storage_id <= 0 ||
                p[2] < BUILDING_STORAGE_PERMISSION_MARKET || p[2] > BUILDING_STORAGE_PERMISSION_CAESAR) {
                return 0;
            }
            if (p[3] < 0) {
                building_storage_toggle_permission(p[2], b);
            } else {
                building_storage_set_permission(p[2], b, p[3] != 0);
            }
            break;
        case BRUTUS_CMD_DISTRIBUTION_BIEN:
            if (!(b = distributeur_en(p[0], p[1])) || !ressource_valide(p[2])) {
                return 0;
            }
            building_distribution_toggle_good_accepted(b, p[2]);
            break;
        case BRUTUS_CMD_DISTRIBUTION_TOUT:
            if (!(b = distributeur_en(p[0], p[1]))) {
                return 0;
            }
            if (p[2]) {
                building_distribution_accept_all_goods(b);
            } else {
                building_distribution_unaccept_all_goods(b);
            }
            break;
        case BRUTUS_CMD_MISE_EN_SOMMEIL:
            if (!(b = batiment_en(p[0], p[1])) || b->state == BUILDING_STATE_RUBBLE) {
                return 0;
            }
            building_mothball_toggle(b);
            break;
        case BRUTUS_CMD_FETE:
            if (city_finance_out_of_money() || p[0] < 0 || p[0] >= MAX_GODS) {
                return 0;
            }
            city_festival_select_god(p[0]);
            if (!city_festival_select_size(p[1])) {
                return 0;
            }
            city_festival_schedule();
            break;
        case BRUTUS_CMD_DON_CESAR:
            // gifts[] n'est pas borné dans city/emperor.c : on borne ici.
            if (p[0] < GIFT_MODEST || p[0] >= GIFT_MAX || !city_emperor_can_send_gift(p[0]) ||
                !city_emperor_set_gift_size(p[0])) {
                return 0;
            }
            city_emperor_send_gift();
            break;
        case BRUTUS_CMD_SALAIRE_GOUVERNEUR:
            if (p[0] < 0 || p[0] > 10 || city_victory_has_won()) {
                return 0;
            }
            city_emperor_set_salary_rank(p[0]);
            city_finance_update_salary();
            city_ratings_update_favor_explanation();
            break;
        case BRUTUS_CMD_COMMERCE_OUVRIR:
            if (p[0] <= 0 || p[0] >= empire_city_get_array_size() || !empire_city_get(p[0]) ||
                empire_city_get(p[0])->is_open) {
                return 0;
            }
            empire_city_open_trade(p[0], 1);
            building_menu_update();
            break;
        case BRUTUS_CMD_COMMERCE_STATUT:
            if (!ressource_valide(p[0]) || (p[1] != TRADE_STATUS_IMPORT && p[1] != TRADE_STATUS_EXPORT)) {
                return 0;
            }
            city_resource_cycle_trade_status(p[0], p[1]);
            break;
        case BRUTUS_CMD_COMMERCE_SEUIL:
            if (!ressource_valide(p[0])) {
                return 0;
            }
            if (p[1]) {
                city_resource_change_export_over(p[0], p[2]);
            } else {
                city_resource_change_import_over(p[0], p[2]);
            }
            break;
        case BRUTUS_CMD_RESSOURCE_SOMMEIL:
            if (!ressource_valide(p[0])) {
                return 0;
            }
            city_resource_toggle_mothballed(p[0]);
            break;
        case BRUTUS_CMD_RESSOURCE_RESERVE:
            if (!ressource_valide(p[0])) {
                return 0;
            }
            city_resource_toggle_stockpiled(p[0]);
            break;
        case BRUTUS_CMD_DEMANDE_CESAR:
            if (!demande_envoyable(p[0])) {
                return 0;
            }
            scenario_request_dispatch(p[0]);
            break;
        case BRUTUS_CMD_LEGION_DEPLACER:
            if (!(m = legion(p[0])) || m->in_distant_battle || m->cursed_by_mars ||
                p[1] < 0 || p[2] < 0 || p[1] >= GRID_SIZE || p[2] >= GRID_SIZE) {
                return 0;
            } else {
                map_tile tuile;
                tuile.x = p[1];
                tuile.y = p[2];
                tuile.grid_offset = map_grid_offset(p[1], p[2]);
                formation_legion_move_to(m, &tuile);
            }
            break;
        case BRUTUS_CMD_LEGION_RENTRER:
            if (p[0] == 0) {
                formation_legion_return_home_all();
            } else if ((m = legion(p[0])) && !m->in_distant_battle) {
                formation_legion_return_home(m);
            } else {
                return 0;
            }
            break;
        case BRUTUS_CMD_LEGION_FORMATION:
            // Dispositions choisissables par le joueur : de FORMATION_COLUMN à FORMATION_MOP_UP
            if (!(m = legion(p[0])) || m->in_distant_battle || p[1] < FORMATION_COLUMN || p[1] > FORMATION_MOP_UP) {
                return 0;
            }
            formation_legion_change_layout(m, p[1]);
            break;
        case BRUTUS_CMD_LEGION_SERVICE:
            if (!legion(p[0])) {
                return 0;
            }
            formation_toggle_empire_service(p[0]);
            formation_calculate_figures();
            break;
        default:
            return 0;
    }
    return 1;
}

static int comparer(const void *a, const void *b)
{
    const int ia = *(const int *) a;
    const int ib = *(const int *) b;
    const brutus_commande *ca = &etat.file[ia];
    const brutus_commande *cb = &etat.file[ib];
    if (ca->tick != cb->tick) {
        return ca->tick < cb->tick ? -1 : 1;
    }
    if (ca->joueur != cb->joueur) {
        return ca->joueur < cb->joueur ? -1 : 1; // l'hôte (joueur 0) d'abord
    }
    if (etat.sequence[ia] != etat.sequence[ib]) {
        return etat.sequence[ia] < etat.sequence[ib] ? -1 : 1;
    }
    return 0;
}

int brutus_commandes_appliquer(uint32_t tick)
{
    if (!etat.taille) {
        return 0;
    }
    static int ordre[FILE_MAX];
    int nb_pretes = 0;
    for (int i = 0; i < etat.taille; i++) {
        if (etat.file[i].tick <= tick) {
            ordre[nb_pretes++] = i;
        }
    }
    if (!nb_pretes) {
        return 0;
    }
    // Clés uniques (séquence) : le tri donne toujours le même ordre.
    qsort(ordre, nb_pretes, sizeof(int), comparer);

    static brutus_commande pretes[FILE_MAX];
    for (int i = 0; i < nb_pretes; i++) {
        pretes[i] = etat.file[ordre[i]];
    }
    // Retire les commandes prêtes de la file en gardant l'ordre des autres.
    int restantes = 0;
    for (int i = 0; i < etat.taille; i++) {
        if (etat.file[i].tick > tick) {
            etat.file[restantes] = etat.file[i];
            etat.sequence[restantes] = etat.sequence[i];
            restantes++;
        }
    }
    etat.taille = restantes;

    // Toujours sans l'aperçu du joueur local et avec l'annulation de la simulation.
    brutus_simulation_debut();
    int autre_joueur_avant = 0; // M7 : un autre joueur a déjà changé quelque chose dans ce lot
    for (int i = 0; i < nb_pretes; i++) {
        brutus_commande *c = &pretes[i];
        // Chaque commande repart du même état d'aléatoire et de caches, quel que soit le
        // regroupement des commandes (image par image en solo, tick par tick en rejouage).
        c->tick = tick;
        random_brutus_reinitialiser_compteur();
        etat.repere++;
        if (etat.journal) {
            brutus_commande_ecrire(etat.journal, c);
        }
        // Joueur de la commande (-1 si hors plage) : seules ses fenêtres et ses messages
        // s'affichent chez lui (brutus_interface_pour_moi).
        etat.joueur_en_cours = (c->joueur >= 0 && c->joueur < BRUTUS_JOUEURS_MAX) ? c->joueur : BRUTUS_JOUEURS_MAX;
        brutus_ville_activer(brutus_versus_ville_du_joueur(c->joueur)); // M8 : Versus, joueur n = ville n
        int tresor_avant = city_finance_treasury();
        int resultat = executer(c);
        etat.joueur_en_cours = -1;
        if (!resultat) {
            etat.total_refusees++;
            etat.refusees_par_type[c->type]++;
        }
        // M7 (conflits) : l'ordre du joueur local a échoué (refusé, ou construction qui n'a rien
        // coûté, donc rien posé) alors qu'un autre joueur venait d'agir dans le même tour (l'hôte
        // passe en premier) : on le lui dit. Seulement l'écran : la partie est la même chez tous.
        int sans_effet = !resultat || (c->type == BRUTUS_CMD_CONSTRUIRE && c->p[0] != BUILDING_REPAIR_LAND &&
            city_finance_treasury() == tresor_avant);
        if (c->joueur == etat.joueur_local && autre_joueur_avant && sans_effet && brutus_multijoueur_actif()) {
            brutus_coop_signaler_conflit();
        }
        if (c->joueur != etat.joueur_local && !sans_effet) {
            autre_joueur_avant = 1;
        }
        etat.total_appliquees++;
        etat.appliquees_par_type[c->type]++;
        brutus_ville_activer_commune();
    }
    random_brutus_reinitialiser_compteur();
    etat.repere++;
    brutus_simulation_fin();
    return nb_pretes;
}

// ---------------------------------------------------------------------------
// Interface

void brutus_commande_construire_depuis_interface(void)
{
    building_type type = building_construction_type();
    int x0, y0, x1, y1;
    building_construction_brutus_lire_brut(&x0, &y0, &x1, &y1);
    int rotation, extra, route;
    building_rotation_brutus_lire(&rotation, &extra, &route);
    // Fin du glisser côté interface : on retire l'aperçu, le vrai placement se fera
    // par la commande, au prochain tick.
    building_construction_brutus_retirer_apercu();
    if (type == BUILDING_NONE) {
        return;
    }
    int32_t p[9] = { type, x0, y0, x1, y1, rotation, extra, route, city_view_orientation() };
    brutus_commande_envoyer(BRUTUS_CMD_CONSTRUIRE, 9, p);
    building_construction_brutus_apres_placement_interface();
}

// ---------------------------------------------------------------------------
// Journal : « <tick> <joueur> <nom> <param>... », une commande par ligne, # = commentaire

void brutus_commande_ecrire(FILE *fp, const brutus_commande *cmd)
{
    const char *nom = brutus_commande_nom(cmd->type);
    if (!fp || !nom) {
        return;
    }
    fprintf(fp, "%u %d %s", (unsigned int) cmd->tick, cmd->joueur, nom);
    int n = brutus_commande_nb_params(cmd->type);
    for (int i = 0; i < n; i++) {
        fprintf(fp, " %d", (int) cmd->p[i]);
    }
    fputc('\n', fp);
}

int brutus_commande_lire_ligne(const char *ligne, brutus_commande *cmd)
{
    while (*ligne == ' ' || *ligne == '\t') {
        ligne++;
    }
    if (!*ligne || *ligne == '#' || *ligne == '\n' || *ligne == '\r') {
        return 0;
    }
    memset(cmd, 0, sizeof(*cmd));
    char *fin;
    unsigned long tick = strtoul(ligne, &fin, 10);
    if (fin == ligne) {
        return -1;
    }
    ligne = fin;
    long joueur = strtol(ligne, &fin, 10);
    if (fin == ligne) {
        return -1;
    }
    ligne = fin;
    while (*ligne == ' ' || *ligne == '\t') {
        ligne++;
    }
    char nom[64];
    int n = 0;
    while (*ligne && *ligne != ' ' && *ligne != '\t' && *ligne != '\n' && *ligne != '\r' && n < 63) {
        nom[n++] = *ligne++;
    }
    nom[n] = 0;
    brutus_commande_type type = BRUTUS_CMD_AUCUNE;
    for (int t = BRUTUS_CMD_AUCUNE + 1; t < BRUTUS_CMD_NOMBRE; t++) {
        if (strcmp(nom, DESCRIPTION[t].nom) == 0) {
            type = t;
            break;
        }
    }
    if (type == BRUTUS_CMD_AUCUNE) {
        return -1;
    }
    cmd->type = type;
    cmd->tick = (uint32_t) tick;
    cmd->joueur = (int) joueur;
    cmd->nb_params = brutus_commande_nb_params(type);
    for (int i = 0; i < cmd->nb_params; i++) {
        long v = strtol(ligne, &fin, 10);
        if (fin == ligne) {
            return -1;
        }
        cmd->p[i] = (int32_t) v;
        ligne = fin;
    }
    return 1;
}
