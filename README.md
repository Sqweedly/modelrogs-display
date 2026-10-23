# MODELROGS Display

Afficheur d'informations sur **GeekMagic SmallTV** (ESP8266 + écran ST7789 240×240), alimenté par une **passerelle Docker modulaire** sur le réseau local.

| Module | Page affichée | À configurer |
|---|---|---|
| `heure` | Heure, date, lever et coucher du soleil | — |
| `meteo` | Météo du jour + 3 jours | — |
| `btc` | Cours du Bitcoin, courbe sur 7 jours | — |
| `supervision` | Services Uptime Kuma (+ écran d'alerte en cas de panne) | Kuma dans `docker-compose.yml` |
| `discord` | Présents dans un salon vocal | Bot dans `docker-compose.yml` |

```
 Internet (Open-Meteo, CoinGecko, Discord)        Réseau local
              │                                     │
              ▼                                     ▼
   ┌──────────────────────────────┐        ┌──────────────────┐
   │ Passerelle Docker (:8080)    │◄───────│  Uptime Kuma     │
   │ ghcr.io/sqweedly/            │        └──────────────────┘
   │   modelrogs-gateway          │
   │ modules choisis dans         │
   │ docker-compose.yml           │
   └──────────────┬───────────────┘
                  │ HTTP local : /modules (manifeste) + une route par module
                  ▼
   ┌──────────────────────────────┐   Wi-Fi de l'afficheur (ouvert)
   │ SmallTV ESP8266              │◄── téléphone : http://192.168.4.1
   │ interface construite d'après │    réglages personnels + OTA /update
   │ les modules de la passerelle │
   └──────────────────────────────┘
```

**Répartition des réglages**

- **`docker-compose.yml`** (serveur) : modules actifs, **secrets et accès**, c'est-à-dire le jeton Discord et l'adresse de Kuma.
- **Interface de l'afficheur** : **préférences** (ville, devise, salon vocal, services supervisés, pages, nuit, luminosité).

Aucun secret ne transite par l'afficheur, dont le point d'accès est ouvert.

---

**Fonctionnement détaillé de la passerelle** : voir [docs/PASSERELLE.md](docs/PASSERELLE.md).

## Contenu du dépôt

| Chemin | Rôle |
|---|---|
| `docker-compose.yml` | Modèle de déploiement de la passerelle (variables d'environnement) |
| `gateway/` | Code de la passerelle (Python, aiohttp, discord.py) et ses tests |
| `docs/PASSERELLE.md` | Fonctionnement détaillé de la passerelle |
| `.github/workflows/image.yml` | Construction et publication automatique de l'image Docker |
| `firmware/bin/` | Firmware prêt à flasher (`.bin.gz` recommandé) |
| `firmware/src/`, `firmware/platformio.ini` | Sources du firmware (PlatformIO) |
| `firmware/tools/` | Polices, compression du binaire, simulateur d'écrans |

---

## Étape 1 — Publier l'image sur GitHub (une seule fois)

L'image est construite **par GitHub** pour PC, NAS et Raspberry Pi (amd64, arm64, armv7), puis hébergée sur `ghcr.io`. Il n'y a rien à installer sur ton PC à part Git.

1. Sur https://github.com, connecté avec le compte **Sqweedly** : **New repository**.
   - Nom : **`modelrogs-display`** (ce nom exact relie l'image au dépôt).
   - Public ou privé, au choix.
   - **Ne coche rien** : ni README, ni .gitignore, ni licence.
2. Sur ton PC, dans le dossier `modelrogs` décompressé, qui contient déjà un historique Git prêt :
   ```bash
   git remote add origin https://github.com/Sqweedly/modelrogs-display.git
   git push -u origin main --tags
   ```
   Si Git demande un mot de passe, utilise un **jeton d'accès** et non ton mot de passe GitHub. Crée-le dans GitHub → *Settings → Developer settings → Personal access tokens → Fine-grained*, sur le dépôt `modelrogs-display`, avec les permissions **Contents** et **Workflows** en lecture et écriture. Sans « Workflows », GitHub refuse l'envoi du fichier `.github/workflows/image.yml`.
3. Le tag `v2.1.0` déclenche la construction. Suis-la dans l'onglet **Actions** du dépôt (environ 5 minutes). Elle est terminée quand la coche est verte.
4. **Rends l'image publique**, une seule fois, pour que ton serveur puisse la télécharger sans identifiants. Sur ton profil GitHub : onglet **Packages** → `modelrogs-gateway` → **Package settings** → **Change visibility** → **Public**.

L'image est alors disponible : `ghcr.io/sqweedly/modelrogs-gateway:2`.

**Publier une nouvelle version** plus tard :
```bash
git tag v2.2.2 && git push --tags
```
Les étiquettes `2.2.2`, `2.2`, `2` et `latest` sont mises à jour.

---

## Étape 2 — Préparer les services externes (selon les modules choisis)

### Module `supervision` : page de statut Uptime Kuma

1. Dans Kuma : **Status Pages → New Status Page**, avec le slug `modelrogs`.
2. Ajoute **toutes** les sondes que tu veux pouvoir cocher sur l'afficheur. Pour le NAS, une sonde **Ping** suffit.
3. Vérifie que `http://<kuma>:3001/status/modelrogs` s'ouvre.

### Module `discord` : le bot

1. https://discord.com/developers/applications → **New Application** → **Bot** → **Reset Token** : copie le jeton.
2. Sur la page **Bot**, active **Server Members Intent**.
3. **OAuth2 → URL Generator** : coche le scope `bot` et la permission **View Channels**. Ouvre l'URL générée et invite le bot sur ton serveur.
4. Dans Discord, active **Paramètres → Avancés → Mode développeur**, puis fais un clic droit sur ton serveur → **Copier l'identifiant du serveur**.

Le salon vocal se choisit ensuite **dans l'interface de l'afficheur**, dans une liste.

---

## Étape 3 — Déployer la passerelle sur le serveur

```bash
mkdir -p ~/modelrogs && cd ~/modelrogs
curl -fsSLO https://raw.githubusercontent.com/Sqweedly/modelrogs-display/main/docker-compose.yml
nano docker-compose.yml        # modules + leurs variables (voir ci-dessous)
chmod 600 docker-compose.yml   # il contient le jeton Discord
mkdir -p cache && sudo chown 10001:10001 cache
docker compose up -d
```

Si le dépôt est privé, le `curl` ne fonctionnera pas : copie simplement `docker-compose.yml` sur le serveur à la main.

**Dans `docker-compose.yml`**, la ligne `MODULES:` liste les modules actifs. Pour chaque module gardé, renseigne son bloc :

```yaml
MODULES: heure,meteo,btc,supervision        # discord retiré : son bloc est ignoré
KUMA_URL: http://192.168.1.10:3001
KUMA_STATUS_SLUG: modelrogs
```

**Vérifications**

```bash
docker compose ps                                  # état « healthy » après ~1 min
docker compose logs gateway                        # « Module xxx actif » / « NON configuré : ... »
curl http://localhost:8080/modules                 # modules annoncés à l'afficheur
```

**Ajouter ou retirer un module plus tard** : modifie `MODULES` et ses variables, puis lance `docker compose up -d`. L'afficheur le prend en compte à l'ouverture de son interface, et au plus tard après 10 minutes.

**Mettre à jour la passerelle** : `docker compose pull && docker compose up -d`.

L'image fait environ 65 Mo (Alpine). Le conteneur est durci :
- utilisateur non root ;
- système de fichiers en lecture seule, sauf `./cache` ;
- aucune capacité Linux.

Le plafond mémoire (`mem_limit`) est commenté par défaut : il fait échouer le démarrage sur les systèmes sans contrôleur mémoire des cgroups (Raspberry Pi OS, certains NAS).

---

## Étape 4 — Flasher l'afficheur (OTA)

1. Connecte ton téléphone ou ton PC au Wi-Fi de l'afficheur. Avec l'ancien firmware, il s'appelle « MODELROGS Discord ».
2. Ouvre **http://192.168.4.1/update**.
3. Envoie **`firmware/bin/modelrogs-display-2.2.1.bin.gz`** (341 Ko). L'OTA accepte directement le fichier compressé.
4. Attends le redémarrage, environ 30 secondes, **sans débrancher**.

**Reprise depuis l'ancien firmware Discord** : le Wi-Fi maison est pré-rempli, et l'adresse de la passerelle est déduite de l'ancienne URL du snapshot (même machine, port 8080). Le point d'accès s'appelle désormais **« MODELROGS Display »**. L'écran demande de valider la configuration (étape 5).

**L'OTA reste toujours accessible** : à l'adresse `/update`, et par le bouton « Mettre à jour le firmware » en haut de l'interface. Elle accepte les fichiers `.bin` et `.bin.gz`. Avec une flash de 4 Mo, la place libre pour la mise à jour suivante est d'environ 2,5 Mo.

**Mode secours** : si le firmware plante 3 fois de suite dans la minute qui suit le démarrage, l'afficheur redémarre en mode secours.
- L'écran est rouge sombre uni.
- Seuls le Wi-Fi (point d'accès compris), l'interface web et l'OTA tournent.
- Tu peux alors flasher un autre firmware, ou cliquer sur « Redémarrer normalement ».
- Une coupure de courant remet aussi le compteur à zéro.

**Si la place manque**, la page affiche « Not Enough Space » et l'ancien firmware reste intact. La section **État** du nouveau firmware indique la taille de la flash et la place libre pour l'OTA.

---

## Étape 5 — Configurer l'afficheur (http://192.168.4.1)

**Validation après l'installation d'un firmware différent**

- Après l'installation de ce firmware à la place d'un autre (par exemple l'ancien « MODELROGS Discord »), l'écran affiche **« Configuration à valider »**.
- Rien ne démarre tant que tu n'as pas vérifié les réglages et cliqué sur **Enregistrer**. Les réglages récupérables sont pré-remplis.
- Ensuite, chaque démarrage lance directement la rotation des pages.

**Conservation de la configuration entre deux versions**

- Une **nouvelle version de MODELROGS Display**, par OTA, conserve tout : Wi-Fi, passerelle, ville, pages, réglages des modules, nuit, luminosités et validation.
- La configuration est stockée dans une zone de la flash que l'OTA n'efface pas. Une signature y identifie le firmware.
- Si une version change le format de la configuration, elle reprend l'ancien format automatiquement.
- Tout est perdu dans trois cas :
  - un flash par câble avec effacement complet de la flash ;
  - l'installation entre-temps d'un autre firmware qui écrit dans cette zone ;
  - un retour à une version plus ancienne que celle qui a écrit la configuration.

**Modifier la configuration plus tard** : connecte-toi au point d'accès de l'afficheur, toujours actif, puis ouvre `192.168.4.1`, ajuste et enregistre. Les changements s'appliquent sans redémarrage. Les mises à jour du firmware passent par le même point d'accès (`/update`).

**Réglages de la page**

- **Général** : adresse de la passerelle (`http://IP_SERVEUR:8080`) et ville.
- **Pages et modules** : la liste suit les modules de la passerelle.
  - Coche les pages à afficher.
  - Renseigne les réglages demandés : devise pour le Bitcoin, salon vocal pour Discord, services à cocher pour la supervision (48 au maximum).
  - Un module retiré de `MODULES` apparaît grisé. Un module mal configuré affiche la variable manquante.
- **Nuit et luminosité** : plage horaire, luminosité de jour et de nuit (0 = écran éteint).
- **État** : Wi-Fi, version de la passerelle, mémoire, flash et place libre pour l'OTA. Boutons « Afficher maintenant ».

**Comportement**
- Rotation des pages cochées, d'une durée chacune (10 s par défaut).
- Discord n'apparaît que si quelqu'un est dans le salon, et passe en tête à chaque arrivée.
- Supervision : tuiles jusqu'à 12 services, au-delà un compteur et la liste des problèmes.
- Une panne affiche l'écran d'alerte rouge jusqu'au retour à la normale.
- Un point ambre en haut à droite signale une donnée ancienne (source injoignable).

---

## Dépannage de l'écran

Ces réglages sont dans `firmware/platformio.ini`. Ils demandent de recompiler le firmware, ou de me demander un binaire corrigé.

| Symptôme | Correction |
|---|---|
| Rouge et bleu inversés | `-D TFT_RGB_ORDER=0` |
| Couleurs en négatif | remplacer `-D TFT_INVERSION_ON=1` par `-D TFT_INVERSION_OFF=1` |
| Image retournée ou décalée | `tft.setRotation(0)` dans `src/ui.cpp` : essayer 2 (180°), 1 ou 3 |
| Parasites | `-D SPI_FREQUENCY=27000000` |

**Compiler** : `cd firmware && pio run`. Cela produit `.pio/build/smalltv/firmware.bin` et `firmware.bin.gz`. Reprends dans `platformio.ini` les lignes `board` et `board_build.ldscript` de ton ancien projet pour garder le même découpage de la flash.

---

## Sécurité

- **Afficheur** : point d'accès ouvert, OTA et interface sans mot de passe (choix assumé, réseau domestique). Aucun secret n'y est stocké.
- **`docker-compose.yml`** : il contient le jeton Discord une fois rempli. Applique `chmod 600` et **ne le remets jamais dans le dépôt GitHub**.
- **Passerelle** : HTTP sans authentification, à n'exposer que sur le réseau local.

---

## Pour aller plus loin

**API de la passerelle** (appelée par l'afficheur, JSON de moins de 2 Ko)

| Route | Rôle |
|---|---|
| `/modules` | Manifeste : modules actifs, état de configuration, réglages à proposer |
| `/time?ville=` | Heure locale, lever et coucher du soleil. Toujours disponible : sert aussi à la plage nocturne |
| `/meteo?ville=` | Météo du jour + 3 jours |
| `/btc?devise=eur` | Bitcoin sur 7 jours |
| `/supervision?ids=3,7` | État des services choisis |
| `/supervision/services` | Liste des sondes Kuma |
| `/discord?salon=<id>` | Présents dans le salon |
| `/discord/salons` | Liste des salons vocaux |
| `/health` | État de la passerelle et de chaque module |

**Types de réglages** compris par l'afficheur dans le manifeste :
- `text` : zone de texte ;
- `select` : liste déroulante, avec options fixes `o` ou liste dynamique `src` ;
- `multi` : cases à cocher, avec liste dynamique `src`.

Ajouter une nouvelle page demande un module dans la passerelle et un écran dans le firmware.

**Outils**
- **Tests de la passerelle** : `cd gateway && python -m tests.test_passerelle`. Ils sont aussi lancés par GitHub avant chaque publication.
- **Aperçu des écrans sur PC** : `firmware/tools/simulateur/apercu.sh`.
- **Polices** : Inter (licence OFL), régénérées par `firmware/tools/make_fonts.py`. Leur jeu de caractères doit rester identique à `CHARSET` dans `gateway/app/text.py`.
