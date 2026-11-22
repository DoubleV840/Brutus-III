#include "tick.h"

#include "building/connectable.h"
#include "building/count.h"
#include "building/dock.h"
#include "building/entertainment.h"
#include "building/figure.h"
#include "building/government.h"
#include "building/granary.h"
#include "building/house_evolution.h"
#include "building/house_population.h"
#include "building/house_service.h"
#include "building/industry.h"
#include "building/lighthouse.h"
#include "building/maintenance.h"
#include "building/warehouse.h"
#include "city/buildings.h"
#include "city/culture.h"
#include "city/emperor.h"
#include "city/festival.h"
#include "city/finance.h"
#include "city/games.h"
#include "city/gods.h"
#include "city/health.h"
#include "city/labor.h"
#include "city/message.h"
#include "city/migration.h"
#include "city/population.h"
#include "city/ratings.h"
#include "city/resource.h"
#include "city/sentiment.h"
#include "city/trade.h"
#include "city/victory.h"
#include "core/config.h"
#include "core/dir.h"
#include "core/random.h"
#include "editor/editor.h"
#include "empire/city.h"
#include "figure/formation.h"
#include "figuretype/crime.h"
#include "game/file.h"
#include "game/settings.h"
#include "game/time.h"
#include "game/tutorial.h"
#include "game/undo.h"
#include "graphics/weather.h"
#include "map/desirability.h"
#include "map/natives.h"
#include "map/road_network.h"
#include "map/routing_terrain.h"
#include "map/tiles.h"
#include "map/water_supply.h"
#include "multijoueur/commande.h" // Brutus III
#include "multijoueur/simulation.h" // Brutus III
#include "scenario/demand_change.h"
#include "scenario/distant_battle.h"
#include "scenario/earthquake.h"
#include "scenario/emperor_change.h"
#include "scenario/empire.h"
#include "scenario/event/controller.h"
#include "scenario/gladiator_revolt.h"
#include "scenario/invasion.h"
#include "scenario/price_change.h"
#include "scenario/random_event.h"
#include "scenario/request.h"
#include "sound/music.h"
#include "widget/minimap.h"
#include "multijoueur/versus.h" // Brutus III (M8)

// Brutus III (M8) : en Versus, ces appels se font une fois par ville, la ville active étant celle qui
// travaille (city_data et listes de bâtiments par type). Une seule ville : un seul appel, comme avant.
#define PAR_VILLE(appel) do { \
        for (int ville_ = 0; ville_ < brutus_versus_villes(); ville_++) { \
            brutus_ville_activer(ville_); \
            appel; \
        } \
        brutus_ville_activer(0); \
    } while (0)

static void advance_year(void)
{
    game_undo_disable();
    game_time_advance_year();

    city_message_clear_old_messages();

    scenario_empire_process_expansion();
    PAR_VILLE(city_population_request_yearly_update());
    PAR_VILLE(city_finance_handle_year_change());
    empire_city_reset_yearly_trade_amounts();
    building_maintenance_update_fire_direction();
    PAR_VILLE(city_ratings_update(1, 0));
}

static void advance_month(void)
{
    PAR_VILLE(city_migration_reset_newcomers());
    PAR_VILLE(city_health_update());
    scenario_random_event_process();
    PAR_VILLE(city_finance_handle_month_change());
    PAR_VILLE(city_resource_consume_food());
    scenario_distant_battle_process();
    scenario_invasion_process();
    scenario_request_process();
    scenario_demand_change_process();
    scenario_price_change_process();
    PAR_VILLE(city_victory_update_months_to_govern());
    formation_update_monthly_morale_at_rest();
    city_message_decrease_delays();
    PAR_VILLE(city_sentiment_decrement_blessing_boost());
    PAR_VILLE(building_industry_advance_stats());
    PAR_VILLE(building_industry_start_strikes());
    building_trim();

    building_connectable_update_connections();
    map_tiles_update_all_roads();
    map_tiles_update_all_highways();
    map_tiles_update_all_water();
    map_tiles_update_all_marshland();
    map_tiles_update_all_beach();
    map_routing_update_land_citizen();
    city_message_sort_and_compact();

    if (game_time_advance_month()) {
        advance_year();
    } else {
        PAR_VILLE(city_ratings_update(0, 1));
    }

    PAR_VILLE(city_population_record_monthly());
    PAR_VILLE(city_festival_update());
    PAR_VILLE(city_games_decrement_month_counts());
    PAR_VILLE(city_gods_update_blessings());
    tutorial_on_month_tick();
    if (setting_monthly_autosave()) {
        game_file_write_saved_game(dir_append_location("autosave.svx", PATH_LOCATION_SAVEGAME));
    }

    city_weather_update(game_time_month());
}

static void advance_day(void)
{
    if (game_time_advance_day()) {
        advance_month();
    }
    if (game_time_day() == 0 || game_time_day() == 8) {
        PAR_VILLE(city_sentiment_update(1));
    }
    if (game_time_day() == 0 || game_time_day() == 7) {
        PAR_VILLE(building_lighthouse_consume_timber());
    }
    tutorial_on_day_tick();
    if (config_get(CONFIG_GP_CH_YEARLY_AUTOSAVE) && game_time_month() == 11 && game_time_day() == 15) {
        // 0-based index so 11 = December, 15 = last day of the month
        game_file_make_yearly_autosave();
    }
    scenario_events_progress_paused(1);
    scenario_events_process_all();
}

static void advance_tick(void)
{
    // NB: these ticks are noop:
    // 0, 10, 11, 13, 14, 15, 18, 26, 41
    // max is 49
    switch (game_time_tick()) {
        case 1: PAR_VILLE(city_gods_calculate_moods(1)); break;
        case 2: sound_music_update(0); break;
        case 3: widget_minimap_invalidate(); break;
        case 4: PAR_VILLE(city_emperor_update()); break;
        case 5: formation_update_all(0); break;
        case 6: map_natives_check_land(1); break;
        case 7: map_road_network_update(); break;
        case 8: PAR_VILLE(building_granaries_calculate_stocks()); break;
        case 9: PAR_VILLE(city_buildings_update_plague()); break;
        case 12: house_service_decay_houses_covered(); break;
        case 16: PAR_VILLE(city_resource_calculate_warehouse_stocks()); break;
        case 17: PAR_VILLE(city_resource_calculate_food_stocks_and_supply_wheat()); break;
        case 19: building_dock_update_open_water_access(); break;
        case 20: PAR_VILLE(building_industry_update_production(1)); break;
        case 21: building_maintenance_check_rome_access(); break;
        case 22: PAR_VILLE(house_population_update_room()); break;
        case 23: PAR_VILLE(house_population_update_migration()); break;
        case 24: PAR_VILLE(house_population_evict_overcrowded()); break;
        case 25: PAR_VILLE(city_labor_update()); break;
        case 27: map_water_supply_update_reservoir_fountain(); break;
        case 28: map_water_supply_update_buildings(); break;
        case 29: formation_update_all(1); break;
        case 30: widget_minimap_invalidate(); break;
        case 31: building_figure_generate(); break;
        case 32: PAR_VILLE(city_trade_update()); break;
        case 33: PAR_VILLE((building_entertainment_run_shows(), city_culture_update_coverage())); break;
        case 34: PAR_VILLE(building_government_distribute_treasury()); break;
        case 35: PAR_VILLE(house_service_decay_culture()); break;
        case 36: PAR_VILLE(house_service_calculate_culture_aggregates()); break;
        case 37: map_desirability_update(); break;
        case 38: building_update_desirability(); break;
        case 39: PAR_VILLE(building_house_process_evolve_and_consume_goods()); break;
        case 40: building_update_state(); break;
        case 42: PAR_VILLE(city_finance_spawn_tourist()); break;
        case 43: building_maintenance_update_burning_ruins(); break;
        case 44: building_maintenance_check_fire_collapse(); break;
        case 45: PAR_VILLE(figure_generate_criminals()); break;
        case 46: PAR_VILLE(building_industry_update_production(0)); break;
        case 47: PAR_VILLE(city_games_decrement_duration()); break;
        case 48: PAR_VILLE(house_service_decay_tax_collector()); break;
        case 49: PAR_VILLE(city_culture_calculate()); break;
    }
    if (game_time_advance_tick()) {
        advance_day();
    }
}

void game_tick_run(void)
{
    if (editor_is_active()) {
        random_generate_next(); // update random to randomize native huts
        figure_action_handle(); // just update the flag figures
        return;
    }
    // Brutus III : les commandes datées de ce tick sont appliquées avant la simulation.
    // Tout le tick se fait sans l'aperçu du joueur local (multijoueur/simulation.c).
    brutus_simulation_debut();
    brutus_commandes_appliquer(brutus_tick_absolu());
    brutus_debut_tick();
    random_generate_next();
    game_undo_reduce_time_available();
    advance_tick();
    figure_action_handle();
    scenario_earthquake_process();
    scenario_gladiator_revolt_process();
    scenario_emperor_change_process();
    city_victory_check();
    brutus_simulation_fin(); // Brutus III
}

void game_tick_cheat_year(void)
{
    advance_year();
}
