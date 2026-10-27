"""Modules de la passerelle.

Chaque module correspond à une page de l'afficheur. Il déclare :
  - les variables d'environnement dont il a besoin (secrets, adresses) ;
  - les réglages que l'utilisateur renseigne dans l'interface de l'afficheur
    (champs décrits dans le manifeste /modules) ;
  - ses routes HTTP.

Types de champs compris par l'afficheur :
  text    zone de texte
  select  liste déroulante : options fixes « o » ou liste dynamique « src »
  multi   cases à cocher, liste dynamique « src », « max » valeurs au plus
Une liste dynamique répond {"ok": 1, "s": [{"id": "...", "n": "..."}]}.
Options d'un champ : « ph » texte indicatif (ou option vide d'une liste),
« g » = 1 champ placé dans une grille de 2 colonnes. Une liste sans « o » ni
« src » prend celles du module (« extra » : « o » ou « src »). « extra » peut
aussi porter « nt » (consigne) et « df » (valeurs par défaut du compose).
L'afficheur renvoie les réglages en paramètres de requête (?cle=valeur,...).
"""

import os
from dataclasses import dataclass, field
from typing import Any, Awaitable, Callable

import aiohttp

Requete = Callable[[aiohttp.ClientSession, dict], Awaitable[dict]]


@dataclass
class Module:
    id: str
    nom: str
    requis: list[str] = field(default_factory=list)
    params: list[dict] = field(default_factory=list)
    routes: dict[str, Requete] = field(default_factory=dict)
    demarrer: Callable[[], Any] | None = None
    arreter: Callable[[], Awaitable[None]] | None = None
    etat: Callable[[], str] | None = None   # état d'exécution (ex. bot connecté)
    extra: dict | Callable[[], dict] = field(default_factory=dict)   # ajouté au manifeste /modules

    def manifeste(self) -> dict:
        return self.extra() if callable(self.extra) else self.extra

    def erreur_config(self) -> str:
        manquantes = [v for v in self.requis if not os.environ.get(v, "").strip()]
        return f"{', '.join(manquantes)} manquant" if manquantes else ""
