#!/bin/bash
# Brutus III : test de rejouage déterministe (phase 2b).
#
#   bash outils/test-rejouage.sh [ticks] [binaire]
#
# 1. crée une sauvegarde de départ à partir d'une carte (si absente) ;
# 2. écrit la carte texte et fabrique un journal avec le bot (graine fixe) ;
# 3. rejoue DEUX fois le même journal, dans deux lancements séparés du programme ;
# 4. compare les empreintes : identiques = OK (code 0), différentes = ÉCHEC (code 1).
#
# Tout est écrit dans build-rejouage/ (ignoré par git). Sans les fichiers du jeu
# (jeu-original/app/jeu1), le test est sauté proprement (code 0).
# Variables facultatives : BRUTUS_CARTE (nom de carte, défaut Lugdunum), BRUTUS_GRAINE (42).
set -euo pipefail

RACINE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$RACINE"

TICKS="${1:-150000}"
BINAIRE="${2:-$RACINE/build-natif/augustus.app/Contents/MacOS/augustus}"
JEU="$RACINE/jeu-original/app/jeu1"
CARTE="${BRUTUS_CARTE:-Lugdunum}"
GRAINE="${BRUTUS_GRAINE:-42}"
TOUS_LES=500
SORTIE="$RACINE/build-rejouage"

if [ ! -f "$JEU/c3.eng" ] && [ ! -f "$JEU/C3.eng" ]; then
  echo "Test de rejouage sauté : fichiers du jeu absents ($JEU)."
  exit 0
fi
if [ ! -x "$BINAIRE" ]; then
  echo "ERREUR : binaire introuvable : $BINAIRE" >&2
  echo "Compile d'abord : cmake -S moteur -B build-natif -DCMAKE_BUILD_TYPE=Release && cmake --build build-natif -j 10" >&2
  exit 1
fi
if ! command -v node >/dev/null 2>&1; then
  echo "ERREUR : node introuvable (nécessaire pour le bot)." >&2
  exit 1
fi

# Sans affichage ni son : le script l'impose lui-même.
export SDL_VIDEODRIVER=dummy
export SDL_AUDIODRIVER=dummy

mkdir -p "$SORTIE"
DEPART="$SORTIE/depart-$CARTE.svx"

lancer() {
  # Le moteur est bavard : son journal va dans un fichier, on n'affiche que ses lignes « Brutus III ».
  local log="$1"
  shift
  if ! "$BINAIRE" "$@" "$JEU" > "$log" 2>&1; then
    echo "ERREUR : le moteur a échoué ($*). Fin du journal ($log) :" >&2
    tail -20 "$log" >&2
    exit 1
  fi
  grep "Brutus III" "$log" || true
}

if [ ! -f "$DEPART" ]; then
  echo "== Sauvegarde de départ depuis la carte « $CARTE »"
  lancer "$SORTIE/creer.log" --creer-sauvegarde "$CARTE" "$DEPART"
fi

echo "== Carte texte et journal du bot (graine $GRAINE, $TICKS ticks)"
lancer "$SORTIE/info.log" --info-carte "$DEPART" "$SORTIE/carte.txt"
node outils/bot-rejouage.mjs "$SORTIE/carte.txt" "$SORTIE/journal.txt" --graine "$GRAINE" --ticks "$TICKS"

for n in 1 2; do
  echo "== Rejouage $n"
  debut=$(date +%s)
  lancer "$SORTIE/rejouage-$n.log" --rejouer "$DEPART" "$SORTIE/journal.txt" \
    --empreintes "$SORTIE/empreintes-$n.txt" --ticks "$TICKS" --tous-les "$TOUS_LES"
  echo "   durée : $(( $(date +%s) - debut )) s"
done

lignes=$(grep -vc '^#' "$SORTIE/empreintes-1.txt" || true)
if cmp -s "$SORTIE/empreintes-1.txt" "$SORTIE/empreintes-2.txt"; then
  echo "OK : empreintes identiques ($lignes empreintes, une tous les $TOUS_LES ticks)."
  tail -1 "$SORTIE/empreintes-1.txt"
  exit 0
fi
echo "ÉCHEC : les empreintes diffèrent. Première différence :"
diff "$SORTIE/empreintes-1.txt" "$SORTIE/empreintes-2.txt" | head -5
exit 1
