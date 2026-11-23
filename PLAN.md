# Brutus III : plan du projet

Ce plan est destiné à ivwi Office. Il a été rédigé le 18/11/2026 à partir de l'analyse faite avec le fondateur. Les choix ci-dessous sont **déjà faits et vérifiés**. Ne les rouvrez pas et ne lancez pas de recherche web générale dessus. Seule exception : la documentation technique précise (Emscripten, Cloudflare Durable Objects), et seulement quand elle est nécessaire.

## 1. Le projet en une phrase

Une version modifiée de Caesar III, jouable dans le navigateur, à deux en ligne et en temps réel, avec deux modes :

- **Coop** : les deux joueurs gèrent la **même ville** en même temps (même trésor, même économie, mêmes événements).
- **Versus** : chaque joueur construit **sa ville sur la même carte**. On gagne militairement (en détruisant l'autre) ou par la meilleure économie à la fin du temps imparti.

Le fondateur veut aussi pouvoir y jouer sur son PC Windows. Le navigateur suffit pour ça.

## 2. Décisions déjà prises

| Sujet | Décision |
|---|---|
| Base du code | **Augustus** (fork de Julius, réécriture libre de Caesar III en C + SDL2, licence **AGPL-3.0**). Il est importé dans `moteur/` avec `git subtree --squash`. |
| Fichiers du jeu | Ce sont les données de la version Windows, extraites de l'installeur `setup-01612-Caesar_III_Trad_FR_1_4-PCWin.exe` (Inno Setup 6.1, version française). Elles vont dans `jeu-original/`, **ignoré par git**. Le CD Mac (`.toast`) n'est pas utilisé. |
| Version web | Compilation Emscripten. Julius documente déjà ce chemin (wiki « Building for Emscripten », option CMake pour les ports SDL). |
| Multijoueur | **Lockstep déterministe** : chaque joueur fait tourner la même simulation et le réseau ne transporte que les **commandes** des joueurs. On ne synchronise jamais l'état complet en continu. La sauvegarde complète sert seulement au démarrage et à la resynchronisation. |
| Serveur | **Cloudflare Worker + Durable Object** (un objet par salon). C'est un simple relais WebSocket, sans aucune logique de jeu. Développement local avec `wrangler dev` (npm). |
| Hébergement | Le code est **public** sur GitHub (`DoubleV840/Brutus-III`, obligatoire avec l'AGPL). La page est servie par **GitHub Pages**, à l'adresse **brutus.ivwi.fr** (CNAME chez OVH, comme pastel.ivwi.fr). |
| Plateforme multijoueur | Navigateur uniquement : les deux joueurs utilisent le même binaire wasm, ce qui aide le déterminisme. La compilation native (macOS/Windows) sert au développement et au solo. |
| Nom | **Brutus III**. Le nom « Caesar » n'apparaît jamais comme nom du produit (marque d'Activision). Mention autorisée : « nécessite les fichiers originaux de Caesar III ». |

## 3. Règles du projet (pour tous les agents)

1. **Aucun fichier du jeu original** (`.sg2 .555 .smk .eng .emp .map .wav`, vidéos, etc.) ne doit être commité ni publié sur le site. Chaque joueur charge ses propres fichiers dans son navigateur. Vérification avant chaque fin de mission : `git ls-files | grep -iE '\.(sg2|555|smk|eng|emp|map|wav)$' | grep -v '^moteur/'` doit être vide (`moteur/` est exclu : Augustus y fournit ses propres cartes libres, `moteur/res/maps/*.map`).
2. **Licence AGPL** : garder `moteur/COPYING` (ou équivalent) et les crédits Julius/Augustus, et mettre un lien « Code source » visible dans le jeu et sur la page.
3. **Le solo doit toujours marcher.** Chaque phase se termine avec un solo jouable.
4. **Isoler nos modifications** : le nouveau code va dans des fichiers à nous (par exemple `moteur/src/multijoueur/…`). Les fichiers d'Augustus ne reçoivent que des points d'accroche courts. Ainsi, `git subtree pull` pourra récupérer les mises à jour d'Augustus sans trop de conflits.
5. **Bac à sable** : les agents n'ont internet que vers npm et PyPI. Tout ce qui doit être téléchargé ailleurs (GitHub, Homebrew, ports Emscripten…) est mis dans un script de `outils/` que **le fondateur lance une fois**, et figure dans le récap de fin de mission.
6. **Pas de push** : c'est le fondateur qui confirme les pushs dans ivwi Office.
7. **Économie de tokens** : pas d'étude juridique, de benchmark ou de recherche « au cas où ». L'analyse est faite et se trouve dans ce document.

## 4. Organisation du dépôt (cible)

```
Brutus-III/
├── PLAN.md               ce document
├── README.md             présentation, licence, comment jouer
├── outils/               scripts du fondateur et de compilation (preparer.sh, build-web.sh, …)
├── moteur/               Augustus (subtree) + nos modifications en C
├── web/                  page d'accueil, chargement des fichiers du jeu, menu multijoueur
├── serveur-multi/        Cloudflare Worker + Durable Object (relais WebSocket)
├── docs/                 commandes.md, determinisme.md, protocole.md, versus.md
├── .github/workflows/    compilation Emscripten + déploiement GitHub Pages
└── jeu-original/         (ignoré) fichiers du jeu du fondateur, pour les tests
```

## 5. Les phases

Chaque phase correspond à une ou plusieurs missions ivwi Office. Une mission livrée est fusionnée sur `main`.

### Phase 0 : état des lieux et récap du CEO (sans code)

**Objectif** : le fondateur veut un récap clair de **ce qui est en place** et de **ce qu'il doit faire de son côté**.

- Le CEO fait vérifier par **un seul** agent dev (lecture et commandes de vérification uniquement) :
  - l'état git (branche, commits, remote `origin`) ;
  - la présence de `moteur/` (Augustus importé ?) ;
  - la présence de `jeu-original/` avec `c3.eng` ;
  - les outils : `cmake`, `emcc`, `innoextract`, SDL2 (`brew list sdl2 sdl2_mixer`), `node`.
- Le CEO rend un récap court en trois parties :
  1. **En place** : ce qui est prêt.
  2. **À faire par le fondateur**, dans l'ordre, avec les commandes exactes à copier-coller (voir § 6).
  3. **Prochaines missions** : le découpage proposé des phases 1 à 5, avec le niveau de mission conseillé pour chacune.
- Niveau conseillé : **rapide**.

### Phase 1 : le jeu tourne en solo, sur Mac et dans le navigateur

- **1a. Compilation native macOS** de `moteur/` (CMake + SDL2 Homebrew), lancée avec `jeu-original/`. Vérifier que la VF « Trad FR 1.4 » est reconnue (textes en français).
- **1b. Compilation web Emscripten**, avec `EM_CACHE=.emcache` dans le dépôt (ignoré par git). Les ports Emscripten (SDL2, SDL2_mixer…) se téléchargent depuis GitHub, ce que le bac à sable bloque. Il faut donc écrire `outils/build-web.sh`, que le fondateur lance **une fois** pour remplir `.emcache`. Ensuite, les agents recompilent sans réseau.
- **1c. Page web** (`web/`) :
  - accueil « Brutus III », avec la mention des fichiers originaux requis et un lien vers le code source ;
  - sélection du dossier du jeu (glisser-déposer ou sélecteur de dossier), copie des seuls fichiers utiles, barre de progression ;
  - stockage dans le navigateur (IDBFS/IndexedDB) pour ne le faire qu'une fois ;
  - sauvegardes persistantes.
- **1d. CI** : `.github/workflows/pages.yml` compile la version Emscripten sur GitHub Actions et la déploie sur GitHub Pages, **sans aucun fichier du jeu**.
- **Terminé quand** :
  - le solo est jouable en français dans Chrome sur le Mac, depuis le serveur local ;
  - sauvegarder puis recharger fonctionne ;
  - la vérification de la règle 1 passe ;
  - le README explique comment compiler.
- Niveau conseillé : **normal**.

### Phase 2 : fondation multijoueur (lockstep)

C'est la phase la plus délicate techniquement. Les deux modes reposent dessus.

- **2a. Couche de commandes** : toute action du joueur qui modifie la partie devient une commande `{type, joueur, tick, paramètres}`, appliquée par la simulation au début d'un tick. Le solo passe par le même chemin, ce qui le teste en permanence.
  - Inventaire complet dans `docs/commandes.md` : construire, démolir, routes et murs, impôts et salaires, priorités de main-d'œuvre, ordres des entrepôts et greniers, commerce (ouvrir une route, quantités), fêtes, dons et réponses à César, édits, ordres aux légions, vitesse et pause, etc.
  - Repérer les endroits où l'interface modifie l'état directement (pistes à confirmer dans le code : `src/building/construction*.c`, `src/window/*`, `src/widget/city*.c`).
- **2b. Déterminisme** :
  - audit de `rand()`, `time()`, l'horloge réelle, la mémoire non initialisée, l'ordre d'itération et les flottants (piste : `src/core/random.c`) ;
  - **empreinte** (hash) de l'état de la partie tous les N ticks ;
  - **test de rejouage automatique** : même sauvegarde + même journal de commandes, rejoué deux fois, doit donner les mêmes empreintes. Le test tourne **sans affichage** (pilote SDL `dummy`) pour pouvoir être lancé par les agents et par la CI ;
  - résultats dans `docs/determinisme.md`.
- **2c. Serveur relais** (`serveur-multi/`) :
  - Worker + Durable Object, WebSocket, salon identifié par un code à 6 caractères, 2 joueurs (prévoir jusqu'à 4) ;
  - le serveur relaie les commandes tick par tick et ne contient aucune logique de jeu ;
  - protocole documenté dans `docs/protocole.md` ;
  - tests avec `wrangler dev`. Si le bac à sable empêche d'ouvrir un port local, le signaler dans le récap.
- **2d. Client réseau** dans la version web :
  - le tick N n'avance que lorsque les commandes de **tous** les joueurs pour N sont arrivées (un message vide sinon), avec un délai d'entrée d'environ 100 à 200 ms ;
  - vitesse et pause partagées ;
  - en cas de déconnexion, la partie se met en pause et attend la reconnexion.
- **2e. Rejoindre et resynchroniser** : l'hôte envoie sa sauvegarde complète quand l'autre joueur arrive, et aussi quand les empreintes divergent (avec un message « resynchronisation… »).
- **Terminé quand** :
  - deux onglets reliés au serveur local restent synchronisés pendant 30 minutes de jeu accéléré, avec un bot de test qui fait des actions au hasard dans les deux onglets ;
  - le test de rejouage passe ;
  - le solo marche toujours.
- Niveau conseillé : **approfondi**, en 2 ou 3 missions (2a+2b, puis 2c+2d+2e).

### Phase 3 : mode Coop (la même ville à deux)

- Menu « Jouer à deux » : **Créer une partie** (l'hôte choisit une carte, un scénario ou une sauvegarde) ou **Rejoindre** (avec le code).
- Les deux joueurs ont tous les pouvoirs. Chacun a sa caméra et ses fenêtres. Les messages et les événements (César, invasions, fêtes, catastrophes) s'affichent chez les deux.
- On voit sur la carte où se trouve l'autre joueur (curseur ou cadre de sa vue). Ajouter des pings sur la carte et un petit chat.
- Conflits : si deux ordres incompatibles tombent au même tick, ils sont appliqués dans un ordre déterministe (l'hôte d'abord), et le second échoue proprement avec un message.
- Sauvegarder une partie coop et la reprendre à deux.
- **Terminé quand** : une carte complète a été jouée à deux sans désynchronisation (bot + test humain du fondateur sur deux machines).
- Niveau conseillé : **normal**.

### Phase 4 : mode Versus (deux villes sur la même carte)

C'est le plus gros chantier côté jeu : le moteur suppose **une seule ville par carte**. Le découper en missions :

- **4a. Modèle à deux villes, sans réseau** :
  - chaque bâtiment et chaque personnage a un **propriétaire** ;
  - les données de ville sont séparées par joueur : trésor, population, logements, emplois, notes (culture, prospérité, paix, faveur), demandes de César, édits, commerce (piste : `src/city/data_private.h` et tout ce qui lit `city_data`) ;
  - chaque système (main-d'œuvre, services, marchés, entrepôts, commerce) ne travaille que pour son propriétaire. Terrain et routes restent partagés ;
  - test en local, sur un seul écran, avec une touche pour changer de joueur.
- **4b. Cartes à deux** : un point d'entrée et un point de départ par joueur, l'éditeur de cartes adapté, et **2 ou 3 cartes versus créées par nous**. Les cartes d'origine sont des fichiers du jeu : on ne les distribue pas.
- **4c. Combat** : les légions, les tours et les murs d'un joueur attaquent les soldats et les bâtiments de l'autre, en réutilisant la logique des envahisseurs. Le combat est une option (activé ou désactivé).
- **4d. Victoire** :
  - militaire : destruction du bâtiment central de l'autre (Sénat ou palais du gouverneur, à trancher en 4a) ;
  - économique : score à la fin d'une durée choisie (population, trésor, prospérité, culture) ;
  - écran de fin avec le détail des scores.
- **4e. Réseau** : brancher le mode sur la couche de la phase 2. Les sauvegardes versus ont un format versionné.
  - **Choix du mode dans le menu « Multijoueur »** (demandé par le fondateur le 23/11/2026) : à la création d'une
    partie, l'hôte choisit d'abord le mode, **Coop** (la même ville à plusieurs) ou **Versus** (chacun sa ville sur
    la même carte), puis la carte. En Versus, les cartes à deux (les nôtres et celles faites dans l'éditeur avec
    une entrée « joueur 2 ») apparaissent en premier ; une carte ordinaire reste possible (les deux villes partent
    alors de la même entrée).
  - **Options du Versus** dans le même écran : combat activé ou non (4c), durée de la partie pour la victoire
    économique (4d).
  - Les invités voient le mode, la carte et les options dans le salon avant le départ. Le passage en Versus se fait
    par une commande au premier tick, comme Alt+W en local, pour que tous démarrent sur la même partie.
- **Terminé quand** : une partie versus complète a été jouée à deux en ligne, et les deux fins (militaire et économique) ont été testées.
- Niveau conseillé : **approfondi**, une mission par sous-phase.

### Phase 5 : mise en ligne

- Déploiement du Worker sur le compte Cloudflare du fondateur, et de la page sur GitHub Pages à l'adresse **brutus.ivwi.fr**.
- Page « Comment jouer » :
  1. avoir le jeu original (GOG ou Steam) ;
  2. glisser son dossier ;
  3. créer une partie ou en rejoindre une avec un code.
- Premier vrai test du fondateur avec son ami. Chacun utilise **ses propres** fichiers du jeu.
- Niveau conseillé : **rapide**.

## 6. Actions du fondateur (connues à l'avance)

Le CEO confirme et complète cette liste dans son récap (phase 0, puis à chaque fin de mission).

1. **Ajouter le projet dans ivwi Office** : « ＋ Nouveau projet… », nom « Brutus III », dossier `~/Programmation/Perso/app-idée-test/Brutus-III`. Le dossier est déjà un dépôt git relié à GitHub, et ivwi Office y fera le premier commit.
2. **Lancer une fois** :
   ```
   bash ~/Programmation/Perso/app-idée-test/Brutus-III/outils/preparer.sh
   ```
   Le script installe les outils Homebrew, importe Augustus dans `moteur/` et extrait les fichiers du jeu du `.exe` (dans Téléchargements) vers `jeu-original/`. Si l'extraction échoue : installer le jeu sur le PC Windows et copier le dossier installé dans `jeu-original/`.
3. **Confirmer les pushs** vers GitHub dans ivwi Office. Le premier push crée `main` sur GitHub.
4. Plus tard, quand le récap le demande :
   - lancer `outils/build-web.sh` une fois (ports Emscripten) ;
   - créer un compte Cloudflare, puis `npx wrangler login` ;
   - GitHub, Settings → Pages → Source : « GitHub Actions » ;
   - chez OVH : CNAME `brutus` → `doublev840.github.io` ;
   - vérifier que le dépôt est **public**.
5. **Côté ami** : il doit avoir son propre Caesar III (GOG ou Steam, quelques euros).

## 7. Risques connus

| Risque | Parade |
|---|---|
| Désynchronisation (déterminisme imparfait) | Test de rejouage et empreintes dès la phase 2. Resynchronisation par la sauvegarde. Même wasm pour les deux joueurs. |
| La VF « Trad FR 1.4 » n'est pas reconnue par Augustus | À vérifier en 1a. Sinon, adapter la détection de langue ou se rabattre sur les fichiers anglais. |
| Fichiers du jeu lourds à charger dans le navigateur | Ne copier que l'utile (vidéos et musiques en option), avec le cache IndexedDB. |
| Bac à sable sans GitHub ni Homebrew | Scripts dans `outils/` lancés par le fondateur, signalés dans chaque récap. |
| Mises à jour d'Augustus | `git subtree pull --prefix=moteur https://github.com/Keriew/augustus.git <branche> --squash`, et nos modifications restent isolées (règle 4). |
| Le versus touche tout le moteur | Mode local à deux joueurs (4a) avant le réseau, et des missions courtes. |
