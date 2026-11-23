#!/bin/bash
# Brutus III (M12) : test du récap de fin de partie du Versus, sans affichage.
#
#   bash outils/test-recap.sh [mois]      (12 mois par défaut)
#
# Sur « Brutus - Deux rives » : un bot par ville (comme outils/test-versus.sh), une bataille au gué et un
# siège (comme outils/test-combat.sh), une durée de partie de N mois (triche 107). Vérifie :
#  1. deux rejouages : mêmes empreintes (le récap est dans le bloc BR3V, donc dans l'empreinte) et même récap ;
#  2. le récap : un relevé au départ, un par mois, un à la fin (N + 1 en tout, le dernier marqué « fin ») ;
#     des soldats tués dans les deux villes, des bâtiments construits par les deux, le premier bâtiment détruit
#     par l'autre ville est à la ville 2, des bâtiments perdus par la ville 2 ;
#  3. sauvegarde au milieu, rechargement par un lancement neuf (comme un invité qui reçoit la partie) :
#     même suite, même récap à la fin (même empreinte du récap) ;
#  4. la partie finie, sauvegardée et rechargée, garde son récap ;
#  5. --capture : l'écran de fin (onglet du score, onglet de la population avec la bulle de survol, tableau),
#     copié dans build-rejouage/recap/captures/ (images du jeu : jamais commitées).
# Sorties : 0 = OK, 1 = échec, 3 = test sauté (jeu absent).
set -euo pipefail

RACINE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$RACINE"
MOIS="${1:-12}"
TICKS=$(( MOIS * 800 + 400 ))
TOUS_LES=400
CARTE="Brutus - Deux rives"
JEU="$RACINE/jeu-original/app/jeu1"
SORTIE="$RACINE/build-rejouage/recap"
BINAIRE="$RACINE/build-natif/augustus.app/Contents/MacOS/augustus"
[ -x "$BINAIRE" ] || BINAIRE="$RACINE/build-natif/augustus"
VERIFIER="$RACINE/outils/verifier-fin-rejouage.mjs"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy

if [ ! -f "$JEU/c3.eng" ] && [ ! -f "$JEU/C3.eng" ]; then
  echo "Test récap SAUTÉ : fichiers du jeu absents ($JEU)."
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
  grep "Brutus III : \(victoire\|recap :\|combat\)" "$log" | sed 's/^/   /' || true
}

echec=0
echouer() {
  echo "ÉCHEC : $*"
  echec=1
}
# « Brutus III : recap : releves 13 premier_detruit 2 type 92 tick 5800 empreinte … »
recap() { # fichier champ
  grep "Brutus III : recap :" "$1" | sed -n "s/.* $2 \([^ ]*\).*/\1/p"
}
# Valeur d'un champ d'une ville dans le dernier relevé : « … | ville 2 population 120 … tues 4 »
dernier_releve() { # fichier ville champ
  grep "Brutus III : recap releve" "$1" | tail -1 | awk -F'|' -v v="$2" -v c="$3" '{
    for (i = 2; i <= NF; i++) { n = split($i, m, " "); if (m[2] == v) for (k = 3; k < n; k += 2) if (m[k] == c) print m[k + 1] }
  }'
}

DEPART="$SORTIE/depart.svx"
echo "== Sauvegarde de départ : $CARTE"
lancer "$SORTIE/creer.log" --creer-sauvegarde "$CARTE" "$DEPART"
lancer "$SORTIE/info.log" --info-carte "$DEPART" "$SORTIE/carte.txt"

echo "== Journal : un bot par ville, une bataille, un siège, durée de $MOIS mois"
node outils/bot-rejouage.mjs "$SORTIE/carte.txt" "$SORTIE/bot-0.txt" --graine 42 --ticks "$TICKS" --joueur 0 > /dev/null
CENTRE0=$(sed -n 's/.*centre \([0-9]*\) \([0-9]*\).*/\1,\2/p' "$SORTIE/bot-0.txt")
node outils/bot-rejouage.mjs "$SORTIE/carte.txt" "$SORTIE/bot-1.txt" --graine 7 --ticks "$TICKS" --joueur 1 --eviter "$CENTRE0" > /dev/null
{
  grep '^#' "$SORTIE/bot-0.txt"
  echo "1 0 triche 103 0 0 0 0"
  {
    echo "2 0 triche 107 $MOIS 0 0 0"                       # durée de la partie
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
    echo "200 0 legion-deplacer 1 48 50"                   # bataille au gué
    echo "200 1 legion-deplacer 2 52 48"
    echo "4000 1 legion-rentrer 2"
    echo "4000 0 legion-rentrer 1"
    echo "4600 0 triche 106 0 0 0 0"                       # la ville 1 refait sa légion
    echo "4700 0 legion-deplacer 1 75 22"                  # contre le mur de la ville 2
    echo "7000 0 legion-deplacer 1 75 18"                  # puis contre ses puits
    grep -hv '^#' "$SORTIE/bot-0.txt" "$SORTIE/bot-1.txt"
  } | awk '{ if ($1 < 2) $1 = 2; print }' | sort -s -n -k1,1
} > "$SORTIE/journal.txt"
echo "   $(grep -vc '^#' "$SORTIE/journal.txt") commandes, $TICKS ticks"

# ---------------------------------------------------------------- 1. deux rejouages
for n in 1 2; do
  echo "== Rejouage $n"
  lancer "$SORTIE/rejouage-$n.log" --rejouer "$DEPART" "$SORTIE/journal.txt" \
    --empreintes "$SORTIE/empreintes-$n.txt" --ticks "$TICKS" --tous-les "$TOUS_LES" \
    --sauvegarde-finale "$SORTIE/fin-$n.svx"
done
if ! cmp -s "$SORTIE/empreintes-1.txt" "$SORTIE/empreintes-2.txt"; then
  echo "ÉCHEC : les deux rejouages diffèrent (non déterministe)."
  diff "$SORTIE/empreintes-1.txt" "$SORTIE/empreintes-2.txt" | head -5
  exit 1
fi
if ! diff <(grep "Brutus III : recap" "$SORTIE/rejouage-1.log") <(grep "Brutus III : recap" "$SORTIE/rejouage-2.log") > /dev/null; then
  echouer "les deux rejouages n'ont pas le même récap."
fi
echo "OK : empreintes identiques ($(grep -vc '^#' "$SORTIE/empreintes-1.txt") empreintes), même récap."

# ---------------------------------------------------------------- 2. contenu du récap
LOG="$SORTIE/rejouage-1.log"
grep -q "Brutus III : victoire : fin economique" "$LOG" || echouer "la partie aurait dû finir à la fin des $MOIS mois."
releves=$(recap "$LOG" releves)
[ "${releves:-0}" = $(( MOIS + 1 )) ] || echouer "$(( MOIS + 1 )) relevés attendus (départ + un par mois, le dernier à la fin), lu : ${releves:-rien}."
grep "Brutus III : recap releve" "$LOG" | tail -1 | grep -q " fin 1 " || echouer "le dernier relevé devrait être celui de la fin."
[ "$(grep -c "Brutus III : recap releve .* fin 1 " "$LOG")" = 1 ] || echouer "un seul relevé de fin attendu."
[ "$(recap "$LOG" premier_detruit)" = 2 ] || echouer "le premier bâtiment détruit par l'autre ville devrait être à la ville 2."
for v in 1 2; do
  tues=$(dernier_releve "$LOG" $v tues)
  construits=$(dernier_releve "$LOG" $v construits)
  [ "${tues:-0}" -gt 0 ] || echouer "ville $v : des soldats tués attendus (bataille au gué), lu : ${tues:-rien}."
  [ "${construits:-0}" -gt 10 ] || echouer "ville $v : des bâtiments construits attendus, lu : ${construits:-rien}."
  pop=$(grep "Brutus III : recap releve" "$LOG" | awk -F'|' -v v="$v" '{
    for (i = 2; i <= NF; i++) { split($i, m, " "); if (m[2] == v && m[4] > p) p = m[4] } } END { print p + 0 }')
  [ "${pop:-0}" -gt 0 ] || echouer "ville $v : la population devrait monter à un moment."
  echo "   ville $v : soldats tués $tues, bâtiments construits $construits, perdus $(dernier_releve "$LOG" $v perdus), pic de population $pop"
done
[ "$(dernier_releve "$LOG" 2 perdus)" -gt 0 ] || echouer "ville 2 : des bâtiments perdus attendus (siège)."
# Les relevés suivent les mois : tick croissant, un mois de jeu (800 ticks) entre deux relevés mensuels
ticks=$(grep "Brutus III : recap releve" "$LOG" | sed 's/.* tick \([0-9]*\) .*/\1/' | tr '\n' ' ')
node -e '
  const t = process.argv[1].trim().split(/ +/).map(Number);
  for (let i = 2; i < t.length - 1; i++) if (t[i] - t[i - 1] !== 800) { console.log("ÉCHEC : relevés mensuels irréguliers : " + t.join(", ")); process.exit(1); }
' "$ticks" || echec=1

# ---------------------------------------------------------------- 3. rechargement au milieu
P=$(( (TICKS / 2) / TOUS_LES * TOUS_LES ))
echo "== Sauvegarde au tick $P, rechargement par un lancement neuf"
rm -f "$SORTIE"/hote.txt.resync-*.svx
lancer "$SORTIE/hote.log" --rejouer "$DEPART" "$SORTIE/journal.txt" \
  --empreintes "$SORTIE/hote.txt" --ticks "$TICKS" --tous-les "$TOUS_LES" --resync-au-tick "$P"
SAUVEGARDE="$SORTIE/hote.txt.resync-$P.svx"
[ -f "$SAUVEGARDE" ] || { echo "ÉCHEC : sauvegarde absente : $SAUVEGARDE"; exit 1; }
lancer "$SORTIE/invite.log" --rejouer "$SAUVEGARDE" "$SORTIE/journal.txt" \
  --empreintes "$SORTIE/invite.txt" --ticks "$(( TICKS - P ))" --tous-les "$TOUS_LES"
node "$VERIFIER" --comparer "$SORTIE/hote.txt" "$SORTIE/invite.txt" "Récap rechargé au tick $P"
# L'hôte se recharge lui aussi au tick P (comme en réseau) : c'est à lui que l'invité doit être identique.
[ "$(recap "$SORTIE/invite.log" empreinte)" = "$(recap "$SORTIE/hote.log" empreinte)" ] ||
  echouer "après rechargement au tick $P, le récap diffère ($(recap "$SORTIE/invite.log" empreinte) au lieu de $(recap "$SORTIE/hote.log" empreinte))."

# ---------------------------------------------------------------- 3 bis. le joueur 2 (réseau)
# En Versus réseau, l'invité voit la ville 2 : hors simulation, sa ville active est la 2. Rejoué comme le joueur 2
# (--joueur-local 1) avec des resynchronisations (sauvegarde puis rechargement), le jeu doit rester identique à
# celui de l'hôte (bug trouvé en M12 : le chargement mettait les données de la ville 1 dans la ville 2).
echo "== Rejoué comme le joueur 2, avec resynchronisations, comparé à l'hôte"
RESYNCS="$(( TICKS / 4 )),$(( TICKS / 2 )),$(( TICKS * 3 / 4 ))"
for j in 0 1; do
  lancer "$SORTIE/joueur-$j.log" --rejouer "$DEPART" "$SORTIE/journal.txt" --empreintes "$SORTIE/joueur-$j.txt" \
    --ticks "$TICKS" --tous-les "$TOUS_LES" --joueur-local "$j" --resync-au-tick "$RESYNCS" > /dev/null
done
if diff <(grep -v '^#' "$SORTIE/joueur-0.txt") <(grep -v '^#' "$SORTIE/joueur-1.txt") > /dev/null; then
  echo "OK : joueur 1 et joueur 2 identiques ($(grep -vc '^#' "$SORTIE/joueur-0.txt") empreintes, resynchronisations aux ticks $RESYNCS)."
else
  echouer "rejoué comme le joueur 2, la partie diffère de celle de l'hôte (resynchronisations aux ticks $RESYNCS)."
fi
[ "$(recap "$SORTIE/joueur-0.log" empreinte)" = "$(recap "$SORTIE/joueur-1.log" empreinte)" ] ||
  echouer "le joueur 2 n'a pas le même récap que l'hôte."

# ---------------------------------------------------------------- 4. partie finie rechargée, captures
echo "== Partie finie, sauvegardée puis rechargée (avec captures de l'écran de fin)"
printf '# brutus-journal 1\n' > "$SORTIE/journal-vide.txt"
mkdir -p "$SORTIE/captures"
AVANT=$(mktemp)
ls "$JEU"/*.png > "$AVANT" 2>/dev/null || true
lancer "$SORTIE/apres.log" --rejouer "$SORTIE/fin-1.svx" "$SORTIE/journal-vide.txt" \
  --empreintes "$SORTIE/apres.txt" --ticks 400 --tous-les "$TOUS_LES" --capture
[ "$(recap "$SORTIE/apres.log" empreinte)" = "$(recap "$LOG" empreinte)" ] ||
  echouer "la partie finie rechargée devrait garder le même récap."
# Les captures arrivent dans le dossier du jeu : on garde les trois de l'écran de fin
NOUVELLES=$(ls "$JEU"/*.png 2>/dev/null | grep -vxF -f "$AVANT" || true)
rm -f "$AVANT"
rm -f "$SORTIE"/captures/*.png
n=0
while IFS= read -r f; do
  [ -n "$f" ] || continue
  case "$(basename "$f")" in
    "full city"*|minimap*) mv "$f" "$SORTIE/captures/" ;;
    city*) n=$(( n + 1 )); mv "$f" "$SORTIE/captures/fin-$n.png" ;;
  esac
done <<< "$NOUVELLES"
[ "$n" = 3 ] || echouer "3 captures de l'écran de fin attendues, $n trouvées."
echo "   captures : $SORTIE/captures/fin-1.png (score), fin-2.png (population, survol), fin-3.png (tableau)"

[ "$echec" = 0 ] || exit 1
echo "OK : $releves relevés, identiques sur deux rejouages, après rechargement et dans la partie finie rechargée."
echo "######## Récap Versus : OK ($CARTE, $MOIS mois)"
