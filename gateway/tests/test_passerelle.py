"""Test de bout en bout de la passerelle, sans Internet.

Un faux serveur local imite Open-Meteo, CoinGecko et Uptime Kuma.
Lancement :  python -m tests.test_passerelle   (depuis le dossier gateway/)
"""

import asyncio
import json
import os
import sys
import tempfile
from datetime import datetime, timedelta, timezone

from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer

MAINTENANT = datetime.now(timezone.utc)
FMT = "%Y-%m-%d %H:%M:%S.000"
NOMS = ["NAS", "Home Assistant", "Pi-hole", "Nextcloud", "Plex", "Routeur", "Vaultwarden", "Kuma",
        "Jellyfin", "Grafana", "Gitea", "Paperless", "Immich", "Frigate", "Mosquitto 🦟"]


def faux_internet() -> web.Application:
    app = web.Application()

    async def geo(r):
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
        mons = [{"id": i, "name": n} for i, n in enumerate(NOMS, start=1)]
        return web.json_response({"publicGroupList": [{"monitorList": mons[:8]}, {"monitorList": mons[8:]}]})

    async def battements(r):
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
            ("/supervision", lambda d: d["tot"] == 0),
            ("/discord?salon=1", lambda d: d["ok"] == 0 and "Non configuré" in d["err"]),
        ]
        for chemin, verif in cas:
            r = await c.get(chemin)
            texte = await r.text()
            d = json.loads(texte)
            ok = r.status == 200 and len(texte.encode()) < 2048 and verif(d)
            echecs += not ok
            print(f"{'OK ' if ok else 'ÉCHEC'} {chemin:40s} {len(texte.encode()):5d} o  {texte[:110]}")

        # Repli : Kuma tombe -> dernière valeur connue avec p=1
        from app.cache import cache
        cache._valeurs["kuma-battements"] = (0, cache._valeurs["kuma-battements"][1])
        await faux.close()
        d = await (await c.get("/supervision?ids=1,2")).json()
        ok = d["ok"] == 1 and d["p"] == 1
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} repli Kuma injoignable -> p=1")

    print("\n" + ("Tous les tests sont passés." if not echecs else f"{echecs} test(s) en échec."))
    return 1 if echecs else 0


if __name__ == "__main__":
    sys.exit(asyncio.run(principal()))
