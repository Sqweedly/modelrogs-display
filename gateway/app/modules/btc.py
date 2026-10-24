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
from datetime import datetime, timezone
from zoneinfo import ZoneInfo

import aiohttp

from .. import images
from ..cache import cache
from ..http import get_json
from ..text import JOURS_COURTS, ecran, nombre_fr
from . import Module

URL = os.environ.get("COINGECKO_URL", "https://api.coingecko.com/api/v3").rstrip("/")
CLE = os.environ.get("COINGECKO_KEY", "")
TZ = os.environ.get("TZ", "Europe/Paris")
DEVISES = {"eur": "€", "usd": "$"}
MAX_CRYPTOS = 3
POINTS = {1: 60, 2: 48, 3: 32}   # points de courbe selon la mise en page

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
    """Précision adaptée : 57 864 € ; 142,35 € ; 2,1840 € ; 0,08214 €."""
    if valeur >= 1000:
        dec = 0
    elif valeur >= 10:
        dec = 2
    elif valeur >= 0.1:
        dec = 4
    else:
        dec = 5
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


def _date_courte(t: float) -> str:
    d = datetime.fromtimestamp(t, timezone.utc).astimezone(ZoneInfo(TZ))
    return f"{JOURS_COURTS[d.weekday()].lower()} {d.day}"


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
            "price_change_percentage": "24h,7d",
        }, entetes)
        return {"t": time.time(), "c": {c["id"]: c for c in liste}}

    return await cache.obtenir(f"crypto-{devise}", 10 * 60, charger, persistant=True)


async def crypto(session: aiohttp.ClientSession, q: dict) -> dict:
    devise = q.get("devise", "eur").lower()
    if devise not in DEVISES:
        devise = "eur"
    ids = choix(q)
    donnees, perime = await _marches(session, devise)
    n_points = POINTS[len(ids)]

    sortie = []
    for cid in ids:
        c = donnees["c"].get(cid)
        if not c or c.get("current_price") is None:
            continue
        symbole, nom = CRYPTOS[cid]
        courbe = _reduire([p for p in (c.get("sparkline_in_7d") or {}).get("price", []) if p], n_points)
        if len(courbe) < 2:
            courbe = [c["current_price"]] * 2
        courbe[-1] = c["current_price"]
        bas, haut = min(courbe), max(courbe)
        ecart = (haut - bas) or 1.0
        v24, u24 = _variation(c.get("price_change_percentage_24h_in_currency")
                              or c.get("price_change_percentage_24h"))
        v7, u7 = _variation(c.get("price_change_percentage_7d_in_currency"))
        sortie.append({
            "s": symbole, "n": ecran(nom, 12),
            "px": _prix(c["current_price"], devise),
            "v24": v24, "u24": u24, "v7": v7, "u7": u7,
            # Points normalisés 0 (plus bas) .. 100 (plus haut) : l'afficheur les met à l'échelle.
            "pts": [round((v - bas) / ecart * 100) for v in courbe],
            "i": images.enregistrer(c.get("image")),
        })
    if not sortie:
        return {"ok": 0, "err": "Cours indisponibles"}

    fin = donnees["t"]
    return {
        "ok": 1,
        "c": sortie,
        "d0": _date_courte(fin - 7 * 86400),
        "d1": _date_courte(fin),
        "p": int(perime),
    }


MODULE = Module(
    id="btc",
    nom="Crypto",
    params=[
        {"k": "ids", "l": "Cryptos affichées (3 au maximum)", "t": "multi", "max": MAX_CRYPTOS, "d": "bitcoin",
         "o": [{"id": cid, "n": f"{nom} ({sym})"} for cid, (sym, nom) in CRYPTOS.items()]},
        {"k": "devise", "l": "Devise", "t": "select", "d": "eur",
         "o": [{"id": "eur", "n": "Euro (€)"}, {"id": "usd", "n": "Dollar ($)"}]},
    ],
    routes={"/btc": crypto},
)
