"""Cryptomonnaies via CoinGecko (1 à 3 au choix, logos officiels, en dollars).

Une requête /coins/markets ramène les 50 plus grosses capitalisations (prix,
variations 24 h, 7 et 30 jours, logo), gardée 10 minutes. Elle sert aussi à
la liste proposée sur l'afficheur : les 10 premières, hors stablecoins et
jetons « enveloppés » (wrapped, staked, bridged).

Sélection : voir cours.py (?s1=&s2=&s3= listes, ?m= saisie manuelle). La
saisie accepte l'identifiant CoinGecko (« pepe ») ou le symbole (« PEPE »,
reconnu parmi les 50 premières). Les afficheurs 2.8 envoient encore
?ids=bitcoin,ethereum : compris aussi.

Le module garde l'identifiant « btc » (page et route /btc) pour rester
compatible avec les afficheurs et les docker-compose existants.
"""

import os
import re

import aiohttp

from .. import images
from ..cache import SourceIndisponible, cache
from ..http import get_json
from ..text import ecran
from . import Module, cours

URL = os.environ.get("COINGECKO_URL", "https://api.coingecko.com/api/v3").rstrip("/")
CLE = os.environ.get("COINGECKO_KEY", "")
TAILLE_LISTE = 10
TTL_MARCHES = 10 * 60
_ENVELOPPES = re.compile(r"wrapped|staked|bridged|restaked|liquid staking|\bweth\b", re.I)


def _entetes() -> dict | None:
    return {"x-cg-demo-api-key": CLE} if CLE else None


async def _marches(session: aiohttp.ClientSession, ids: list[str] | None = None) -> tuple[list[dict], bool]:
    """Les 50 premières capitalisations, ou les identifiants demandés."""
    cle = "crypto-top" if ids is None else "crypto-ids-" + ",".join(sorted(ids))

    async def charger() -> list[dict]:
        params = {"vs_currency": "usd", "price_change_percentage": "24h,7d,30d"}
        if ids is None:
            params.update(order="market_cap_desc", per_page=50, page=1)
        else:
            params["ids"] = ",".join(ids)
        return await get_json(session, f"{URL}/coins/markets", params, _entetes())

    return await cache.obtenir(cle, TTL_MARCHES, charger, persistant=True)


def _stable(c: dict) -> bool:
    """Stablecoin ou jeton adossé à un autre (WBTC, stETH…) : exclu de la liste."""
    texte = f"{c.get('id', '')} {c.get('name', '')}"
    if _ENVELOPPES.search(texte) or "usd" in (c.get("symbol") or "").lower():
        return True
    prix = c.get("current_price") or 0
    return abs(prix - 1) < 0.02 and abs(c.get("price_change_percentage_7d_in_currency") or 0) < 0.6


def principales(marches: list[dict]) -> list[dict]:
    return [c for c in marches if not _stable(c)][:TAILLE_LISTE]


async def liste(session: aiohttp.ClientSession, q: dict) -> dict:
    """Liste des 10 premières cryptos, pour les listes déroulantes de l'afficheur."""
    marches, perime = await _marches(session)
    return {"ok": 1, "s": [{"id": c["id"], "n": ecran(f"{c['name']} ({c['symbol'].upper()})", 40)}
                           for c in principales(marches)], "p": int(perime)}


def _choix(q: dict) -> list[str]:
    if not cours.valeurs_saisies(q) and q.get("ids"):          # afficheur 2.8
        return [x for x in q["ids"].split(",") if x.strip()][:cours.MAX_VALEURS]
    return cours.selection(q, "CRYPTOS", "bitcoin")


async def _trouver(session: aiohttp.ClientSession, valeurs: list[str]) -> tuple[list[tuple[str, dict | None]], bool]:
    """Valeur saisie -> données CoinGecko (None si inconnue)."""
    try:
        top, perime = await _marches(session)
    except SourceIndisponible:
        top, perime = [], True
    par_id = {c["id"]: c for c in top}
    par_symbole: dict[str, dict] = {}
    for c in top:                                              # le mieux classé l'emporte
        par_symbole.setdefault((c.get("symbol") or "").lower(), c)

    trouves: list[tuple[str, dict | None]] = []
    a_demander = []
    for v in valeurs:
        cle = v.strip().lower()
        c = par_id.get(cle) or par_symbole.get(cle)
        trouves.append((v, c))
        if c is None:
            a_demander.append(cle)
    if a_demander:
        try:
            autres, p2 = await _marches(session, a_demander)
            perime |= p2
            autres_par_id = {c["id"]: c for c in autres}
            trouves = [(v, c or autres_par_id.get(v.strip().lower())) for v, c in trouves]
        except SourceIndisponible:
            if not top:
                raise
    return trouves, perime


def _element(c: dict) -> dict:
    return cours.element(
        c["id"], (c.get("symbol") or "?").upper()[:5], c["current_price"],
        c.get("price_change_percentage_24h_in_currency") or c.get("price_change_percentage_24h"),
        c.get("price_change_percentage_7d_in_currency"),
        c.get("price_change_percentage_30d_in_currency"),
        images.enregistrer([images.perso("crypto", c["id"]), images.perso("crypto", c.get("symbol") or ""),
                            c.get("image")]), 0,
    )


async def _courbe_30j(session: aiohttp.ClientSession, cid: str) -> list[float]:
    """Prix horaires sur 30 jours (CoinGecko market_chart), gardés 30 min."""
    async def charger() -> list[float]:
        donnees = await get_json(session, f"{URL}/coins/{cid}/market_chart",
                                 {"vs_currency": "usd", "days": 30}, _entetes())
        return [p for _, p in donnees.get("prices", [])]

    valeurs, _ = await cache.obtenir(f"crypto30-{cid}-usd", 30 * 60, charger, persistant=True)
    return valeurs


async def crypto(session: aiohttp.ClientSession, q: dict) -> dict:
    """Cryptos choisies. Une seule : courbe sur 30 jours incluse (« p30 ») et ses
    repères de prix (« rep », voir cours.reperes).
    Deux ou trois : prix et variations, « x » = page dédiée à afficher."""
    trouves, perime = await _trouver(session, _choix(q))
    sortie = []
    for v, c in trouves:
        if c and c.get("current_price") is not None:
            sortie.append(_element(c))
        else:
            sortie.append(cours.introuvable(v.strip().lower(), v.strip().upper()[:5]))
    if len(sortie) == 1 and not sortie[0].get("e"):
        e = sortie[0]
        try:
            valeurs, actuel = await _courbe_30j(session, e["id"]), trouves[0][1]["current_price"]
            e["p30"], e["rep"] = cours.normaliser(valeurs, actuel), cours.reperes(valeurs, actuel)
        except Exception:  # noqa: BLE001 — sans courbe, la page reste utile
            e["p30"] = []
    return {"ok": 1, "c": sortie, "p": int(perime)}


async def trente_jours(session: aiohttp.ClientSession, q: dict) -> dict:
    """Une crypto sur 30 jours (page dédiée) : ?id=ripple."""
    cid = q.get("id", "").strip().lower()
    trouves, perime = await _trouver(session, [cid]) if cid else ([(cid, None)], False)
    c = trouves[0][1]
    if not c or c.get("current_price") is None:
        return {"ok": 0, "err": "Crypto inconnue"}
    e = _element(c)
    valeurs = await _courbe_30j(session, c["id"])
    e["p30"], e["rep"] = cours.normaliser(valeurs, c["current_price"]), cours.reperes(valeurs, c["current_price"])
    return {"ok": 1, "c": [e], "p": int(perime)}


MODULE = Module(
    id="btc",
    nom="Crypto",
    params=cours.params("ex. pepe, kaspa"),
    extra=lambda: cours.manifeste("CRYPTOS", "bitcoin", src="/btc/liste"),
    routes={"/btc": crypto, "/btc/30j": trente_jours, "/btc/liste": liste},
)
