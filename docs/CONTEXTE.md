# Reprendre le projet MODELROGS Display

Ce fichier sert à reprendre le projet exactement où il s'est arrêté, avec une nouvelle personne ou un nouvel assistant. Il complète :
- `README.md` : installation et usage ;
- `docs/PASSERELLE.md` : fonctionnement de la passerelle ;
- `docs/DEVELOPPEMENT.md` : outils, compilation, tests, simulateur ;
- `CHANGELOG.md` : historique.

Il rassemble ce que le code ne dit pas : l'état à la clôture, la façon de travailler, les choix de design retenus et le backlog.

À lire en premier, dans cet ordre : ce fichier, `CHANGELOG.md` (les 3 dernières versions), puis `docs/DEVELOPPEMENT.md`.

---

## 1. État à la clôture (2026-10-31)

- **Dernière version : 2.12.1**, tag `v2.12.1` sur `main`. Le numéro ne vient que du tag : GitHub Actions l'injecte dans le firmware et dans l'image. Aucun numéro n'est écrit dans le code.
- **Vérifié avant la clôture**, sur une copie vierge du dépôt :
  - tests de la passerelle (`python -m tests.test_passerelle`), tous passés ;
  - test de conservation de la configuration du firmware (structure `Config` v8, 908 octets sur 1024) ;
  - simulateur (44 écrans, 420 cas de placement de textes, 0 différence) ;
  - compilation complète du firmware ESP8266 : 592 Ko, RAM 46 %, IRAM 93 %. Elle a été faite avec arduino-cli et les mêmes options que `platformio.ini`. La compilation PlatformIO officielle est celle de GitHub Actions.
- **Non vérifié avant la clôture** :
  - la construction de l'image Docker (pas de Docker disponible pour la vérification ; GitHub Actions la construit à chaque tag) ;
  - le fonctionnement sur le matériel réel : voir le § 5, « À vérifier en premier ».

## 2. Environnement du propriétaire

Le dépôt est privé. Aucun jeton n'est écrit ici : ils ne vont que dans le `docker-compose.yml` de la machine.

| Élément | Valeur |
|---|---|
| Dépôt | `github.com/Sqweedly/modelrogs-display`, privé. Le propriétaire pousse lui-même : pas d'application Claude installée sur GitHub. |
| Image de la passerelle | `ghcr.io/sqweedly/modelrogs-gateway:2` (`pull_policy: always`) |
| « Docker passerelle » | la machine qui fait tourner le conteneur `modelrogs-gateway`, à l'adresse `192.168.1.212:8680` (port publié 8680) |
| Immich | `http://192.168.1.241:2283`. Version annoncée « 3.10 », à confirmer dans Administration > À propos. Albums « 18 ans Justine » et « Alana ». |
| Afficheur | GeekMagic SmallTV (ESP8266, écran ST7789 240 × 240). Mise à jour par `http://<afficheur>/update`, à conserver. |

## 3. Façon de travailler (règles du propriétaire)

1. **Avant chaque compilation ou version**, demander : *remplacer la version actuelle* (même numéro, tag déplacé) ou *émettre une nouvelle version*.
2. **Numérotation** :
   - correctif (x.y.**Z**) : retouche légère ;
   - mineure (x.**Y**.0) : modification plus importante, nouvelle page ou nouvelle fonction ;
   - majeure (**X**.0.0) : grand changement. L'étiquette `:2` de l'image change alors dans `docker-compose.yml`.
3. **`CHANGELOG.md` complet**, mis à jour à chaque version, y compris quand une version est remplacée.
4. **Avant de corriger, dire ce qu'on va faire** : cause trouvée, correction proposée, puis attendre l'accord. Pour un changement d'écran, montrer un design (maquette PNG produite avec le simulateur) et attendre sa validation. Le propriétaire demande souvent « résume et propose » : un résumé précis et complet, puis une proposition.
5. **Pour faire tester une fonction ou une page de la passerelle**, parler du « docker passerelle » et donner des commandes qui visent son adresse et son port, par exemple `curl -s "http://192.168.1.212:8680/immich?s=souvenirs" ; echo`. Ne pas donner de commandes visant le port d'Immich ou de Plex, sauf pour un diagnostic explicite, en le disant.
6. **Jamais de jeton ni de clé** dans un fichier suivi par Git.
7. **Livraison** : un zip du dépôt **avec le dossier `.git`** (historique, `main`, tags), pour que le propriétaire pousse lui-même. Un zip sans `.git` (`git archive`) ne convient pas. Dans le zip :
   - `core.fileMode=false`, sinon `apercu.sh` paraît modifié après une décompression sous Windows ;
   - `main` suit `origin/main`.

   Commandes de poussée :
   ```
   git push origin main
   git push origin vX.Y.Z            # nouvelle version
   git push --force origin vX.Y.Z    # version remplacée
   ```
8. **Être précis et autocritique** dans les comptes rendus : ce qui est vérifié, ce qui ne l'est pas, ce qui reste supposé.

## 4. Backlog

À traiter. Pour chacun, résumer et proposer avant de coder.

1. **Discord** : afficher les amis connectés au serveur Discord qui ne sont dans aucun salon vocal.
2. **Plex, nouveautés** : un écran des derniers ajouts du serveur Plex, avec une page Films et une page Séries (affiche et date d'ajout).
3. **Tesla** : afficher la charge de la batterie d'une Tesla, source à définir (TeslaMate ou autre).
4. **Piste d'amélioration (non demandée)** : préparer à l'avance la photo suivante de la page Photos. Aujourd'hui, l'image est téléchargée et convertie quand l'afficheur la demande ; sur une machine lente, le délai de 2,5 s de l'afficheur peut être dépassé. Voir « Limites connues » dans `docs/PASSERELLE.md`.

## 5. À vérifier en premier (sur le matériel réel)

La page Photos (2.12.x) n'a été validée sur le vrai serveur que pour la source « Au hasard ». Après `docker compose pull && docker compose up -d` sur le docker passerelle :

```
curl -s "http://192.168.1.212:8680/immich?s=souvenirs" ; echo     # attendu : "b":"Il y a N ans"
curl -s "http://192.168.1.212:8680/immich?s=favoris" ; echo       # attendu : {"ok":1,"i":"…"}
curl -s "http://192.168.1.212:8680/immich?s=a%3A9de3b847-d1b9-4d93-973a-1d2eb559f80e" ; echo
curl -s "http://192.168.1.212:8680/immich/sources" ; echo
docker logs modelrogs-gateway 2>&1 | grep -i immich | tail -10
```

Le log doit afficher « N photo(s) en souvenir aujourd'hui », « N photo(s) dans les favoris » et « N photo(s) dans l'album … ».

Points précis à confirmer :
- **Souvenirs** (corrigés en 2.12.0, deuxième version) :
  - le format `for=AAAA-MM-JJ` vient de la spécification OpenAPI d'Immich (`open-api/immich-openapi-specs.json`, branche `main`) ;
  - `/api/assets/memory-lane` n'existe plus et n'est plus appelé.
- **Favoris** : le propriétaire a 1 favori qui ne s'affichait pas avant la 2.12.0. La relance sans le filtre « photos seulement » n'est pas confirmée sur son serveur.
- **Albums** : lus par `POST /api/search/metadata` avec `albumIds`, non confirmé sur son serveur.
- Vérifier aussi que la publication GitHub Actions de `v2.12.1` est verte : onglet Actions, puis la Release et l'image `2.12.1` sur ghcr.io.

## 6. Choix de design retenus

Ces choix ont été validés par le propriétaire sur maquette. Ne pas les changer sans demande.

- **Général** :
  - fond noir pur, flat design en couleurs (pas d'icônes monochromes) ;
  - textes secondaires en gris **neutres**, sans teinte bleue : `GRIS_CLAIR` C8C8C8, `GRIS_REPERE` A0A0A0, `GRIS_GRILLE` 3C3C3C ;
  - le code de dessin est dans `firmware/src/pages.cpp` et `ui.cpp` ; les couleurs d'accent par page sont dans `ui.h`.
- **Heure** : en-tête « mercredi 28 octobre » (jour et mois en toutes lettres, en minuscules, sans l'année), en blanc et centré sur la largeur (design A, 2.9.1).
- **Pages de cours** (Crypto, Actions, Indices, Devises, Commodities, Custom) :
  - tous les prix sont en dollars, sauf les indices, en points ;
  - vrais logos sans nom à l'écran : logo à pleine taille (44 px), sans disque ni contour (2.11.0) ;
  - devises et indices : pastilles avec le symbole (`gateway/tools/icones_devises.py`) ;
  - icônes personnelles possibles dans le volume `/icones` de la passerelle.
- **Écran à une seule valeur** (30 jours) :
  - en haut, l'icône et le prix, centré sur la hauteur de l'icône ;
  - bulles « 7 j » et « 30 j » sur une ligne (pas de 24 h) ;
  - courbe remplie d'une couleur unie à 15 %, sans dégradé ;
  - 2 repères de prix sans unité : le repère bas a sa valeur au-dessus de la ligne, le repère haut en dessous ;
  - dates sur l'axe horizontal, aucun filigrane ;
  - intitulés 24 h / 7 j / 30 j en blanc.
- **Écrans à 2 ou 3 valeurs** :
  - 2 lignes par valeur : icône tout à gauche et prix centré sur sa hauteur, puis les variations 24 h / 7 j en bulles ;
  - pas de carte grise de fond (2.11.0) ;
  - tailles de police uniformes sur un même écran, identiques pour toutes les catégories.
- **Plex** : nom de l'utilisateur centré à droite de l'affiche. À 2 lecteurs, le nom est centré et abaissé de 6 px, sauf si le titre tient sur 2 lignes.
- **Discord** : icône en haut à gauche, nom du salon centré sur l'espace à droite de l'icône. La page n'apparaît que si quelqu'un est connecté.
- **Photos** (design D, 2.12.0) :
  - photo seule en plein écran, sans texte ni points de pagination ;
  - pastille violette « Il y a N ans » en haut à gauche pour un souvenir ;
  - photo en hauteur : entière, sur un fond flou fait d'elle-même ;
  - durée d'affichage propre à la page (30 s par défaut, de 10 s à 5 min).
- **Ordre de rotation** : Heure, Météo, Discord, Supervision, Crypto, Actions, Indices, Devises, Commodities, Custom, Photos, Plex. Plex passe en priorité au début d'une lecture ; Discord et Plex sont masqués quand ils n'ont rien à montrer.

## 7. Règles techniques à ne pas casser

- **Réponses lues par l'afficheur** : moins de 2 048 octets, 4 096 pour `/modules`. Les tests le vérifient pour chaque route.
- **Configuration de l'afficheur en EEPROM** (`Config`, `firmware/src/config.h`) :
  - toute modification ajoute ses champs **à la fin** de la structure, augmente `CONFIG_VERSION` et ajoute une migration dans `config.cpp` ;
  - la migration est couverte par `tools/simulateur/test_config.cpp` ;
  - les réglages de l'utilisateur ne doivent jamais être perdus lors d'une mise à jour.
- **`PageId`** : ajouter les nouvelles pages à la fin ; l'ordre d'affichage est donné par `ROTATION`.
- **Taille du firmware et de l'IRAM** : l'IRAM est à 93 %. Ne rien ajouter en `IRAM_ATTR` sans nécessité.
- **Images** : la passerelle fait tout le travail (Pillow) et envoie du RGB565 ligne par ligne, 240 × 240 au plus. L'afficheur ne garde jamais une image entière en mémoire.
- **Textes affichés** : tout passe par `ecran()` (`gateway/app/text.py`). Le jeu de caractères doit rester identique à celui des polices.
