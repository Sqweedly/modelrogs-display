# Script PlatformIO : injecte le numéro de version dans le firmware.
#
# La version vient de la variable d'environnement MODELROGS_VERSION, fournie
# par GitHub Actions à partir du tag (v2.3.0 -> 2.3.0). Sans elle (compilation
# sur un PC), la version vaut « dev ».
Import("env")  # noqa: F821 — fourni par PlatformIO

import os

version = os.environ.get("MODELROGS_VERSION", "").strip() or "dev"
env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(version))])  # noqa: F821
print(f"MODELROGS Display : version du firmware = {version}")
