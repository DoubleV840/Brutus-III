// Brutus III : en-tête de partie partagé (voir entete.h)
#include "multijoueur/entete.h"

#include "core/log.h"
#include "game/settings.h"
#include "multijoueur/commande.h"
#include "multijoueur/empreinte.h"

#include <stdlib.h>
#include <string.h>

// Bloc ajouté après les morceaux de la sauvegarde : marque (16 octets), taille (u32), texte.
static const char MARQUE[16] = "BRUTUS3-ENTETE1";
#define TEXTE_MAX 8192

static const char *NOMS_REGLAGES[BRUTUS_REGLAGE_NOMBRE] = {
    "brutus_difficulte", "brutus_dieux", "brutus_sauvegarde_mensuelle"
};

static struct {
    // En-tête lu dans le fichier en cours de chargement
    int lu;
    char texte_lu[TEXTE_MAX];
    // Réglages locaux mis de côté pendant une partie chargée avec un en-tête
    int locaux_valides;
    int config_locale[CONFIG_MAX_ENTRIES];
    int reglage_local[BRUTUS_REGLAGE_NOMBRE];
    // Vrai pendant l'application d'un en-tête (les accroches laissent passer)
    int application;
    char texte[TEXTE_MAX];
} etat;

int brutus_entete_cle_partagee(config_key key)
{
    if (key < 0 || key >= CONFIG_MAX_ENTRIES) {
        return 0;
    }
    const char *nom = config_brutus_nom(key);
    if (!nom) {
        return 0;
    }
    // Options de jeu, plus la suppression des vieux messages (city_message_clear_old_messages)
    return strncmp(nom, "gameplay_", 9) == 0 || key == CONFIG_UI_AUTO_DELETE_OLD_COMMON_MESSAGES;
}

static int reglage_en_vigueur(brutus_reglage r)
{
    switch (r) {
        case BRUTUS_REGLAGE_DIFFICULTE: return setting_difficulty();
        case BRUTUS_REGLAGE_DIEUX: return setting_gods_enabled();
        case BRUTUS_REGLAGE_SAUVEGARDE_MENSUELLE: return setting_monthly_autosave();
        default: return 0;
    }
}

// Texte « nom=valeur », une ligne par réglage, dans l'ordre des clés.
static const char *composer(void)
{
    size_t n = 0;
    etat.texte[0] = 0;
    for (int k = 0; k < CONFIG_MAX_ENTRIES; k++) {
        if (brutus_entete_cle_partagee(k) && n < TEXTE_MAX) {
            n += snprintf(etat.texte + n, TEXTE_MAX - n, "%s=%d\n", config_brutus_nom(k), config_get(k));
        }
    }
    for (int r = 0; r < BRUTUS_REGLAGE_NOMBRE && n < TEXTE_MAX; r++) {
        n += snprintf(etat.texte + n, TEXTE_MAX - n, "%s=%d\n", NOMS_REGLAGES[r], reglage_en_vigueur(r));
    }
    return etat.texte;
}

const char *brutus_entete_texte(void)
{
    return composer();
}

void brutus_entete_ecrire_fichier(FILE *fp)
{
    const char *texte = composer();
    uint32_t taille = (uint32_t) strlen(texte);
    uint8_t taille_le[4] = {
        (uint8_t) taille, (uint8_t) (taille >> 8), (uint8_t) (taille >> 16), (uint8_t) (taille >> 24)
    };
    fwrite(MARQUE, 1, sizeof(MARQUE), fp);
    fwrite(taille_le, 1, 4, fp);
    fwrite(texte, 1, taille, fp);
}

static void lire_depuis(const uint8_t *octets, size_t disponibles)
{
    etat.lu = 0;
    if (disponibles < sizeof(MARQUE) + 4 || memcmp(octets, MARQUE, sizeof(MARQUE)) != 0) {
        return;
    }
    const uint8_t *t = octets + sizeof(MARQUE);
    uint32_t taille = t[0] | (t[1] << 8) | (t[2] << 16) | ((uint32_t) t[3] << 24);
    if (taille >= TEXTE_MAX || sizeof(MARQUE) + 4 + taille > disponibles) {
        return;
    }
    memcpy(etat.texte_lu, t + 4, taille);
    etat.texte_lu[taille] = 0;
    etat.lu = 1;
}

void brutus_entete_lire_fichier(FILE *fp)
{
    uint8_t tampon[sizeof(MARQUE) + 4 + TEXTE_MAX];
    size_t n = fread(tampon, 1, sizeof(tampon), fp);
    lire_depuis(tampon, n);
}

void brutus_entete_lire_tampon(buffer *buf)
{
    size_t reste = buf->size > buf->index ? buf->size - buf->index : 0;
    lire_depuis(buf->data + buf->index, reste);
}

static void garder_locaux(void)
{
    if (etat.locaux_valides) {
        return;
    }
    for (int k = 0; k < CONFIG_MAX_ENTRIES; k++) {
        etat.config_locale[k] = config_get(k);
    }
    for (int r = 0; r < BRUTUS_REGLAGE_NOMBRE; r++) {
        etat.reglage_local[r] = reglage_en_vigueur(r);
    }
    etat.locaux_valides = 1;
}

void brutus_entete_restaurer_locaux(void)
{
    etat.lu = 0;
    if (!etat.locaux_valides) {
        return;
    }
    etat.application = 1;
    for (int k = 0; k < CONFIG_MAX_ENTRIES; k++) {
        if (brutus_entete_cle_partagee(k)) {
            config_set(k, etat.config_locale[k]);
        }
    }
    for (int r = 0; r < BRUTUS_REGLAGE_NOMBRE; r++) {
        setting_brutus_definir(r, etat.reglage_local[r]);
    }
    etat.application = 0;
    etat.locaux_valides = 0;
}

void brutus_entete_appliquer_lu(void)
{
    if (!etat.lu) {
        brutus_entete_restaurer_locaux();
        return;
    }
    garder_locaux();
    etat.application = 1;
    // Une option absente de l'en-tête (sauvegarde d'une version plus ancienne) prend sa
    // valeur par défaut : pareil chez tous les joueurs.
    for (int k = 0; k < CONFIG_MAX_ENTRIES; k++) {
        if (brutus_entete_cle_partagee(k)) {
            config_set(k, config_get_default_value(k));
        }
    }
    const char *ligne = etat.texte_lu;
    while (*ligne) {
        const char *fin = strchr(ligne, '\n');
        size_t longueur = fin ? (size_t) (fin - ligne) : strlen(ligne);
        char nom[128];
        const char *egal = memchr(ligne, '=', longueur);
        if (egal && (size_t) (egal - ligne) < sizeof(nom)) {
            size_t ln = (size_t) (egal - ligne);
            memcpy(nom, ligne, ln);
            nom[ln] = 0;
            int valeur = atoi(egal + 1);
            int trouve = 0;
            for (int k = 0; k < CONFIG_MAX_ENTRIES && !trouve; k++) {
                if (brutus_entete_cle_partagee(k) && strcmp(config_brutus_nom(k), nom) == 0) {
                    config_set(k, valeur);
                    trouve = 1;
                }
            }
            for (int r = 0; r < BRUTUS_REGLAGE_NOMBRE && !trouve; r++) {
                if (strcmp(NOMS_REGLAGES[r], nom) == 0) {
                    setting_brutus_definir(r, valeur);
                    trouve = 1;
                }
            }
            if (!trouve) {
                log_info("Brutus III : réglage inconnu dans l'en-tête, ignoré :", nom, 0);
            }
        }
        if (!fin) {
            break;
        }
        ligne = fin + 1;
    }
    etat.application = 0;
    etat.lu = 0;
}

uint64_t brutus_entete_empreinte(uint64_t h)
{
    const char *texte = composer();
    return brutus_fnv1a(h, texte, strlen(texte));
}

int brutus_entete_config_change(config_key key, int valeur)
{
    if (etat.application || !brutus_entete_cle_partagee(key)) {
        return 1;
    }
    if (brutus_multijoueur_actif()) {
        log_info("Brutus III : réglage de la partie verrouillé en multijoueur :", config_brutus_nom(key), 0);
        return 0;
    }
    if (etat.locaux_valides) {
        etat.config_locale[key] = valeur;
    }
    return 1;
}

int brutus_entete_reglage_change(brutus_reglage reglage, int valeur)
{
    if (etat.application) {
        return 1;
    }
    if (brutus_multijoueur_actif()) {
        log_info("Brutus III : réglage de la partie verrouillé en multijoueur :", NOMS_REGLAGES[reglage], 0);
        return 0;
    }
    if (etat.locaux_valides) {
        etat.reglage_local[reglage] = valeur;
    }
    return 1;
}

int brutus_entete_config_locale(config_key key, int valeur_en_vigueur)
{
    if (etat.locaux_valides && brutus_entete_cle_partagee(key)) {
        return etat.config_locale[key];
    }
    return valeur_en_vigueur;
}

int brutus_entete_reglage_local(brutus_reglage reglage, int valeur_en_vigueur)
{
    if (etat.locaux_valides) {
        return etat.reglage_local[reglage];
    }
    return valeur_en_vigueur;
}

int brutus_entete_definir_local(const char *nom, int valeur)
{
    for (int k = 0; k < CONFIG_MAX_ENTRIES; k++) {
        const char *n = config_brutus_nom(k);
        if (n && strcmp(n, nom) == 0) {
            config_set(k, valeur);
            return 1;
        }
    }
    for (int r = 0; r < BRUTUS_REGLAGE_NOMBRE; r++) {
        if (strcmp(NOMS_REGLAGES[r], nom) == 0) {
            if (brutus_entete_reglage_change(r, valeur)) {
                setting_brutus_definir(r, valeur);
            }
            return 1;
        }
    }
    return 0;
}
