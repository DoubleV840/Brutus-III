#!/bin/bash
# Brutus III : test de rejouage déterministe (phase 2b).
#
#   bash outils/test-rejouage.sh [ticks] [binaire]
#
# 1. crée une sauvegarde de départ à partir d'une carte (si absente ou plus vieille que le moteur) ;
# 2. écrit la carte texte et fabrique un journal avec le bot (graine fixe) ;
# 3. rejoue DEUX fois le même journal, dans deux lancements séparés du programme, et vérifie
#    que toutes les commandes du journal ont été appliquées (ligne « # fin ») ;
# 4. compare les empreintes : identiques = OK, différentes = ÉCHEC (code 1) ;
# 5. test de rechargement (cas de la resynchronisation) : rejoue la première moitié, écrit une
#    sauvegarde, la recharge dans un 3e lancement, continue avec le même journal et compare
#    chaque empreinte de la seconde moitié à celles du rejouage continu.
#
# Tout est écrit dans build-rejouage/ (ignoré par git). Sans les fichiers du jeu
# (jeu-original/app/jeu1), le test est sauté proprement (code 0).
# Variables facultatives : BRUTUS_CARTE (nom de carte, défaut Lugdunum), BRUTUS_GRAINE (42).
set -euo pipefail

RACINE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$RACINE"

TICKS="${1:-150000}"
JEU="$RACINE/jeu-original/app/jeu1"
CARTE="${BRUTUS_CARTE:-Lugdunum}"
GRAINE="${BRUTUS_GRAINE:-42}"
TOUS_LES=500
SORTIE="$RACINE/build-rejouage"

if [ ! -f "$JEU/c3.eng" ] && [ ! -f "$JEU/C3.eng" ]; then
  echo "Test de rejouage sauté : fichiers du jeu absents ($JEU)."
  exit 0
fi
# Binaire : macOS (paquet .app) ou Linux / Windows (exécutable simple).
if [ -n "${2:-}" ]; then
  BINAIRE="$2"
elif [ -x "$RACINE/build-natif/augustus.app/Contents/MacOS/augustus" ]; then
  BINAIRE="$RACINE/build-natif/augustus.app/Contents/MacOS/augustus"
else
  BINAIRE="$RACINE/build-natif/augustus"
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

# Sans affichage ni son (le moteur l'impose aussi lui-même en mode rejouage).
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

# Vérifie la ligne « # fin » : toutes les commandes du journal appliquées, aucune perdue.
verifier_fin() {
  local fichier="$1"
  node "$RACINE/outils/verifier-fin-rejouage.mjs" "$fichier"
}

if [ ! -f "$DEPART" ] || [ "$BINAIRE" -nt "$DEPART" ]; then
  echo "== Sauvegarde de départ depuis la carte « $CARTE »"
  rm -f "$DEPART"
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
  verifier_fin "$SORTIE/empreintes-$n.txt"
done

lignes=$(grep -vc '^#' "$SORTIE/empreintes-1.txt" || true)
if cmp -s "$SORTIE/empreintes-1.txt" "$SORTIE/empreintes-2.txt"; then
  echo "OK : empreintes identiques ($lignes empreintes, une tous les $TOUS_LES ticks)."
  tail -1 "$SORTIE/empreintes-1.txt"
else
  echo "ÉCHEC : les empreintes diffèrent. Première différence :"
  diff "$SORTIE/empreintes-1.txt" "$SORTIE/empreintes-2.txt" | head -5
  exit 1
fi

# Test de rechargement : moitié du parcours (multiple de TOUS_LES pour que les empreintes tombent
# aux mêmes ticks), sauvegarde, rechargement dans un autre lancement, suite du journal.
MOITIE=$(( TICKS / 2 / TOUS_LES * TOUS_LES ))
if [ "$MOITIE" -le 0 ]; then
  echo "Test de rechargement sauté : trop peu de ticks ($TICKS)."
  exit 0
fi
echo "== Rechargement : $MOITIE ticks, sauvegarde, puis $(( TICKS - MOITIE )) ticks depuis la sauvegarde"
rm -f "$SORTIE/milieu.svx"
lancer "$SORTIE/rejouage-moitie.log" --rejouer "$DEPART" "$SORTIE/journal.txt" \
  --empreintes "$SORTIE/empreintes-moitie.txt" --ticks "$MOITIE" --tous-les "$TOUS_LES" \
  --sauvegarde-finale "$SORTIE/milieu.svx"
lancer "$SORTIE/rejouage-reprise.log" --rejouer "$SORTIE/milieu.svx" "$SORTIE/journal.txt" \
  --empreintes "$SORTIE/empreintes-reprise.txt" --ticks "$(( TICKS - MOITIE ))" --tous-les "$TOUS_LES"
verifier_fin "$SORTIE/empreintes-reprise.txt"
node "$RACINE/outils/verifier-fin-rejouage.mjs" --comparer "$SORTIE/empreintes-1.txt" "$SORTIE/empreintes-reprise.txt"
