"""Diagnostic de bout en bout, activable à la demande.

Activation : DIAGNOSTIC=1 dans docker-compose.yml (au démarrage), ou à la volée
avec /debug?on=1 (et /debug?off=1 pour l'arrêter, jusqu'au prochain démarrage).
Désactivé, il ne journalise rien et ne garde aucune requête : seule la mesure
du retard de la boucle tourne (une mesure par seconde, sans écriture).

Activé :
  - chaque requête reçue est journalisée (logger « diagnostic ») avec sa route,
    son code, sa taille, sa durée de traitement et l'identifiant X-Req envoyé
    par l'afficheur, qui permet de la retrouver dans le journal de l'afficheur ;
  - ses étapes sont notées : cache (frais, rechargé, périmé), appels sortants
    (aiohttp, via TraceConfig), téléchargement et conversion des images ;
  - les 100 dernières requêtes et des statistiques par route et par source
    sont consultables sur /debug (JSON) ;
  - un retard de la boucle asyncio de plus de 100 ms (traitement bloquant) est
    signalé dans le journal.

La durée mesurée est celle du traitement, de la réception de la requête à la
réponse prête ; l'envoi du corps à l'afficheur (115 Ko pour une photo) n'y est
pas compté : l'afficheur le mesure de son côté.
"""

import asyncio
import contextvars
import logging
import os
import time
from collections import Counter, defaultdict, deque

from aiohttp import TraceConfig, web

log = logging.getLogger("diagnostic")

MAX_REQUETES = 100      # requêtes gardées pour /debug
MAX_DUREES = 200        # durées gardées par route et par source (statistiques)
MAX_ETAPES = 20         # étapes notées par requête
SEUIL_BOUCLE_MS = 100   # retard de boucle signalé au-delà

_actif = os.environ.get("DIAGNOSTIC", "0").strip().lower() in ("1", "true", "oui", "yes", "on")
_debut = time.time()
_etapes: contextvars.ContextVar[list | None] = contextvars.ContextVar("etapes", default=None)
_requetes: deque = deque(maxlen=MAX_REQUETES)
_durees: dict[str, deque] = defaultdict(lambda: deque(maxlen=MAX_DUREES))
_erreurs: Counter = Counter()
_sorties: dict[str, deque] = defaultdict(lambda: deque(maxlen=MAX_DUREES))
_sorties_erreurs: Counter = Counter()
_boucle = {"max_ms": 0.0, "recents": deque(maxlen=60)}


def actif() -> bool:
    return _actif


def activer(etat: bool) -> None:
    global _actif
    if etat != _actif:
        log.info("Diagnostic %s", "activé" if etat else "désactivé")
    _actif = etat


def etape(nom: str, ms: float | None = None, **info) -> None:
    """Note une étape de la requête en cours (sans effet hors diagnostic)."""
    if not _actif:
        return
    liste = _etapes.get()
    if liste is None or len(liste) >= MAX_ETAPES:
        return
    e = {"n": nom}
    if ms is not None:
        e["ms"] = round(ms, 1)
    e.update(info)
    liste.append(e)


def _texte_etape(e: dict) -> str:
    detail = " ".join(f"{k}={v}" for k, v in e.items() if k not in ("n", "ms"))
    return e["n"] + (f" {e['ms']} ms" if "ms" in e else "") + (f" ({detail})" if detail else "")


@web.middleware
async def intergiciel(request: web.Request, handler):
    if not _actif or request.path == "/debug":
        return await handler(request)
    jeton = _etapes.set([])
    t0 = time.perf_counter()
    statut, taille = 500, 0
    try:
        reponse = await handler(request)
        statut = reponse.status
        taille = reponse.content_length or 0
        return reponse
    except web.HTTPException as exc:
        statut = exc.status
        raise
    finally:
        ms = (time.perf_counter() - t0) * 1000
        etapes = _etapes.get() or []
        _etapes.reset(jeton)
        info = request.match_info.route.resource
        route = info.canonical if info is not None else request.path
        xreq = request.headers.get("X-Req", "")
        _requetes.append({"t": round(time.time(), 3), "x": xreq, "ip": request.remote or "",
                          "r": request.path_qs[:120], "s": statut, "o": taille, "ms": round(ms, 1), "e": etapes})
        _durees[route].append(ms)
        if statut >= 400:
            _erreurs[route] += 1
        log.info("%s %s -> %d, %d o, %.1f ms, X-Req=%s%s", request.remote, request.path_qs[:120], statut, taille,
                 ms, xreq or "-", (" | " + " ; ".join(_texte_etape(e) for e in etapes)) if etapes else "")


def trace_config() -> TraceConfig:
    """Mesure de chaque appel sortant fait avec la session aiohttp de la passerelle
    (jusqu'à la réception des en-têtes de la réponse)."""
    tc = TraceConfig()

    async def debut(session, ctx, params):
        ctx.t0 = time.perf_counter()

    async def fin(session, ctx, params):
        if not _actif:
            return
        ms = (time.perf_counter() - ctx.t0) * 1000
        hote = params.url.host or "?"
        _sorties[hote].append(ms)
        if params.response.status >= 400:
            _sorties_erreurs[hote] += 1
        etape("http " + hote, ms, s=params.response.status)

    async def erreur(session, ctx, params):
        if not _actif:
            return
        ms = (time.perf_counter() - getattr(ctx, "t0", time.perf_counter())) * 1000
        hote = params.url.host or "?"
        _sorties[hote].append(ms)
        _sorties_erreurs[hote] += 1
        etape("http " + hote, ms, err=type(params.exception).__name__)

    tc.on_request_start.append(debut)
    tc.on_request_end.append(fin)
    tc.on_request_exception.append(erreur)
    return tc


async def surveiller_boucle() -> None:
    """Retard de la boucle asyncio, mesuré chaque seconde : un traitement
    bloquant (calcul ou appel synchrone) le fait monter."""
    boucle = asyncio.get_running_loop()
    while True:
        t = boucle.time()
        await asyncio.sleep(1.0)
        retard = max(0.0, (boucle.time() - t - 1.0) * 1000)
        _boucle["recents"].append(retard)
        _boucle["max_ms"] = max(_boucle["max_ms"], retard)
        if _actif and retard > SEUIL_BOUCLE_MS:
            log.warning("Boucle retardée de %.0f ms (traitement bloquant)", retard)


def _stats(durees) -> dict:
    v = sorted(durees)
    if not v:
        return {"n": 0}
    return {"n": len(v), "med_ms": round(v[len(v) // 2], 1), "p95_ms": round(v[min(len(v) - 1, int(len(v) * 0.95))], 1),
            "max_ms": round(v[-1], 1)}


def etat(n: int = MAX_REQUETES, extra: dict | None = None) -> dict:
    """Contenu de /debug."""
    return {
        "ok": 1,
        "diagnostic": int(_actif),
        "depuis_s": int(time.time() - _debut),
        "boucle": {"max_ms": round(_boucle["max_ms"], 1),
                   "max_60s_ms": round(max(_boucle["recents"], default=0.0), 1)},
        "routes": {r: {**_stats(d), "err": _erreurs[r]} for r, d in sorted(_durees.items())},
        "sorties": {h: {**_stats(d), "err": _sorties_erreurs[h]} for h, d in sorted(_sorties.items())},
        **(extra or {}),
        "requetes": list(_requetes)[-n:][::-1],
    }
