# Mode Versus : deux villes sur la même carte (M8 à M12)

État au 23/11/2026 : **M8 à M11 terminées en local** (un seul écran, une touche pour changer de joueur) :
deux villes séparées, trois cartes à deux créées par nous, avec une entrée et une sortie par joueur, le
combat entre les deux villes, et la fin de partie (victoire militaire ou économique, écran des scores).
**M12** (branche `m12-versus-reseau`) : le Versus en réseau, choisi dans le menu Multijoueur avec ses options, et le
récap de fin de partie (courbes et faits marquants) ; voir « Versus en réseau et récap de fin ».

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

## Combat (M10)

- **Option de la partie.** Activé par défaut au passage en Versus. **Alt+G** (Versus local, en ville) l'éteint ou le
  rallume : commande `triche` 105 (0 éteint, 1 allume, autre valeur : bascule), donc rejouable et déterministe.
  L'option est dans le bloc `BR3V` (version 5) et dans l'empreinte ; un bloc plus ancien se lit « combat activé ».
  Le choix dans le menu Multijoueur viendra avec M12. Combat éteint : les deux villes s'ignorent, comme en M9.
- **Qui se bat** (`multijoueur/combat_versus.c`) : les militaires d'une ville (soldats des légions, sentinelles des
  murs, balistes, archers des tours de guet) attaquent ceux de l'autre, avec les règles qu'Augustus applique aux
  envahisseurs, sans rien changer aux dégâts :
  - corps à corps : au contact (`figure_combat_attack_figure_at`), y compris les soldats au fort ;
  - à distance : javelots, flèches et carreaux visent et touchent les soldats et les sentinelles de l'autre ville
    (`is_valid_missile_target`, `is_non_citizen` dans `figuretype/missile.c`) ; les balistes et les archers des
    tours de guet restent sur leur tour et ne sont pas des cibles (comme pour les envahisseurs) ;
  - une légion en formation « nettoyage » poursuit aussi les militaires de l'autre ville.
  Les habitants (marchands, préfets…) ne sont pas attaqués. Les envahisseurs du scénario attaquent tout le monde et
  sont attaqués par les deux villes, comme avant.
- **Bâtiments.** Un soldat arrêté à son étendard qui touche un bâtiment de l'autre ville le frappe tous les 4 ticks
  (`building_destroy_increase_enemy_damage`, mêmes points de vie que pour les envahisseurs : 10 pour un bâtiment,
  60 palissade et tour de guet, 150 porte, 200 mur). Pour attaquer une ville, on place donc sa légion contre ses
  bâtiments ; quand ils tombent, on avance la légion. Les forts ne se détruisent pas (comme pour les envahisseurs).
  Les effets de la destruction (note de paix, ruines) comptent pour la ville du bâtiment. Les villages indigènes ne sont
  à personne : les soldats ne les attaquent pas.
- **Légions par ville.** Une légion est à la ville de son fort. Chaque ville ne recrute (casernes), ne compte
  (conseiller militaire, nombre de légions permis pour poser un fort, totaux de soldats), ne fait défiler, ne
  sélectionne d'un clic et ne commande que les siennes : une commande de légion pour celle de l'autre ville est
  refusée. Le moral lié au mess (nourriture des soldats) est celui du mess de la ville de la légion ; il faut un mess
  dans sa ville pour poser un fort.
- **Affichage.** Les soldats, les étendards et les sentinelles de l'autre ville prennent sa teinte (comme ses
  bâtiments depuis M8). Un soldat qui frappe un bâtiment prend ses images d'attaque (corps à corps) ; il ne frappe
  pas pendant qu'il tire.
- **Avertissement.** Chez le joueur de la ville attaquée seulement : « La ville rouge attaque vos soldats ! »
  (corps à corps ou tir) ou « … vos bâtiments ! », au plus toutes les 15 secondes pour chaque sorte. C'est de
  l'affichage : la partie (et l'empreinte) n'en dépend pas.
- **Test** : `bash outils/test-combat.sh [ticks]` (12 000 par défaut), sur « Brutus - Deux rives ». Journal écrit à
  la main : un fort de légionnaires pour la ville 1, un fort de javeliniers, quatre puits et un mur pour la ville 2,
  soldats tout de suite (`triche` 106, pour les tests : chaque fort de la ville du joueur reçoit ses soldats), bataille
  au gué, puis la ville 1 refait sa légion et attaque le mur, puis les puits de la ville 2. Le bilan du rejouage donne, pour
  chaque ville, les soldats créés, vivants, tués par l'autre ville, et ses bâtiments détruits par l'autre ville.
  Résultat du 23/11 : 4 soldats de la ville 1 et 8 de la ville 2 tués (bataille entre les ticks 700 et 1 300),
  11 bâtiments de la ville 2 détruits (le mur, puis les puits) ; combat éteint : aucun mort ni bâtiment détruit entre les villes (3 soldats par
  ville désertent quand même : légion loin du fort sans nourriture, règle d'Augustus) ; empreintes identiques,
  rechargement en pleine bataille (tick 1 000) identique ; la commande du joueur 2 pour la légion 1 est refusée.

## Victoire (M11)

- **Victoire militaire** (choix du fondateur, 23/11) : on gagne en **détruisant entièrement la ville de l'autre**.
  Une ville a perdu quand les soldats de l'autre ville lui ont détruit au moins un bâtiment et qu'il ne lui reste
  plus aucun bâtiment que des soldats peuvent détruire (vérifié à chaque tick). Ne comptent pas, puisqu'ils ne
  tombent pas : les forts et leur terrain, les portes, les ponts, les barrages, les places (une case au moins du
  bâtiment doit être destructible, `map_routing_is_destroyable`). Les légions n'ont pas à être tuées. Une ville
  encore vide (jamais attaquée) ne perd pas. Ce qui disparaît autrement après la première attaque (incendie,
  démolition par son joueur) compte aussi. Deux villes détruites au même tick : égalité. Sans combat (option
  éteinte), il n'y a pas de victoire militaire.
- **Victoire économique.** La partie a une **durée** en mois, comptée depuis le passage en Versus : 10 ans par
  défaut. Au premier mois où elle est atteinte, la ville au meilleur score gagne (égalité possible) :
  `score = population + trésor / 10 + 25 × (prospérité + culture)` (notes de 0 à 100, celles du conseiller).
  En Versus local, **Alt+T** change la durée (5 ans, 10 ans, 20 ans, sans limite) : commande `triche` 107
  (valeur ≥ 0 : durée en mois, valeur < 0 : durée suivante), donc rejouable et déterministe. Une durée déjà
  dépassée finit la partie au mois suivant. Le choix dans le menu Multijoueur viendra avec M12.
- **Une seule fin.** La première des deux décide ; la partie continue ensuite si les joueurs le veulent, sans autre
  fin. En Versus, la victoire du scénario d'Augustus (objectifs, renvoi par César) ne s'applique pas
  (`city_victory_check`).
- **Écran de fin** (`multijoueur/fin_versus.c`) : chez chaque joueur, dès que la ville est à l'écran : « Victoire ! »,
  « Défaite » ou « Égalité », la raison, et les scores des deux villes au moment de la fin (population, trésor,
  prospérité, culture, score), avec « Continuer à jouer » et « Menu principal ». L'écran est de l'interface : la
  fin est décidée par la simulation, et l'écran s'ouvre hors simulation (une fois par fin, et de nouveau au
  chargement d'une partie finie).
- **Sauvegarde** : bloc `BR3V` version 6 : durée, mois écoulés, fin (raison, gagnant, tick), scores de la fin, et
  pour chaque ville la dernière ville qui lui a détruit un bâtiment.
  Un bloc plus ancien se lit « 10 ans depuis le chargement, pas de fin ».
- **Test** : `bash outils/test-victoire.sh [ticks]` (6 000 par défaut), sur « Brutus - Deux rives ». Journal écrit à la
  main : la ville 2 pose deux puits, la ville 1 envoie sa légion contre eux (et une variante avec un troisième puits
  hors d'atteinte) ; puis une durée de 2 mois, avec et sans dépense de la ville 2. Le bilan du rejouage donne la fin
  (`Brutus III : victoire : fin … gagnant … tick …`), et pour chaque ville son score et ses bâtiments restants. `--capture` enregistre aussi l'écran de fin.

Résultat du 23/11 : la légion de la ville 1 (16 soldats) détruit les deux puits de la ville 2, qui n'a plus rien, au tick
1 220 (« fin militaire, gagnant 1 ») ; le fort de la ville 1 n'est pas compté dans ses bâtiments ; combat éteint : pas de
fin ; un puits hors d'atteinte : pas de fin (1 bâtiment restant) ; empreintes identiques ; rechargement en plein
siège (tick 500) : même suite et même fin ; partie finie, sauvegardée puis rechargée : toujours finie, même gagnant et même tick. Durée de
2 mois : fin économique au tick 1 600, ville 1 gagnante (600 contre 598 : la ville 2 a payé quatre puits) ; sans
dépense : égalité. Le solo reste identique au bit près (Lugdunum, Tarraco, Carthago) ; les tests Versus et combat
passent.

## Versus en réseau et récap de fin (M12)

### Menu Multijoueur

- **Créer une partie** : l'hôte choisit d'abord le **mode** (Coop ou Versus), puis, en Versus, les **options** : combat
  (activé ou désactivé) et **durée** (5 ans, 10 ans, 20 ans, sans limite ; 10 ans par défaut). Ensuite « Nouvelle partie »
  (choix de la carte), « Charger une sauvegarde », ou, depuis une ville, « Créer avec cette ville ». Une ville déjà en
  Versus local ne peut devenir qu'une partie Versus.
- **Cartes à deux en tête** : en Versus, la liste des cartes commence par celles qui ont une entrée pour le joueur 2
  (bloc `BR3C` : nos trois cartes et celles faites dans l'éditeur), dans l'ordre d'origine ; les autres suivent (les deux
  villes partent alors de la même entrée). `brutus_carte_versus_fichier_a_deux()` lit les 25 derniers octets du fichier.
- **Ce que voient les invités** : l'hôte donne au relais une ligne d'information (« Versus, carte « Brutus - Deux rives »,
  combat activé, 10 ans ») ; le relais la renvoie à chacun dans `bienvenue` (docs/protocole.md) et la fenêtre
  Multijoueur l'affiche à côté du code, avant le départ comme pendant la partie.
- **Départ** : en Versus, l'hôte est mis en pause tant que l'autre joueur n'est pas là (pas d'avance gratuite). Au
  premier tour du lockstep, l'hôte envoie trois commandes, appliquées par tous au même tick : `triche` 103 (passage en
  Versus), 105 (combat), 107 (durée) — les mêmes que Alt+W, Alt+G, Alt+T en local. Si une resynchronisation les perd,
  elles repartent 400 ticks plus tard (`brutus_menu_versus_lancer`, appelé à chaque avance du lockstep). Le salon
  Versus a 2 places.
- **Chacun sa ville** : en réseau, le joueur n joue la ville n (hôte : bleue ; invité : rouge). Au passage en Versus et
  à l'arrivée dans une partie Versus, la vue de l'invité part de l'entrée de sa ville (« Versus : vous jouez la ville
  rouge. »). Lors d'une resynchronisation, chacun garde sa vue (avant M12 la sauvegarde imposait celle de l'hôte, en
  Coop aussi). Alt+W, Alt+G et Alt+T restent refusés en réseau.
- **Ville active et chargement** (bug trouvé par le test réseau) : hors simulation, la ville active est la ville vue,
  donc la 2 chez l'invité. Le chargement d'une partie se fait maintenant toujours avec la ville 0 active (début de
  `savegame_load_from_state`, fin de `game_file_load_saved_game`) ; la ville vue ne redevient active qu'après
  (`brutus_versus_apres_chargement`). Avant, chaque resynchronisation mettait chez l'invité les données de la ville 1
  dans la case de la ville 2, et la partie divergeait. `--joueur-local N` (rejouage) rejoue comme le joueur N ; le
  test du récap vérifie qu'avec des resynchronisations le joueur 2 a la même partie que l'hôte.

### Pseudos (demandés par le fondateur le 23/11)

- **Saisie** : champ « Pseudo » en haut de la fenêtre Multijoueur (20 caractères), rempli au départ avec celui retenu
  par le navigateur (`localStorage`, clé `brutus-pseudo`). Il part au relais en créant ou en rejoignant une partie, et à
  chaque reconnexion (le client le garde avec sa session).
- **Transport** : le relais le nettoie et le met dans la liste `joueurs` de chaque place (docs/protocole.md). Il ne passe
  jamais par la simulation : pas d'effet sur l'empreinte, rien dans la sauvegarde.
- **Affichage** (sans pseudo : « Joueur N », « la ville bleue ») : liste des joueurs du salon, chat, message de chat en
  haut de la ville, avertissement de combat (« Marcus (rouge) attaque vos bâtiments ! »), écran de fin (raison,
  légende des courbes, bulle, tableau, faits marquants : « la ville de Léa »). Les couleurs restent : bleue pour la
  ville 1, rouge pour la ville 2. En Versus local (un seul écran), pas de pseudos.
- **Test** : dans `test-reseau-memoire.sh --versus`, l'hôte s'appelle Léa et l'invité Marcus ; chacun doit voir le
  pseudo de l'autre à la fin, après coupures, rechargement de la page de l'hôte et second onglet.

### Récap de fin de partie

- **Relevés** (`multijoueur/recap_versus.c`) : au passage en Versus, puis au début de chaque mois (après les notes) et
  au moment de la fin, pour chaque ville : population, trésor, prospérité, culture, score, soldats des légions,
  bâtiments construits, bâtiments perdus et soldats tués (ces trois-là cumulés depuis le début). Les relevés
  s'arrêtent à la fin de la partie (au plus 1 202, soit 100 ans).
  - *construits* : bâtiments posés par une commande de construction de la ville (un lot de maisons compte pour
    chaque maison ; les parties d'un grand bâtiment ne comptent pas) ;
  - *perdus* : bâtiments en service détruits par incendie, effondrement, tremblement de terre, peste, émeute ou
    ennemi (pas la démolition par son joueur) ;
  - *tués* : militaires de la ville tués par l'autre ville.
  Le premier bâtiment détruit par les soldats de l'autre ville est aussi noté (ville, type, date).
- **Même récap chez les deux joueurs** : les relevés sont faits par la simulation et rangés dans le bloc `BR3V`
  (**version 7**, à la suite de la fin de partie) : ils sont dans l'empreinte, dans chaque sauvegarde et dans chaque
  resynchronisation. Un bloc plus ancien se lit sans récap.
- **Écran de fin** (`multijoueur/fin_versus.c`, 608 × 464) : titre et raison, puis des onglets : Score, Population,
  Trésor, Prospérité, Culture, Soldats, Construits, Perdus, Tués (une courbe par ville, bleue et rouge, la sienne
  par-dessus, graduations rondes, dates en bas, valeur finale à droite, légende ; au survol, un repère et une bulle
  avec la date et les deux valeurs) et Tableau (scores de la fin et derniers totaux). Dessous, les faits marquants :
  durée de la partie, pic de population de chaque ville, plus grosse bataille (le mois où le plus de soldats sont
  morts), premier bâtiment détruit, bascule du score (le dernier mois où la tête a changé). Couleurs vérifiées pour
  les daltonismes et le contraste sur le fond clair des courbes.
- **Test** : `bash outils/test-recap.sh [mois]` (12 par défaut) sur « Brutus - Deux rives » : un bot par ville, une
  bataille au gué, un siège, durée de N mois. Deux rejouages identiques (empreintes et récap) ; N + 1 relevés, le
  dernier marqué fin, un mois de jeu entre deux relevés ; soldats tués dans les deux villes, premier bâtiment détruit
  à la ville 2 ; rechargement au milieu par un lancement neuf : même récap que l'hôte ; partie finie rechargée : même
  récap. `--capture` enregistre l'écran de fin (onglet du score, population avec la bulle, tableau), copiées dans
  `build-rejouage/recap/captures/` (images du jeu : jamais commitées).
- **Test réseau** : `bash outils/test-reseau-memoire.sh court --versus` (deux moteurs wasm dans Node, relais en
  mémoire, tout le scénario de coupures et de resynchronisations du test réseau) : l'hôte crée en Versus comme le
  menu ; l'invité voit la ligne d'information, chacun voit sa ville, mêmes options, même fin et même récap.

## Laissé aux missions suivantes

- **Combat** : les forts adverses ne se détruisent pas (comme pour les envahisseurs).
- **Envahisseurs** : créés hors passe de ville, donc à la ville 1 (ce qui ne change rien au combat : ils attaquent et
  sont attaqués par tous). Nos cartes n'ont pas de point d'invasion.
- **Victoire** : une ville qui ne garde qu'un bâtiment hors d'atteinte (une île, derrière l'eau) ne peut pas être battue
  militairement.
- **Empire** (routes ouvertes, prix), **événements du scénario**, bataille lointaine : communs. Sur « Rivage »,
  l'entrée et la sortie des navires sont communes.
- Les jalons de population (« 500 habitants »…) sont comptés une fois pour toute la partie.
- Le bot de test construit mal (sa population tombe vers 100 habitants vers le tick 20 000, avant comme après M8) :
  les chiffres de fin ne mesurent pas la qualité du jeu.
