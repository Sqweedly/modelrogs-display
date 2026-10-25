# Fonctionnement de la passerelle

La passerelle est un seul programme Python, lancé dans un conteneur Docker. Elle s'intercale entre les afficheurs et les services extérieurs : elle récupère les données, les met en forme et les sert aux afficheurs sous forme de petits JSON prêts à dessiner.

## 1. Vue d'ensemble

```
                        ┌──────────── conteneur modelrogs-gateway ────────────┐
                        │  un seul processus Python, port 8080                │
 SmallTV (ESP8266) ───► │  ┌───────────────┐   ┌──────────┐   ┌───────────┐  │ ──► Open-Meteo (HTTPS)
  HTTP, réseau local    │  │ serveur web   │──►│  modules │──►│  cache    │  │ ──► CoinGecko  (HTTPS)
  GET /meteo, /btc,     │  │ (aiohttp)     │   │          │   │ mémoire + │  │ ──► Uptime Kuma (LAN)
  /img…                 │  └───────────────┘   └────┬─────┘   │ /cache    │  │ ──► Plex (LAN)
                        │                           │         └───────────┘  │
                        │   images (Pillow) │ bot Discord ◄──────────────────│──── Discord (WebSocket)
                        └─────────────────────────────────────────────────────┘
```

**Pourquoi une passerelle :** l'ESP8266 a environ 40 Ko de mémoire libre. Il ne peut pas gérer le HTTPS des API ni lire leurs réponses volumineuses (l'historique des cryptos fait plusieurs dizaines de Ko), ni décoder des images, et encore moins tenir une connexion Discord. La passerelle fait ce travail lourd. L'afficheur ne reçoit que l'essentiel : moins de 2 Ko par page, en HTTP sur le réseau local.

**Un seul processus, plusieurs tâches en parallèle.** Python fonctionne en mode asynchrone (asyncio) : pendant qu'une requête attend la réponse de CoinGecko, le même processus répond à l'afficheur et reçoit les événements Discord. Il n'y a ni thread ni processus multiples, d'où la faible consommation : environ 50 à 60 Mo de mémoire, et un processeur quasiment au repos.

## 2. Démarrage du conteneur

1. Docker lance `python -m app`. Le dossier du cache est créé si besoin et donné à l'utilisateur 10001 (ou `PUID`:`PGID`), puis le processus abandonne root.
2. La passerelle lit la variable **`MODULES`** (par exemple `heure,meteo,crypto,supervision,discord,plex` ; `btc` est l'ancien nom de `crypto`).
   - Elle ne garde que les modules qu'elle connaît.
   - Un nom inconnu est ignoré, avec un avertissement dans les journaux.
3. Pour chaque module retenu, elle vérifie ses **variables obligatoires**, par exemple `KUMA_URL` et `KUMA_STATUS_SLUG` pour la supervision.
   - S'il en manque une : `Module xxx NON configuré : … manquant` dans les journaux, et le module répond « non configuré ».
   - Sinon : `Module xxx actif`.
4. Elle enregistre les **routes HTTP** de chaque module actif. Un module absent de `MODULES` n'a aucune route : son adresse renvoie une erreur 404.
5. Elle démarre les **tâches de fond** des modules qui en ont. Seul Discord en a une : le bot se connecte à Discord et y reste connecté.
6. Elle ouvre une **session HTTP partagée**. Toutes les requêtes vers l'extérieur passent par elle, avec un délai maximal de 8 secondes et l'identifiant `MODELROGS-Display/<version>`.
7. Elle écoute sur le port 8080 : `Passerelle x.y.z prête sur le port 8080`.

À l'arrêt du conteneur, le bot se déconnecte proprement de Discord et la session HTTP est fermée.

## 3. Le système de modules

Chaque module est décrit par une fiche (`gateway/app/modules/`) :

| Champ | Rôle | Exemple (supervision) |
|---|---|---|
| `id`, `nom` | identifiant et libellé | `supervision`, « Supervision » |
| `requis` | variables d'environnement obligatoires | `KUMA_URL`, `KUMA_STATUS_SLUG` |
| `params` | réglages à proposer dans l'interface de l'afficheur | `ids` : « tous » par défaut, ou cases à cocher (liste lue sur `/supervision/services`, 48 au maximum) |
| `routes` | adresses HTTP servies | `/supervision`, `/supervision/services` |
| `demarrer` / `arreter` / `etat` | tâche de fond éventuelle | (Discord uniquement) |

**Le manifeste `/modules`** rend l'ensemble modulaire. L'afficheur l'interroge pour savoir :
- quels modules existent ;
- s'ils sont configurés ;
- quels champs il doit afficher dans son interface web.

```json
{"ok":1,"v":"2.6.0","m":[
  {"id":"btc","n":"Crypto","ok":1,"err":"",
   "p":[{"k":"ids","l":"Cryptos affichées (3 au maximum)","t":"multi","max":3,"d":"bitcoin",
         "o":[{"id":"bitcoin","n":"Bitcoin (BTC)"},{"id":"ethereum","n":"Ethereum (ETH)"},…]},
        {"k":"devise","l":"Devise","t":"select","d":"eur",
         "o":[{"id":"eur","n":"Euro (€)"},{"id":"usd","n":"Dollar ($)"}]}]},
  {"id":"discord","n":"Discord","ok":0,"err":"DISCORD_TOKEN manquant","p":[…]}
]}
```

L'afficheur ne connaît ni CoinGecko ni Kuma : il affiche les champs décrits par la passerelle, enregistre les choix sous forme d'une courte chaîne (`ids=bitcoin,ethereum&devise=eur`, `ids=3,7,12`) et la renvoie telle quelle à chaque appel (`/btc?ids=bitcoin,ethereum&devise=eur`).

**Types de champs** :
- `text` : zone de texte ;
- `select` : liste déroulante, avec options fixes `o` ou liste dynamique `src` ;
- `multi` : cases à cocher, avec liste dynamique `src` ; si `tous` est fourni, une case « tout » (valeur `*`, cochée par défaut) remplace la sélection.

**Où vont les réglages :**
- **les secrets et les accès** (jeton Discord, adresse de Kuma) sont dans le `docker-compose.yml`, côté serveur ;
- **les préférences** (ville, devise, salon, services) sont dans l'afficheur.

L'afficheur, dont le point d'accès est ouvert, ne voit donc jamais un secret.

## 4. Parcours d'une requête

Exemple : affichage de la page Crypto avec Bitcoin seul.

1. **L'afficheur appelle** `GET http://IP_SERVEUR:8080/btc?ids=bitcoin&devise=eur`.
2. **Le serveur web** route la requête vers le module `btc` (« Crypto »), après avoir vérifié qu'il est configuré.
3. **Le cache** est consulté avec la clé `crypto-eur` :
   - si la donnée a moins de 10 minutes, elle est utilisée telle quelle ;
   - sinon, la passerelle fait **un seul appel** CoinGecko `/coins/markets` pour **toutes** les cryptos proposées : prix, variations 24 h, 7 jours et 30 jours, adresse du logo officiel. Quel que soit le choix fait sur l'afficheur, CoinGecko n'est donc appelé qu'une fois toutes les 10 minutes par devise.
   - Une seule crypto étant choisie, la passerelle ajoute sa **courbe sur 30 jours** (`/coins/bitcoin/market_chart?days=30`, environ 720 prix horaires, gardés 30 minutes).
4. **La mise en forme** :
   - prix formaté à la française, avec au moins 4 chiffres (`57 864 €`, `142,35 €`, `2,184 €`) ;
   - variations sur 24 h, 7 jours et 30 jours, avec le signe et une virgule décimale ;
   - réduction de la courbe à **60 points** par moyenne de paquets, le dernier point restant le prix actuel ;
   - **normalisation de 0 à 100** : l'afficheur n'a plus qu'à les étirer à la hauteur de son graphique ;
   - logo : l'adresse est enregistrée et remplacée par un **jeton court** (`"i"`), que l'afficheur utilisera sur `/img` (section 6) ;
   - `x` : 1 si la variation sur 7 jours atteint ±5 % (page dédiée quand plusieurs cryptos sont affichées).
5. **La réponse**, environ 300 octets :
   ```json
   {"ok":1,"c":[{"id":"bitcoin","s":"BTC","n":"Bitcoin","px":"57 864 €",
    "v24":"-0,1 %","u24":0,"v7":"+7,2 %","u7":1,"v30":"+18,6 %","u30":1,"x":1,
    "i":"3f9c0a1b2c4d","p30":[0,10,14,…,89]}],"p":0}
   ```
6. **L'afficheur dessine.** Il n'a fait aucun calcul ; seul le logo est demandé à part, déjà converti.

**Les erreurs se traduisent toujours par du JSON.** Que ce soit un module non configuré, une source injoignable ou un bug, la passerelle répond `{"ok":0,"err":"…"}` avec un code HTTP 200. L'afficheur affiche « Données indisponibles » et le message, au lieu de rester bloqué.

## 5. Le cache

| Donnée | Durée de validité | Conservée sur disque (dossier du cache) |
|---|---|---|
| Géocodage d'une ville (coordonnées, fuseau) | 1 an | oui |
| Météo | 15 min | non |
| Cours et variations des cryptos (toutes à la fois) | 10 min | oui |
| Courbe sur 30 jours d'une crypto | 30 min | oui |
| Liste des sondes Kuma | 1 min | non |
| États des sondes Kuma | 30 s | non |
| Lectures Plex | 5 s | non |
| Images téléchargées (affiches, logos, photos) | 40 dernières | non (mémoire) |
| Images converties pour l'écran | 120 dernières | non (mémoire) |

- **Pas d'appels en double.** Un verrou par donnée garantit que si plusieurs requêtes arrivent pendant une recharge, une seule va chercher la source ; les autres attendent son résultat.
- **Repli sur la dernière valeur connue.** Si la source tombe, la passerelle renvoie la dernière donnée avec **`"p":1`** (périmée), et l'afficheur montre un petit point ambre. Sans aucune donnée antérieure, elle répond `ok:0`.
- **Persistance sur disque.** Grâce au dossier de l'hôte monté sur `/cache`, un redémarrage du conteneur n'oblige pas à tout redemander.
- **Taille bornée.** Le cache contient au plus 200 entrées (variable `CACHE_MAX_ENTREES`). Au-delà, les moins récemment utilisées sont supprimées, en mémoire comme sur disque. Au démarrage, les fichiers de plus d'un an ou en surnombre sont effacés.
- **Villes.** La clé est normalisée : « Lyon », « lyon » et «  LYON  » partagent la même entrée. Une ville introuvable est mémorisée 1 heure : l'afficheur reçoit « Ville introuvable » sans nouvel appel à Open-Meteo.

Ces durées protègent aussi les API gratuites : avec plusieurs afficheurs, CoinGecko n'est appelé qu'une fois toutes les 10 minutes, quel que soit le nombre d'écrans.

## 6. Les modules en détail

### Heure (`/time`), toujours active

- Elle n'est pas soumise à `MODULES` : l'afficheur en a besoin pour la **plage nocturne**, même si la page Heure est désactivée.
- La ville est géocodée par Open-Meteo pour obtenir son **fuseau horaire** (par exemple `Europe/Paris`).
- La passerelle renvoie :
  - l'heure UTC (`e`) et le décalage en secondes (`o`), changement d'heure compris ;
  - le jour et la date en français ;
  - le lever et le coucher du soleil.
- L'afficheur garde ensuite l'heure tout seul avec son horloge interne et se resynchronise toutes les 10 minutes. Il n'a pas besoin de NTP.

### Météo (`/meteo?ville=`)

- Elle fait **un seul appel Open-Meteo**, gratuit et sans clé : conditions actuelles, la journée en cours et 5 jours de prévisions. La page Heure en affiche la météo du jour, la page Météo les 5 jours.
- Le code météo international (WMO, de 0 à 99) est converti en :
  - un **libellé français** (« Éclaircies », « Averses »…) ;
  - un **code d'icône** de 0 à 8, que l'afficheur dessine. La nuit, le soleil devient une lune.
- Les températures sont arrondies au degré.

### Crypto (`/btc?ids=&devise=` et `/btc/30j?id=&devise=`)

Voir le parcours d'une requête (section 4) : depuis la 2.7.0, la réponse ne contient plus de courbe sur 7 jours, mais les variations 24 h, 7 j et 30 j de chaque crypto.
- **Réglages de l'afficheur** : jusqu'à **3 cryptos** parmi une liste (Bitcoin, Ethereum, XRP, Solana, BNB, Cardano, Dogecoin, TRON, Litecoin, Polkadot), et la devise (`eur` ou `usd`). Sans choix, Bitcoin.
- **Une crypto choisie** : la réponse contient sa **courbe sur 30 jours** (`p30`, 60 points normalisés), tirée de CoinGecko `/coins/<id>/market_chart?days=30` et gardée 30 minutes.
- **Deux ou trois** : pas de courbe (écran « prix en vedette »). Chaque crypto porte `x: 1` si sa variation sur 7 jours atteint **+5 % ou −5 %** : l'afficheur lui consacre alors une page supplémentaire, qu'il demande à **`/btc/30j?id=<id>`** (même contenu qu'une crypto seule, courbe sur 30 jours comprise).
- **Prix** : au moins 4 chiffres, 2 décimales quand elles suffisent (`57 864 €`, `142,35 €`, `2,184 €`, `0,08214 €`).
- **Logos officiels** : ceux que CoinGecko fournit avec les cours, convertis par la passerelle (voir « Images »).
- Le module garde l'identifiant `btc` (route `/btc`) pour rester compatible ; `crypto` est accepté dans `MODULES`.
- Une clé CoinGecko « Demo », facultative (`COINGECKO_KEY`), peut être ajoutée si l'API refuse les appels anonymes.

### Supervision (`/supervision?ids=` et `/supervision/services`)

- La passerelle lit l'**API publique de la page de statut Kuma**, la même que celle qui affiche la page dans le navigateur. Elle n'a donc besoin ni d'identifiant ni de mot de passe Kuma.
- **`/supervision/services`** renvoie toutes les sondes de la page (identifiant et nom), pour les cases à cocher de l'afficheur.
- **`/supervision?ids=3,7,12`** (ou **`ids=*`**, ou sans `ids` : toutes les sondes de la page, réglage par défaut) :
  1. lit les derniers battements de chaque sonde choisie ;
  2. traduit l'état Kuma en état d'écran : en ligne → OK, hors ligne → panne, en attente → instable, maintenance → maintenance ;
  3. **trie les problèmes en premier** ;
  4. n'envoie que les **services en défaut** (12 au plus), chacun avec ses minutes dans l'état actuel (`m`, et `b` = 1 si le début est plus ancien que l'historique de Kuma), plus les compteurs (OK, pannes, instables, maintenances) ;
  5. calcule l'**alerte** : la panne la plus ancienne, sa durée en minutes et le nombre total de pannes.
- Pour ajouter un service, il suffit de l'ajouter à la page de statut dans Kuma : avec le réglage par défaut (« Tous les services »), il apparaît sur l'écran sans toucher à l'afficheur.
- **Délai d'apparition : une minute environ.** Kuma garde lui-même ses réponses de page de statut en cache quelques minutes ; la passerelle ajoute à ses requêtes un paramètre qui change à chaque appel (`?_=<heure>`), ignoré par Kuma, pour toujours obtenir la liste à jour. Elle relit la liste chaque minute.

### Discord (`/discord?salon=` et `/discord/salons`)

C'est le seul module qui **ne va pas chercher** l'information à chaque appel :
- Au démarrage, le bot ouvre une **connexion permanente** (WebSocket) avec Discord.
- Discord lui **envoie** chaque entrée et sortie dans les salons vocaux du serveur ; le bot tient la liste des présents **en mémoire**.
- Une requête de l'afficheur est donc servie instantanément, sans aucun appel à Discord.
- Un compteur d'arrivées (`ev`) augmente à chaque entrée. Quand l'afficheur le voit augmenter, il fait passer la page Discord en priorité.
- Un nouvel arrivant porte le badge « arrivé » pendant 2 minutes.
- Pour chaque présent : pseudo, **photo de profil** (jeton `i`), **durée de présence** (`d`, vide pour les personnes déjà là au démarrage du bot) et **état** (`e`) : micro coupé (1), casque coupé (2), partage d'écran (4), caméra (8). L'en-tête reçoit le nom et l'**icône du serveur** (`g`, `gi`). Ces informations arrivent avec les événements vocaux : aucun droit supplémentaire.
- **`/discord/salons`** liste les salons vocaux du serveur pour le menu déroulant de l'afficheur.
- Si Discord coupe, discord.py **se reconnecte automatiquement**. Entre-temps, les réponses portent `p:1`.
- **Permissions** : le bot ne lit aucun message, n'écrit rien et ne rejoint pas le vocal. Il lui faut seulement « Voir les salons ». L'option « Server Members Intent » n'est pas nécessaire (`DISCORD_MEMBERS_INTENT: "0"`).

### Plex (`/plex`)

- La passerelle lit `{PLEX_URL}/status/sessions` avec le jeton `PLEX_TOKEN` (compte administrateur), toutes les 5 secondes au plus.
- Pour chaque lecture : titre (la série pour un épisode), sous-titre (`Film · 2024`, `S2 E5 · Le Retour`, artiste pour une musique), qui regarde, progression (`pg`, de 0 à 1000), position et temps restant, **lecture directe ou transcodage** (`tc`), pause (`pa`).
- **Une seule lecture** : la réponse porte le jeton de l'**affiche** (`i`) : celle du film, de la série pour un épisode, de l'album pour une musique. Plusieurs lectures : pas d'affiche, 3 lignes au plus.
- Un compteur de lectures (`ev`) augmente à chaque nouvelle lecture : l'afficheur fait alors passer la page Plex en priorité. Sans lecture, la page est masquée.
- Seule la progression change d'un appel à l'autre : l'afficheur ne redessine alors que les barres, sans retélécharger l'affiche.

### Images (`/img`)

L'ESP8266 n'a pas la mémoire pour décoder un JPEG ou un PNG confortablement. La passerelle fait donc tout le travail, avec la bibliothèque **Pillow** :
1. un module enregistre l'adresse d'une image (affiche Plex, logo CoinGecko, photo ou icône Discord) et met un **jeton** de 12 caractères dans sa réponse ;
2. l'afficheur demande `/img?t=<jeton>&w=78&h=117&f=000000&r=8` : taille exacte, couleur du fond de l'écran sous l'image, rayon des coins (`r` = moitié de la taille pour un cercle), `m=a` pour un logo à garder entier ;
3. la passerelle télécharge l'image (une fois), la recadre, arrondit les coins avec un bord lissé, la pose sur le fond et renvoie **w × h pixels RGB565 bruts** (2 octets par pixel, ordre de l'ESP) : 18 Ko pour une affiche, 1,8 Ko pour un logo ;
4. l'afficheur les envoie à l'écran **ligne par ligne au fil de la réception** : il n'utilise qu'une ligne de mémoire (moins de 320 octets), jamais l'image entière.

Une image indisponible renvoie une erreur HTTP ; l'afficheur dessine alors un remplaçant (pastille de couleur avec l'initiale).

## 7. Mise en forme des textes

Les polices de l'afficheur ne contiennent qu'un jeu de caractères réduit : ASCII, accents français courants, `€ ° · …`. Tout texte destiné à l'écran passe par `ecran()` (`gateway/app/text.py`) :
- **translittération** des caractères absents : `Ê` → `E`, `ñ` → `n`, `æ` → `ae` ;
- **suppression des émojis** : « Mosquitto 🦟 » devient « Mosquitto » ;
- **troncature** avec « … » à une longueur adaptée à chaque emplacement.

Ce jeu de caractères doit rester identique à celui des polices du firmware (`firmware/tools/make_fonts.py`).

## 8. Le conteneur

**Image** (`ghcr.io/sqweedly/modelrogs-gateway`) :
- construite en deux étapes :
  1. installation des bibliothèques (aiohttp, discord.py, tzdata) ;
  2. copie du seul résultat dans une **Alpine Python** vierge, sans pip ni cache.
- environ 65 Mo.
- publiée par GitHub Actions pour amd64, arm64 et armv7, après passage des tests.

**Réglages du `docker-compose.yml` :**

| Réglage | Effet |
|---|---|
| `environment:` | Modules et accès (dont `PLEX_URL`, `PLEX_TOKEN`) |
| `ports: 8080:8080` | Seule porte d'entrée, pour l'afficheur |
| `volumes: ./cache:/cache` | Dossier de l'hôte (chemin libre à gauche) : seul endroit où le conteneur peut écrire |
| `cap_add: CHOWN, SETUID, SETGID` | Au démarrage seulement : crée le dossier du cache, lui donne le bon propriétaire, puis quitte root |
| `PUID` / `PGID` (facultatif) | Propriétaire des fichiers du cache sur l'hôte (10001 par défaut) |
| `read_only: true` + `tmpfs: /tmp` | Système de fichiers en lecture seule |
| `cap_drop: ALL`, `no-new-privileges` | Aucun autre privilège ; une fois root quitté, la passerelle n'en a plus aucun |
| `restart: unless-stopped` | Redémarre seul après une panne ou un redémarrage du serveur |
| `HEALTHCHECK` (dans l'image) | Interroge `/health` chaque minute : Docker affiche `healthy` ou `unhealthy` |
| `logging` | Journaux limités à 3 × 5 Mo |

**Flux réseau :**

| Sens | Destination | Protocole |
|---|---|---|
| Entrant | afficheur → passerelle, port 8080 | HTTP (réseau local) |
| Sortant | Open-Meteo, CoinGecko | HTTPS |
| Sortant | Discord | WebSocket sécurisé (HTTPS) |
| Sortant | Uptime Kuma, Plex | HTTP sur le réseau local |
| Sortant | Images : CDN Discord, CoinGecko | HTTPS |

Le port 8080 n'a **aucune authentification** : il ne doit pas être ouvert vers Internet.

## 9. Rythme des appels de l'afficheur

| Appel | Fréquence | Pourquoi |
|---|---|---|
| `/modules` | toutes les 10 min, et à l'ouverture de l'interface | Suivre les modules activés |
| `/time` | toutes les 10 min | Recaler l'horloge interne |
| `/discord` | toutes les 5 s | Réagir vite aux arrivées (réponse instantanée, depuis la mémoire) |
| `/supervision` | toutes les 30 s | Alerte rapide en cas de panne |
| `/plex` | toutes les 5 s | Montrer la page dès le début d'une lecture, suivre la progression |
| `/meteo`, `/btc` | à chaque affichage de la page | Données en cache côté passerelle |
| `/img` | à chaque dessin d'une page avec image | Images converties gardées en mémoire par la passerelle |

## 10. Observer la passerelle

```bash
docker compose logs -f gateway                    # démarrage, modules, erreurs de sources
curl http://IP_SERVEUR:8080/health                # état de chaque module, bot Discord compris
curl http://IP_SERVEUR:8080/modules               # ce que voit l'afficheur
curl "http://IP_SERVEUR:8080/supervision?ids=1,2" # exactement ce que reçoit l'écran
docker stats modelrogs-gateway                    # mémoire et processeur
```

N'importe quelle adresse appelée par l'afficheur peut s'ouvrir dans un navigateur : on voit exactement les données qu'il reçoit. C'est le meilleur réflexe de diagnostic quand une page affiche « Données indisponibles ».
