#include "victory.h"

#include "building/construction.h"
#include "city/data_private.h"
#include "city/figures.h"
#include "city/finance.h"
#include "city/message.h"
#include "core/config.h"
#include "game/time.h"
#include "scenario/criteria.h"
#include "scenario/property.h"
#include "sound/music.h"
#include "sound/speech.h"
#include "window/mission_end.h"
#include "window/victory_dialog.h"
#include "graphics/window.h"
#include "multijoueur/commande.h" // Brutus III
#include "multijoueur/versus.h" // Brutus III (M11)

static struct {
    int state;
    int force_win;
    int force_lose;
    // Brutus III : en multijoueur, données locales (chaque joueur a ses fenêtres), hors simulation.
    int dialogue_a_montrer; // la victoire est arrivée pendant que ce joueur n'était pas dans la ville
    int continuer_demande; // ce joueur a choisi « continuer à gouverner » : la commande n'est pas encore jouée
} data;

void city_victory_reset(void)
{
    data.state = VICTORY_STATE_NONE;
    data.force_win = 0;
    data.continuer_demande = 0;
}

void city_victory_brutus_continuer_demande(void)
{
    data.continuer_demande = 1;
}

int city_victory_brutus_victoire_annoncee(void)
{
    return city_data.mission.victory_message_shown;
}

/*
 * Brutus III : en solo, les ticks s'arrêtent dès qu'une fenêtre de fin (victoire, vidéo, intermède, fin de
 * mission) remplace la ville. En multijoueur, le lockstep impose les ticks : sans ce garde-fou, chaque tick
 * rouvrait la vidéo de victoire (ou l'intermède), et le jeu restait figé sur la fin de mission.
 * Une fenêtre de fin ne s'ouvre donc que si ce joueur est dans la ville (ou ses fenêtres), et pas pendant
 * qu'il attend sa commande « continuer à gouverner ».
 */
static int ecran_fin_possible(void)
{
    if (!brutus_multijoueur_actif()) {
        return 1;
    }
    window_id id = window_get_id();
    return id >= WINDOW_CITY && id <= WINDOW_TRADE_LEDGER && !data.continuer_demande;
}

void city_victory_force_win(void)
{
    data.force_win = 1;
}

void city_victory_force_lose(void)
{
    data.force_lose = 1;
}

int city_victory_state(void)
{
    return data.state;
}

static int determine_victory_state(void)
{
    int state = VICTORY_STATE_WON;
    int has_criteria = 0;

    if (scenario_criteria_culture_enabled()) {
        has_criteria = 1;
        if (city_data.ratings.culture < scenario_criteria_culture()) {
            state = VICTORY_STATE_NONE;
        }
    }
    if (scenario_criteria_prosperity_enabled()) {
        has_criteria = 1;
        if (city_data.ratings.prosperity < scenario_criteria_prosperity()) {
            state = VICTORY_STATE_NONE;
        }
    }
    if (scenario_criteria_peace_enabled()) {
        has_criteria = 1;
        if (city_data.ratings.peace < scenario_criteria_peace()) {
            state = VICTORY_STATE_NONE;
        }
    }
    if (scenario_criteria_favor_enabled()) {
        has_criteria = 1;
        if (city_data.ratings.favor < scenario_criteria_favor()) {
            state = VICTORY_STATE_NONE;
        }
    }
    if (scenario_criteria_population_enabled()) {
        has_criteria = 1;
        if (city_data.population.population < scenario_criteria_population()) {
            state = VICTORY_STATE_NONE;
        }
    }

    if (!has_criteria) {
        state = VICTORY_STATE_NONE;
    }
    // More sensible options for surival time:
    // require the user to play to the end, even if other win criteria have been set and are met.
    // At the end, let the user lose if the other win criteria are not met
    if (game_time_year() >= scenario_criteria_max_year()) {
        if (scenario_criteria_time_limit_enabled()) {
            // Lose game automatically when you go over the time limit
            state = VICTORY_STATE_LOST;
        } else if (scenario_criteria_survival_enabled()) {
            if (!has_criteria) {
                state = VICTORY_STATE_WON;
            } else if (state != VICTORY_STATE_WON) {
                // Lose game if you do not meet the criteria at the end of the road
                state = VICTORY_STATE_LOST;
            }
        }
    } else if (scenario_criteria_survival_enabled()) {
        // Do not win the game when other criteria are met when survival time is enabled
        state = VICTORY_STATE_NONE;
    }
    if (scenario_criteria_time_limit_enabled() || scenario_criteria_survival_enabled()) {
        has_criteria = 1;
    }

    if (city_figures_total_invading_enemies() > 2 + city_data.figure.soldiers) {
        if (city_data.population.population < city_data.population.highest_ever / 4) {
            state = VICTORY_STATE_LOST;
        }
    }
    if (city_figures_total_invading_enemies() > 0) {
        if (city_data.population.population <= 0) {
            state = VICTORY_STATE_LOST;
        }
    }
    if (!has_criteria) {
        state = VICTORY_STATE_NONE;
    }
    return state;
}

void city_victory_check(void)
{
    if (brutus_versus_actif()) {
        return; // Brutus III (M11) : en Versus, la fin vient de multijoueur/victoire_versus.c
    }
    if (scenario_is_open_play() && !data.force_win && !data.force_lose) {
        return;
    }
    data.state = determine_victory_state();

    if (city_data.mission.has_won) {
        data.state = city_data.mission.continue_months_left <= 0 ? VICTORY_STATE_WON : VICTORY_STATE_NONE;
    }
    if (data.force_win) {
        data.state = VICTORY_STATE_WON;
    } else if (data.force_lose) {
        data.state = VICTORY_STATE_LOST;
    }
    if (data.state == VICTORY_STATE_NONE) {
        // Brutus III : plus de victoire en cours (commande « continuer » jouée, chez ce joueur ou un autre)
        data.dialogue_a_montrer = 0;
        data.continuer_demande = 0;
        return;
    }
    int ecran = ecran_fin_possible(); // Brutus III : toujours 1 en solo
    if (ecran) {
        building_construction_clear_type();
    }
    if (data.state == VICTORY_STATE_LOST) {
        if (city_data.mission.fired_message_shown) {
            if (ecran) { // Brutus III
                window_mission_end_show_fired();
                data.force_lose = 0;
            }
        } else {
            city_data.mission.fired_message_shown = 1;
            city_message_post(1, MESSAGE_FIRED, 0, 0);
        }
        data.force_win = 0;
    } else if (data.state == VICTORY_STATE_WON) {
        if (city_data.mission.victory_message_shown) {
            if (ecran) { // Brutus III
                sound_music_stop();
                if (data.dialogue_a_montrer) {
                    data.dialogue_a_montrer = 0;
                    window_victory_dialog_show();
                } else {
                    window_mission_end_show_won();
                    data.force_win = 0;
                }
            }
        } else {
            sound_music_stop();
            city_data.mission.victory_message_shown = 1;
            data.continuer_demande = 0;
            sound_speech_play_file("wavs/fanfare_nu2.wav");
            if (ecran) {
                window_victory_dialog_show();
            } else {
                data.dialogue_a_montrer = 1; // Brutus III : montré dès que ce joueur revient dans la ville
            }
        }
        data.force_lose = 0;
    }
}

void city_victory_update_months_to_govern(void)
{
    if (city_data.mission.has_won) {
        city_data.mission.continue_months_left--;
    }
}

void city_victory_continue_governing(int months)
{
    city_data.mission.victory_message_shown = 0;
    city_data.mission.has_won = 1;
    city_data.mission.continue_months_left += months;
    city_data.mission.continue_months_chosen = months;
    city_data.emperor.salary_rank = 0;
    city_data.emperor.salary_amount = 0;
    city_finance_update_salary();
}

void city_victory_stop_governing(void)
{
    city_data.mission.has_won = 0;
    city_data.mission.continue_months_left = 0;
    city_data.mission.continue_months_chosen = 0;
}

int city_victory_has_won(void)
{
    return city_data.mission.has_won;
}
