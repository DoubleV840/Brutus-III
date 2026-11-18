#!/usr/bin/env python3
"""Contrôle des fichiers embarqués par Emscripten (--embed-file) dans la publication.

Appelé par outils/verifier-publication.sh :
    python3 outils/verifier-embarques.py <dossier publié> <moteur/res>

Le wasm est soit un fichier .wasm, soit intégré en base64 dans la page
(SINGLE_FILE=1, SINGLE_FILE_BINARY_ENCODE=0). Avec --embed-file, les chemins
des fichiers embarqués sont des chaînes du wasm : « /assets/... ».
Chacun doit exister dans moteur/res/assets (ou moteur/res/packed_assets) et
ne pas avoir une extension des données de Caesar III, sauf s'il est suivi par
git dans moteur/res (fichiers fournis par Augustus, ex. i18n/*.555).
Échoue si aucun wasm ou aucun chemin n'est trouvé (format changé : à revoir).
"""
import base64
import os
import re
import subprocess
import sys

INTERDITES = {'.sg2', '.555', '.smk', '.eng', '.emp', '.map', '.wav', '.mp3',
              '.sav', '.svx', '.inf', '.bik', '.avi', '.mpg', '.exe', '.dll'}


def wasms(dossier):
    for racine, _, fichiers in os.walk(dossier):
        for nom in fichiers:
            chemin = os.path.join(racine, nom)
            with open(chemin, 'rb') as f:
                contenu = f.read()
            if contenu.startswith(b'\0asm'):
                yield chemin, contenu
            elif nom.lower().endswith(('.html', '.js')):
                for bloc in re.findall(rb'[A-Za-z0-9+/]{1000,}={0,2}', contenu):
                    try:
                        donnees = base64.b64decode(bloc, validate=True)
                    except ValueError:
                        continue
                    if donnees.startswith(b'\0asm'):
                        yield chemin + ' (wasm en base64)', donnees


def main():
    dossier, res = sys.argv[1], sys.argv[2]
    sources = [os.path.join(res, 'assets'), os.path.join(res, 'packed_assets')]
    # Fichiers suivis par git dans moteur/res : les seuls tolérés avec une
    # extension « du jeu » (ex. polices i18n/*.555 fournies par Augustus).
    racine_depot = os.path.dirname(os.path.dirname(os.path.abspath(res)))
    try:
        sortie = subprocess.run(['git', '-C', racine_depot, 'ls-files', '-z', 'moteur/res'],
                                check=True, capture_output=True).stdout
        suivis = {p.decode('utf-8') for p in sortie.split(b'\0') if p}
    except (OSError, subprocess.CalledProcessError):
        suivis = set()
    ok = True
    trouves = 0
    for origine, wasm in wasms(dossier):
        trouves += 1
        if b'jeu-original' in wasm:
            print(f'ERREUR : « jeu-original » apparaît dans {origine}', file=sys.stderr)
            ok = False
        chemins = sorted(set(re.findall(rb'/assets/[^\x00]{1,300}', wasm)))
        if not chemins:
            print(f'ERREUR : aucune liste de fichiers embarqués lisible dans {origine}', file=sys.stderr)
            ok = False
            continue
        inconnus = []
        for brut in chemins:
            relatif = brut[len(b'/assets/'):].decode('utf-8', 'replace')
            if os.path.splitext(relatif)[1].lower() in INTERDITES:
                suivi = [s for s in sources if os.path.relpath(os.path.join(s, relatif), racine_depot) in suivis]
                if suivi:
                    print(f'   toléré (fourni par Augustus, suivi par git) : /assets/{relatif}')
                else:
                    print(f'ERREUR : fichier embarqué interdit : /assets/{relatif}', file=sys.stderr)
                    ok = False
            if not any(os.path.isfile(os.path.join(s, relatif)) for s in sources):
                inconnus.append(relatif)
        if inconnus:
            for r in inconnus[:20]:
                print(f'ERREUR : embarqué mais absent de moteur/res : /assets/{r}', file=sys.stderr)
            print(f'({len(inconnus)} fichier(s) embarqué(s) hors de moteur/res)', file=sys.stderr)
            ok = False
        print(f'{origine} : {len(chemins)} fichier(s) embarqué(s), {len(chemins) - len(inconnus)} trouvés dans moteur/res')
    if trouves == 0:
        print('ERREUR : aucun wasm trouvé (ni .wasm ni base64 dans la page).', file=sys.stderr)
        ok = False
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
