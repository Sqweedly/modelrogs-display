# Script PlatformIO : produit firmware.bin.gz à côté de firmware.bin.
# L'OTA de l'ESP8266 accepte directement le fichier compressé (décompression
# par le chargeur au redémarrage) : le transfert est plus court et demande
# moins de place libre dans la flash.
Import("env")  # noqa: F821 — fourni par PlatformIO

import gzip
import shutil


def compresser(source, target, env):
    binaire = str(target[0])
    with open(binaire, "rb") as entree, gzip.open(binaire + ".gz", "wb", compresslevel=9) as sortie:
        shutil.copyfileobj(entree, sortie)
    print(f"Firmware compressé : {binaire}.gz")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", compresser)  # noqa: F821
