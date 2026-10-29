"""Génère les pastilles des devises et des indices boursiers.

  gateway/app/icones/devises/<CODE>.png   (symbole de la monnaie : €, £, C$…)
  gateway/app/icones/indices/<SYMBOLE>.png (nom court de l'indice : CAC, DAX…)

Chaque pastille : un disque de couleur avec le symbole ou le nom court, en PNG
128 x 128 à fond transparent. Les couleurs sont choisies pour que deux
pastilles d'une même page ne se ressemblent pas.

Usage, depuis la racine du dépôt :
    pip install pillow
    python gateway/tools/icones_devises.py <Inter-Black.otf ou .ttf> <police CJK>

- Inter 4.x : https://github.com/rsms/inter/releases (extras/otf ou extras/ttf) ;
- police CJK, pour le seul « 元 » du yuan : Noto Serif CJK (ex. paquet
  fonts-noto-cjk, fichier NotoSerifCJK-Bold.ttc).
Les deux polices sont sous licence SIL Open Font License.

Pour ajouter une devise ou un indice : une ligne dans DEVISES ou INDICES,
puis relancer le script (il réécrit les deux dossiers).
"""

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ICONES = Path(__file__).resolve().parents[1] / "app" / "icones"
TAILLE = 128
SURECH = 4                   # dessin 4x plus grand puis réduit : bords lissés
LARGEUR_MAX = 0.84           # le symbole occupe au plus 84 % du disque

# code ISO : (symbole, couleur du disque, couleur du symbole)
DEVISES = {
    "USD": ("$", "#1E8E3E", "#FFFFFF"),
    "EUR": ("€", "#1F4FB4", "#FFD43B"),
    "GBP": ("£", "#6A2C91", "#FFFFFF"),
    "JPY": ("¥", "#E8452C", "#FFFFFF"),
    "CHF": ("Fr", "#C0392B", "#FFFFFF"),
    "CNY": ("元", "#F5B700", "#8B0000"),
    "CAD": ("C$", "#E84393", "#FFFFFF"),
    "AUD": ("A$", "#00A6A6", "#FFFFFF"),
    "HKD": ("HK$", "#8E44AD", "#FFFFFF"),
    "SGD": ("S$", "#FF7F50", "#FFFFFF"),
    "INR": ("₹", "#F28C28", "#FFFFFF"),
    "KRW": ("₩", "#3A86FF", "#FFFFFF"),
    "BRL": ("R$", "#2ECC71", "#14213D"),
    "MXN": ("Mex$", "#7B2D26", "#FFE8A3"),
    "ZAR": ("R", "#3D5A40", "#FFB612"),
    "TRY": ("₺", "#00B4D8", "#FFFFFF"),
    "NZD": ("NZ$", "#14213D", "#E5E5E5"),
    "SEK": ("kr", "#006AA7", "#FECC00"),
    "NOK": ("kr", "#5E548E", "#FFFFFF"),
    "DKK": ("kr", "#B23A48", "#FFFFFF"),
    "PLN": ("zł", "#F72585", "#FFFFFF"),
    "CZK": ("Kč", "#264653", "#E9C46A"),
    "HUF": ("Ft", "#6A994E", "#FFFFFF"),
    "ILS": ("₪", "#4361EE", "#FFFFFF"),
    "AED": ("Dh", "#2A9D8F", "#FFFFFF"),
    "SAR": ("SR", "#606C38", "#FEFAE0"),
    "THB": ("฿", "#9D4EDD", "#FFFFFF"),
    "MYR": ("RM", "#023E8A", "#FFCC00"),
    "IDR": ("Rp", "#D62828", "#FFFFFF"),
    "PHP": ("₱", "#FFB703", "#023047"),
    "TWD": ("NT$", "#8D6E63", "#FFFFFF"),
}
# Indices : symbole Yahoo sans « ^ » -> (nom court, couleur du disque, couleur du texte)
INDICES = {
    "FCHI": ("CAC", "#1F4FB4", "#FFFFFF"),
    "GSPC": ("S&P", "#1E8E3E", "#FFFFFF"),
    "IXIC": ("NDQ", "#00A6A6", "#FFFFFF"),
    "DJI": ("DOW", "#14213D", "#FFD43B"),
    "GDAXI": ("DAX", "#D62828", "#FFD43B"),
    "FTSE": ("FTSE", "#6A2C91", "#FFFFFF"),
    "STOXX50E": ("SX5E", "#F28C28", "#FFFFFF"),
    "N225": ("NKY", "#E84393", "#FFFFFF"),
    "HSI": ("HSI", "#8D6E63", "#FFFFFF"),
    "SSMI": ("SMI", "#C0392B", "#FFFFFF"),
}
TAILLE_TEXTE = {1: 72, 2: 56, 3: 42, 4: 34}   # en % du disque, selon le nombre de caractères


def pastille(symbole: str, fond: str, encre: str, police: str) -> Image.Image:
    w = TAILLE * SURECH
    image = Image.new("RGBA", (w, w), (0, 0, 0, 0))
    dessin = ImageDraw.Draw(image)
    dessin.ellipse((0, 0, w - 1, w - 1), fill=fond)
    taille = TAILLE_TEXTE[min(len(symbole), 4)] * w // 100
    f = ImageFont.truetype(police, taille)
    while f.getbbox(symbole)[2] - f.getbbox(symbole)[0] > w * LARGEUR_MAX:
        taille -= SURECH * 4
        f = ImageFont.truetype(police, taille)
    dessin.text((w / 2, w / 2), symbole, font=f, fill=encre, anchor="mm")
    return image.resize((TAILLE, TAILLE), Image.LANCZOS)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    inter, cjk = sys.argv[1], sys.argv[2]
    for dossier, table in (("devises", DEVISES), ("indices", INDICES)):
        sortie = ICONES / dossier
        sortie.mkdir(parents=True, exist_ok=True)
        for ancien in sortie.glob("*.png"):
            ancien.unlink()
        for code, (symbole, fond, encre) in table.items():
            police = cjk if any(ord(c) > 0x2E80 for c in symbole) else inter
            pastille(symbole, fond, encre, police).save(sortie / f"{code}.png", optimize=True)
        print(f"{len(table)} pastilles écrites dans {sortie}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
