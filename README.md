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

Le relais est un Cloudflare Worker + un Durable Object par salon. Il ne fait que transmettre les messages des joueurs, sans logique de jeu. Protocole : `docs/protocole.md`. Pas encore branché dans le jeu (mission M6b), et pas encore déployé (M13) : tout tourne en local, sans compte Cloudflare.

```bash
cd serveur-multi
npm ci              # installe wrangler (version exacte, package-lock.json)
npm test            # tests automatiques, sur le vrai runtime local (workerd)
npm run demo        # démo : deux joueurs créent / rejoignent un salon et échangent
npm run dev         # relais local sur ws://127.0.0.1:8787 (Ctrl+C pour arrêter)
npm run demo -- ws://127.0.0.1:8787   # la démo contre le relais lancé par npm run dev
```

`npm test` et `npm run demo` démarrent eux-mêmes un relais local sur un port libre de 127.0.0.1, puis l'arrêtent.

## Publication

La CI `.github/workflows/pages.yml` publie la version web sur GitHub Pages (https://brutus.ivwi.fr) à chaque push sur `main` (ou à la main : onglet Actions → Pages → « Run workflow ») :

1. **build** : installe Emscripten 7.0.0, restaure le cache des ports (`.emcache/`), lance `bash outils/build-web.sh`, qui prépare `build-pages/` (`index.html` = la page du jeu, `CNAME`, `.nojekyll`), puis le garde-fou `outils/verifier-publication.sh`.
2. **test** : `npm ci && npm test` dans `web/test`, sur la page publiée.
3. **deploy** : publie, seulement si les deux jobs précédents ont réussi.

Le garde-fou bloque la publication si le dossier contient un fichier ou un nom typique des données de Caesar III, un fichier non prévu, plus de 60 Mo, ou si un fichier embarqué dans le wasm ne vient pas de `moteur/res/`. À lancer aussi en local : `bash outils/verifier-publication.sh build-pages`.

Réglages à faire une fois :

- GitHub, dépôt `DoubleV840/Brutus-III` → Settings → Pages → Source : « GitHub Actions » ;
- chez OVH, zone DNS de `ivwi.fr` : CNAME `brutus` → `doublev840.github.io.` ; puis, dans Settings → Pages, renseigner `brutus.ivwi.fr` comme domaine personnalisé et cocher « Enforce HTTPS » quand le certificat est prêt.
