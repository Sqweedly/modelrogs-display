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
#define CONFIG_VERSION  7
#define LEGACY_MAGIC    0x4D524447UL   // "MRDG" : firmware Discord (v1), Display 2.0/2.1 (v2, v3)

// Une page = un module de la passerelle (même identifiant que /modules).
enum PageId : uint8_t {
  PG_HEURE = 0,
  PG_METEO,
  PG_DISCORD,
  PG_SUP,
  PG_BTC,       // page « Crypto » (identifiant « btc » conservé)
  PG_PLEX,
  PG_ACTIONS,   // 2.9 : pages de cours (même rendu que Crypto)
  PG_DEVISES,
  PG_COMMO,
  PG_INDICES,   // 2.10 : indices boursiers (même rendu)
  PG_CUSTOM,    // 2.11 : 3 valeurs de toutes catégories (même rendu)
  PG_COUNT
};

extern const char* const PAGE_NAMES[PG_COUNT];   // libellés affichés
extern const char* const PAGE_IDS[PG_COUNT];     // identifiants des modules
// Ordre d'affichage (rotation et interface) : les pages de cours se suivent.
extern const uint8_t ROTATION[PG_COUNT];
// Pages de cours : Crypto, Actions, Indices, Devises, Commodities, Custom
bool pageCours(uint8_t page);

struct Config {
  uint32_t magic;
  uint16_t version;

  char wifiSsid[33];
  char wifiPass[65];
  char apSsid[33];

  char gateway[96];     // ex. http://192.168.1.44:8080
  char ville[48];       // utilisée par l'heure et la météo

  uint16_t pages;       // bit n = page n affichée
  uint8_t  pageSec;     // durée d'affichage d'une page (s)

  uint8_t  nightOn;
  uint16_t nightStart;  // minutes depuis minuit
  uint16_t nightEnd;
  uint8_t  brightDay;   // 5..100 %
  uint8_t  brightNight; // 0..100 % (0 = écran éteint)

  uint8_t validated;    // 1 = configuration validée dans l'interface (bouton Enregistrer)

  // Réglages de chaque module, sous forme de requête « cle=valeur&... »
  // décrite par le manifeste de la passerelle (ex. "s1=AAPL&s2=MC.PA&m=BNP.PA").
  char pHeure[24];
  char pMeteo[24];
  char pDiscord[40];
  char pSup[8];         // la Supervision n'a plus de réglage (2.8.1)
  char pPlex[4];        // la page Plex n'a pas de réglage
  char pCrypto[72];     // pages de cours : 3 listes (s1, s2, s3) + saisie (m)
  char pActions[72];
  char pDevises[72];
  char pCommo[72];
  char pIndices[56];    // 2.10 : pris sur `reserve` (structure 6, même taille que la 5)

  uint8_t reserve[8];   // place libre pour de futurs réglages (initialisée à 0)

  // 2.11 (structure 7) : ajouté à la fin, les structures 5 et 6 en sont le début
  char pCustom[96];     // valeurs préfixées par catégorie : « s1=c%3Abitcoin&s2=a%3ATSLA… »
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
