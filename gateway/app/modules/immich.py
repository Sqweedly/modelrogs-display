"""Page Photos : photos d'un serveur Immich local, en plein écran.

Variables : IMMICH_URL (ex. http://192.168.1.10:2283) et IMMICH_KEY (clé d'API
créée dans Immich, en lecture seule si la version le permet). La passerelle
interroge Immich sur le réseau local : aucune photo ne sort de chez soi.

Réglages sur l'afficheur :
  s      source : vide = au hasard ; « favoris » ; « souvenirs » (photos prises
         un jour comme aujourd'hui, les années passées) ; « a:<id> » un album ;
  duree  durée d'affichage de la page Photos, en secondes (30 par défaut) :
         utilisée par l'afficheur à la place de la durée commune des pages.

À chaque passage, la passerelle choisit une photo de la source en évitant les
20 dernières montrées, et renvoie {"ok": 1, "i": jeton} (+ « b » : « Il y a
3 ans » pour un souvenir). L'image est préparée par images.py en mode photo :
recadrée au centre, ou posée entière sur un fond flou si elle est en hauteur.
Sans souvenir ce jour-là, la page montre une photo au hasard (sans pastille).

Points d'entrée Immich utilisés (en-tête x-api-key) :
  POST /api/search/random            photos au hasard
  POST /api/search/metadata          favoris (isFavorite) et photos d'un album
                                     (albumIds : les versions récentes ne
                                     donnent plus la liste dans l'album)
  GET  /api/memories?for=AAAA-MM-JJ&type=on_this_day
                                     souvenirs du jour (date simple : un
                                     horodatage complet est refusé, erreur 400)
  GET  /api/albums                   liste des albums
  GET  /api/albums/<id>              photos d'un album (anciennes versions ; sans
                                     paramètre : Immich récent refuse les paramètres
                                     inconnus, erreur 400)

Droits de la clé d'API (Immich > Paramètres du compte > Clés d'API) :
  album.read   liste des albums et leurs photos
  asset.read   recherche des photos (au hasard, favoris, photos d'un album)
  asset.view   l'image elle-même
  memory.read  souvenirs
  GET  /api/assets/<id>/thumbnail?size=preview   l'image
"""

import datetime as dt
import logging
import os
import random
from collections import deque
from zoneinfo import ZoneInfo

import aiohttp

from .. import images
from ..cache import SourceIndisponible, cache
from ..http import get_json
from ..text import ecran
from . import Module

log = logging.getLogger("immich")

TTL_LISTE = 10 * 60          # liste d'un album ou des favoris gardée 10 minutes
HISTORIQUE = 20              # photos récentes à ne pas remontrer
DUREES = [10, 20, 30, 45, 60, 90, 120, 300]
DUREE_DEFAUT = 30

_vues: dict[str, deque] = {}


def _url() -> str:
    return os.environ.get("IMMICH_URL", "").strip().rstrip("/")


def _entetes() -> dict:
    return {"x-api-key": os.environ.get("IMMICH_KEY", "").strip(), "Accept": "application/json"}


async def _post(session: aiohttp.ClientSession, chemin: str, corps: dict):
    async with session.post(f"{_url()}{chemin}", json=corps, headers=_entetes()) as reponse:
        reponse.raise_for_status()
        return await reponse.json(content_type=None)


async def _get(session: aiohttp.ClientSession, chemin: str, params: dict | None = None):
    return await get_json(session, f"{_url()}{chemin}", params, _entetes())


def _images(assets) -> list[dict]:
    """Photos seulement (pas de vidéo, rien de la corbeille)."""
    return [a for a in (assets or []) if isinstance(a, dict) and a.get("id")
            and (a.get("type") or "IMAGE") == "IMAGE" and not a.get("isTrashed")]


async def _hasard(session: aiohttp.ClientSession) -> list[dict]:
    return _images(await _post(session, "/api/search/random", {"size": 50, "type": "IMAGE"}))


MAX_PAGES = 5               # recherche : 5 pages de 1000 photos au plus


async def _recherche(session: aiohttp.ClientSession, filtre: dict, avec_type: bool = True) -> list[dict]:
    """Photos correspondant au filtre (/api/search/metadata), page par page.
    avec_type : filtre « photos seulement » demandé à Immich (sinon, appliqué ici)."""
    photos: list[dict] = []
    page = 1
    for _ in range(MAX_PAGES):
        corps = dict(filtre, size=1000, page=page)
        if avec_type:
            corps["type"] = "IMAGE"
        r = await _post(session, "/api/search/metadata", corps)
        bloc = (r or {}).get("assets") or {}
        photos += _images(bloc.get("items"))
        suivante = bloc.get("nextPage")
        if not suivante:
            break
        page = int(suivante)
    return photos


async def _favoris(session: aiohttp.ClientSession) -> list[dict]:
    async def charger() -> list[dict]:
        photos = await _recherche(session, {"isFavorite": True})
        if not photos:          # certaines versions ne combinent pas favori et type : filtre ici
            photos = await _recherche(session, {"isFavorite": True}, avec_type=False)
        log.info("%d photo(s) dans les favoris", len(photos))
        return photos
    valeur, _ = await cache.obtenir("immich-favoris", TTL_LISTE, charger)
    return valeur


async def _album(session: aiohttp.ClientSession, album: str) -> list[dict]:
    async def charger() -> list[dict]:
        # Immich récent : les photos d'un album se demandent à la recherche ;
        # l'album lui-même ne les liste plus.
        photos: list[dict] = []
        for avec_type in (True, False):
            try:
                photos = await _recherche(session, {"albumIds": [album]}, avec_type)
            except aiohttp.ClientResponseError as exc:
                if exc.status not in (400, 404, 422):
                    raise
                break                       # recherche par album inconnue de cette version
            if photos:
                break
        if not photos:          # anciennes versions d'Immich : photos listées dans l'album
            # (sans paramètre : Immich récent refuse tout paramètre inconnu, erreur 400)
            r = await _get(session, f"/api/albums/{album}")
            photos = _images((r or {}).get("assets"))
        log.info("%d photo(s) dans l'album %s", len(photos), album)
        return photos
    valeur, _ = await cache.obtenir(f"immich-album-{album}", TTL_LISTE, charger)
    return valeur


def _aujourdhui() -> dt.date:
    """Date du jour dans le fuseau TZ (l'image Alpine n'a pas de fuseaux système : sans
    cela, la date serait celle de l'heure UTC, la veille juste après minuit)."""
    try:
        return dt.datetime.now(ZoneInfo(os.environ.get("TZ") or "Europe/Paris")).date()
    except (KeyError, ValueError):
        return dt.date.today()


def _annee(asset: dict) -> int | None:
    date = asset.get("localDateTime") or asset.get("fileCreatedAt") or ""
    try:
        return int(date[:4])
    except ValueError:
        return None


async def _souvenirs(session: aiohttp.ClientSession) -> list[tuple[dict, int]]:
    """Photos prises un jour comme aujourd'hui les années passées : (photo, il y a N ans)."""
    aujourdhui = _aujourdhui()

    async def charger() -> list[tuple[dict, int]]:
        # « for » : une date simple AAAA-MM-JJ (un horodatage complet est refusé, erreur 400)
        souvenirs = await _get(session, "/api/memories",
                               {"for": aujourdhui.isoformat(), "type": "on_this_day"})
        trouves: list[tuple[dict, int]] = []
        for s in souvenirs or []:
            if (s.get("type") or "on_this_day") != "on_this_day":
                continue
            annee = (s.get("data") or {}).get("year")
            for a in _images(s.get("assets")):
                n = aujourdhui.year - (annee or _annee(a) or aujourdhui.year)
                if n > 0:
                    trouves.append((a, n))
        log.info("%d photo(s) en souvenir aujourd'hui", len(trouves))
        return trouves

    valeur, _ = await cache.obtenir(f"immich-souvenirs-{aujourdhui.isoformat()}", TTL_LISTE, charger)
    return valeur


def _choisir(source: str, candidats: list):
    """Une photo au hasard, en évitant les 20 dernières montrées pour cette source."""
    vues = _vues.setdefault(source, deque(maxlen=HISTORIQUE))
    ident = (lambda c: c[0]["id"]) if candidats and isinstance(candidats[0], tuple) else (lambda c: c["id"])
    neufs = [c for c in candidats if ident(c) not in vues]
    if not neufs:                       # toutes vues : nouveau tour, sans répéter la dernière
        derniere = vues[-1] if vues else None
        vues.clear()
        neufs = [c for c in candidats if ident(c) != derniere] or candidats
    choix = random.choice(neufs)
    vues.append(ident(choix))
    return choix


def _cause(exc: Exception, source: str) -> str:
    """Message d'erreur affiché (l'erreur d'origine peut être enveloppée par le cache)."""
    origine = exc.__cause__ if isinstance(exc, SourceIndisponible) and exc.__cause__ else exc
    statut = getattr(origine, "status", 0)
    if statut in (401, 403):
        return "Clé Immich refusée"
    if statut == 404 and source.startswith("a:"):
        return "Album introuvable"
    if statut == 400:
        return "Requête refusée par Immich"
    return "Immich indisponible"


def _image(asset: dict) -> str:
    return images.enregistrer(f"{_url()}/api/assets/{asset['id']}/thumbnail?size=preview", _entetes(), photo=True)


async def photo(session: aiohttp.ClientSession, q: dict) -> dict:
    """Une photo de la source choisie : {"ok": 1, "i": jeton, "b": pastille éventuelle}."""
    source = (q.get("s") or "").strip()
    try:
        if source == "souvenirs":
            try:
                candidats = await _souvenirs(session)
            except (aiohttp.ClientError, SourceIndisponible, TimeoutError) as exc:
                origine = exc.__cause__ if isinstance(exc, SourceIndisponible) and exc.__cause__ else exc
                statut = getattr(origine, "status", 0)
                log.warning("souvenirs indisponibles (%s)%s : photo au hasard", origine,
                            " ; vérifier le droit memory.read de la clé" if statut in (401, 403) else "")
                candidats = []
            if candidats:
                a, n = _choisir(source, candidats)
                return {"ok": 1, "i": _image(a), "b": f"Il y a {n} an{'s' if n > 1 else ''}"}
            source = ""                                    # aucun souvenir aujourd'hui : au hasard
        if source == "favoris":
            candidats = await _favoris(session)
        elif source.startswith("a:"):
            candidats = await _album(session, source[2:])
        else:
            candidats = await _hasard(session)
    except (aiohttp.ClientError, SourceIndisponible, TimeoutError) as exc:
        origine = exc.__cause__ if isinstance(exc, SourceIndisponible) and exc.__cause__ else exc
        log.warning("source « %s » en échec : %s", source or "hasard", origine)
        return {"ok": 0, "err": _cause(exc, source)}
    if not candidats:
        log.warning("aucune photo pour la source « %s »", source or "hasard")
        return {"ok": 0, "err": "Aucune photo"}
    return {"ok": 1, "i": _image(_choisir(source or "hasard", candidats))}


async def sources(session: aiohttp.ClientSession, q: dict) -> dict:
    """Liste de l'interface : favoris, souvenirs, puis les albums."""
    liste = [{"id": "favoris", "n": "Favoris"}, {"id": "souvenirs", "n": "Souvenirs (ce jour-là)"}]
    try:
        albums = await _get(session, "/api/albums")
    except (aiohttp.ClientError, TimeoutError) as exc:
        return {"ok": 0, "err": _cause(exc, "")}
    albums = sorted(albums or [], key=lambda a: (a.get("albumName") or "").lower())
    liste += [{"id": f"a:{a['id']}", "n": ecran(f"Album · {a.get('albumName') or 'sans nom'}", 40)}
              for a in albums[:30] if a.get("id")]
    return {"ok": 1, "s": liste}


MODULE = Module(
    id="immich",
    nom="Photos",
    requis=["IMMICH_URL", "IMMICH_KEY"],
    params=[
        {"k": "s", "t": "select", "l": "Photos à montrer", "ph": "Au hasard", "src": "/immich/sources"},
        {"k": "duree", "t": "select", "l": "Durée d'affichage", "d": str(DUREE_DEFAUT),
         "o": [{"id": str(s), "n": f"{s} s" if s < 60 else f"{s // 60} min" + (f" {s % 60} s" if s % 60 else "")}
               for s in DUREES]},
    ],
    routes={"/immich": photo, "/immich/sources": sources},
)
