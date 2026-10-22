"""Cache mémoire avec durée de vie, repli sur la dernière valeur connue
et persistance facultative sur disque (volume /cache)."""

import asyncio
import json
import logging
import os
import time
from pathlib import Path
from typing import Any, Awaitable, Callable

log = logging.getLogger("cache")

CACHE_DIR = Path(os.environ.get("CACHE_DIR", "/cache"))


class SourceIndisponible(Exception):
    """Aucune donnée fraîche ni ancienne n'est disponible."""


class Cache:
    def __init__(self) -> None:
        self._valeurs: dict[str, tuple[float, Any]] = {}
        self._verrous: dict[str, asyncio.Lock] = {}

    def _verrou(self, cle: str) -> asyncio.Lock:
        return self._verrous.setdefault(cle, asyncio.Lock())

    async def obtenir(
        self,
        cle: str,
        ttl: float,
        charger: Callable[[], Awaitable[Any]],
        persistant: bool = False,
    ) -> tuple[Any, bool]:
        """Renvoie (donnée, périmée).

        - donnée fraîche en cache : renvoyée telle quelle ;
        - sinon on recharge ; en cas d'échec on renvoie la dernière valeur
          connue avec périmée=True ;
        - rien du tout : SourceIndisponible.
        """
        entree = self._valeurs.get(cle)
        if entree is None and persistant:
            entree = self._lire_disque(cle)
            if entree is not None:
                self._valeurs[cle] = entree

        if entree is not None and time.time() - entree[0] < ttl:
            return entree[1], False

        async with self._verrou(cle):
            entree = self._valeurs.get(cle)
            if entree is not None and time.time() - entree[0] < ttl:
                return entree[1], False
            try:
                valeur = await charger()
            except Exception as exc:  # noqa: BLE001 — toute erreur de source
                log.warning("Source %s en échec : %s", cle, exc)
                if entree is not None:
                    return entree[1], True
                raise SourceIndisponible(str(exc)) from exc

            self._valeurs[cle] = (time.time(), valeur)
            if persistant:
                self._ecrire_disque(cle, self._valeurs[cle])
            return valeur, False

    # --- persistance -----------------------------------------------------

    @staticmethod
    def _fichier(cle: str) -> Path:
        nom = "".join(c if c.isalnum() or c in "-_" else "_" for c in cle)
        return CACHE_DIR / f"{nom}.json"

    def _lire_disque(self, cle: str) -> tuple[float, Any] | None:
        try:
            contenu = json.loads(self._fichier(cle).read_text(encoding="utf-8"))
            return float(contenu["t"]), contenu["v"]
        except (OSError, ValueError, KeyError):
            return None

    def _ecrire_disque(self, cle: str, entree: tuple[float, Any]) -> None:
        try:
            CACHE_DIR.mkdir(parents=True, exist_ok=True)
            self._fichier(cle).write_text(
                json.dumps({"t": entree[0], "v": entree[1]}, ensure_ascii=False),
                encoding="utf-8",
            )
        except OSError as exc:
            log.warning("Écriture du cache %s impossible : %s", cle, exc)


cache = Cache()
