#ifndef MULTIJOUEUR_GENERATEUR_CARTES_H
#define MULTIJOUEUR_GENERATEUR_CARTES_H

/*
 * Brutus III (M9) : les cartes Versus de Brutus III, créées par programme (aucune donnée du jeu
 * d'origine). Chaque carte est symétrique : les deux joueurs partent à égalité (même terrain, même
 * distance aux ressources, même route de Rome). Le résultat est un fichier .mapx ordinaire
 * d'Augustus, plus le bloc des points du joueur 2 (multijoueur/carte_versus.h) ; il s'ouvre et se
 * retouche dans l'éditeur de cartes.
 *
 *   augustus --creer-carte-versus <modèle> <fichier.mapx> <dossier du jeu>
 */

typedef enum {
    BRUTUS_CARTE_DEUX_RIVES = 0, // une rivière en diagonale, un gué au centre
    BRUTUS_CARTE_LAC = 1,        // une plaine, un lac au centre, des forêts entre les deux villes
    BRUTUS_CARTE_COTE = 2,       // « rivage » : une côte au sud (navires), des collines entre les deux villes
    BRUTUS_CARTE_COLLINES = 3,   // une crête rocheuse au milieu (deux cols), un étang et des prés de chaque côté
    BRUTUS_CARTE_MARAIS = 4,     // un fleuve en diagonale bordé de marais et de prés humides, trois gués
    BRUTUS_CARTE_NOMBRE = 5
} brutus_modele_carte;

/** Nom du fichier de la carte (« Brutus - Deux rives.mapx »…), ou 0. */
const char *brutus_carte_versus_nom_fichier(int modele);

/** Numéro du modèle d'après son nom court (« deux-rives », « lac », « rivage », « collines », « marais ») ou son numéro ; -1 sinon. */
int brutus_carte_versus_modele(const char *nom);

/**
 * Au démarrage : copie dans le dossier des cartes celles de Brutus III qui n'y sont pas encore
 * (elles viennent des assets : moteur/res/assets/Brutus/cartes). Une carte refaite depuis (numéro de version
 * plus grand que celui noté dans « brutus-cartes-version.txt ») remplace une fois l'ancienne copie.
 */
void brutus_cartes_versus_installer(void);

/** Crée la carte dans l'éditeur (à l'état neuf) et l'écrit. Retourne 1 si c'est fait. */
int brutus_generer_carte_versus(int modele, const char *fichier);

#endif // MULTIJOUEUR_GENERATEUR_CARTES_H
