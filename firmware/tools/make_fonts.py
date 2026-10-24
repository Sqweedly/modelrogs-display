#!/usr/bin/env python3
"""Génère les polices lissées (format VLW de TFT_eSPI) à partir d'Inter.

Usage :
    pip install freetype-py
    python tools/make_fonts.py <dossier contenant Inter-ExtraBold.ttf et Inter-Black.ttf>

Produit src/fonts/inter_fonts.h : un tableau PROGMEM par taille, chargé par
tft.loadFont(...). Le jeu de caractères complet DOIT rester identique à
CHARSET dans gateway/app/text.py (la passerelle translittère le reste) (la passerelle filtre les textes avec).

Format VLW (entiers 32 bits gros-boutistes) :
  en-tête : nb glyphes, version (11), taille, 0, ascent, descent
  par glyphe (7 entiers) : unicode, hauteur, largeur, avance, dY, dX, 0
  puis les bitmaps 8 bits (alpha) de chaque glyphe, dans le même ordre.
"""

import struct
import sys
from pathlib import Path

import freetype

COMPLET = (
    "".join(chr(c) for c in range(0x20, 0x7F))
    + "àâçéèêëîïôùûüœÀÇÉÈ€°·…"
)
CHIFFRES = "0123456789"

# nom, fichier, taille en px, caractères
POLICES = [
    ("F11", "Inter-ExtraBold.ttf", 12, COMPLET),
    ("F13", "Inter-ExtraBold.ttf", 14, COMPLET),
    ("F16", "Inter-ExtraBold.ttf", 16, COMPLET),
    ("F18", "Inter-Black.ttf", 18, COMPLET),
    ("F32", "Inter-Black.ttf", 32, CHIFFRES + "ABCDEFGHIJKLMNOPQRSTUVWXYZ €$.,+-%/:…"),
    ("F52", "Inter-Black.ttf", 52, CHIFFRES + "°-/ "),
    ("F76", "Inter-Black.ttf", 76, CHIFFRES + ":"),
]


def vlw(chemin: Path, taille: int, caracteres: str) -> bytes:
    face = freetype.Face(str(chemin))
    face.set_pixel_sizes(0, taille)
    drapeaux = freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_LIGHT

    def metrique(car: str) -> tuple[int, int]:
        face.load_char(car, drapeaux)
        g = face.glyph
        return g.bitmap_top, g.bitmap.rows - g.bitmap_top

    ascent = metrique("d")[0] if face.get_char_index("d") else taille
    descent = metrique("p")[1] if face.get_char_index("p") else 0

    entetes, bitmaps = [], []
    for car in dict.fromkeys(caracteres):  # sans doublon, ordre conservé
        if not face.get_char_index(car):
            print(f"  ! glyphe absent : {car!r}", file=sys.stderr)
            continue
        face.load_char(car, drapeaux)
        g = face.glyph
        b = g.bitmap
        lignes = []
        for y in range(b.rows):
            debut = y * b.pitch
            lignes.append(bytes(b.buffer[debut: debut + b.width]))
        avance = round(g.advance.x / 64)
        if not (b.rows < 256 and b.width < 256 and avance < 256):
            raise ValueError(f"glyphe trop grand : {car!r}")
        entetes.append(struct.pack(
            ">7i", ord(car), b.rows, b.width, avance, g.bitmap_top, g.bitmap_left, 0))
        bitmaps.append(b"".join(lignes))

    tete = struct.pack(">6i", len(entetes), 11, taille, 0, ascent, descent)
    return tete + b"".join(entetes) + b"".join(bitmaps)


def tableau_c(nom: str, donnees: bytes) -> str:
    lignes = []
    for i in range(0, len(donnees), 24):
        lignes.append("  " + ",".join(f"0x{o:02X}" for o in donnees[i: i + 24]) + ",")
    return f"const uint8_t {nom}[] PROGMEM = {{\n" + "\n".join(lignes) + "\n};\n"


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    source = Path(sys.argv[1])
    sortie = Path(__file__).resolve().parent.parent / "src" / "fonts" / "inter_fonts.h"
    sortie.parent.mkdir(parents=True, exist_ok=True)

    morceaux = [
        "// Généré par tools/make_fonts.py — ne pas modifier à la main.\n"
        "// Police Inter (c) The Inter Project Authors, licence SIL OFL 1.1 (voir OFL.txt).\n"
        "#pragma once\n#include <Arduino.h>\n\n"
    ]
    total = 0
    for nom, fichier, taille, caracteres in POLICES:
        donnees = vlw(source / fichier, taille, caracteres)
        total += len(donnees)
        print(f"{nom}: {fichier} {taille}px, {len(donnees) / 1024:.1f} Ko")
        morceaux.append(tableau_c(nom, donnees) + "\n")
    sortie.write_text("".join(morceaux), encoding="utf-8")
    print(f"Total : {total / 1024:.1f} Ko -> {sortie}")


if __name__ == "__main__":
    main()
