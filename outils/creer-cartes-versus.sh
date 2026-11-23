#!/bin/bash
# Brutus III (M9) : (re)crée les cartes Versus de Brutus III avec le moteur natif.
#
#   bash outils/creer-cartes-versus.sh
#
# Les cartes sont créées par programme (moteur/src/multijoueur/generateur_cartes.c), sans aucune
# donnée du jeu d'origine, et rangées dans moteur/res/assets/Brutus/cartes/ (embarquées dans la
# version web et dans l'application). Au démarrage, le jeu copie celles qui manquent dans le
# dossier des cartes. Le moteur a besoin des fichiers du jeu pour démarrer (images de l'éditeur).
# Sorties : 0 = OK, 1 = échec, 3 = sauté (jeu absent).
set -euo pipefail

RACINE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$RACINE"
JEU="$RACINE/jeu-original/app/jeu1"
DOSSIER="$RACINE/moteur/res/assets/Brutus/cartes"
BINAIRE="$RACINE/build-natif/augustus.app/Contents/MacOS/augustus"
[ -x "$BINAIRE" ] || BINAIRE="$RACINE/build-natif/augustus"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy

if [ ! -f "$JEU/c3.eng" ] && [ ! -f "$JEU/C3.eng" ]; then
  echo "Cartes Versus SAUTÉES : fichiers du jeu absents ($JEU)."
  exit 3
fi
[ -x "$BINAIRE" ] || { echo "ERREUR : moteur natif introuvable (cmake --build build-natif)." >&2; exit 1; }
mkdir -p "$DOSSIER" "$RACINE/build-rejouage"

for modele in deux-rives lac rivage; do
  case "$modele" in
    deux-rives) nom="Brutus - Deux rives.mapx" ;;
    lac) nom="Brutus - Lac central.mapx" ;;
    rivage) nom="Brutus - Rivage.mapx" ;;
  esac
  log="$RACINE/build-rejouage/carte-$modele.log"
  if ! "$BINAIRE" --creer-carte-versus "$modele" "$DOSSIER/$nom" "$JEU" > "$log" 2>&1; then
    echo "ERREUR : carte « $modele » non créée. Fin du journal ($log) :" >&2
    tail -20 "$log" >&2
    exit 1
  fi
  grep "Brutus III : carte" "$log"
done
ls -la "$DOSSIER"
