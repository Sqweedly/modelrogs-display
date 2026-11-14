"""Vérification des listes des mosaïques (3.5.0) auprès de Yahoo Finance.

Interroge chaque symbole des listes fixes (Actions Monde, Actions Europe) et
des listes intégrées (Indices, Matières premières), une requête par seconde,
et affiche ceux que Yahoo ne connaît pas.

Lancement, sur la machine de la passerelle :
    docker exec <conteneur de la passerelle> python -m app.verifier
    docker exec <conteneur de la passerelle> python -m app.verifier europe
Catégories : actions, europe, indices, commodities (toutes par défaut).
Code de sortie : 0 si tout est connu, 1 sinon.
"""

import asyncio
import sys

import aiohttp

from .cache import SourceIndisponible
from .modules import cotations, marches


async def verifier(categories: list[str]) -> int:
    inconnus = 0
    async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=15)) as session:
        for cat in categories:
            if cat in marches.FICHIERS:
                li = marches.liste(cat)
                age = li.age()
                print(f"\n== {cat} : {len(li.valeurs)} valeurs, {li.source}")
                print(f"   mise à jour : {li.date.isoformat() if li.date else '?'}"
                      + (f" (il y a {age} jours{', à revoir' if li.vieille() else ''})" if age is not None else
                         " (date absente : ligne « # mise à jour : AAAA-MM-JJ »)"))
            else:
                print(f"\n== {cat}")
            for ident, symbole, code in marches.symboles(cat):
                try:
                    g = await cotations._graphique(session, symbole)
                    p24, _, _ = cotations._variations(g)
                    print(f"   OK      {symbole:12s} {code:6s} {g['px']:>14,.2f} {g['dev']:4s}"
                          f" 24 h {p24:+.1f} %" if p24 is not None else "")
                except cotations.Introuvable:
                    inconnus += 1
                    print(f"   INCONNU {symbole:12s} {code:6s} symbole inconnu de Yahoo : à corriger")
                except SourceIndisponible as exc:
                    inconnus += 1
                    print(f"   ÉCHEC   {symbole:12s} {code:6s} {exc.__cause__ or exc}")
                await asyncio.sleep(1.0)
    await cotations.fermer()
    print(f"\n{inconnus} symbole(s) à revoir." if inconnus else "\nTous les symboles sont connus de Yahoo.")
    return 1 if inconnus else 0


def main() -> None:
    demandees = [a.lower() for a in sys.argv[1:]] or ["actions", "europe", "indices", "commodities"]
    inconnues = [c for c in demandees if c not in ("actions", "europe", "indices", "commodities")]
    if inconnues:
        print(f"Catégorie inconnue : {', '.join(inconnues)} (actions, europe, indices, commodities)")
        sys.exit(2)
    sys.exit(asyncio.run(verifier(demandees)))


if __name__ == "__main__":
    main()
