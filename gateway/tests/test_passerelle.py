"""Test de bout en bout de la passerelle, sans Internet.

Un faux serveur local imite Open-Meteo, CoinGecko et Uptime Kuma.
Lancement :  python -m tests.test_passerelle   (depuis le dossier gateway/)
"""

import asyncio
import io
import json
import os
import sys
import tempfile
import time
from datetime import datetime, timedelta, timezone

from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer
from PIL import Image, ImageDraw

MAINTENANT = datetime.now(timezone.utc)
APPELS_GEO: list[str] = []
PLEX: list[dict] = []
base_faux = ""
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
            "daily": {"time": ["2026-10-23", "2026-10-24", "2026-10-25", "2026-10-26", "2026-10-27", "2026-10-28"],
                      "weather_code": [2, 61, 3, 0, 45, 95],
                      "temperature_2m_max": [19.2, 15.1, 16.0, 20.4, 14.2, 13.0],
                      "temperature_2m_min": [12.1, 10.0, 9.4, 11.0, 8.3, 7.1],
                      "sunrise": ["2026-10-23T08:13", "", "", "", "", ""],
                      "sunset": ["2026-10-23T18:39", "", "", "", "", ""]}})

    async def cg(r):
        assert r.query["vs_currency"] in ("eur", "usd") and r.query["sparkline"] == "true"
        usd = r.query["vs_currency"] == "usd"
        bases = {"bitcoin": 54000, "ethereum": 2412, "ripple": 2.184, "dogecoin": 0.08214}
        sortie = []
        for cid in r.query["ids"].split(","):
            if cid not in bases:
                continue
            b = bases[cid] * (1.15 if usd else 1)
            sortie.append({"id": cid, "current_price": b, "image": f"{base_faux}/logo/{cid}.png",
                           "price_change_percentage_24h_in_currency": -0.8 if cid == "ethereum" else 1.9,
                           "price_change_percentage_7d_in_currency": -3.1 if cid == "ethereum" else 7.2,
                           "sparkline_in_7d": {"price": [b * (1 + ((i % 7) - 3) / 100) for i in range(168)]}})
        return web.json_response(sortie)

    async def logo(r):
        im = Image.new("RGBA", (250, 250), (0, 0, 0, 0))
        ImageDraw.Draw(im).ellipse((0, 0, 249, 249), fill=(247, 147, 26, 255))
        b = io.BytesIO(); im.save(b, "PNG")
        return web.Response(body=b.getvalue(), content_type="image/png")

    async def affiche(r):
        # Envoi en plusieurs morceaux espacés, comme un vrai serveur Plex :
        # la passerelle doit lire l'image jusqu'au bout.
        assert r.headers.get("X-Plex-Token") == "jeton-plex"
        im = Image.new("RGB", (600, 900), (90, 30, 140))
        grain = Image.effect_noise((600, 300), 60).convert("RGB")   # rend le fichier volumineux
        im.paste(grain, (0, 0))                                      # (haut de l'affiche seulement)
        b = io.BytesIO(); im.save(b, "JPEG", quality=95)
        donnees = b.getvalue()
        assert len(donnees) > 3 * 4096
        reponse = web.StreamResponse()
        reponse.content_type = "image/jpeg"
        await reponse.prepare(r)
        for i in range(0, len(donnees), 4096):
            await reponse.write(donnees[i:i + 4096])
            await asyncio.sleep(0.005)
        await reponse.write_eof()
        return reponse

    async def plex_sessions(r):
        assert r.headers.get("X-Plex-Token") == "jeton-plex"
        return web.json_response({"MediaContainer": {"size": len(PLEX), "Metadata": PLEX}})

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
    app.router.add_get("/cg/coins/markets", cg)
    app.router.add_get("/logo/{nom}", logo)
    app.router.add_get("/plex/library/metadata/{n}/thumb/{t}", affiche)
    app.router.add_get("/plex/status/sessions", plex_sessions)
    app.router.add_get("/kuma/api/status-page/modelrogs", page)
    app.router.add_get("/kuma/api/status-page/heartbeat/modelrogs", battements)
    return app


async def principal() -> int:
    faux = TestServer(faux_internet())
    await faux.start_server()
    base = str(faux.make_url(""))
    global base_faux
    base_faux = base
    os.environ.update({
        "OPEN_METEO_GEO_URL": base + "/geo", "OPEN_METEO_URL": base + "/prev", "COINGECKO_URL": base + "/cg",
        "PLEX_URL": base + "/plex", "PLEX_TOKEN": "jeton-plex",
        "KUMA_URL": base + "/kuma", "KUMA_STATUS_SLUG": "modelrogs",
        "CACHE_DIR": tempfile.mkdtemp(), "TZ": "Europe/Paris",
        # Discord demandé mais non configuré : doit être signalé, pas planter
        "MODULES": "heure,meteo,crypto,supervision,discord,plex,inconnu",
    })
    os.environ.pop("DISCORD_TOKEN", None)
    from app.main import creer_app   # importé après la configuration

    echecs = 0
    async with TestClient(TestServer(creer_app())) as c:
        tous = ",".join(str(i) for i in range(1, 16))
        cas = [
            ("/health", lambda d: d["modules"]["discord"].startswith("non configuré")),
            ("/modules", lambda d: [m["id"] for m in d["m"]] == ["heure", "meteo", "btc", "supervision", "discord", "plex"]
                and next(m for m in d["m"] if m["id"] == "discord")["ok"] == 0),
            ("/time?ville=Lyon", lambda d: d["ok"] and d["lv"] == "08:13" and d["o"] in (3600, 7200)),
            ("/meteo?ville=Lyon", lambda d: d["ok"] and d["i"] == 1 and len(d["j"]) == 5
                and [j["d"] for j in d["j"]] == ["Sam.", "Dim.", "Lun.", "Mar.", "Mer."]),
            ("/btc", lambda d: d["ok"] and len(d["c"]) == 1 and d["c"][0]["s"] == "BTC"
                and d["c"][0]["px"] == "54 000 €" and len(d["c"][0]["pts"]) == 60 and d["c"][0]["i"]),
            ("/btc?devise=usd&ids=ethereum,ripple", lambda d: [c["s"] for c in d["c"]] == ["ETH", "XRP"]
                and d["c"][0]["px"].endswith("$") and len(d["c"][1]["pts"]) == 48 and d["c"][0]["u7"] == 0),
            ("/btc?ids=bitcoin,ethereum,ripple,dogecoin,inconnu", lambda d: len(d["c"]) == 3
                and d["c"][2]["px"] == "2,1840 €" and len(d["c"][2]["pts"]) == 32),
            ("/plex", lambda d: d["ok"] and d["n"] == 0 and d["s"] == []),
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

        # --- Plex : une lecture (affiche), puis trois (liste, sans affiche)
        from app.cache import cache as C
        PLEX.append({"sessionKey": "1", "type": "movie", "title": "Les Gardiens du Phare", "year": 2024,
                     "thumb": "/library/metadata/10/thumb/1", "duration": 7_080_000, "viewOffset": 2_530_000,
                     "User": {"title": "Marc"}, "Player": {"state": "playing"}})
        C._valeurs.pop("plex-sessions", None)
        d = await (await c.get("/plex")).json()
        un = d["s"][0] if d["s"] else {}
        ok = (d["n"] == 1 and d["ev"] == 1 and un["t"] == "Les Gardiens du Phare" and un["st"] == "Film · 2024"
              and un["pos"] == "42:10" and un["r"] == "reste 1 h 16" and un["pg"] == 357 and un["tc"] == 0 and un["i"])
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} Plex, une lecture -> {json.dumps(d, ensure_ascii=False)[:150]}")

        r = await c.get(f"/img?t={un.get('i')}&w=78&h=117&f=000000&r=8")
        corps = await r.read()
        px = int.from_bytes(corps[(58 * 78 + 39) * 2:(58 * 78 + 40) * 2], "little")
        coin = int.from_bytes(corps[0:2], "little")
        ok = r.status == 200 and len(corps) == 78 * 117 * 2 and px == ((90 & 0xF8) << 8 | (30 & 0xFC) << 3 | 140 >> 3) and coin == 0
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} affiche Plex 78x117 -> {len(corps)} o, centre {px:#06x}, coin arrondi {coin:#06x}")

        PLEX.append({"sessionKey": "2", "type": "episode", "grandparentTitle": "Station Orbis", "title": "Le Retour",
                     "parentIndex": 2, "index": 5, "grandparentThumb": "/library/metadata/11/thumb/2",
                     "duration": 2_700_000, "viewOffset": 1_944_000, "User": {"title": "Julie"},
                     "Player": {"state": "playing"}, "TranscodeSession": {"videoDecision": "transcode"}})
        PLEX.append({"sessionKey": "3", "type": "track", "title": "Nuit Claire", "grandparentTitle": "Les Ondes",
                     "duration": 200_000, "viewOffset": 20_000, "User": {"title": "Salon"}, "Player": {"state": "paused"}})
        C._valeurs.pop("plex-sessions", None)
        d = await (await c.get("/plex")).json()
        ok = (d["n"] == 3 and d["ev"] == 3 and d["tc"] == 1 and all(s["i"] == "" for s in d["s"])
              and d["s"][1]["st"] == "S2 E5 · Le Retour" and d["s"][2]["pa"] == 1 and d["s"][2]["st"] == "Les Ondes")
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} Plex, trois lectures -> n={d['n']} ev={d['ev']} tc={d['tc']}")
        PLEX.clear()

        # --- Discord : états vocaux, durée de présence, avatar
        from types import SimpleNamespace as N
        from app.modules import discord_vocal as D

        class Avatar:
            def replace(self, size, format):
                return N(url=f"https://cdn.exemple/avatar.{format}?size={size}")
        vocal = N(self_mute=True, mute=False, self_deaf=False, deaf=False, self_stream=True, self_video=False)
        m = D._membre(N(display_name="Léa", voice=vocal, display_avatar=Avatar()), time.time() - 4380, time.time())
        m2 = D._membre(N(display_name="Nico", voice=None, display_avatar=Avatar()), 0, time.time())
        ok = (m["e"] == D.MUET | D.LIVE and m["d"] == "1 h 13" and m["i"] and m["w"] == 0
              and m2["d"] == "" and m2["e"] == 0 and D._depuis(30) == "à l'instant" and D._depuis(2880) == "48 min")
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} Discord : membre -> {m}")

        # --- Logo crypto : ajusté dans un carré, sur le fond demandé
        d = await (await c.get("/btc")).json()
        r = await c.get(f"/img?t={d['c'][0]['i']}&w=30&h=30&f=1C212C&m=a")
        corps = await r.read()
        ok = r.status == 200 and len(corps) == 1800 and int.from_bytes(corps[:2], "little") == (0x1C & 0xF8) << 8 | (0x21 & 0xFC) << 3 | 0x2C >> 3
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} logo crypto 30x30 sur fond carte -> {len(corps)} o")
        r = await c.get("/img?t=inconnu&w=30&h=30")
        ok = r.status == 404
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} image inconnue -> {r.status}")

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
