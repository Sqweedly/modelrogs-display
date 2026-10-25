#pragma once
#include <Arduino.h>

// =====================================================
// Données reçues de la passerelle (gardées en RAM entre deux appels)
// =====================================================

#define MAX_DISC_NAMES  8
#define MAX_SUP_ITEMS   12
#define NAME_BYTES      44   // 14 caractères UTF-8 + « … »
#define IMG_TOKEN       13   // jeton d'image de la passerelle (12 caractères)
#define MAX_PLEX        3

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
  char     serveur[NAME_BYTES] = "";
  char     icone[IMG_TOKEN] = "";                // icône du serveur
  char     names[MAX_DISC_NAMES][NAME_BYTES];
  bool     isNew[MAX_DISC_NAMES];
  char     depuis[MAX_DISC_NAMES][16];           // « 1 h 12 », « 48 min », « » si inconnu
  uint8_t  etat[MAX_DISC_NAMES];                 // DISC_MUET | DISC_SOURD | DISC_LIVE | DISC_CAMERA
  char     avatar[MAX_DISC_NAMES][IMG_TOKEN];
};

enum : uint8_t { DISC_MUET = 1, DISC_SOURD = 2, DISC_LIVE = 4, DISC_CAMERA = 8 };

// Lectures Plex en cours (page visible seulement pendant une lecture)
struct PlexState {
  bool     ok = false;
  bool     stale = false;
  bool     evKnown = false;
  uint32_t ev = 0;           // compteur de nouvelles lectures (passerelle)
  uint32_t version = 0;      // change quand le contenu (titres, affiche...) change
  uint32_t progres = 0;      // change quand seule la progression change
  uint32_t lastPoll = 0;
  uint8_t  n = 0;            // lectures en cours
  uint8_t  tc = 0;           // dont transcodages
  uint8_t  count = 0;        // lignes reçues (<= MAX_PLEX)
  struct Ligne {
    char     titre[72];
    char     sous[72];
    char     user[NAME_BYTES];
    char     pos[10];
    char     reste[18];
    char     affiche[IMG_TOKEN];
    char     biblio[24];     // bibliothèque Plex (Films, Séries…)
    char     ep[16];         // « S1 · E3 » pour un épisode
    uint16_t pg;             // progression 0..1000
    uint8_t  mode;           // 0 lecture directe, 1 flux direct, 2 transcodage
    bool     tc, pause;
  } l[MAX_PLEX];
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
  uint32_t mins[MAX_SUP_ITEMS];             // minutes dans l'état actuel (services en défaut)
  bool     borne[MAX_SUP_ITEMS];            // durée minimale : début hors de l'historique Kuma
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
extern PlexState    gPlex;
extern ModulesState gMods;

void   fetchModules();
bool   moduleUsable(uint8_t page);   // page utilisable d'après la passerelle
bool   discordConfigured();          // salon vocal choisi
String modulePath(uint8_t page);     // "/<module>?<réglages>"

void syncTime();
bool pollDiscord();          // true si quelqu'un vient d'arriver
void pollSup();
bool pollPlex();             // true si une lecture vient de commencer

bool     timeValid();
uint32_t localEpoch();       // secondes locales depuis 1970
int      localMinuteOfDay(); // -1 si l'heure est inconnue
