# Mode Versus : deux villes sur la même carte (M8, M9)

État au 23/11/2026 : **M8 et M9 terminées, en local** (un seul écran, une touche pour changer de joueur) :
deux villes séparées, et trois cartes à deux créées par nous, avec une entrée et une sortie par joueur.
Le réseau du Versus est la mission M12 ; le combat, M10 ; la victoire, M11.

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
   message rappelle la ville en cours (1 bleue, 2 rouge).
3. En partie réseau, Alt+W est refusé (Versus en réseau : M12).

## Test automatique

```bash
bash outils/test-versus.sh [ticks] [carte]   # 60000 ticks, Lugdunum par défaut
```

Deux bots (un par ville, loin l'un de l'autre : `outils/bot-rejouage.mjs --joueur 1 --eviter x,y`), rejoués deux
fois : empreintes identiques ; chaque ville a ses habitants, son trésor et ses bâtiments ; sauvegarde au milieu,
rechargement par un lancement neuf, suite identique. Une demande de César faite à la ville 1 (triche 104) ne
concerne pas la ville 2. Résultat du 23/11 (fin de M8) : ville 1 avec 100 habitants et 163 bâtiments, ville 2 avec
197 habitants et 145 bâtiments, 121 empreintes identiques, rechargement identique, demande de César dans la ville 1
seulement. Le solo reste identique au bit près (Lugdunum, Tarraco, Carthago).

## Séparé par ville (fin de M8)

- **Services des personnages** (`figure/service.c`) : préfet, ingénieur, marchande, prêtre, recruteur, percepteur,
  médecin, école, spectacles, touristes ne servent que les maisons et bâtiments de leur ville. Le missionnaire calme
  tous les villages indigènes (ils ne sont à personne).
- **Boucles globales devenues « une fois par ville »** (`game/tick.c`) : incendies et effondrements
  (`building/maintenance.c`, le seuil « moins de 10 habitants » est celui de chaque ville), accès à Rome (et son
  message d'obstruction, une fois par ville). « Détruire le dernier bâtiment posé » ne prend que ceux de la ville.
  Les autres boucles sur tout le tableau des bâtiments sont communes à bon droit : désirabilité, ruines en feu
  (le feu passe d'une ville à l'autre), usure des couvertures, cibles des indigènes, rotations au chargement.
- **Messages et avertissements** : chaque message garde la ville concernée (`city_message.brutus_ville`, -1 pour
  toutes) : celle d'une passe de ville, d'un personnage, d'un bâtiment ou d'une commande ; ce qui arrive entre deux
  passes (événements du scénario, invasions…) concerne tout le monde (`brutus_versus_ville_concernee()`).
  La fenêtre, la liste des messages, le compteur de la barre latérale et les avertissements ne montrent que ceux
  de la ville du joueur (`brutus_interface_pour_moi()`). La ville de chaque message est dans le bloc `BR3V`.
- **Demandes de César** (`scenario/request.c`) : chaque ville a les siennes (copiées de la ville 1 au passage en
  Versus), traitées une fois par ville, sauvées dans le bloc `BR3V`. Triche 104 (tests) : César demande 100 deniers
  à la ville du joueur.
- **Affichage** : en Versus, les bâtiments de l'autre ville prennent sa teinte dans la vue de la ville et sa couleur
  sur la mini-carte (ville 1 bleue, ville 2 rouge). `--capture` (rejouage) enregistre l'image de toute la ville et
  de la mini-carte à la fin, dans le dossier du jeu.
- **Chargement** : l'accès à Rome et la ville de la bataille lointaine sont recalculés pour chaque ville.

Bloc `BR3V` : version 3 (2 : ville de chaque message ; 3 : demandes de César de chaque ville). Les versions 1 et 2
se lisent encore.

## Cartes à deux (M9)

- **Points du joueur 2.** Le joueur 1 garde l'entrée et la sortie du scénario. L'entrée et la sortie du joueur 2
  sont à nous (`multijoueur/carte_versus.c`) : dans la carte, un bloc `BR3C` écrit après les données d'Augustus
  (un Augustus d'origine ouvre la carte sans le voir) ; dans une partie, le bloc `BR3V` (version 4), écrit dès que
  la carte en a, même avant le passage en Versus, pour qu'un invité qui charge la sauvegarde de départ ait les mêmes
  points. Au passage en Versus, la ville 2 prend les points du joueur 2 (`city_data.map`) : route de Rome,
  arrivée des migrants et des marchands, départ des émigrants. Carte sans ces points (toutes les cartes d'origine) :
  la ville 2 reçoit ceux de la ville 1, comme en M8.
- **Éditeur de cartes.** Menu « Points des personnes » : « Entrée joueur 2 » et « Sortie joueur 2 » (même règle que
  l'entrée et la sortie : sur le bord, terrain dégagé). Leur drapeau porte le numéro 2. Ils sont enregistrés avec
  la carte. Une nouvelle carte n'en a pas.
- **Nos trois cartes** (100 × 100, symétriques : même terrain, mêmes ressources, même route de Rome pour les deux
  joueurs ; jeu libre, la victoire du Versus viendra en M11 ; 4 000 deniers au départ) :
  - **Brutus - Deux rives** : un fleuve en diagonale, un gué au centre ;
  - **Brutus - Lac central** : une plaine, un lac au centre, une forêt en arc entre les deux villes ;
  - **Brutus - Rivage** : une côte au sud ouverte aux navires, des collines entre les deux villes, deux passages.
  Elles sont créées par programme (`multijoueur/generateur_cartes.c`, aucune donnée du jeu d'origine) :
  `bash outils/creer-cartes-versus.sh` les refait dans `moteur/res/assets/Brutus/cartes/`. Elles sont embarquées
  avec les assets (application et page web) ; au démarrage, le jeu copie celles qui manquent dans le dossier des
  cartes (une carte déjà présente, peut-être retouchée, n'est pas écrasée).
- **Versus local** : Alt+W garde la vue de chaque ville ; la première fois, la vue part de l'entrée de la ville.
- **Tests** : `bash outils/test-versus.sh 60000 "Brutus - Deux rives"` (de même « Brutus - Lac central »,
  « Brutus - Rivage ») : le bot du joueur 2 part de l'entrée du joueur 2 (`entree2` dans `--info-carte`). Le bilan du
  rejouage donne l'entrée et la sortie de chaque ville. `--capture-editeur <carte>` ouvre la carte dans l'éditeur,
  la vue sur l'entrée du joueur 2, le menu des points ouvert, et enregistre l'image de l'écran.

Résultat du 23/11 : les trois cartes passent le test Versus (empreintes identiques, rechargement identique,
demande de César séparée) ; la ville 2 a l'entrée 99,29 et la sortie 71,0 sur « Deux rives », en continu comme
après rechargement.

## Laissé aux missions suivantes

- **Envahisseurs et combat** : les envahisseurs sont créés hors passe de ville, donc à la ville 1 ; M10. Nos cartes
  n'ont pas de point d'invasion.
- **Victoire** (`city_victory_check`) : vérifiée pour la ville 1 seulement ; M11.
- **Empire** (routes ouvertes, prix), **événements du scénario**, bataille lointaine : communs. Sur « Rivage »,
  l'entrée et la sortie des navires sont communes.
- Les jalons de population (« 500 habitants »…) sont comptés une fois pour toute la partie.
- Le bot de test construit mal (sa population tombe vers 100 habitants vers le tick 20 000, avant comme après M8) :
  les chiffres de fin ne mesurent pas la qualité du jeu.
