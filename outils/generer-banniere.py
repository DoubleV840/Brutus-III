#!/usr/bin/env python3
"""Brutus III : génère les images « BRUTUS III » du jeu.

- Banniere_Menu.png (284 x 61) : remplace la bannière « AUGUSTUS » du menu principal
  (moteur/res/assets/Graphics/UI/Augustus_Main_Menu_Banner_Sml.png, CC BY-SA 3.0), même format :
  plaque de pierre, grande initiale qui dépasse en haut, lettres dorées ombrées.
- Logo_Demarrage.png (même dessin, deux fois plus grand) : remplace le logo « CAESAR III » de
  l'écran « Cliquez pour démarrer » (window/logo.c).

La pierre est générée (bruit), rien n'est repris du jeu original.
Sortie : moteur/res/assets/Graphics/Brutus/ (lue par Graphics/Brutus.xml).
Dépendances : pillow, numpy, et la police Baskerville de macOS.
Usage : python3 outils/generer-banniere.py
"""
import os

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

LARGEUR, HAUTEUR = 284, 61  # à l'échelle 1
PLAQUE = (8, 20, 282, 59)  # x0, y0, x1, y1 (inclus), comme la bannière d'Augustus
POLICE = '/System/Library/Fonts/Supplemental/Baskerville.ttc'
POLICE_INDEX = 4  # Baskerville SemiBold
SUR = 4  # suréchantillonnage des lettres, pour des bords lisses

RACINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOSSIER = os.path.join(RACINE, 'moteur/res/assets/Graphics/Brutus')
IMAGES = [('Banniere_Menu.png', 1), ('Logo_Demarrage.png', 2)]


def bruit(h, w, graine, pas_min, octaves=5):
    """Bruit de valeur fractal dans [0, 1] (pas_min : taille des plus petits détails)."""
    rng = np.random.default_rng(graine)
    total = np.zeros((h, w))
    amplitude, somme = 1.0, 0.0
    for o in range(octaves):
        pas = pas_min * 2 ** o
        grille = rng.random((h // pas + 2, w // pas + 2))
        petit = Image.fromarray((grille * 255).astype(np.uint8))
        grand = petit.resize(((w // pas + 2) * pas, (h // pas + 2) * pas), Image.BICUBIC)
        total += amplitude * (np.asarray(grand, dtype=float)[:h, :w] / 255.0)
        somme += amplitude
        amplitude *= 0.55
    return total / somme


def pierre(w, h, e):
    """Marbre beige veiné de brun, proche de la plaque d'Augustus (e : échelle)."""
    n1 = bruit(h, w, 3, 4 * e)
    n2 = bruit(h, w, 11, 4 * e)
    y, x = np.mgrid[0:h, 0:w] / e
    veines = np.abs(np.sin((x * 0.045 + y * 0.03 + n1 * 7.0) * np.pi))
    veines = 1.0 - np.clip(veines * 3.0, 0, 1)  # traits fins là où le sinus s'annule
    base = np.array([158, 142, 116], dtype=float)
    clair = np.array([205, 190, 160], dtype=float)
    fonce = np.array([112, 82, 52], dtype=float)
    t = (n2 - 0.5) * 3.4
    img = base + np.clip(t, 0, 1)[..., None] * (clair - base) + np.clip(-t, 0, 1)[..., None] * (fonce - base)
    img = img * (1 - 0.45 * veines[..., None]) + fonce * 0.45 * veines[..., None]
    grain = (np.random.default_rng(5).random((h, w)) - 0.5) * 18
    img += grain[..., None]
    return np.clip(img, 0, 255)


def biseau(img, epaisseur):
    """Bords de la plaque : clairs en haut et à gauche, sombres en bas et à droite."""
    h, w, _ = img.shape
    for i in range(epaisseur):
        f = 1.0 - i / epaisseur
        img[i, i:w - i] = img[i, i:w - i] * (1 + 0.35 * f)
        img[i:h - i, i] = img[i:h - i, i] * (1 + 0.25 * f)
        img[h - 1 - i, i:w - i] = img[h - 1 - i, i:w - i] * (1 - 0.45 * f)
        img[i:h - i, w - 1 - i] = img[i:h - i, w - 1 - i] * (1 - 0.40 * f)
    img[0, :] = img[-1, :] = img[:, 0] = img[:, -1] = (60, 45, 30)
    return np.clip(img, 0, 255)


def masque_texte(w, h, e):
    """Masque (suréchantillonné) de « BRUTUS III » : grande initiale, puis petites capitales."""
    k = SUR * e
    m = Image.new('L', (w * SUR, h * SUR), 0)
    d = ImageDraw.Draw(m)
    grande = ImageFont.truetype(POLICE, 72 * k, index=POLICE_INDEX)
    petite = ImageFont.truetype(POLICE, 41 * k, index=POLICE_INDEX)
    ligne_base = 54 * k  # bas des lettres, comme sur la bannière d'Augustus
    x = 14 * k
    morceaux = [('B', grande, 2 * k), ('RUTUS', petite, int(2.2 * k)), (' ', petite, 0), ('III', petite, int(2.2 * k))]
    largeur_totale = 0
    for texte, police, espace in morceaux:
        for c in texte:
            largeur_totale += police.getlength(c) + espace
    marge = (PLAQUE[2] - 6) * k - x
    echelle = min(1.0, marge / largeur_totale)
    for texte, police, espace in morceaux:
        if echelle < 1.0:
            police = police.font_variant(size=int(police.size * echelle))
            espace = int(espace * echelle)
        for c in texte:
            d.text((x, ligne_base), c, font=police, fill=255, anchor='ls')
            x += police.getlength(c) + espace
    return m


def image(e):
    """L'image entière à l'échelle e (1 : menu principal, 2 : écran de démarrage)."""
    largeur, hauteur = LARGEUR * e, HAUTEUR * e
    toile = np.zeros((hauteur, largeur, 4), dtype=float)

    x0, y0, x1, y1 = (v * e for v in PLAQUE)
    x1, y1 = x1 + e - 1, y1 + e - 1
    plaque = biseau(pierre(x1 - x0 + 1, y1 - y0 + 1, e), 3 * e)
    toile[y0:y1 + 1, x0:x1 + 1, :3] = plaque
    toile[y0:y1 + 1, x0:x1 + 1, 3] = 255
    fond = Image.fromarray(toile.astype(np.uint8), 'RGBA')

    grand = masque_texte(largeur, hauteur, e)
    lettres = grand.resize((largeur, hauteur), Image.LANCZOS)

    # Ombre portée sombre, décalée vers le bas à droite, et contour brun.
    ombre = ImageChops.offset(lettres, 2 * e, 2 * e).filter(ImageFilter.GaussianBlur(1.4 * e))
    contour = lettres.filter(ImageFilter.MaxFilter(2 * e + 1))
    noir = Image.new('RGBA', (largeur, hauteur), (20, 12, 5, 255))
    fond = Image.composite(noir, fond, ombre.point(lambda v: int(v * 0.85)))
    brun = Image.new('RGBA', (largeur, hauteur), (70, 42, 10, 255))
    fond = Image.composite(brun, fond, contour)
    # Le contour et l'ombre peuvent dépasser de la plaque (grande initiale) : on garde leur opacité.
    alpha = np.maximum(np.asarray(fond)[..., 3], np.maximum(np.asarray(contour), np.asarray(ombre) * 0.85))

    # Or : dégradé vertical, relief (clair en haut à gauche, sombre en bas à droite).
    y = np.linspace(0, 1, hauteur)[:, None]
    haut = np.array([255, 236, 140], dtype=float)
    milieu = np.array([232, 186, 60], dtype=float)
    bas = np.array([170, 110, 25], dtype=float)
    t = np.clip((y - 0.25) / 0.65, 0, 1)[..., None]
    degrade = np.where(t < 0.5, haut + (milieu - haut) * (t * 2), milieu + (bas - milieu) * ((t - 0.5) * 2))
    degrade = np.broadcast_to(degrade, (hauteur, largeur, 3)).copy()
    m = np.asarray(lettres, dtype=float) / 255.0
    flou = np.asarray(lettres.filter(ImageFilter.GaussianBlur(1.0 * e)), dtype=float) / 255.0
    relief = np.roll(np.roll(flou, e, 0), e, 1) - np.roll(np.roll(flou, -e, 0), -e, 1)
    degrade *= (1 - 0.35 * relief)[..., None]
    degrade = np.clip(degrade, 0, 255)

    rgb = np.asarray(fond, dtype=float)[..., :3]
    rgb = rgb * (1 - m[..., None]) + degrade * m[..., None]
    return Image.fromarray(np.dstack([rgb, alpha]).astype(np.uint8), 'RGBA')


def main():
    os.makedirs(DOSSIER, exist_ok=True)
    for nom, e in IMAGES:
        chemin = os.path.join(DOSSIER, nom)
        image(e).save(chemin, optimize=True)
        print('écrit :', chemin)


if __name__ == '__main__':
    main()
