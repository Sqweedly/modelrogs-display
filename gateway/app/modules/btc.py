"""Cryptomonnaies sur 7 jours via CoinGecko (1 à 3 au choix, logos officiels).

Une seule requête /coins/markets ramène toutes les cryptos proposées
(prix, variations 24 h et 7 jours, courbe horaire sur 7 jours, logo) :
quel que soit le choix fait sur l'afficheur, CoinGecko n'est interrogé
qu'une fois toutes les 10 minutes par devise.

Le module garde l'identifiant « btc » (page et route /btc) pour rester
compatible avec les afficheurs et les docker-compose existants.
"""

import os
import time

import aiohttp

from .. import images
from ..cache import cache
from ..http import get_json
from ..text import ecran, nombre_fr
from . import Module

URL = os.environ.get("COINGECKO_URL", "https://api.coingecko.com/api/v3").rstrip("/")
CLE = os.environ.get("COINGECKO_KEY", "")
DEVISES = {"eur": "€", "usd": "$"}
MAX_CRYPTOS = 3
POINTS_30J = 60          # points de la courbe sur 30 jours
SEUIL_DEDIEE = 5.0       # |variation 7 j| >= 5 % : page dédiée (30 jours) sur l'afficheur

# Cryptos proposées sur l'afficheur : identifiant CoinGecko -> (symbole, nom)
CRYPTOS = {
    "bitcoin": ("BTC", "Bitcoin"),
    "ethereum": ("ETH", "Ethereum"),
    "ripple": ("XRP", "XRP"),
    "solana": ("SOL", "Solana"),
    "binancecoin": ("BNB", "BNB"),
    "cardano": ("ADA", "Cardano"),
    "dogecoin": ("DOGE", "Dogecoin"),
    "tron": ("TRX", "TRON"),
    "litecoin": ("LTC", "Litecoin"),
    "polkadot": ("DOT", "Polkadot"),
}


def _variation(pct: float | None) -> tuple[str, int]:
    pct = pct or 0.0
    signe = "+" if pct >= 0 else "-"
    return f"{signe}{nombre_fr(abs(pct), 1)} %", int(pct >= 0)


def _prix(valeur: float, devise: str) -> str:
    """Au moins 4 chiffres, 2 décimales quand elles suffisent :
    57 864 € ; 142,35 € ; 12,35 € ; 2,184 € ; 0,08214 €."""
    if valeur >= 1000:
        dec = 0
    elif valeur >= 10:
        dec = 2
    elif valeur >= 1:
        dec = 3
    elif valeur > 0:
        # chiffres significatifs : 0,08214 (4 chiffres après les zéros de tête)
        dec = 3
        while valeur * 10 ** dec < 1000 and dec < 10:
            dec += 1
    else:
        dec = 2
    return f"{nombre_fr(valeur, dec)} {DEVISES[devise]}"


def _reduire(valeurs: list[float], n: int) -> list[float]:
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


def choix(q: dict) -> list[str]:
    """?ids=bitcoin,ethereum (ordre conservé, 3 au plus) ; Bitcoin par défaut."""
    ids = []
    for x in q.get("ids", "").split(","):
        x = x.strip().lower()
        if x in CRYPTOS and x not in ids:
            ids.append(x)
    return ids[:MAX_CRYPTOS] or ["bitcoin"]


async def _marches(session: aiohttp.ClientSession, devise: str) -> tuple[dict, bool]:
    async def charger() -> dict:
        entetes = {"x-cg-demo-api-key": CLE} if CLE else None
        liste = await get_json(session, f"{URL}/coins/markets", {
            "vs_currency": devise,
            "ids": ",".join(CRYPTOS),
            "sparkline": "true",
            "price_change_percentage": "24h,7d,30d",
        }, entetes)
        return {"t": time.time(), "c": {c["id"]: c for c in liste}}

    return await cache.obtenir(f"crypto-{devise}", 10 * 60, charger, persistant=True)


def _normaliser(valeurs: list[float], n: int, actuel: float) -> list[int]:
    """Courbe réduite à n points, normalisée de 0 (plus bas) à 100 (plus haut)."""
    courbe = _reduire([v for v in valeurs if v], n) or [actuel, actuel]
    if len(courbe) < 2:
        courbe = [actuel] * 2
    courbe[-1] = actuel
    bas, haut = min(courbe), max(courbe)
    ecart = (haut - bas) or 1.0
    return [round((v - bas) / ecart * 100) for v in courbe]


async def _courbe_30j(session: aiohttp.ClientSession, cid: str, devise: str) -> list[float]:
    """Prix horaires sur 30 jours (CoinGecko market_chart), gardés 30 min."""
    async def charger() -> list[float]:
        entetes = {"x-cg-demo-api-key": CLE} if CLE else None
        donnees = await get_json(session, f"{URL}/coins/{cid}/market_chart",
                                 {"vs_currency": devise, "days": 30}, entetes)
        return [p for _, p in donnees.get("prices", [])]

    valeurs, _ = await cache.obtenir(f"crypto30-{cid}-{devise}", 30 * 60, charger, persistant=True)
    return valeurs


def _element(cid: str, c: dict, devise: str) -> dict:
    symbole, nom = CRYPTOS[cid]
    v24, u24 = _variation(c.get("price_change_percentage_24h_in_currency")
                          or c.get("price_change_percentage_24h"))
    pct7 = c.get("price_change_percentage_7d_in_currency") or 0.0
    v7, u7 = _variation(pct7)
    v30, u30 = _variation(c.get("price_change_percentage_30d_in_currency"))
    return {
        "id": cid, "s": symbole, "n": ecran(nom, 12),
        "px": _prix(c["current_price"], devise),
        "v24": v24, "u24": u24, "v7": v7, "u7": u7, "v30": v30, "u30": u30,
        # Page dédiée (30 jours) si la variation sur 7 jours atteint +/- 5 %
        "x": int(abs(pct7) >= SEUIL_DEDIEE),
        "i": images.enregistrer(c.get("image")),
    }


def _devise(q: dict) -> str:
    devise = q.get("devise", "eur").lower()
    return devise if devise in DEVISES else "eur"


async def crypto(session: aiohttp.ClientSession, q: dict) -> dict:
    """Cryptos choisies. Une seule : courbe sur 30 jours incluse (« p30 »).
    Deux ou trois : prix et variations, « x » = page dédiée à afficher."""
    devise = _devise(q)
    ids = choix(q)
    donnees, perime = await _marches(session, devise)

    sortie = []
    for cid in ids:
        c = donnees["c"].get(cid)
        if c and c.get("current_price") is not None:
            sortie.append(_element(cid, c, devise))
    if not sortie:
        return {"ok": 0, "err": "Cours indisponibles"}
    if len(sortie) == 1:
        e = sortie[0]
        try:
            e["p30"] = _normaliser(await _courbe_30j(session, e["id"], devise), POINTS_30J,
                                   donnees["c"][e["id"]]["current_price"])
        except Exception:  # noqa: BLE001 — sans courbe, la page reste utile
            e["p30"] = []
    return {"ok": 1, "c": sortie, "p": int(perime)}


async def trente_jours(session: aiohttp.ClientSession, q: dict) -> dict:
    """Une crypto sur 30 jours (page dédiée) : ?id=ripple&devise=eur."""
    devise = _devise(q)
    cid = q.get("id", "").strip().lower()
    if cid not in CRYPTOS:
        return {"ok": 0, "err": "Crypto inconnue"}
    donnees, perime = await _marches(session, devise)
    c = donnees["c"].get(cid)
    if not c or c.get("current_price") is None:
        return {"ok": 0, "err": "Cours indisponibles"}
    e = _element(cid, c, devise)
    e["p30"] = _normaliser(await _courbe_30j(session, cid, devise), POINTS_30J, c["current_price"])
    return {"ok": 1, "c": [e], "p": int(perime)}


MODULE = Module(
    id="btc",
    nom="Crypto",
    params=[
        {"k": "ids", "l": "Cryptos affichées (3 au maximum)", "t": "multi", "max": MAX_CRYPTOS, "d": "bitcoin",
         "o": [{"id": cid, "n": f"{nom} ({sym})"} for cid, (sym, nom) in CRYPTOS.items()]},
        {"k": "devise", "l": "Devise", "t": "select", "d": "eur",
         "o": [{"id": "eur", "n": "Euro (€)"}, {"id": "usd", "n": "Dollar ($)"}]},
    ],
    routes={"/btc": crypto, "/btc/30j": trente_jours},
)
