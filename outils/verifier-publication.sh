#!/bin/bash
# Garde-fou avant publication (règle 1 de PLAN.md) : échoue si le dossier publié
# peut contenir un fichier de Caesar III.
#
#   bash outils/verifier-publication.sh build-pages
#
# Vérifie :
#  1. aucun nom de fichier typique des données du jeu (liste de .gitignore + extras) ;
#  2. seuls des types de fichiers web attendus (liste blanche) ;
#  3. taille totale raisonnable (BRUTUS_PUBLICATION_MAX_MO, 60 par défaut) ;
#  4. aucune mention de « jeu-original » ;
#  5. fichiers embarqués (--embed-file) : le wasm est intégré en base64 dans la
#     page (SINGLE_FILE). On le décode, on lit la liste des chemins « /assets/… »
#     et chacun doit exister dans moteur/res/assets (ou packed_assets).
#     Si aucun wasm ou aucune liste n'est trouvé, le script échoue (mieux vaut un
#     faux positif qu'un contrôle qui passe sans rien vérifier).
set -euo pipefail

RACINE="$(cd "$(dirname "$0")/.." && pwd)"
DOSSIER="${1:-}"
MAX_MO="${BRUTUS_PUBLICATION_MAX_MO:-60}"

if [ -z "$DOSSIER" ] || [ ! -d "$DOSSIER" ]; then
  echo "Usage : bash outils/verifier-publication.sh <dossier publié>" >&2
  exit 2
fi
DOSSIER="$(cd "$DOSSIER" && pwd)"
echo "== Garde-fou de publication : $DOSSIER"

ERREUR=0
erreur() { echo "ERREUR : $*" >&2; ERREUR=1; }

if [ ! -f "$DOSSIER/index.html" ]; then
  erreur "index.html absent."
fi

# 1. Noms typiques des données de Caesar III (cf. .gitignore et règle 1).
INTERDITS=$(find "$DOSSIER" \( -iname '*.sg2' -o -iname '*.555' -o -iname '*.smk' \
  -o -iname '*.eng' -o -iname '*.emp' -o -iname '*.map' -o -iname '*.wav' \
  -o -iname '*.mp3' -o -iname '*.sav' -o -iname '*.svx' -o -iname '*.inf' \
  -o -iname '*.bik' -o -iname '*.avi' -o -iname '*.mpg' -o -iname '*.exe' \
  -o -iname '*.dll' -o -iname 'c3*' -o -iname 'caesar*' -o -iname 'jeu-original' \) -print)
if [ -n "$INTERDITS" ]; then
  echo "$INTERDITS" >&2
  erreur "fichiers ou dossiers typiques des données du jeu (ci-dessus)."
fi

# 2. Liste blanche : la page est un fichier unique (SINGLE_FILE) + CNAME + .nojekyll.
#    Un .data (--preload-file) est refusé : il faudrait d'abord l'ajouter au contrôle 5.
AUTRES=$(find "$DOSSIER" -type f ! -name 'CNAME' ! -name '.nojekyll' \
  ! -iname '*.html' ! -iname '*.js' ! -iname '*.wasm' ! -iname '*.css' \
  ! -iname '*.ico' ! -iname '*.svg' -print)
if [ -n "$AUTRES" ]; then
  echo "$AUTRES" >&2
  erreur "fichiers non prévus dans la publication (ci-dessus). Mets à jour la liste blanche de ce script si c'est voulu."
fi

# 3. Taille totale (le jeu original fait ~670 Mo ; la page ~30 Mo).
TAILLE_KO=$(du -sk "$DOSSIER" | awk '{print $1}')
if [ "$TAILLE_KO" -gt $((MAX_MO * 1024)) ]; then
  erreur "le dossier pèse $((TAILLE_KO / 1024)) Mo (> $MAX_MO Mo). Anormal : vérifie son contenu."
fi

# 4. Mention du dossier des fichiers du jeu.
if grep -rIl 'jeu-original' "$DOSSIER" 2>/dev/null; then
  erreur "mention de jeu-original/ dans les fichiers ci-dessus."
fi

# 5. Fichiers embarqués dans le wasm.
if ! python3 "$RACINE/outils/verifier-embarques.py" "$DOSSIER" "$RACINE/moteur/res"; then
  erreur "contrôle des fichiers embarqués."
fi

if [ "$ERREUR" -ne 0 ]; then
  echo "ÉCHEC du garde-fou : rien ne doit être publié." >&2
  exit 1
fi
echo "OK : publication sans fichier du jeu ($((TAILLE_KO / 1024)) Mo)."
