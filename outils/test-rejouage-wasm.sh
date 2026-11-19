#!/bin/bash
# Brutus III : rejouage déterministe en WebAssembly, sous Node (phases 2b et M6a).
#
#   bash outils/test-rejouage-wasm.sh [ticks]
#
# 1. compile une variante wasm du moteur pour Node (build-rejouage-wasm/, option CMake
#    BRUTUS_REJOUAGE_NODE ; la page publiée n'est pas touchée) ;
# 2. pour chaque carte de BRUTUS_CARTES (défaut : Lugdunum Tarraco Carthago) : rejoue DEUX fois
#    le journal de outils/test-rejouage.sh sous Node et compare (wasm ↔ wasm) ;
# 3. rejoue en multijoueur avec l'interface scriptée (M6a) : mêmes empreintes ;
# 4. resynchronisation : invité wasm chargé sur la sauvegarde de l'hôte natif au tick 1/3 ;
# 5. compare aussi avec les empreintes natives (natif ↔ wasm).
#
# À lancer après bash outils/test-rejouage.sh (même nombre de ticks, mêmes cartes), qui crée
# build-rejouage/<carte>/depart.svx, journal.txt et les empreintes natives.
# Sans les fichiers du jeu (jeu-original/app/jeu1), le test est sauté (code 0).
set -euo pipefail

RACINE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$RACINE"

TICKS="${1:-150000}"
JEU="$RACINE/jeu-original/app/jeu1"
CARTES="${BRUTUS_CARTES:-${BRUTUS_CARTE:-Lugdunum Tarraco Carthago}}"
TOUS_LES=500
BUILD="$RACINE/build-rejouage-wasm"
export EM_CACHE="$RACINE/.emcache"
VERIFIER="$RACINE/outils/verifier-fin-rejouage.mjs"

if [ ! -f "$JEU/c3.eng" ] && [ ! -f "$JEU/C3.eng" ]; then
  echo "Test de rejouage wasm sauté : fichiers du jeu absents ($JEU)."
  exit 0
fi
for CARTE in $CARTES; do
  if [ ! -f "$RACINE/build-rejouage/$CARTE/depart.svx" ] || [ ! -f "$RACINE/build-rejouage/$CARTE/journal.txt" ]; then
    echo "ERREUR : lance d'abord bash outils/test-rejouage.sh $TICKS (carte $CARTE absente)" >&2
    exit 1
  fi
done

echo "== Compilation wasm pour Node (build-rejouage-wasm/)"
emcmake cmake -S moteur -B "$BUILD" -DTARGET_PLATFORM=emscripten -DCMAKE_BUILD_TYPE=Release \
  -DBRUTUS_REJOUAGE_NODE=ON -DCMAKE_EXE_LINKER_FLAGS="" \
  -DBRUTUS_SHELL_IN="$RACINE/web/shell.html.in" > "$BUILD.log" 2>&1 || { tail -20 "$BUILD.log"; exit 1; }
cmake --build "$BUILD" -j 10 >> "$BUILD.log" 2>&1 || { grep -E "error" "$BUILD.log" | head -20; exit 1; }
JS="$BUILD/augustus.js"

# Lance le moteur wasm sous Node ; les chemins de l'hôte sont montés sous /hote.
lancer_wasm() {
  local log="$1"
  shift
  if ! node "$JS" "$@" "/hote$JEU" > "$log" 2>&1; then
    echo "ERREUR : le rejouage wasm a échoué. Fin du journal ($log) :" >&2
    tail -20 "$log" >&2
    exit 1
  fi
}

code=0
P1=$(( TICKS / 3 / TOUS_LES * TOUS_LES ))
P2=$(( 2 * TICKS / 3 / TOUS_LES * TOUS_LES ))
for CARTE in $CARTES; do
  SORTIE="$RACINE/build-rejouage/$CARTE"
  DEPART="$SORTIE/depart.svx"
  echo "######## Carte « $CARTE » (wasm)"
  for n in 1 2; do
    echo "== Rejouage wasm $n"
    debut=$(date +%s)
    lancer_wasm "$SORTIE/rejouage-wasm-$n.log" --rejouer "/hote$DEPART" "/hote$SORTIE/journal.txt" \
      --empreintes "/hote$SORTIE/empreintes-wasm-$n.txt" --ticks "$TICKS" --tous-les "$TOUS_LES"
    grep "Brutus III" "$SORTIE/rejouage-wasm-$n.log" || true
    echo "   durée : $(( $(date +%s) - debut )) s"
    node "$VERIFIER" "$SORTIE/empreintes-wasm-$n.txt"
  done
  if cmp -s "$SORTIE/empreintes-wasm-1.txt" "$SORTIE/empreintes-wasm-2.txt"; then
    echo "OK : wasm ↔ wasm identiques ($(grep -vc '^#' "$SORTIE/empreintes-wasm-1.txt") empreintes)."
  else
    echo "ÉCHEC : wasm ↔ wasm différents :"
    diff "$SORTIE/empreintes-wasm-1.txt" "$SORTIE/empreintes-wasm-2.txt" | head -5
    code=1
  fi

  echo "== Rejouage wasm, interface ouverte en multijoueur"
  debut=$(date +%s)
  lancer_wasm "$SORTIE/rejouage-wasm-interface.log" --rejouer "/hote$DEPART" "/hote$SORTIE/journal.txt" \
    --empreintes "/hote$SORTIE/empreintes-wasm-interface.txt" --ticks "$TICKS" --tous-les "$TOUS_LES" \
    --interface-test
  echo "   durée : $(( $(date +%s) - debut )) s ; $(grep '^# interface' "$SORTIE/empreintes-wasm-interface.txt" | sed 's/^# //')"
  node "$VERIFIER" "$SORTIE/empreintes-wasm-interface.txt"
  if ! node "$VERIFIER" --comparer "$SORTIE/empreintes-wasm-1.txt" "$SORTIE/empreintes-wasm-interface.txt" \
      "interface ouverte (wasm)"; then
    code=1
  fi

  # Invité wasm sur la sauvegarde écrite par l'hôte natif (test-rejouage.sh)
  SAUVEGARDE="$SORTIE/empreintes-hote.txt.resync-$P1.svx"
  if [ -f "$SAUVEGARDE" ] && [ -f "$SORTIE/empreintes-hote.txt" ]; then
    echo "== Invité wasm chargé au tick $P1 sur la sauvegarde de l'hôte natif"
    lancer_wasm "$SORTIE/rejouage-wasm-invite.log" --rejouer "/hote$SAUVEGARDE" "/hote$SORTIE/journal.txt" \
      --empreintes "/hote$SORTIE/empreintes-wasm-invite.txt" --ticks "$(( TICKS - P1 ))" --tous-les "$TOUS_LES" \
      --resync-au-tick "$P2"
    node "$VERIFIER" "$SORTIE/empreintes-wasm-invite.txt"
    if ! node "$VERIFIER" --comparer "$SORTIE/empreintes-hote.txt" "$SORTIE/empreintes-wasm-invite.txt" \
        "invité wasm ↔ hôte natif"; then
      code=1
    fi
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
done
exit "$code"
