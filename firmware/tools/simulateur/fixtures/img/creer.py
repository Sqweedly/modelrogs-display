"""Crée les images FICTIVES du simulateur (affiches, avatars, icône, logos de
remplacement). Sur l'afficheur, ces images viennent de Plex, Discord et CoinGecko."""
import os
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ICI = Path(__file__).resolve().parent
# Police Inter Black (facultative) : chemin dans la variable INTER_TTF
TTF = Path(os.environ.get("INTER_TTF", "Inter-Black.ttf"))

def police(t):
    try:
        return ImageFont.truetype(str(TTF), t)
    except OSError:
        return ImageFont.load_default()

def degrade(w, h, c1, c2):
    im = Image.new("RGB", (w, h)); d = ImageDraw.Draw(im)
    for y in range(h):
        t = y / h
        d.line((0, y, w, y), fill=tuple(int(a + (b - a) * t) for a, b in zip(c1, c2)))
    return im

# Affiche fictive : dégradé, « phare » stylisé, titre
a = degrade(300, 450, (0x5A, 0x1E, 0x8C), (0x10, 0x0C, 0x30)); d = ImageDraw.Draw(a)
d.ellipse((190, 40, 250, 100), fill=(255, 214, 120))
d.polygon([(130, 420), (170, 420), (160, 200), (140, 200)], fill=(230, 230, 240))
d.polygon([(150, 210), (300, 150), (300, 270)], fill=(255, 240, 180, 80))
d.rectangle((0, 400, 300, 450), fill=(20, 30, 60))
d.text((150, 330), "PHARE", font=police(54), fill=(255, 255, 255), anchor="mm")
a.save(ICI / "affiche1.jpg", quality=90)

gens = {"alex": ((0x7C, 0x4D, 0xFF), (0x3A, 0x1E, 0x8C)), "sam": ((0x00, 0xB8, 0x5C), (0x00, 0x4A, 0x26)),
        "nico": ((0xFF, 0x8C, 0x00), (0x8C, 0x2E, 0x00)), "lea": ((0xFF, 0x4D, 0x8C), (0x8C, 0x10, 0x3A)),
        "max": ((0x1E, 0x90, 0xFF), (0x0A, 0x2E, 0x6E)), "jo": ((0x00, 0xC2, 0xE8), (0x00, 0x4A, 0x5A)),
        "tom": ((0xC8, 0xD2, 0xE0), (0x50, 0x5A, 0x6E))}
for nom, (c1, c2) in gens.items():
    im = degrade(128, 128, c1, c2); d = ImageDraw.Draw(im)
    d.ellipse((34, 22, 94, 82), fill=(255, 255, 255, 200))       # silhouette
    d.ellipse((14, 86, 114, 186), fill=(255, 255, 255, 200))
    im.save(ICI / f"av-{nom}.png")

ic = degrade(128, 128, (0x2B, 0x8A, 0x6E), (0x5B, 0x4A, 0xBE)); d = ImageDraw.Draw(ic)
d.text((64, 66), "R", font=police(80), fill=(255, 255, 255), anchor="mm")
ic.save(ICI / "serveur.png")

# Logos de REMPLACEMENT (pastille + lettre), transparents autour comme ceux de CoinGecko
for nom, coul, lettre in (("logo-btc", (247, 147, 26), "B"), ("logo-eth", (98, 126, 234), "E"),
                          ("logo-xrp", (35, 41, 47), "X")):
    im = Image.new("RGBA", (250, 250), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.ellipse((0, 0, 249, 249), fill=coul + (255,))
    d.text((125, 130), lettre, font=police(150), fill=(255, 255, 255), anchor="mm")
    im.save(ICI / f"{nom}.png")
