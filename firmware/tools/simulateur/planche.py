"""Assemble les captures out/*.ppm en une planche planche.png (Pillow requis).
Les 3 lignes du haut et du bas, masquées par le boîtier, sont teintées en rouge."""
import glob
from PIL import Image, ImageDraw

fichiers = sorted(glob.glob("out/*.ppm"))
ECH, MARGE, LIB, COLS = 2, 16, 22, 4
lignes = (len(fichiers) + COLS - 1) // COLS
planche = Image.new("RGB", (COLS * (240 * ECH + MARGE) + MARGE, lignes * (240 * ECH + MARGE + LIB) + MARGE), (40, 44, 52))
dessin = ImageDraw.Draw(planche)
for k, f in enumerate(fichiers):
    x = MARGE + (k % COLS) * (240 * ECH + MARGE)
    y = MARGE + (k // COLS) * (240 * ECH + MARGE + LIB)
    dessin.text((x, y), f.split("/")[-1][:-4], fill=(220, 220, 220))
    ecran = Image.open(f).convert("RGB")
    # Bandes masquées par le boîtier (3 px en haut et en bas), en rouge translucide
    voile = Image.new("RGB", (240, 3), (255, 0, 0))
    for bande in (0, 237):
        zone = ecran.crop((0, bande, 240, bande + 3))
        ecran.paste(Image.blend(zone, voile, 0.55), (0, bande))
    planche.paste(ecran.resize((240 * ECH, 240 * ECH), Image.NEAREST), (x, y + LIB))
planche.save("planche.png")
print("planche.png :", len(fichiers), "écrans")
