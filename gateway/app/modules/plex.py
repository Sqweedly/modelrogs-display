"""Lectures en cours sur un serveur Plex (page visible seulement pendant une lecture).

Source : {PLEX_URL}/status/sessions, avec le jeton PLEX_TOKEN (compte
administrateur du serveur). Une seule lecture : l'afficheur montre l'affiche
(film, série pour un épisode, album pour une musique). Plusieurs lectures :
liste de 3 lignes au plus.

« ev » augmente à chaque nouvelle lecture : l'afficheur s'en sert pour
montrer la page dès qu'une lecture commence.
"""

import os

import aiohttp

from .. import images
from ..cache import cache
from ..http import get_json
from ..text import ecran
from . import Module

MAX_LIGNES = 3


def _plex() -> tuple[str, str]:
    return os.environ.get("PLEX_URL", "").strip().rstrip("/"), os.environ.get("PLEX_TOKEN", "").strip()


def _duree(ms: int) -> str:
    """Position : 42:10 ou 1:05:09."""
    s = max(0, int(ms // 1000))
    h, m, s = s // 3600, s % 3600 // 60, s % 60
    return f"{h}:{m:02d}:{s:02d}" if h else f"{m}:{s:02d}"


def _reste(ms: int) -> str:
    """Temps restant : « reste 1 h 16 », « reste 8 min »."""
    minutes = max(0, round(ms / 60000))
    if minutes >= 60:
        return f"reste {minutes // 60} h {minutes % 60:02d}"
    return f"reste {minutes} min"


def _textes(m: dict) -> tuple[str, str, str]:
    """(titre, sous-titre, image) selon le type de média."""
    genre = m.get("type")
    if genre == "episode":
        saison, episode = m.get("parentIndex"), m.get("index")
        code = f"S{saison} E{episode}" if saison is not None and episode is not None else "Épisode"
        return (m.get("grandparentTitle") or m.get("title", ""),
                f"{code} · {m.get('title', '')}",
                m.get("grandparentThumb") or m.get("thumb"))
    if genre == "track":
        return (m.get("title", ""), m.get("grandparentTitle") or m.get("parentTitle") or "Musique",
                m.get("parentThumb") or m.get("thumb"))
    if genre == "movie":
        annee = m.get("year")
        return m.get("title", ""), f"Film · {annee}" if annee else "Film", m.get("thumb")
    return m.get("title", ""), m.get("librarySectionTitle") or "", m.get("thumb")


def _transcodage(m: dict) -> int:
    ts = m.get("TranscodeSession") or {}
    decisions = (ts.get("videoDecision"), ts.get("audioDecision"))
    return int("transcode" in decisions)


class Suivi:
    """Mémorise les lectures déjà vues pour compter les nouvelles (« ev »)."""

    def __init__(self) -> None:
        self.vues: set[str] = set()
        self.evenements = 0

    def mettre_a_jour(self, cles: set[str]) -> None:
        nouvelles = cles - self.vues
        if nouvelles:
            self.evenements += len(nouvelles)
        self.vues = cles


suivi = Suivi()


async def _sessions(session: aiohttp.ClientSession) -> tuple[list[dict], bool]:
    url, jeton = _plex()

    async def charger() -> list[dict]:
        donnees = await get_json(session, f"{url}/status/sessions", None,
                                 {"X-Plex-Token": jeton, "Accept": "application/json"})
        return (donnees.get("MediaContainer") or {}).get("Metadata") or []

    # 5 s : l'afficheur interroge souvent pour montrer la page dès le début d'une lecture
    return await cache.obtenir("plex-sessions", 5, charger)


async def lectures(session: aiohttp.ClientSession, q: dict) -> dict:
    url, jeton = _plex()
    metas, perime = await _sessions(session)
    suivi.mettre_a_jour({str(m.get("sessionKey") or m.get("key") or i) for i, m in enumerate(metas)})

    # Lectures actives d'abord, puis en pause ; ordre stable
    metas = sorted(metas, key=lambda m: ((m.get("Player") or {}).get("state") == "paused",
                                         str(m.get("sessionKey", ""))))
    lignes = []
    for m in metas[:MAX_LIGNES]:
        titre, sous_titre, image = _textes(m)
        duree, position = int(m.get("duration") or 0), int(m.get("viewOffset") or 0)
        lignes.append({
            "t": ecran(titre, 40),
            "st": ecran(sous_titre, 40),
            "u": ecran((m.get("User") or {}).get("title") or (m.get("Player") or {}).get("title") or "?", 14),
            "pg": round(1000 * position / duree) if duree else 0,
            "pos": _duree(position),
            "r": _reste(duree - position) if duree else "",
            "tc": _transcodage(m),
            "pa": int((m.get("Player") or {}).get("state") == "paused"),
            # Affiche uniquement utile quand il n'y a qu'une lecture
            "i": images.enregistrer(f"{url}{image}", {"X-Plex-Token": jeton})
                 if image and len(metas) == 1 else "",
        })
    return {
        "ok": 1,
        "n": len(metas),
        "tc": sum(_transcodage(m) for m in metas),
        "s": lignes,
        "ev": suivi.evenements,
        "p": int(perime),
    }


MODULE = Module(
    id="plex",
    nom="Plex",
    requis=["PLEX_URL", "PLEX_TOKEN"],
    routes={"/plex": lectures},
)
