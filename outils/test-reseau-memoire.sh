#!/bin/bash
# Brutus III : test du client réseau SANS PORT (M6b) : deux instances wasm du moteur dans Node,
# reliées par un relais en mémoire qui reprend le protocole (docs/protocole.md), avec coupures
# simulées. Aucune connexion réseau, aucun port.
#
#   bash outils/test-reseau-memoire.sh [ticks] [carte]
#
# Défauts : 225000 ticks (= 30 minutes de jeu à 200 %, 8 ms par tick), carte Lugdunum.
# Option « court » à la place des ticks : un essai de 6000 ticks (quelques secondes).
#
# 1. compile la variante wasm pour Node (build-reseau-wasm/, option CMake BRUTUS_RESEAU_NODE) ;
# 2. crée la sauvegarde de départ avec le moteur natif (build-natif) si besoin ;
# 3. lance outils/test-reseau-memoire.mjs et écrit le rapport dans build-reseau/rapport-memoire.txt.
# Sans les fichiers du jeu (jeu-original/app/jeu1), le test est sauté (code 0).
#
# Avec la variable BRUTUS_RELAIS (ex. ws://127.0.0.1:8787), le même scénario passe par un VRAI relais
# (wrangler dev) et la cadence réelle du jeu : c'est ce que lance outils/test-reseau-30min.sh.
# BRUTUS_VITESSE (500 par défaut) : vitesse du jeu dans ce mode (200 % = 8 ms par tick).
set -euo pipefail

RACINE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$RACINE"

TICKS="${1:-225000}"
if [ "$TICKS" = "court" ]; then
  TICKS=6000
fi
CARTE="${2:-Lugdunum}"
JEU="$RACINE/jeu-original/app/jeu1"
BUILD="$RACINE/build-reseau-wasm"
SORTIE="$RACINE/build-reseau"
export EM_CACHE="$RACINE/.emcache"
RELAIS="${BRUTUS_RELAIS:-}"
VITESSE="${BRUTUS_VITESSE:-500}"
RAPPORT_FICHIER="$SORTIE/rapport-memoire.txt"
EXTRA=""
if [ -n "$RELAIS" ]; then
  RAPPORT_FICHIER="$SORTIE/rapport-relais.txt"
  EXTRA="--relais $RELAIS --vitesse $VITESSE"
fi

if [ ! -f "$JEU/c3.eng" ] && [ ! -f "$JEU/C3.eng" ]; then
  echo "Test réseau sans port sauté : fichiers du jeu absents ($JEU)."
  exit 0
fi
mkdir -p "$SORTIE"

echo "== Compilation wasm pour Node (build-reseau-wasm/)"
emcmake cmake -S moteur -B "$BUILD" -DTARGET_PLATFORM=emscripten -DCMAKE_BUILD_TYPE=Release \
  -DBRUTUS_RESEAU_NODE=ON -DCMAKE_EXE_LINKER_FLAGS="" \
  -DBRUTUS_SHELL_IN="$RACINE/web/shell.html.in" > "$BUILD.log" 2>&1 || { tail -20 "$BUILD.log"; exit 1; }
cmake --build "$BUILD" -j 10 >> "$BUILD.log" 2>&1 || { grep -E "error" "$BUILD.log" | head -20; exit 1; }

# Sans affichage ni son pour le moteur natif (sauvegarde de départ, carte texte).
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy
NATIF="$RACINE/build-natif/augustus.app/Contents/MacOS/augustus"
if [ ! -x "$NATIF" ]; then
  NATIF="$RACINE/build-natif/augustus"
fi
if [ ! -x "$NATIF" ]; then
  echo "ERREUR : moteur natif introuvable. Compile d'abord : cmake -S moteur -B build-natif -DCMAKE_BUILD_TYPE=Release && cmake --build build-natif -j 10" >&2
  exit 1
fi

DEPART="$SORTIE/depart-$CARTE.svx"
if [ ! -f "$DEPART" ] || [ "$NATIF" -nt "$DEPART" ]; then
  echo "== Sauvegarde de départ ($CARTE)"
  rm -f "$DEPART"
  "$NATIF" --creer-sauvegarde "$CARTE" "$DEPART" "$JEU" > "$SORTIE/creer-$CARTE.log" 2>&1 \
    || { tail -10 "$SORTIE/creer-$CARTE.log"; exit 1; }
fi

echo "== Carte texte et journaux du bot ($TICKS ticks)"
"$NATIF" --info-carte "$DEPART" "$SORTIE/carte-$CARTE.txt" "$JEU" > "$SORTIE/info-$CARTE.log" 2>&1 \
  || { tail -10 "$SORTIE/info-$CARTE.log"; exit 1; }
node "$RACINE/outils/bot-rejouage.mjs" "$SORTIE/carte-$CARTE.txt" "$SORTIE/journal-hote.txt" --graine 42 --ticks "$TICKS"
node "$RACINE/outils/bot-rejouage.mjs" "$SORTIE/carte-$CARTE.txt" "$SORTIE/journal-invite.txt" --graine 43 --ticks "$TICKS"

echo "== Test : $TICKS ticks, carte $CARTE"
node "$RACINE/outils/test-reseau-memoire.mjs" --ticks "$TICKS" --carte "$CARTE" \
  --depart "$DEPART" --jeu "$JEU" --module "$BUILD/augustus.js" --rapport "$RAPPORT_FICHIER" \
  --journaux "$SORTIE/journal-hote.txt,$SORTIE/journal-invite.txt" $EXTRA
