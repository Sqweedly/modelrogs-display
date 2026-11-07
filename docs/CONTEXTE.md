# Reprendre le projet MODELROGS Display

Ce fichier sert à reprendre le projet exactement où il s'est arrêté, avec une nouvelle personne ou un nouvel assistant. Il complète :
- `README.md` : installation et usage ;
- `docs/PASSERELLE.md` : fonctionnement de la passerelle ;
- `docs/DEVELOPPEMENT.md` : outils, compilation, tests, simulateur ;
- `CHANGELOG.md` : historique.

Il rassemble ce que le code ne dit pas : l'état à la clôture, la façon de travailler, les choix de design retenus et le backlog.

À lire en premier, dans cet ordre : ce fichier, `CHANGELOG.md` (les 3 dernières versions), puis `docs/DEVELOPPEMENT.md`.

---

## 1. État à la clôture (2026-11-05)

- **En essai sur `beta` : 3.1.0** (personnages, backlog n° 22 ; écran de nuit en « cadran évidé »), pas encore publiée : à tester sur l'afficheur avec le firmware `dev-<commit>` de **Actions → Vérification → Branch: beta**, puis à passer sur `main` avec le tag `v3.1.0`.
- **Dernière version publiée : 3.0.2** (2026-11-07 : écran de nuit en horloge à aiguilles, backlog n° 21). Précédente : 3.0.1 (2026-11-06 : la nuit, une panne affiche le même bloc PANNE + récap Supervision que le jour). Précédente : **3.0.0**, tag `v3.0.0` sur `main` (interface web à 5 onglets, écran de nuit, nouvel essai réseau, rétroéclairage sigma-delta, ville sur la passerelle ; voir `CHANGELOG.md`). Publiée directement sur `main`, sans beta, à la demande du propriétaire (exception à la règle n° 9) ; un commit par point du backlog pour pouvoir en retirer un. Précédente : 2.15.0 (page Tesla). Le diagnostic a été validé sur le matériel réel par le propriétaire (mesure du 2026-11-01, voir backlog n° 13 et 14). Le numéro ne vient que du tag : GitHub Actions l'injecte dans le firmware et dans l'image. Aucun numéro n'est écrit dans le code.
- **Vérifié avant la clôture**, sur une copie vierge du dépôt :
  - tests de la passerelle (`python -m tests.test_passerelle`), tous passés ;
  - test de conservation de la configuration du firmware (structure `Config` v8, 908 octets sur 1024 ; `reserve[0]` bit 0 = page Tesla déjà activée une fois, 2.15.0) ;
  - page Tesla : tests avec un vrai serveur mosquitto en local (sauté sur GitHub, où mosquitto n'est pas installé) ;
  - 3.0.0 : compilation PlatformIO 6.1.16 locale : code en IRAM 27 735 octets (−892 avec le sigma-delta), RAM statique 37 780 octets, flash 608 135 octets ; banc d'essai de l'interface web (`test_web.cpp`) passé ; écran de nuit rendu au simulateur. **Non vérifié sur le matériel** : écran de nuit, alerte de nuit, nouvel essai réseau, mémoire libre pendant l'envoi de chaque onglet ;
  - 3.0.2 : flash 609 419 octets (+1 332), RAM statique 37 784 (+4), IRAM inchangée ; test de l'écran de nuit (`test_nuit.cpp`, redessin partiel = complet, 2 363 cas) passé ; polices régénérées avec Inter 4.1 (seule `F_26` change, « : » ajouté). **Non vérifié sur le matériel** : ambre la nuit, scintillement ;
  - 3.1.0 (beta) : flash 620 999 octets (+11 580 depuis la 3.0.2), RAM statique 37 972 (+188), IRAM inchangée ; `test_persos` (116 cas), `test_nuit` (redessin partiel et absence de recouvrement), `test_config` (personnages activés d'office) passés. **Non vérifié sur le matériel** : enchaînement des blocs, temps de dessin, rendu ;
  - `/modules` de la passerelle : 4 077 octets dans les tests pour un plafond de 4 096 (mémoire de l'afficheur) ; ne plus rien y ajouter sans réduire ailleurs ;
  - simulateur (44 écrans, 420 cas de placement de textes, 0 différence) ;
  - compilation complète du firmware ESP8266 avec PlatformIO 6.1.16 (version de la CI) : RAM 45,4 %, code en IRAM 28 627 octets sur 32 768. Seule la compilation de GitHub Actions fait foi pour les binaires livrés.
- **Leçon de la 3.0.0** (publication du 2026-11-05 arrêtée dans les tests) : lancer les tests de la passerelle avec **Python 3.12**, la version de GitHub (`/usr/bin/python3.12` dans un environnement neuf, `pip install -r requirements.txt`), et aussi avec une horloge `time.monotonic()` petite (machine démarrée depuis quelques secondes, comme une machine de GitHub) ; dans un test, ne jamais mettre un repère de `time.monotonic()` à 0 (utiliser `-RELECTURE_PERSO` ou `images.inventaire_perso()`). Les journaux de GitHub Actions se lisent avec `gh run view <id> --log-failed` quand le dépôt est attaché à la session.
- **Non vérifié avant la clôture** :
  - la construction de l'image Docker (pas de Docker disponible pour la vérification ; GitHub Actions la construit à chaque tag) ;

## 2. Environnement du propriétaire

Le dépôt est privé. Aucun jeton n'est écrit ici : ils ne vont que dans le `docker-compose.yml` de la machine.

| Élément | Valeur |
|---|---|
| Dépôt | `github.com/Sqweedly/modelrogs-display`, privé. Le propriétaire pousse lui-même : pas d'application Claude installée sur GitHub. |
| Image de la passerelle | `ghcr.io/sqweedly/modelrogs-gateway:3` depuis la 3.0.0 (`pull_policy: always`) ; ville : `VILLE: Sernhac` à mettre dans son `docker-compose.yml` |
| « Docker passerelle » | la machine qui fait tourner le conteneur `modelrogs-gateway`, à l'adresse `192.168.1.X:8680` (port publié 8680) |
| Immich | `http://192.168.1.X:2283`. Version annoncée « 3.10 », à confirmer dans Administration >  |
| Afficheur | GeekMagic SmallTV (ESP8266, écran ST7789 240 × 240). Mise à jour par `http://<afficheur>/update`, à conserver. |

## 3. Façon de travailler (règles du propriétaire)

1. **Avant chaque compilation ou version**, demander : *remplacer la version actuelle* (même numéro, tag déplacé) ou *émettre une nouvelle version*.
2. **Numérotation** :
   - correctif (x.y.**Z**) : retouche légère ;
   - mineure (x.**Y**.0) : modification plus importante, nouvelle page ou nouvelle fonction ;
   - majeure (**X**.0.0) : grand changement. L'étiquette majeure de l'image (`:3` depuis la 3.0.0) change alors dans `docker-compose.yml`.
3. **`CHANGELOG.md` complet**, mis à jour à chaque version, y compris quand une version est remplacée.
4. **Avant de corriger, dire ce qu'on va faire** : cause trouvée, correction proposée, puis attendre l'accord. Pour un changement d'écran, montrer un design (maquette PNG produite avec le simulateur) et attendre sa validation. Le propriétaire demande souvent « résume et propose » : un résumé précis et complet, puis une proposition.
5. **Pour faire tester une fonction ou une page de la passerelle**, parler du « docker passerelle » et donner des commandes qui visent son adresse et son port, par exemple `curl -s "http://192.168.1.X:8680/immich?s=souvenirs" ; echo`. Ne pas donner de commandes visant le port d'Immich ou de Plex, sauf pour un diagnostic explicite, en le disant.
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
9. **Versions de test : branche `beta`**. Les essais se font sur la branche `beta`, jamais sur `main`. Après `git push origin beta`, le firmware de test se compile par **Actions → Vérification → Run workflow → Branch: beta**. Il est téléchargeable 7 jours dans la rubrique « Artifacts » de l'exécution (`firmware-dev`, version affichée `dev-<commit>`). La même exécution publie l'image de la passerelle avec la seule étiquette `beta` (`ghcr.io/sqweedly/modelrogs-gateway:beta`), à mettre temporairement dans le `docker-compose.yml`. Aucune Release n'est créée. Seuls ces binaires de GitHub font foi : un binaire compilé ailleurs (arduino-cli, par exemple) n'est pas identique.

## 4. Backlog

À traiter. Pour chacun, résumer et proposer avant de coder.

Points faits retirés le 2026-11-06 (n° 3, 9, 11, 12, 14, 15, et 5 et 13 vérifiés par le propriétaire) et le 2026-11-07 (n° 21, fait en 3.0.2) ; numéros d'origine gardés (cités dans le CHANGELOG). Pas de version cible pour la suite.

1. **Discord** : afficher les amis connectés au serveur Discord qui ne sont dans aucun salon vocal.
2. **Plex, nouveautés** : un écran des derniers ajouts du serveur Plex, avec une page Films et une page Séries (affiche et date d'ajout).
4. **Piste d'amélioration (non demandée)** : préparer à l'avance la photo suivante de la page Photos. Aujourd'hui, l'image est téléchargée et convertie quand l'afficheur la demande ; sur une machine lente, le délai de 2,5 s de l'afficheur peut être dépassé. Voir « Limites connues » dans `docs/PASSERELLE.md`.
6. **Passerelle, Yahoo** : erreur intermittente `curl: (16) Error in the HTTP2 framing layer` sur l'indice `^GSPC` (journal du 2026-10-31). La donnée précédente reste affichée ; à examiner.
7. **Photos en JPEG** : envoyer la photo compressée en JPEG au lieu de RGB565 brut, et la décoder sur l'afficheur. Constat mesuré le 2026-10-31 : une photo fait 115 200 octets et sa réception prend 1 à 2,2 s (50 à 115 Ko/s), contre environ 50 ms pour l'écran et 200 ms au plus pour la passerelle ; la limite est le débit réseau de l'ESP8266 (fenêtre TCP de 2 144 octets en lwIP « Lower Memory »). Piste : bibliothèque TJpg_Decoder de Bodmer (basée sur tjpgd), dont l'auteur annonce 167 ms de décodage à 160 MHz pour son image de test (forum Arduino, « New Jpeg decoder library - based on tjpgd »). À établir avant de coder : taille réelle des JPEG 240 × 240, mémoire nécessaire pendant le décodage (le plus grand bloc libre descend à environ 20 Ko), lecture du flux réseau au fil de l'eau (sans garder l'image entière), comportement sur une image corrompue. La route `/img` en RGB565 reste en place pour les autres images et en secours. Essai sur la branche `beta`, avec mesure avant et après.
8. **Pile réseau « Higher Bandwidth »** (essai sur `beta` le 2026-11-03, **non concluant, suspendu**, piste laissée ouverte) : résultat de l'essai (1 h 54) : 103 images incomplètes sur 1 049 et 22 délais dépassés (contre 0 et 1 avec la 2.14.0 le 2026-11-01), blocages de plus de 2,4 s même sur des icônes de 2 888 octets, aucun gain mesurable sur les photos ; mais signal à −78/−82 dBm au lieu de −71/−75 : les deux causes ne peuvent pas être séparées (le propriétaire a depuis déplacé l'afficheur, retour à −72 dBm, et pense que la pile n'est pas en cause). Retour beta → 2.14.0 par OTA réussi. À refaire, si on reprend la piste, au même emplacement et dans la même heure que la 2.14.0. Description d'origine : `-D PIO_FRAMEWORK_ARDUINO_LWIP2_HIGHER_BANDWIDTH` dans `build_flags` (bibliothèque `lwip2-1460-feat`). MSS 536 → 1 460 octets, fenêtre de réception 2 144 → 5 840 octets, tampon d'envoi 1 072 → 2 920 octets. Compilé le 2026-10-31 : IRAM et RAM statique inchangées, flash +12 octets ; le coût est dans le tas pendant les transferts (tampons lwIP alloués dynamiquement). Gain attendu sur la réception des photos : jusqu'à environ 2,7 fois, non garanti. Touche indirectement l'OTA (même pile réseau, code de l'OTA inchangé) : une OTA échouée laisse l'ancien firmware en place, et l'Updater réduit son tampon de 4 Ko à 256 octets si le tas libre passe sous 8 Ko. Méthode prévue : beta 1 = mesure seule (ligne « Dernière photo », réception en `readBytes` conservée) ; beta 2 = beta 1 + Higher Bandwidth ; relever temps de réception, minimums de mémoire et de plus grand bloc libre, images manquantes, et faire une OTA depuis la beta 2. Critères de succès fixés avant l'essai (mesures du diagnostic, 1 à 2 h, mêmes conditions que le 2026-11-01) : temps total médian des `/img` de photos nettement sous 2,1 s ; plus grand bloc libre jamais sous 10 Ko environ ; pas plus d'échecs réseau qu'avant (11 pour 6 633 requêtes) ; 3 OTA consécutives réussies depuis la beta, dont un retour à la 2.14.0. Sinon, ne pas intégrer à la 3.0.0.
10. **Réception des images en mode bloc** : dans `gwImage` (`firmware/src/net.cpp`), remplacer `readBytes` (lecture octet par octet héritée de `Stream`, avec un `tcp_recved` par octet) par une lecture en bloc (`available()` puis `WiFiClient::read(buf, n)`, arrêt si `connected()` est faux ou après 2,5 s sans données). Essayé en 2.12.1-sd2 : aucun gain de vitesse mesuré, et des « réceptions interrompues » (photos indisponibles, icônes remplacées par une pastille), absentes de la 2.12.1. Mécanisme non identifié par la lecture du code du cœur. Ne pas reprendre sans avoir trouvé la cause (relever d'abord les octets reçus au moment de l'échec, avec le diagnostic de la 2.12.1-sd3) ; d'ici là, la règle du § 6 s'applique (garder `readBytes`). Code de l'essai : `2.12.1-sd2.patch` et `2.12.1-sd3.patch` (diagnostic).
16. **Vigilance Météo-France** (demandé le 2026-11-06) : une page de vigilance pour le département de la ville affichée (`VILLE` de la passerelle), **affichée uniquement en vigilance orange ou rouge** (masquée en vert et en jaune). Source : API publique de Météo-France « Bulletin Vigilance », clé gratuite à créer sur portail-api.meteofrance.fr (à mettre dans `docker-compose.yml`, jamais dans Git) ; le bulletin donne, par département, la couleur maximale et une couleur par phénomène (vent, pluie-inondation, orages, crues, neige-verglas, canicule, grand froid…) pour J et J+1. À préciser au moment de le traiter : passage de la ville au numéro de département (le géocodage actuel donne le nom du département, pas son numéro), contenu de l'écran (maquette), place dans la rotation (simple page ou prioritaire comme une panne), comportement la nuit.
17. **Mot du jour** (demandé le 2026-11-06, idée « culture et apprentissage ») : un mot rare avec sa définition, ou un mot d'une langue apprise avec sa traduction, changé chaque jour. Liste rangée dans la passerelle (pas de source extérieure). À préciser : mot rare ou langue (laquelle), taille de la liste, origine des définitions (droits).
18. **Ce jour dans l'histoire** (demandé le 2026-11-06, idée « culture et apprentissage ») : un événement marquant à la date du jour. Source à trouver et à vérifier (API ouverte en français, ou liste rangée dans la passerelle).
19. **Photo de l'espace du jour** (demandé le 2026-11-06, idée « culture et apprentissage ») : la photo astronomique quotidienne de la NASA (APOD), affichée comme les photos Immich (même chaîne d'images). Clé gratuite à vérifier ; certains jours la publication est une vidéo (à gérer).
20. **Jeu de réflexion** (demandé le 2026-11-06) : une énigme ou un calcul mental par jour, la réponse affichée le lendemain. À préciser : type de jeu, source des énigmes (liste rangée dans la passerelle ou générée), niveau.
22. **Petit animal virtuel** : fait en **3.1.0, en essai sur `beta`** (Nimbus, Pousse, Volta, Bip ; voir `CHANGELOG.md` et `docs/maquettes/personnages-3.1.png`). À retirer du backlog après validation sur l'afficheur et publication.
