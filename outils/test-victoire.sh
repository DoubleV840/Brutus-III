#!/bin/bash
# Brutus III (M11) : test de la fin du Versus (victoire militaire et économique), sans affichage.
#
#   bash outils/test-victoire.sh [ticks]
#
# Sur « Brutus - Deux rives » (ville 1 en bas à gauche, ville 2 en haut à droite) :
# 1. militaire : la ville 2 pose un Sénat, la ville 1 un fort de légionnaires (soldats tout de suite,
#    triche 106) et envoie sa légion contre le Sénat ; quand il tombe, la ville 1 a gagné. Rejoué deux
#    fois : empreintes identiques. Même journal, combat désactivé : pas de fin.
#    Sauvegarde en plein siège, rechargement par un lancement neuf : suite identique, même fin.
#    Sauvegarde après la fin, rechargée : la partie reste finie (même gagnant, même tick).
# 2. économique : durée de 2 mois (triche 107) ; la ville 2 dépense (quatre puits), la ville 1 non :
#    à la fin des 2 mois, la ville 1 gagne au score. Sans dépense : égalité.
# Sorties : 0 = OK, 1 = échec, 3 = test sauté (jeu absent).
set -euo pipefail

RACINE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$RACINE"
TICKS="${1:-6000}"
TOUS_LES=250
CARTE="Brutus - Deux rives"
JEU="$RACINE/jeu-original/app/jeu1"
SORTIE="$RACINE/build-rejouage/victoire"
BINAIRE="$RACINE/build-natif/augustus.app/Contents/MacOS/augustus"
[ -x "$BINAIRE" ] || BINAIRE="$RACINE/build-natif/augustus"
VERIFIER="$RACINE/outils/verifier-fin-rejouage.mjs"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy

if [ ! -f "$JEU/c3.eng" ] && [ ! -f "$JEU/C3.eng" ]; then
  echo "Test victoire SAUTÉ : fichiers du jeu absents ($JEU)."
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
  grep "Brutus III : \(victoire\|score\|combat\)" "$log" | sed 's/^/   /' || true
}

# « Brutus III : victoire : fin militaire gagnant 1 tick 1234 duree 120 mois 0 »
champ() { # fichier champ
  grep "Brutus III : victoire :" "$1" | sed -n "s/.* $2 \([^ ]*\).*/\1/p"
}
echec=0
attendre() { # fichier champ valeur message
  local lu
  lu=$(champ "$1" "$2")
  if [ "$lu" != "$3" ]; then
    echo "ÉCHEC : $4 ($2 attendu : $3, lu : ${lu:-rien})."
    echec=1
  fi
}

DEPART="$SORTIE/depart.svx"
echo "== Sauvegarde de départ : $CARTE"
lancer "$SORTIE/creer.log" --creer-sauvegarde "$CARTE" "$DEPART"

# construire : type x0 y0 x1 y1 rotation rotation_extra orientation_route orientation_vue
militaire() {
  local combat="$1"
  echo "# brutus-journal 1"
  echo "1 0 triche 103 0 0 0 0"
  [ "$combat" = 1 ] || echo "2 0 triche 105 0 0 0 0"
  echo "3 0 construire 154 16 90 16 90 0 0 1 0"          # ville 1 : un mess (il en faut un pour un fort)
  echo "4 0 construire 40 12 84 12 84 0 0 1 0"           # ville 1 : fort de légionnaires
  echo "5 1 construire 85 74 16 74 16 0 0 1 0"           # ville 2 : le Sénat
  echo "10 0 triche 106 0 0 0 0"                         # soldats tout de suite
  echo "200 0 legion-deplacer 1 74 21"                   # contre le Sénat de la ville 2
}
economique() {
  local depense="$1"
  echo "# brutus-journal 1"
  echo "1 0 triche 103 0 0 0 0"
  echo "2 0 triche 107 2 0 0 0"                          # durée : 2 mois
  if [ "$depense" = 1 ]; then
    for x in 72 74 76 78; do
      echo "5 1 construire 92 $x 20 $x 20 0 0 1 0"       # ville 2 : quatre puits
    done
  fi
}
militaire 1 > "$SORTIE/journal-militaire.txt"
militaire 0 > "$SORTIE/journal-militaire-sans.txt"
economique 1 > "$SORTIE/journal-economique.txt"
economique 0 > "$SORTIE/journal-egalite.txt"

# ---------------------------------------------------------------- 1. militaire
for n in 1 2; do
  echo "== Militaire, rejouage $n"
  lancer "$SORTIE/militaire-$n.log" --rejouer "$DEPART" "$SORTIE/journal-militaire.txt" \
    --empreintes "$SORTIE/militaire-$n.txt" --ticks "$TICKS" --tous-les "$TOUS_LES" \
    --sauvegarde-finale "$SORTIE/militaire-fin-$n.svx"
done
if ! cmp -s "$SORTIE/militaire-1.txt" "$SORTIE/militaire-2.txt"; then
  echo "ÉCHEC : les deux rejouages de la victoire militaire diffèrent (non déterministe)."
  diff "$SORTIE/militaire-1.txt" "$SORTIE/militaire-2.txt" | head -5
  exit 1
fi
echo "OK : empreintes identiques ($(grep -vc '^#' "$SORTIE/militaire-1.txt") empreintes)."
attendre "$SORTIE/militaire-1.log" fin militaire "le Sénat de la ville 2 aurait dû tomber"
attendre "$SORTIE/militaire-1.log" gagnant 1 "la ville 1 aurait dû gagner"
TICK_FIN=$(champ "$SORTIE/militaire-1.log" tick)

echo "== Militaire, combat désactivé"
lancer "$SORTIE/militaire-sans.log" --rejouer "$DEPART" "$SORTIE/journal-militaire-sans.txt" \
  --empreintes "$SORTIE/militaire-sans.txt" --ticks "$TICKS" --tous-les "$TOUS_LES"
attendre "$SORTIE/militaire-sans.log" fin aucune "combat désactivé, la partie n'aurait pas dû finir"

if [ "$echec" = 0 ]; then
  P=$(( (TICK_FIN / 2) / TOUS_LES * TOUS_LES ))
  [ "$P" -ge "$TOUS_LES" ] || P=$TOUS_LES
  echo "== Sauvegarde au tick $P (en plein siège, fin au tick $TICK_FIN), rechargement par un lancement neuf"
  rm -f "$SORTIE"/militaire-hote.txt.resync-*.svx
  lancer "$SORTIE/militaire-hote.log" --rejouer "$DEPART" "$SORTIE/journal-militaire.txt" \
    --empreintes "$SORTIE/militaire-hote.txt" --ticks "$TICKS" --tous-les "$TOUS_LES" --resync-au-tick "$P"
  SAUVEGARDE="$SORTIE/militaire-hote.txt.resync-$P.svx"
  [ -f "$SAUVEGARDE" ] || { echo "ÉCHEC : sauvegarde absente : $SAUVEGARDE"; exit 1; }
  lancer "$SORTIE/militaire-invite.log" --rejouer "$SAUVEGARDE" "$SORTIE/journal-militaire.txt" \
    --empreintes "$SORTIE/militaire-invite.txt" --ticks "$(( TICKS - P ))" --tous-les "$TOUS_LES"
  node "$VERIFIER" --comparer "$SORTIE/militaire-hote.txt" "$SORTIE/militaire-invite.txt" "Siège rechargé au tick $P"
  attendre "$SORTIE/militaire-invite.log" fin militaire "après rechargement en plein siège"
  attendre "$SORTIE/militaire-invite.log" tick "$(champ "$SORTIE/militaire-hote.log" tick)" \
    "après rechargement en plein siège, même tick de fin"

  echo "== Partie finie, sauvegardée puis rechargée"
  printf '# brutus-journal 1\n' > "$SORTIE/journal-vide.txt"
  lancer "$SORTIE/militaire-apres.log" --rejouer "$SORTIE/militaire-fin-1.svx" "$SORTIE/journal-vide.txt" \
    --empreintes "$SORTIE/militaire-apres.txt" --ticks 500 --tous-les "$TOUS_LES"
  attendre "$SORTIE/militaire-apres.log" fin militaire "la partie rechargée aurait dû rester finie"
  attendre "$SORTIE/militaire-apres.log" gagnant 1 "partie rechargée, même gagnant"
  attendre "$SORTIE/militaire-apres.log" tick "$TICK_FIN" "partie rechargée, même tick de fin"
fi

# ---------------------------------------------------------------- 2. économique
for n in 1 2; do
  echo "== Économique, rejouage $n"
  lancer "$SORTIE/economique-$n.log" --rejouer "$DEPART" "$SORTIE/journal-economique.txt" \
    --empreintes "$SORTIE/economique-$n.txt" --ticks "$TICKS" --tous-les "$TOUS_LES"
done
if ! cmp -s "$SORTIE/economique-1.txt" "$SORTIE/economique-2.txt"; then
  echo "ÉCHEC : les deux rejouages de la victoire économique diffèrent (non déterministe)."
  diff "$SORTIE/economique-1.txt" "$SORTIE/economique-2.txt" | head -5
  exit 1
fi
echo "OK : empreintes identiques ($(grep -vc '^#' "$SORTIE/economique-1.txt") empreintes)."
attendre "$SORTIE/economique-1.log" fin economique "la durée de 2 mois aurait dû finir la partie"
attendre "$SORTIE/economique-1.log" gagnant 1 "la ville 1 (qui n'a rien dépensé) aurait dû gagner au score"
attendre "$SORTIE/economique-1.log" mois 2 "fin après 2 mois"

echo "== Économique, sans dépense"
lancer "$SORTIE/egalite.log" --rejouer "$DEPART" "$SORTIE/journal-egalite.txt" \
  --empreintes "$SORTIE/egalite.txt" --ticks "$TICKS" --tous-les "$TOUS_LES"
attendre "$SORTIE/egalite.log" fin economique "la durée de 2 mois aurait dû finir la partie"
attendre "$SORTIE/egalite.log" gagnant 0 "deux villes identiques : égalité attendue"

[ "$echec" = 0 ] || exit 1
echo "OK : victoire militaire de la ville 1 au tick $TICK_FIN (Sénat de la ville 2 détruit), rien sans combat ;"
echo "     victoire économique de la ville 1 après 2 mois, égalité sans dépense ; rechargements identiques."
echo "######## Victoire Versus : OK ($CARTE, $TICKS ticks)"
