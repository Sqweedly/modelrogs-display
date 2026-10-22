"""Assemble les captures out/*.ppm en une planche planche.png (Pillow requis)."""
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
    planche.paste(Image.open(f).resize((240 * ECH, 240 * ECH), Image.NEAREST), (x, y + LIB))
planche.save("planche.png")
print("planche.png :", len(fichiers), "écrans")
