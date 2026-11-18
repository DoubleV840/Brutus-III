#!/bin/bash
# Brutus III : rejouage déterministe en WebAssembly, sous Node (phase 2b).
#
#   bash outils/test-rejouage-wasm.sh [ticks]
#
# 1. compile une variante wasm du moteur pour Node (build-rejouage-wasm/, option CMake
#    BRUTUS_REJOUAGE_NODE ; la page publiée n'est pas touchée) ;
# 2. rejoue DEUX fois le journal de outils/test-rejouage.sh sous Node et compare (wasm ↔ wasm) ;
# 3. compare aussi avec les empreintes natives (natif ↔ wasm), si elles existent.
#
# À lancer après bash outils/test-rejouage.sh (même nombre de ticks), qui crée
# build-rejouage/depart-*.svx et build-rejouage/journal.txt.
# Sans les fichiers du jeu (jeu-original/app/jeu1), le test est sauté (code 0).
set -euo pipefail

RACINE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$RACINE"

TICKS="${1:-150000}"
JEU="$RACINE/jeu-original/app/jeu1"
CARTE="${BRUTUS_CARTE:-Lugdunum}"
TOUS_LES=500
SORTIE="$RACINE/build-rejouage"
BUILD="$RACINE/build-rejouage-wasm"
export EM_CACHE="$RACINE/.emcache"

if [ ! -f "$JEU/c3.eng" ] && [ ! -f "$JEU/C3.eng" ]; then
  echo "Test de rejouage wasm sauté : fichiers du jeu absents ($JEU)."
  exit 0
fi
DEPART="$SORTIE/depart-$CARTE.svx"
if [ ! -f "$DEPART" ] || [ ! -f "$SORTIE/journal.txt" ]; then
  echo "ERREUR : lance d'abord bash outils/test-rejouage.sh $TICKS" >&2
  exit 1
fi

echo "== Compilation wasm pour Node (build-rejouage-wasm/)"
emcmake cmake -S moteur -B "$BUILD" -DTARGET_PLATFORM=emscripten -DCMAKE_BUILD_TYPE=Release \
  -DBRUTUS_REJOUAGE_NODE=ON -DCMAKE_EXE_LINKER_FLAGS="" \
  -DBRUTUS_SHELL_IN="$RACINE/web/shell.html.in" > "$BUILD.log" 2>&1 || { tail -20 "$BUILD.log"; exit 1; }
cmake --build "$BUILD" -j 10 >> "$BUILD.log" 2>&1 || { grep -E "error" "$BUILD.log" | head -20; exit 1; }
JS="$BUILD/augustus.js"

# Sous Node, les fichiers de l'ordinateur sont vus sous /hote.
for n in 1 2; do
  echo "== Rejouage wasm $n"
  debut=$(date +%s)
  if ! node "$JS" --rejouer "/hote$DEPART" "/hote$SORTIE/journal.txt" \
      --empreintes "/hote$SORTIE/empreintes-wasm-$n.txt" --ticks "$TICKS" --tous-les "$TOUS_LES" \
      "/hote$JEU" > "$SORTIE/rejouage-wasm-$n.log" 2>&1; then
    echo "ERREUR : le rejouage wasm a échoué. Fin du journal :" >&2
    tail -20 "$SORTIE/rejouage-wasm-$n.log" >&2
    exit 1
  fi
  grep "Brutus III" "$SORTIE/rejouage-wasm-$n.log" || true
  echo "   durée : $(( $(date +%s) - debut )) s"
done

code=0
if cmp -s "$SORTIE/empreintes-wasm-1.txt" "$SORTIE/empreintes-wasm-2.txt"; then
  echo "OK : wasm ↔ wasm identiques ($(grep -vc '^#' "$SORTIE/empreintes-wasm-1.txt") empreintes)."
else
  echo "ÉCHEC : wasm ↔ wasm différents :"
  diff "$SORTIE/empreintes-wasm-1.txt" "$SORTIE/empreintes-wasm-2.txt" | head -5
  code=1
fi
if [ -f "$SORTIE/empreintes-1.txt" ]; then
  if cmp -s "$SORTIE/empreintes-1.txt" "$SORTIE/empreintes-wasm-1.txt"; then
    echo "OK : natif ↔ wasm identiques."
  else
    echo "DIFFÉRENT : natif ↔ wasm. Première différence :"
    diff "$SORTIE/empreintes-1.txt" "$SORTIE/empreintes-wasm-1.txt" | head -5
    code=1
  fi
fi
exit "$code"
