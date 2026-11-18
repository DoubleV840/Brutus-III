#!/bin/bash
# Compilation web (WebAssembly) de moteur/ avec Emscripten.
#
#   bash outils/build-web.sh            # Release
#   bash outils/build-web.sh Debug      # avec ASSERTIONS Emscripten
#
# - sortie : build-web/augustus.html (fichier unique, wasm intégré)
# - cache Emscripten (ports SDL2, SDL2_mixer, mpg123…) : .emcache/ à la racine
#   La 1re fois, les ports se téléchargent depuis GitHub. Ensuite, plus besoin du réseau.
# - idempotent : relancer le script recompile seulement ce qui a changé.
# - n'embarque JAMAIS de fichier de jeu-original/ (règle 1 de PLAN.md) : seuls
#   les assets libres d'Augustus (moteur/res/assets) sont intégrés, et c'est vérifié à la fin.
set -euo pipefail

RACINE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$RACINE"

TYPE="${1:-Release}"
SORTIE="$RACINE/build-web"
export EM_CACHE="$RACINE/.emcache"

if ! command -v emcc >/dev/null 2>&1 || ! command -v emcmake >/dev/null 2>&1; then
  echo "ERREUR : emcc/emcmake introuvable. Installe Emscripten : brew install emscripten" >&2
  exit 1
fi
if [ ! -f moteur/CMakeLists.txt ]; then
  echo "ERREUR : moteur/ (Augustus) est absent. Lance d'abord : bash outils/preparer.sh" >&2
  exit 1
fi

mkdir -p "$EM_CACHE" "$SORTIE"
echo "== Emscripten : $(emcc --version | head -1)"
echo "== Cache : $EM_CACHE"

# Ports Emscripten : on les prépare UNE fois, en séquence, avec exactement les
# options de moteur/CMakeLists.txt. Sinon chaque fichier .c compilé en parallèle
# tente de télécharger/compiler les ports en même temps (verrou du cache bloqué).
echo "== Ports Emscripten (SDL2, SDL2_mixer mp3+ogg, mpg123) : téléchargés la 1re fois seulement"
TMP_PORTS="$SORTIE/ports-test"
mkdir -p "$TMP_PORTS"
echo 'int main(void) { return 0; }' > "$TMP_PORTS/vide.c"
if ! emcc "$TMP_PORTS/vide.c" -o "$TMP_PORTS/vide.js" \
     -sUSE_SDL=2 -sUSE_SDL_MIXER=2 '-sSDL2_MIXER_FORMATS=["mp3,ogg"]' -sUSE_MPG123=1; then
  echo "ERREUR : impossible de préparer les ports Emscripten." >&2
  echo "Si c'est un problème réseau : il faut github.com, codeload.github.com," >&2
  echo "release-assets.githubusercontent.com (releases GitHub) et www.mpg123.de." >&2
  echo "Le bac à sable des agents ne les a pas tous : lance ce script une fois depuis ton Terminal." >&2
  exit 1
fi

echo "== Configuration ($TYPE) dans build-web/"
emcmake cmake -S moteur -B "$SORTIE" \
  -DTARGET_PLATFORM=emscripten \
  -DCMAKE_BUILD_TYPE="$TYPE"

echo "== Compilation"
emmake cmake --build "$SORTIE" -j "$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

echo "== Vérification : aucun fichier du jeu original dans la sortie"
if grep -rIl --include='*.txt' --include='*.make' --include='*.rsp' 'jeu-original' "$SORTIE/CMakeFiles" 2>/dev/null; then
  echo "ERREUR : la compilation fait référence à jeu-original/ (fichiers ci-dessus). Règle 1 de PLAN.md." >&2
  exit 1
fi
if find "$SORTIE" -type f \( -iname '*.sg2' -o -iname '*.555' -o -iname '*.smk' -o -iname '*.eng' -o -iname '*.emp' -o -iname '*.map' -o -iname '*.wav' \) | grep .; then
  echo "ERREUR : fichiers du jeu original trouvés dans build-web/ (liste ci-dessus). Règle 1 de PLAN.md." >&2
  exit 1
fi

echo
echo "OK : $(ls -lh "$SORTIE/augustus.html" | awk '{print $5}') -> build-web/augustus.html"
echo "(La page jouable avec chargement du dossier du jeu arrive en M2.)"
