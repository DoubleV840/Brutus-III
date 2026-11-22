#ifndef MULTIJOUEUR_REJOUAGE_H
#define MULTIJOUEUR_REJOUAGE_H

/*
 * Brutus III : mode rejouage sans affichage (phase 2b).
 *
 *   augustus --rejouer <sauvegarde> <journal|-> --empreintes <sortie> [--ticks N] [--tous-les N]
 *            [--sauvegarde-finale <fichier>] <dossier du jeu>
 *   augustus --creer-sauvegarde <nom de carte> <sortie.svx> <dossier du jeu>
 *   augustus --info-carte <sauvegarde> <sortie> <dossier du jeu>
 *
 * Voir docs/determinisme.md et outils/test-rejouage.sh.
 */

/**
 * Retire de argv les options du rejouage (pour que le reste soit lu par Augustus).
 * Retourne le nouveau argc. Mémorise le mode demandé.
 */
int brutus_rejouage_extraire_options(int argc, char **argv);

/** Vrai si un mode sans affichage a été demandé. */
int brutus_rejouage_demande(void);

/** Vrai pour --reseau <sauvegarde> (test du client réseau sous Node) : le programme ne quitte pas après. */
int brutus_rejouage_garder_actif(void);

/** Exécute le mode demandé (après l'initialisation du jeu). Retourne le code de sortie. */
int brutus_rejouage_executer(void);

#endif // MULTIJOUEUR_REJOUAGE_H
