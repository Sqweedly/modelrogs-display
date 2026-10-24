#include "sante.h"

#include <ESP8266WiFi.h>

bool gSafeMode = false;

// Mémoire RTC : conservée lors d'un redémarrage logiciel, perdue à la
// coupure de courant. Les 128 premiers octets sont réservés à l'OTA : on
// utilise le bloc 64 (octet 256).
static const uint32_t RTC_BLOCK  = 64;
static const uint32_t RTC_MAGIC  = 0x5AFE0D16UL;
static const uint8_t  SAFE_AFTER = 3;              // plantages successifs -> mode secours
static const uint32_t STABLE_MS  = 60000UL;        // démarrage réussi après 1 minute

static const int      MINUTE_REDEMARRAGE = 2 * 60 + 15;     // 2 h 15
static const uint32_t DUREE_MINI_MS      = 3600000UL;       // 1 h : un seul redémarrage par nuit
static const uint32_t SANS_HEURE_MS      = 25UL * 3600000UL;
static const uint32_t BLOC_CRITIQUE      = 4096;            // octets
static const uint32_t CRITIQUE_MS        = 60000UL;

struct BootGuard {
  uint32_t magic;
  uint32_t crashes;      // plantages successifs
  uint8_t  cause;        // CauseRedemarrage du redémarrage volontaire en cours
  uint8_t  nuit;         // redémarrage pendant la plage nocturne
  uint16_t planifies;    // redémarrages planifiés depuis la mise sous tension
};

static BootGuard guard;
static char      causeTexte[64] = "";
static bool      nuitAuDemarrage = false;
static uint32_t  minTas = UINT32_MAX, minBloc = UINT32_MAX;

static void ecrireGuard() {
  guard.magic = RTC_MAGIC;
  ESP.rtcUserMemoryWrite(RTC_BLOCK, (uint32_t*)&guard, sizeof(guard));
}

bool santeDemarrage() {
  BootGuard lu;
  ESP.rtcUserMemoryRead(RTC_BLOCK, (uint32_t*)&lu, sizeof(lu));
  bool valide = lu.magic == RTC_MAGIC;
  uint32_t reason = ESP.getResetInfoPtr()->reason;
  bool plantage = reason == REASON_WDT_RST || reason == REASON_EXCEPTION_RST ||
                  reason == REASON_SOFT_WDT_RST;

  guard.crashes   = (valide && plantage) ? lu.crashes + 1 : 0;
  guard.planifies = valide ? lu.planifies : 0;
  uint8_t cause   = (valide && reason == REASON_SOFT_RESTART) ? lu.cause : (uint8_t)CAUSE_AUCUNE;
  nuitAuDemarrage = valide && reason == REASON_SOFT_RESTART && lu.nuit;
  guard.cause = CAUSE_AUCUNE;
  guard.nuit  = 0;
  ecrireGuard();

  const char* t;
  switch (reason) {
    case REASON_DEFAULT_RST:   t = "mise sous tension"; break;
    case REASON_EXT_SYS_RST:   t = "réinitialisation externe"; break;
    case REASON_DEEP_SLEEP_AWAKE: t = "réveil"; break;
    case REASON_SOFT_RESTART:
      switch (cause) {
        case CAUSE_PLANIFIE:   t = "redémarrage planifié (2 h 15)"; break;
        case CAUSE_SANS_HEURE: t = "redémarrage planifié (25 h sans heure)"; break;
        case CAUSE_URGENCE:    t = "redémarrage d'urgence (mémoire)"; break;
        case CAUSE_INTERFACE:  t = "redémarrage depuis l'interface"; break;
        default:               t = "mise à jour ou redémarrage logiciel"; break;
      }
      break;
    default: t = "plantage"; break;
  }
  strncpy(causeTexte, t, sizeof(causeTexte) - 1);
  Serial.printf("Demarrage : raison %u, cause %u, plantages successifs %u\n",
                reason, cause, guard.crashes);
  return guard.crashes >= SAFE_AFTER;
}

void santeRedemarrer(CauseRedemarrage cause, bool nuit) {
  guard.crashes = 0;
  guard.cause = cause;
  guard.nuit = nuit;
  if (cause == CAUSE_PLANIFIE || cause == CAUSE_SANS_HEURE) guard.planifies++;
  ecrireGuard();
  Serial.printf("Redemarrage volontaire, cause %u\n", cause);
  // Arrêt complet de la radio avant le redémarrage : elle repart de zéro.
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(200);
  ESP.restart();   // réinitialise toute la mémoire de travail
}

void santeTick(bool heureConnue, int minuteDuJour, bool nuit) {
  static bool stable = false;
  static uint32_t dernierReleve = 0, critiqueDepuis = 0;
  uint32_t now = millis();

  if (!stable && now > STABLE_MS) {
    stable = true;
    guard.crashes = 0;   // démarrage réussi : compteur de plantages remis à zéro
    ecrireGuard();
  }

  // Relevé mémoire une fois par seconde
  if (now - dernierReleve >= 1000) {
    dernierReleve = now;
    uint32_t tas = ESP.getFreeHeap(), bloc = ESP.getMaxFreeBlockSize();
    minTas = min(minTas, tas);
    minBloc = min(minBloc, bloc);

    // Filet de sécurité : mémoire morcelée pendant plus d'une minute
    if (bloc < BLOC_CRITIQUE) {
      if (!critiqueDepuis) critiqueDepuis = now ? now : 1;
      else if (now - critiqueDepuis > CRITIQUE_MS) santeRedemarrer(CAUSE_URGENCE, nuit);
    } else {
      critiqueDepuis = 0;
    }
  }

  // Redémarrage préventif quotidien
  if (now > DUREE_MINI_MS && heureConnue && minuteDuJour == MINUTE_REDEMARRAGE) {
    santeRedemarrer(CAUSE_PLANIFIE, nuit);
  }
  if (!heureConnue && now > SANS_HEURE_MS) {
    santeRedemarrer(CAUSE_SANS_HEURE, nuit);
  }
}

bool santeNuitAuDemarrage() { return nuitAuDemarrage; }
const char* santeCauseDemarrage() { return causeTexte; }
uint32_t santeMinTasLibre() { return minTas == UINT32_MAX ? ESP.getFreeHeap() : minTas; }
uint32_t santeMinBlocLibre() { return minBloc == UINT32_MAX ? ESP.getMaxFreeBlockSize() : minBloc; }
uint32_t santeRedemarragesPlanifies() { return guard.planifies; }
