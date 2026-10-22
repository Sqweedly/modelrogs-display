"""Mise en forme des textes destinés à l'écran de l'ESP.

Les polices embarquées dans le firmware ne contiennent qu'un jeu de caractères
limité (ASCII + lettres accentuées courantes + quelques symboles). Tout texte
envoyé à l'écran passe donc par `ecran()` : les caractères absents sont
translittérés (Ê -> E, æ -> ae), sinon supprimés (émojis...).

Ce jeu de caractères DOIT rester identique à celui de firmware/tools/make_fonts.py.
"""

import unicodedata

CHARSET = (
    "".join(chr(c) for c in range(0x20, 0x7F))
    + "àâçéèêëîïôùûüœÀÇÉÈ€°·…"
)
_OK = set(CHARSET)

# Caractères que la décomposition Unicode ne sait pas ramener à l'ASCII
_TRANSLIT = {
    "æ": "ae", "Æ": "AE", "Œ": "OE", "ß": "ss", "ø": "o", "Ø": "O",
    "ł": "l", "Ł": "L", "đ": "d", "Đ": "D", "–": "-", "—": "-",
    "‘": "'", "’": "'", "“": '"', "”": '"', "«": '"', "»": '"',
    " ": " ", " ": " ", "\t": " ",
}

JOURS = ["Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi", "Dimanche"]
JOURS_COURTS = ["Lun.", "Mar.", "Mer.", "Jeu.", "Ven.", "Sam.", "Dim."]
MOIS = [
    "janvier", "février", "mars", "avril", "mai", "juin",
    "juillet", "août", "septembre", "octobre", "novembre", "décembre",
]


def ecran(texte: str, max_len: int | None = None) -> str:
    """Rend un texte affichable par les polices de l'ESP."""
    sortie = []
    for car in str(texte or ""):
        if car in _OK:
            sortie.append(car)
        elif car in _TRANSLIT:
            sortie.append(_TRANSLIT[car])
        else:
            base = unicodedata.normalize("NFKD", car)
            sortie.append("".join(c for c in base if c in _OK and c != " "))
    resultat = " ".join("".join(sortie).split())
    if max_len and len(resultat) > max_len:
        resultat = resultat[: max_len - 1].rstrip() + "…"
    return resultat


def nombre_fr(valeur: float, decimales: int = 0) -> str:
    """57830.4 -> '57 830' ; 1.84 (2 déc.) -> '1,84'."""
    texte = f"{valeur:,.{decimales}f}"
    return texte.replace(",", " ").replace(".", ",")
