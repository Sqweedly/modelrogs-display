"""Cours du Bitcoin sur 7 jours via CoinGecko."""

import os
from datetime import datetime, timezone
from zoneinfo import ZoneInfo

import aiohttp

from ..cache import cache
from ..http import get_json
from ..text import JOURS_COURTS, nombre_fr
from . import Module

URL = os.environ.get("COINGECKO_URL", "https://api.coingecko.com/api/v3/coins/bitcoin/market_chart")
CLE = os.environ.get("COINGECKO_KEY", "")
TZ = os.environ.get("TZ", "Europe/Paris")
DEVISES = {"eur": "€", "usd": "$"}
NB_POINTS = 60


def _variation(ancien: float, nouveau: float) -> tuple[str, int]:
    pct = (nouveau - ancien) / ancien * 100 if ancien else 0.0
    signe = "+" if pct >= 0 else "-"
    return f"{signe}{nombre_fr(abs(pct), 1)} %", int(pct >= 0)


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


def _date_courte(ms: float) -> str:
    d = datetime.fromtimestamp(ms / 1000, timezone.utc).astimezone(ZoneInfo(TZ))
    return f"{JOURS_COURTS[d.weekday()].lower()} {d.day}"


async def btc(session: aiohttp.ClientSession, q: dict) -> dict:
    devise = q.get("devise", "eur").lower()
    if devise not in DEVISES:
        devise = "eur"

    async def charger() -> dict:
        entetes = {"x-cg-demo-api-key": CLE} if CLE else None
        return await get_json(session, URL, {"vs_currency": devise, "days": 7}, entetes)

    donnees, perime = await cache.obtenir(f"btc-{devise}", 10 * 60, charger, persistant=True)
    prix = donnees["prices"]  # [[horodatage ms, prix], ...]
    if len(prix) < 2:
        raise ValueError("historique BTC vide")

    actuel = prix[-1][1]
    il_y_a_24h = prix[-1][0] - 24 * 3600 * 1000
    ref_24h = next((p for t, p in prix if t >= il_y_a_24h), prix[0][1])
    courbe = _reduire([p for _, p in prix], NB_POINTS)
    bas, haut = min(courbe), max(courbe)
    ecart = (haut - bas) or 1.0
    v24, u24 = _variation(ref_24h, actuel)
    v7, u7 = _variation(prix[0][1], actuel)

    return {
        "ok": 1,
        "px": f"{nombre_fr(actuel)} {DEVISES[devise]}",
        "v24": v24, "u24": u24,
        "v7": v7, "u7": u7,
        # Points normalisés 0 (plus bas) .. 100 (plus haut) : l'afficheur les met à l'échelle.
        "pts": [round((v - bas) / ecart * 100) for v in courbe],
        "d0": _date_courte(prix[0][0]),
        "d1": _date_courte(prix[-1][0]),
        "p": int(perime),
    }


MODULE = Module(
    id="btc",
    nom="Bitcoin",
    params=[{
        "k": "devise", "l": "Devise", "t": "select", "d": "eur",
        "o": [{"id": "eur", "n": "Euro (€)"}, {"id": "usd", "n": "Dollar ($)"}],
    }],
    routes={"/btc": btc},
)
