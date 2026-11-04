"""Photos provisoires de la page Tesla (app/icones/tesla/), 480 x 336.

Dessin générique d'une berline, sans marque ni logo, avec un repère par état
(route, panneau P, câble de charge, éclair). Chacun peut les remplacer par ses
propres photos dans le dossier monté sur /icones (tesla/route.jpg…).
Lancement, depuis gateway/ :  python tools/photos_tesla.py
"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

W, H, K = 480, 336, 4            # dessin 4 fois plus grand, puis réduit (bords lissés)
SORTIE = Path(__file__).resolve().parent.parent / "app" / "icones" / "tesla"
ETATS = [("route", "En route"), ("stationnee", "Stationnée"), ("branchee", "Branchée"), ("charge", "En charge")]


def police(nom: str, taille: int):
    try:
        return ImageFont.truetype(nom, taille * K)
    except OSError:
        return ImageFont.load_default()


def voiture(d: ImageDraw.ImageDraw, cx: int, cy: int, c: tuple) -> None:
    s = K
    d.rounded_rectangle((cx - 150 * s, cy - 18 * s, cx + 150 * s, cy + 30 * s), 22 * s, fill=c)
    d.polygon([(cx - 95 * s, cy - 16 * s), (cx - 50 * s, cy - 58 * s), (cx + 45 * s, cy - 58 * s),
               (cx + 105 * s, cy - 16 * s)], fill=c)
    vitre = (20, 24, 30)
    d.polygon([(cx - 80 * s, cy - 20 * s), (cx - 44 * s, cy - 50 * s), (cx - 4 * s, cy - 50 * s),
               (cx - 4 * s, cy - 20 * s)], fill=vitre)
    d.polygon([(cx + 4 * s, cy - 20 * s), (cx + 4 * s, cy - 50 * s), (cx + 40 * s, cy - 50 * s),
               (cx + 88 * s, cy - 20 * s)], fill=vitre)
    for x in (cx - 92 * s, cx + 92 * s):
        d.ellipse((x - 30 * s, cy + 2 * s, x + 30 * s, cy + 62 * s), fill=(12, 12, 14))
        d.ellipse((x - 14 * s, cy + 18 * s, x + 14 * s, cy + 46 * s), fill=(90, 94, 100))


def eclair(d: ImageDraw.ImageDraw, x: int, y: int, h: int, c: tuple) -> None:
    w = h * 6 // 10
    d.polygon([(x + w * 6 // 10, y), (x, y + h * 6 // 10), (x + w // 2, y + h * 6 // 10)], fill=c)
    d.polygon([(x + w // 2, y + h * 4 // 10), (x + w, y + h * 4 // 10), (x + w * 4 // 10, y + h)], fill=c)


def photo(nom: str, titre: str) -> Image.Image:
    im = Image.new("RGB", (W * K, H * K))
    d = ImageDraw.Draw(im)
    for y in range(H * K):                                   # léger dégradé
        g = int(14 + 18 * y / (H * K))
        d.line((0, y, W * K, y), fill=(g, g + 2, g + 6))
    sol = 250 * K
    d.rectangle((0, sol, W * K, H * K), fill=(28, 30, 34))
    if nom == "route":
        for x in range(0, W * K, 90 * K):
            d.rectangle((x, sol + 40 * K, x + 50 * K, sol + 48 * K), fill=(200, 200, 200))
    if nom == "stationnee":
        d.rounded_rectangle((40 * K, 40 * K, 100 * K, 100 * K), 10 * K, fill=(40, 90, 200))
        d.text((70 * K, 70 * K), "P", fill="white", font=police("DejaVuSans-Bold.ttf", 22), anchor="mm")
    cx, cy = 240 * K, 180 * K
    voiture(d, cx, cy, (140, 20, 24))
    if nom in ("branchee", "charge"):
        d.line((cx + 150 * K, cy - 4 * K, cx + 190 * K, cy - 4 * K, cx + 205 * K, sol - 10 * K),
               fill=(230, 230, 230), width=5 * K, joint="curve")
        d.rounded_rectangle((cx + 190 * K, sol - 70 * K, cx + 222 * K, sol - 6 * K), 6 * K, fill=(70, 74, 80))
    if nom == "charge":
        eclair(d, 410 * K, 30 * K, 60 * K, (0, 220, 110))
    d.text((W * K // 2, 312 * K), "Photo provisoire — " + titre, fill=(150, 150, 150),
           font=police("DejaVuSans.ttf", 15), anchor="mm")
    return im.resize((W, H), Image.LANCZOS)


if __name__ == "__main__":
    SORTIE.mkdir(parents=True, exist_ok=True)
    for nom, titre in ETATS:
        photo(nom, titre).save(SORTIE / f"{nom}.png", optimize=True)
        print(SORTIE / f"{nom}.png")
