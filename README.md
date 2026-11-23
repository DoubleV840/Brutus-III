# Brutus III

Projet créé depuis ivwi Office le 18/11/2026. Demande de départ : Nouveau projet Brutus III. Lis PLAN.md à la racine du projet : tout est déjà décidé, pas de recherche web. Fais uniquement la phase 0, sans écrire de code : fais vérifier l'état du projet par un seul dev, puis rends-moi un récap court. 1) Ce qui est en place. 2) Ce que je dois faire de mon côté, dans l'ordre, avec les commandes exactes. 3) Le découpage des missions suivantes que tu proposes, avec le niveau conseillé pour chacune.

## Compiler

Prérequis (macOS, Homebrew) : `brew install cmake sdl2 sdl2_mixer emscripten` (`sdl2` est fourni par `sdl2-compat`, ça marche).
Les fichiers du jeu original vont dans `jeu-original/` (ignoré par git, voir `outils/preparer.sh`).

### Version native macOS (développement, solo)

```bash
cmake -S moteur -B build-natif -DCMAKE_BUILD_TYPE=Release
cmake --build build-natif -j 8
```

Lancer avec les fichiers du jeu (le dossier qui contient `c3.eng`) :

```bash
build-natif/augustus.app/Contents/MacOS/augustus --windowed "$PWD/jeu-original/app/jeu1"
```

Le journal s'affiche dans le terminal (et dans `~/Library/Application Support/augustus/augustus/augustus-log.txt`). Avec la VF, il doit contenir `Detected language: French`.
Sans écran (tests automatiques) : préfixer par `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`.

### Version web (WebAssembly)

```bash
bash outils/build-web.sh          # ou : bash outils/build-web.sh Debug
```

Sortie : `build-web/augustus.html`. Le cache Emscripten est dans `.emcache/` (ignoré par git) ; la 1re fois, les ports (SDL2, SDL2_mixer, ogg, vorbis, mpg123) se téléchargent depuis GitHub, ensuite plus besoin de réseau. Aucun fichier de `jeu-original/` n'est intégré (le script le vérifie).

La page vient de `web/shell.html.in` (dérivée de celle d'Augustus, passée au moteur par l'option CMake `BRUTUS_SHELL_IN`). Pour jouer en local :

```bash
cd build-web && python3 -m http.server 8000
# puis ouvrir http://localhost:8000/augustus.html dans Chrome
```

Au premier chargement, la page demande le dossier du jeu (celui qui contient `C3.sg2` et `c3.eng`, ou un dossier parent). Elle copie les fichiers utiles dans le stockage du navigateur (IndexedDB, via IDBFS monté sur `/C3`). Les exécutables, DLL et notes ne sont pas copiés. Ensuite, le dossier n'est plus demandé. Les sauvegardes (`.sav`, `.svx`), la config et la progression de campagne sont enregistrées dans le navigateur après chaque écriture. « Changer de dossier du jeu » garde les sauvegardes. « Tout effacer » supprime tout.

Test sans navigateur de la page (IDBFS, copie, sauvegardes après rechargement) :

```bash
cd web/test && npm install && npm test   # après bash outils/build-web.sh
```

## Multijoueur : commandes et déterminisme

Les actions du joueur passent par une file de commandes (`moteur/src/multijoueur/`, inventaire dans `docs/commandes.md`). Test de rejouage sans affichage (il demande les fichiers du jeu ; sans eux, il est sauté) :

```bash
bash outils/test-rejouage.sh        # natif : même journal rejoué 2 fois, empreintes comparées, puis sauvegarde → rechargement → suite
bash outils/test-rejouage-wasm.sh   # wasm sous Node (cible séparée) + comparaison natif ↔ wasm
```

Méthode, audit et résultats : `docs/determinisme.md`. Les sorties vont dans `build-rejouage/` (ignoré par git).

## Multijoueur : relais local (serveur-multi/)

Le relais est un Cloudflare Worker + un Durable Object par salon. Il ne fait que transmettre les messages des joueurs, sans logique de jeu. Protocole : `docs/protocole.md`. Il est branché dans la version web (client `web/lockstep.js`, menu « Multijoueur » du jeu), mais pas encore déployé (M13) : tout tourne en local, sans compte Cloudflare.

```bash
cd serveur-multi
npm ci              # installe wrangler (version exacte, package-lock.json)
npm test            # tests automatiques, sur le vrai runtime local (workerd)
npm run demo        # démo : deux joueurs créent / rejoignent un salon et échangent
npm run dev         # relais local sur ws://127.0.0.1:8787 (Ctrl+C pour arrêter)
npm run demo -- ws://127.0.0.1:8787   # la démo contre le relais lancé par npm run dev
```

`npm test` et `npm run demo` démarrent eux-mêmes un relais local sur un port libre de 127.0.0.1, puis l'arrêtent. S'il est impossible d'ouvrir un port local (bac à sable), ils s'arrêtent au bout de 30 s avec un message : seul `npm run test:sans-port` (réassemblage des sauvegardes) tourne alors. Le job CI `relais` lance `npm test`, mais le déploiement du site ne dépend pas de lui.

## Multijoueur en local

Trois terminaux (le relais, la page, puis le navigateur) :

```bash
bash outils/build-web.sh                                  # une fois (et après chaque changement du moteur ou de web/)
cd serveur-multi && npm ci && npm run dev                 # terminal 1 : le relais, ws://127.0.0.1:8787
cd build-web && python3 -m http.server 8000               # terminal 2 : la page
```

Puis, dans **une fenêtre de Chrome par joueur, côte à côte** (deux à quatre ; un onglet masqué est ralenti par le navigateur, les autres joueurs l'attendent) : ouvrir `http://localhost:8000/augustus.html` dans chacune, choisir le dossier du jeu dans chacune.

Tout se fait dans le jeu, par le menu **« Multijoueur »** : un bouton au menu principal (sous « Nouvelle campagne »), et la première ligne du menu **Fichier** en partie.

1. Fenêtre 1 (hôte) : **Multijoueur** → choisir le mode (**Coop** ou **Versus**) ; en Coop, **« Places »** règle le nombre de joueurs (2, 3 ou 4, toi compris ; le Versus reste à 2) → **« Nouvelle partie »** (choix de la carte) ou **« Charger une sauvegarde »** ; la partie multijoueur est créée dès que la ville est à l'écran, et la fenêtre montre le code de 6 caractères. Depuis une ville déjà ouverte : **Fichier → Multijoueur → « Créer avec cette ville »**.
2. Chaque autre fenêtre : **Multijoueur** → saisir le code (minuscules acceptées) → **« Rejoindre »**, en restant au menu principal. Tant que la partie n'a pas démarré, c'est le **salon** : chacun voit qui est là et qui est prêt, et clique **« Je suis prêt »** (l'hôte aussi). Quand tous les joueurs connectés (deux au moins) sont prêts, l'hôte envoie sa partie, tous la chargent (« resynchronisation… ») et le jeu démarre pour tous. En Coop, on peut encore rejoindre une partie déjà commencée, tant qu'il reste une place : on y entre directement.
3. Vitesse et pause sont partagées. Si un joueur se déconnecte, le jeu se met en pause pour tous (« en attente de … ») et reprend à son retour. L'hôte peut recharger sa page : la partie est récupérée auprès de l'invité.
4. « Quitter la partie » revient au solo.
5. Mode Coop : vous gérez **la même ville**, avec les mêmes pouvoirs ; chacun a sa caméra et ses fenêtres, et les événements (César, invasions, fêtes, catastrophes) s'affichent chez tous. Sur la mini-carte, un **cadre de couleur** montre la vue de chaque autre joueur (bleu, orange, vert, violet pour les joueurs 1 à 4), et la case sous sa souris est colorée dans la ville. **Touche G** : signaler l'endroit sous la souris (il clignote chez tous ; « Voir » dans le chat y amène). La fenêtre **Multijoueur** (Fichier → Multijoueur) a un **chat** (un message reçu s'affiche aussi en haut de la ville), la latence de chaque joueur, et pour l'hôte un bouton **« Retirer »**. Si deux ordres incompatibles tombent dans le même tour (par exemple deux bâtiments sur la même case), le premier dans l'ordre des joueurs (l'hôte d'abord) passe et les autres voient « Un autre joueur a agi au même moment : ton ordre n'a pas abouti. »
6. Sauvegarder fonctionne chez tous les joueurs. Pour **reprendre une partie multijoueur**, l'hôte charge la sauvegarde (avant de créer la partie, ou après tant que personne n'est arrivé), puis les autres rejoignent.
7. En partie multijoueur, **« Charger », « Recommencer » et « Quitter la mission » sont désactivés** (ils changeraient la ville d'un seul joueur : divergence). Si tu quittes quand même la ville par le menu principal, la partie se met en attente pour tous et la fenêtre Multijoueur s'ouvre : le bouton **« Revenir dans la partie »** te ramène dans la partie en cours. Chacun garde une copie de la partie en quittant la ville : même si tous les joueurs l'ont quittée, le premier qui clique « Revenir » relance la partie pour tous. Si l'hôte reste bloqué plus de 60 s à récupérer sa partie, il est ramené en solo avec un message.

Le multijoueur ne fonctionne que si la page est servie depuis `localhost` ou `127.0.0.1` (relais en local jusqu'à sa mise en ligne, M13) ; ailleurs, et dans la version native, la fenêtre Multijoueur explique où jouer. Si quelque chose cloche, noter le message affiché dans la fenêtre et la console du navigateur.


Tests du client réseau, sans navigateur :

```bash
bash outils/test-reseau-memoire.sh court   # ~30 s : deux moteurs wasm dans Node, relais en mémoire, aucun port
bash outils/test-reseau-memoire.sh court --joueurs=3   # Coop à 3 (ou --joueurs=4) : arrivée en cours de partie, coupures du joueur 3, rechargement de sa page ; rapport : build-reseau/rapport-memoire-coop3.txt
# Image de la fenêtre Multijoueur à 4 (accueil, salon, mini-carte) : moteur natif, --rejouer <svx> <journal> --empreintes <f> --ticks 200 --multijoueur --capture-salon <dossier du jeu> (PNG dans le dossier du jeu)
bash outils/test-reseau-memoire.sh         # 225 000 ticks (30 minutes de jeu à 200 %), ~1 min ; rapport : build-reseau/rapport-memoire.txt
bash outils/test-reseau-30min.sh           # même scénario avec le VRAI relais en local (wrangler dev) et la cadence réelle du jeu, ~10 min ; rapport : build-reseau/rapport-relais.txt
BRUTUS_VITESSE=200 bash outils/test-reseau-30min.sh 225000 Lugdunum --production   # constantes de production (ping 20 s, coupure 45 s…), 20 min réelles ou plus
```

Le premier lance une compilation wasm pour Node (`build-reseau-wasm/`, quelques minutes la première fois). Le dernier ouvre un port local : à lancer dans ton terminal, pas dans un bac à sable. `--production` garde les délais réels du client (une empreinte tous les 50 groupes, ping 20 s, coupure 45 s) ; rapport dans `build-reseau/rapport-relais-production.txt`. **Code de sortie 3 = test non exécuté** (jeu absent) : ce n'est pas un succès. Un test réussi affiche « RÉSULTAT OK » et sort avec 0. Le scénario (coupures, resynchronisations, panne injectée, rechargement de l'hôte) est décrit dans `docs/determinisme.md`. Depuis le 22/11, il couvre aussi la route de Rome coupée par des murs, une forte latence (300 à 700 ms entre joueurs), une arrivée pendant une resynchronisation et la même partie ouverte dans deux onglets (ces trois derniers avec le relais en mémoire seulement).

## Versus local (M8, M9)

Deux villes sur la même carte, sur un seul écran : en ville, **Alt+W** fait passer la partie en Versus, puis chaque **Alt+W** fait jouer l'autre ville (trésor, population, bâtiments, messages et demandes de César à elle ; les bâtiments de l'autre ville sont teintés de sa couleur : 1 bleue, 2 rouge). Principe, limites et ce qui reste : `docs/versus.md`. Test automatique : `bash outils/test-versus.sh` (deux bots, déterminisme, sauvegarde et rechargement).

Cinq cartes à deux, créées par nous et installées au démarrage dans le dossier des cartes : **Brutus - Deux rives**, **Brutus - Lac central**, **Brutus - Rivage**, **Brutus - Collines**, **Brutus - Marais** (symétriques, une entrée et une sortie par joueur). Au départ d'un Versus, chaque joueur arrive au centre de sa zone. L'éditeur de cartes place l'entrée et la sortie du joueur 2 (menu « Points des personnes »). Les cartes se refont avec `bash outils/creer-cartes-versus.sh`.

## Publication

La CI `.github/workflows/pages.yml` publie la version web sur GitHub Pages (https://brutus.ivwi.fr) à chaque push sur `main` (ou à la main : onglet Actions → Pages → « Run workflow ») :

1. **build** : installe Emscripten 7.0.0, restaure le cache des ports (`.emcache/`), lance `bash outils/build-web.sh`, qui prépare `build-pages/` (`index.html` = la page du jeu, `CNAME`, `.nojekyll`), puis le garde-fou `outils/verifier-publication.sh`.
2. **test** : `npm ci && npm test` dans `web/test`, sur la page publiée.
3. **deploy** : publie, seulement si les deux jobs précédents ont réussi.

Le garde-fou bloque la publication si le dossier contient un fichier ou un nom typique des données de Caesar III, un fichier non prévu, plus de 60 Mo, ou si un fichier embarqué dans le wasm ne vient pas de `moteur/res/`. À lancer aussi en local : `bash outils/verifier-publication.sh build-pages`.

Réglages à faire une fois :

- GitHub, dépôt `DoubleV840/Brutus-III` → Settings → Pages → Source : « GitHub Actions » ;
- chez OVH, zone DNS de `ivwi.fr` : CNAME `brutus` → `doublev840.github.io.` ; puis, dans Settings → Pages, renseigner `brutus.ivwi.fr` comme domaine personnalisé et cocher « Enforce HTTPS » quand le certificat est prêt.
