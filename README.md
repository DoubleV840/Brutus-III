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

Sortie : `build-web/augustus.html`. Le cache Emscripten est dans `.emcache/` (ignoré par git) ; la 1re fois, les ports (SDL2, SDL2_mixer, ogg, vorbis, mpg123) se téléchargent depuis GitHub, ensuite plus besoin de réseau. Aucun fichier de `jeu-original/` n'est intégré (le script le vérifie). La page jouable arrive en phase 1c.
