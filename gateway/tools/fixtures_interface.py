"""Données d'essai de l'interface web de l'afficheur (firmware/tools/simulateur/fixtures_web/).

Lance la vraie passerelle sur le faux Internet des tests et enregistre /modules et
les listes déroulantes. Depuis gateway/ :
    python tools/fixtures_interface.py ../firmware/tools/simulateur/fixtures_web
"""
import asyncio, json, os, sys, tempfile
sys.path.insert(0, ".")
from aiohttp.test_utils import TestClient, TestServer
import tests.test_passerelle as T

async def main(dest):
    faux = TestServer(T.faux_internet()); await faux.start_server()
    base = str(faux.make_url("")); T.base_faux = base
    os.environ.update({
        "OPEN_METEO_GEO_URL": base + "/geo", "OPEN_METEO_URL": base + "/prev", "COINGECKO_URL": base + "/cg",
        "PLEX_URL": base + "/plex", "PLEX_TOKEN": "x", "YAHOO_URL": base + "/yf", "FRANKFURTER_URL": base + "/bce",
        "LOGO_DEV_URL": base + "/ld", "LOGO_DEV_TOKEN": "pk", "NVSTLY_URL": base + "/nv",
        "KUMA_URL": base + "/kuma", "KUMA_STATUS_SLUG": "m", "CACHE_DIR": tempfile.mkdtemp(), "TZ": "Europe/Paris",
        "ICONES_PERSO": tempfile.mkdtemp(), "IMMICH_URL": base + "/immich", "IMMICH_KEY": "k",
        "TESLA_MQTT_HOTE": "127.0.0.1", "TESLA_MQTT_PORT": "1", "VILLE": "Sernhac",
        # chez le propriétaire : pas de module « indices » (page absente), Discord non configuré
        "MODULES": "heure,meteo,crypto,supervision,discord,plex,actions,devises,commodities,custom,photos,tesla",
    })
    os.environ.pop("DISCORD_TOKEN", None)
    from app.main import creer_app
    async with TestClient(TestServer(creer_app())) as c:
        # modules : format compact (afficheur 3.2.0, ?f=2) ; modules_v1 : format d'origine
        # (passerelle plus ancienne), pour vérifier que l'interface est la même
        for nom, route in [("modules", "/modules?f=2"), ("modules_v1", "/modules"), ("btc_liste", "/btc/liste"),
                           ("custom_liste", "/custom/liste"), ("actions_liste", "/actions/liste"),
                           ("devises_liste", "/devises/liste"), ("commodities_liste", "/commodities/liste"),
                           ("immich_sources", "/immich/sources")]:
            d = await (await c.get(route)).json()
            json.dump(d, open(f"{dest}/{nom}.json", "w"), ensure_ascii=False)
            print(nom, len(json.dumps(d, ensure_ascii=False).encode()), "o")
    await faux.close()
asyncio.run(main(sys.argv[1]))
