#include "nom_jeu.h"

#include <string.h>

#define ANCIEN_NOM "Augustus"
#define TAILLE_TAMPON 4096

static char tampon[TAILLE_TAMPON];

static int est_lettre_ascii(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int ajouter(size_t *n, const char *texte, size_t longueur)
{
    if (*n + longueur >= TAILLE_TAMPON) {
        return 0;
    }
    memcpy(&tampon[*n], texte, longueur);
    *n += longueur;
    return 1;
}

const char *brutus_nom_jeu_traduire(const char *utf8)
{
    if (!utf8 || !strstr(utf8, ANCIEN_NOM)) {
        return utf8;
    }
    const size_t longueur_ancien = strlen(ANCIEN_NOM);
    size_t n = 0;
    const char *p = utf8;
    const char *trouve;
    while ((trouve = strstr(p, ANCIEN_NOM)) != NULL) {
        const char *fin = trouve + longueur_ancien;
        if (est_lettre_ascii(*fin) || (trouve > utf8 && est_lettre_ascii(trouve[-1]))) {
            // Mot décliné ou plus long : on le laisse tel quel.
            if (!ajouter(&n, p, fin - p)) {
                return utf8;
            }
            p = fin;
            continue;
        }
        const char *debut = trouve;
        const char *remplacement = BRUTUS_NOM_JEU;
        // Français : « d'Augustus » devient « de Brutus III » (pas d'élision devant un B).
        if (trouve >= utf8 + 2 && trouve[-1] == '\'' && trouve[-2] == 'd' &&
            (trouve == utf8 + 2 || !est_lettre_ascii(trouve[-3]))) {
            debut = trouve - 2;
            remplacement = "de " BRUTUS_NOM_JEU;
        }
        if (!ajouter(&n, p, debut - p) || !ajouter(&n, remplacement, strlen(remplacement))) {
            return utf8;
        }
        p = fin;
    }
    if (!ajouter(&n, p, strlen(p))) {
        return utf8;
    }
    tampon[n] = '\0';
    return tampon;
}

void brutus_nom_jeu_textes_originaux(uint8_t *donnees, int taille)
{
    static const char ancien[] = "Caesar III";
    static const char nouveau[] = BRUTUS_NOM_JEU;
    const int longueur = (int) sizeof(ancien) - 1;
    // Le remplacement sur place exige la même longueur (erreur de compilation sinon).
    char meme_longueur[sizeof(ancien) == sizeof(nouveau) ? 1 : -1];
    (void) meme_longueur;
    for (int i = 0; i + longueur <= taille; i++) {
        if (donnees[i] == 'C' && memcmp(&donnees[i], ancien, longueur) == 0) {
            memcpy(&donnees[i], nouveau, longueur);
            i += longueur - 1;
        }
    }
}
