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
// - Pages en rotation : Heure, Météo, Discord, Supervision, Bitcoin
//   (+ écran d'alerte prioritaire en cas de panne)
// - Toutes les données viennent de la passerelle Docker (HTTP local)
// =====================================================

#include <Arduino.h>
#include <ESP8266WiFi.h>

#include "app.h"
#include "config.h"
#include "net.h"
#include "pages.h"
#include "ui.h"
#include "web.h"

#define PIN_BACKLIGHT 5   // rétroéclairage actif à l'état BAS

// Périodes de rafraîchissement
static const uint32_t TIME_SYNC_MS    = 10UL * 60 * 1000;
static const uint32_t TIME_RETRY_MS   = 30UL * 1000;
static const uint32_t DISCORD_POLL_MS = 5UL * 1000;
static const uint32_t SUP_POLL_MS     = 30UL * 1000;
static const uint32_t MODULES_MS      = 10UL * 60 * 1000;

enum ScreenState : uint8_t { S_BOOT, S_VALIDATE, S_SETUP, S_CONNECTING, S_NO_GATEWAY, S_PAGES };

static ScreenState screen = S_BOOT;
static int8_t   curPage = -1;          // -1 : aucune page affichée
static uint32_t pageSince = 0;
static bool     alertShown = false;    // l'écran d'alerte est affiché en ce moment
static bool     alertActive = false;   // une panne est en cours
static bool     idleShown = false;
static int8_t   forcedPage = -1;
static bool     priorityDiscord = false;
static bool     configChanged = false;
static bool     screenOff = false;
static uint8_t  lastBrightness = 255;
static uint32_t drawnDiscVersion = 0, drawnSupVersion = 0;

// ------------------------------------------------------------- mode secours
//
// Un compteur en mémoire RTC (conservée lors d'un redémarrage, effacée à la
// coupure de courant) compte les plantages successifs survenus dans la
// première minute. À 3, l'afficheur démarre en mode secours : ni pages ni
// appels à la passerelle, seulement le Wi-Fi, l'interface web et l'OTA.
// Les 128 premiers octets de la mémoire RTC sont réservés à l'OTA :
// on utilise le bloc 64 (octet 256).

bool gSafeMode = false;

static const uint32_t RTC_BLOCK   = 64;
static const uint32_t RTC_MAGIC   = 0x5AFE0D15UL;
static const uint8_t  SAFE_AFTER  = 3;      // plantages successifs
static const uint32_t STABLE_MS   = 60000;  // démarrage réussi après 1 minute

struct BootGuard {
  uint32_t magic;
  uint32_t crashes;
};

static void writeGuard(uint32_t crashes) {
  BootGuard g{ RTC_MAGIC, crashes };
  ESP.rtcUserMemoryWrite(RTC_BLOCK, (uint32_t*)&g, sizeof(g));
}

static uint32_t updateGuardAtBoot() {
  BootGuard g;
  ESP.rtcUserMemoryRead(RTC_BLOCK, (uint32_t*)&g, sizeof(g));
  uint32_t reason = ESP.getResetInfoPtr()->reason;
  bool crashed = reason == REASON_WDT_RST || reason == REASON_EXCEPTION_RST ||
                 reason == REASON_SOFT_WDT_RST;
  uint32_t crashes = (g.magic == RTC_MAGIC && crashed) ? g.crashes + 1 : 0;
  writeGuard(crashes);
  Serial.printf("Demarrage : raison %u, plantages successifs %u\n", reason, crashes);
  return crashes;
}

void clearBootGuardAndRestart() {
  writeGuard(0);
  delay(100);
  ESP.restart();
}

// ------------------------------------------------------------ rétroéclairage

static void applyBacklight(uint8_t percent) {
  if (percent == lastBrightness) return;
  lastBrightness = percent;
  analogWrite(PIN_BACKLIGHT, 1023 - (int)percent * 1023 / 100);
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
  if (p == PG_SUP) return supConfigured();
  return true;
}

static void showPage(uint8_t p, bool restartTimer = true) {
  curPage = p;
  if (restartTimer) pageSince = millis();
  idleShown = false;

  uint8_t idx = 0, cnt = 0;
  for (uint8_t i = 0; i < PG_COUNT; i++) {
    if (pageAvailable(i) || i == p) {
      if (i == p) idx = cnt;
      cnt++;
    }
  }

  switch (p) {
    case PG_HEURE:   drawHeure(idx, cnt); break;
    case PG_METEO:   drawMeteo(idx, cnt); break;
    case PG_DISCORD: drawDiscord(idx, cnt); drawnDiscVersion = gDisc.version; break;
    case PG_SUP:     drawSupervision(idx, cnt); drawnSupVersion = gSup.version; break;
    case PG_BTC:     drawBtc(idx, cnt); break;
  }
}

static void nextPage() {
  for (uint8_t k = 1; k <= PG_COUNT; k++) {
    uint8_t p = (curPage + k + PG_COUNT) % PG_COUNT;
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

// Écran d'alerte : affiché une durée de page, en alternance avec la rotation.
// curPage garde la dernière page normale, pour reprendre la rotation ensuite.
static void showAlert() {
  drawAlert();
  alertShown = true;
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
  gMods.lastFetch = 0;
  lastBrightness = 255;
}

// --------------------------------------------------------- écrans d'état

static void setScreen(ScreenState s) {
  if (s == screen) return;
  screen = s;
  curPage = -1;
  alertShown = false;
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

  pinMode(PIN_BACKLIGHT, OUTPUT);
  analogWriteRange(1023);
  analogWriteFreq(1000);

  gSafeMode = updateGuardAtBoot() >= SAFE_AFTER;

  loadConfig();

  if (gSafeMode) {
    // Le strict minimum, OTA en priorité : pas de polices, pas de passerelle.
    Serial.println(F("MODE SECOURS : Wi-Fi + interface web + OTA uniquement"));
    netBegin();
    webBegin();
    uiBegin();
    tft.fillScreen(Col::ALERTE2);   // écran rouge sombre uni = mode secours
    applyBacklight(30);
    return;
  }

  uiBegin();
  applyBacklight(cfg.brightDay);
  drawSplash("Démarrage...", "Point d'accès \"" + String(cfg.apSsid) + "\"", "http://192.168.4.1");

  netBegin();
  webBegin();
}

// --------------------------------------------------------------------- loop

void loop() {
  webTick();      // DNS captif + interface web + OTA, à chaque tour
  netTick();

  if (gSafeMode) return;   // mode secours : rien d'autre ne tourne

  static bool stable = false;
  if (!stable && millis() > STABLE_MS) {
    stable = true;
    writeGuard(0);         // démarrage réussi : compteur de plantages remis à zéro
  }

  // --- Luminosité / nuit (appliquée dans tous les états)
  uint8_t brightness = nightActive() ? cfg.brightNight : cfg.brightDay;
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
  if (pageEnabled(PG_DISCORD) && moduleUsable(PG_DISCORD) &&
      (gDisc.lastPoll == 0 || now - gDisc.lastPoll > DISCORD_POLL_MS)) {
    if (pollDiscord()) priorityDiscord = true;
  }
  if (pageEnabled(PG_SUP) && moduleUsable(PG_SUP) &&
      (gSup.lastPoll == 0 || now - gSup.lastPoll > SUP_POLL_MS)) {
    pollSup();
  }
  bool supAlert = pageEnabled(PG_SUP) && moduleUsable(PG_SUP) && supConfigured() && gSup.alert;

  // --- Écran éteint la nuit (luminosité de nuit à 0)
  bool off = brightness == 0;
  if (off != screenOff) {
    screenOff = off;
    if (!off) configChanged = true;   // au réveil : redessiner
  }
  if (screenOff) return;              // écran éteint : on ne dessine rien

  // --- Après une modification de configuration
  if (configChanged) {
    configChanged = false;
    alertShown = false;
    alertActive = false;               // une panne en cours sera réaffichée aussitôt
    if (forcedPage < 0) {
      curPage = (curPage >= 0 ? curPage - 1 : -1);   // reprendre la rotation
      nextPage();
    }
  }

  const uint32_t dureePage = (uint32_t)cfg.pageSec * 1000UL;

  // --- Alerte supervision : une nouvelle panne s'affiche immédiatement, puis
  //     l'écran d'alerte alterne avec les pages (1 écran sur 2) jusqu'au retour
  //     à la normale.
  if (supAlert && !alertActive) {
    alertActive = true;
    showAlert();
    return;
  }
  if (!supAlert && alertActive) {
    alertActive = false;
    if (alertShown) {
      alertShown = false;
      nextPage();
      return;
    }
  }

  // --- Demandes ponctuelles (prioritaires, même pendant une alerte)
  if (forcedPage >= 0) {
    uint8_t p = forcedPage;
    forcedPage = -1;
    alertShown = false;
    showPage(p);
    return;
  }
  if (priorityDiscord) {
    priorityDiscord = false;
    if (pageAvailable(PG_DISCORD)) {
      alertShown = false;
      showPage(PG_DISCORD);
      return;
    }
  }

  // --- Écran d'alerte affiché : mise à jour, puis page suivante après une durée
  if (alertShown) {
    if (drawnSupVersion != gSup.version) {   // redessiné sans relancer le minuteur
      drawAlert();
      drawnSupVersion = gSup.version;
    }
    if (now - pageSince >= dureePage) {
      alertShown = false;
      nextPage();
    }
    return;
  }

  // --- Rotation (une alerte en cours s'intercale entre chaque page)
  if (curPage < 0) {
    if (supAlert && now - pageSince > 5000UL) { showAlert(); return; }
    if (now - pageSince > 5000UL || !idleShown) nextPage();
    return;
  }
  if (now - pageSince >= dureePage) {
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
      if (gSup.version != drawnSupVersion) showPage(PG_SUP, false);
      break;
  }
}
