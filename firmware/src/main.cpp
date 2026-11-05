// =====================================================
// MODELROGS DISPLAY — GeekMagic SmallTV (ESP8266 + ST7789 240x240)
//
// - Point d'accès PERMANENT et ouvert (WIFI_AP_STA) + portail captif
// - Interface web 192.168.4.1 : Wi-Fi, pages, ville, nuit, luminosités,
//   choix des services supervisés
// - OTA /update inchangée (ESP8266HTTPUpdateServer)
// - Après l'installation d'un firmware différent : « Configuration à valider »
//   jusqu'au premier Enregistrer ; ensuite, démarrage direct sur les pages
// - Mode secours : après 3 plantages au démarrage, seuls le Wi-Fi,
//   l'interface web et l'OTA tournent (l'OTA reste toujours joignable)
// - Pages en rotation : Heure, Météo, Discord, Supervision, Crypto, Actions,
//   Indices, Devises, Commodities, Custom, Photos, Plex
//   (+ écran d'alerte prioritaire en cas de panne). Discord et Plex ne
//   s'affichent que s'il y a quelqu'un / une lecture, et passent devant
//   dès une arrivée / le début d'une lecture.
// - Panne : bloc « PANNE + récapitulatif Supervision » affiché aussitôt, puis
//   après chaque page tant que la panne dure. Sans panne, la Supervision
//   n'apparaît dans la rotation que si un service est instable ou si Kuma
//   ne répond pas.
// - Toutes les données viennent de la passerelle Docker (HTTP local)
// =====================================================

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <sigma_delta.h>

#include "app.h"
#include "config.h"
#include "diag.h"
#include "net.h"
#include "pages.h"
#include "sante.h"
#include "ui.h"
#include "web.h"

#define PIN_BACKLIGHT 5   // rétroéclairage actif à l'état BAS

// Périodes de rafraîchissement
static const uint32_t TIME_SYNC_MS    = 10UL * 60 * 1000;
static const uint32_t TIME_RETRY_MS   = 30UL * 1000;
static const uint32_t DISCORD_POLL_MS = 5UL * 1000;
static const uint32_t SUP_POLL_MS     = 30UL * 1000;
static const uint32_t PLEX_POLL_MS    = 5UL * 1000;
// Tesla : la passerelle répond de sa mémoire (MQTT) ; 15 s suffisent pour
// montrer la page au début d'une charge et suivre la durée restante.
static const uint32_t TESLA_POLL_MS   = 15UL * 1000;
// Discord et Plex sans rien à montrer (personne en salon, aucune lecture) :
// interrogés 3 fois moins souvent (3.0.0). Une arrivée ou un début de lecture
// est vu au plus 15 s plus tard ; ensuite, le rythme de 5 s reprend.
static const uint32_t REPOS_POLL_MS   = 15UL * 1000;
static const uint32_t MODULES_MS      = 10UL * 60 * 1000;

enum ScreenState : uint8_t { S_BOOT, S_VALIDATE, S_SETUP, S_CONNECTING, S_NO_GATEWAY, S_PAGES };

static ScreenState screen = S_BOOT;
static int8_t   curPage = -1;          // -1 : aucune page affichée
static uint32_t pageSince = 0;
static bool     alertShown = false;    // l'écran d'alerte est affiché en ce moment
static bool     blocSup = false;       // récap Supervision affiché après l'écran PANNE
static bool     alertActive = false;   // une panne est en cours
static bool     idleShown = false;
static int8_t   forcedPage = -1;
static bool     priorityDiscord = false;
static bool     priorityPlex = false;
static bool     priorityTesla = false;   // une charge vient de commencer
// Écran de nuit (3.0.0) : affiché pendant la plage nocturne ; minute dessinée
// (-1 : heure inconnue, « --:-- » ; -2 : à redessiner) ; alerte de supervision
// affichée à la place.
static bool     enNuit = false;
static int      nuitMinute = -2;
static bool     nuitAlerte = false;
static bool     nuitRecap = false;      // bloc de panne la nuit : récap Supervision affiché (sinon PANNE)
static bool     configChanged = false;
static bool     screenOff = false;
static uint8_t  lastBrightness = 255;
static uint32_t drawnDiscVersion = 0, drawnSupVersion = 0;
static uint32_t drawnPlexVersion = 0, drawnPlexProgres = 0;
static uint32_t drawnTeslaVersion = 0, drawnTeslaInfos = 0;

// Mode secours, redémarrage préventif et mesures mémoire : voir sante.cpp

// ------------------------------------------------------------ rétroéclairage

// Générateur sigma-delta matériel de l'ESP8266 (sigma_delta.h, 3.0.0) au lieu
// de la PWM logicielle d'analogWrite : aucune interruption, aucun code en IRAM.
// Essayé en 2.12.1-sd1. Actif à l'état BAS : 0 % = broche HAUTE en continu,
// 100 % = BASSE en continu (générateur détaché, simple écriture de broche).
static void applyBacklight(uint8_t percent) {
  if (percent == lastBrightness) return;
  lastBrightness = percent;
  if (percent == 0 || percent >= 100) {
    sigmaDeltaDetachPin(PIN_BACKLIGHT);
    digitalWrite(PIN_BACKLIGHT, percent == 0 ? HIGH : LOW);
    return;
  }
  // Rapport cyclique = part du temps à l'état HAUT : 255 - percent * 255 / 100.
  sigmaDeltaWrite(0, (uint8_t)(255 - (uint16_t)percent * 255 / 100));
  if (!sigmaDeltaIsPinAttached(PIN_BACKLIGHT)) sigmaDeltaAttachPin(PIN_BACKLIGHT);
}

static bool nightActive() {
  if (!cfg.nightOn) return false;
  int m = localMinuteOfDay();
  if (m < 0) return false;
  if (cfg.nightStart <= cfg.nightEnd) return m >= cfg.nightStart && m < cfg.nightEnd;
  return m >= cfg.nightStart || m < cfg.nightEnd;   // plage à cheval sur minuit
}

// ---------------------------------------------------------------- rotation

static bool pageAvailable(uint8_t p) {
  if (!pageEnabled(p) || !moduleUsable(p)) return false;   // désactivée ici ou sur la passerelle
  if (p == PG_DISCORD) return gDisc.ok && gDisc.n > 0;     // masquée si personne
  if (p == PG_PLEX) return gPlex.ok && gPlex.n > 0;        // masquée sans lecture
  if (p == PG_TESLA) return gTesla.ok;                     // masquée tant que rien n'est reçu
  // Supervision : pendant une panne, elle passe dans le bloc « PANNE +
  // Supervision » (hors rotation). Sans panne, elle n'est dans la rotation que
  // si un service est instable ou si Kuma / la passerelle ne répond pas.
  if (p == PG_SUP) return !gSup.alert && (!gSup.ok || gSup.in > 0);
  return true;
}

// Rang d'une page dans l'ordre d'affichage (ROTATION)
static uint8_t rang(uint8_t p) {
  for (uint8_t i = 0; i < PG_COUNT; i++) if (ROTATION[i] == p) return i;
  return 0;
}

// Position de la page p dans la rotation (points en bas de l'écran)
static void positionPage(uint8_t p, uint8_t& idx, uint8_t& cnt) {
  idx = cnt = 0;
  for (uint8_t i = 0; i < PG_COUNT; i++) {
    const uint8_t q = ROTATION[i];
    if (pageAvailable(q) || q == p) {
      if (q == p) idx = cnt;
      cnt++;
    }
  }
}

// Durée d'affichage d'une page : la durée commune, sauf la page Photos, qui a
// la sienne (réglage « duree » de la page, 30 s par défaut).
static uint32_t dureeDe(int p) {
  if (p == PG_PHOTOS) {
    long s = modParam(PG_PHOTOS, "duree").toInt();
    return (uint32_t)(s >= 5 && s <= 600 ? s : 30) * 1000UL;
  }
  return (uint32_t)cfg.pageSec * 1000UL;
}

static void showPage(uint8_t p, bool restartTimer = true) {
  curPage = p;
  if (restartTimer) pageSince = millis();
  idleShown = false;

  uint8_t idx, cnt;
  positionPage(p, idx, cnt);

  switch (p) {
    case PG_HEURE:   drawHeure(idx, cnt); break;
    case PG_METEO:   drawMeteo(idx, cnt); break;
    case PG_DISCORD: drawDiscord(idx, cnt); drawnDiscVersion = gDisc.version; break;
    case PG_SUP:     drawSupervision(idx, cnt); drawnSupVersion = gSup.version; break;
    case PG_BTC:
    case PG_ACTIONS:
    case PG_DEVISES:
    case PG_COMMO:
    case PG_INDICES:
    case PG_CUSTOM:  drawCours(p, idx, cnt); break;
    case PG_PHOTOS:  drawPhotos(idx, cnt); break;
    case PG_TESLA:
      drawTesla();
      drawnTeslaVersion = gTesla.version;
      drawnTeslaInfos = gTesla.infos;
      break;
    case PG_PLEX:
      drawPlex(idx, cnt);
      drawnPlexVersion = gPlex.version;
      drawnPlexProgres = gPlex.progres;
      break;
  }
}

static void nextPage() {
  const int depart = curPage >= 0 ? rang(curPage) : -1;
  for (uint8_t k = 1; k <= PG_COUNT; k++) {
    uint8_t p = ROTATION[(depart + k + PG_COUNT) % PG_COUNT];
    if (pageAvailable(p)) {
      showPage(p);
      return;
    }
  }
  // Rien à afficher (ex. Discord seul et salon vide)
  curPage = -1;
  pageSince = millis();
  if (!idleShown) {
    drawSplash("Aucune page à afficher", "Configuration : http://" + WiFi.localIP().toString(),
               "ou Wi-Fi \"" + String(cfg.apSsid) + "\"");
    idleShown = true;
  }
}

// Écran PANNE : premier écran du bloc (une durée de page), suivi du récap Supervision.
// curPage garde la dernière page normale, pour reprendre la rotation ensuite.
static void showAlert() {
  drawAlert();
  alertShown = true;
  blocSup = false;
  drawnSupVersion = gSup.version;
  pageSince = millis();
}

void schedulerForce(int8_t page) { forcedPage = page; }

void schedulerConfigChanged(bool villeChanged) {
  configChanged = true;
  if (villeChanged) {                      // resynchroniser l'heure de la nouvelle ville
    gTime.valid = false;
    gTime.lastTry = 0;
  }
  gSup.lastPoll = 0;
  gDisc.lastPoll = 0;
  gPlex.lastPoll = 0;
  gMods.lastFetch = 0;
  lastBrightness = 255;
}

// --------------------------------------------------------- écrans d'état

static void setScreen(ScreenState s) {
  if (s == screen) return;
  screen = s;
  curPage = -1;
  alertShown = blocSup = false;
  switch (s) {
    case S_VALIDATE:
      drawSplash("Configuration à valider", "1. Wi-Fi \"" + String(cfg.apSsid) + "\"",
                 "2. 192.168.4.1 > Enregistrer");
      break;
    case S_SETUP:
      drawSplash("Configuration requise", "1. Wi-Fi \"" + String(cfg.apSsid) + "\"",
                 "2. Ouvrir http://192.168.4.1");
      break;
    case S_CONNECTING:
      drawSplash("Connexion au Wi-Fi...", cfg.wifiSsid,
                 "Configuration : Wi-Fi \"" + String(cfg.apSsid) + "\"");
      break;
    case S_NO_GATEWAY:
      drawSplash("Passerelle à configurer", "http://" + WiFi.localIP().toString(),
                 "ou Wi-Fi \"" + String(cfg.apSsid) + "\" > 192.168.4.1");
      break;
    default:
      break;
  }
}

// -------------------------------------------------------------------- setup

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n=== MODELROGS DISPLAY v" FW_VERSION " ==="));

  // Écran éteint pendant l'initialisation (pas de flash la nuit) : simple
  // écriture de broche, rien d'autre avant la lecture du compteur de plantages.
  // pinMode(OUTPUT) rend aussi la broche au GPIO si le sigma-delta y était
  // encore relié avant un redémarrage logiciel.
  pinMode(PIN_BACKLIGHT, OUTPUT);
  digitalWrite(PIN_BACKLIGHT, HIGH);

  // Compteur de plantages lu AVANT tout le reste : un plantage dans la suite
  // de setup() (rétroéclairage compris) mène bien au mode secours.
  gSafeMode = santeDemarrage();

  loadConfig();
  diagBegin();            // diagnostic : compteurs et événements Wi-Fi (voir diag.h)

  if (gSafeMode) {
    // Le strict minimum, OTA en priorité : pas de polices, pas de passerelle.
    Serial.println(F("MODE SECOURS : Wi-Fi + interface web + OTA uniquement"));
    netBegin();
    webBegin();
    uiBegin();
    tft.fillScreen(Col::ALERTE2);   // écran rouge sombre uni = mode secours
    // Pleine luminosité par simple écriture de broche : le mode secours ne
    // dépend pas du code de luminosité (sigma-delta).
    digitalWrite(PIN_BACKLIGHT, LOW);
    return;
  }

  sigmaDeltaSetup(0, 1220);   // fréquence de base la plus basse du générateur

  uiBegin();
  // Après un redémarrage planifié de nuit, on reste à la luminosité de nuit.
  applyBacklight(santeNuitAuDemarrage() && cfg.nightOn ? cfg.brightNight : cfg.brightDay);
  drawSplash("Démarrage...", "Point d'accès \"" + String(cfg.apSsid) + "\"", "http://192.168.4.1");

  netBegin();
  webBegin();
}

// --------------------------------------------------------------------- loop

void loop() {
  webTick();      // DNS captif + interface web + OTA, à chaque tour
  netTick();

  if (gSafeMode) return;   // mode secours : rien d'autre ne tourne

  netSonde();     // mode diagnostic seulement : sonde réseau toutes les 30 s

  // --- Luminosité / nuit (appliquée dans tous les états). Après un
  //     redémarrage de nuit, la luminosité de nuit est gardée jusqu'à
  //     ce que l'heure soit connue.
  bool nuit = nightActive() || (!timeValid() && santeNuitAuDemarrage() && cfg.nightOn);
  // Panne de supervision la nuit : alerte affichée à la luminosité de jour
  // (plus visible), jusqu'au retour à la normale (3.0.0).
  const bool alerteNuit = nuit && pageEnabled(PG_SUP) && moduleUsable(PG_SUP) && gSup.alert;
  uint8_t brightness = nuit && !alerteNuit ? cfg.brightNight : cfg.brightDay;

  // --- Redémarrage préventif (2 h 15), filet de sécurité mémoire, mesures
  santeTick(timeValid(), localMinuteOfDay(), nuit);

  applyBacklight(brightness);

  // --- États sans pages
  // Nouveau firmware : rien ne démarre avant validation dans l'interface web.
  if (!cfg.validated) { setScreen(S_VALIDATE); return; }
  if (cfg.wifiSsid[0] == '\0') { setScreen(S_SETUP); return; }
  if (!staConnected()) {
    if (screen != S_PAGES || millis() - gwLastOkMs > 60000UL) setScreen(S_CONNECTING);
    return;
  }
  if (cfg.gateway[0] == '\0') { setScreen(S_NO_GATEWAY); return; }
  if (screen != S_PAGES) {
    screen = S_PAGES;
    curPage = -1;
    configChanged = true;
  }

  uint32_t now = millis();

  // --- Données de fond
  if (gMods.lastFetch == 0 || now - gMods.lastFetch > (gMods.known ? MODULES_MS : TIME_RETRY_MS)) {
    fetchModules();
  }
  if (!gTime.valid ? now - gTime.lastTry > TIME_RETRY_MS || gTime.lastTry == 0
                   : now - gTime.atMs > TIME_SYNC_MS) {
    syncTime();
  }
  // La nuit : seules l'heure et la supervision sont demandées (écran de nuit).
  if (!nuit && pageEnabled(PG_DISCORD) && moduleUsable(PG_DISCORD) &&
      (gDisc.lastPoll == 0 ||
       now - gDisc.lastPoll > (gDisc.ok && gDisc.n > 0 ? DISCORD_POLL_MS : REPOS_POLL_MS))) {
    if (pollDiscord()) priorityDiscord = true;
  }
  if (pageEnabled(PG_SUP) && moduleUsable(PG_SUP) &&
      (gSup.lastPoll == 0 || now - gSup.lastPoll > SUP_POLL_MS)) {
    pollSup();
  }
  if (!nuit && pageEnabled(PG_PLEX) && moduleUsable(PG_PLEX) &&
      (gPlex.lastPoll == 0 ||
       now - gPlex.lastPoll > (gPlex.ok && gPlex.n > 0 ? PLEX_POLL_MS : REPOS_POLL_MS))) {
    if (pollPlex()) priorityPlex = true;
  }
  if (!nuit && pageEnabled(PG_TESLA) && moduleUsable(PG_TESLA) &&
      (gTesla.lastPoll == 0 || now - gTesla.lastPoll > TESLA_POLL_MS)) {
    if (pollTesla()) priorityTesla = true;
  }
  bool supAlert = pageEnabled(PG_SUP) && moduleUsable(PG_SUP) && gSup.alert;

  // --- Écran éteint la nuit (luminosité de nuit à 0)
  bool off = brightness == 0;
  if (off != screenOff) {
    screenOff = off;
    if (!off) configChanged = true;   // au réveil : redessiner
  }
  if (screenOff) return;              // écran éteint : on ne dessine rien

  // --- Écran de nuit (3.0.0) : l'heure seule ; aucune autre page (rotation,
  //     priorités Plex, Discord, Tesla, pages demandées depuis l'interface).
  //     Une panne de supervision affiche à la place le même bloc que le jour
  //     (écran PANNE puis récap Supervision, une durée de page chacun, 3.0.1),
  //     à la luminosité de jour, en boucle jusqu'au retour à la normale.
  if (nuit) {
    if (!enNuit || configChanged) {   // entrée dans la nuit, retour d'un écran d'état, réglages
      enNuit = true;
      nuitMinute = -2;
      nuitAlerte = false;
    }
    configChanged = false;
    forcedPage = -1;
    priorityDiscord = priorityPlex = priorityTesla = false;
    alertShown = blocSup = alertActive = false;
    if (supAlert) {
      bool dessiner = false;
      if (!nuitAlerte) {                       // début de la panne : écran PANNE
        nuitAlerte = true;
        nuitRecap = false;
        pageSince = millis();
        dessiner = true;
      } else if (millis() - pageSince >= (uint32_t)cfg.pageSec * 1000UL) {   // PANNE <-> récap
        nuitRecap = !nuitRecap;
        pageSince = millis();
        dessiner = true;
      }
      if (dessiner || drawnSupVersion != gSup.version) {   // mise à jour sans relancer le minuteur
        if (nuitRecap) drawSupervision(0, 0);
        else drawAlert();
        drawnSupVersion = gSup.version;
      }
      return;
    }
    const int m = localMinuteOfDay();
    if (nuitAlerte || m != nuitMinute) {
      drawNuit();
      nuitMinute = m;
      nuitAlerte = false;
    }
    return;
  }
  if (enNuit) {                       // fin de la plage nocturne : reprise de la rotation
    enNuit = false;
    configChanged = true;
  }

  // --- Après une modification de configuration
  if (configChanged) {
    configChanged = false;
    alertShown = blocSup = false;
    alertActive = false;               // une panne en cours sera réaffichée aussitôt
    if (forcedPage < 0) {
      // reprendre la rotation sur la même page : on part de la page précédente
      curPage = curPage >= 0 ? ROTATION[(rang(curPage) + PG_COUNT - 1) % PG_COUNT] : -1;
      nextPage();
    }
  }

  const uint32_t dureePage = (uint32_t)cfg.pageSec * 1000UL;

  // --- Panne : le bloc « PANNE + Supervision » s'affiche immédiatement, puis
  //     après chaque page jusqu'au retour à la normale.
  if (supAlert && !alertActive) {
    alertActive = true;
    showAlert();
    return;
  }
  if (!supAlert && alertActive) {
    alertActive = false;
    if (alertShown || blocSup) {
      alertShown = blocSup = false;
      nextPage();
      return;
    }
  }

  // --- Demandes ponctuelles (prioritaires, même pendant une alerte)
  if (forcedPage >= 0) {
    uint8_t p = forcedPage;
    forcedPage = -1;
    alertShown = blocSup = false;
    showPage(p);
    return;
  }
  if (priorityDiscord) {
    priorityDiscord = false;
    if (pageAvailable(PG_DISCORD)) {
      alertShown = blocSup = false;
      showPage(PG_DISCORD);
      return;
    }
  }
  if (priorityPlex) {                      // une lecture vient de commencer
    priorityPlex = false;
    if (pageAvailable(PG_PLEX)) {
      alertShown = blocSup = false;
      showPage(PG_PLEX);
      return;
    }
  }

  if (priorityTesla) {                     // une charge vient de commencer
    priorityTesla = false;
    if (pageAvailable(PG_TESLA)) {
      alertShown = blocSup = false;
      showPage(PG_TESLA);
      return;
    }
  }

  // --- Écran d'alerte affiché : mise à jour, puis page suivante après une durée
  if (alertShown) {
    if (drawnSupVersion != gSup.version) {   // redessiné sans relancer le minuteur
      drawAlert();
      drawnSupVersion = gSup.version;
    }
    if (now - pageSince >= dureePage) {      // fin de l'écran PANNE : récap Supervision
      alertShown = false;
      blocSup = true;
      drawSupervision(0, 0);
      drawnSupVersion = gSup.version;
      pageSince = millis();
    }
    return;
  }

  // --- Récap Supervision du bloc : mise à jour, puis page suivante
  if (blocSup) {
    if (drawnSupVersion != gSup.version) {
      drawSupervision(0, 0);
      drawnSupVersion = gSup.version;
    }
    if (now - pageSince >= dureePage) {
      blocSup = false;
      nextPage();
    }
    return;
  }

  // --- Rotation (pendant une panne, le bloc s'intercale après chaque page)
  if (curPage < 0) {
    if (supAlert && now - pageSince > 5000UL) { showAlert(); return; }
    if (now - pageSince > 5000UL || !idleShown) nextPage();
    return;
  }
  if (now - pageSince >= dureeDe(curPage)) {
    // Pages de cours : pages dédiées (+/- 5 % sur 7 jours) avant de passer à la suite
    if (pageCours(curPage)) {
      uint8_t idx, cnt;
      positionPage(curPage, idx, cnt);
      if (coursEcranSuivant(curPage, idx, cnt)) {
        pageSince = millis();
        return;
      }
    }
    if (supAlert) showAlert();
    else nextPage();
    return;
  }

  // --- Mises à jour de la page affichée
  switch (curPage) {
    case PG_HEURE:
      if (clockTick()) showPage(PG_HEURE, false);
      break;
    case PG_DISCORD:
      if (gDisc.version != drawnDiscVersion) {
        if (pageAvailable(PG_DISCORD)) showPage(PG_DISCORD, false);
        else nextPage();          // tout le monde est parti
      }
      break;
    case PG_SUP:
      if (gSup.version != drawnSupVersion) {
        if (pageAvailable(PG_SUP)) showPage(PG_SUP, false);
        else nextPage();          // tout est revenu à la normale
      }
      break;
    case PG_TESLA:
      if (gTesla.version != drawnTeslaVersion) {
        if (pageAvailable(PG_TESLA)) showPage(PG_TESLA, false);
        else nextPage();          // plus de données
      } else if (gTesla.infos != drawnTeslaInfos) {
        teslaBandeau();           // pourcentage ou durée : photo non retéléchargée
        drawnTeslaInfos = gTesla.infos;
      }
      break;
    case PG_PLEX:
      if (gPlex.version != drawnPlexVersion) {
        if (pageAvailable(PG_PLEX)) showPage(PG_PLEX, false);
        else nextPage();          // plus aucune lecture
      } else if (gPlex.progres != drawnPlexProgres) {
        plexProgress();           // seule la progression a bougé : pas d'affiche retéléchargée
        drawnPlexProgres = gPlex.progres;
      }
      break;
  }
}
