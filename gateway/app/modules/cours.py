"""Outils communs aux pages de cours : Crypto, Actions, Devises, Commodities.

Sélection (3 valeurs au plus), dans l'ordre des champs de l'afficheur :
  ?s1=…&s2=…&s3=…  les trois listes déroulantes ;
  ?m=…              la saisie manuelle (une ou plusieurs valeurs, séparées
                    par des virgules ou des espaces).
Si l'afficheur n'envoie rien, la passerelle prend les valeurs de la variable
d'environnement de la page (CRYPTOS, ACTIONS, DEVISES, COMMODITIES).

Tous les prix sont en dollars.
"""

import os
import re

from ..text import nombre_fr

MAX_VALEURS = 3
POINTS_30J = 60          # points de la courbe sur 30 jours
SEUIL_DEDIEE = 5.0       # |variation 7 j| >= 5 % : page dédiée (30 jours) sur l'afficheur

NOTE = "3 au plus, dans l'ordre : listes 1 à 3, puis saisie"


def valeurs_saisies(q: dict) -> list[str]:
    """Valeurs envoyées par l'afficheur, dans l'ordre des champs (sans doublon)."""
    brutes = [q.get("s1", ""), q.get("s2", ""), q.get("s3", "")]
    brutes += re.split(r"[,;\s]+", q.get("m", ""))
    sortie = []
    for v in brutes:
        v = v.strip()
        if v and v.upper() not in (x.upper() for x in sortie):
            sortie.append(v)
    return sortie


def valeurs_compose(variable: str, defaut: str) -> list[str]:
    brut = os.environ.get(variable, "").strip() or defaut
    return [v for v in (x.strip() for x in re.split(r"[,;\s]+", brut)) if v]


def selection(q: dict, variable: str, defaut: str) -> list[str]:
    """3 valeurs au plus : celles de l'afficheur, sinon celles du docker-compose."""
    return (valeurs_saisies(q) or valeurs_compose(variable, defaut))[:MAX_VALEURS]


def variation(pct: float | None) -> tuple[str, int]:
    pct = pct or 0.0
    signe = "+" if pct >= 0 else "-"
    return f"{signe}{nombre_fr(abs(pct), 1)} %", int(pct >= 0)


def prix(valeur: float) -> str:
    """En dollars, au moins 4 chiffres, 2 décimales quand elles suffisent :
    57 864 $ ; 142,35 $ ; 2,184 $ ; 0,08214 $."""
    if valeur >= 1000:
        dec = 0
    elif valeur >= 10:
        dec = 2
    elif valeur >= 1:
        dec = 3
    elif valeur > 0:
        dec = 3
        while valeur * 10 ** dec < 1000 and dec < 10:
            dec += 1
    else:
        dec = 2
    return f"{nombre_fr(valeur, dec)} $"


def reduire(valeurs: list[float], n: int) -> list[float]:
    """Moyenne par paquets pour garder n points (le dernier point reste exact)."""
    if len(valeurs) <= n:
        return valeurs
    taille = len(valeurs) / n
    sortie = []
    for i in range(n):
        paquet = valeurs[int(i * taille): max(int((i + 1) * taille), int(i * taille) + 1)]
        sortie.append(sum(paquet) / len(paquet))
    sortie[-1] = valeurs[-1]
    return sortie


def normaliser(valeurs: list[float], actuel: float, n: int = POINTS_30J) -> list[int]:
    """Courbe réduite à n points, normalisée de 0 (plus bas) à 100 (plus haut)."""
    courbe = reduire([v for v in valeurs if v], n) or [actuel, actuel]
    if len(courbe) < 2:
        courbe = [actuel] * 2
    courbe[-1] = actuel
    bas, haut = min(courbe), max(courbe)
    ecart = (haut - bas) or 1.0
    return [round((v - bas) / ecart * 100) for v in courbe]


def code_court(symbole: str) -> str:
    """Code affiché dans la pastille de remplacement : « MC.PA » -> « MC »."""
    s = re.split(r"[.=]", symbole.lstrip("^"))[0]
    return s.upper()[:5] or "?"


def element(ident: str, code: str, valeur: float, pct24: float | None, pct7: float | None,
            pct30: float | None, image: str, forme: int) -> dict:
    """Élément prêt à afficher. forme : 0 logo rond (crypto), 1 logo posé sur un
    disque (actions, icônes), 2 image découpée en rond (drapeaux, lingots)."""
    v24, u24 = variation(pct24)
    v7, u7 = variation(pct7)
    v30, u30 = variation(pct30)
    return {
        "id": ident, "s": code, "px": prix(valeur),
        "v24": v24, "u24": u24, "v7": v7, "u7": u7, "v30": v30, "u30": u30,
        "x": int(abs(pct7 or 0.0) >= SEUIL_DEDIEE),
        "i": image, "f": forme,
    }


def introuvable(ident: str, code: str, indisponible: bool = False) -> dict:
    """Valeur saisie inconnue (e = 1) ou momentanément sans cours (e = 2)."""
    return {"id": ident, "s": code, "e": 2 if indisponible else 1}


def params(exemple: str) -> list[dict]:
    """Champs de l'interface : 3 listes (options « o » ou « src » du module, voir
    `manifeste`) et une saisie manuelle, en grille de 2 colonnes (« g »)."""
    return [
        {"k": "s1", "t": "select", "g": 1, "ph": "1. —"},
        {"k": "s2", "t": "select", "g": 1, "ph": "2. —"},
        {"k": "s3", "t": "select", "g": 1, "ph": "3. —"},
        {"k": "m", "t": "text", "g": 1, "ph": exemple},
    ]


def manifeste(variable: str, defaut: str, options: list[dict] | None = None,
              src: str = "") -> dict:
    """Compléments du manifeste /modules : liste commune aux 3 listes déroulantes,
    consigne, et valeurs du docker-compose utilisées si rien n'est choisi."""
    extra = {"nt": NOTE, "df": ", ".join(valeurs_compose(variable, defaut))}
    if options is not None:
        extra["o"] = options
    if src:
        extra["src"] = src
    return extra
