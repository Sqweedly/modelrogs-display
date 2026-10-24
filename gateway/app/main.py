"""MODELROGS Display — passerelle HTTP pour les afficheurs SmallTV.

L'afficheur (ESP8266) n'appelle que cette passerelle, en HTTP sur le réseau
local. Chaque réponse est un JSON court (< 2 Ko) déjà prêt à dessiner.
En cas d'erreur, la réponse reste un JSON lisible : {"ok": 0, "err": "..."}.

Modules : variable MODULES (ex. "heure,meteo,crypto,supervision,discord,plex").
Le manifeste /modules décrit les modules actifs et les réglages que
l'utilisateur doit renseigner dans l'interface de l'afficheur.
"""

import json
import logging
import os

import aiohttp
from aiohttp import web

from .cache import SourceIndisponible, cache
from . import images
from .modules import Module, btc, discord_vocal, meteo, plex, supervision

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s : %(message)s")
log = logging.getLogger("passerelle")

# Version injectée dans l'image par GitHub Actions (tag Git) ; « dev » en local.
VERSION = os.environ.get("MODELROGS_VERSION", "").strip() or "dev"
PORT = int(os.environ.get("PORT", "8080"))

CATALOGUE: dict[str, Module] = {m.id: m for m in (
    meteo.MODULE_HEURE, meteo.MODULE_METEO, discord_vocal.MODULE, supervision.MODULE, btc.MODULE, plex.MODULE,
)}
# Autres noms acceptés dans MODULES
ALIAS = {"crypto": "btc"}

SESSION = web.AppKey("session", aiohttp.ClientSession)
ACTIFS = web.AppKey("actifs", list)


def _json(donnees: dict) -> web.Response:
    return web.json_response(donnees, dumps=lambda o: json.dumps(o, ensure_ascii=False, separators=(",", ":")))


def modules_demandes() -> list[Module]:
    noms = [n.strip().lower() for n in os.environ.get("MODULES", "heure,meteo,btc").split(",") if n.strip()]
    actifs = []
    for nom in noms:
        nom = ALIAS.get(nom, nom)
        if nom in CATALOGUE and CATALOGUE[nom] in actifs:
            continue
        if nom in CATALOGUE:
            actifs.append(CATALOGUE[nom])
        else:
            log.warning("Module inconnu ignoré : %s (disponibles : %s)", nom, ", ".join(CATALOGUE))
    return actifs


def _route(module: Module, traitement):
    async def gestionnaire(request: web.Request) -> web.Response:
        if (erreur := module.erreur_config()):
            return _json({"ok": 0, "err": f"Non configuré : {erreur}"})
        try:
            return _json(await traitement(request.app[SESSION], dict(request.query)))
        except meteo.VilleInconnue:
            return _json({"ok": 0, "err": "Ville introuvable"})
        except SourceIndisponible:
            return _json({"ok": 0, "err": "Source indisponible"})
        except Exception as exc:  # noqa: BLE001 — l'afficheur doit toujours recevoir un JSON
            log.exception("Erreur dans le module %s", module.id)
            return _json({"ok": 0, "err": f"Erreur : {type(exc).__name__}"})
    return gestionnaire


async def route_heure(request: web.Request) -> web.Response:
    try:
        return _json(await meteo.heure(request.app[SESSION], dict(request.query)))
    except Exception as exc:  # noqa: BLE001
        log.exception("Erreur /time")
        return _json({"ok": 0, "err": f"Erreur : {type(exc).__name__}"})


async def route_image(request: web.Request) -> web.Response:
    """Pixels RGB565 bruts d'une image annoncée par un module (voir images.py)."""
    return await images.route_image(request, request.app[SESSION])


async def route_modules(request: web.Request) -> web.Response:
    liste = []
    for m in request.app[ACTIFS]:
        erreur = m.erreur_config()
        liste.append({"id": m.id, "n": m.nom, "ok": int(not erreur), "err": erreur, "p": m.params})
    return _json({"ok": 1, "v": VERSION, "m": liste})


async def route_sante(request: web.Request) -> web.Response:
    detail = {}
    for m in request.app[ACTIFS]:
        erreur = m.erreur_config()
        detail[m.id] = f"non configuré ({erreur})" if erreur else (m.etat() if m.etat else "ok")
    return _json({"ok": 1, "version": VERSION, "modules": detail})


async def demarrage(app: web.Application) -> None:
    cache.nettoyer_disque()
    app[SESSION] = aiohttp.ClientSession(
        timeout=aiohttp.ClientTimeout(total=8),
        headers={"User-Agent": f"MODELROGS-Display/{VERSION}"},
    )
    for m in app[ACTIFS]:
        erreur = m.erreur_config()
        if erreur:
            log.warning("Module %s NON configuré : %s", m.id, erreur)
        else:
            log.info("Module %s actif", m.id)
            if m.demarrer:
                m.demarrer()
    log.info("Passerelle %s prête sur le port %d", VERSION, PORT)


async def arret(app: web.Application) -> None:
    for m in app[ACTIFS]:
        if m.arreter and not m.erreur_config():
            await m.arreter()
    await app[SESSION].close()


def creer_app() -> web.Application:
    app = web.Application()
    app[ACTIFS] = modules_demandes()
    app.router.add_get("/health", route_sante)
    app.router.add_get("/modules", route_modules)
    app.router.add_get("/time", route_heure)
    app.router.add_get("/img", route_image)
    for m in app[ACTIFS]:
        for chemin, traitement in m.routes.items():
            app.router.add_get(chemin, _route(m, traitement))
    app.on_startup.append(demarrage)
    app.on_cleanup.append(arret)
    return app


def main() -> None:
    web.run_app(creer_app(), port=PORT, access_log=None, print=None)


if __name__ == "__main__":
    main()
