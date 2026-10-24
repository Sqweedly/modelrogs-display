"""Cache mémoire borné, avec durée de vie, repli sur la dernière valeur
connue et persistance facultative sur disque (volume /cache).

Limites :
  - MAX_ENTREES entrées au plus ; au-delà, les moins récemment utilisées
    sont supprimées (mémoire, verrou et fichier disque) ;
  - au démarrage, les fichiers disque de plus d'un an sont supprimés et
    seuls les MAX_ENTREES plus récents sont gardés.
"""

import asyncio
import hashlib
import json
import logging
import os
import time
from collections import OrderedDict
from pathlib import Path
from typing import Any, Awaitable, Callable

log = logging.getLogger("cache")

CACHE_DIR = Path(os.environ.get("CACHE_DIR", "/cache"))
MAX_ENTREES = int(os.environ.get("CACHE_MAX_ENTREES", "200"))
AGE_MAX_DISQUE = 365 * 86400


class SourceIndisponible(Exception):
    """Aucune donnée fraîche ni ancienne n'est disponible."""


class Cache:
    def __init__(self, max_entrees: int = MAX_ENTREES) -> None:
        self.max_entrees = max_entrees
        self._valeurs: "OrderedDict[str, tuple[float, Any]]" = OrderedDict()
        self._persistantes: set[str] = set()
        self._verrous: dict[str, asyncio.Lock] = {}
        self._alerte_droits = False

    def __len__(self) -> int:
        return len(self._valeurs)

    # --- accès ---------------------------------------------------------------

    def _verrou(self, cle: str) -> asyncio.Lock:
        return self._verrous.setdefault(cle, asyncio.Lock())

    def _ranger(self, cle: str, entree: tuple[float, Any], persistant: bool) -> None:
        self._valeurs[cle] = entree
        self._valeurs.move_to_end(cle)
        if persistant:
            self._persistantes.add(cle)
        while len(self._valeurs) > self.max_entrees:
            ancienne, _ = self._valeurs.popitem(last=False)
            self._oublier(ancienne)

    def _oublier(self, cle: str) -> None:
        verrou = self._verrous.get(cle)
        if verrou is not None and not verrou.locked():
            del self._verrous[cle]
        if cle in self._persistantes:
            self._persistantes.discard(cle)
            try:
                self._fichier(cle).unlink(missing_ok=True)
            except OSError:
                pass

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
        - rien du tout : SourceIndisponible (l'erreur d'origine est dans __cause__).
        """
        entree = self._valeurs.get(cle)
        if entree is None and persistant:
            entree = self._lire_disque(cle)
            if entree is not None:
                self._ranger(cle, entree, persistant)

        if entree is not None:
            self._valeurs.move_to_end(cle)
            if time.time() - entree[0] < ttl:
                return entree[1], False

        verrou = self._verrou(cle)
        try:
            async with verrou:
                entree = self._valeurs.get(cle, entree)
                if entree is not None and time.time() - entree[0] < ttl:
                    return entree[1], False
                try:
                    valeur = await charger()
                except Exception as exc:  # noqa: BLE001 — toute erreur de source
                    log.warning("Source %s en échec : %s", cle, exc)
                    if entree is not None:
                        return entree[1], True
                    raise SourceIndisponible(str(exc)) from exc

                nouvelle = (time.time(), valeur)
                self._ranger(cle, nouvelle, persistant)
                if persistant:
                    self._ecrire_disque(cle, nouvelle)
                return valeur, False
        finally:
            # Verrou d'une clé absente du cache (échec, ou clé évincée) : supprimé
            if cle not in self._valeurs and not verrou.locked():
                self._verrous.pop(cle, None)

    # --- persistance ---------------------------------------------------------

    @staticmethod
    def _fichier(cle: str) -> Path:
        # Préfixe lisible + empreinte : aucun conflit entre deux clés proches
        lisible = "".join(c if c.isascii() and (c.isalnum() or c in "-_") else "_" for c in cle)[:40]
        empreinte = hashlib.sha1(cle.encode("utf-8")).hexdigest()[:10]
        return CACHE_DIR / f"{lisible}-{empreinte}.json"

    def _lire_disque(self, cle: str) -> tuple[float, Any] | None:
        try:
            contenu = json.loads(self._fichier(cle).read_text(encoding="utf-8"))
            if contenu.get("k") != cle:
                return None
            return float(contenu["t"]), contenu["v"]
        except (OSError, ValueError, KeyError, AttributeError):
            return None

    def _ecrire_disque(self, cle: str, entree: tuple[float, Any]) -> None:
        try:
            CACHE_DIR.mkdir(parents=True, exist_ok=True)
            self._fichier(cle).write_text(
                json.dumps({"k": cle, "t": entree[0], "v": entree[1]}, ensure_ascii=False),
                encoding="utf-8",
            )
        except PermissionError:
            if not self._alerte_droits:   # un seul message, pas un par écriture
                self._alerte_droits = True
                log.warning("Cache disque %s non inscriptible (utilisateur %s) : le cache reste en mémoire. "
                            "Utilisez le volume nommé du docker-compose.yml, ou : "
                            "sudo chown -R 10001:10001 <dossier du cache>", CACHE_DIR, os.getuid())
        except OSError as exc:
            log.warning("Écriture du cache %s impossible : %s", cle, exc)

    def nettoyer_disque(self) -> None:
        """Au démarrage : supprime les fichiers trop anciens ou en surnombre."""
        try:
            fichiers = sorted(CACHE_DIR.glob("*.json"), key=lambda f: f.stat().st_mtime, reverse=True)
        except OSError:
            return
        limite = time.time() - AGE_MAX_DISQUE
        supprimes = 0
        for rang, fichier in enumerate(fichiers):
            try:
                if rang >= self.max_entrees or fichier.stat().st_mtime < limite:
                    fichier.unlink()
                    supprimes += 1
            except OSError:
                pass
        if supprimes:
            log.info("Cache disque : %d fichier(s) supprimé(s)", supprimes)


cache = Cache()
