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
# - prépare aussi build-pages/ (ce que la CI publie) et lance le garde-fou
#   outils/verifier-publication.sh.
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
# - BRUTUS_SHELL_IN : notre page (web/shell.html.in) au lieu de celle d'Augustus.
# - CMAKE_EXE_LINKER_FLAGS vide : M1 y mettait -sDEFAULT_TO_CXX (pour embind,
#   retiré du moteur en M2). Sans cette ligne, l'ancienne valeur resterait
#   dans le cache CMake d'un build-web/ existant.
emcmake cmake -S moteur -B "$SORTIE" \
  -DTARGET_PLATFORM=emscripten \
  -DCMAKE_BUILD_TYPE="$TYPE" \
  -DBRUTUS_SHELL_IN="$RACINE/web/shell.html.in" \
  -DCMAKE_EXE_LINKER_FLAGS=""

# Pour CMake, la page n'est pas une dépendance de l'édition de liens : on la
# force, pour qu'une modification de web/ soit toujours prise en compte.
rm -f "$SORTIE/augustus.html"

echo "== Compilation"
emmake cmake --build "$SORTIE" -j "$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo "== Vérification : aucun fichier du jeu original dans la sortie"
if grep -rIl --include='*.txt' --include='*.make' --include='*.rsp' 'jeu-original' "$SORTIE/CMakeFiles" 2>/dev/null; then
  echo "ERREUR : la compilation fait référence à jeu-original/ (fichiers ci-dessus). Règle 1 de PLAN.md." >&2
  exit 1
fi
# Les assets sont embarqués dans augustus.html (--embed-file) : on vérifie leur
# source dans la commande d'édition de liens (uniquement moteur/), puis le
# contenu réel de la page via le garde-fou de publication (plus bas).
LINK_TXT="$SORTIE/CMakeFiles/augustus.dir/link.txt"
if [ ! -f "$LINK_TXT" ]; then
  echo "ERREUR : $LINK_TXT introuvable : impossible de vérifier les fichiers embarqués." >&2
  exit 1
fi
EMBARQUES="$(grep -oE -- '--(embed|preload)-file [^ ]+' "$LINK_TXT" | awk '{print $2}' || true)"
if [ -z "$EMBARQUES" ]; then
  echo "ERREUR : aucun --embed-file dans $LINK_TXT (format changé ? à revoir)." >&2
  exit 1
fi
echo "$EMBARQUES" | while read -r SOURCE; do
  case "$SOURCE" in
    "$RACINE/moteur/"*) echo "   embarqué : ${SOURCE#"$RACINE/"}" ;;
    *) echo "ERREUR : fichier embarqué hors de moteur/ : $SOURCE. Règle 1 de PLAN.md." >&2; exit 1 ;;
  esac
done

echo "== Vérification : IDBFS présent dans le JS (sauvegardes persistantes)"
if ! grep -q '<title>Brutus III</title>' "$SORTIE/augustus.html"; then
  echo "ERREUR : la page web/shell.html.in n'a pas été utilisée." >&2
  exit 1
fi
if ! grep -q 'IDBFS=' "$SORTIE/augustus.html"; then
  echo "ERREUR : IDBFS absent du JS généré (voir EXPORTED_RUNTIME_METHODS dans moteur/CMakeLists.txt)." >&2
  exit 1
fi

echo "== Dossier de publication (build-pages/) et garde-fou"
bash "$RACINE/outils/preparer-publication.sh"

echo
echo "OK : $(ls -lh "$SORTIE/augustus.html" | awk '{print $5}') -> build-web/augustus.html"
echo "Tester : cd build-web && python3 -m http.server 8000   puis http://localhost:8000/augustus.html"
