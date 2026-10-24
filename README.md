# MODELROGS Display

Afficheur d'informations sur **GeekMagic SmallTV** (ESP8266 + écran ST7789 240×240), alimenté par une **passerelle Docker modulaire** sur le réseau local.

| Module | Page affichée | À configurer |
|---|---|---|
| `heure` | Jour, date (JJ/MM/AA), heure, et la météo du jour (icône, température, min / max) | — |
| `meteo` | Prévisions sur 5 jours : les 2 prochains en grand, les 3 suivants dessous | — |
| `crypto` | 1 à 3 cryptos (BTC, ETH, XRP…) avec logos officiels, courbes sur 7 jours | — |
| `supervision` | Services Uptime Kuma (+ écran d'alerte en cas de panne) | Kuma dans `docker-compose.yml` |
| `discord` | Présents dans un salon vocal : photos de profil, micro, casque, partage d'écran | Bot dans `docker-compose.yml` |
| `plex` | Lectures en cours, avec l'affiche quand il n'y en a qu'une | Plex dans `docker-compose.yml` |

```
 Internet (Open-Meteo, CoinGecko, Discord)        Réseau local
              │                                     │
              ▼                                     ▼
   ┌──────────────────────────────┐        ┌──────────────────┐
   │ Passerelle Docker (:8080)    │◄───────│  Uptime Kuma     │
   │ ghcr.io/sqweedly/            │        │  Plex            │
   │                              │        └──────────────────┘
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

- **`docker-compose.yml`** (serveur) : modules actifs, **secrets et accès**, c'est-à-dire les jetons Discord et Plex et les adresses de Kuma et Plex.
- **Interface de l'afficheur** : **préférences** (ville, cryptos et devise, salon vocal, services supervisés, pages, nuit, luminosité).

Aucun secret ne transite par l'afficheur, dont le point d'accès est ouvert.

---

**Fonctionnement détaillé de la passerelle** : voir [docs/PASSERELLE.md](docs/PASSERELLE.md).

**Nouveautés de chaque version** : voir [CHANGELOG.md](CHANGELOG.md).

## Contenu du dépôt

Le dépôt ne contient que des **sources**. Les fichiers prêts à l'emploi (firmware, `docker-compose.yml`) sont produits par GitHub et publiés dans les **Releases**.

| Chemin | Rôle |
|---|---|
| `docker-compose.yml` | Modèle de déploiement de la passerelle (variables d'environnement) |
| `gateway/` | Code de la passerelle (Python, aiohttp, discord.py) et ses tests |
| `firmware/src/`, `firmware/platformio.ini` | Sources du firmware (PlatformIO) |
| `firmware/tools/` | Version, compression du binaire, polices, simulateur et test de configuration |
| `docs/PASSERELLE.md` | Fonctionnement détaillé de la passerelle |
| `CHANGELOG.md` | Journal des versions ; la section de chaque version est reprise dans les notes de sa Release |
| `.github/workflows/verification.yml` | À chaque envoi sur `main` : tests et compilation, sans publication |
| `.github/workflows/publication.yml` | À chaque tag `vX.Y.Z` : tests, firmware, image Docker et Release |

---

## Étape 1 — Récupérer une version

**Dépôt privé** : les Releases ne sont téléchargeables qu'en étant connecté à GitHub avec le compte Sqweedly. L'image Docker est publique et se télécharge sans identifiants.

Sur la page du dépôt : **Releases** (colonne de droite) → la dernière version. Elle contient :

| Fichier | Usage |
|---|---|
| `modelrogs-display.bin.gz` | Firmware à flasher par OTA (recommandé) |
| `modelrogs-display.bin` | Le même, non compressé |
| `docker-compose.yml` | Modèle de déploiement de la passerelle |
| `SHA256SUMS.txt` | Empreintes pour vérifier les fichiers |

L'image de la passerelle est publiée en même temps : `ghcr.io/sqweedly/modelrogs-gateway`, avec les étiquettes `X.Y.Z`, `X.Y`, `X` et `latest`. Le `docker-compose.yml` utilise l'étiquette majeure (par exemple `:2`) : `docker compose pull` suit automatiquement les versions 2.x.

**Publier une nouvelle version** :
1. ajoute une section `## [2.7.1] — date` en tête de `CHANGELOG.md` (ajouts, modifications, corrections, consignes de mise à jour) ;
2. envoie les modifications sur `main`, puis crée le tag :
   ```bash
   git tag v2.7.1
   git push origin v2.7.1
   ```
Ou dans GitHub : **Releases → Draft a new release → Choose a tag** → saisir `v2.7.1` → **Create new tag** → **Publish release**. Dans ce cas, GitHub complète la Release existante au lieu d'en créer une.

Les notes de la Release reprennent automatiquement la section de la version dans `CHANGELOG.md` (un avertissement s'affiche dans **Actions** si elle manque).

GitHub Actions enchaîne alors, en 10 minutes environ (onglet **Actions**) :
1. les tests ;
2. la compilation du firmware ;
3. l'image Docker multi-architecture ;
4. la Release avec ses fichiers.

Si une étape échoue, rien n'est publié. Le numéro de version vient uniquement du tag : il est injecté dans le firmware (écran de démarrage, interface) et dans la passerelle (`/health`).

**Quota GitHub Actions** : un dépôt privé dispose de 2 000 minutes par mois avec un compte gratuit. Une publication en consomme environ 15, une vérification environ 5.

---

## Étape 2 — Préparer les services externes (selon les modules choisis)

### Module `supervision` : page de statut Uptime Kuma

1. Dans Kuma : **Status Pages → New Status Page**, avec le slug `modelrogs`.
2. Ajoute les sondes à superviser. Par défaut, l'afficheur montre **tout ce qu'affiche cette page** ; tu peux ensuite restreindre la liste dans son interface. Pour le NAS, une sonde **Ping** suffit.
3. Vérifie que `http://<kuma>:3001/status/modelrogs` s'ouvre.

### Module `discord` : le bot

1. https://discord.com/developers/applications → **New Application** → **Bot** → **Reset Token** : copie le jeton.
2. Sur la page **Bot**, l'option **Server Members Intent** n'est **pas** nécessaire (laisse `DISCORD_MEMBERS_INTENT: "0"`).
3. **OAuth2 → URL Generator** : coche le scope `bot` et la permission **View Channels**. Ouvre l'URL générée et invite le bot sur ton serveur.
4. Dans Discord, active **Paramètres → Avancés → Mode développeur**, puis fais un clic droit sur ton serveur → **Copier l'identifiant du serveur**.

Le salon vocal se choisit ensuite **dans l'interface de l'afficheur**, dans une liste. L'icône du serveur et les photos de profil s'affichent automatiquement.

### Module `plex` : adresse et jeton

1. `PLEX_URL` : l'adresse du serveur Plex vue depuis le serveur Docker, par exemple `http://192.168.1.10:32400`.
2. `PLEX_TOKEN` : le jeton d'un compte **administrateur** du serveur. Plex explique comment le trouver dans son aide, article « Finding an authentication token / X-Plex-Token » : en bref, dans l'application web Plex, **⋯ → Obtenir des infos → Afficher le XML** sur un média, puis copier la valeur `X-Plex-Token` à la fin de l'adresse.
3. Vérifie depuis le serveur Docker : `curl -H "X-Plex-Token: <jeton>" http://192.168.1.10:32400/status/sessions` doit répondre.

---

## Étape 3 — Déployer la passerelle sur le serveur

1. Télécharge `docker-compose.yml` depuis la dernière Release (étape 1), puis copie-le sur le serveur (WinSCP, `scp`, gestionnaire de fichiers du NAS).
2. Sur le serveur :
   ```bash
   mkdir -p ~/modelrogs && cd ~/modelrogs     # dossier où tu as copié docker-compose.yml
   nano docker-compose.yml        # modules + leurs variables (voir ci-dessous)
   chmod 600 docker-compose.yml   # il contient les jetons Discord et Plex
   docker compose up -d
   ```

**Dans `docker-compose.yml`**, la ligne `MODULES:` liste les modules actifs. Pour chaque module gardé, renseigne son bloc :

```yaml
MODULES: heure,meteo,crypto,supervision     # discord et plex retirés : leurs blocs sont ignorés
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

**Dossier du cache** : la ligne `volumes:` indique où la passerelle écrit son cache sur l'hôte (`./cache:/cache` = sous-dossier `cache` à côté du `docker-compose.yml`). Remplace la partie gauche par le chemin de ton choix, par exemple `/volume1/docker/modelrogs/cache:/cache`. Rien à créer à la main : au démarrage, le conteneur crée le dossier si besoin et lui donne le bon propriétaire (utilisateur 10001, ou `PUID`/`PGID` si tu les renseignes), puis abandonne définitivement root avant de lancer la passerelle. C'est la seule raison des lignes `cap_add: CHOWN, SETUID, SETGID`.

**Erreur « Permission denied » sur le cache** (versions avant 2.4) : reprends le `docker-compose.yml` actuel, ou lance `sudo chown -R 10001:10001 cache && docker compose restart`. La passerelle fonctionne quand même : seule la conservation du cache après un redémarrage est perdue.

L'image fait environ 75 Mo (Alpine, avec la bibliothèque d'images Pillow). Le conteneur est durci :
- passerelle exécutée sans root (root sert seulement, au démarrage, à préparer le dossier du cache) ;
- système de fichiers en lecture seule, sauf le dossier du cache ;
- aucune capacité Linux.

Le plafond mémoire (`mem_limit`) est commenté par défaut : il fait échouer le démarrage sur les systèmes sans contrôleur mémoire des cgroups (Raspberry Pi OS, certains NAS).

---

## Étape 4 — Flasher l'afficheur (OTA)

1. Connecte ton téléphone ou ton PC au Wi-Fi de l'afficheur. Avec l'ancien firmware, il s'appelle « MODELROGS Discord ».
2. Ouvre **http://192.168.4.1/update**.
3. Envoie **`modelrogs-display.bin.gz`** (environ 340 Ko), téléchargé depuis la dernière Release. L'OTA accepte directement le fichier compressé.
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
  - Renseigne les réglages demandés : cryptos (3 au maximum) et devise pour Crypto, salon vocal pour Discord, services de la supervision : par défaut **tous ceux de la page de statut Kuma**, y compris ceux ajoutés plus tard ; décoche « Tous les services… » pour en choisir 48 au maximum.
  - Un module retiré de `MODULES` apparaît grisé. Un module mal configuré affiche la variable manquante.
- **Nuit et luminosité** : plage horaire, luminosité de jour et de nuit (0 = écran éteint).
- **État** :
  - Wi-Fi, version de la passerelle (avec le délai avant le prochain essai si elle ne répond pas) ;
  - mémoire libre et plus grand bloc libre, avec leur minimum depuis le démarrage, et fragmentation ;
  - cause du dernier démarrage (mise sous tension, redémarrage planifié, urgence, mise à jour, plantage) ;
  - nombre de redémarrages préventifs depuis la mise sous tension ;
  - flash et place libre pour l'OTA ;
  - boutons « Afficher maintenant ».

**Comportement**
- Rotation des pages cochées, d'une durée chacune (10 s par défaut).
- Discord n'apparaît que si quelqu'un est dans le salon, et passe en tête à chaque arrivée. Jusqu'à 4 personnes : une carte chacune (photo, durée de présence, micro ou casque coupé, LIVE, caméra) ; au-delà, une grille de photos.
- Plex n'apparaît que pendant une lecture, et passe en tête dès qu'une lecture commence. Une lecture : affiche, titre, qui regarde, lecture directe ou transcodage, progression. Plusieurs : liste de 3 lignes.
- Crypto : la mise en page suit le nombre de cryptos choisies (1 : grande page ; 2 : deux moitiés ; 3 : trois lignes).
- Supervision : **n'apparaît qu'en cas de problème** (panne, service instable, Kuma injoignable) ; tout va bien = page masquée. Tuiles jusqu'à 12 services, au-delà un compteur et la liste des problèmes. Une maintenance seule ne fait pas apparaître la page.
- Une panne affiche l'écran d'alerte rouge immédiatement, puis l'alerte alterne avec les pages (un écran sur deux) jusqu'au retour à la normale.
- Un point ambre en haut à droite signale une donnée ancienne (source injoignable).

**Fonctionnement en continu**

- **Redémarrage préventif chaque nuit à 2 h 15** (heure locale fournie par la passerelle) :
  - la radio Wi-Fi est arrêtée, puis l'ESP redémarre, ce qui réinitialise toute sa mémoire de travail ;
  - si la plage nocturne est active, l'écran reste à la luminosité de nuit pendant le redémarrage ;
  - un seul redémarrage par nuit ;
  - si l'heure est inconnue (passerelle injoignable), redémarrage après 25 h de fonctionnement.
- **Redémarrage d'urgence** si la mémoire reste trop morcelée pendant une minute (plus grand bloc libre sous 4 Ko). C'est un filet de sécurité qui ne devrait jamais servir.
- **Passerelle injoignable** : les appels sont espacés (5 s, 15 s, 30 s, puis 60 s) pour que l'afficheur et son interface restent réactifs. Le rythme normal reprend dès qu'elle répond.
- Un redémarrage logiciel ne coupe pas l'alimentation. Pour une vraie coupure électrique, il faut une prise connectée programmée.

---

## Dépannage de l'écran

Ces réglages sont dans `firmware/platformio.ini` et `firmware/src/ui.cpp`. Après modification, envoie-la sur `main` puis publie une nouvelle version (étape 1) : GitHub recompile le firmware.

| Symptôme | Correction |
|---|---|
| Rouge et bleu inversés | `-D TFT_RGB_ORDER=0` |
| Couleurs en négatif | remplacer `-D TFT_INVERSION_ON=1` par `-D TFT_INVERSION_OFF=1` |
| Image retournée ou décalée | `tft.setRotation(0)` dans `src/ui.cpp` : essayer 2 (180°), 1 ou 3 |
| Parasites | `-D SPI_FREQUENCY=27000000` |

**Compiler sur un PC** (facultatif) : `cd firmware && pio run`. Cela produit `.pio/build/smalltv/firmware.bin` et `firmware.bin.gz`, en version « dev ».

**Firmware de test sans publier** : chaque envoi sur `main` compile le firmware. Il est téléchargeable 7 jours dans **Actions → Vérification → l'exécution → Artifacts**.

---

## Sécurité

- **Afficheur** : point d'accès ouvert, OTA et interface sans mot de passe (choix assumé, réseau domestique). Aucun secret n'y est stocké.
- **`docker-compose.yml`** : il contient les jetons Discord et Plex une fois rempli. Applique `chmod 600` et **ne le remets jamais dans le dépôt GitHub**.
- **Passerelle** : HTTP sans authentification, à n'exposer que sur le réseau local.

---

## Pour aller plus loin

**API de la passerelle** (appelée par l'afficheur, JSON de moins de 2 Ko, sauf `/img`)

| Route | Rôle |
|---|---|
| `/modules` | Manifeste : modules actifs, état de configuration, réglages à proposer |
| `/time?ville=` | Heure locale, lever et coucher du soleil. Toujours disponible : sert aussi à la plage nocturne |
| `/meteo?ville=` | Météo du jour (page Heure) + 5 jours (page Météo) |
| `/btc?ids=bitcoin,ethereum&devise=eur` | Cryptos sur 7 jours (3 au plus, Bitcoin par défaut) |
| `/supervision?ids=3,7` | État des services choisis ; `ids=*` ou sans `ids` : tous les services de la page de statut |
| `/supervision/services` | Liste des sondes Kuma |
| `/discord?salon=<id>` | Présents dans le salon |
| `/discord/salons` | Liste des salons vocaux |
| `/plex` | Lectures Plex en cours |
| `/img?t=<jeton>&w=&h=&f=&r=` | Image (affiche, logo, photo) en pixels RGB565 bruts, prête à afficher |
| `/health` | État de la passerelle et de chaque module |

**Types de réglages** compris par l'afficheur dans le manifeste :
- `text` : zone de texte ;
- `select` : liste déroulante, avec options fixes `o` ou liste dynamique `src` ;
- `multi` : cases à cocher, avec liste dynamique `src` ; si `tous` est fourni, une case « tout » (valeur `*`, cochée par défaut) remplace la sélection.

Ajouter une nouvelle page demande un module dans la passerelle et un écran dans le firmware.

**Outils**
- **Tests de la passerelle** : `cd gateway && python -m tests.test_passerelle`. Ils sont lancés par GitHub à chaque envoi et avant chaque publication.
- **Test de conservation de la configuration du firmware** : `firmware/tools/simulateur/test_config.cpp`, lancé lui aussi par GitHub.
- **Aperçu des écrans sur PC** : `firmware/tools/simulateur/apercu.sh`.
- **Polices** : Inter (licence OFL), régénérées par `firmware/tools/make_fonts.py`. Leur jeu de caractères doit rester identique à `CHARSET` dans `gateway/app/text.py`.
