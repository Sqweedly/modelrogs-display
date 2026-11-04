"""Batterie d'une Tesla, lue sur le serveur MQTT de TeslaMate.

TeslaMate publie chaque donnée de la voiture sur un sujet
« teslamate/[espace/]cars/<n°>/<donnée> », en message retenu (sauf
« healthy ») : à la connexion, le serveur renvoie aussitôt la dernière valeur
connue, même si la voiture dort. La passerelle ne s'abonne qu'aux données
utiles à l'écran (jamais à la position).

États envoyés à l'afficheur (« e ») :
  0 en route    state = driving
  1 stationnée  ni en route, ni branchée
  2 branchée    plugged_in = true, sans charge en cours (charge terminée comprise)
  3 en charge   charging_state = Charging (page prioritaire au début d'une charge)

« ev » augmente à chaque début de charge : l'afficheur s'en sert pour montrer
la page aussitôt (comme Plex au début d'une lecture). « p » = 1 : la passerelle
n'est plus connectée au serveur MQTT, les valeurs sont les dernières reçues.

Photos : une par état, 240 x 168, recadrée au centre. Dossier personnel
/icones/tesla/ (route, stationnee, branchee, charge : .png, .jpg…), sinon
image provisoire incluse dans la passerelle.
"""

import asyncio
import logging
import os
import time
import uuid

import aiomqtt

from .. import images
from ..text import ecran
from . import Module

log = logging.getLogger("tesla")

ROUTE, STATIONNEE, BRANCHEE, CHARGE = 0, 1, 2, 3
PHOTOS = {ROUTE: "route", STATIONNEE: "stationnee", BRANCHEE: "branchee", CHARGE: "charge"}
# Données lues (le reste de ce que publie TeslaMate est ignoré)
SUJETS = ("state", "battery_level", "rated_battery_range_km", "est_battery_range_km",
          "plugged_in", "charging_state", "charge_limit_soc", "time_to_full_charge")
PAUSES_S = (5, 10, 30, 60)   # nouvel essai de connexion au serveur MQTT
AMORCAGE_S = 5               # après la connexion : messages retenus, pas de « début de charge »


def _reglages() -> dict:
    espace = os.environ.get("TESLA_MQTT_ESPACE", "").strip().strip("/")
    voiture = os.environ.get("TESLA_VOITURE", "1").strip() or "1"
    try:
        port = int(os.environ.get("TESLA_MQTT_PORT", "1883"))
    except ValueError:
        port = 1883
    return {
        "hote": os.environ.get("TESLA_MQTT_HOTE", "").strip(),
        "port": port,
        "utilisateur": os.environ.get("TESLA_MQTT_UTILISATEUR", "").strip() or None,
        "mot_de_passe": os.environ.get("TESLA_MQTT_MOT_DE_PASSE", "") or None,
        "prefixe": "teslamate/" + (f"{espace}/" if espace else "") + f"cars/{voiture}/",
    }


def _valeur(brut: bytes | str) -> str | None:
    """Charge utile -> texte ; vide ou « nil » (valeur absente chez TeslaMate) -> None."""
    texte = (brut.decode("utf-8", "replace") if isinstance(brut, bytes) else str(brut)).strip()
    return None if texte in ("", "nil", "null") else texte


def _nombre(texte: str | None) -> float | None:
    try:
        return float(texte) if texte is not None else None
    except ValueError:
        return None


def duree(heures: float | None) -> str:
    """time_to_full_charge (heures décimales) -> « 1 h 05 min », « 45 min », "" si inconnu."""
    if heures is None or heures <= 0:
        return ""
    minutes = max(1, round(heures * 60))
    return f"{minutes // 60} h {minutes % 60:02d} min" if minutes >= 60 else f"{minutes} min"


class Voiture:
    """Dernières valeurs reçues, état de la connexion, compteur de débuts de charge."""

    def __init__(self) -> None:
        self.valeurs: dict[str, str | None] = {}
        self.connecte = False
        self.erreur = ""
        self.evenements = 0
        self._fin_amorcage = 0.0
        self._tache: asyncio.Task | None = None

    # --- données -----------------------------------------------------------

    def recevoir(self, donnee: str, brut: bytes | str) -> None:
        avant = self.etat() == CHARGE
        self.valeurs[donnee] = _valeur(brut)
        # Début d'une charge ; pas pendant l'envoi des messages retenus qui suit
        # la connexion (une charge déjà en cours n'est pas un début)
        if self.etat() == CHARGE and not avant and time.monotonic() >= self._fin_amorcage:
            self.evenements += 1

    def etat(self) -> int:
        v = self.valeurs
        if v.get("charging_state") == "Charging":
            return CHARGE
        if (v.get("state") or "") == "driving":
            return ROUTE
        if (v.get("plugged_in") or "").lower() == "true":
            return BRANCHEE
        return STATIONNEE

    def ligne(self, etat: int) -> str:
        """Ligne d'information sous le pourcentage."""
        v = self.valeurs
        limite = _nombre(v.get("charge_limit_soc"))
        if etat == CHARGE:
            if limite is None:
                return "En charge"
            reste = duree(_nombre(v.get("time_to_full_charge")))
            return f"{limite:.0f} % dans {reste}" if reste else f"Fin de charge à {limite:.0f} %"
        if etat == BRANCHEE:
            return f"Limite de charge : {limite:.0f} %" if limite is not None else ""
        km = _nombre(v.get("rated_battery_range_km"))
        if km is None:
            km = _nombre(v.get("est_battery_range_km"))
        return f"Autonomie {km:.0f} km" if km is not None else ""

    def photo(self, etat: int) -> str:
        nom = PHOTOS[etat]
        return images.enregistrer([images.perso("tesla", nom), images.icone("tesla", nom)])

    # --- connexion au serveur MQTT -----------------------------------------

    def demarrer(self) -> None:
        self._tache = asyncio.create_task(self._executer())

    async def arreter(self) -> None:
        if self._tache:
            self._tache.cancel()
            try:
                await self._tache
            except (asyncio.CancelledError, Exception):  # noqa: BLE001
                pass

    def etat_connexion(self) -> str:
        if self.connecte:
            return "connecté" if "battery_level" in self.valeurs else "connecté, en attente de TeslaMate"
        return self.erreur or "connexion en cours"

    async def _executer(self) -> None:
        r = _reglages()
        echecs = 0
        while True:
            try:
                async with aiomqtt.Client(
                    r["hote"], r["port"], username=r["utilisateur"], password=r["mot_de_passe"],
                    identifier=f"modelrogs-{uuid.uuid4().hex[:8]}", keepalive=30, timeout=10,
                ) as client:
                    for sujet in SUJETS:
                        await client.subscribe(r["prefixe"] + sujet, qos=1)
                    self.connecte, self.erreur, echecs = True, "", 0
                    self._fin_amorcage = time.monotonic() + AMORCAGE_S
                    log.info("TeslaMate : connecté à %s:%d (%s…)", r["hote"], r["port"], r["prefixe"])
                    async for message in client.messages:
                        sujet = str(message.topic)
                        if sujet.startswith(r["prefixe"]):
                            self.recevoir(sujet[len(r["prefixe"]):], message.payload)
            except asyncio.CancelledError:
                raise
            except Exception as exc:  # noqa: BLE001 — MqttError, nom d'hôte inconnu… : nouvel essai
                if self.connecte or echecs == 0:
                    log.warning("TeslaMate : serveur MQTT %s:%d injoignable (%s)", r["hote"], r["port"], exc)
                self.erreur = "serveur MQTT injoignable"
            self.connecte = False
            await asyncio.sleep(PAUSES_S[min(echecs, len(PAUSES_S) - 1)])
            echecs += 1


voiture = Voiture()


async def batterie(session, q: dict) -> dict:
    niveau = _nombre(voiture.valeurs.get("battery_level"))
    if niveau is None:
        return {"ok": 0, "err": "En attente de TeslaMate" if voiture.connecte else "TeslaMate injoignable"}
    etat = voiture.etat()
    return {
        "ok": 1,
        "e": etat,
        "b": max(0, min(100, round(niveau))),
        "l": ecran(voiture.ligne(etat), 40),
        "i": voiture.photo(etat),
        "ev": voiture.evenements,
        "p": int(not voiture.connecte),
    }


MODULE = Module(
    id="tesla",
    nom="Tesla",
    requis=["TESLA_MQTT_HOTE"],
    routes={"/tesla": batterie},
    demarrer=voiture.demarrer,
    arreter=voiture.arreter,
    etat=voiture.etat_connexion,
    extra={"nt": "Affichée en route, stationnée ou branchée ; montrée aussitôt au début d'une charge."},
)
