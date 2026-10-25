"""Supervision via l'API publique d'une page de statut Uptime Kuma.

Aucune clé n'est nécessaire : la passerelle lit la page de statut indiquée
par KUMA_STATUS_SLUG. Cette page doit contenir toutes les sondes que l'on
veut pouvoir cocher depuis l'interface de l'afficheur.
"""

import os
import time
from datetime import datetime, timezone

import aiohttp

from ..cache import cache
from ..http import get_json
from ..text import ecran
from . import Module

# États envoyés à l'afficheur
PANNE, INSTABLE, MAINTENANCE, OK = 2, 1, 3, 0
_ORDRE = {PANNE: 0, INSTABLE: 1, MAINTENANCE: 2, OK: 3}
# Statut Kuma -> état écran (0 = down, 1 = up, 2 = pending, 3 = maintenance)
_KUMA = {0: PANNE, 1: OK, 2: INSTABLE, 3: MAINTENANCE}

MAX_TUILES = 12      # au-delà : mode « résumé » (compteur + problèmes), pour les anciens afficheurs
MAX_PROBLEMES = 12   # services en défaut envoyés (l'écran en montre 6, ou 5 + « autres »)
MAX_CHOIX = 48       # services cochables sur l'afficheur


# Liste des sondes relue chaque minute : une sonde ajoutée ou retirée de la page
# de statut apparaît (ou disparaît) sur l'écran et dans l'interface en ~1 min.
TTL_SONDES = 60
TTL_BATTEMENTS = 30


def _kuma() -> tuple[str, str]:
    return os.environ.get("KUMA_URL", "").rstrip("/"), os.environ.get("KUMA_STATUS_SLUG", "")


def _frais() -> dict:
    """Paramètre ignoré par Kuma mais qui change l'adresse : Kuma garde ses
    réponses de page de statut en cache plusieurs minutes, par adresse ; on
    obtient ainsi une liste à jour (au plus une requête par période de cache)."""
    return {"_": int(time.time())}


async def _sondes(session: aiohttp.ClientSession) -> tuple[list[dict], bool]:
    """Sondes de la page de statut : [{id, nom}] (cache TTL_SONDES)."""
    url, slug = _kuma()

    async def charger() -> list[dict]:
        donnees = await get_json(session, f"{url}/api/status-page/{slug}", _frais())
        vues: dict[int, str] = {}
        for groupe in donnees.get("publicGroupList", []):
            for sonde in groupe.get("monitorList", []):
                vues.setdefault(int(sonde["id"]), sonde.get("name", f"#{sonde['id']}"))
        return [{"id": i, "nom": n} for i, n in vues.items()]

    return await cache.obtenir("kuma-sondes", TTL_SONDES, charger)


async def _battements(session: aiohttp.ClientSession) -> tuple[dict, bool]:
    """Derniers battements par sonde (cache TTL_BATTEMENTS)."""
    url, slug = _kuma()

    async def charger() -> dict:
        donnees = await get_json(session, f"{url}/api/status-page/heartbeat/{slug}", _frais())
        return donnees.get("heartbeatList", {})

    return await cache.obtenir("kuma-battements", TTL_BATTEMENTS, charger)


def _date_kuma(texte: str) -> datetime | None:
    try:
        return datetime.fromisoformat(texte.replace(" ", "T")[:19]).replace(tzinfo=timezone.utc)
    except (ValueError, AttributeError):
        return None


def _minutes_dans_etat(historique: list[dict]) -> tuple[int, bool]:
    """Minutes écoulées depuis le passage dans l'état actuel (dernière suite de
    battements de même statut). Le booléen indique une durée minimale : tout
    l'historique connu est dans cet état, le début réel est plus ancien."""
    if not historique:
        return 0, False
    statut = historique[-1].get("status")
    debut, borne = None, True
    for battement in reversed(historique):
        if battement.get("status") != statut:
            borne = False
            break
        debut = battement.get("time")
    moment = _date_kuma(debut) if debut else None
    if moment is None:
        return 0, False
    return max(0, int((datetime.now(timezone.utc) - moment).total_seconds() // 60)), borne


def _minutes_en_panne(historique: list[dict]) -> int:
    """Minutes écoulées depuis le passage en panne (dernière suite de statuts 0)."""
    if not historique or historique[-1].get("status") != 0:
        return 0
    return _minutes_dans_etat(historique)[0]


async def liste_services(session: aiohttp.ClientSession, q: dict) -> dict:
    """Liste dynamique pour l'interface de l'afficheur : toutes les sondes."""
    sondes, perime = await _sondes(session)
    return {
        "ok": 1,
        "s": [{"id": str(s["id"]), "n": s["nom"][:40]}
              for s in sorted(sondes, key=lambda s: s["nom"].lower())],
        "p": int(perime),
    }


async def etat(session: aiohttp.ClientSession, q: dict) -> dict:
    """État des sondes, prêt à afficher.

    ?ids=3,7,12 : sondes choisies sur l'afficheur ;
    ?ids=* ou ids absent/vide : toutes les sondes de la page de statut Kuma,
    y compris celles ajoutées plus tard (réglage par défaut).
    """
    valeurs = [x.strip() for x in q.get("ids", "").split(",") if x.strip()]
    tous = not valeurs or "*" in valeurs
    choisis = {int(x) for x in valeurs if x.isdigit()}
    sondes, perime1 = await _sondes(session)
    battements, perime2 = await _battements(session)

    services = []
    for sonde in sondes:
        if not tous and sonde["id"] not in choisis:
            continue
        historique = battements.get(str(sonde["id"])) or []
        statut = historique[-1].get("status") if historique else None
        e = _KUMA.get(statut, INSTABLE)
        minutes, borne = _minutes_dans_etat(historique) if e != OK else (0, False)
        services.append({"nom": sonde["nom"], "e": e, "duree": minutes, "borne": borne,
                         "min": minutes if e == PANNE else 0})

    services.sort(key=lambda s: (_ORDRE[s["e"]], s["nom"].lower()))
    compte = {k: sum(1 for s in services if s["e"] == k) for k in (OK, INSTABLE, PANNE, MAINTENANCE)}
    total = len(services)

    # Depuis la 2.8.0, l'afficheur ne montre que les services en défaut : seuls
    # eux sont envoyés (pannes d'abord), 12 au plus, quel que soit le total.
    problemes = [s for s in services if s["e"] != OK]
    mode = "t" if total <= MAX_TUILES else "r"
    affiches, autres = problemes[:MAX_PROBLEMES], max(0, len(problemes) - MAX_PROBLEMES)

    # L'alerte met en avant la panne la plus ancienne.
    pannes = sorted((s for s in services if s["e"] == PANNE), key=lambda s: -s["min"])
    alerte = {"n": ecran(pannes[0]["nom"], 16), "m": pannes[0]["min"], "k": len(pannes)} if pannes else None

    return {
        "ok": 1,
        "tot": total, "okn": compte[OK], "pa": compte[PANNE], "in": compte[INSTABLE], "ma": compte[MAINTENANCE],
        "md": mode,
        # m : minutes dans l'état actuel ; b = 1 : durée minimale (début hors de l'historique Kuma)
        "s": [{"n": ecran(s["nom"], 14), "e": s["e"], "m": s["duree"], "b": int(s["borne"])} for s in affiches],
        "x": autres,
        "al": alerte,
        "p": int(perime1 or perime2),
    }


MODULE = Module(
    id="supervision",
    nom="Supervision",
    requis=["KUMA_URL", "KUMA_STATUS_SLUG"],
    # « tous » : case « tout ce qu'affiche la page de statut » (valeur « * »),
    # cochée par défaut ; décochée, on choisit les services un par un.
    params=[{"k": "ids", "l": "Services affichés", "t": "multi",
             "src": "/supervision/services", "max": MAX_CHOIX,
             "tous": "Tous les services de la page de statut Kuma (y compris ceux ajoutés plus tard)"}],
    routes={"/supervision": etat, "/supervision/services": liste_services},
)
