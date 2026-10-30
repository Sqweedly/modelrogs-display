"""Page Custom : jusqu'à 3 valeurs prises dans toutes les pages de cours.

Exemple : Bitcoin, l'action Tesla et l'or sur une même page. Chaque valeur
est servie exactement comme sur sa page d'origine (Crypto, Actions, Indices,
Devises, Commodities) : même cours, mêmes variations, même logo, même unité
(dollars, points pour un indice). L'afficheur utilise donc les mêmes écrans.

Saisie d'une valeur : « catégorie:valeur ».
  c: ou crypto:        identifiant CoinGecko ou symbole   c:bitcoin, crypto:PEPE
  a: ou action:        symbole Yahoo                      a:TSLA, action:MC.PA
  i: ou indice:        indice (avec ou sans ^)            i:FCHI, indice:CAC40
  d: ou devise:        code ISO                           d:EUR
  m: ou matiere:       contrat Yahoo                      m:GC=F, matiere:CL=F
Sans préfixe, la catégorie est devinée : contrat « =F » ou connu -> matière
première ; « ^… » ou indice connu -> indice ; code de devise connu ->
devise ; crypto parmi les 50 premières capitalisations -> crypto ; sinon
action. Les listes de l'interface (/custom/liste, 5 valeurs par catégorie)
donnent des valeurs déjà préfixées (« c:bitcoin »).

Valeurs par défaut (rien choisi sur l'afficheur) : variable CUSTOM du
docker-compose, ex. « c:bitcoin,a:TSLA,m:GC=F ».
"""

import aiohttp

from .. import images
from ..cache import SourceIndisponible
from . import Module, btc, cotations, cours

PREFIXES = {
    "c": "c", "crypto": "c", "cryptos": "c",
    "a": "a", "action": "a", "actions": "a",
    "i": "i", "indice": "i", "indices": "i",
    "d": "d", "devise": "d", "devises": "d",
    "m": "m", "matiere": "m", "matieres": "m", "commodity": "m", "commodities": "m",
}
PAGES = {"a": "actions", "i": "indices", "d": "devises", "m": "commodities"}
PAR_CATEGORIE = 5          # valeurs proposées par catégorie dans les listes de l'interface
DEFAUT = "c:bitcoin,a:TSLA,m:GC=F"


async def _symboles_crypto(session: aiohttp.ClientSession) -> set[str]:
    """Identifiants et symboles des 50 premières cryptos (pour deviner la catégorie)."""
    try:
        top, _ = await btc._marches(session)
    except SourceIndisponible:
        return set()
    return {c["id"].lower() for c in top} | {(c.get("symbol") or "").lower() for c in top}


async def categorie(session: aiohttp.ClientSession, saisie: str) -> tuple[str, str]:
    """« c:bitcoin » -> ("c", "bitcoin") ; sans préfixe, catégorie devinée."""
    saisie = saisie.strip()
    if ":" in saisie:
        prefixe, valeur = saisie.split(":", 1)
        p = PREFIXES.get(prefixe.strip().lower())
        if p:
            return p, valeur.strip()
    haut = saisie.upper()
    if haut.endswith("=F") or haut in cotations.COMMODITIES:
        return "m", haut
    if haut.startswith("^") or cotations.indice(haut) in cotations.INDICES:
        return "i", saisie
    if haut in cotations.DEVISES or (len(haut) == 3 and haut.isalpha() and images.icone("devises", haut)):
        return "d", haut
    if saisie.lower() in await _symboles_crypto(session):
        return "c", saisie
    return "a", saisie


async def _une(session: aiohttp.ClientSession, saisie: str, avec_courbe: bool) -> tuple[dict, bool]:
    """Une valeur prête à afficher (identifiant préfixé), et si son cours est périmé."""
    p, valeur = await categorie(session, saisie)
    if p == "c":
        try:
            trouves, perime = await btc._trouver(session, [valeur])
        except SourceIndisponible:
            return cours.introuvable(f"c:{valeur.lower()}", valeur.upper()[:5], indisponible=True), False
        c = trouves[0][1]
        if not c or c.get("current_price") is None:
            return cours.introuvable(f"c:{valeur.lower()}", valeur.upper()[:5]), perime
        e = btc._element(c)
        if avec_courbe:
            try:
                points = await btc._courbe_30j(session, c["id"])
                e["p30"] = cours.normaliser(points, c["current_price"])
                e["rep"] = cours.reperes(points, c["current_price"])
            except Exception:  # noqa: BLE001 — sans courbe, la page reste utile
                e["p30"] = []
        e["id"] = f"c:{c['id']}"
        return e, perime
    e = await cotations._cotation(session, PAGES[p], valeur, avec_courbe)
    perime = bool(e.pop("_p", 0))
    e["id"] = f"{p}:{e['id']}"
    return e, perime


async def valeurs(session: aiohttp.ClientSession, q: dict) -> dict:
    """Valeurs choisies (3 au plus). Une seule : courbe sur 30 jours (« p30 », « rep »)."""
    choix = cours.selection(q, "CUSTOM", DEFAUT)
    seule = len(choix) == 1
    sortie, perime = [], False
    for saisie in choix:
        e, p = await _une(session, saisie, seule)
        sortie.append(e)
        perime |= p
    if all(e.get("e") == 2 for e in sortie):
        return {"ok": 0, "err": cotations._erreur()}
    return {"ok": 1, "c": sortie, "p": int(perime)}


async def trente_jours(session: aiohttp.ClientSession, q: dict) -> dict:
    """Une valeur sur 30 jours (page dédiée) : ?id=a:TSLA."""
    saisie = q.get("id", "").strip()
    if not saisie:
        return {"ok": 0, "err": "Valeur inconnue"}
    e, perime = await _une(session, saisie, True)
    if e.get("e"):
        return {"ok": 0, "err": cotations._erreur() if e["e"] == 2 else "Valeur introuvable"}
    return {"ok": 1, "c": [e], "p": int(perime)}


async def liste(session: aiohttp.ClientSession, q: dict) -> dict:
    """Les 5 premières valeurs de chaque page, avec la catégorie devant le nom
    (25 en tout ; toute autre valeur se saisit dans le champ libre)."""
    n = PAR_CATEGORIE
    options = []
    try:
        marches, _ = await btc._marches(session)
        options += [{"id": f"c:{c['id']}", "n": f"Crypto · {c['name']} ({c['symbol'].upper()})"}
                    for c in btc.principales(marches)[:n]]
    except SourceIndisponible:
        pass                                     # les autres catégories restent proposées
    options += [{"id": f"a:{k}", "n": f"Action · {v}"} for k, v in list(cotations.ACTIONS.items())[:n]]
    options += [{"id": f"i:{k}", "n": f"Indice · {v[0]}"} for k, v in list(cotations.INDICES.items())[:n]]
    options += [{"id": f"d:{k}", "n": f"Devise · {v}"} for k, v in list(cotations.DEVISES.items())[:n]]
    options += [{"id": f"m:{k}", "n": f"Matière · {v[0]}"} for k, v in list(cotations.COMMODITIES.items())[:n]]
    return {"ok": 1, "s": options}


MODULE = Module(
    id="custom",
    nom="Custom",
    params=cours.params("ex. c:pepe, a:NFLX"),
    extra=lambda: cours.manifeste("CUSTOM", DEFAUT, src="/custom/liste"),
    routes={"/custom": valeurs, "/custom/30j": trente_jours, "/custom/liste": liste},
    arreter=cotations.fermer,
)
