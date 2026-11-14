"""MODELROGS Display — passerelle HTTP pour les afficheurs SmallTV.

L'afficheur (ESP8266) n'appelle que cette passerelle, en HTTP sur le réseau
local. Chaque réponse est un JSON court (< 2 Ko) déjà prêt à dessiner.
En cas d'erreur, la réponse reste un JSON lisible : {"ok": 0, "err": "..."}.

Modules : variable MODULES (ex. "heure,meteo,crypto,supervision,discord,plex").
Le manifeste /modules décrit les modules actifs et les réglages que
l'utilisateur doit renseigner dans l'interface de l'afficheur.
"""

import asyncio
import json
import logging
import os

import aiohttp
from aiohttp import web

from .cache import SourceIndisponible, cache
from . import diagnostic, images
from .modules import Module, btc, cotations, cours, custom, discord_vocal, immich, marches, meteo, plex, supervision, tesla

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s : %(message)s")
log = logging.getLogger("passerelle")

# Version injectée dans l'image par GitHub Actions (tag Git) ; « dev » en local.
VERSION = os.environ.get("MODELROGS_VERSION", "").strip() or "dev"
# Commit de l'image (7 caractères) : distingue deux images d'un même numéro
REVISION = os.environ.get("MODELROGS_REVISION", "").strip()[:7]
PORT = int(os.environ.get("PORT", "8080"))

CATALOGUE: dict[str, Module] = {m.id: m for m in (
    meteo.MODULE_HEURE, meteo.MODULE_METEO, discord_vocal.MODULE, supervision.MODULE, btc.MODULE, plex.MODULE,
    cotations.MODULE_ACTIONS, cotations.MODULE_EUROPE, cotations.MODULE_INDICES, cotations.MODULE_DEVISES,
    cotations.MODULE_COMMODITIES,
    custom.MODULE, immich.MODULE, tesla.MODULE,
)}
# Autres noms acceptés dans MODULES
ALIAS = {"crypto": "btc", "commodites": "commodities", "matieres": "commodities", "indice": "indices", "photos": "immich",
         "actions_europe": "europe"}

SESSION = web.AppKey("session", aiohttp.ClientSession)
ACTIFS = web.AppKey("actifs", list)
SURVEILLANCE = web.AppKey("surveillance", asyncio.Task)


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
    # 3.5.0 : « Actions Europe » vient avec « actions » (docker-compose inchangé)
    if CATALOGUE["actions"] in actifs and CATALOGUE["europe"] not in actifs:
        actifs.insert(actifs.index(CATALOGUE["actions"]) + 1, CATALOGUE["europe"])
    return actifs


def _route(module: Module, traitement):
    async def gestionnaire(request: web.Request) -> web.Response:
        if (erreur := module.erreur_config()):
            return _json({"ok": 0, "err": f"Non configuré : {erreur}"})
        try:
            q = dict(request.query)
            return _json(cours.appliquer_seuil(await traitement(request.app[SESSION], q), q))
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
    # ?f=2 (afficheur 3.2.0 et suivants) : pages de cours au format compact
    # (cours.compacter) ; sans ce paramètre, format d'origine (afficheurs plus anciens)
    compact = request.query.get("f") == "2"
    liste = []
    for m in request.app[ACTIFS]:
        if m.id == "europe" and not compact:
            continue        # 3.5.0 : page inconnue des afficheurs avant 3.2.0 (format d'origine, < 4 Ko)
        erreur = m.erreur_config()
        entree = {"id": m.id, "n": m.nom, "ok": int(not erreur), "err": erreur, "p": m.params, **m.manifeste()}
        if not compact:
            entree.pop("ld", None)
            entree.pop("lv", None)
        liste.append(cours.compacter(entree) if compact else entree)
    return _json({"ok": 1, "v": VERSION, "m": liste})


async def route_sante(request: web.Request) -> web.Response:
    detail = {}
    for m in request.app[ACTIFS]:
        erreur = m.erreur_config()
        detail[m.id] = f"non configuré ({erreur})" if erreur else (m.etat() if m.etat else "ok")
    return _json({"ok": 1, "version": VERSION, "modules": detail})


async def route_ping(request: web.Request) -> web.Response:
    """Réponse minimale : sonde réseau du diagnostic de l'afficheur."""
    return _json({"ok": 1})


async def route_debug(request: web.Request) -> web.Response:
    """Diagnostic (voir diagnostic.py) : /debug, /debug?on=1, /debug?off=1, /debug?n=20.
    Réponse destinée à un humain (curl), pas à l'afficheur : pas de limite de 2 Ko."""
    q = request.query
    if q.get("on") == "1":
        diagnostic.activer(True)
    elif q.get("off") == "1":
        diagnostic.activer(False)
    try:
        n = max(0, min(diagnostic.MAX_REQUETES, int(q.get("n", diagnostic.MAX_REQUETES))))
    except ValueError:
        n = diagnostic.MAX_REQUETES
    extra = {"version": VERSION, "cache": {"entrees": len(cache)}, "images": images.compteurs()}
    return web.json_response(diagnostic.etat(n, extra),
                             dumps=lambda o: json.dumps(o, ensure_ascii=False, indent=1))


async def demarrage(app: web.Application) -> None:
    cache.nettoyer_disque()
    app[SESSION] = aiohttp.ClientSession(
        timeout=aiohttp.ClientTimeout(total=8),
        headers={"User-Agent": f"MODELROGS-Display/{VERSION}"},
        trace_configs=[diagnostic.trace_config()],
    )
    app[SURVEILLANCE] = asyncio.create_task(diagnostic.surveiller_boucle())
    for m in app[ACTIFS]:
        erreur = m.erreur_config()
        if erreur:
            log.warning("Module %s NON configuré : %s", m.id, erreur)
        else:
            log.info("Module %s actif", m.id)
            if m.demarrer:
                m.demarrer()
    if any(m.id in ("actions", "indices", "devises", "commodities", "custom") for m in app[ACTIFS]):
        log.info("Yahoo Finance : client %s", cotations.client_yahoo())
    if any(m.id in ("actions", "europe", "indices", "commodities") for m in app[ACTIFS]):
        for cat in ("actions", "europe"):
            if CATALOGUE[cat] in app[ACTIFS]:
                marches.liste(cat)                  # lecture et contrôle de la date au démarrage
        marches.demarrer(lambda: app[SESSION])  # mosaïques : cours Yahoo répartis sur une heure
    images.inventaire_perso()
    log.info("Passerelle %s%s prête sur le port %d", VERSION, f" ({REVISION})" if REVISION else "", PORT)
    if diagnostic.actif():
        log.info("Diagnostic activé (DIAGNOSTIC=1) : requêtes journalisées, détail sur /debug")


async def arret(app: web.Application) -> None:
    app[SURVEILLANCE].cancel()
    await marches.arreter()
    for m in app[ACTIFS]:
        if m.arreter and not m.erreur_config():
            await m.arreter()
    await app[SESSION].close()


def creer_app() -> web.Application:
    app = web.Application(middlewares=[diagnostic.intergiciel])
    app[ACTIFS] = modules_demandes()
    app.router.add_get("/health", route_sante)
    app.router.add_get("/ping", route_ping)
    app.router.add_get("/debug", route_debug)
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
