# Mode Versus : deux villes sur la même carte (M8)

État au 23/11/2026 : **fondation de M8 en place, en local** (un seul écran, une touche pour changer de joueur).
Le réseau du Versus est la mission M12 ; les cartes à deux points de départ, M9 ; le combat, M10 ; la victoire, M11.

## Principe

- **Propriétaire.** Chaque bâtiment et chaque personnage a un champ `brutus_proprietaire` (numéro de ville, 0 par
  défaut). Il est donné à la création : c'est la ville **active** à ce moment-là.
- **Données de ville par ville.** Les données de ville d'Augustus (`city_data` : trésor, population, emplois,
  notes, César, commerce, fêtes, dieux…) existent une fois par ville (`brutus_villes_donnees[]`, `city/data_private.h`).
  `city_data` est une macro qui désigne la ville active : tout le code d'Augustus qui lit ou écrit `city_data`
  travaille, sans changement, pour la ville active.
- **Listes de bâtiments par ville.** `building_first_of_type()` donne la liste de la ville active : un marché ne
  trouve que les entrepôts de sa ville, un recruteur que les bâtiments de sa ville, les stocks ne comptent que la
  ville, etc. (`building/building.c`).
- **Qui est la ville active ?**
  - dans la simulation (`game/tick.c`) : les fonctions de ville sont appelées une fois par ville (`PAR_VILLE`) ;
    chaque personnage agit pour la ville de son propriétaire (`figure/action.c`), chaque bâtiment produit ses
    personnages pour la sienne (`building/figure.c`) ;
  - pendant une commande : la ville du joueur qui l'envoie (joueur n = ville n) : ce qu'il construit est à lui,
    et c'est son trésor qui paie ;
  - hors simulation (interface) : la ville **vue** par le joueur local (barre du haut, conseillers…).
- **Commun aux deux villes** : le terrain, les routes, l'eau, la carte, le temps, l'empire, les événements du
  scénario, les points d'entrée et de sortie (en attendant M9).

Avec **une seule ville** (solo, Coop), rien ne change : une seule passe, ville active 0, aucun octet de plus dans
la sauvegarde ni dans l'empreinte. Vérifié : les empreintes du rejouage solo sont identiques au bit près avant et
après M8, sur Lugdunum, Tarraco et Carthago.

## Sauvegarde

Bloc binaire `BR3V` après l'en-tête de partie (`multijoueur/versus.c`), seulement en Versus :
version, nombre de villes, propriétaire de chaque bâtiment et de chaque personnage, puis les données de chaque
ville en plus (format de `city/data.c`, version courante). Le format d'Augustus ne change pas ; une sauvegarde
Versus s'ouvre dans Augustus d'origine comme une partie à une seule ville. L'empreinte (déterminisme) inclut le
bloc.

## Jouer en Versus local

Dans une partie (natif ou navigateur), en ville :

1. **Alt+W** : la partie passe en Versus (commande `triche` 103, donc rejouable et déterministe). La ville 2
   démarre vide, avec le trésor de départ du scénario.
2. **Alt+W** encore : tu joues pour l'autre ville (barre du haut, conseillers, constructions, impôts…). Un
   message rappelle la ville en cours (1 bleue, 2 orange).
3. En partie réseau, Alt+W est refusé (Versus en réseau : M12).

## Test automatique

```bash
bash outils/test-versus.sh [ticks] [carte]   # 60000 ticks, Lugdunum par défaut
```

Deux bots (un par ville, loin l'un de l'autre : `outils/bot-rejouage.mjs --joueur 1 --eviter x,y`), rejoués deux
fois : empreintes identiques ; chaque ville a ses habitants, son trésor et ses bâtiments ; sauvegarde au milieu,
rechargement par un lancement neuf, suite identique. Résultat du 23/11 : ville 1 avec 399 habitants et
157 bâtiments, ville 2 avec 291 habitants et 145 bâtiments, 121 empreintes identiques, rechargement identique.

## Ce qui reste pour finir M8

- **Services des personnages** : un préfet, une marchande, un recruteur ou un prêtre d'une ville sert encore les
  maisons de l'autre ville sur son passage (`figure/service.c`, couverture des maisons). Filtrer par propriétaire.
- **Boucles sur tout le tableau des bâtiments** dans des fonctions de ville : trois sont filtrées (emplois,
  ajout d'habitants, destination des touristes) ; les autres sont globales et à revoir une par une
  (`grep -rn "i < building_count()" moteur/src`).
- **Messages et avertissements** : publiés pour tout le monde ; à réserver à la ville concernée.
- **Victoire** (`city_victory_check`) : vérifiée pour la ville 1 seulement (M11 la refait).
- **Affichage** : couleur du propriétaire sur les bâtiments (vue de la ville et mini-carte).
- **Rechargement** : une partie Versus rechargée suit exactement la même suite chez tous (vérifié), mais diffère
  plus qu'en solo de la partie continue : quelques recalculs du chargement (`game/file.c`) ne se font que pour la
  ville active. Les stocks des greniers sont déjà refaits pour chaque ville.
- **Empire, demandes de César, événements du scénario, invasions** : communs pour l'instant.
