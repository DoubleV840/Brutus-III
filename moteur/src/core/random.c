#include "core/random.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MAX_RANDOM 100

static struct {
    uint32_t iv1;
    uint32_t iv2;
    int8_t random1_7bit;
    int16_t random1_15bit;
    int8_t random2_7bit;
    int16_t random2_15bit;
    int pool_index;
    int32_t pool[MAX_RANDOM];
    time_t last_seed;
} data;

static uint32_t brutus_compteur; // Brutus III : voir random_from_stdlib

void random_init(void)
{
    memset(&data, 0, sizeof(data));
    brutus_compteur = 0; // Brutus III
    data.iv1 = 0x54657687;
    data.iv2 = 0x72641663;
}

void random_generate_next(void)
{
    brutus_compteur = 0; // Brutus III : chaque tick repart du même état
    data.pool[data.pool_index++] = data.random1_7bit;
    if (data.pool_index >= MAX_RANDOM) {
        data.pool_index = 0;
    }
    for (int i = 0; i < 31; i++) {
        unsigned int r1 = (((data.iv1 & 0x10) >> 4) ^ data.iv1) & 1;
        unsigned int r2 = (((data.iv2 & 0x10) >> 4) ^ data.iv2) & 1;
        data.iv1 = data.iv1 >> 1;
        data.iv2 = data.iv2 >> 1;
        if (r1) {
            data.iv1 |= 0x40000000;
        }
        if (r2) {
            data.iv2 |= 0x40000000;
        }
    }
    data.random1_7bit = data.iv1 & 0x7f;
    data.random1_15bit = data.iv1 & 0x7fff;
    data.random2_7bit = data.iv2 & 0x7f;
    data.random2_15bit = data.iv2 & 0x7fff;
}

void random_generate_pool(void)
{
    data.pool_index = 0;
    for (int i = 0; i < MAX_RANDOM; i++) {
        random_generate_next();
    }
}

int8_t random_byte(void)
{
    return data.random1_7bit;
}

int8_t random_byte_alt(void)
{
    return data.random2_7bit;
}

int16_t random_short(void)
{
    return data.random1_15bit;
}

int16_t random_short_alt(void)
{
    return data.random2_15bit;
}


int32_t random_from_pool(int index)
{
    return data.pool[(data.pool_index + index) % MAX_RANDOM];
}

void random_load_state(buffer *buf)
{
    data.iv1 = buffer_read_u32(buf);
    data.iv2 = buffer_read_u32(buf);
    // Brutus III : les valeurs dérivées et la réserve (pool) ne sont pas sauvegardées.
    // On les recalcule à partir de iv1/iv2 sans faire avancer le générateur, pour que
    // deux programmes qui chargent la même sauvegarde aient exactement le même état.
    brutus_compteur = 0;
    data.random1_7bit = data.iv1 & 0x7f;
    data.random1_15bit = data.iv1 & 0x7fff;
    data.random2_7bit = data.iv2 & 0x7f;
    data.random2_15bit = data.iv2 & 0x7fff;
    uint32_t iv = data.iv1;
    for (int n = 0; n < MAX_RANDOM; n++) {
        for (int i = 0; i < 31; i++) {
            unsigned int r = (((iv & 0x10) >> 4) ^ iv) & 1;
            iv >>= 1;
            if (r) {
                iv |= 0x40000000;
            }
        }
        data.pool[n] = iv & 0x7f;
    }
    data.pool_index = 0;
}

void random_save_state(buffer *buf)
{
    buffer_write_u32(buf, data.iv1);
    buffer_write_u32(buf, data.iv2);
}

// Brutus III : générateur déterministe de la simulation (remplace srand(time)/rand()).
// Sortie = splitmix64(iv1:iv2 + compteur), sur 31 bits, identique en natif et en wasm.
#define BRUTUS_RAND_MAX 0x7fffffff

void random_brutus_reinitialiser_compteur(void)
{
    brutus_compteur = 0;
}

static uint64_t brutus_melanger(uint64_t z)
{
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

int random_from_stdlib(void)
{
    brutus_compteur++;
    uint64_t graine = ((uint64_t) data.iv1 << 32) | data.iv2;
    uint64_t x = brutus_melanger(graine + 0x9e3779b97f4a7c15ULL * brutus_compteur);
    return (int) (x >> 33);
}

int random_between_from_stdlib(int min, int max)
{
    int diff = max - min;
    int rnd = 0;
    if (diff > 0) {
        rnd = random_from_stdlib() % diff;
    }
    return min + rnd;
}

double random_fractional_from_stdlib(void)
{
    return (double) random_from_stdlib() / (double) BRUTUS_RAND_MAX;
}

// Brutus III : ancien comportement (horloge réelle), pour l'interface, le son et les effets visuels.
int random_ui_from_stdlib(void)
{
    time_t t;
    t = time(&t);
    if (data.last_seed != t) {
        srand((unsigned int) t);
        data.last_seed = t;
    }
    return rand();
}

int random_ui_between_from_stdlib(int min, int max)
{
    int diff = max - min;
    int rnd = 0;
    if (diff > 0) {
        rnd = random_ui_from_stdlib() % diff;
    }
    return min + rnd;
}

double random_ui_fractional_from_stdlib(void)
{
    return (double) random_ui_from_stdlib() / (double) RAND_MAX;
}
