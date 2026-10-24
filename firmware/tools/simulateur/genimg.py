"""Simulateur : convertit une image de fixtures/img/ exactement comme la passerelle
(gateway/app/images.py, fonction convertir) et écrit les pixels RGB565 bruts.
Usage : python3 genimg.py <jeton> <w> <h> <fond RRGGBB> <rayon> <a|-> <sortie>"""
import sys
import types
from pathlib import Path

ICI = Path(__file__).resolve().parent
sys.path.insert(0, str(ICI.parents[2] / "gateway"))
# images.py importe aiohttp pour sa route HTTP : inutile ici
faux = types.ModuleType("aiohttp")
faux.web = types.SimpleNamespace(Request=object, Response=object)
faux.ClientSession = object
sys.modules.setdefault("aiohttp", faux)
from app.images import convertir  # noqa: E402

jeton, w, h, fond, rayon, mode, sortie = sys.argv[1:8]
source = next((p for p in (ICI / "fixtures" / "img").glob(jeton + ".*") if p.suffix in (".png", ".jpg")), None)
if source is None:
    sys.exit(1)
pixels = convertir(source.read_bytes(), int(w), int(h), tuple(bytes.fromhex(fond)), int(rayon), mode == "a")
Path(sortie).write_bytes(pixels)
