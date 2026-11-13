# Analyse du firmware afficheur — version 3.3.0

Date : 2026-11-11. Base : code 3.3.0 (commit 45208eb), cœur ESP8266 Arduino 3.1.2, TFT_eSPI 2.5.43, ArduinoJson 7.2.1.
Périmètre : firmware uniquement. ESP8266 seulement. La partie OTA n'est pas concernée : elle est signalée, jamais modifiée.

Légende : **V** = vérifié dans le code ou les sources du cœur ; **NV** = non vérifié sur l'appareil, à mesurer ; **S** = supposé.

## Suite donnée (2026-11-11)

- **P2** (`/meteo` redemandé par la page Heure) : traité en **3.4.0**, dans le cadre plus large de la copie gardée 10 minutes des données non dynamiques (`src/memo.cpp`).
- Les autres propositions sont au backlog (`docs/CONTEXTE.md`) :

| Proposition | Backlog |
|---|---|
| P1 | n° 31 |
| Mesures sans changement de comportement | n° 32 |
| P3 | n° 33 |
| P4 | n° 34 |
| P5 | n° 35 |
| P6 | n° 36 |
| P7 | n° 37 |
| P8 | n° 38 |
| Mot de passe facultatif du point d'accès | n° 39 |

## 1. Ce qui est déjà bien fait (à garder)

- Lecture JSON en flux : `useHTTP10(true)` + `getStream()` + filtre ArduinoJson. C'est la méthode recommandée par la documentation d'ArduinoJson pour HTTPClient. **V**
- `Stream::timedRead` appelle `yield()` : une lecture lente ne déclenche pas le chien de garde. **V** (Stream.cpp)
- Temporisation après échec (5, 15, 30, 60 s) : une fois qu'une requête a échoué, les suivantes du même tour répondent tout de suite (`gwGet`, net.cpp l.156). **V**
- `useFont` ne recharge pas une police déjà chargée (ui.cpp l.40). **V**
- `WiFi.persistent(false)` : pas d'écriture de la configuration Wi-Fi en flash à chaque `begin`. **V**
- Santé : relevé mémoire, redémarrage si le plus grand bloc reste sous 4 Ko pendant 60 s, mode secours après 3 plantages. **V**
- Mémoire mesurée par l'utilisateur en 3.3.0 : 23 Ko libres au minimum, plus grand bloc 21 Ko au minimum. Aucun signe de fragmentation qui s'aggrave.

## 2. Autocritique : constats précédents corrigés

- J'avais estimé « jusqu'à environ 20 s de blocage » si tous les sondages échouent dans le même tour. **Faux** : la temporisation coupe les appels suivants dès le premier échec. Le pire cas réel est environ 5 s pour l'échec (2 essais × 2,5 s), plus la somme des requêtes qui ont réussi avant.
- J'avais noté « 7 `malloc` à chaque changement de police » comme risque de fragmentation. Le coût n'existe que lors d'un vrai changement de police, et le plus grand bloc mesuré (21 Ko) ne montre pas de problème. Priorité basse.

## 3. Constats et propositions

### P1 — Sondages en série dans le même tour de `loop()` (moyenne)
- Constat **V** : main.cpp l.392-417. `fetchModules`, `syncTime`, `pollDiscord`, `pollSup`, `pollPlex` et `pollTesla` peuvent tous partir dans le même tour. Pendant ce temps, `server.handleClient()`, le DNS et la rotation des pages attendent.
- Effet **NV** : si la passerelle répond lentement sans échouer, les durées s'additionnent. Le relevé `diag` (durées par requête) permet de le chiffrer.
- Proposition : une seule requête de fond par tour, à tour de rôle (petit ordonnanceur). Le comportement ne change pas, mais l'interface web et l'écran restent réactifs.
- Coût : quelques dizaines d'octets de flash. Risque faible.

### P2 — Page Heure : `/meteo` redemandé à chaque redessin complet (moyenne) — traité en 3.4.0
- Constat **V** : pages.cpp l.294 (`drawHeure`) et l.364 (`drawMeteo`). `gWx` est déjà rempli par `retenirMeteo`.
- Proposition : réutiliser `gWx` s'il a moins de N minutes (par exemple la période de la passerelle), et ne redemander qu'au-delà.
- Gain : une requête HTTP et un `JsonDocument` de moins par passage sur la page Heure, et un affichage plus rapide.
- Point à trancher : la météo affichée peut avoir jusqu'à N minutes de retard (aujourd'hui, elle a au plus une rotation de retard).

### P3 — Connexions TCP fermées par `stop()` qui restent en TIME_WAIT (basse à moyenne, à mesurer)
- Constat **V** : un `WiFiClient` neuf par requête, fermé par `http.end()`. Dans lwIP, une connexion fermée reste quelques minutes en TIME_WAIT et garde sa mémoire (PR esp8266/Arduino #8738, issue #1923). lwIP2 du cœur 3.1.2 : `MEMP_NUM_TCP_PCB_TIME_WAIT` = 5 (lwipopts.h l.3696), ce qui limite déjà le coût.
- `WiFiClient::abort()` existe dans le cœur 3.1.2 et libère tout de suite (RST).
- Effet **NV** : les mesures actuelles (bloc minimum 21 Ko) ne montrent pas de manque de mémoire.
- Proposition : d'abord mesurer (mémoire libre juste avant et juste après une série de requêtes, dans `/debug.json`). N'appeler `client.abort()` après une lecture complète que si la mesure montre un gain réel.
- Risque **S** : la passerelle (aiohttp) recevrait un RST au lieu d'une fermeture normale. Sans conséquence en HTTP/1.0 une fois la réponse lue, mais cela peut ajouter des lignes de journal.

### P4 — Veille modem du Wi-Fi (à vérifier avant toute action)
- Constat **V** : le code n'appelle pas `setSleepMode`. D'après la documentation du cœur (doc/esp8266wifi/generic-class.rst l.123), en veille LIGHT ou MODEM, le temporisateur TCP passe de 250 ms à 3 s.
- **NV** : en mode `WIFI_AP_STA`, il est probable que le SDK n'entre pas en veille, puisque le point d'accès doit rester actif. Non confirmé dans une source officielle.
- Proposition : ajouter `WiFi.getSleepMode()` dans `/debug.json` pour lire la valeur réelle. Ne forcer `WIFI_NONE_SLEEP` que si la valeur n'est pas NONE. L'appareil est sur secteur, donc la consommation n'est pas un problème.

### P5 — `WiFi.begin` relancé toutes les 30 s alors que l'auto-reconnexion est active (basse)
- Constat **V** : net.cpp l.35 (`setAutoReconnect(true)`) et l.52 (`begin` si la connexion est perdue depuis plus de 30 s).
- **S** : un `begin` en pleine tentative du SDK peut relancer l'association depuis le début. Je n'ai pas de source qui le démontre.
- Proposition : garder le filet de sécurité, mais à 60 s, et seulement si `WiFi.status()` vaut `WL_NO_SSID_AVAIL`, `WL_CONNECT_FAILED` ou `WL_DISCONNECTED`. À n'appliquer que si des reconnexions lentes sont observées.

### P6 — `/restart` accessible en GET (basse)
- Constat **V** : web.cpp l.1039. Un navigateur qui précharge le lien, ou un simple rechargement de la page, redémarre l'afficheur.
- Proposition : passer en POST (bouton dans un formulaire). Cela ne touche pas à l'OTA.

### P7 — `esc()` construit sa chaîne sans réserver la place (basse)
- Constat **V** : web.cpp l.37-50. La chaîne grandit caractère par caractère, ce qui provoque plusieurs réallocations.
- Proposition : `out.reserve(strlen(in) + 8)`. Gain modeste, sur l'interface web uniquement.

### P8 — DNS captif (basse, à vérifier)
- Constat **V** : `dnsServer.start(53, "*", softAPIP)` (web.cpp l.1053).
- **NV** : il reste à vérifier si le serveur répond aussi aux requêtes reçues côté station (réseau de la maison). Le code ne le montre pas directement.
- Proposition : vérifier sur l'appareil avant toute modification.

### P9 — Flash : référence à SPIFFS par TFT_eSPI (déjà au backlog, n° 25)
- `Smooth_font.h` déclare `fontFS = SPIFFS`, ce qui ajoute environ 26 Ko de flash alors que les polices sont en PROGMEM.

## 4. Sécurité : signalement seulement

- Le point d'accès est ouvert (net.cpp l.20). On peut proposer un mot de passe facultatif, réglé dans la configuration, avec l'ouvert par défaut pour la première installation.
- La mise à jour OTA (`/update`) n'a pas de mot de passe. **Signalé seulement**. Conformément à la règle du projet, je ne propose aucune modification de l'OTA.

## 5. Ce que je ne recommande pas

- Changer de variante lwIP (n° 8 suspendu) : pas de besoin mesuré.
- Garder les connexions HTTP ouvertes (`setReuse(true)`) : le gain en temps est faible sur un réseau local, et cela complique la gestion des erreurs avec HTTP/1.0.
- Toute modification de l'OTA.

## 6. Ordre proposé

1. Mesures sans changer le comportement : mode de veille et mémoire avant/après les requêtes, dans `/debug.json`.
2. P1 (un sondage par tour) et P2 (cache météo de la page Heure).
3. P6 et P7 (petites corrections web).
4. P3, P4 et P5 seulement si les mesures les justifient.

Sources :
- PR esp8266/Arduino #8738 (WiFiClient::abort, TIME_WAIT) : https://github.com/esp8266/Arduino/pull/8738
- Issue esp8266/Arduino #1923 (mémoire rendue après TIME_WAIT) : https://github.com/esp8266/Arduino/issues/1923
- PR esp8266/Arduino #2767 (MEMP_NUM_TCP_PCB_TIME_WAIT) : https://github.com/esp8266/Arduino/pull/2767
- Documentation du cœur ESP8266, generic-class.rst (setSleepMode)
- ArduinoJson, « How to use ArduinoJson with HTTPClient »

## 7. Mesures sur l'afficheur (3.5.0, 2026-11-12, 1 h 48 de marche)

Relevé `/debug.json` du propriétaire. **V** = mesuré ; **S** = interprétation.

| Mesure | Valeur | Conclusion |
|---|---|---|
| `reseau.veille` | 2 (modem) | **V**. Ma supposition de P4 (« veille probablement inactive en AP + STA ») était **fausse**. Essai en 3.5.1 : veille coupée. |
| Tours de requêtes de fond | 2 270 tours, 2 308 requêtes, 39 ms en moyenne, pire 2 544 ms (2 requêtes) | **V**. Les requêtes ne s'accumulent presque jamais. Le pire tour correspond au délai dépassé sur `/plex` (même date). P1 / n° 31 : gain visible quasi nul aujourd'hui. |
| Pauses de l'interface web | pire 4 450 ms, 46 de plus de 1 s, 0 de plus de 5 s | **S** : la pire pause égale presque la pire image (4 438 ms) ; ce sont donc les photos qui bloquent l'interface, pas les sondages. |
| TIME_WAIT | 6 (max 6), 184 octets chacune | **V** : environ 1,1 Ko immobilisé. Sans plafond, on en attendrait une quarantaine (2 min chacune, une requête toutes les 2,8 s) ; il y a donc un plafond. Mécanisme non vérifié. P3 / n° 33 : petit gain. |
| Onglet Pages, au plus bas | 16 904 octets libres, plus grand bloc 15 048 octets (contre 23 240 et 21 080 en 3.3.0) | **S** : baisse due aux réponses gardées (4 205 octets, 11 copies) et aux TIME_WAIT. Corrigé en 3.5.1 : copies rendues avant la construction de l'onglet. |
| Réponses gardées | 188 servies depuis la mémoire, 99 demandées, 0 secours | **V** : environ 65 % de requêtes en moins sur ces pages. |
| Délais dépassés | 3 sur 3 019 requêtes, tous rattrapés au second essai ; 0 déconnexion | **V**. P5 / n° 35 : rien à corriger. |

