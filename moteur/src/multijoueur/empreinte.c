// Brutus III : empreinte de l'état de la partie (voir empreinte.h)
#include "multijoueur/empreinte.h"

#include "game/file_io.h"
#include "multijoueur/simulation.h"

static int en_cours;

uint64_t brutus_fnv1a(uint64_t h, const void *donnees, size_t taille)
{
    const uint8_t *octets = (const uint8_t *) donnees;
    for (size_t i = 0; i < taille; i++) {
        h ^= octets[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

int brutus_empreinte_en_cours(void)
{
    return en_cours;
}

void brutus_empreinte_marquer(int valeur)
{
    en_cours = valeur;
}

uint64_t brutus_empreinte_partie(void)
{
    // M6a : sans l'aperçu du joueur local, avec les copies de secours de la simulation
    brutus_simulation_debut();
    uint64_t h = game_file_io_brutus_empreinte();
    brutus_simulation_fin();
    return h;
}
