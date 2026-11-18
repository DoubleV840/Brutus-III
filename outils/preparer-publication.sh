#!/bin/bash
# Prépare le dossier publié sur GitHub Pages à partir de build-web/, puis lance
# le garde-fou (outils/verifier-publication.sh). Appelé à la fin de build-web.sh
# et par la CI (.github/workflows/pages.yml).
#
# build-pages/
#   index.html   = build-web/augustus.html (fichier unique : JS + wasm + assets
#                  d'Augustus intégrés), renommé pour que https://brutus.ivwi.fr/ ouvre le jeu
#   CNAME        = brutus.ivwi.fr
#   .nojekyll
set -euo pipefail

RACINE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$RACINE"
PAGE="build-web/augustus.html"
PUBLI="build-pages"

if [ ! -f "$PAGE" ]; then
  echo "ERREUR : $PAGE absent. Lance d'abord : bash outils/build-web.sh" >&2
  exit 1
fi

rm -rf "$PUBLI"
mkdir -p "$PUBLI"
cp "$PAGE" "$PUBLI/index.html"
echo "brutus.ivwi.fr" > "$PUBLI/CNAME"
: > "$PUBLI/.nojekyll"

bash outils/verifier-publication.sh "$PUBLI"
