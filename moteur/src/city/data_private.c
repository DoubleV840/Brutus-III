#include "data_private.h"

// Brutus III (M8) : une copie par ville, `city_data` = la ville active (voir data_private.h)
struct city_data_t brutus_villes_donnees[2];
struct city_data_t *brutus_ville_courante = &brutus_villes_donnees[0];
