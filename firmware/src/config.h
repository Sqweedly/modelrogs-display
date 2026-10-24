#pragma once
#include <Arduino.h>

// =====================================================
// Configuration persistante (EEPROM émulée en flash)
// =====================================================

// Version du firmware : injectée à la compilation depuis le tag Git
// (tools/version.py). « dev » pour une compilation locale.
#ifndef FW_VERSION
#define FW_VERSION      "dev"
#endif
#define EEPROM_SIZE     1024

// Identité du firmware dans l'EEPROM (zone que l'OTA ne touche pas).
//  - PRODUCT_MAGIC identifie le firmware MODELROGS Display : NE JAMAIS LE CHANGER.
//    Même identité = même firmware : la configuration validée est conservée
//    d'une version à l'autre.
//  - CONFIG_VERSION décrit la structure ci-dessous : toute évolution de la
//    structure l'incrémente ET ajoute une migration dans config.cpp.
//    Pour ajouter un réglage sans migration, prendre de la place dans `reserve`.
#define PRODUCT_MAGIC   0x5044524DUL   // "MRDP"
#define CONFIG_VERSION  4
#define LEGACY_MAGIC    0x4D524447UL   // "MRDG" : firmware Discord (v1), Display 2.0/2.1 (v2, v3)

// Une page = un module de la passerelle (même identifiant que /modules).
enum PageId : uint8_t {
  PG_HEURE = 0,
  PG_METEO,
  PG_DISCORD,
  PG_SUP,
  PG_BTC,       // page « Crypto » (identifiant « btc » conservé)
  PG_PLEX,
  PG_COUNT
};

extern const char* const PAGE_NAMES[PG_COUNT];   // libellés affichés
extern const char* const PAGE_IDS[PG_COUNT];     // identifiants des modules

struct Config {
  uint32_t magic;
  uint16_t version;

  char wifiSsid[33];
  char wifiPass[65];
  char apSsid[33];

  char gateway[96];     // ex. http://192.168.1.44:8080
  char ville[48];       // utilisée par l'heure et la météo

  uint8_t  pages;       // bit n = page n affichée
  uint8_t  pageSec;     // durée d'affichage d'une page (s)

  uint8_t  nightOn;
  uint16_t nightStart;  // minutes depuis minuit
  uint16_t nightEnd;
  uint8_t  brightDay;   // 5..100 %
  uint8_t  brightNight; // 0..100 % (0 = écran éteint)

  // Réglages de chaque module, sous forme de requête « cle=valeur&... »
  // décrite par le manifeste de la passerelle (ex. "ids=3,7,12").
  char pHeure[24];
  char pMeteo[24];
  char pDiscord[40];
  char pSup[300];
  char pBtc[24];         // réglages Crypto jusqu'en 2.5 (repris dans pCrypto)

  uint8_t validated;    // 1 = configuration validée dans l'interface (bouton Enregistrer)

  // --- Ajouts 2.6, pris dans l'ancienne réserve (63 octets) : même structure,
  //     pas de migration. ext26 = 1 une fois ces champs initialisés.
  char    pCrypto[52];  // « ids=bitcoin,ethereum,ripple&devise=eur »
  char    pPlex[4];     // la page Plex n'a pas de réglage
  uint8_t ext26;
  uint8_t reserve[6];   // place libre pour de futurs réglages (initialisée à 0)
};

extern Config cfg;

void loadConfig();
void saveConfig();
bool pageEnabled(uint8_t page);

// Réglages d'un module : tampon et taille.
char*  modParams(uint8_t page);
size_t modParamsSize(uint8_t page);
// Valeur d'une clé dans les réglages d'un module ("" si absente).
String modParam(uint8_t page, const char* key);
