#pragma once
#include <Arduino.h>

// =====================================================
// Données reçues de la passerelle (gardées en RAM entre deux appels)
// =====================================================

#define MAX_DISC_NAMES  8
#define MAX_SUP_ITEMS   12
#define NAME_BYTES      44   // 14 caractères UTF-8 + « … »

struct TimeState {
  bool     valid = false;
  uint32_t epoch = 0;        // UTC au moment de la synchro
  int32_t  offset = 0;       // décalage local (s)
  uint32_t atMs = 0;         // millis() au moment de la synchro
  uint32_t lastTry = 0;
  char jour[16] = "";
  char date[32] = "";
  char lever[6] = "";
  char coucher[6] = "";
};

struct DiscordState {
  bool     ok = false;       // dernière réponse exploitable
  bool     stale = false;
  bool     evKnown = false;
  uint32_t ev = 0;           // compteur d'arrivées (passerelle)
  uint32_t version = 0;      // change quand la liste affichée change
  uint32_t lastPoll = 0;
  uint8_t  n = 0;
  uint8_t  count = 0;        // noms reçus (<= MAX_DISC_NAMES)
  char     err[48] = "";
  char     canal[NAME_BYTES] = "";
  char     names[MAX_DISC_NAMES][NAME_BYTES];
  bool     isNew[MAX_DISC_NAMES];
};

enum SupEtat : uint8_t { SUP_OK = 0, SUP_INSTABLE = 1, SUP_PANNE = 2, SUP_MAINT = 3 };

struct SupState {
  bool     ok = false;
  bool     stale = false;
  bool     resume = false;   // > 12 services : compteur + problèmes
  uint32_t version = 0;
  uint32_t lastPoll = 0;
  uint8_t  tot = 0, okn = 0, pa = 0, in = 0, ma = 0;
  uint8_t  count = 0;
  uint8_t  extra = 0;
  char     err[48] = "";
  char     names[MAX_SUP_ITEMS][NAME_BYTES];
  uint8_t  st[MAX_SUP_ITEMS];
  bool     alert = false;
  char     alName[NAME_BYTES] = "";
  uint16_t alMin = 0;
  uint8_t  alCount = 0;
};

// Modules annoncés par la passerelle (/modules)
struct ModulesState {
  bool     known = false;   // manifeste lu au moins une fois
  uint8_t  present = 0;     // bit n : module de la page n activé sur la passerelle
  uint8_t  ok = 0;          // bit n : module activé ET configuré
  uint32_t lastFetch = 0;
  char     version[12] = "";
};

extern TimeState    gTime;
extern DiscordState gDisc;
extern SupState     gSup;
extern ModulesState gMods;

void   fetchModules();
bool   moduleUsable(uint8_t page);   // page utilisable d'après la passerelle
bool   discordConfigured();          // salon vocal choisi
String modulePath(uint8_t page);     // "/<module>?<réglages>"

void syncTime();
bool pollDiscord();          // true si quelqu'un vient d'arriver
void pollSup();

bool     timeValid();
uint32_t localEpoch();       // secondes locales depuis 1970
int      localMinuteOfDay(); // -1 si l'heure est inconnue
