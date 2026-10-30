# Fonctionnement de la passerelle

La passerelle est un seul programme Python, lancé dans un conteneur Docker. Elle s'intercale entre les afficheurs et les services extérieurs : elle récupère les données, les met en forme et les sert aux afficheurs sous forme de petits JSON prêts à dessiner.

## 1. Vue d'ensemble

```
                        ┌──────────── conteneur modelrogs-gateway ────────────┐
                        │  un seul processus Python, port 8080                │
 SmallTV (ESP8266) ───► │  ┌───────────────┐   ┌──────────┐   ┌───────────┐  │ ──► Open-Meteo (HTTPS)
  HTTP, réseau local    │  │ serveur web   │──►│  modules │──►│  cache    │  │ ──► CoinGecko, Yahoo, BCE (HTTPS)
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
| `params` | réglages à proposer dans l'interface de l'afficheur | aucun : tout ce qu'affiche la page de statut Kuma est suivi |
| `routes` | adresses HTTP servies | `/supervision` |
| `demarrer` / `arreter` / `etat` | tâche de fond éventuelle | (Discord uniquement) |

**Le manifeste `/modules`** rend l'ensemble modulaire. L'afficheur l'interroge pour savoir :
- quels modules existent ;
- s'ils sont configurés ;
- quels champs il doit afficher dans son interface web.

```json
{"ok":1,"v":"2.9.0","m":[
  {"id":"actions","n":"Actions","ok":1,"err":"",
   "p":[{"k":"s1","t":"select","g":1,"ph":"1. —"},{"k":"s2","t":"select","g":1,"ph":"2. —"},
        {"k":"s3","t":"select","g":1,"ph":"3. —"},{"k":"m","t":"text","g":1,"ph":"ex. BNP.PA, ^FCHI"}],
   "nt":"3 au plus, dans l'ordre : listes 1 à 3, puis saisie","df":"AAPL, MC.PA",
   "o":[{"id":"AAPL","n":"Apple"},{"id":"MSFT","n":"Microsoft"},…]},
  {"id":"btc","n":"Crypto","ok":1,"err":"","p":[…],"nt":"…","df":"bitcoin","src":"/btc/liste"},
  {"id":"discord","n":"Discord","ok":0,"err":"DISCORD_TOKEN manquant","p":[…]}
]}
```

L'afficheur ne connaît ni Yahoo ni Kuma : il affiche les champs décrits par la passerelle, enregistre les choix sous forme d'une courte chaîne (`s1=AAPL&s2=MC.PA&m=BNP.PA`) et la renvoie telle quelle à chaque appel (`/actions?s1=AAPL&s2=MC.PA&m=BNP.PA`).

**Types de champs** :
- `text` : zone de texte ;
- `select` : liste déroulante, avec options fixes `o` ou liste dynamique `src` ; sans l'un ni l'autre, la liste commune du module (`o` ou `src` au niveau du module) ; `ph` : texte de l'option vide (ou indication d'une zone de texte) ;
- `multi` : cases à cocher, avec liste dynamique `src` ; si `tous` est fourni, une case « tout » (valeur `*`, cochée par défaut) remplace la sélection.

`g` = 1 place les champs consécutifs dans une grille de 2 colonnes. Au niveau du module, `nt` est une consigne affichée au-dessus des champs et `df` les valeurs utilisées quand rien n'est choisi (celles du `docker-compose.yml`).

**Où vont les réglages :**
- **les secrets et les accès** (jeton Discord, adresse de Kuma) sont dans le `docker-compose.yml`, côté serveur ;
- **les préférences** (ville, salon, valeurs des pages de cours) sont dans l'afficheur ; pour les pages de cours, les valeurs du `docker-compose.yml` servent quand l'afficheur n'en envoie aucune.

L'afficheur, dont le point d'accès est ouvert, ne voit donc jamais un secret.

## 4. Parcours d'une requête

Exemple : affichage de la page Crypto avec Bitcoin seul.

1. **L'afficheur appelle** `GET http://IP_SERVEUR:8080/btc?s1=bitcoin`.
2. **Le serveur web** route la requête vers le module `btc` (« Crypto »), après avoir vérifié qu'il est configuré.
3. **Le cache** est consulté avec la clé `crypto-top` :
   - si la donnée a moins de 10 minutes, elle est utilisée telle quelle ;
   - sinon, la passerelle fait **un seul appel** CoinGecko `/coins/markets` qui ramène les **50 premières capitalisations** : prix en dollars, variations 24 h, 7 jours et 30 jours, adresse du logo officiel. Une crypto hors de ce classement (saisie manuelle) fait l'objet d'un appel à part, gardé lui aussi 10 minutes ;
   - une seule crypto étant choisie, la passerelle ajoute sa **courbe sur 30 jours** (`/coins/bitcoin/market_chart?days=30`, environ 720 prix horaires, gardés 30 minutes).
4. **La mise en forme** :
   - prix formaté à la française, en dollars, avec au moins 4 chiffres (`57 864 $`, `142,35 $`, `2,184 $`) ;
   - variations sur 24 h, 7 jours et 30 jours, avec le signe et une virgule décimale ;
   - réduction de la courbe à **60 points** par moyenne de paquets, le dernier point restant le prix actuel ;
   - **normalisation de 0 à 100** : l'afficheur n'a plus qu'à les étirer à la hauteur de son graphique ;
   - logo : l'adresse est enregistrée et remplacée par un **jeton court** (`"i"`), que l'afficheur utilisera sur `/img` (section 6) ; `f` indique sa forme (0 logo rond, 1 logo posé sur un disque, 2 image découpée en rond) ;
   - `x` : 1 si la variation sur 7 jours atteint ±5 % (page dédiée quand plusieurs valeurs sont affichées).
5. **La réponse**, environ 300 octets :
   ```json
   {"ok":1,"c":[{"id":"bitcoin","s":"BTC","px":"57 864 $",
    "v24":"-0,1 %","u24":0,"v7":"+7,2 %","u7":1,"v30":"+18,6 %","u30":1,"x":1,
    "i":"3f9c0a1b2c4d","f":0,"p30":[0,10,14,…,89]}],"p":0}
   ```
6. **L'afficheur dessine.** Il n'a fait aucun calcul ; seul le logo est demandé à part, déjà converti.

**Les erreurs se traduisent toujours par du JSON.** Que ce soit un module non configuré, une source injoignable ou un bug, la passerelle répond `{"ok":0,"err":"…"}` avec un code HTTP 200. L'afficheur affiche « Données indisponibles » et le message, au lieu de rester bloqué.

## 5. Le cache

| Donnée | Durée de validité | Conservée sur disque (dossier du cache) |
|---|---|---|
| Géocodage d'une ville (coordonnées, fuseau) | 1 an | oui |
| Météo | 15 min | non |
| Cours et variations des cryptos (50 premières à la fois) | 10 min | oui |
| Cours d'une action, d'une devise ou d'une matière première (Yahoo, BCE) | 5 min | oui |
| Symbole inconnu de Yahoo | 1 h (aucun nouvel appel) | non |
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

### Pages de cours : Crypto, Actions, Indices, Devises, Commodities, Custom

Routes : `/btc`, `/actions`, `/indices`, `/devises`, `/commodities`, `/custom`, chacune avec sa page dédiée `/<page>/30j?id=<id>`. Tous les prix sont **en dollars**, sauf les indices, en **points** (sans unité).

- **Sélection**, la même pour les quatre pages : `?s1=&s2=&s3=` (les trois listes de l'interface) puis `?m=` (saisie libre, une ou plusieurs valeurs séparées par des virgules ou des espaces). Les **3 premières** valeurs, dans l'ordre des champs, sont retenues. Si l'afficheur n'envoie rien, la passerelle prend la variable du compose : `CRYPTOS`, `ACTIONS`, `DEVISES`, `COMMODITIES`.
- **Une valeur** (et page dédiée `/30j`) : la réponse contient sa **courbe sur 30 jours** (`p30`, normalisée de 0 à 100) et ses **repères de prix** (`rep`, depuis la 2.9.1) : `[{"y": 21, "t": "55 000"}, {"y": 74, "t": "60 000"}]`.
  - `y` est la position du repère sur la même échelle que `p30` (0 = cours le plus bas de la période, 100 = le plus haut) ; `t` est le libellé prêt à afficher, **sans unité** (le prix en haut de l'écran porte déjà le « $ »). L'afficheur écrit le libellé du repère bas au-dessus de son pointillé et celui du repère haut en dessous : les deux restent entre les lignes.
  - Deux repères au plus, sur des valeurs « rondes » : la passerelle prend le plus petit pas de 1, 2 ou 5 × 10ⁿ qui donne exactement deux valeurs dans la plage (150 et 200 pour un cours entre 150 et 200 $). Si un pas en donne trois et le suivant une seule, elle garde les deux extrêmes du premier (58 000 et 62 000 pour 57 000 à 62 900 $) ; sinon, une seule valeur. Cours plat : aucun repère.
  - Les décimales suivent le pas (`2,5`, `0,0000045`).
  - Les dates sous la courbe (il y a 30 jours, il y a 15 jours, aujourd'hui) sont calculées par l'afficheur avec son horloge : la passerelle ne les envoie pas.
  - Un afficheur 2.9.0 ou plus ancien ignore `rep`.
- **Deux ou trois** : pas de courbe (écran « prix en vedette »). Chaque valeur porte `x: 1` si sa variation sur 7 jours atteint **+5 % ou −5 %** : l'afficheur lui consacre alors une page supplémentaire, qu'il demande à **`/<page>/30j?id=<id>`**.
- **Valeur inconnue** : elle reste à sa place dans la réponse, sous la forme `{"id":"XYZ","s":"XYZ","e":1}` (`e: 2` : source momentanément sans réponse). L'afficheur montre « ? », le code et « Introuvable ».
- **Prix** : au moins 4 chiffres, 2 décimales quand elles suffisent (`57 864 $`, `142,35 $`, `2,184 $`, `0,08214 $`).
- `s` : code court, écrit dans la pastille qui remplace un logo absent (`MC` pour `MC.PA`).

**Crypto** (`/btc`, module `btc` ; `crypto` est accepté dans `MODULES`)
- Source : CoinGecko (voir le parcours d'une requête, section 4). Une clé « Demo », facultative (`COINGECKO_KEY`), peut être ajoutée si l'API refuse les appels anonymes.
- **Listes de l'interface** : `/btc/liste`, les **10 premières capitalisations**, hors stablecoins (USDT, USDC…) et jetons adossés (WBTC, stETH…), relue toutes les 10 minutes.
- **Saisie** : identifiant CoinGecko (`pepe`) ou symbole (`PEPE`, reconnu parmi les 50 premières).
- **Logos** : ceux que CoinGecko fournit avec les cours (une icône personnelle passe avant, voir « Icônes personnelles »).
- Un afficheur 2.8 envoie encore `?ids=bitcoin,ethereum&devise=eur` : les cryptos sont reprises, la devise ignorée.

**Actions, Indices, Devises, Commodities** (module `cotations.py`)
- Source : **Yahoo Finance** (`/v8/finance/chart/<symbole>?range=1mo&interval=1d`, sans clé) : cours actuel et clôtures quotidiennes du mois, **gardés 5 minutes par valeur**. C'est une API non officielle :
  - Yahoo refuse (erreur 429) les programmes qu'il ne prend pas pour un navigateur, en se fiant aussi à la façon dont la connexion chiffrée est établie. La passerelle l'appelle donc avec **curl_cffi**, qui se connecte exactement comme Chrome (méthode de yfinance). Le log de démarrage indique le client utilisé : `Yahoo Finance : client curl_cffi (Chrome)`. curl_cffi n'existe pas pour les processeurs ARM 32 bits (armv7) : la passerelle y utilise aiohttp, avec le risque de refus ;
  - après un refus (401, 403, 429), Yahoo n'est plus appelé pendant **15 minutes** ; la page affiche la cause (« Yahoo refuse l'accès (429) ») ou, s'ils existent, les derniers cours connus, marqués périmés (`p`: 1) ;
  - un symbole inconnu n'est redemandé qu'au bout d'une heure.
- **Variations** : 24 h = depuis la clôture précédente ; 7 j et 30 j = depuis la clôture d'il y a 7 et 30 jours. Elles sont calculées dans la devise de cotation.
- **Conversion en dollars** : les cotations en cents (`USX` : blé, maïs, soja, café) sont divisées par 100 ; les autres devises (LVMH en euros, Londres en pence) sont converties au taux du jour (`EURUSD=X`…).
- **Actions** : symboles Yahoo (`AAPL`, `MC.PA` pour Paris). Liste : Apple, Microsoft, Nvidia, Alphabet, Amazon, Meta, Tesla, TotalEnergies, LVMH, Airbus.
- **Indices** (module `indices`, variable `INDICES`) : symboles Yahoo, avec ou sans « ^ » (`FCHI` = `^FCHI`). Liste : CAC 40 `FCHI`, S&P 500 `GSPC`, Nasdaq Composite `IXIC`, Dow Jones `DJI`, DAX `GDAXI`, FTSE 100 `FTSE`, Euro Stoxx 50 `STOXX50E`, Nikkei 225 `N225`, Hang Seng `HSI`, SMI `SSMI`. Les saisies courantes sont reconnues, en un mot : `CAC40`, `SP500`, `NASDAQ`, `DOW`, `DAX`, `NIKKEI`… Tout autre indice Yahoo se saisit par son symbole (`^IBEX`, `AEX`). Le cours est donné **en points**, sans conversion ni unité (« 8 123 ») ; repères de prix, variations et courbe sont calculés de la même façon que pour les autres pages.
- **Devises** : code ISO, coté en dollars (`EURUSD=X`). Si Yahoo ne répond pas, la passerelle prend les taux quotidiens de la **BCE** (Frankfurter, `FRANKFURTER_URL`, sans clé). Liste : EUR, GBP, JPY, CHF, CNY, CAD, AUD, HKD, SGD, INR.
- **Commodities** : contrats à terme Yahoo. Liste : or `GC=F`, argent `SI=F`, cuivre `HG=F`, pétrole `CL=F`, gaz naturel `NG=F`, blé `ZW=F`, maïs `ZC=F`, soja `ZS=F`, café `KC=F`, cacao `CC=F`.
- **Logos** (une icône personnelle passe toujours en premier, voir ci-dessous) :
  - actions : **Logo.dev** si la clé `LOGO_DEV_TOKEN` est renseignée (`/ticker/<symbole>?format=png&theme=dark&fallback=404`), puis les logos inclus dans `gateway/app/icones/actions/`, puis le dépôt GitHub `nvstly/icons` pour les actions cotées aux États-Unis ;
  - devises : **pastilles incluses** (`icones/devises/`, 31 devises) : un disque de couleur avec le symbole de la monnaie (€, £, ¥, ₹, C$, HK$…). Elles sont produites par `gateway/tools/icones_devises.py`, qui fixe symboles et couleurs ;
  - indices : **pastilles incluses** (`icones/indices/`) avec le nom court de l'indice (CAC, S&P, NDQ, DOW, DAX, FTSE, SX5E, NKY, HSI, SMI), produites par le même script ;
  - matières premières : icônes incluses (`icones/commodities/`).
  - Les licences des icônes incluses sont décrites dans `gateway/app/icones/LICENCES.md`.

**Custom** (module `custom.py`, variable `CUSTOM`)
- Jusqu'à 3 valeurs de toutes catégories sur une même page (ex. Bitcoin, l'action Tesla et l'or). Chaque valeur est servie exactement comme sur sa page d'origine : cours, variations, logo, unité (dollars, points pour un indice), courbe et repères pour une seule valeur.
- **Saisie** : `catégorie:valeur`, avec `c:` (crypto), `a:` (action), `i:` (indice), `d:` (devise), `m:` (matière première) ; les noms longs sont acceptés (`crypto:`, `action:`, `indice:`, `devise:`, `matiere:`). Exemples : `c:pepe`, `a:MC.PA`, `i:CAC40`, `d:EUR`, `m:CL=F`.
- **Sans préfixe**, la catégorie est devinée : contrat `=F` ou matière connue, puis `^…` ou indice connu, puis code de devise connu, puis crypto parmi les 50 premières capitalisations ; sinon, action.
- **Listes de l'interface** : `/custom/liste` propose les **5 premières valeurs de chaque catégorie** (25 en tout : 5 cryptos, 5 actions, 5 indices, 5 devises, 5 matières), avec la catégorie devant le nom (« Crypto · Bitcoin (BTC) », « Matière · Or ») ; toute autre valeur se saisit dans le champ libre.
- Les identifiants des réponses sont préfixés (`c:bitcoin`, `a:TSLA`) : la page dédiée est demandée à `/custom/30j?id=a:TSLA`.
- Valeurs par défaut : `CUSTOM: c:bitcoin,a:TSLA,m:GC=F`.

**Icônes personnelles** (toutes les pages de cours)

Pour remplacer une icône sans attendre une nouvelle version, ou en ajouter une qui manque, il suffit de déposer un fichier dans un dossier de l'hôte monté sur `/icones` (variable `ICONES_PERSO` pour un autre chemin) :

```yaml
volumes:
  - ./cache:/cache
  - ./icones:/icones:ro      # chemin libre à gauche du « : »
```

- **Rangement** : un sous-dossier par page ; le nom du fichier est la valeur saisie dans l'interface de l'afficheur, **sans tenir compte des majuscules** :

  | Page | Fichier | Exemples |
  |---|---|---|
  | Indices | `indices/<symbole Yahoo sans ^>.png` | `indices/FCHI.png`, `indices/IBEX.png` |
  | Devises | `devises/<code ISO>.png` | `devises/EUR.png`, `devises/krw.png` |
  | Actions | `actions/<symbole Yahoo>.png` | `actions/AAPL.png`, `actions/MC.PA.png` |
  | Commodities | `commodities/<symbole Yahoo>.png` (ou le nom de l'icône incluse : `or`, `petrole`…) | `commodities/GC=F.png` |
  | Crypto | `crypto/<identifiant CoinGecko ou symbole>.png` | `crypto/bitcoin.png`, `crypto/PEPE.png` |

- **Format** : PNG de préférence, à fond transparent ; WebP et JPG sont acceptés (si deux fichiers portent le même nom, l'ordre de préférence est PNG, WebP, JPG). Image **carrée**, de **64 px au moins** (128 px conseillés), **4 Mo au plus**. La passerelle la réduit à la taille de l'écran et la pose sur le fond de la page. Pour les devises, elle est **découpée en rond** : le sujet doit remplir le carré (comme les pastilles incluses). Pour les autres pages, elle est gardée entière et centrée.
- **Priorité** : l'icône personnelle passe avant toutes les autres sources (Logo.dev, icône incluse, CoinGecko, GitHub). Un fichier illisible est ignoré : la source suivante prend le relais.
- **Prise en compte sans redémarrage** : le dossier est relu toutes les 30 secondes. La date et la taille du fichier entrent dans le jeton de l'image : une icône ajoutée, remplacée ou supprimée apparaît en moins d'une minute, au passage suivant de la page.
- **Droits** : le dossier est monté en lecture seule (`:ro`) ; ses fichiers doivent seulement être lisibles par tous (c'est le cas par défaut), aucun changement de propriétaire n'est nécessaire.
- **Contrôle** : au démarrage, le log indique `Icônes personnelles : <nombre> dans /icones` et signale chaque fichier inutilisable (`Icône personnelle inutilisable : devises/EUR.png (…)`) ainsi que les sous-dossiers inconnus. Sans dossier monté, rien n'est écrit.
- **Licences** : ces fichiers restent sur l'hôte et ne sont pas redistribués. Des icônes dont la licence interdit la redistribution (Icons8 en version gratuite, par exemple) peuvent donc y être déposées pour un usage personnel ; leurs conditions (lien d'attribution notamment) restent à respecter.

### Supervision (`/supervision`)

- La passerelle lit l'**API publique de la page de statut Kuma**, la même que celle qui affiche la page dans le navigateur. Elle n'a donc besoin ni d'identifiant ni de mot de passe Kuma.
- **`/supervision`** suit **toutes les sondes de la page de statut** (depuis la 2.8.1, plus de choix sur l'afficheur ; un ancien `?ids=` est ignoré) :
  1. lit les derniers battements de chaque sonde ;
  2. traduit l'état Kuma en état d'écran : en ligne → OK, hors ligne → panne, en attente → instable, maintenance → maintenance ;
  3. **trie les problèmes en premier** ;
  4. n'envoie que les **services en défaut** (12 au plus), chacun avec ses minutes dans l'état actuel (`m`, et `b` = 1 si le début est plus ancien que l'historique de Kuma), plus les compteurs (OK, pannes, instables, maintenances) ;
  5. calcule l'**alerte** : la panne la plus ancienne, sa durée en minutes et le nombre total de pannes.
- Pour ajouter ou retirer un service, il suffit de l'ajouter à la page de statut dans Kuma ou de l'en retirer : l'écran suit sans toucher à l'afficheur.
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

### Photos Immich (`/immich`, module `immich`)

- **Accès** : `IMMICH_URL` (ex. `http://192.168.1.10:2283`, l'adresse du serveur Immich) et `IMMICH_KEY`, clé d'API créée dans Immich (Paramètres du compte > Clés d'API). La passerelle interroge Immich sur le réseau local, avec l'en-tête `x-api-key` ; aucune photo ne sort de chez soi.
- **Droits nécessaires de la clé d'API** (côté Immich ; la page ne fait que lire) :

  | Droit | Pour |
  |---|---|
  | `album.read` | la liste des albums de l'interface, et leurs photos |
  | `asset.read` | la recherche des photos : au hasard, favoris, photos d'un album |
  | `asset.view` | l'image elle-même |
  | `memory.read` | les souvenirs (« Il y a N ans ») |

  Un droit manquant donne « Clé Immich refusée » sur l'afficheur, et la requête refusée apparaît dans le log de la passerelle.
- **Réglages de l'afficheur** : `s`, la source (vide = au hasard, `favoris`, `souvenirs`, `a:<id>` pour un album, liste fournie par `/immich/sources`), et `duree`, la durée d'affichage de la page en secondes (10 s à 5 min, 30 s par défaut). L'afficheur utilise cette durée à la place de la durée commune des pages.
- **Choix de la photo** : à chaque passage, une photo au hasard dans la source, en évitant les 20 dernières montrées ; quand toutes ont été vues, un nouveau tour commence, sans remontrer la dernière. Les listes d'un album et des favoris sont gardées 10 minutes ; vidéos et photos de la corbeille sont ignorées.
- **Photos d'un album** : demandées à la recherche d'Immich, filtrée sur l'album (`POST /api/search/metadata`, `albumIds`), page par page (5 000 photos au plus) : les versions récentes ne listent plus les photos dans la fiche de l'album, et refusent tout paramètre inconnu (erreur 400). Les versions plus anciennes sont lues dans la fiche de l'album (`GET /api/albums/<id>`).
- **Favoris** : même recherche (`isFavorite`) ; si le filtre « photos seulement » côté Immich ne renvoie rien, la passerelle relance sans lui et trie elle-même.
- **Log** : une ligne par chargement de liste (« 128 photo(s) dans l'album … », « 3 photo(s) dans les favoris ») et, en cas d'échec, la source, la requête et le code d'erreur d'Immich.
- **Souvenirs** : photos prises un jour comme aujourd'hui, les années passées (`/api/memories?for=AAAA-MM-JJ&type=on_this_day`, la date du jour dans le fuseau `TZ`). La réponse porte alors `b` (« Il y a 3 ans »), affiché en pastille en haut à gauche. Sans souvenir ce jour-là, ou si Immich refuse les souvenirs (droit `memory.read` manquant), une photo au hasard, sans pastille ; la cause est écrite dans le log.
- **Réponse** : `{"ok":1,"i":"<jeton>"}` (+ `b`), ou `{"ok":0,"err":"Clé Immich refusée" | "Album introuvable" | "Requête refusée par Immich" | "Immich indisponible" | "Aucune photo"}`.
- **Tester depuis le docker passerelle** (adresse et port de la passerelle, pas ceux d'Immich) : `curl -s "http://<passerelle>:8080/immich/sources"`, puis `curl -s "http://<passerelle>:8080/immich?s=a%3A<id d'album>"` ; réponse attendue `{"ok":1,"i":"…"}`.
- **Image** : la version réduite d'Immich (`/api/assets/<id>/thumbnail?size=preview`), préparée en **mode photo** (voir `/img`) : 240 × 240, recadrée au centre ; une photo nettement plus haute que large (plus de 1,15 fois) est posée entière au centre, sur un fond fait d'elle-même, agrandie, floutée et assombrie. L'afficheur la dessine en plein écran, sans texte ni points de pagination (environ 115 Ko, 1 à 2 secondes).

### Images (`/img`)

L'ESP8266 n'a pas la mémoire pour décoder un JPEG ou un PNG confortablement. La passerelle fait donc tout le travail, avec la bibliothèque **Pillow** :
1. un module enregistre l'adresse d'une image (affiche Plex, logo CoinGecko ou Logo.dev, photo ou icône Discord, icône incluse dans l'image Docker, icône personnelle de `/icones`) et met un **jeton** de 12 caractères dans sa réponse. Un jeton peut regrouper **plusieurs sources**, essayées dans l'ordre (ex. icône personnelle, Logo.dev, puis icône incluse) ;
2. l'afficheur demande `/img?t=<jeton>&w=78&h=117&f=000000&r=8` : taille exacte, couleur du fond de l'écran sous l'image, rayon des coins (`r` = moitié de la taille pour un cercle), `m=a` pour un logo à garder entier ;
3. la passerelle télécharge l'image (une fois), la recadre, arrondit les coins avec un bord lissé, la pose sur le fond et renvoie **w × h pixels RGB565 bruts** (2 octets par pixel, ordre de l'ESP) : 18 Ko pour une affiche, 1,8 Ko pour un logo. Un **logo** (`m=a`) est d'abord **rogné** (depuis la 2.9.1) : sa marge transparente, ou son fond uni de la couleur des coins (Logo.dev pose l'icône au milieu d'un grand carré), est retirée, puis le logo est agrandi pour occuper 94 % du cadre. Les **icônes personnelles** ne sont jamais rognées : leur cadrage est respecté ;
4. l'afficheur les envoie à l'écran **ligne par ligne au fil de la réception** : il n'utilise qu'une ligne de mémoire (moins de 320 octets), jamais l'image entière.

Une image enregistrée **en mode photo** (module `immich`) est toujours recadrée pour remplir le cadre, ou posée entière sur un fond flou d'elle-même si elle est nettement plus haute que large. Taille maximale : 240 × 240 (l'écran entier).

Une image indisponible renvoie une erreur HTTP ; l'afficheur dessine alors un remplaçant (pastille de couleur avec le code). L'échec est mémorisé une heure : pas de nouvelle tentative à chaque affichage.

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
| `volumes: ./icones:/icones:ro` (facultatif) | Icônes personnelles des pages de cours, lues seulement (section 6) |
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
| Sortant | Open-Meteo, CoinGecko, Yahoo Finance, BCE (Frankfurter) | HTTPS |
| Sortant | Discord | WebSocket sécurisé (HTTPS) |
| Sortant | Uptime Kuma, Plex | HTTP sur le réseau local |
| Sortant | Images : CDN Discord, CoinGecko, Logo.dev, GitHub (nvstly/icons) | HTTPS |
| Sortant | Immich (photos) | HTTP sur le réseau local |

Le port 8080 n'a **aucune authentification** : il ne doit pas être ouvert vers Internet.

## 9. Rythme des appels de l'afficheur

| Appel | Fréquence | Pourquoi |
|---|---|---|
| `/modules` | toutes les 10 min, et à l'ouverture de l'interface | Suivre les modules activés |
| `/time` | toutes les 10 min | Recaler l'horloge interne |
| `/discord` | toutes les 5 s | Réagir vite aux arrivées (réponse instantanée, depuis la mémoire) |
| `/supervision` | toutes les 30 s | Alerte rapide en cas de panne |
| `/plex` | toutes les 5 s | Montrer la page dès le début d'une lecture, suivre la progression |
| `/meteo`, `/btc`, `/actions`, `/devises`, `/commodities` | à chaque affichage de la page | Données en cache côté passerelle |
| `/img` | à chaque dessin d'une page avec image | Images converties gardées en mémoire par la passerelle |

## 10. Observer la passerelle

```bash
docker compose logs -f gateway                    # démarrage, modules, erreurs de sources
curl http://IP_SERVEUR:8080/health                # état de chaque module, bot Discord compris
curl http://IP_SERVEUR:8080/modules               # ce que voit l'afficheur
curl "http://IP_SERVEUR:8080/supervision" # exactement ce que reçoit l'écran
docker stats modelrogs-gateway                    # mémoire et processeur
```

N'importe quelle adresse appelée par l'afficheur peut s'ouvrir dans un navigateur : on voit exactement les données qu'il reçoit. C'est le meilleur réflexe de diagnostic quand une page affiche « Données indisponibles ».
