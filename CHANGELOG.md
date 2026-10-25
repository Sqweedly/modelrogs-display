# Journal des versions

Toutes les évolutions de MODELROGS Display : firmware de l'afficheur (SmallTV ESP8266) et passerelle Docker.

- Les numéros suivent la règle **majeure.mineure.correctif** :
  - correctif (2.6.**1**) : retouche légère ou correction ;
  - mineure (2.**7**.0) : nouvelle fonction, nouvel écran ou nouveau réglage ;
  - majeure (**3**.0.0) : refonte ou rupture de compatibilité ; il faudra alors modifier l'étiquette de l'image dans `docker-compose.yml` (`:2` → `:3`).
- Firmware et passerelle portent toujours le même numéro.
- Chaque version est publiée dans les **Releases** du dépôt (firmware, `docker-compose.yml`, empreintes). L'image de la passerelle est `ghcr.io/sqweedly/modelrogs-gateway`.
- **Mise à jour :** passerelle d'abord (`docker compose pull && docker compose up -d`), puis afficheur (`http://192.168.4.1/update`). La configuration de l'afficheur est conservée d'une version à l'autre.

---

## [2.7.1] — 2026-10-25

Pages Discord et Météo réorganisées, bas de l'écran mieux utilisé.

### Modifié
- **Discord**
  - En-tête : icône du serveur (40 px) et nom du serveur seulement, centrés ensemble ; plus de nombre de personnes ni de nom de salon.
  - Photos de profil aussi grandes que possible selon le nombre de personnes : 1 en grand, 2 côte à côte, 2 × 2 pour 3 ou 4, 3 colonnes sur 2 lignes pour 5 ou 6. Au-delà de 6 : 5 photos et une pastille « +N ».
  - Pseudo centré sous chaque photo, raccourci à 7 caractères + « … » au-delà de 8 ; taille choisie pour que tous les pseudos de l'écran tiennent.
  - Espaces égaux entre les photos, en largeur comme en hauteur ; durée de présence retirée.
- **Météo** : 2 jours en haut et 3 en bas ; chaque rangée est répartie sur la largeur avec des espaces égaux ; icônes plus grandes.
- **Bas de l'écran** : sur les pages Discord, Météo et Crypto (2 ou 3 cryptos), le contenu descend jusqu'à 6 px au-dessus des points de pagination.

### Corrigé
- **Crypto, 3 cryptos** : la virgule des prix (et le bas du « $ ») ne touche plus les variations 24 h et 7 j.

### Mise à jour
- Passerelle inchangée (republiée en 2.7.1 avec le même code) : seul l'afficheur est à mettre à jour. `docker-compose.yml` inchangé.

## [2.7.0] — 2026-10-25

Pages Heure, Météo et Crypto redessinées, pages Crypto sur 30 jours.

### Ajouté
- **Crypto, page dédiée** : avec 2 ou 3 cryptos, chaque crypto dont la variation sur 7 jours atteint **+5 % ou −5 %** a sa propre page, identique à celle d'une crypto seule (30 jours). Elle s'affiche juste après l'écran « prix en vedette », avec la durée d'une page normale.
- **Crypto, courbe sur 30 jours** pour une crypto seule et pour les pages dédiées, avec la mention « 30 J » en filigrane dans le graphique.
- **Variation sur 30 jours** en plus des variations 24 h et 7 jours.

### Modifié
- **Heure**
  - En-tête : jour à gauche, date JJ/MM/AA à droite (en gras, calculée par l'afficheur), alignés sur les bords de l'heure.
  - **Météo du jour** à la place du lever et du coucher du soleil, sans carte ni libellé, posée en bas de l'écran : icône à gauche, température actuelle en grand et min / max à droite.
  - L'icône est agrandie selon son empreinte réelle (un soleil rond n'occupe pas la même place qu'un nuage avec pluie) ; la température et le min / max sont centrés sur sa hauteur. Avec une température longue (« -12° »), l'icône rétrécit un peu pour que rien ne dépasse.
  - L'heure est centrée entre l'en-tête et le haut de la météo.
- **Météo** : prévisions sur **5 jours**, sans nom de ville ni météo du jour, en 5 blocs identiques (3 en haut, 2 en bas, centrés, espaces égaux). Chaque icône est ajustée à son empreinte dans une zone fixe ; les températures passent en 12 px quand elles sont trop longues pour garder un écart entre les blocs.
- **Crypto**
  - Plus de titre, de date ni de bulle.
  - **1 crypto** : logo et prix centrés sur une ligne (prix centré sur la hauteur du logo), variations 24 h, 7 j et 30 j en colonnes, puis la plus grande courbe possible sur 30 jours.
  - **2 cryptos** : pour chacune, logo de 56 px et prix centrés ensemble, puis « 24 h … » et « 7 j … » sur une ligne, centrés.
  - **3 cryptos** : le logo a exactement la hauteur du bloc prix + variations (même haut, même bas), « 24 h … » et « 7 j … » sur une ligne, en colonnes communes ; le tout centré sur l'écran.
  - Tailles de police choisies automatiquement : les plus grandes qui tiennent pour toutes les cryptos de l'écran ; espaces verticaux égaux entre les lignes.
  - Intitulés des variations (24 h, 7 j, 30 j) dans la couleur de la variation (vert ou rouge).
  - **Prix** avec au moins 4 chiffres, 2 décimales quand elles suffisent : 57 864 €, 142,35 €, 2,184 €, 0,08214 €.
- **Passerelle**
  - 6 jours demandés à Open-Meteo (aujourd'hui et les 5 suivants).
  - Variations 24 h, 7 j et 30 j de chaque crypto ; indicateur de page dédiée ; courbe sur 30 jours (CoinGecko `market_chart`, gardée 30 minutes) et route `/btc/30j`.
- Polices : symbole « ° » ajouté au 32 px ; nouvelle taille de 26 px (prix).

### Mise à jour
- Passerelle avant l'afficheur, sinon la page Météo n'affiche que 3 jours et les écrans Crypto n'ont ni courbe ni variation sur 30 jours. `docker-compose.yml` inchangé.

---

## [2.6.0] — 2026-10-24

Nouvelles pages Plex et Crypto, page Discord refaite, images sur l'écran.

### Ajouté
- **Images sur l'écran** (affiches, logos, photos de profil) : la passerelle télécharge l'image, la recadre, arrondit ses coins, la pose sur la couleur de fond et l'envoie en pixels prêts à afficher (route `/img`). L'afficheur la dessine ligne par ligne, sans jamais garder l'image entière en mémoire.
- **Page Plex** (module `plex`, variables `PLEX_URL` et `PLEX_TOKEN`)
  - Visible **uniquement pendant une lecture**, et affichée en premier dans les 5 secondes qui suivent le début d'une lecture.
  - Une lecture : **affiche** du film (de la série pour un épisode, de l'album pour une musique), titre, qui regarde, lecture directe, transcodage ou pause, progression et temps restant.
  - Plusieurs lectures : liste de 3 lignes.
  - Seule la barre de progression est redessinée pendant la lecture.
- **Page Crypto** (remplace la page Bitcoin ; le module garde l'identifiant `btc`, `crypto` est accepté dans `MODULES`)
  - Jusqu'à **3 cryptos** au choix parmi 10 (Bitcoin, Ethereum, XRP, Solana, BNB, Cardano, Dogecoin, TRON, Litecoin, Polkadot). Une 4e case cochée est refusée.
  - Mise en page selon le nombre : une grande page, deux moitiés ou trois lignes.
  - **Logos officiels** fournis par CoinGecko.
  - Une seule requête CoinGecko toutes les 10 minutes pour toutes les cryptos.

### Modifié
- **Discord** : icône du serveur, **photos de profil**, durée de présence, micro ou casque coupé, partage d'écran (LIVE), caméra. Jusqu'à 4 personnes : une carte chacune ; au-delà, une grille de photos (et « +N » au-delà de 8).
- **Supervision** : page **masquée quand tout va bien** ; elle apparaît en cas de panne, de service instable ou si Kuma ne répond pas. L'écran d'alerte est inchangé.
- **Supervision** : une sonde ajoutée ou retirée dans Kuma apparaît sur l'écran et dans l'interface **en une minute environ** (au lieu d'un quart d'heure).
- Image Docker : environ 75 Mo (bibliothèque d'images Pillow, compilée pour ARM 32 bits).

### Corrigé
- Images incomplètes : la passerelle ne gardait que le premier morceau des fichiers téléchargés, et l'afficheur dessinait un remplaçant (« play » pour Plex, initiale pour Discord).

### Mise à jour
- `docker-compose.yml` : ajouter `PLEX_URL`, `PLEX_TOKEN` et `plex` dans `MODULES`.
- Afficheur : la page Plex est activée automatiquement ; les cryptos se cochent dans l'interface.

---

## [2.5.0] — 2026-10-24

### Ajouté
- **Supervision : tous les services par défaut.** L'écran affiche tout ce que montre la page de statut Kuma, y compris les sondes ajoutées plus tard. Dans l'interface, la case « Tous les services de la page de statut Kuma » est cochée par défaut ; décochée, on choisit les services un par un (48 au plus).

### Modifié
- Supervision : la bulle de synthèse (« Tout est OK », « N en panne ») revient en haut à droite, alignée sur le titre.
- `DISCORD_MEMBERS_INTENT` vaut `"0"` par défaut : l'accès privilégié « Server Members » n'est pas nécessaire.
- Le message « Aucun service sélectionné » disparaît.

### Mise à jour
- Passerelle avant l'afficheur, sinon l'écran Supervision affiche « 0 service ». Une sélection manuelle existante est conservée.

---

## [2.4.0] — 2026-10-24

### Ajouté
- **Redémarrage préventif chaque nuit à 2 h 15** (Wi-Fi arrêté puis redémarrage), avec la luminosité de nuit conservée pendant le redémarrage. Redémarrage aussi après 25 h sans heure connue, et en urgence si la mémoire reste trop morcelée.
- Section **État** de l'interface : mémoire minimale, plus grand bloc libre, cause du dernier démarrage, nombre de redémarrages préventifs, délai avant le prochain essai vers la passerelle.

### Modifié
- **Couleurs saturées**, écran d'alerte en rouge profond, polices plus épaisses.
- L'écran d'alerte **alterne avec les pages** (un écran sur deux) au lieu de rester affiché ; une nouvelle panne s'affiche immédiatement.
- Pages Heure et Bitcoin sur toute la surface de l'écran.
- Météo : jours à venir en min / max, mêmes couleurs que le jour actuel (min en bleu, max en ambre).
- Appels espacés quand la passerelle ne répond pas (5, 15, 30 puis 60 s), pour que l'afficheur reste réactif.
- Passerelle : cache limité à 200 entrées, ville introuvable mémorisée 1 h, noms de ville normalisés (« Lyon » = « lyon »).
- Passerelle : le dossier du cache sur le serveur est créé et reçoit les bons droits au démarrage (`PUID` / `PGID` facultatifs), puis la passerelle abandonne les droits root.

### Corrigé
- Heure : le « : » est centré (mêmes écarts de chaque côté, centré sur la hauteur des chiffres) ; « 00:00 » ne dépasse plus de l'écran.
- Passerelle : erreur « Permission denied » sur le dossier du cache.

### Mise à jour
- `docker-compose.yml` : reprendre celui de la Release (lignes `cap_add`).

---

## [2.3.0] — 2026-10-23

### Ajouté
- **Compilation et publication automatiques par GitHub Actions** : un tag `vX.Y.Z` lance les tests, compile le firmware, publie l'image Docker multi-architecture et crée la Release avec les fichiers et leurs empreintes.
- Chaque envoi sur `main` lance les tests et compile un firmware de test, téléchargeable 7 jours.

### Modifié
- Les binaires ne sont plus dans le dépôt : ils sont publiés dans les Releases.
- Un seul numéro de version, issu du tag, pour le firmware et la passerelle.

---

## [2.2.1] — 2026-10-23

### Ajouté
- Documentation détaillée de la passerelle (`docs/PASSERELLE.md`).

### Modifié
- Numéros de version alignés entre passerelle et firmware. Firmware identique à 2.2.0.

## [2.2.0] — 2026-10-23

### Modifié
- Orientation de l'écran identique à l'ancien firmware.
- Réglages d'usine de la dalle conservés : noirs profonds, fond noir pur.
- Marges de 3 px en haut et en bas, masquées par le boîtier.
- Point d'accès renommé « MODELROGS Display » à la reprise d'une ancienne configuration.
- **« Configuration à valider »** seulement après l'installation d'un firmware différent ; la configuration et sa validation sont conservées entre versions du même firmware.
- Luminosité de jour par défaut à 60 %.

## [2.1.1] — 2026-10-23

### Ajouté
- **Mode secours** : après 3 plantages au démarrage, seuls le Wi-Fi, l'interface web et l'OTA tournent, pour pouvoir toujours reflasher.

### Modifié
- `docker-compose.yml` : plafond mémoire désactivé par défaut (erreur « memory.max » sur Raspberry Pi OS et certains NAS).

---

## [2.1.0] — 2026-10-22

Première version publiée dans ce dépôt.

### Ajouté
- **Passerelle modulaire** (Python aiohttp, image Alpine sur ghcr.io) : modules `heure`, `meteo`, `btc`, `supervision` et `discord` (bot intégré), choisis dans `docker-compose.yml` ; secrets et accès dans les variables d'environnement.
- **Firmware SmallTV** : pages Heure, Météo, Discord, Supervision et Bitcoin en rotation, écran d'alerte en cas de panne.
- Interface web de l'afficheur (point d'accès ouvert, `192.168.4.1`) construite d'après les modules de la passerelle : Wi-Fi, passerelle, ville, pages, réglages des modules, nuit et luminosités.
- OTA `/update` conservée.
- Reprise automatique de la configuration du firmware « MODELROGS Discord » et de MODELROGS Display 2.0 (Wi-Fi, passerelle, services supervisés).

[2.7.1]: https://github.com/Sqweedly/modelrogs-display/releases/tag/v2.7.1
[2.7.0]: https://github.com/Sqweedly/modelrogs-display/releases/tag/v2.7.0
[2.6.0]: https://github.com/Sqweedly/modelrogs-display/releases/tag/v2.6.0
[2.5.0]: https://github.com/Sqweedly/modelrogs-display/releases/tag/v2.5.0
[2.4.0]: https://github.com/Sqweedly/modelrogs-display/releases/tag/v2.4.0
[2.3.0]: https://github.com/Sqweedly/modelrogs-display/releases/tag/v2.3.0
[2.2.1]: https://github.com/Sqweedly/modelrogs-display/releases/tag/v2.2.1
[2.1.0]: https://github.com/Sqweedly/modelrogs-display/releases/tag/v2.1.0
