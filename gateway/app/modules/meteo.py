"""Météo, heure locale et lever/coucher du soleil via Open-Meteo (gratuit, sans clé)."""

import os
import time
from datetime import datetime, timezone
from zoneinfo import ZoneInfo

import aiohttp

from ..cache import SourceIndisponible, cache
from ..http import get_json
from ..text import JOURS, JOURS_COURTS, MOIS, ecran
from . import Module

GEOCODAGE = os.environ.get("OPEN_METEO_GEO_URL", "https://geocoding-api.open-meteo.com/v1/search")
PREVISIONS = os.environ.get("OPEN_METEO_URL", "https://api.open-meteo.com/v1/forecast")

TZ_DEFAUT = os.environ.get("TZ", "Europe/Paris")
VILLE_DEFAUT = os.environ.get("VILLE_DEFAUT", "Paris")

# Codes d'icônes partagés avec le firmware (ui.cpp : weatherIcon)
SOLEIL, ECLAIRCIES, NUAGEUX, BROUILLARD, PLUIE, NEIGE, ORAGE, LUNE, LUNE_NUAGE = range(9)

# Code WMO -> (icône de jour, libellé)
WMO = {
    0: (SOLEIL, "Ensoleillé"), 1: (ECLAIRCIES, "Peu nuageux"), 2: (ECLAIRCIES, "Éclaircies"),
    3: (NUAGEUX, "Couvert"), 45: (BROUILLARD, "Brouillard"), 48: (BROUILLARD, "Brouillard givrant"),
    51: (PLUIE, "Bruine"), 53: (PLUIE, "Bruine"), 55: (PLUIE, "Bruine dense"),
    56: (PLUIE, "Bruine verglaçante"), 57: (PLUIE, "Bruine verglaçante"),
    61: (PLUIE, "Pluie faible"), 63: (PLUIE, "Pluie"), 65: (PLUIE, "Forte pluie"),
    66: (PLUIE, "Pluie verglaçante"), 67: (PLUIE, "Pluie verglaçante"),
    71: (NEIGE, "Neige faible"), 73: (NEIGE, "Neige"), 75: (NEIGE, "Forte neige"),
    77: (NEIGE, "Grains de neige"), 80: (PLUIE, "Averses"), 81: (PLUIE, "Averses"),
    82: (PLUIE, "Fortes averses"), 85: (NEIGE, "Averses de neige"), 86: (NEIGE, "Averses de neige"),
    95: (ORAGE, "Orage"), 96: (ORAGE, "Orage et grêle"), 99: (ORAGE, "Orage et grêle"),
}


def _icone(code: int, jour: bool = True) -> tuple[int, str]:
    icone, libelle = WMO.get(int(code), (NUAGEUX, "Variable"))
    if not jour:
        if icone == SOLEIL:
            return LUNE, "Dégagé"
        if icone == ECLAIRCIES:
            return LUNE_NUAGE, libelle
    return icone, libelle


class VilleInconnue(ValueError):
    """La ville demandée n'existe pas pour le service de géocodage."""


# Villes introuvables, mémorisées 1 h pour ne pas réinterroger l'API à chaque
# appel de l'afficheur (faute de frappe dans le réglage, par exemple).
INCONNUE_TTL = 3600
INCONNUES_MAX = 200
_inconnues: dict[str, float] = {}


def normaliser_ville(ville: str) -> str:
    """« Lyon », «  lyon » et « LYON » désignent la même entrée de cache."""
    return " ".join((ville or "").split()).casefold()[:64]


async def geocoder(session: aiohttp.ClientSession, ville: str) -> dict:
    """Ville -> {nom, lat, lon, tz}. Conservé 1 an (cache disque)."""
    ville = " ".join((ville or "").split()) or VILLE_DEFAUT
    cle = normaliser_ville(ville)

    depuis = _inconnues.get(cle)
    if depuis is not None:
        if time.time() - depuis < INCONNUE_TTL:
            raise VilleInconnue(ville)
        del _inconnues[cle]

    async def charger() -> dict:
        donnees = await get_json(session, GEOCODAGE, {"name": ville, "count": 1, "language": "fr"})
        resultats = donnees.get("results") or []
        if not resultats:
            raise VilleInconnue(ville)
        res = resultats[0]
        return {"nom": res.get("name", ville), "lat": res["latitude"], "lon": res["longitude"],
                "tz": res.get("timezone") or TZ_DEFAUT}

    try:
        lieu, _ = await cache.obtenir(f"geo-{cle}", 365 * 86400, charger, persistant=True)
    except SourceIndisponible as exc:
        if isinstance(exc.__cause__, VilleInconnue):
            if len(_inconnues) >= INCONNUES_MAX:
                _inconnues.pop(next(iter(_inconnues)))   # la plus ancienne
            _inconnues[cle] = time.time()
            raise VilleInconnue(ville) from None
        raise
    return lieu


async def _previsions(session: aiohttp.ClientSession, lieu: dict) -> tuple[dict, bool]:
    async def charger() -> dict:
        return await get_json(session, PREVISIONS, {
            "latitude": lieu["lat"],
            "longitude": lieu["lon"],
            "current": "temperature_2m,weather_code,is_day",
            "daily": "weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset",
            "timezone": lieu["tz"],
            "forecast_days": 4,
        })

    return await cache.obtenir(f"meteo-{lieu['lat']:.2f}-{lieu['lon']:.2f}", 15 * 60, charger)


async def meteo(session: aiohttp.ClientSession, q: dict) -> dict:
    lieu = await geocoder(session, q.get("ville", ""))
    donnees, perime = await _previsions(session, lieu)

    actuel, jours = donnees["current"], donnees["daily"]
    icone, libelle = _icone(actuel["weather_code"], bool(actuel.get("is_day", 1)))
    prevision = []
    for i in range(1, 4):
        date = datetime.fromisoformat(jours["time"][i])
        prevision.append({
            "d": JOURS_COURTS[date.weekday()],
            "i": _icone(jours["weather_code"][i])[0],
            "mx": round(jours["temperature_2m_max"][i]),
            "mn": round(jours["temperature_2m_min"][i]),
        })
    return {
        "ok": 1,
        "v": ecran(lieu["nom"], 18),
        "t": round(actuel["temperature_2m"]),
        "l": ecran(libelle, 20),
        "i": icone,
        "mx": round(jours["temperature_2m_max"][0]),
        "mn": round(jours["temperature_2m_min"][0]),
        "j": prevision,
        "p": int(perime),
    }


async def heure(session: aiohttp.ClientSession, q: dict) -> dict:
    """Heure locale de la ville + lever/coucher du soleil (route commune /time).

    L'afficheur calcule l'heure lui-même entre deux synchronisations :
    heure locale = e (epoch UTC) + o (décalage en secondes).
    Toujours disponible : elle sert aussi à la plage nocturne.
    """
    lever = coucher = ""
    try:
        lieu = await geocoder(session, q.get("ville", ""))
        tz = ZoneInfo(lieu["tz"])
        try:
            donnees, _ = await _previsions(session, lieu)
            lever = donnees["daily"]["sunrise"][0][-5:]
            coucher = donnees["daily"]["sunset"][0][-5:]
        except Exception:  # noqa: BLE001 — l'heure reste utile sans lever/coucher
            pass
    except Exception:  # noqa: BLE001 — ville inconnue : fuseau par défaut
        tz = ZoneInfo(TZ_DEFAUT)

    maintenant = datetime.now(timezone.utc)
    local = maintenant.astimezone(tz)
    return {
        "ok": 1,
        "e": int(maintenant.timestamp()),
        "o": int(local.utcoffset().total_seconds()),
        "j": JOURS[local.weekday()],
        "d": f"{local.day} {MOIS[local.month - 1]} {local.year}",
        "lv": lever,
        "cc": coucher,
    }


# La page Heure n'a besoin d'aucune route propre : elle utilise /time.
MODULE_HEURE = Module(id="heure", nom="Heure")
MODULE_METEO = Module(id="meteo", nom="Météo", routes={"/meteo": meteo})
