#!/bin/bash
# Préparation de Brutus III : à lancer UNE fois par le fondateur, pas par les agents
# (leur bac à sable n'a accès ni à GitHub ni à Homebrew).
#
#   bash ~/Programmation/Perso/app-idée-test/Brutus-III/outils/preparer.sh [chemin/vers/setup.exe]
#
# 1. installe les outils (Homebrew)
# 2. importe le moteur Augustus dans moteur/ (git subtree, un seul commit)
# 3. extrait les fichiers du jeu original du .exe dans jeu-original/ (ignoré par git)
set -euo pipefail

cd "$(dirname "$0")/.."
EXE="${1:-$HOME/Downloads/setup-01612-Caesar_III_Trad_FR_1_4-PCWin.exe}"
AUGUSTUS="https://github.com/Keriew/augustus.git"

echo "== 1/3 Outils (Homebrew) : peut prendre plusieurs minutes la première fois"
brew install cmake sdl2 sdl2_mixer emscripten innoextract

echo
echo "== 2/3 Moteur Augustus dans moteur/"
if [ -d moteur ]; then
  echo "moteur/ existe déjà : rien à faire."
else
  if ! git rev-parse -q --verify HEAD >/dev/null; then
    echo "Le dépôt n'a encore aucun commit : ajoute d'abord le projet dans ivwi Office (« ＋ Nouveau projet… »), puis relance ce script."
    exit 1
  fi
  if [ "$(git rev-parse --abbrev-ref HEAD)" != "main" ]; then
    echo "Le dossier n'est pas sur main (une mission ivwi Office est peut-être en cours). Attends qu'elle soit finie, puis relance."
    exit 1
  fi
  if [ -n "$(git status --porcelain)" ]; then
    echo "Il y a des modifications non commitées dans le dossier. Attends la fin de la mission en cours, puis relance."
    exit 1
  fi
  BRANCHE=$(git ls-remote --symref "$AUGUSTUS" HEAD | awk '/^ref:/ { sub("refs/heads/", "", $2); print $2 }')
  echo "Branche par défaut d'Augustus : $BRANCHE"
  git subtree add --prefix=moteur "$AUGUSTUS" "$BRANCHE" --squash -m "Import du moteur Augustus ($BRANCHE) dans moteur/"
fi

echo
echo "== 3/3 Fichiers du jeu original dans jeu-original/ (jamais commités)"
if [ -n "$(find jeu-original -iname 'c3.eng' 2>/dev/null | head -1)" ]; then
  echo "jeu-original/ contient déjà les fichiers du jeu : rien à faire."
elif [ ! -f "$EXE" ]; then
  echo "Installeur introuvable : $EXE"
  echo "Relance avec le bon chemin : bash outils/preparer.sh /chemin/vers/setup.exe"
  exit 1
else
  mkdir -p jeu-original
  if ! innoextract --silent --output-dir jeu-original "$EXE"; then
    echo "innoextract n'a pas réussi à ouvrir l'installeur."
    echo "Solution de secours : installe le jeu sur ton PC Windows et copie le dossier installé dans $PWD/jeu-original/"
    exit 1
  fi
  TROUVE=$(find jeu-original -iname 'c3.eng' | head -1)
  if [ -z "$TROUVE" ]; then
    echo "Extraction faite, mais c3.eng est introuvable dans jeu-original/ : regarde ce qui a été extrait."
    exit 1
  fi
  echo "Fichiers du jeu trouvés dans : $(dirname "$TROUVE")"
fi

echo
echo "Vérification : aucun fichier du jeu suivi par git…"
if git ls-files | grep -iE '\.(sg2|555|smk|eng|emp|map|wav)$' | grep -v '^moteur/'; then
  echo "ATTENTION : des fichiers du jeu sont suivis par git (liste ci-dessus). Ne pousse rien et préviens le CEO."
  exit 1
fi
echo "OK. Préparation terminée : tu peux envoyer la mission suivante dans ivwi Office."
