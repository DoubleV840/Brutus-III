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

Le relais est un Cloudflare Worker + un Durable Object par salon. Il ne fait que transmettre les messages des joueurs, sans logique de jeu. Protocole : `docs/protocole.md`. Il est branché dans la version web (mission M6b : client `web/lockstep.js`, panneau « Jouer à deux »), mais pas encore déployé (M13) : tout tourne en local, sans compte Cloudflare.

```bash
cd serveur-multi
npm ci              # installe wrangler (version exacte, package-lock.json)
npm test            # tests automatiques, sur le vrai runtime local (workerd)
npm run demo        # démo : deux joueurs créent / rejoignent un salon et échangent
npm run dev         # relais local sur ws://127.0.0.1:8787 (Ctrl+C pour arrêter)
npm run demo -- ws://127.0.0.1:8787   # la démo contre le relais lancé par npm run dev
```

`npm test` et `npm run demo` démarrent eux-mêmes un relais local sur un port libre de 127.0.0.1, puis l'arrêtent. S'il est impossible d'ouvrir un port local (bac à sable), ils s'arrêtent au bout de 30 s avec un message : seul `npm run test:sans-port` (réassemblage des sauvegardes) tourne alors. Le job CI `relais` lance `npm test`, mais le déploiement du site ne dépend pas de lui.

## Jouer à deux en local

Trois terminaux (le relais, la page, puis le navigateur) :

```bash
bash outils/build-web.sh                                  # une fois (et après chaque changement du moteur ou de web/)
cd serveur-multi && npm ci && npm run dev                 # terminal 1 : le relais, ws://127.0.0.1:8787
cd build-web && python3 -m http.server 8000               # terminal 2 : la page
```

Puis, dans **deux fenêtres de Chrome placées côte à côte** (un onglet masqué est ralenti par le navigateur, l'autre joueur l'attend) : ouvrir `http://localhost:8000/augustus.html` dans chacune, choisir le dossier du jeu dans chacune. L'hôte lance ou charge une partie (la ville doit être à l'écran) ; l'invité peut rester au menu principal : il reçoit la partie de l'hôte (cas à confirmer à la main).

1. Fenêtre 1 (hôte), la ville à l'écran : bouton **« Jouer à deux »** en bas à gauche → **« Créer une partie »** : un code de 6 caractères s'affiche.
2. Fenêtre 2 : **« Jouer à deux »** → saisir le code → **« Rejoindre »**. L'hôte envoie sa partie, les deux la rechargent (« resynchronisation… »), puis le jeu reprend pour les deux.
3. Vitesse et pause sont partagées. Si l'autre joueur se déconnecte, le jeu se met en pause (« en attente de … ») et reprend à son retour. L'hôte peut recharger sa page : la partie est récupérée auprès de l'invité.
4. « Quitter la partie à deux » revient au solo.
5. En partie à deux, **« Charger », « Recommencer » et « Quitter la mission » sont désactivés** (ils changeraient la ville d'un seul joueur : divergence). Si tu quittes quand même la ville par le menu principal, la partie à deux se met en attente : le bouton **« Revenir dans la partie »** te ramène dans la partie en cours (l'hôte la récupère auprès de l'invité). Si l'hôte reste bloqué plus de 60 s à récupérer sa partie, il est ramené en solo avec un message.

Le panneau n'apparaît que si la page est servie depuis `localhost` ou `127.0.0.1` (le site public reste inchangé jusqu'à la mise en ligne du relais, M13). Pas de chat ni de curseur de l'autre joueur (phase 3). **La page n'a pas encore été essayée dans un navigateur** : si quelque chose cloche, noter le message affiché dans le panneau et la console du navigateur.

Tests du client réseau, sans navigateur :

```bash
bash outils/test-reseau-memoire.sh court   # ~30 s : deux moteurs wasm dans Node, relais en mémoire, aucun port
bash outils/test-reseau-memoire.sh         # 225 000 ticks (30 minutes de jeu à 200 %), ~1 min ; rapport : build-reseau/rapport-memoire.txt
bash outils/test-reseau-30min.sh           # même scénario avec le VRAI relais en local (wrangler dev) et la cadence réelle du jeu, ~10 min ; rapport : build-reseau/rapport-relais.txt
BRUTUS_VITESSE=200 bash outils/test-reseau-30min.sh 225000 Lugdunum --production   # constantes de production (ping 20 s, coupure 45 s…), 20 min réelles ou plus
```

Le premier lance une compilation wasm pour Node (`build-reseau-wasm/`, quelques minutes la première fois). Le dernier ouvre un port local : à lancer dans ton terminal, pas dans un bac à sable. `--production` garde les délais réels du client (une empreinte tous les 50 groupes, ping 20 s, coupure 45 s) ; rapport dans `build-reseau/rapport-relais-production.txt`. **Code de sortie 3 = test non exécuté** (jeu absent) : ce n'est pas un succès. Un test réussi affiche « RÉSULTAT OK » et sort avec 0. Le scénario (coupures, resynchronisations, panne injectée, rechargement de l'hôte) est décrit dans `docs/determinisme.md`.

## Publication

La CI `.github/workflows/pages.yml` publie la version web sur GitHub Pages (https://brutus.ivwi.fr) à chaque push sur `main` (ou à la main : onglet Actions → Pages → « Run workflow ») :

1. **build** : installe Emscripten 7.0.0, restaure le cache des ports (`.emcache/`), lance `bash outils/build-web.sh`, qui prépare `build-pages/` (`index.html` = la page du jeu, `CNAME`, `.nojekyll`), puis le garde-fou `outils/verifier-publication.sh`.
2. **test** : `npm ci && npm test` dans `web/test`, sur la page publiée.
3. **deploy** : publie, seulement si les deux jobs précédents ont réussi.

Le garde-fou bloque la publication si le dossier contient un fichier ou un nom typique des données de Caesar III, un fichier non prévu, plus de 60 Mo, ou si un fichier embarqué dans le wasm ne vient pas de `moteur/res/`. À lancer aussi en local : `bash outils/verifier-publication.sh build-pages`.

Réglages à faire une fois :

- GitHub, dépôt `DoubleV840/Brutus-III` → Settings → Pages → Source : « GitHub Actions » ;
- chez OVH, zone DNS de `ivwi.fr` : CNAME `brutus` → `doublev840.github.io.` ; puis, dans Settings → Pages, renseigner `brutus.ivwi.fr` comme domaine personnalisé et cocher « Enforce HTTPS » quand le certificat est prêt.
