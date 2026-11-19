#ifndef MULTIJOUEUR_ENTETE_H
#define MULTIJOUEUR_ENTETE_H

/*
 * Brutus III : en-tête de partie partagé (M6a).
 *
 * Les réglages qui changent la simulation mais ne sont pas dans la sauvegarde d'Augustus
 * (options « gameplay_* », suppression automatique des vieux messages, difficulté, dieux,
 * sauvegarde mensuelle) sont écrits avec la sauvegarde, dans un bloc texte ajouté à la fin du
 * fichier (le format d'Augustus et sa version ne changent pas). Au chargement, ils remplacent
 * les réglages locaux, avant la lecture de l'état. Les réglages locaux sont gardés à part et
 * remis pour une partie sans en-tête (nouvelle carte, ancienne sauvegarde). Voir
 * docs/determinisme.md.
 */

#include "core/buffer.h"
#include "core/config.h"

#include <stdint.h>
#include <stdio.h>

typedef enum {
    BRUTUS_REGLAGE_DIFFICULTE = 0,
    BRUTUS_REGLAGE_DIEUX,
    BRUTUS_REGLAGE_SAUVEGARDE_MENSUELLE,
    BRUTUS_REGLAGE_NOMBRE
} brutus_reglage;

/** Vrai si cette option de config fait partie de l'en-tête (elle change la simulation). */
int brutus_entete_cle_partagee(config_key key);

/** Écrit l'en-tête (réglages en vigueur) à la fin d'un fichier de sauvegarde. */
void brutus_entete_ecrire_fichier(FILE *fp);

/** Lit l'en-tête à la position courante (après les morceaux de la sauvegarde), s'il y en a un. */
void brutus_entete_lire_fichier(FILE *fp);
void brutus_entete_lire_tampon(buffer *buf);

/** Avant de lire l'état : applique l'en-tête lu, ou remet les réglages locaux s'il n'y en a pas. */
void brutus_entete_appliquer_lu(void);

/** Nouvelle partie (carte, campagne) : réglages locaux. */
void brutus_entete_restaurer_locaux(void);

/** Ajoute l'en-tête (réglages en vigueur) à une empreinte FNV-1a. */
uint64_t brutus_entete_empreinte(uint64_t h);

/** Texte de l'en-tête en vigueur (diagnostic). */
const char *brutus_entete_texte(void);

/**
 * Accroches de core/config.c et game/settings.c : un réglage partagé change.
 * Retourne 0 s'il faut refuser le changement (multijoueur : réglages de la partie verrouillés).
 * En solo, le changement s'applique et devient aussi le réglage local.
 */
int brutus_entete_config_change(config_key key, int valeur);
int brutus_entete_reglage_change(brutus_reglage reglage, int valeur);

/** Valeur à enregistrer dans les réglages locaux (fichier .ini ou de réglages). */
int brutus_entete_config_locale(config_key key, int valeur_en_vigueur);
int brutus_entete_reglage_local(brutus_reglage reglage, int valeur_en_vigueur);

/** Rejouage (--reglage nom=valeur) : change un réglage local par son nom. 0 si inconnu. */
int brutus_entete_definir_local(const char *nom, int valeur);

#endif // MULTIJOUEUR_ENTETE_H
