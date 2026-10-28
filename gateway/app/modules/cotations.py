"""Pages Actions, Devises et Commodities (cours en dollars, Yahoo Finance).

Source : Yahoo Finance (/v8/finance/chart, sans clé), une requête par valeur
qui ramène le cours et les clôtures quotidiennes sur un mois, gardée 5 minutes.
Devises : taux de la BCE (Frankfurter, sans clé) si Yahoo ne répond pas.

Yahoo refuse (erreur 429) les programmes qu'il ne prend pas pour un
navigateur, en se fiant aussi à la façon dont la connexion chiffrée est
établie. La passerelle l'appelle donc avec curl_cffi, qui se connecte
exactement comme Chrome (méthode de yfinance). curl_cffi n'existe pas pour
les processeurs ARM 32 bits (armv7) : la passerelle utilise alors aiohttp,
avec le risque de refus. Après un refus, Yahoo n'est plus appelé pendant
15 minutes et la page affiche la cause.

Conversions en dollars :
  - cotations en cents (« USX » : blé, maïs, soja, café…) divisées par 100 ;
  - cotations en pence (« GBp ») divisées par 100 puis converties ;
  - autres devises (ex. LVMH en euros) converties au taux du jour.
Les variations sont celles de la cotation d'origine.

Logos :
  - Actions : logo.dev (clé LOGO_DEV_TOKEN, facultative), puis logo inclus
    (gateway/app/icones/actions), puis dépôt GitHub nvstly/icons (actions
    cotées aux États-Unis) ; sinon l'afficheur dessine une pastille avec le code ;
  - Devises : drapeaux inclus (gateway/app/icones/devises) ;
  - Commodities : icônes incluses (gateway/app/icones/commodities).
"""

import datetime as dt
import os
import time
from urllib.parse import quote

import aiohttp

try:                                  # absent en armv7 (voir plus haut)
    from curl_cffi.requests import AsyncSession as _CurlSession
except ImportError:                   # pragma: no cover — dépend de la plateforme
    _CurlSession = None

from .. import images
from ..cache import SourceIndisponible, cache
from ..http import get_json
from . import Module, cours

YAHOO = os.environ.get("YAHOO_URL", "https://query1.finance.yahoo.com").rstrip("/")
FRANKFURTER = os.environ.get("FRANKFURTER_URL", "https://api.frankfurter.app").rstrip("/")
LOGO_DEV = os.environ.get("LOGO_DEV_URL", "https://img.logo.dev").rstrip("/")
NVSTLY = os.environ.get("NVSTLY_URL", "https://raw.githubusercontent.com/nvstly/icons/main/ticker_icons").rstrip("/")
TTL_COURS = 5 * 60
TTL_INCONNU = 3600
PAUSE_REFUS = 15 * 60       # après un refus de Yahoo, pas de nouvel appel avant 15 min
# Yahoo refuse souvent les clients qui ne ressemblent pas à un navigateur
ENTETES = {"User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
                         "Chrome/126.0 Safari/537.36", "Accept": "application/json"}


class Introuvable(Exception):
    """Symbole inconnu de la source."""


class Refus(Exception):
    """Yahoo refuse l'accès (401, 403, 429)."""


_refus = {"jusqua": 0.0, "cause": ""}
_curl: "_CurlSession | None" = None


def client_yahoo() -> str:
    return "curl_cffi (Chrome)" if _CurlSession else "aiohttp"


async def _yahoo_json(session: aiohttp.ClientSession, url: str, params: dict) -> dict:
    """Appel à Yahoo : curl_cffi si disponible, sinon aiohttp. 404 -> Introuvable,
    401 / 403 / 429 -> Refus (et pause de 15 minutes)."""
    global _curl
    if time.time() < _refus["jusqua"]:
        raise Refus(_refus["cause"])
    if _CurlSession is not None:
        if _curl is None:
            _curl = _CurlSession(impersonate="chrome", timeout=8)
        reponse = await _curl.get(url, params=params, headers={"Accept": "application/json"})
        statut = reponse.status_code
        donnees = reponse.json() if statut < 400 else None
    else:
        async with session.get(url, params=params, headers=ENTETES) as reponse:
            statut = reponse.status
            donnees = await reponse.json(content_type=None) if statut < 400 else None
    if statut == 404:
        raise Introuvable(url)
    if statut in (401, 403, 429):
        _refus["jusqua"] = time.time() + PAUSE_REFUS
        _refus["cause"] = f"Yahoo refuse l'accès ({statut})"
        raise Refus(_refus["cause"])
    if statut >= 400:
        raise RuntimeError(f"Yahoo : erreur {statut}")
    return donnees


async def fermer() -> None:
    global _curl
    if _curl is not None:
        await _curl.close()
        _curl = None


# --- Listes proposées sur l'afficheur (10 par page) -------------------------
#  identifiant (symbole Yahoo ou code devise) -> (nom de la liste, code court, icône, forme)
ACTIONS = {
    "AAPL": "Apple", "MSFT": "Microsoft", "NVDA": "Nvidia", "GOOGL": "Alphabet", "AMZN": "Amazon",
    "META": "Meta", "TSLA": "Tesla", "TTE.PA": "TotalEnergies", "MC.PA": "LVMH", "AIR.PA": "Airbus",
}
DEVISES = {
    "EUR": "Euro", "GBP": "Livre sterling", "JPY": "Yen", "CHF": "Franc suisse", "CNY": "Yuan",
    "CAD": "Dollar canadien", "AUD": "Dollar australien", "HKD": "Dollar de Hong Kong",
    "SGD": "Dollar de Singapour", "INR": "Roupie indienne",
}
# symbole Yahoo -> (nom, code court, icône incluse, forme)
COMMODITIES = {
    "GC=F": ("Or", "OR", "or", 2),
    "SI=F": ("Argent", "AG", "argent", 2),
    "HG=F": ("Cuivre", "CU", "cuivre", 2),
    "CL=F": ("Pétrole (WTI)", "WTI", "petrole", 1),
    "NG=F": ("Gaz naturel", "GAZ", "gaz", 1),
    "ZW=F": ("Blé", "BLE", "ble", 1),
    "ZC=F": ("Maïs", "MAIS", "mais", 1),
    "ZS=F": ("Soja", "SOJA", "soja", 1),
    "KC=F": ("Café", "CAFE", "cafe", 1),
    "CC=F": ("Cacao", "CAC", "cacao", 1),
}


# --- Yahoo Finance -----------------------------------------------------------

async def _graphique(session: aiohttp.ClientSession, symbole: str) -> dict:
    """Cours et clôtures quotidiennes sur un mois : {"px", "dev", "t": [...], "c": [...]}."""
    async def charger() -> dict:
        donnees = await _yahoo_json(session, f"{YAHOO}/v8/finance/chart/{quote(symbole, safe='=^.-')}",
                                    {"range": "1mo", "interval": "1d"})
        resultat = ((donnees.get("chart") or {}).get("result") or [None])[0]
        if not resultat:
            raise Introuvable(symbole)
        meta = resultat.get("meta") or {}
        temps = resultat.get("timestamp") or []
        closes = (((resultat.get("indicators") or {}).get("quote") or [{}])[0].get("close")) or []
        points = [(t, c) for t, c in zip(temps, closes) if c]
        prix = meta.get("regularMarketPrice") or (points[-1][1] if points else None)
        if prix is None:
            raise Introuvable(symbole)
        return {"px": float(prix), "dev": meta.get("currency") or "USD",
                "t": [t for t, _ in points], "c": [float(c) for _, c in points]}

    return await _avec_inconnu(f"yahoo-{symbole}", charger)


_inconnus: dict[str, float] = {}


async def _avec_inconnu(cle: str, charger) -> dict:
    """Cache de 5 min ; un symbole inconnu est mémorisé une heure (pas de nouvel appel).
    Source en échec : dernière valeur connue, marquée « p » (périmée)."""
    if time.time() - _inconnus.get(cle, 0) < TTL_INCONNU:
        raise Introuvable(cle)
    try:
        valeur, perime = await cache.obtenir(cle, TTL_COURS, charger, persistant=True)
        if perime:
            valeur = dict(valeur, p=1)
        return valeur
    except SourceIndisponible as exc:
        if isinstance(exc.__cause__, Introuvable):
            _inconnus[cle] = time.time()
            while len(_inconnus) > 100:
                _inconnus.pop(next(iter(_inconnus)))
            raise Introuvable(cle) from exc
        raise


def _variations(g: dict) -> tuple[float | None, float | None, float | None]:
    """Variations 1 jour de bourse, 7 jours et 30 jours (en %), sur la cotation d'origine."""
    px, temps, closes = g["px"], g["t"], g["c"]
    if not closes:
        return None, None, None
    # Dernière barre = séance en cours (ou dernière séance) : la référence 24 h
    # est la clôture précédente.
    precedente = closes[-2] if len(closes) >= 2 else closes[-1]

    def depuis(jours: int) -> float | None:
        limite = (temps[-1] if temps else time.time()) - jours * 86400
        ref = next((c for t, c in zip(reversed(temps), reversed(closes)) if t <= limite), closes[0])
        return (px / ref - 1) * 100 if ref else None

    return ((px / precedente - 1) * 100 if precedente else None), depuis(7), (px / closes[0] - 1) * 100


async def _taux_usd(session: aiohttp.ClientSession, devise: str) -> float:
    """Dollars pour 1 unité de devise (Yahoo, sinon BCE)."""
    if devise == "USD":
        return 1.0
    try:
        return (await _graphique(session, f"{devise}USD=X"))["px"]
    except (Introuvable, SourceIndisponible):
        return (await _frankfurter(session, devise))["px"]


async def _en_dollars(session: aiohttp.ClientSession, g: dict) -> tuple[float, list[float]]:
    """Cours et clôtures convertis en dollars."""
    devise, facteur = g["dev"], 1.0
    if devise == "USX":                    # cents
        devise, facteur = "USD", 0.01
    elif devise in ("GBp", "GBX"):         # pence
        devise, facteur = "GBP", 0.01
    facteur *= await _taux_usd(session, devise.upper())
    return g["px"] * facteur, [c * facteur for c in g["c"]]


async def _frankfurter(session: aiohttp.ClientSession, devise: str) -> dict:
    """Secours des devises : taux quotidiens de la BCE sur un mois."""
    async def charger() -> dict:
        debut = (dt.date.today() - dt.timedelta(days=31)).isoformat()
        try:
            donnees = await get_json(session, f"{FRANKFURTER}/{debut}..", {"from": devise, "to": "USD"})
        except aiohttp.ClientResponseError as exc:
            if exc.status in (404, 422):
                raise Introuvable(devise) from exc
            raise
        jours = sorted((donnees.get("rates") or {}).items())
        points = [(dt.datetime.fromisoformat(j).replace(tzinfo=dt.timezone.utc).timestamp(), v.get("USD"))
                  for j, v in jours if v.get("USD")]
        if not points:
            raise Introuvable(devise)
        return {"px": float(points[-1][1]), "dev": "USD", "t": [t for t, _ in points],
                "c": [float(c) for _, c in points]}

    return await _avec_inconnu(f"bce-{devise}", charger)


# --- Valeurs prêtes à afficher ---------------------------------------------

def _logo_action(symbole: str) -> str:
    sources = [images.perso("actions", symbole)]
    jeton = os.environ.get("LOGO_DEV_TOKEN", "").strip()
    if jeton:
        sources.append(f"{LOGO_DEV}/ticker/{quote(symbole, safe='.')}?token={quote(jeton)}"
                       "&format=png&theme=dark&size=128&fallback=404")
    sources.append(images.icone("actions", symbole.upper()))
    if symbole.isalpha():                  # cotation américaine : dépôt nvstly/icons
        sources.append(f"{NVSTLY}/{symbole.upper()}.png")
    return images.enregistrer(sources)


async def _cotation(session: aiohttp.ClientSession, page: str, ident: str, avec_courbe: bool) -> dict:
    """Une valeur de la page : prix, variations, logo (et courbe 30 jours)."""
    if page == "actions":
        symbole = ident.upper()
        code, logo, forme = cours.code_court(symbole), _logo_action(symbole), 1
    elif page == "devises":
        symbole = f"{ident.upper()}USD=X"
        code, forme = ident.upper()[:4], 2
        logo = images.enregistrer([images.perso("devises", ident), images.icone("devises", ident.upper())])
    else:
        symbole = ident.upper()
        nom, code, fichier, forme = COMMODITIES.get(symbole, ("", cours.code_court(symbole), "", 1))
        logo = images.enregistrer([images.perso("commodities", symbole), images.perso("commodities", fichier),
                                   images.icone("commodities", fichier) if fichier else ""])

    try:
        if page == "devises":
            try:
                g = await _graphique(session, symbole)
            except (Introuvable, SourceIndisponible):
                g = await _frankfurter(session, ident.upper())
        else:
            g = await _graphique(session, symbole)
    except Introuvable:
        return cours.introuvable(ident, code)
    except SourceIndisponible:
        return cours.introuvable(ident, code, indisponible=True)

    p24, p7, p30 = _variations(g)
    try:
        prix, closes = await _en_dollars(session, g)
    except (Introuvable, SourceIndisponible):
        return cours.introuvable(ident, code, indisponible=True)
    e = cours.element(ident, code, prix, p24, p7, p30, logo, forme)
    if avec_courbe:
        e["p30"], e["rep"] = cours.normaliser(closes, prix), cours.reperes(closes, prix)
    if g.get("p"):
        e["_p"] = 1           # cours périmé (source en échec) : signalé au niveau de la réponse
    return e


def _erreur() -> str:
    return _refus["cause"] if time.time() < _refus["jusqua"] else "Cours indisponibles"


def _reponse(sortie: list[dict]) -> dict:
    perime = any(e.pop("_p", 0) for e in sortie)
    return {"ok": 1, "c": sortie, "p": int(perime)}


def _page(page: str, variable: str, defaut: str):
    async def liste(session: aiohttp.ClientSession, q: dict) -> dict:
        """Valeurs choisies. Une seule : courbe sur 30 jours incluse (« p30 »).
        Deux ou trois : « x » = page dédiée à afficher (|7 j| >= 5 %)."""
        choix = cours.selection(q, variable, defaut)
        seule = len(choix) == 1
        sortie = [await _cotation(session, page, ident, seule) for ident in choix]
        if all(e.get("e") == 2 for e in sortie):
            return {"ok": 0, "err": _erreur()}
        return _reponse(sortie)

    async def trente_jours(session: aiohttp.ClientSession, q: dict) -> dict:
        """Une valeur sur 30 jours (page dédiée) : ?id=…"""
        ident = q.get("id", "").strip()
        if not ident:
            return {"ok": 0, "err": "Valeur inconnue"}
        e = await _cotation(session, page, ident, True)
        if e.get("e"):
            return {"ok": 0, "err": _erreur() if e["e"] == 2 else "Valeur introuvable"}
        return _reponse([e])

    return liste, trente_jours


def _options(dico: dict) -> list[dict]:
    return [{"id": k, "n": v if isinstance(v, str) else v[0]} for k, v in dico.items()]


_actions, _actions30 = _page("actions", "ACTIONS", "AAPL")
_devises, _devises30 = _page("devises", "DEVISES", "EUR")
_commodities, _commodities30 = _page("commodities", "COMMODITIES", "GC=F")

MODULE_ACTIONS = Module(
    id="actions", nom="Actions",
    params=cours.params("ex. BNP.PA, ^FCHI"),
    extra=lambda: cours.manifeste("ACTIONS", "AAPL", _options(ACTIONS)),
    routes={"/actions": _actions, "/actions/30j": _actions30},
    arreter=fermer,
)
MODULE_DEVISES = Module(
    id="devises", nom="Devises",
    params=cours.params("ex. KRW, BRL"),
    extra=lambda: cours.manifeste("DEVISES", "EUR", _options(DEVISES)),
    routes={"/devises": _devises, "/devises/30j": _devises30},
    arreter=fermer,
)
MODULE_COMMODITIES = Module(
    id="commodities", nom="Commodities",
    params=cours.params("ex. BZ=F, PL=F"),
    extra=lambda: cours.manifeste("COMMODITIES", "GC=F", _options(COMMODITIES)),
    routes={"/commodities": _commodities, "/commodities/30j": _commodities30},
    arreter=fermer,
)
