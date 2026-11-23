#!/bin/bash
# Brutus III (M10) : test du combat entre les villes du Versus, sans affichage.
#
#   bash outils/test-combat.sh [ticks]
#
# Sur « Brutus - Deux rives » (ville 1 en bas à gauche, ville 2 en haut à droite, un gué au centre) :
# 1. journal écrit à la main : passage en Versus, un fort de légionnaires pour la ville 1, un fort de
#    javeliniers pour la ville 2 (et un mess chacune), des puits et un mur de la ville 2, soldats tout de suite (triche 106) ;
#    les deux légions vont au gué et se battent ; puis la ville 1 refait sa légion et l'envoie contre les
#    puits et le mur de la ville 2 ; le joueur 2 essaie aussi de commander la légion de la ville 1 (refusé) ;
# 2. rejoué deux fois : empreintes identiques ;
# 3. combat activé : chaque ville a des soldats tués par l'autre, et des bâtiments de la ville 2 sont détruits ;
# 4. même journal, combat désactivé (triche 105 0) : aucun soldat tué ni bâtiment détruit par l'autre ville
#    (quelques soldats peuvent déserter : légion loin du fort, sans nourriture, comme dans Augustus) ;
# 5. sauvegarde au milieu de la bataille, rechargement par un lancement neuf, suite identique.
# Sorties : 0 = OK, 1 = échec, 3 = test sauté (jeu absent).
set -euo pipefail

RACINE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$RACINE"
TICKS="${1:-12000}"
TOUS_LES=250
CARTE="Brutus - Deux rives"
JEU="$RACINE/jeu-original/app/jeu1"
SORTIE="$RACINE/build-rejouage/combat"
BINAIRE="$RACINE/build-natif/augustus.app/Contents/MacOS/augustus"
[ -x "$BINAIRE" ] || BINAIRE="$RACINE/build-natif/augustus"
VERIFIER="$RACINE/outils/verifier-fin-rejouage.mjs"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy

if [ ! -f "$JEU/c3.eng" ] && [ ! -f "$JEU/C3.eng" ]; then
  echo "Test combat SAUTÉ : fichiers du jeu absents ($JEU)."
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

# Formations : la première légion créée est la 1, la seconde la 2 (la carte n'a pas de troupeaux).
# construire : type x0 y0 x1 y1 rotation rotation_extra orientation_route orientation_vue
journal() {
  local combat="$1"
  echo "# brutus-journal 1"
  echo "1 0 triche 103 0 0 0 0"
  [ "$combat" = 1 ] || echo "2 0 triche 105 0 0 0 0"
  echo "3 0 construire 154 16 90 16 90 0 0 1 0"          # un mess par ville (il en faut un pour un fort)
  echo "3 1 construire 154 86 14 86 14 0 0 1 0"
  echo "4 0 construire 40 12 84 12 84 0 0 1 0"           # ville 1 : fort de légionnaires
  echo "4 1 construire 44 80 10 80 10 0 0 1 0"           # ville 2 : fort de javeliniers
  for x in 72 74 76 78; do
    echo "5 1 construire 92 $x 20 $x 20 0 0 1 0"         # ville 2 : quatre puits
  done
  echo "5 1 construire 6 73 24 80 24 0 0 1 0"            # ville 2 : un mur
  echo "10 0 triche 106 0 0 0 0"                         # soldats tout de suite
  echo "10 1 triche 106 0 0 0 0"
  echo "200 0 legion-deplacer 1 48 50"                   # les deux légions vont au gué
  echo "200 1 legion-deplacer 2 52 48"
  echo "300 1 legion-deplacer 1 0 99"                    # refusé : ce n'est pas sa légion
  echo "5000 1 legion-rentrer 2"                         # la ville 2 rentre ce qui reste
  echo "5000 0 legion-rentrer 1"
  echo "5600 0 triche 106 0 0 0 0"                       # la ville 1 refait sa légion
  echo "5700 0 legion-deplacer 1 75 22"                  # contre le mur de la ville 2
  echo "8500 0 legion-deplacer 1 75 18"                  # puis contre ses puits
}
journal 1 > "$SORTIE/journal.txt"
journal 0 > "$SORTIE/journal-sans.txt"

for n in 1 2; do
  echo "== Rejouage $n (combat activé)"
  lancer "$SORTIE/rejouage-$n.log" --rejouer "$DEPART" "$SORTIE/journal.txt" \
    --empreintes "$SORTIE/empreintes-$n.txt" --ticks "$TICKS" --tous-les "$TOUS_LES"
done
if ! cmp -s "$SORTIE/empreintes-1.txt" "$SORTIE/empreintes-2.txt"; then
  echo "ÉCHEC : les deux rejouages du combat diffèrent (non déterministe)."
  diff "$SORTIE/empreintes-1.txt" "$SORTIE/empreintes-2.txt" | head -5
  exit 1
fi
echo "OK : empreintes identiques ($(grep -vc '^#' "$SORTIE/empreintes-1.txt") empreintes)."

echo "== Rejouage combat désactivé"
lancer "$SORTIE/rejouage-sans.log" --rejouer "$DEPART" "$SORTIE/journal-sans.txt" \
  --empreintes "$SORTIE/empreintes-sans.txt" --ticks "$TICKS" --tous-les "$TOUS_LES"

# « Brutus III : combat ville 1 : activé, crees 23 vivants 16 tues 7 detruits 0 »
valeur() { # fichier ville champ
  grep "Brutus III : combat ville $2 :" "$1" | sed -n "s/.* $3 \([0-9]*\).*/\1/p"
}
echec=0
for v in 1 2; do
  tues=$(valeur "$SORTIE/rejouage-1.log" $v tues)
  if [ "${tues:-0}" -le 0 ]; then
    echo "ÉCHEC : combat activé, aucun soldat de la ville $v n'a été tué par l'autre ville."
    echec=1
  fi
  tues=$(valeur "$SORTIE/rejouage-sans.log" $v tues)
  if [ "${tues:-1}" -ne 0 ]; then
    echo "ÉCHEC : combat désactivé, des soldats de la ville $v ont été tués par l'autre ville (${tues:-?})."
    echec=1
  fi
done
tues1=$(valeur "$SORTIE/rejouage-1.log" 1 tues)
tues2=$(valeur "$SORTIE/rejouage-1.log" 2 tues)
detruits=$(valeur "$SORTIE/rejouage-1.log" 2 detruits)
detruits_sans=$(valeur "$SORTIE/rejouage-sans.log" 2 detruits)
if [ "${detruits:-0}" -le 8 ]; then  # le mur (8 cases) puis au moins un puits
  echo "ÉCHEC : combat activé, le mur et les puits de la ville 2 auraient dû tomber (${detruits:-0} détruits)."
  echec=1
fi
if [ "${detruits_sans:-1}" -ne 0 ]; then
  echo "ÉCHEC : combat désactivé, des bâtiments de la ville 2 ont été détruits (${detruits_sans:-?})."
  echec=1
fi
refus=$(sed -n 's/^# bilan legion-deplacer appliquees \([0-9]*\) refusees \([0-9]*\)$/\1 \2/p' "$SORTIE/empreintes-1.txt")
if [ "$refus" != "5 1" ]; then  # « appliquees » compte toutes les commandes du type, refusées comprises
  echo "ÉCHEC : legion-deplacer : 5 commandes dont 1 refusée attendues (lu : ${refus:-rien})."
  echec=1
fi
[ "$echec" = 0 ] || exit 1
echo "OK : combat activé, $tues1 soldats de la ville 1 et $tues2 de la ville 2 tués par l'autre ville,"
echo "     $detruits bâtiments de la ville 2 détruits ; combat désactivé, ni mort ni destruction entre villes ;"
echo "     le joueur 2 ne commande pas la légion de la ville 1."

P=$(( 1000 / TOUS_LES * TOUS_LES ))
echo "== Sauvegarde au tick $P (en pleine bataille), puis rechargement par un lancement neuf"
rm -f "$SORTIE"/empreintes-hote.txt.resync-*.svx
lancer "$SORTIE/rejouage-hote.log" --rejouer "$DEPART" "$SORTIE/journal.txt" \
  --empreintes "$SORTIE/empreintes-hote.txt" --ticks "$TICKS" --tous-les "$TOUS_LES" --resync-au-tick "$P"
SAUVEGARDE="$SORTIE/empreintes-hote.txt.resync-$P.svx"
[ -f "$SAUVEGARDE" ] || { echo "ÉCHEC : sauvegarde absente : $SAUVEGARDE"; exit 1; }
lancer "$SORTIE/rejouage-invite.log" --rejouer "$SAUVEGARDE" "$SORTIE/journal.txt" \
  --empreintes "$SORTIE/empreintes-invite.txt" --ticks "$(( TICKS - P ))" --tous-les "$TOUS_LES"
node "$VERIFIER" --comparer "$SORTIE/empreintes-hote.txt" "$SORTIE/empreintes-invite.txt" "Combat rechargé au tick $P"
echo "######## Combat Versus : OK ($CARTE, $TICKS ticks)"
