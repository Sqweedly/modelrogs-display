# Développer sur MODELROGS Display

Ce guide permet de reprendre le projet **depuis un clone vierge** : compiler et flasher le firmware, lancer la passerelle sur son PC, prévisualiser les écrans, régénérer les polices et publier sa propre version.

Le dépôt contient tout ce qu'il faut ; seuls les outils (PlatformIO, Python, g++) et, pour les polices, les fichiers Inter sont à télécharger.

---

## 1. Organisation du dépôt

```
firmware/                  Firmware de l'afficheur (ESP8266, PlatformIO)
  platformio.ini           Carte, découpage de la flash, broches de l'écran, bibliothèques
  src/                     Code : main (rotation), pages (écrans), ui (dessin),
                           app (appels à la passerelle), web (interface), config
  src/fonts/               Polices Inter déjà générées (inter_fonts.h) et leur licence
  tools/version.py         Injecte le numéro de version à la compilation
  tools/gzip_bin.py        Produit aussi firmware.bin.gz après la compilation
  tools/make_fonts.py      Régénère les polices (facultatif, voir § 6)
  tools/simulateur/        Aperçu des écrans sur PC, sans flasher (voir § 5)
gateway/                   Passerelle Python (aiohttp), un module par page
  app/modules/             heure, meteo, btc (crypto), cotations (actions, devises,
                           commodities), cours (outils communs), supervision,
                           discord_vocal, plex
  app/icones/              Drapeaux, icônes et logos inclus dans l'image (LICENCES.md)
  tests/                   Tests automatiques (sans accès Internet)
  Dockerfile
docker-compose.yml         Modèle de déploiement de la passerelle (sans jetons)
.github/workflows/         Vérification à chaque envoi, publication à chaque tag
docs/                      PASSERELLE.md (fonctionnement), DEVELOPPEMENT.md (ce guide)
```

## 2. Outils à installer

| Outil | Pour quoi | Remarque |
|---|---|---|
| **VS Code** + extension **PlatformIO IDE** | compiler et flasher le firmware | à l'ouverture du dépôt, VS Code propose l'extension (`.vscode/extensions.json`) |
| **Python 3.12** ou plus | passerelle, scripts | |
| **Docker** | construire ou lancer l'image de la passerelle | facultatif pour développer |
| **g++** et **Pillow** (`pip install pillow`) | simulateur d'écrans | Linux, macOS ou WSL sous Windows |
| **freetype-py** (`pip install freetype-py`) | régénérer les polices | seulement pour changer de police ou de caractères |

PlatformIO télécharge seul, au premier lancement, la plateforme ESP8266 (cœur Arduino 3.1.2) et les bibliothèques aux versions fixées dans `platformio.ini` (TFT_eSPI 2.5.43, ArduinoJson 7.2.1).

## 3. Firmware

### Compiler

Dans VS Code : **Fichier > Ouvrir le dossier… > `firmware`** (le dossier qui contient `platformio.ini`), puis l'icône ✓ « Build » de la barre PlatformIO. En ligne de commande :

```
cd firmware
pio run
```

Résultat : `firmware/.pio/build/smalltv/firmware.bin` et `firmware.bin.gz`. Une compilation locale porte la version « dev » ; pour une autre version : `MODELROGS_VERSION=2.8.1 pio run`.

### Adapter à son matériel

Tout est dans `platformio.ini` :
- **Flash et OTA** : `board` et `board_build.ldscript` fixent le découpage de la flash (4 Mo, dont 1 Mo de système de fichiers, valeurs d'un SmallTV standard). Si l'appareil tourne déjà avec un autre firmware mis à jour par `/update`, garder **le même découpage** que ce firmware-là, sinon l'OTA peut échouer.
- **Écran** : broches et réglages ST7789 dans `build_flags`. Les commentaires indiquent quoi changer si les couleurs sont inversées, en négatif, ou si l'image parasite.

### Flasher

- **Appareil qui a déjà un firmware avec `/update`** (par exemple une version précédente de MODELROGS Display) : ouvrir `http://<IP de l'afficheur>/update` (ou `http://192.168.4.1/update` sur son point d'accès) et envoyer `firmware.bin`.
- **Premier flash par USB** : il faut un adaptateur série USB-TTL **3,3 V** relié aux broches RX, TX, GND et 3,3 V de l'ESP8266, GPIO0 à la masse au démarrage pour passer en mode programmation (boîtier à ouvrir). Puis `pio run -t upload --upload-port <port>` (`COM3`, `/dev/ttyUSB0`…). Cela **remplace définitivement** le firmware d'origine : à faire en connaissance de cause.
- **Journal série** : `pio device monitor` (115 200 bauds).

Au premier démarrage, l'afficheur ouvre le point d'accès Wi-Fi « MODELROGS Display » (sans mot de passe, `http://192.168.4.1`) : y saisir le Wi-Fi maison et l'adresse de la passerelle.

## 4. Passerelle

### Lancer sur son PC, sans Docker

```
cd gateway
python -m venv .venv
. .venv/bin/activate              # Windows : .venv\Scripts\activate
pip install -r requirements.txt
CACHE_DIR=./cache MODULES=heure,meteo,crypto python -m app
```

Sous Windows (PowerShell), définir les variables avant : `$env:CACHE_DIR="cache"; $env:MODULES="heure,meteo,crypto"; python -m app`.

Lancer avec son compte habituel, **sans `sudo`** : lancée en root, la passerelle bascule sur le compte `PUID` (10001 par défaut), comme dans le conteneur, et ne peut alors plus lire le dossier du projet.

La passerelle écoute sur le port 8080 (`PORT` pour le changer). Vérifier : `http://localhost:8080/health`, puis `http://localhost:8080/meteo?ville=Lyon`. Pour la brancher sur un afficheur, renseigner l'IP du PC dans son interface.

Les autres modules demandent leurs variables (les mêmes que dans `docker-compose.yml`) :

| Module | Variables |
|---|---|
| `supervision` | `KUMA_URL`, `KUMA_STATUS_SLUG` |
| `discord` | `DISCORD_TOKEN`, `DISCORD_GUILD_ID` (et `DISCORD_MEMBERS_INTENT`) |
| `plex` | `PLEX_URL`, `PLEX_TOKEN` |
| `crypto`, `actions`, `devises`, `commodities` | facultatives : `CRYPTOS`, `ACTIONS`, `DEVISES`, `COMMODITIES` (valeurs par défaut), `LOGO_DEV_TOKEN`, `COINGECKO_KEY` |
| tous | `TZ`, `VILLE_DEFAUT` |

Ne jamais enregistrer de jeton dans un fichier suivi par git.

### Tests

```
cd gateway
python -m tests.test_passerelle
```

Ils simulent Open-Meteo, CoinGecko, Kuma et Plex : aucun accès Internet ni jeton n'est nécessaire. La dernière ligne doit être « Tous les tests sont passés. »

### Image Docker

```
docker build -t modelrogs-gateway:dev --build-arg VERSION=dev gateway
```

Puis, dans `docker-compose.yml`, remplacer `image:` par `modelrogs-gateway:dev` et retirer `pull_policy: always`.

### Ajouter une page

Une page = un module dans `gateway/app/modules/` (fiche `MODULE` : nom, variables requises, réglages proposés à l'afficheur, routes) **et** un écran dans `firmware/src/pages.cpp`, appelé par la rotation de `main.cpp`. Côté firmware, une page ajoutée prend un identifiant dans `PageId` (`config.h`), un nom et un identifiant de module (`PAGE_NAMES`, `PAGE_IDS`), une place dans l'ordre d'affichage (`ROTATION`) et, si elle a des réglages, un tampon dans `Config` : toute modification de `Config` augmente `CONFIG_VERSION` et ajoute une migration dans `config.cpp`, vérifiée par `test_config.cpp`. Le format des réponses et des réglages est décrit dans `docs/PASSERELLE.md`.

## 5. Simulateur d'écrans

Il compile le **vrai** code de dessin (`ui.cpp`, `pages.cpp`, `app.cpp`) avec une imitation de l'écran, et le nourrit avec des réponses d'exemple (`fixtures/*.json`).

Prérequis : g++, Python avec Pillow, et ArduinoJson. Le plus simple est de lancer une fois `pio run` dans `firmware` : la bibliothèque arrive dans `.pio/libdeps/smalltv/ArduinoJson/src`, chemin utilisé par défaut. Sinon, indiquer son dossier `src` dans la variable `ARDUINOJSON`.

```
cd firmware/tools/simulateur
sh apercu.sh
```

Résultat : un fichier par écran dans `out/` et une planche de tous les écrans, `planche.png`. Le script lance aussi le test de conservation de la configuration et un test de placement des textes. Sous Windows, passer par WSL ou Git Bash.

## 6. Polices

Les polices sont déjà générées dans `firmware/src/fonts/inter_fonts.h` : il n'y a rien à faire pour compiler. Pour en changer la taille ou ajouter des caractères :

1. Télécharger Inter 4.x : <https://github.com/rsms/inter/releases> ; les fichiers `Inter-ExtraBold.ttf` et `Inter-Black.ttf` sont dans `extras/ttf/` de l'archive.
2. `pip install freetype-py`
3. Depuis `firmware` : `python tools/make_fonts.py <dossier contenant les deux .ttf>`

Le jeu de caractères de `make_fonts.py` doit rester **identique** à `CHARSET` dans `gateway/app/text.py` : la passerelle remplace tout caractère absent des polices.

## 7. Publier sa version

- **Vérification** : chaque envoi sur `main` lance les tests, le test de configuration et la compilation du firmware (`.github/workflows/verification.yml`).
- **Publication** : pousser un tag `vX.Y.Z` lance `publication.yml`, qui compile le firmware à ce numéro, construit l'image Docker multi-architecture, la publie sur ghcr.io et crée la Release.
- **Dans un autre dépôt (fork)** : remplacer `ghcr.io/sqweedly/modelrogs-gateway` par son propre nom d'image dans `publication.yml` (variable `IMAGE`) et dans `docker-compose.yml`, et autoriser les Actions à écrire les paquets (Settings > Actions > General > Workflow permissions).
- **Numérotation** et notes de version : voir `CHANGELOG.md`.
