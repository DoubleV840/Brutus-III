#!/bin/bash
# Brutus III (M8) : test du Versus local (deux villes sur la même carte), sans affichage.
#
#   bash outils/test-versus.sh [ticks] [carte]
#
# 1. sauvegarde de départ de la carte (comme outils/test-rejouage.sh) ;
# 2. journal Versus : « triche 103 » au tick 1 (la partie passe en Versus), puis deux bots :
#    joueur 0 (ville 1) et joueur 1 (ville 2), qui bâtissent loin l'un de l'autre ;
# 3. rejouage deux fois : empreintes identiques (déterminisme) ;
# 4. chaque ville a sa population, son trésor et ses bâtiments (les deux villes vivent) ;
# 5. sauvegarde au milieu (Versus compris), rechargement par un lancement neuf, suite identique.
# Sorties : 0 = OK, 1 = échec, 3 = test sauté (jeu absent).
set -euo pipefail

RACINE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$RACINE"
TICKS="${1:-60000}"
CARTE="${2:-Lugdunum}"
TOUS_LES=500
JEU="$RACINE/jeu-original/app/jeu1"
SORTIE="$RACINE/build-rejouage/versus-$CARTE"
BINAIRE="$RACINE/build-natif/augustus.app/Contents/MacOS/augustus"
[ -x "$BINAIRE" ] || BINAIRE="$RACINE/build-natif/augustus"
VERIFIER="$RACINE/outils/verifier-fin-rejouage.mjs"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy

if [ ! -f "$JEU/c3.eng" ] && [ ! -f "$JEU/C3.eng" ]; then
  echo "Test Versus SAUTÉ : fichiers du jeu absents ($JEU)."
  exit 3
fi
if [ ! -x "$BINAIRE" ]; then
  echo "ERREUR : moteur natif introuvable (cmake --build build-natif)." >&2
  exit 1
fi
mkdir -p "$SORTIE"

lancer() {
  local log="$1"
  shift
  if ! "$BINAIRE" "$@" "$JEU" > "$log" 2>&1; then
    echo "ERREUR : le moteur a échoué ($*). Fin du journal ($log) :" >&2
    tail -20 "$log" >&2
    exit 1
  fi
  grep "Brutus III" "$log" | grep -v "^INFO" | sed 's/^/   /' || true
}

DEPART="$SORTIE/depart.svx"
echo "== Sauvegarde de départ : $CARTE"
lancer "$SORTIE/creer.log" --creer-sauvegarde "$CARTE" "$DEPART"
lancer "$SORTIE/info.log" --info-carte "$DEPART" "$SORTIE/carte.txt"

echo "== Journal Versus : triche 103, puis un bot par ville"
node outils/bot-rejouage.mjs "$SORTIE/carte.txt" "$SORTIE/bot-0.txt" --graine 42 --ticks "$TICKS" --joueur 0
CENTRE0=$(sed -n 's/.*centre \([0-9]*\) \([0-9]*\).*/\1,\2/p' "$SORTIE/bot-0.txt")
node outils/bot-rejouage.mjs "$SORTIE/carte.txt" "$SORTIE/bot-1.txt" --graine 7 --ticks "$TICKS" --joueur 1 --eviter "$CENTRE0"
CENTRE1=$(sed -n 's/.*centre \([0-9]*\) \([0-9]*\).*/\1,\2/p' "$SORTIE/bot-1.txt")
echo "   ville 1 autour de $CENTRE0, ville 2 autour de $CENTRE1"
{
  grep '^#' "$SORTIE/bot-0.txt"
  echo "1 0 triche 103 0 0 0 0"
  # Les commandes du bot qui tombent avant le tick 2 attendent un peu (la partie doit être en Versus).
  # Tick 100 : César demande 100 deniers à la ville 1 seulement (triche 104 du joueur 0).
  { echo "100 0 triche 104 0 0 0 0"; grep -hv '^#' "$SORTIE/bot-0.txt" "$SORTIE/bot-1.txt"; } |
    awk '{ if ($1 < 2) $1 = 2; print }' | sort -s -n -k1,1
} > "$SORTIE/journal.txt"
echo "   $(grep -vc '^#' "$SORTIE/journal.txt") commandes"

for n in 1 2; do
  echo "== Rejouage $n"
  lancer "$SORTIE/rejouage-$n.log" --rejouer "$DEPART" "$SORTIE/journal.txt" \
    --empreintes "$SORTIE/empreintes-$n.txt" --ticks "$TICKS" --tous-les "$TOUS_LES"
done
if ! cmp -s "$SORTIE/empreintes-1.txt" "$SORTIE/empreintes-2.txt"; then
  echo "ÉCHEC : les deux rejouages Versus diffèrent (non déterministe)."
  diff "$SORTIE/empreintes-1.txt" "$SORTIE/empreintes-2.txt" | head -5
  exit 1
fi
echo "OK : empreintes identiques ($(grep -vc '^#' "$SORTIE/empreintes-1.txt") empreintes)."

# Les deux villes vivent : chacune a des bâtiments et des habitants à elle.
for v in 1 2; do
  ligne=$(grep "Brutus III : ville $v :" "$SORTIE/rejouage-1.log" || true)
  if [ -z "$ligne" ]; then
    echo "ÉCHEC : la partie n'est pas passée en Versus (pas de ligne pour la ville $v)."
    exit 1
  fi
  bat=$(echo "$ligne" | sed -n 's/.*, \([0-9]*\) bâtiments.*/\1/p')
  pop=$(echo "$ligne" | sed -n 's/.*population \([0-9-]*\),.*/\1/p')
  if [ "${bat:-0}" -le 0 ] || [ "${pop:-0}" -le 0 ]; then
    echo "ÉCHEC : la ville $v n'a pas de bâtiments ou pas d'habitants ($ligne)."
    exit 1
  fi
done
echo "OK : les deux villes ont leurs bâtiments, leurs habitants et leur trésor."

# Demandes de César par ville : celle de la triche 104 (joueur 0) ne concerne que la ville 1.
d1=$(grep "Brutus III : ville 1 :" "$SORTIE/rejouage-1.log" | sed -n 's/.*César : \([0-9]*\) en cours.*/\1/p')
d2=$(grep "Brutus III : ville 2 :" "$SORTIE/rejouage-1.log" | sed -n 's/.*César : \([0-9]*\) en cours.*/\1/p')
if [ "${d1:-0}" -lt 1 ] || [ "${d2:-0}" -ne 0 ]; then
  echo "ÉCHEC : demandes de César mélangées (ville 1 : ${d1:-?} en cours, ville 2 : ${d2:-?})."
  exit 1
fi
echo "OK : la demande de César faite à la ville 1 ne concerne pas la ville 2."

P=$(( TICKS / 2 / TOUS_LES * TOUS_LES ))
echo "== Sauvegarde au tick $P (Versus compris), puis rechargement par un lancement neuf"
rm -f "$SORTIE"/empreintes-hote.txt.resync-*.svx
lancer "$SORTIE/rejouage-hote.log" --rejouer "$DEPART" "$SORTIE/journal.txt" \
  --empreintes "$SORTIE/empreintes-hote.txt" --ticks "$TICKS" --tous-les "$TOUS_LES" --resync-au-tick "$P"
SAUVEGARDE="$SORTIE/empreintes-hote.txt.resync-$P.svx"
[ -f "$SAUVEGARDE" ] || { echo "ÉCHEC : sauvegarde absente : $SAUVEGARDE"; exit 1; }
lancer "$SORTIE/rejouage-invite.log" --rejouer "$SAUVEGARDE" "$SORTIE/journal.txt" \
  --empreintes "$SORTIE/empreintes-invite.txt" --ticks "$(( TICKS - P ))" --tous-les "$TOUS_LES"
node "$VERIFIER" --comparer "$SORTIE/empreintes-hote.txt" "$SORTIE/empreintes-invite.txt" "Versus rechargé au tick $P"
echo "######## Versus local : OK ($CARTE, $TICKS ticks)"
