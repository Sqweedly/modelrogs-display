"""Bot Discord intégré : suit en temps réel les salons vocaux d'un serveur.

Le bot ne lit aucun message, n'écrit rien et ne rejoint pas le vocal :
il écoute seulement les entrées et sorties des salons vocaux.
Permission nécessaire sur le serveur : « Voir les salons ».
Le salon affiché est choisi dans l'interface de l'afficheur (?salon=<id>).
"""

import asyncio
import logging
import os
import time

import aiohttp
import discord

from .. import images
from ..text import ecran
from . import Module

log = logging.getLogger("discord")
# Le bot n'utilise pas la voix : on masque l'avertissement « PyNaCl is not installed ».
logging.getLogger("discord.client").addFilter(lambda r: "PyNaCl" not in r.getMessage())

NOUVEAU_S = 120  # un membre est signalé « arrivé » pendant 2 minutes
MAX_MEMBRES = 8


class Bot:
    def __init__(self) -> None:
        self.token = os.environ.get("DISCORD_TOKEN", "").strip()
        self.guild_id = int(os.environ.get("DISCORD_GUILD_ID") or 0)
        # salon -> {membre: (pseudo, heure d'arrivée)}
        self.salons: dict[int, dict[int, tuple[str, float]]] = {}
        self.evenements: dict[int, int] = {}   # arrivées par salon (priorité côté afficheur)
        self.connecte = False
        self.pret = False
        self.erreur = ""
        self.client: discord.Client | None = None
        self._tache: asyncio.Task | None = None

    # --- cycle de vie ----------------------------------------------------

    def demarrer(self) -> None:
        intents = discord.Intents.none()
        intents.guilds = True
        intents.voice_states = True
        intents.members = os.environ.get("DISCORD_MEMBERS_INTENT", "0") == "1"
        self.client = discord.Client(intents=intents)
        self._brancher()
        self._tache = asyncio.create_task(self._executer())

    async def _executer(self) -> None:
        try:
            await self.client.start(self.token, reconnect=True)
        except discord.LoginFailure:
            self.erreur = "Jeton Discord refusé"
            log.error("Connexion Discord refusée : vérifier DISCORD_TOKEN")
        except discord.PrivilegedIntentsRequired:
            self.erreur = "Intent Server Members à activer"
            log.error("Activer « Server Members Intent » sur la page du bot, ou DISCORD_MEMBERS_INTENT=0")
        except Exception as exc:  # noqa: BLE001
            self.erreur = "Erreur Discord"
            log.exception("Bot Discord arrêté : %s", exc)

    async def arreter(self) -> None:
        if self.client and not self.client.is_closed():
            await self.client.close()

    def etat(self) -> str:
        return "connecté" if self.connecte else (self.erreur or "connexion en cours")

    # --- suivi des salons ------------------------------------------------

    def _serveur(self) -> discord.Guild | None:
        return self.client.get_guild(self.guild_id) if self.client else None

    def _recharger(self) -> None:
        serveur = self._serveur()
        if serveur is None:
            self.erreur = "Serveur Discord introuvable"
            log.error("Serveur %s introuvable : vérifier DISCORD_GUILD_ID et l'invitation du bot",
                      self.guild_id)
            return
        self.erreur = ""
        anciens = self.salons
        self.salons = {}
        for salon in serveur.voice_channels + serveur.stage_channels:
            precedents = anciens.get(salon.id, {})
            self.salons[salon.id] = {
                m.id: (m.display_name, precedents.get(m.id, ("", 0.0))[1]) for m in salon.members
            }

    def _brancher(self) -> None:
        client = self.client

        @client.event
        async def on_ready() -> None:
            self.connecte = self.pret = True
            self._recharger()
            presents = sum(len(m) for m in self.salons.values())
            log.info("Bot connecté : %d salon(s) vocal(aux), %d présent(s)", len(self.salons), presents)

        @client.event
        async def on_resumed() -> None:
            self.connecte = True
            self._recharger()

        @client.event
        async def on_disconnect() -> None:
            self.connecte = False

        @client.event
        async def on_guild_channel_create(salon) -> None:
            self._recharger()

        @client.event
        async def on_guild_channel_delete(salon) -> None:
            self._recharger()

        @client.event
        async def on_voice_state_update(membre, avant, apres) -> None:
            if membre.guild.id != self.guild_id:
                return
            depart = avant.channel.id if avant.channel else None
            arrivee = apres.channel.id if apres.channel else None
            if depart == arrivee:
                if arrivee in self.salons and membre.id in self.salons[arrivee]:
                    self.salons[arrivee][membre.id] = (membre.display_name, self.salons[arrivee][membre.id][1])
                return
            if depart in self.salons:
                self.salons[depart].pop(membre.id, None)
            if arrivee is not None:
                self.salons.setdefault(arrivee, {})[membre.id] = (membre.display_name, time.time())
                self.evenements[arrivee] = self.evenements.get(arrivee, 0) + 1

    # --- réponses à l'afficheur ------------------------------------------

    def _indisponible(self) -> dict | None:
        if self.erreur and not self.pret:
            return {"ok": 0, "err": self.erreur}
        if not self.pret:
            return {"ok": 0, "err": "Connexion à Discord..."}
        return None

    async def liste_salons(self, session: aiohttp.ClientSession, q: dict) -> dict:
        if (refus := self._indisponible()):
            return refus
        serveur = self._serveur()
        salons = sorted(serveur.voice_channels + serveur.stage_channels, key=lambda s: s.position) if serveur else []
        # Identifiants en texte : trop grands pour un entier JSON sûr
        return {"ok": 1, "s": [{"id": str(s.id), "n": s.name[:40]} for s in salons]}

    async def instantane(self, session: aiohttp.ClientSession, q: dict) -> dict:
        if (refus := self._indisponible()):
            return refus
        texte = q.get("salon", "").strip()
        if not texte.isdigit():
            return {"ok": 0, "err": "Salon non choisi"}
        salon_id = int(texte)
        salon = self.client.get_channel(salon_id)
        if salon is None or salon_id not in self.salons:
            return {"ok": 0, "err": "Salon introuvable"}

        maintenant = time.time()
        arrivees = self.salons[salon_id]
        membres = sorted(salon.members, key=lambda m: (arrivees.get(m.id, ("", 0.0))[1], m.display_name.lower()))
        serveur = self._serveur()
        return {
            "ok": 1,
            "g": ecran(serveur.name if serveur else "", 18),
            "gi": images.enregistrer(_icone(serveur.icon) if serveur and serveur.icon else ""),
            "c": ecran(salon.name, 18),
            "n": len(membres),
            "m": [_membre(m, arrivees.get(m.id, ("", 0.0))[1], maintenant) for m in membres[:MAX_MEMBRES]],
            "ev": self.evenements.get(salon_id, 0),
            "p": int(not self.connecte),
        }


# États vocaux (champ « e », bits combinables)
MUET, SOURD, LIVE, CAMERA = 1, 2, 4, 8


def _icone(asset) -> str:
    """Image Discord (avatar, icône de serveur) en PNG 128 px."""
    return str(asset.replace(size=128, format="png").url)


def _depuis(secondes: float) -> str:
    """Durée de présence : « à l'instant », « 48 min », « 1 h 12 »."""
    minutes = int(secondes // 60)
    if minutes < 1:
        return "à l'instant"
    if minutes < 60:
        return f"{minutes} min"
    return f"{minutes // 60} h {minutes % 60:02d}"


def _membre(m, arrivee: float, maintenant: float) -> dict:
    vocal = m.voice
    etat = 0
    if vocal:
        etat |= MUET if (vocal.self_mute or vocal.mute) else 0
        etat |= SOURD if (vocal.self_deaf or vocal.deaf) else 0
        etat |= LIVE if vocal.self_stream else 0
        etat |= CAMERA if vocal.self_video else 0
    return {
        "n": ecran(m.display_name, 14),
        "w": int(0 < arrivee and maintenant - arrivee < NOUVEAU_S),
        # Heure d'arrivée inconnue pour les présents au démarrage du bot : pas de durée
        "d": _depuis(maintenant - arrivee) if arrivee else "",
        "e": etat,
        "i": images.enregistrer(_icone(m.display_avatar)),
    }


bot = Bot()

MODULE = Module(
    id="discord",
    nom="Discord",
    requis=["DISCORD_TOKEN", "DISCORD_GUILD_ID"],
    params=[{"k": "salon", "l": "Salon vocal", "t": "select", "src": "/discord/salons"}],
    routes={"/discord": bot.instantane, "/discord/salons": bot.liste_salons},
    demarrer=bot.demarrer,
    arreter=bot.arreter,
    etat=bot.etat,
)
