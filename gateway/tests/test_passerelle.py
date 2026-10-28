"""Test de bout en bout de la passerelle, sans Internet.

Un faux serveur local imite Open-Meteo, CoinGecko, Yahoo Finance, la BCE
(Frankfurter), logo.dev, Uptime Kuma et Plex.
Lancement :  python -m tests.test_passerelle   (depuis le dossier gateway/)
"""

import asyncio
import io
import json
import os
import sys
import tempfile
import time
from pathlib import Path
from datetime import datetime, timedelta, timezone

from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer
from PIL import Image, ImageDraw

MAINTENANT = datetime.now(timezone.utc)
APPELS_GEO: list[str] = []
APPELS_YAHOO: list[str] = []
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

    # CoinGecko : 50 premières capitalisations (ordre), ou identifiants demandés
    CRYPTOS = [("bitcoin", "btc", "Bitcoin", 62100), ("ethereum", "eth", "Ethereum", 2773.8),
               ("tether", "usdt", "Tether", 1.0), ("ripple", "xrp", "XRP", 2.5116),
               ("binancecoin", "bnb", "BNB", 690), ("usd-coin", "usdc", "USDC", 1.0),
               ("solana", "sol", "Solana", 180), ("staked-ether", "steth", "Lido Staked Ether", 2770),
               ("tron", "trx", "TRON", 0.32), ("dogecoin", "doge", "Dogecoin", 0.094461),
               ("cardano", "ada", "Cardano", 0.71), ("wrapped-bitcoin", "wbtc", "Wrapped Bitcoin", 62000),
               ("hyperliquid", "hype", "Hyperliquid", 41), ("chainlink", "link", "Chainlink", 18.4),
               ("stellar", "xlm", "Stellar", 0.39), ("sui", "sui", "Sui", 3.6)]
    HORS_TOP = [("pepe", "pepe", "Pepe", 0.0000102)]

    async def cg(r):
        assert r.query["vs_currency"] == "usd" and "sparkline" not in r.query
        if "ids" in r.query:
            demandes = r.query["ids"].split(",")
            liste = [c for c in CRYPTOS + HORS_TOP if c[0] in demandes]
        else:
            assert r.query["order"] == "market_cap_desc"
            liste = CRYPTOS
        sortie = []
        for cid, sym, nom, b in liste:
            sortie.append({"id": cid, "symbol": sym, "name": nom, "current_price": b,
                           "image": f"{base_faux}/logo/{cid}.png",
                           "price_change_percentage_24h_in_currency": -0.8 if cid == "ethereum" else 1.9,
                           "price_change_percentage_7d_in_currency": 0.01 if sym.startswith("usd") or sym == "usdt"
                           else (-3.1 if cid == "ethereum" else 7.2),
                           "price_change_percentage_30d_in_currency": -9.4 if cid == "ethereum" else 18.6})
        return web.json_response(sortie)

    # Yahoo Finance : clôtures quotidiennes sur un mois (22 séances), montantes
    COTES = {"AAPL": (240.0, 268.4, "USD"), "MC.PA": (600.0, 640.0, "EUR"), "EURUSD=X": (1.12, 1.16, "USD"),
             "JPYUSD=X": (0.0064, 0.006612, "USD"), "NFLX": (1100.0, 1210.0, "USD"), "GC=F": (3800.0, 4112.0, "USD"), "ZW=F": (560.0, 541.25, "USX")}
    APPELS_YAHOO.clear()

    async def yahoo(r):
        sym = r.match_info["sym"]
        APPELS_YAHOO.append(sym)
        assert r.query["range"] == "1mo" and r.query["interval"] == "1d" and "Mozilla" in r.headers["User-Agent"]
        if sym == "KRWUSD=X":
            return web.Response(status=503)
        if sym == "REFUS":
            return web.Response(status=429, text="Too Many Requests")
        if sym not in COTES:
            return web.json_response({"chart": {"result": None, "error": {"code": "Not Found"}}}, status=404)
        debut, fin, dev = COTES[sym]
        t0 = int((MAINTENANT - timedelta(days=30)).timestamp())
        temps = [t0 + i * 86400 * 30 // 21 for i in range(22)]
        closes = [debut + (fin - debut) * i / 21 for i in range(22)]
        return web.json_response({"chart": {"result": [{
            "meta": {"currency": dev, "symbol": sym, "regularMarketPrice": fin},
            "timestamp": temps, "indicators": {"quote": [{"close": closes}]}}], "error": None}})

    async def bce(r):
        assert r.query["to"] == "USD"
        if r.query["from"] != "KRW":
            return web.json_response({"message": "not found"}, status=404)
        jours = [(MAINTENANT.date() - timedelta(days=30 - i)).isoformat() for i in range(31)]
        return web.json_response({"base": "KRW", "rates": {j: {"USD": 0.00070 + i * 0.000001} for i, j in enumerate(jours)}})

    async def logodev(r):
        assert r.query["token"] == "pk_test" and r.query["fallback"] == "404" and r.query["theme"] == "dark"
        if r.match_info["sym"] != "MC.PA":
            return web.Response(status=404)
        return await logo(r)

    async def cg30(r):
        assert r.query["days"] == "30"
        t0 = int((MAINTENANT - timedelta(days=30)).timestamp() * 1000)
        return web.json_response({"prices": [[t0 + i * 3_600_000, 100 + i] for i in range(721)]})

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
    app.router.add_get("/cg/coins/{id}/market_chart", cg30)
    app.router.add_get("/yf/v8/finance/chart/{sym}", yahoo)
    app.router.add_get("/bce/{periode}", bce)
    app.router.add_get("/ld/ticker/{sym}", logodev)
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
        "YAHOO_URL": base + "/yf", "FRANKFURTER_URL": base + "/bce", "LOGO_DEV_URL": base + "/ld",
        "LOGO_DEV_TOKEN": "pk_test", "NVSTLY_URL": base + "/nv",
        "ACTIONS": "AAPL", "DEVISES": "EUR, JPY",
        "KUMA_URL": base + "/kuma", "KUMA_STATUS_SLUG": "modelrogs",
        "CACHE_DIR": tempfile.mkdtemp(), "TZ": "Europe/Paris", "ICONES_PERSO": tempfile.mkdtemp(),
        # Discord demandé mais non configuré : doit être signalé, pas planter
        "MODULES": "heure,meteo,crypto,supervision,discord,plex,actions,devises,commodities,inconnu",
    })
    os.environ.pop("DISCORD_TOKEN", None)
    from app.main import creer_app   # importé après la configuration

    echecs = 0
    async with TestClient(TestServer(creer_app())) as c:
        cas = [
            ("/health", lambda d: d["modules"]["discord"].startswith("non configuré")),
            ("/modules", lambda d: [m["id"] for m in d["m"]] == ["heure", "meteo", "btc", "supervision", "discord", "plex",
                                                                  "actions", "devises", "commodities"]
                and next(m for m in d["m"] if m["id"] == "discord")["ok"] == 0),
            # Cours : 3 listes + saisie ; 10 options ; valeurs du compose annoncées
            ("/modules", lambda d: all(
                [p["k"] for p in m["p"]] == ["s1", "s2", "s3", "m"] and m["nt"]
                for m in d["m"] if m["id"] in ("btc", "actions", "devises", "commodities"))
                and next(m for m in d["m"] if m["id"] == "btc")["src"] == "/btc/liste"
                and len(next(m for m in d["m"] if m["id"] == "actions")["o"]) == 10
                and next(m for m in d["m"] if m["id"] == "devises")["df"] == "EUR, JPY"
                and next(m for m in d["m"] if m["id"] == "commodities")["o"][0] == {"id": "GC=F", "n": "Or"}),
            ("/time?ville=Lyon", lambda d: d["ok"] and d["lv"] == "08:13" and d["o"] in (3600, 7200)),
            ("/meteo?ville=Lyon", lambda d: d["ok"] and d["i"] == 1 and len(d["j"]) == 5
                and [j["d"] for j in d["j"]] == ["Sam.", "Dim.", "Lun.", "Mar.", "Mer."]),
            # Crypto en dollars. Une seule (CRYPTOS absent : Bitcoin) : courbe 30 jours
            ("/btc", lambda d: d["ok"] and len(d["c"]) == 1 and d["c"][0]["s"] == "BTC"
                and d["c"][0]["px"] == "62 100 $" and len(d["c"][0]["p30"]) == 60 and d["c"][0]["f"] == 0
                and d["c"][0]["p30"][0] == 0 and d["c"][0]["v30"] == "+18,6 %" and d["c"][0]["i"]),
            # Deux ou trois : pas de courbe ; x = page dédiée si |7 j| >= 5 %
            ("/btc?s1=ethereum&s2=ripple", lambda d: [c["s"] for c in d["c"]] == ["ETH", "XRP"]
                and d["c"][0]["px"] == "2 774 $" and "p30" not in d["c"][1]
                and [c["x"] for c in d["c"]] == [0, 1] and d["c"][0]["u7"] == 0),
            # Ordre des champs, 3 au plus ; symbole saisi reconnu ; au moins 4 chiffres
            ("/btc?s1=&s2=ripple&s3=&m=DOGE,%20bitcoin,%20eth", lambda d: [c["id"] for c in d["c"]]
                == ["ripple", "dogecoin", "bitcoin"] and d["c"][0]["px"] == "2,512 $" and d["c"][1]["px"] == "0,09446 $"),
            # Hors des 50 premières (identifiant CoinGecko) ; saisie inconnue signalée
            ("/btc?m=pepe,zzzz", lambda d: d["c"][0]["px"] == "0,00001020 $" and d["c"][1] == {"id": "zzzz", "s": "ZZZZ", "e": 1}),
            ("/btc?m=zzzz", lambda d: d["ok"] and d["c"] == [{"id": "zzzz", "s": "ZZZZ", "e": 1}]),
            # Afficheur 2.8 : ?ids= et devise ignorée
            ("/btc?ids=ethereum,ripple&devise=eur", lambda d: [c["s"] for c in d["c"]] == ["ETH", "XRP"]),
            # Liste proposée : 10 premières, sans stablecoins ni jetons enveloppés
            ("/btc/liste", lambda d: [x["id"] for x in d["s"]] == ["bitcoin", "ethereum", "ripple", "binancecoin", "solana",
                                                                  "tron", "dogecoin", "cardano", "hyperliquid", "chainlink"]
                and d["s"][0]["n"] == "Bitcoin (BTC)"),
            ("/btc/30j?id=ripple", lambda d: d["ok"] and d["c"][0]["s"] == "XRP"
                and len(d["c"][0]["p30"]) == 60 and d["c"][0]["x"] == 1),
            ("/btc/30j?id=inconnu", lambda d: d["ok"] == 0),
            # Actions : ACTIONS du compose (une seule : courbe) ; euros convertis en dollars
            ("/actions", lambda d: d["ok"] and [c["id"] for c in d["c"]] == ["AAPL"] and d["c"][0]["px"] == "268,40 $"
                and len(d["c"][0]["p30"]) == 22 and d["c"][0]["f"] == 1 and d["c"][0]["v30"] == "+11,8 %"),
            ("/actions?s1=AAPL&s2=MC.PA&m=XYZ", lambda d: [c.get("e", 0) for c in d["c"]] == [0, 0, 1]
                and d["c"][1]["px"] == "742,40 $" and d["c"][1]["s"] == "MC" and d["c"][2]["s"] == "XYZ"
                and "p30" not in d["c"][0]),
            ("/actions/30j?id=MC.PA", lambda d: d["ok"] and len(d["c"][0]["p30"]) == 22),
            ("/actions/30j?id=XYZ", lambda d: d["ok"] == 0),
            # Devises : DEVISES du compose ; KRW sans Yahoo -> taux BCE ; drapeaux inclus
            ("/devises", lambda d: [c["px"] for c in d["c"]] == ["1,160 $", "0,006612 $"]
                and all(c["i"] and c["f"] == 2 for c in d["c"])),
            ("/devises?m=krw", lambda d: d["c"][0]["s"] == "KRW" and d["c"][0]["px"] == "0,0007300 $"
                and d["c"][0]["v24"] == "+0,1 %" and d["c"][0]["i"]),
            ("/devises?s1=EUR&m=XXX", lambda d: d["c"][1]["e"] == 1),
            # Commodities : cotation en cents -> dollars ; icônes incluses
            ("/commodities?s1=GC=F&s2=ZW%3DF&s3=CL=F", lambda d: [c.get("px") for c in d["c"]] == ["4 112 $", "5,413 $", None]
                and d["c"][2] == {"id": "CL=F", "s": "WTI", "e": 1} and d["c"][0]["i"] and d["c"][0]["f"] == 2
                and d["c"][1]["v30"] == "-3,3 %" and d["c"][1]["u30"] == 0),
            ("/plex", lambda d: d["ok"] and d["n"] == 0 and d["s"] == []),
            # Toutes les sondes de la page de statut, défauts seuls envoyés (pannes d'abord)
            ("/supervision", lambda d: d["tot"] == 15 and d["md"] == "r" and d["pa"] == 2
                and d["al"]["n"] == "NAS" and d["al"]["m"] == 4
                and {"n": "NAS", "e": 2, "m": 4, "b": 0} in d["s"]
                and all(x["e"] != 0 for x in d["s"]) and len(d["s"]) == d["pa"] + d["in"] + d["ma"]),
            # Ancienne configuration de l'afficheur : le choix de services est ignoré
            ("/supervision?ids=2,3,4", lambda d: d["tot"] == 15 and d["pa"] == 2),
            ("/supervision?ids=*", lambda d: d["tot"] == 15),
            ("/modules", lambda d: next(m for m in d["m"] if m["id"] == "supervision")["p"] == []),
            ("/discord?salon=1", lambda d: d["ok"] == 0 and "Non configuré" in d["err"]),
        ]
        for chemin, verif in cas:
            r = await c.get(chemin)
            texte = await r.text()
            d = json.loads(texte)
            # Réponses lues par l'afficheur : moins de 2 Ko (manifeste de l'interface : 4 Ko)
            ok = r.status == 200 and len(texte.encode()) < (4096 if chemin == "/modules" else 2048) and verif(d)
            echecs += not ok
            print(f"{'OK ' if ok else 'ÉCHEC'} {chemin:40s} {len(texte.encode()):5d} o  {texte[:110]}")

        r = await c.get("/supervision/services")
        ok = r.status == 404
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} /supervision/services retiré -> {r.status}")

        # --- Plex : une lecture (affiche), puis trois (liste, sans affiche)
        from app.cache import cache as C
        PLEX.append({"sessionKey": "1", "type": "movie", "title": "Les Gardiens du Phare", "year": 2024,
                     "librarySectionTitle": "Films",
                     "thumb": "/library/metadata/10/thumb/1", "duration": 7_080_000, "viewOffset": 2_530_000,
                     "User": {"title": "Marc"}, "Player": {"state": "playing"}})
        C._valeurs.pop("plex-sessions", None)
        d = await (await c.get("/plex")).json()
        un = d["s"][0] if d["s"] else {}
        ok = (d["n"] == 1 and d["ev"] == 1 and un["t"] == "Les Gardiens du Phare" and un["st"] == "Film · 2024"
              and un["pos"] == "42:10" and un["r"] == "reste 1 h 16" and un["pg"] == 357 and un["tc"] == 0 and un["i"]
              and un["b"] == "Films" and un["md"] == 0 and un["ep"] == "")
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
                     "librarySectionTitle": "Séries",
                     "parentIndex": 2, "index": 5, "grandparentThumb": "/library/metadata/11/thumb/2",
                     "duration": 2_700_000, "viewOffset": 1_944_000, "User": {"title": "Julie"},
                     "Player": {"state": "playing"}, "TranscodeSession": {"videoDecision": "transcode"}})
        PLEX.append({"sessionKey": "3", "type": "track", "title": "Nuit Claire", "grandparentTitle": "Les Ondes",
                     "duration": 200_000, "viewOffset": 20_000, "User": {"title": "Salon"}, "Player": {"state": "paused"}})
        C._valeurs.pop("plex-sessions", None)
        d = await (await c.get("/plex")).json()
        ok = (d["n"] == 3 and d["ev"] == 3 and d["tc"] == 1 and d["s"][0]["i"] and d["s"][1]["i"] and d["s"][2]["i"] == ""
              and d["s"][1]["st"] == "S2 E5 · Le Retour" and d["s"][2]["pa"] == 1 and d["s"][2]["st"] == "Les Ondes"
              and d["s"][1]["b"] == "Séries" and d["s"][1]["md"] == 2 and d["s"][1]["ep"] == "S2 · E5"
              and d["s"][2]["md"] == 0 and d["s"][2]["b"] == "")
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

        # --- Logos des cours : logo.dev, sinon icône incluse ; échec mémorisé
        d = await (await c.get("/actions?s1=MC.PA&s2=AAPL&s3=NFLX")).json()
        etats = []
        for e in d["c"]:
            r = await c.get(f"/img?t={e['i']}&w=36&h=36&f=1C212C&m=a")
            etats.append(r.status)
        r2 = await c.get(f"/img?t={d['c'][2]['i']}&w=36&h=36&f=1C212C&m=a")
        # MC.PA : logo.dev ; AAPL : absent de logo.dev -> icône incluse ;
        # NFLX : ni l'un ni l'autre (nvstly injoignable ici) -> 502 puis 404 immédiat
        ok = etats == [200, 200, 502] and r2.status == 404
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} logos d'actions -> {etats}, puis {r2.status}")

        d = await (await c.get("/devises?s1=GBP&s2=KRW&m=XYZ")).json()
        r = await c.get(f"/img?t={d['c'][1]['i']}&w=56&h=56&f=000000&r=28")
        corps = await r.read()
        centre = int.from_bytes(corps[(28 * 56 + 28) * 2:(28 * 56 + 29) * 2], "little")
        ok = r.status == 200 and len(corps) == 56 * 56 * 2 and centre != 0 and int.from_bytes(corps[:2], "little") == 0
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} pastille incluse 56x56 découpée en rond -> {r.status}")

        # --- Icônes personnelles (dossier monté sur /icones)
        from PIL import Image as PILImage
        from app import images as IM
        perso = Path(os.environ["ICONES_PERSO"])

        def poser(chemin: str, couleur) -> None:
            f = perso / chemin
            f.parent.mkdir(parents=True, exist_ok=True)
            if couleur is None:
                f.write_bytes(b"pas une image")
            else:
                PILImage.new("RGB", (64, 64), couleur).save(f)
            IM._perso_lu = -IM.RELECTURE_PERSO          # relecture immédiate (30 s en vrai)

        async def centre_de(url: str) -> tuple[int, int, str]:
            d = await (await c.get(url)).json()
            e = d["c"][0]
            r = await c.get(f"/img?t={e['i']}&w=20&h=20&f=000000")
            corps = await r.read()
            return r.status, int.from_bytes(corps[(10 * 20 + 10) * 2:(10 * 20 + 11) * 2], "little"), e["i"]

        _, _, jeton_inclus = await centre_de("/devises?s1=EUR")
        poser("devises/eur.png", (255, 0, 0))                     # minuscules acceptées
        s1, c1, j1 = await centre_de("/devises?s1=EUR")
        poser("devises/eur.png", (0, 0, 255))                     # remplacée : nouveau jeton
        s2, c2, j2 = await centre_de("/devises?s1=EUR")
        (perso / "devises/eur.png").unlink(); IM._perso_lu = -IM.RELECTURE_PERSO
        _, _, j3 = await centre_de("/devises?s1=EUR")              # supprimée : retour à l'incluse
        ok = (s1, c1, s2, c2) == (200, 0xF800, 200, 0x001F) and len({jeton_inclus, j1, j2}) == 3 and j3 == jeton_inclus
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} icône perso devise : posée, remplacée, retirée -> {hex(c1)}, {hex(c2)}")

        poser("actions/MC.PA.png", (0, 255, 0))                   # passe avant logo.dev
        poser("commodities/GC=F.png", (255, 0, 0))                # symbole Yahoo
        poser("crypto/btc.png", (0, 0, 255))                      # symbole crypto
        resultats = [await centre_de(u) for u in ("/actions?s1=MC.PA", "/commodities?s1=GC=F", "/btc")]
        ok = [(s, x) for s, x, _ in resultats] == [(200, 0x07E0), (200, 0xF800), (200, 0x001F)]
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} icône perso action, commodity, crypto -> {[hex(x) for _, x, _ in resultats]}")

        poser("devises/EUR.png", None)                            # fichier invalide : source suivante
        IM.inventaire_perso()
        s, x, _ = await centre_de("/devises?s1=EUR")
        ok = s == 200 and x != 0
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} icône perso invalide -> pastille incluse ({s})")

        APPELS_YAHOO.clear()
        await c.get("/actions?m=XYZ"); await c.get("/actions?m=XYZ")
        ok = APPELS_YAHOO == []
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} symbole inconnu mémorisé -> {len(APPELS_YAHOO)} appel(s) à Yahoo")

        # --- Sans curl_cffi (armv7) : même résultat avec aiohttp
        from app.modules import cotations as CO
        sauve = CO._CurlSession
        CO._CurlSession = None
        C._valeurs["yahoo-AAPL"] = (0, C._valeurs["yahoo-AAPL"][1])      # cours expiré : nouvel appel
        APPELS_YAHOO.clear()
        d = await (await c.get("/actions?s1=AAPL")).json()
        CO._CurlSession = sauve
        ok = APPELS_YAHOO == ["AAPL"] and d["c"][0]["px"] == "268,40 $"
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} Yahoo sans curl_cffi (aiohttp) -> {d['c'][0].get('px')}")

        # --- Refus de Yahoo (429) : cause affichée, plus d'appel pendant 15 min,
        #     dernière valeur connue gardée (marquée périmée)
        APPELS_YAHOO.clear()
        d1 = await (await c.get("/actions?s1=REFUS")).json()
        C._valeurs["yahoo-AAPL"] = (0, C._valeurs["yahoo-AAPL"][1])        # cours AAPL expiré
        d2 = await (await c.get("/actions?s1=AAPL")).json()
        d3 = await (await c.get("/commodities?s1=GC=F")).json()
        ok = (d1 == {"ok": 0, "err": "Yahoo refuse l'accès (429)"} and APPELS_YAHOO == ["REFUS"]
              and d2["ok"] == 1 and d2["p"] == 1 and d2["c"][0]["px"] == "268,40 $" and d3["ok"] == 1)
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} refus Yahoo -> {d1}, appels {APPELS_YAHOO}, cours gardés p={d2.get('p')}")
        CO._refus["jusqua"] = 0
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
        etat = await (await c.get("/supervision")).json()
        ok = S.TTL_SONDES <= 60 and etat["tot"] == 16 and all(q.get("_") for q in REQUETES_KUMA)
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} sonde ajoutée dans Kuma -> tot={etat['tot']}, "
              f"Kuma interrogé sans son cache")
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
