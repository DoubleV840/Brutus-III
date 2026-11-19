#!/bin/bash
# Brutus III : test de rejouage déterministe (phases 2b et M6a).
#
#   bash outils/test-rejouage.sh [ticks] [binaire]
#
# Pour chaque carte de BRUTUS_CARTES (défaut : Lugdunum Tarraco Carthago) :
# 1. crée une sauvegarde de départ (si absente ou plus vieille que le moteur) ;
# 2. écrit la carte texte et fabrique un journal avec le bot (graine fixe) ;
# 3. rejoue DEUX fois le même journal, dans deux lancements séparés, vérifie que toutes les
#    commandes du journal ont été appliquées (ligne « # fin ») et compare les empreintes ;
# 4. « interface ouverte » (M6a) : même journal en multijoueur (ticks imposés de l'extérieur,
#    image par image) avec une interface scriptée (chaque conseiller, fiches, liste des messages,
#    glissers, fantôme de démolition) : mêmes empreintes que le rejouage 3 ;
# 5. annulation par joueur (M6a) : le joueur 0 ne peut pas annuler la construction du joueur 1 ;
# 6. resynchronisation (cas du réseau, M6a) : l'« hôte » rejoue, écrit une sauvegarde à 1/3 et à
#    2/3 du parcours et la recharge dans le même programme ; chaque « invité » charge la même
#    sauvegarde dans un lancement neuf et continue (en rechargeant aux points suivants, comme
#    tout le monde). Les empreintes de l'invité doivent être celles de l'hôte.
#    Pour information : on compare aussi la partie rechargée à la partie continue (pas exigé :
#    le chargement d'Augustus refait des calculs, c'est pourquoi tous les joueurs, hôte compris,
#    rechargent la sauvegarde).
#
# Tout est écrit dans build-rejouage/<carte>/ (ignoré par git). Sans les fichiers du jeu
# (jeu-original/app/jeu1), le test est sauté proprement (code 0).
# Variables facultatives : BRUTUS_CARTES, BRUTUS_GRAINE (42), BRUTUS_SANS_INTERFACE=1 (saute 4).
set -euo pipefail

RACINE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$RACINE"

TICKS="${1:-150000}"
JEU="$RACINE/jeu-original/app/jeu1"
CARTES="${BRUTUS_CARTES:-${BRUTUS_CARTE:-Lugdunum Tarraco Carthago}}"
GRAINE="${BRUTUS_GRAINE:-42}"
TOUS_LES=500

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

VERIFIER="$RACINE/outils/verifier-fin-rejouage.mjs"

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
  node "$VERIFIER" "$1"
}

# Points de resynchronisation : 1/3 et 2/3 du parcours, multiples de TOUS_LES.
P1=$(( TICKS / 3 / TOUS_LES * TOUS_LES ))
P2=$(( 2 * TICKS / 3 / TOUS_LES * TOUS_LES ))

for CARTE in $CARTES; do
  SORTIE="$RACINE/build-rejouage/$CARTE"
  mkdir -p "$SORTIE"
  DEPART="$SORTIE/depart.svx"
  echo "######## Carte « $CARTE »"

  if [ ! -f "$DEPART" ] || [ "$BINAIRE" -nt "$DEPART" ]; then
    echo "== Sauvegarde de départ"
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

  if [ "${BRUTUS_SANS_INTERFACE:-0}" != "1" ]; then
    echo "== Interface ouverte en multijoueur (conseillers, fiches, messages, glissers)"
    debut=$(date +%s)
    lancer "$SORTIE/rejouage-interface.log" --rejouer "$DEPART" "$SORTIE/journal.txt" \
      --empreintes "$SORTIE/empreintes-interface.txt" --ticks "$TICKS" --tous-les "$TOUS_LES" --interface-test
    echo "   durée : $(( $(date +%s) - debut )) s ; $(grep '^# interface' "$SORTIE/empreintes-interface.txt" | sed 's/^# //')"
    verifier_fin "$SORTIE/empreintes-interface.txt"
    node "$VERIFIER" --comparer "$SORTIE/empreintes-1.txt" "$SORTIE/empreintes-interface.txt" "interface ouverte"
  fi

  echo "== Annulation par joueur"
  premiere=$(grep -m1 ' construire ' "$SORTIE/journal.txt" | cut -d' ' -f3-)
  printf '# brutus-journal 1\n10 1 %s\n' "$premiere" > "$SORTIE/journal-annul-a.txt"
  printf '# brutus-journal 1\n10 1 %s\n20 0 annuler\n' "$premiere" > "$SORTIE/journal-annul-b.txt"
  printf '# brutus-journal 1\n10 1 %s\n20 1 annuler\n' "$premiere" > "$SORTIE/journal-annul-c.txt"
  for v in a b c; do
    lancer "$SORTIE/annul-$v.log" --rejouer "$DEPART" "$SORTIE/journal-annul-$v.txt" \
      --empreintes "$SORTIE/empreintes-annul-$v.txt" --ticks 40 --tous-les 10 > /dev/null
  done
  if ! grep -q '^# bilan annuler appliquees 1 refusees 1' "$SORTIE/empreintes-annul-b.txt"; then
    echo "ÉCHEC : l'annulation du joueur 0 sur la construction du joueur 1 n'a pas été refusée."
    exit 1
  fi
  if ! cmp -s <(grep -v '^#' "$SORTIE/empreintes-annul-a.txt") <(grep -v '^#' "$SORTIE/empreintes-annul-b.txt"); then
    echo "ÉCHEC : l'annulation refusée a changé la partie."
    exit 1
  fi
  if cmp -s <(grep -v '^#' "$SORTIE/empreintes-annul-a.txt") <(grep -v '^#' "$SORTIE/empreintes-annul-c.txt"); then
    echo "ÉCHEC : l'annulation du joueur 1 n'a rien changé (première construction : $premiere)."
    exit 1
  fi
  echo "OK : chacun n'annule que sa propre construction."

  if [ "$P1" -le 0 ] || [ "$P2" -le "$P1" ]; then
    echo "Resynchronisation sautée : trop peu de ticks ($TICKS)."
    continue
  fi
  echo "== Resynchronisation aux ticks $P1 et $P2 : hôte (même programme) et invités (lancements neufs)"
  rm -f "$SORTIE"/empreintes-hote.txt.resync-*.svx
  lancer "$SORTIE/rejouage-hote.log" --rejouer "$DEPART" "$SORTIE/journal.txt" \
    --empreintes "$SORTIE/empreintes-hote.txt" --ticks "$TICKS" --tous-les "$TOUS_LES" \
    --resync-au-tick "$P1,$P2"
  verifier_fin "$SORTIE/empreintes-hote.txt"
  for P in "$P1" "$P2"; do
    SAUVEGARDE="$SORTIE/empreintes-hote.txt.resync-$P.svx"
    if [ ! -f "$SAUVEGARDE" ]; then
      echo "ÉCHEC : sauvegarde de resynchronisation absente : $SAUVEGARDE"
      exit 1
    fi
    SUITE=()
    if [ "$P" = "$P1" ]; then
      SUITE=(--resync-au-tick "$P2")
    fi
    lancer "$SORTIE/rejouage-invite-$P.log" --rejouer "$SAUVEGARDE" "$SORTIE/journal.txt" \
      --empreintes "$SORTIE/empreintes-invite-$P.txt" --ticks "$(( TICKS - P ))" --tous-les "$TOUS_LES" \
      "${SUITE[@]+"${SUITE[@]}"}"
    verifier_fin "$SORTIE/empreintes-invite-$P.txt"
    node "$VERIFIER" --comparer "$SORTIE/empreintes-hote.txt" "$SORTIE/empreintes-invite-$P.txt" \
      "invité chargé au tick $P"
  done
  # Pour information : partie rechargée (hôte) et partie continue.
  if node "$VERIFIER" --comparer "$SORTIE/empreintes-1.txt" "$SORTIE/empreintes-hote.txt" \
      "partie rechargée" > "$SORTIE/continu-vs-recharge.txt" 2>&1; then
    echo "   info : partie rechargée identique à la partie continue."
  else
    echo "   info (non bloquant) : $(sed 's/^ÉCHEC : //' "$SORTIE/continu-vs-recharge.txt")"
  fi
done
echo "######## Toutes les cartes : OK ($CARTES)"
