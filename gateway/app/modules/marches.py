"""Mosaïques des marchés (3.5.0) : une par catégorie.

Pour chaque catégorie, la mosaïque montre les 12 plus fortes variations sur
24 h (en valeur absolue) parmi :
  btc          les 30 premières capitalisations CoinGecko, sans stablecoins ;
  actions      la liste fixe « Actions Monde » (app/listes/actions_monde.txt) ;
  europe       la liste fixe « Actions Europe » (app/listes/actions_europe.txt) ;
  indices      les 20 indices de cotations.INDICES ;
  commodities  les 20 matières premières de cotations.COMMODITIES.

Route /marche/<catégorie>?s24=3&s7=5[&s1=…&s2=…&s3=…&m=…] :
  {"ok": 1,
   "t": [[code, variation 24 h × 10, liseré], …]   12 tuiles au plus,
   "a": [[id, id, id], [id, …]]                    pages automatiques (|24 h| ≥ s24,
                                                   2 pages de 3 au plus, sans les
                                                   valeurs choisies),
   "x": [id, …]                                    pages sur 30 jours (|7 j| ≥ s7, 3 au plus),
   "ch": 1                                         des valeurs ont été choisies (s1…m),
   "p": 1                                          données périmées (facultatif)}
Les « id » sont ceux de la route de la catégorie (/btc?s1=<id>, /actions?s1=<id>…).

Actualisation des cours Yahoo (actions, indices, matières premières) : sur une
heure, une requête à la fois, réparties ; seulement pour les catégories
demandées par un afficheur dans les 2 dernières heures. Au premier passage,
les cours manquants sont chargés plus vite (une requête toutes les 2 s).
La Crypto reprend l'appel CoinGecko des pages de cours (10 min).

Listes fixes : modifiables sans reconstruire l'image (volume Docker monté sur
/listes, variable LISTES_DIR). Une liste datée de plus de 15 jours est
signalée dans le journal et dans l'interface de l'afficheur.
"""

import asyncio
import datetime as dt
import logging
import os
import re
import time
from pathlib import Path

import aiohttp

from ..cache import SourceIndisponible
from . import btc, cotations, cours

log = logging.getLogger("marches")

TUILES = 12
TOP_CRYPTO = 30
MAX_AUTO = 2              # pages automatiques à 3 valeurs, au plus
MAX_30J = 3               # pages sur 30 jours, au plus
PERIODE = 3600.0          # actualisation complète des cours Yahoo
PAS_RATTRAPAGE = 2.0      # premier chargement : une requête toutes les 2 s
ACTIVITE = 2 * 3600.0     # catégorie suivie tant qu'un afficheur l'a demandée récemment
AGE_LISTE = 15            # jours : au-delà, liste signalée « à revoir »
SEUIL24, SEUIL7 = 3.0, 5.0

DOSSIER_DEFAUT = Path(__file__).resolve().parent.parent / "listes"
FICHIERS = {"actions": "actions_monde.txt", "europe": "actions_europe.txt"}
CATEGORIES = ("btc", "actions", "europe", "indices", "commodities")


# --- Listes fixes --------------------------------------------------------------

class Liste:
    """Liste fixe d'actions : [(symbole Yahoo, code court, nom)] et sa date."""

    def __init__(self, cat: str) -> None:
        self.cat = cat
        self.valeurs: list[tuple[str, str, str]] = []
        self.date: dt.date | None = None
        self.source = ""
        self._mtime = -1.0

    def chemin(self) -> Path:
        perso = os.environ.get("LISTES_DIR", "/listes").strip()
        p = Path(perso) / FICHIERS[self.cat] if perso else None
        return p if p and p.is_file() else DOSSIER_DEFAUT / FICHIERS[self.cat]

    def charger(self) -> None:
        """(Re)lit le fichier s'il a changé : une liste modifiée est prise sans redémarrage."""
        p = self.chemin()
        try:
            mtime = p.stat().st_mtime
        except OSError:
            return
        if str(p) == self.source and mtime == self._mtime:
            return
        valeurs, date = [], None
        for ligne in p.read_text(encoding="utf-8").splitlines():
            ligne = ligne.strip()
            if ligne.startswith("#"):
                m = re.search(r"mise à jour\s*:\s*(\d{4}-\d{2}-\d{2})", ligne)
                if m:
                    try:
                        date = dt.date.fromisoformat(m.group(1))
                    except ValueError:
                        pass
                continue
            morceaux = [x.strip() for x in ligne.split(";")]
            if len(morceaux) >= 2 and morceaux[0]:
                code = (morceaux[1] or cours.code_court(morceaux[0]))[:5].upper()
                nom = morceaux[2] if len(morceaux) > 2 and morceaux[2] else morceaux[0]
                if morceaux[0].upper() not in (v[0] for v in valeurs):
                    valeurs.append((morceaux[0].upper(), code, nom))
        self.valeurs, self.date, self.source, self._mtime = valeurs[:TOP_CRYPTO], date, str(p), mtime
        log.info("Liste %s : %d valeurs (%s), mise à jour le %s", self.cat, len(self.valeurs), p,
                 self.date.isoformat() if self.date else "?")
        if self.vieille():
            log.warning("Liste %s datée du %s (plus de %d jours) : à revoir (%s)", self.cat,
                        self.date.isoformat() if self.date else "?", AGE_LISTE, FICHIERS[self.cat])

    def age(self) -> int | None:
        return (dt.date.today() - self.date).days if self.date else None

    def vieille(self) -> bool:
        a = self.age()
        return a is None or a > AGE_LISTE


LISTES = {cat: Liste(cat) for cat in FICHIERS}


def liste(cat: str) -> Liste:
    LISTES[cat].charger()
    return LISTES[cat]


def options(cat: str) -> list[dict]:
    """Options des listes déroulantes de l'interface (« Mes 3 valeurs »)."""
    return [{"id": s, "n": f"{n} ({c})"} for s, c, n in liste(cat).valeurs]


def infos_liste(cat: str) -> dict:
    """Date de la liste pour l'interface : « ld » (AAAA-MM-JJ), « lv » = 1 si à revoir."""
    li = liste(cat)
    return {"ld": li.date.isoformat() if li.date else "", "lv": int(li.vieille())}


# --- Valeurs suivies par catégorie ------------------------------------------------

def symboles(cat: str) -> list[tuple[str, str, str]]:
    """[(id de la route, symbole Yahoo, code court)] des catégories Yahoo."""
    if cat in FICHIERS:
        return [(s, s, c) for s, c, _ in liste(cat).valeurs]
    if cat == "indices":
        return [(k, cotations.symbole_indice(k), v[1]) for k, v in cotations.INDICES.items()]
    if cat == "commodities":
        return [(k, k, v[1]) for k, v in cotations.COMMODITIES.items()]
    return []


# Dernières variations connues : DONNEES[cat][id] = (code, v24, v7, instant, périmée)
DONNEES: dict[str, dict[str, tuple[str, float, float, float, bool]]] = {c: {} for c in CATEGORIES}
_demandes: dict[str, float] = {}
_essais: dict[tuple[str, str], float] = {}  # dernière tentative (réussie ou non) par valeur
_reveil: asyncio.Event | None = None      # catégorie nouvellement demandée : la boucle repart aussitôt


async def _attendre(secondes: float) -> None:
    global _reveil
    if _reveil is None:
        _reveil = asyncio.Event()
    try:
        await asyncio.wait_for(_reveil.wait(), secondes)
    except asyncio.TimeoutError:
        pass
    _reveil.clear()


def _demander(cat: str) -> None:
    nouvelle = time.time() - _demandes.get(cat, 0) > ACTIVITE
    _demandes[cat] = time.time()
    if nouvelle and _reveil is not None:
        _reveil.set()


async def _charger_yahoo(session: aiohttp.ClientSession, cat: str, ident: str, symbole: str, code: str) -> None:
    _essais[(cat, ident)] = time.time()      # un symbole en échec attend lui aussi son tour (1 h)
    try:
        g = await cotations._graphique(session, symbole)
    except (cotations.Introuvable, SourceIndisponible) as exc:
        log.info("Mosaïque %s : %s indisponible (%s)", cat, symbole, type(exc).__name__)
        DONNEES[cat].pop(ident, None)
        return
    p24, p7, _ = cotations._variations(g)
    if p24 is None:
        return
    DONNEES[cat][ident] = (code, p24, p7 or 0.0, time.time(), bool(g.get("p")))


def _a_charger(maintenant: float) -> list[tuple[float, str, str, str, str]]:
    """Valeurs des catégories suivies, de la plus ancienne à la plus récente (0 = jamais)."""
    sortie = []
    for cat in ("actions", "europe", "indices", "commodities"):
        if maintenant - _demandes.get(cat, 0) > ACTIVITE:
            continue
        for ident, symbole, code in symboles(cat):
            sortie.append((_essais.get((cat, ident), 0.0), cat, ident, symbole, code))
    sortie.sort(key=lambda x: x[0])
    return sortie


async def boucle(session_fn) -> None:
    """Actualisation répartie sur une heure (voir l'en-tête du module)."""
    while True:
        try:
            maintenant = time.time()
            file = _a_charger(maintenant)
            if not file:
                await _attendre(60)
                continue
            instant, cat, ident, symbole, code = file[0]
            if instant == 0:                              # jamais chargé : rattrapage
                await _charger_yahoo(session_fn(), cat, ident, symbole, code)
                await asyncio.sleep(PAS_RATTRAPAGE)
                continue
            pas = PERIODE / len(file)
            if maintenant - instant < PERIODE:            # tout est frais : on attend le plus ancien
                await _attendre(max(1.0, min(pas, PERIODE - (maintenant - instant))))
                continue
            await _charger_yahoo(session_fn(), cat, ident, symbole, code)
            await asyncio.sleep(pas)
        except asyncio.CancelledError:
            raise
        except Exception:  # noqa: BLE001 — la boucle ne doit jamais s'arrêter
            log.exception("Actualisation des mosaïques")
            await asyncio.sleep(30)


_tache: asyncio.Task | None = None


def demarrer(session_fn) -> None:
    global _tache
    if _tache is None:
        _tache = asyncio.create_task(boucle(session_fn))


async def arreter() -> None:
    global _tache
    if _tache is not None:
        _tache.cancel()
        _tache = None


async def _crypto(session: aiohttp.ClientSession) -> tuple[list[tuple[str, str, float, float]], bool]:
    """[(id, code, v24, v7)] des 30 premières cryptos hors stablecoins."""
    marches, perime = await btc._marches(session)
    sortie = []
    for c in marches:
        if btc._stable(c) or c.get("current_price") is None:
            continue
        v24 = c.get("price_change_percentage_24h_in_currency") or c.get("price_change_percentage_24h")
        if v24 is None:
            continue
        sortie.append((c["id"], (c.get("symbol") or "?").upper()[:5], float(v24),
                       float(c.get("price_change_percentage_7d_in_currency") or 0.0)))
        if len(sortie) == TOP_CRYPTO:
            break
    return sortie, perime


# --- Route --------------------------------------------------------------------------

def _seuil(q: dict, cle: str, defaut: float) -> float:
    try:
        return max(0.0, min(100.0, float(q.get(cle, "").replace(",", "."))))
    except ValueError:
        return defaut


def _cle(cat: str, ident: str) -> str:
    """Comparaison des valeurs choisies et des valeurs de la mosaïque."""
    ident = ident.strip().upper()
    return cotations.indice(ident) if cat == "indices" else ident


def selection(cat: str, valeurs: list[tuple[str, str, float, float]], choisies: list[str],
              s24: float, s7: float) -> dict:
    """Tuiles, pages automatiques et pages sur 30 jours (fonction pure, testée seule).
    valeurs : [(id, code, v24, v7)]."""
    tuiles = sorted(valeurs, key=lambda v: -abs(v[2]))[:TUILES]
    deja = {_cle(cat, c) for c in choisies}
    codes_choisis = {c.strip().upper() for c in choisies}
    auto = [v for v in tuiles if abs(v[2]) >= s24
            and _cle(cat, v[0]) not in deja and v[1].upper() not in codes_choisis][:3 * MAX_AUTO]
    pages = [[v[0] for v in auto[i:i + 3]] for i in range(0, len(auto), 3)]
    en_page = {v[0] for v in auto}
    trente = [v[0] for v in sorted(tuiles, key=lambda v: -abs(v[3])) if abs(v[3]) >= s7][:MAX_30J]
    return {
        "t": [[v[1], int(round(v[2] * 10)), int(v[0] in en_page)] for v in tuiles],
        "a": pages,
        "x": trente,
    }


def _route(cat: str):
    async def marche(session: aiohttp.ClientSession, q: dict) -> dict:
        _demander(cat)
        s24, s7 = _seuil(q, "s24", SEUIL24), _seuil(q, "s7", SEUIL7)
        choisies = cours.valeurs_saisies(q)[:cours.MAX_VALEURS]
        perime = False
        if cat == "btc":
            try:
                valeurs, perime = await _crypto(session)
            except SourceIndisponible:
                return {"ok": 0, "err": "Source indisponible"}
        else:
            valeurs = [(ident, v[0], v[1], v[2]) for ident, v in DONNEES[cat].items()]
            perime = any(v[4] for v in DONNEES[cat].values())
            if cat in FICHIERS:
                actives = {s for s, _, _ in liste(cat).valeurs}
                valeurs = [v for v in valeurs if v[0] in actives]   # liste modifiée entre-temps
        if not valeurs:
            return {"ok": 0, "err": "Cours en préparation"}
        rep = {"ok": 1, **selection(cat, valeurs, choisies, s24, s7), "ch": int(bool(choisies))}
        if perime:
            rep["p"] = 1
        return rep
    return marche


ROUTES = {f"/marche/{cat}": _route(cat) for cat in CATEGORIES}
