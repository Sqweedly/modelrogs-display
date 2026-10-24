"""Test de bout en bout de la passerelle, sans Internet.

Un faux serveur local imite Open-Meteo, CoinGecko et Uptime Kuma.
Lancement :  python -m tests.test_passerelle   (depuis le dossier gateway/)
"""

import asyncio
import json
import os
import sys
import tempfile
import time
from datetime import datetime, timedelta, timezone

from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer

MAINTENANT = datetime.now(timezone.utc)
APPELS_GEO: list[str] = []
REQUETES_KUMA: list[dict] = []
FMT = "%Y-%m-%d %H:%M:%S.000"
NOMS = ["NAS", "Home Assistant", "Pi-hole", "Nextcloud", "Plex", "Routeur", "Vaultwarden", "Kuma",
        "Jellyfin", "Grafana", "Gitea", "Paperless", "Immich", "Frigate", "Mosquitto 🦟"]


def faux_internet() -> web.Application:
    app = web.Application()

    async def geo(r):
        APPELS_GEO.append(r.query["name"])
        nom = r.query["name"]
        if nom.lower().startswith("atlantide"):          # ville inexistante
            return web.json_response({"generationtime_ms": 0.1})
        if nom.lower().startswith("ville"):              # villes de test du plafond
            n = int(nom[5:])
            return web.json_response({"results": [{"name": nom, "latitude": 40 + n / 100, "longitude": 2 + n / 100,
                                                   "timezone": "Europe/Paris"}]})
        return web.json_response({"results": [{"name": "Lyon", "latitude": 45.75, "longitude": 4.85,
                                               "timezone": "Europe/Paris"}]})

    async def prev(r):
        return web.json_response({
            "current": {"temperature_2m": 16.6, "weather_code": 2, "is_day": 1},
            "daily": {"time": ["2026-10-23", "2026-10-24", "2026-10-25", "2026-10-26"],
                      "weather_code": [2, 61, 3, 0],
                      "temperature_2m_max": [19.2, 15.1, 16.0, 20.4],
                      "temperature_2m_min": [12.1, 10.0, 9.4, 11.0],
                      "sunrise": ["2026-10-23T08:13", "", "", ""],
                      "sunset": ["2026-10-23T18:39", "", "", ""]}})

    async def cg(r):
        assert r.query["vs_currency"] in ("eur", "usd")
        t0 = int((MAINTENANT - timedelta(days=7)).timestamp() * 1000)
        base = 54000 if r.query["vs_currency"] == "eur" else 62000
        return web.json_response({"prices": [[t0 + i * 3_600_000, base + i * 23 + (i % 7) * 150]
                                             for i in range(169)]})

    async def page(r):
        REQUETES_KUMA.append(dict(r.query))
        mons = [{"id": i, "name": n} for i, n in enumerate(NOMS, start=1)]
        return web.json_response({"publicGroupList": [{"monitorList": mons[:8]}, {"monitorList": mons[8:]}]})

    async def battements(r):
        REQUETES_KUMA.append(dict(r.query))
        hb = {}
        for i in range(1, len(NOMS) + 1):
            if i == 1:   # NAS en panne depuis 4 minutes
                hb["1"] = [{"status": 1, "time": (MAINTENANT - timedelta(minutes=6)).strftime(FMT)},
                           {"status": 0, "time": (MAINTENANT - timedelta(minutes=4)).strftime(FMT)},
                           {"status": 0, "time": (MAINTENANT - timedelta(minutes=1)).strftime(FMT)}]
            else:
                hb[str(i)] = [{"status": {4: 2, 9: 0}.get(i, 1), "time": MAINTENANT.strftime(FMT)}]
        return web.json_response({"heartbeatList": hb})

    app.router.add_get("/geo", geo)
    app.router.add_get("/prev", prev)
    app.router.add_get("/cg", cg)
    app.router.add_get("/kuma/api/status-page/modelrogs", page)
    app.router.add_get("/kuma/api/status-page/heartbeat/modelrogs", battements)
    return app


async def principal() -> int:
    faux = TestServer(faux_internet())
    await faux.start_server()
    base = str(faux.make_url(""))
    os.environ.update({
        "OPEN_METEO_GEO_URL": base + "/geo", "OPEN_METEO_URL": base + "/prev", "COINGECKO_URL": base + "/cg",
        "KUMA_URL": base + "/kuma", "KUMA_STATUS_SLUG": "modelrogs",
        "CACHE_DIR": tempfile.mkdtemp(), "TZ": "Europe/Paris",
        # Discord demandé mais non configuré : doit être signalé, pas planter
        "MODULES": "heure,meteo,btc,supervision,discord,inconnu",
    })
    os.environ.pop("DISCORD_TOKEN", None)
    from app.main import creer_app   # importé après la configuration

    echecs = 0
    async with TestClient(TestServer(creer_app())) as c:
        tous = ",".join(str(i) for i in range(1, 16))
        cas = [
            ("/health", lambda d: d["modules"]["discord"].startswith("non configuré")),
            ("/modules", lambda d: [m["id"] for m in d["m"]] == ["heure", "meteo", "btc", "supervision", "discord"]
                and next(m for m in d["m"] if m["id"] == "discord")["ok"] == 0),
            ("/time?ville=Lyon", lambda d: d["ok"] and d["lv"] == "08:13" and d["o"] in (3600, 7200)),
            ("/meteo?ville=Lyon", lambda d: d["ok"] and d["i"] == 1 and len(d["j"]) == 3),
            ("/btc", lambda d: d["ok"] and d["px"].endswith("€") and len(d["pts"]) == 60),
            ("/btc?devise=usd", lambda d: d["ok"] and d["px"].endswith("$")),
            ("/supervision/services", lambda d: d["ok"] and len(d["s"]) == 15 and isinstance(d["s"][0]["id"], str)),
            ("/supervision?ids=2,3,4,5,6,7,8", lambda d: d["md"] == "t" and d["s"][0]["e"] == 1 and d["al"] is None),
            ("/supervision?ids=1,2,3,4,5,6,7,8", lambda d: d["al"]["n"] == "NAS" and d["al"]["m"] == 4),
            ("/supervision?ids=" + tous, lambda d: d["md"] == "r" and len(d["s"]) == 3 and d["pa"] == 2),
            ("/supervision", lambda d: d["tot"] == 15 and d["md"] == "r" and d["al"]["n"] == "NAS"),
            ("/supervision?ids=*", lambda d: d["tot"] == 15 and d["pa"] == 2),
            ("/supervision?ids=%2A", lambda d: d["tot"] == 15),
            ("/supervision?ids=*,3", lambda d: d["tot"] == 15),
            ("/modules", lambda d: next(m for m in d["m"] if m["id"] == "supervision")["p"][0]["tous"]),
            ("/discord?salon=1", lambda d: d["ok"] == 0 and "Non configuré" in d["err"]),
        ]
        for chemin, verif in cas:
            r = await c.get(chemin)
            texte = await r.text()
            d = json.loads(texte)
            ok = r.status == 200 and len(texte.encode()) < 2048 and verif(d)
            echecs += not ok
            print(f"{'OK ' if ok else 'ÉCHEC'} {chemin:40s} {len(texte.encode()):5d} o  {texte[:110]}")

        # --- Cache : ville inconnue mémorisée, clé normalisée, taille bornée
        from app.cache import cache as C
        from app.cache import CACHE_DIR

        # --- Sonde ajoutée dans Kuma : visible dès l'expiration du cache (1 min)
        from app.modules import supervision as S
        NOMS.append("Nouvelle sonde")
        C._valeurs["kuma-sondes"] = (time.time() - S.TTL_SONDES - 1, C._valeurs["kuma-sondes"][1])
        liste = await (await c.get("/supervision/services")).json()
        etat = await (await c.get("/supervision")).json()
        ok = (S.TTL_SONDES <= 60 and any(s["n"] == "Nouvelle sonde" for s in liste["s"])
              and etat["tot"] == 16 and all(q.get("_") for q in REQUETES_KUMA))
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} sonde ajoutée dans Kuma -> {len(liste['s'])} services listés, "
              f"tot={etat['tot']}, Kuma interrogé sans son cache")
        NOMS.pop()

        APPELS_GEO.clear()
        reponses = [await (await c.get("/meteo?ville=Atlantide")).json() for _ in range(5)]
        ok = all(r == {"ok": 0, "err": "Ville introuvable"} for r in reponses) and len(APPELS_GEO) == 1
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} ville inconnue x5 -> {len(APPELS_GEO)} appel(s) de géocodage")

        APPELS_GEO.clear()
        for v in ("  LYON ", "lyon", "Lyon", "lYoN"):
            await c.get("/meteo?ville=" + v)
        ok = len(APPELS_GEO) == 0
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} « LYON », « lyon »… -> même entrée ({len(APPELS_GEO)} appel)")

        C.max_entrees = 20
        for i in range(60):
            await c.get(f"/time?ville=ville{i}")
        fichiers = len(list(CACHE_DIR.glob("*.json")))
        ok = len(C) <= 20 and fichiers <= 20 and len(C._verrous) <= 20
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} 60 villes, plafond 20 -> {len(C)} entrées, "
              f"{fichiers} fichiers, {len(C._verrous)} verrous")

        C.max_entrees = 200
        # Repli : Kuma tombe -> dernière valeur connue avec p=1
        await c.get("/supervision?ids=1,2")   # réalimente le cache Kuma (évincé par le test de plafond)
        C._valeurs["kuma-battements"] = (0, C._valeurs["kuma-battements"][1])
        await faux.close()
        d = await (await c.get("/supervision?ids=1,2")).json()
        ok = d["ok"] == 1 and d["p"] == 1
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} repli Kuma injoignable -> p=1")

    print("\n" + ("Tous les tests sont passés." if not echecs else f"{echecs} test(s) en échec."))
    return 1 if echecs else 0


if __name__ == "__main__":
    sys.exit(asyncio.run(principal()))
