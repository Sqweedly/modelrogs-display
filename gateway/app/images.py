"""Images prêtes à afficher pour l'ESP (affiches Plex, logos, avatars Discord).

L'ESP8266 n'a pas la mémoire pour décoder confortablement un JPEG ou un PNG.
La passerelle fait donc tout le travail :
  1. un module enregistre l'adresse d'une image et obtient un jeton court
     (`enregistrer`), qu'il glisse dans sa réponse JSON ;
  2. l'afficheur demande /img?t=<jeton>&w=..&h=..&f=<fond>&r=<rayon> ;
  3. la passerelle télécharge l'image (une fois, en cache), la recadre,
     arrondit les coins (ou la découpe en cercle), la pose sur la couleur de
     fond de l'écran, et renvoie les pixels bruts : w*h valeurs RGB565 de
     16 bits, petit-boutistes, ligne par ligne.

L'afficheur dessine ces lignes au fur et à mesure de leur réception, sans
jamais garder l'image entière en mémoire.

Un jeton peut désigner plusieurs sources, essayées dans l'ordre (ex. icône
personnelle, logo.dev, icône incluse dans l'image Docker, dépôt GitHub). Une
source locale s'écrit « fichier:<chemin> ». Si aucune ne répond, l'échec est
mémorisé une heure : l'afficheur dessine alors sa pastille de remplacement sans
attendre.

Icônes personnelles : un dossier de l'hôte monté sur /icones (ICONES_PERSO),
rangé comme app/icones (actions/, devises/, commodities/, crypto/), passe avant
toute autre source. Il est relu toutes les 30 secondes ; la date et la taille
du fichier entrent dans le jeton, si bien qu'une icône ajoutée, remplacée ou
supprimée est prise en compte sans redémarrer. Voir docs/PASSERELLE.md.
"""

import asyncio
import hashlib
import io
import sys
import logging
import os
import re
import time
from pathlib import Path
from array import array
from collections import OrderedDict

import aiohttp
from aiohttp import web
from PIL import Image, ImageChops, ImageDraw

log = logging.getLogger("images")

MAX_COTE = 160            # pixels, par côté (l'écran fait 240)
MAX_SOURCES = 300         # jetons connus
MAX_ORIGINAUX = 40        # images téléchargées gardées en mémoire
MAX_RENDUS = 120          # images converties gardées en mémoire
TAILLE_MAX_SOURCE = 4 * 1024 * 1024
DUREE_ECHEC = 3600        # secondes : image introuvable, pas de nouvel essai avant
ICONES = Path(__file__).resolve().parent / "icones"   # icônes incluses dans l'image Docker
ICONES_PERSO = Path(os.environ.get("ICONES_PERSO", "/icones"))   # icônes de l'utilisateur
CATEGORIES_PERSO = ("actions", "devises", "commodities", "crypto")
EXTENSIONS = (".png", ".webp", ".jpg", ".jpeg")    # par ordre de préférence
RELECTURE_PERSO = 30      # secondes entre deux relectures du dossier personnel

_sources: "OrderedDict[str, tuple[list[str], dict]]" = OrderedDict()
_echecs: dict[str, float] = {}
_originaux: "OrderedDict[str, bytes]" = OrderedDict()
_rendus: "OrderedDict[tuple, bytes]" = OrderedDict()
_verrous: dict[str, asyncio.Lock] = {}
_perso: dict[str, dict[str, Path]] = {}
_perso_lu = -RELECTURE_PERSO


def _ranger(dico: OrderedDict, cle, valeur, maximum: int) -> None:
    dico[cle] = valeur
    dico.move_to_end(cle)
    while len(dico) > maximum:
        dico.popitem(last=False)


def enregistrer(url: str | list[str] | None, entetes: dict | None = None) -> str:
    """Adresse(s) d'image -> jeton court à transmettre à l'afficheur ("" si aucune).
    Plusieurs adresses : essayées dans l'ordre."""
    urls = [u for u in ([url] if isinstance(url, str) else (url or [])) if u]
    if not urls:
        return ""
    jeton = hashlib.sha1("|".join(urls).encode("utf-8")).hexdigest()[:12]
    _ranger(_sources, jeton, (urls, entetes or {}), MAX_SOURCES)
    return jeton


def icone(categorie: str, nom: str) -> str:
    """Source « fichier: » d'une icône incluse, "" si le fichier n'existe pas."""
    chemin = ICONES / categorie / f"{nom}.png"
    return f"fichier:{chemin}" if chemin.is_file() else ""


def _lire_perso() -> dict[str, dict[str, Path]]:
    """Index du dossier personnel : catégorie -> nom en minuscules -> fichier."""
    index: dict[str, dict[str, Path]] = {}
    for categorie in CATEGORIES_PERSO:
        rang: dict[str, tuple[int, Path]] = {}
        try:
            fichiers = list((ICONES_PERSO / categorie).iterdir())
        except OSError:                      # dossier absent ou illisible
            fichiers = []
        for f in fichiers:
            ext = f.suffix.lower()
            if ext in EXTENSIONS and f.is_file():
                cle, pref = f.stem.lower(), EXTENSIONS.index(ext)
                if cle not in rang or pref < rang[cle][0]:
                    rang[cle] = (pref, f)
        index[categorie] = {cle: f for cle, (_, f) in rang.items()}
    return index


def perso(categorie: str, nom: str) -> str:
    """Source d'une icône personnelle, "" s'il n'y en a pas. Sans tenir compte
    des majuscules : devises/eur.png convient pour EUR."""
    global _perso, _perso_lu
    if not nom:
        return ""
    if time.monotonic() - _perso_lu >= RELECTURE_PERSO:
        _perso, _perso_lu = _lire_perso(), time.monotonic()
    chemin = _perso.get(categorie, {}).get(nom.strip().lower())
    if chemin is None:
        return ""
    try:
        etat = chemin.stat()
    except OSError:
        return ""
    return f"fichier:{chemin}#{etat.st_mtime_ns}-{etat.st_size}"   # nouvelle version = nouveau jeton


def inventaire_perso() -> None:
    """Au démarrage : nombre d'icônes personnelles, fichiers inutilisables."""
    if not ICONES_PERSO.is_dir():
        return
    global _perso, _perso_lu
    _perso, _perso_lu = _lire_perso(), time.monotonic()
    total = 0
    for categorie, fichiers in _perso.items():
        for chemin in fichiers.values():
            try:
                if chemin.stat().st_size > TAILLE_MAX_SOURCE:
                    raise ValueError("plus de 4 Mo")
                with Image.open(chemin) as image:
                    image.verify()
                total += 1
            except Exception as exc:  # noqa: BLE001 — signalé, ignoré à l'affichage
                log.warning("Icône personnelle inutilisable : %s/%s (%s)", categorie, chemin.name, exc)
    inconnus = sorted(d.name for d in ICONES_PERSO.iterdir() if d.is_dir() and d.name not in CATEGORIES_PERSO)
    log.info("Icônes personnelles : %d dans %s%s", total, ICONES_PERSO,
             f" (dossiers ignorés : {', '.join(inconnus)})" if inconnus else "")


async def _telecharger(session: aiohttp.ClientSession, url: str, entetes: dict) -> bytes:
    if url.startswith("fichier:"):
        chemin = Path(re.sub(r"#\d+-\d+$", "", url[len("fichier:"):]))
        if chemin.stat().st_size > TAILLE_MAX_SOURCE:
            raise ValueError("image trop lourde")
        donnees = chemin.read_bytes()
        Image.open(io.BytesIO(donnees)).verify()
        return donnees
    # Lecture jusqu'à la fin du fichier : un serveur réel envoie l'image en
    # plusieurs morceaux (content.read(n) ne rendrait que le premier).
    morceaux, taille = [], 0
    async with session.get(url, headers=entetes) as reponse:
        reponse.raise_for_status()
        async for morceau in reponse.content.iter_chunked(64 * 1024):
            taille += len(morceau)
            if taille > TAILLE_MAX_SOURCE:
                raise ValueError("image trop lourde")
            morceaux.append(morceau)
    donnees = b"".join(morceaux)
    Image.open(io.BytesIO(donnees)).verify()   # une page d'erreur n'est pas une image
    return donnees


async def _original(session: aiohttp.ClientSession, jeton: str) -> bytes:
    if jeton in _originaux:
        _originaux.move_to_end(jeton)
        return _originaux[jeton]
    urls, entetes = _sources[jeton]
    verrou = _verrous.setdefault(jeton, asyncio.Lock())
    try:
        async with verrou:
            if jeton in _originaux:
                return _originaux[jeton]
            erreur: Exception = ValueError("aucune source")
            for url in urls:
                try:
                    donnees = await _telecharger(session, url, entetes)
                except Exception as exc:  # noqa: BLE001 — source suivante
                    erreur = exc
                    continue
                _ranger(_originaux, jeton, donnees, MAX_ORIGINAUX)
                return donnees
            raise erreur
    finally:
        if not verrou.locked():
            _verrous.pop(jeton, None)


def _rgb565(image: Image.Image) -> bytes:
    """RGB 8 bits -> RGB565 petit-boutiste (ordre natif de l'ESP8266)."""
    brut = image.tobytes()
    sortie = array("H", bytes(len(brut) // 3 * 2))
    for i in range(len(sortie)):
        r, g, b = brut[3 * i], brut[3 * i + 1], brut[3 * i + 2]
        sortie[i] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    if sys.byteorder != "little":
        sortie.byteswap()
    return sortie.tobytes()


def convertir(donnees: bytes, w: int, h: int, fond: tuple[int, int, int], rayon: int,
              ajuster: bool = False) -> bytes:
    """Recadre (ou ajuste, pour un logo) l'image en w x h, arrondit, pose sur le fond."""
    source = Image.open(io.BytesIO(donnees))
    source.draft("RGB", (w * 2, h * 2))          # JPEG : décodage réduit, plus rapide
    source = source.convert("RGBA")

    if ajuster:   # logo : entier, centré, sans déformation
        source.thumbnail((w, h), Image.LANCZOS)
        image = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        image.paste(source, ((w - source.width) // 2, (h - source.height) // 2))
    else:         # affiche, avatar : remplit tout le cadre, recadrage au centre
        echelle = max(w / source.width, h / source.height)
        tw, th = max(w, round(source.width * echelle)), max(h, round(source.height * echelle))
        source = source.resize((tw, th), Image.LANCZOS)
        g, t = (tw - w) // 2, (th - h) // 2
        image = source.crop((g, t, g + w, t + h))

    # Coins arrondis / cercle, lissés (masque dessiné 4x plus grand)
    if rayon > 0:
        k = 4
        masque = Image.new("L", (w * k, h * k), 0)
        ImageDraw.Draw(masque).rounded_rectangle((0, 0, w * k - 1, h * k - 1), rayon * k, fill=255)
        masque = masque.resize((w, h), Image.LANCZOS)
        image.putalpha(ImageChops.multiply(image.getchannel("A"), masque))

    base = Image.new("RGBA", (w, h), fond + (255,))
    base.alpha_composite(image)
    return _rgb565(base.convert("RGB"))


def _entier(q: dict, cle: str, defaut: int, mini: int, maxi: int) -> int:
    try:
        return max(mini, min(maxi, int(q.get(cle, defaut))))
    except ValueError:
        return defaut


async def route_image(request: web.Request, session: aiohttp.ClientSession) -> web.Response:
    q = request.query
    jeton = q.get("t", "")
    if jeton not in _sources:
        return web.Response(status=404, text="image inconnue")
    if time.time() - _echecs.get(jeton, 0) < DUREE_ECHEC:
        return web.Response(status=404, text="image introuvable")
    w = _entier(q, "w", 32, 1, MAX_COTE)
    h = _entier(q, "h", 32, 1, MAX_COTE)
    rayon = _entier(q, "r", 0, 0, MAX_COTE)
    ajuster = q.get("m", "") == "a"
    try:
        fond = bytes.fromhex(q.get("f", "000000")[:6].rjust(6, "0"))
    except ValueError:
        fond = b"\x00\x00\x00"
    cle = (jeton, w, h, fond, rayon, ajuster)

    if cle in _rendus:
        _rendus.move_to_end(cle)
        corps = _rendus[cle]
    else:
        try:
            donnees = await _original(session, jeton)
            corps = await asyncio.get_running_loop().run_in_executor(
                None, convertir, donnees, w, h, tuple(fond), rayon, ajuster)
        except Exception as exc:  # noqa: BLE001 — l'afficheur dessine alors un remplaçant
            log.warning("Image %s indisponible : %s", jeton, exc)
            _echecs[jeton] = time.time()
            if len(_echecs) > MAX_SOURCES:
                _echecs.pop(next(iter(_echecs)))
            return web.Response(status=502, text="image indisponible")
        _ranger(_rendus, cle, corps, MAX_RENDUS)
    return web.Response(body=corps, content_type="application/octet-stream",
                        headers={"Cache-Control": "no-store"})
