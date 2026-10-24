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

## [2.7.0] — 2026-10-24

Pages Heure, Météo et Crypto redessinées.

### Modifié
- **Heure**
  - En-tête en gras : le jour à gauche, la date au format JJ/MM/AA à droite, calculée par l'afficheur.
  - Chiffres de l'heure réduits d'environ 10 % (68 px au lieu de 76 px).
  - Carte **météo du jour** à la place du lever et du coucher du soleil : grande icône, température actuelle, min et max. Pas de libellé, l'icône suffit.
  - Tout est aligné sur les mêmes bords, avec des espaces égaux entre l'en-tête, l'heure et la carte.
- **Météo**
  - Prévisions sur **5 jours**, sans nom de ville ni météo du jour (affichée sur la page Heure).
  - Les 2 prochains jours en grand, les 3 suivants dessous, avec les plus grandes icônes possibles.
- **Crypto**
  - Plus de titre, de date ni de mention « 7 jours ».
  - 1 crypto : logo et nom agrandis, courbe jusqu'en bas de l'écran.
  - 2 cryptos : deux moitiés rééquilibrées, logos et noms agrandis.
  - 3 cryptos : trois bandes comme la page à 2 ; la variation sur 7 jours est centrée automatiquement entre le symbole et le prix.
  - Prix avec **2 décimales au plus** (sans décimale à partir de 1 000) : 57 864 €, 142,35 €, 2,18 €.
- **Passerelle :** 6 jours demandés à Open-Meteo (aujourd'hui et les 5 suivants).
- Polices : symbole « ° » ajouté au 32 px.

### Mise à jour
- Passerelle avant l'afficheur, sinon la page Météo n'affiche que 3 jours. `docker-compose.yml` inchangé.

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

[2.7.0]: https://github.com/Sqweedly/modelrogs-display/releases/tag/v2.7.0
[2.6.0]: https://github.com/Sqweedly/modelrogs-display/releases/tag/v2.6.0
[2.5.0]: https://github.com/Sqweedly/modelrogs-display/releases/tag/v2.5.0
[2.4.0]: https://github.com/Sqweedly/modelrogs-display/releases/tag/v2.4.0
[2.3.0]: https://github.com/Sqweedly/modelrogs-display/releases/tag/v2.3.0
[2.2.1]: https://github.com/Sqweedly/modelrogs-display/releases/tag/v2.2.1
[2.1.0]: https://github.com/Sqweedly/modelrogs-display/releases/tag/v2.1.0
