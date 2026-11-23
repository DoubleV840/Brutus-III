#ifndef GAME_CHEATS_H
#define GAME_CHEATS_H

#include <stdint.h>

#define MAX_COMMAND_SIZE 64

void game_cheat_activate(void);

int game_cheat_tooltip_enabled(void);

void game_cheat_money(void);

void game_cheat_victory(void);

/**
 * Does nothing, just set breakpoint in implementation and call it with alt + b
 */
void game_cheat_breakpoint(void);

void game_cheat_console(void);

void game_cheat_parse_command(uint8_t *command);

int game_cheat_extra_legions(void);

int game_cheat_disabled_legions_consumption(void);

int game_cheat_disabled_invasions(void);

void game_cheat_show_custom_events(uint8_t *args);

void game_cheat_show_editor(uint8_t *args);

int game_cheat_filled_markets(void);
void game_cheat_reset_markets_filled(void);

/* Brutus III : triches par la file de commandes (numéros spéciaux en plus de la console) */
#include <stdint.h>
#define BRUTUS_TRICHE_ARGENT 100
#define BRUTUS_TRICHE_VICTOIRE 101
#define BRUTUS_TRICHE_INVASION 102
#define BRUTUS_TRICHE_VERSUS 103 // M8 : la partie passe en Versus (deux villes)
#define BRUTUS_TRICHE_DEMANDE 104 // M8 (test) : César demande 100 deniers à la ville du joueur
#define BRUTUS_TRICHE_COMBAT 105 // M10 : combat entre les villes (0 éteint, 1 allume, autre : bascule)
#define BRUTUS_TRICHE_SOLDATS 106 // M10 (test) : remplit les forts de la ville du joueur
int game_cheat_brutus_executer(int numero, const int32_t *args);
#define BRUTUS_TRICHE_EFFETS 4
int game_cheat_brutus_effet(int numero);
void game_cheat_brutus_definir_effet(int numero, int valeur);

#endif // GAME_CHEATS_H
