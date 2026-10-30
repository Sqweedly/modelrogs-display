"""Test de bout en bout de la passerelle, sans Internet.

Un faux serveur local imite Open-Meteo, CoinGecko, Yahoo Finance, la BCE
(Frankfurter), logo.dev, Uptime Kuma et Plex.
Lancement :  python -m tests.test_passerelle   (depuis le dossier gateway/)
"""

import asyncio
import io
import json
import re
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
SOUVENIRS_REFUSES = [False]
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
             "JPYUSD=X": (0.0064, 0.006612, "USD"), "NFLX": (1100.0, 1210.0, "USD"), "GC=F": (3800.0, 4112.0, "USD"), "ZW=F": (560.0, 541.25, "USX"),
             "^FCHI": (7900.0, 8123.45, "EUR"), "^GSPC": (6500.0, 6789.12, "USD")}
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
        # Comme Logo.dev : l'icône au milieu d'un grand carré de fond uni
        im = Image.new("RGB", (128, 128), (255, 255, 255))
        ImageDraw.Draw(im).rectangle((48, 48, 79, 79), fill=(20, 40, 160))
        b = io.BytesIO(); im.save(b, "PNG")
        return web.Response(body=b.getvalue(), content_type="image/png")

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
    # --- Faux Immich : 3 photos (dont une en hauteur), une vidéo, un album, un souvenir
    def actif(i, **k):
        return dict({"id": i, "type": "IMAGE", "localDateTime": "2023-07-03T10:00:00.000Z"}, **k)
    PHOTOS = [actif("p1"), actif("p2"), actif("haute"), {"id": "v1", "type": "VIDEO"}]

    def cle_ok(r):
        return r.headers.get("x-api-key") == "cle-immich"

    async def im_hasard(r):
        if not cle_ok(r):
            return web.Response(status=401)
        corps = await r.json()
        assert corps["type"] == "IMAGE"
        return web.json_response(PHOTOS)

    async def im_meta(r):
        if not cle_ok(r):
            return web.Response(status=401)
        corps = await r.json()
        assert corps.get("type", "IMAGE") == "IMAGE" and corps["size"] == 1000
        if "albumIds" in corps:                                # photos d'un album (Immich récent)
            if corps["albumIds"] == ["pas-un-uuid"]:
                return web.Response(status=400, text="Bad Request")
            if corps["albumIds"] != ["alb1"]:
                return web.json_response({"assets": {"items": [], "nextPage": None}})
            # deux pages : 2 photos, puis la photo en hauteur et la vidéo
            if corps["page"] == 1:
                return web.json_response({"assets": {"items": PHOTOS[:2], "nextPage": "2"}})
            return web.json_response({"assets": {"items": PHOTOS[2:], "nextPage": None}})
        assert corps["isFavorite"] is True
        if "type" in corps:          # comme certaines versions : favori + type ne renvoie rien
            return web.json_response({"assets": {"items": [], "nextPage": None}})
        return web.json_response({"assets": {"items": [actif("p2", isFavorite=True)], "nextPage": None}})

    async def im_souvenirs(r):
        # Comme Immich récent : « for » doit être une date simple AAAA-MM-JJ (sinon 400)
        if not cle_ok(r) or SOUVENIRS_REFUSES[0]:
            return web.Response(status=403)
        if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", r.query.get("for", "")):
            return web.Response(status=400, text="Bad Request")
        annee = datetime.now().year - 3
        return web.json_response([{"type": "on_this_day", "data": {"year": annee}, "assets": [actif("p1")]},
                                  {"type": "birthday", "data": {"year": annee}, "assets": [actif("p2")]}])

    async def im_albums(r):
        return web.json_response([{"id": "alb1", "albumName": "Vacances", "assetCount": 3}])

    async def im_album(r):
        # Comme Immich récent : tout paramètre inconnu est refusé (400), et l'album ne
        # liste plus ses photos ; « ancien » les liste encore
        if r.query:
            return web.Response(status=400, text="Bad Request")
        if r.match_info["id"] == "ancien":
            return web.json_response({"id": "ancien", "assets": PHOTOS[:2]})
        if r.match_info["id"] == "pas-un-uuid":
            return web.Response(status=400, text="Bad Request")
        if r.match_info["id"] != "alb1":
            return web.Response(status=404)
        return web.json_response({"id": "alb1", "assets": []})

    async def im_image(r):
        assert cle_ok(r) and r.query.get("size") == "preview"
        if r.match_info["id"] == "haute":                      # photo en hauteur, rouge
            im = Image.new("RGB", (300, 600), (220, 20, 20))
        else:                                                  # photo en largeur, verte
            im = Image.new("RGB", (800, 600), (20, 200, 40))
        b = io.BytesIO(); im.save(b, "JPEG", quality=95)
        return web.Response(body=b.getvalue(), content_type="image/jpeg")

    app.router.add_post("/immich/api/search/random", im_hasard)
    app.router.add_post("/immich/api/search/metadata", im_meta)
    app.router.add_get("/immich/api/memories", im_souvenirs)
    app.router.add_get("/immich/api/albums", im_albums)
    app.router.add_get("/immich/api/albums/{id}", im_album)
    app.router.add_get("/immich/api/assets/{id}/thumbnail", im_image)
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
        "MODULES": "heure,meteo,crypto,supervision,discord,plex,actions,indices,devises,commodities,custom,photos,inconnu",
        "IMMICH_URL": base + "/immich", "IMMICH_KEY": "cle-immich",
    })
    os.environ.pop("DISCORD_TOKEN", None)
    from app.main import creer_app   # importé après la configuration

    echecs = 0
    # --- Repères de prix (calcul seul)
    from app.modules import cours as CR
    attendus = [
        (([150 + i * 50 / 59 for i in range(60)], 200), ["150", "200"]),
        (([57000 + i * 100 for i in range(60)], 62900), ["58 000", "62 000"]),     # 3 valeurs -> extrêmes
        (([168.2 + i * 0.03 for i in range(60)], 170), ["169", "170"]),
        (([0.000004 + i * 1e-8 for i in range(60)], 0.0000046), ["0,0000040", "0,0000045"]),
        (([5, 5, 5], 5), []),                                                        # cours plat
    ]
    for (valeurs, actuel), libelles in attendus:
        obtenu = [r["t"] for r in CR.reperes(valeurs, actuel)]
        ok = obtenu == libelles
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} repères {libelles or 'aucun'} -> {obtenu}")
    r = CR.reperes([150 + i * 50 / 59 for i in range(60)], 200)
    ok = [x["y"] for x in r] == [0, 100]
    echecs += not ok
    print(f"{'OK ' if ok else 'ÉCHEC'} repères placés comme la courbe (0 = plus bas, 100 = plus haut) -> {[x['y'] for x in r]}")

    async with TestClient(TestServer(creer_app())) as c:
        cas = [
            ("/health", lambda d: d["modules"]["discord"].startswith("non configuré")),
            ("/modules", lambda d: [m["id"] for m in d["m"]] == ["heure", "meteo", "btc", "supervision", "discord", "plex",
                                                                  "actions", "indices", "devises", "commodities", "custom",
                                                                  "immich"]
                and next(m for m in d["m"] if m["id"] == "discord")["ok"] == 0),
            # Cours : 3 listes + saisie ; 10 options ; valeurs du compose annoncées
            ("/modules", lambda d: all(
                [p["k"] for p in m["p"]] == ["s1", "s2", "s3", "m"] and m["nt"]
                for m in d["m"] if m["id"] in ("btc", "actions", "indices", "devises", "commodities", "custom"))
                and next(m for m in d["m"] if m["id"] == "custom")["src"] == "/custom/liste"
                and next(m for m in d["m"] if m["id"] == "indices")["o"][0] == {"id": "FCHI", "n": "CAC 40"}
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
            # Une seule : repères de prix de la courbe (2 au plus), absents à 2 ou 3 valeurs
            ("/btc", lambda d: 1 <= len(d["c"][0]["rep"]) <= 2
                and all(set(r) == {"y", "t"} and 0 <= r["y"] <= 100 and "$" not in r["t"] for r in d["c"][0]["rep"])),
            ("/actions?s1=AAPL", lambda d: len(d["c"]) == 1 and "rep" in d["c"][0] and "p30" in d["c"][0]),
            ("/btc?s1=ethereum&s2=ripple", lambda d: all("rep" not in c for c in d["c"])),
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
            # Indices : en points (sans unité ni conversion en dollars), saisies courantes reconnues
            ("/indices?s1=FCHI&m=SP500", lambda d: [c["id"] for c in d["c"]] == ["FCHI", "GSPC"]
                and [c["px"] for c in d["c"]] == ["8 123", "6 789"] and [c["s"] for c in d["c"]] == ["CAC", "S&P"]
                and all(c["f"] == 2 and c["i"] for c in d["c"])),
            ("/indices?m=%5EFCHI", lambda d: d["c"][0]["px"] == "8 123" and "$" not in "".join(r["t"] for r in d["c"][0]["rep"])
                and len(d["c"][0]["p30"]) == 22),
            ("/indices?m=cac%2040", lambda d: d["c"][0]["id"] == "FCHI"),
            ("/indices/30j?id=FCHI", lambda d: d["ok"] and d["c"][0]["px"] == "8 123"),
            ("/indices?m=ZZZ", lambda d: d["c"][0] == {"id": "ZZZ", "s": "ZZZ", "e": 1}),
            # Photos Immich : source, durée (30 s par défaut), liste des sources
            ("/modules", lambda d: [p["k"] for p in next(m for m in d["m"] if m["id"] == "immich")["p"]] == ["s", "duree"]
                and next(m for m in d["m"] if m["id"] == "immich")["p"][1]["d"] == "30"),
            ("/immich", lambda d: d["ok"] == 1 and len(d["i"]) == 12 and "b" not in d),
            ("/immich?s=souvenirs", lambda d: d["ok"] == 1 and d["b"] == "Il y a 3 ans"),
            ("/immich?s=favoris", lambda d: d["ok"] == 1 and d["i"]),
            ("/immich?s=a%3Aalb1", lambda d: d["ok"] == 1 and d["i"]),
            ("/immich?s=a%3Ainconnu", lambda d: d == {"ok": 0, "err": "Album introuvable"}),
            ("/immich?s=a%3Apas-un-uuid", lambda d: d == {"ok": 0, "err": "Requête refusée par Immich"}),
            # Ancienne version d'Immich : photos lues dans l'album lui-même
            ("/immich?s=a%3Aancien", lambda d: d["ok"] == 1 and d["i"]),
            ("/immich/sources", lambda d: [s["id"] for s in d["s"]] == ["favoris", "souvenirs", "a:alb1"]
                and d["s"][2]["n"] == "Album · Vacances"),
            # Custom : 3 valeurs de catégories différentes, servies comme sur leur page d'origine
            ("/custom?s1=c%3Abitcoin&s2=a%3AAAPL&s3=m%3AGC%3DF", lambda d: [c["id"] for c in d["c"]]
                == ["c:bitcoin", "a:AAPL", "m:GC=F"] and [c["px"] for c in d["c"]] == ["62 100 $", "268,40 $", "4 112 $"]
                and [c["f"] for c in d["c"]] == [0, 1, 2] and all("p30" not in c for c in d["c"])),
            # Sans préfixe : catégorie devinée (indice connu, devise, crypto du top 50, action)
            ("/custom?m=CAC40,EUR,ETH", lambda d: [c["id"] for c in d["c"]] == ["i:FCHI", "d:EUR", "c:ethereum"]
                and d["c"][0]["px"] == "8 123"),
            ("/custom?m=AAPL", lambda d: d["c"][0]["id"] == "a:AAPL" and len(d["c"][0]["p30"]) == 22 and d["c"][0]["rep"]),
            ("/custom?m=c%3Abitcoin", lambda d: len(d["c"][0]["p30"]) == 60 and d["c"][0]["rep"]),
            ("/custom/30j?id=m%3AGC%3DF", lambda d: d["ok"] and d["c"][0]["id"] == "m:GC=F" and d["c"][0]["p30"]),
            ("/custom?m=c%3Azzzz,a%3AXYZ", lambda d: [c.get("e") for c in d["c"]] == [1, 1]),
            # Rien de choisi : valeurs par défaut (CUSTOM absent : Bitcoin, Tesla, or)
            ("/custom", lambda d: [c["id"] for c in d["c"]] == ["c:bitcoin", "a:TSLA", "m:GC=F"]),
            ("/custom/liste", lambda d: d["s"][0]["id"].startswith("c:") and any(o["id"] == "i:FCHI" for o in d["s"])
                and any(o["n"] == "Matière · Or" for o in d["s"])
                and sum(not o["id"].startswith("c:") for o in d["s"]) == 20
                and sum(o["id"].startswith("c:") for o in d["s"]) == 5),
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

        # Icône perso avec marge transparente : son cadrage est respecté (pas de rognage)
        f = perso / "crypto" / "ethereum.png"
        im = PILImage.new("RGBA", (128, 128), (0, 0, 0, 0))
        im.paste((255, 0, 0, 255), (40, 40, 88, 88)); im.save(f)
        IM._perso_lu = -IM.RELECTURE_PERSO
        d = await (await c.get("/btc?s1=ethereum")).json()
        r = await c.get(f"/img?t={d['c'][0]['i']}&w=32&h=32&f=000000&m=a")
        corps = await r.read()
        coin = int.from_bytes(corps[(4 * 32 + 4) * 2:(4 * 32 + 5) * 2], "little")
        centre = int.from_bytes(corps[(16 * 32 + 16) * 2:(16 * 32 + 17) * 2], "little")
        ok = r.status == 200 and coin == 0 and centre == 0xF800
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} icône perso avec marge non rognée -> coin {hex(coin)}, centre {hex(centre)}")

        poser("devises/EUR.png", None)                            # fichier invalide : source suivante
        IM.inventaire_perso()
        s, x, _ = await centre_de("/devises?s1=EUR")
        ok = s == 200 and x != 0
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} icône perso invalide -> pastille incluse ({s})")

        # --- Logo rogné : la marge de Logo.dev disparaît, l'icône remplit le cadre
        (perso / "actions" / "MC.PA.png").unlink(); IM._perso_lu = -IM.RELECTURE_PERSO   # retour à Logo.dev
        d = await (await c.get("/actions?s1=MC.PA")).json()
        r = await c.get(f"/img?t={d['c'][0]['i']}&w=36&h=36&f=000000&m=a")
        corps = await r.read()
        px_ = [int.from_bytes(corps[i:i + 2], "little") for i in range(0, len(corps), 2)]
        bleu = sum(1 for v in px_ if v == ((20 & 0xF8) << 8 | (40 & 0xFC) << 3 | 160 >> 3))
        blanc = sum(1 for v in px_ if v == 0xFFFF)
        ok = r.status == 200 and bleu > 36 * 36 * 0.8 and blanc == 0
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} logo Logo.dev rogné et agrandi -> {bleu} px de logo / {36 * 36}, {blanc} px de marge")

        # --- Photos : pas de répétition, plein écran, photo en hauteur sur fond flou, clé refusée
        from app.modules import immich as IMM
        IMM._vues.clear()
        suite = [(await (await c.get("/immich?s=a%3Aalb1")).json())["i"] for _ in range(4)]
        ok = len(set(suite[:3])) == 3 and suite[3] != suite[2]
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} Immich : les 3 photos de l'album avant toute répétition, "
              f"puis jamais deux fois de suite -> {len(set(suite[:3]))} différentes")
        jeton_haute = IMM._image({"id": "haute"})
        r = await c.get(f"/img?t={jeton_haute}&w=240&h=240&f=000000")
        corps = await r.read()
        def pixel(x, y):
            v = int.from_bytes(corps[(y * 240 + x) * 2:(y * 240 + x) * 2 + 2], "little")
            return (v >> 11) << 3, ((v >> 5) & 0x3F) << 2, (v & 0x1F) << 3
        centre, bord = pixel(120, 120), pixel(4, 120)
        ok = r.status == 200 and len(corps) == 240 * 240 * 2 and centre[0] > 200 and 40 < bord[0] < 180 and bord[1] < 40
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} Immich : photo en hauteur 240x240, entière au centre {centre}, fond flou {bord}")
        r = await c.get(f"/img?t={IMM._image({'id': 'p1'})}&w=240&h=240&f=000000")
        corps = await r.read()
        ok = r.status == 200 and pixel(2, 2)[1] > 150 and pixel(237, 237)[1] > 150
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} Immich : photo en largeur recadrée, plein écran {pixel(2, 2)}")
        os.environ["IMMICH_KEY"] = "mauvaise"
        d = await (await c.get("/immich")).json()
        os.environ["IMMICH_KEY"] = "cle-immich"
        ok = d == {"ok": 0, "err": "Clé Immich refusée"}
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} Immich : clé refusée -> {d}")
        # Souvenirs refusés (droit memory.read manquant) : photo au hasard, pas d'erreur
        from app.cache import cache as CACHE
        CACHE._valeurs.pop(f"immich-souvenirs-{IMM._aujourdhui().isoformat()}", None)
        SOUVENIRS_REFUSES[0] = True
        d = await (await c.get("/immich?s=souvenirs")).json()
        SOUVENIRS_REFUSES[0] = False
        ok = d["ok"] == 1 and d["i"] and "b" not in d
        echecs += not ok
        print(f"{'OK ' if ok else 'ÉCHEC'} Immich : souvenirs refusés -> photo au hasard {d}")

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
