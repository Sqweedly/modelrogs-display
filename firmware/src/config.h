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
#define CONFIG_VERSION  8
#define LEGACY_MAGIC    0x4D524447UL   // "MRDG" : firmware Discord (v1), Display 2.0/2.1 (v2, v3)

// Une page = un module de la passerelle (même identifiant que /modules).
enum PageId : uint8_t {
  PG_HEURE = 0,
  PG_METEO,
  PG_DISCORD,
  PG_SUP,
  PG_BTC,       // page « Crypto » (identifiant « btc » conservé)
  PG_PLEX,
  PG_ACTIONS,   // 2.9 : pages de cours (même rendu que Crypto) ; « Actions Monde » (3.5.0)
  PG_EUROPE,    // 3.5.0 : « Actions Europe » (place de l'ancienne page Devises, même bit)
  PG_COMMO,
  PG_INDICES,   // 2.10 : indices boursiers (même rendu)
  PG_CUSTOM,    // 2.11 : 3 valeurs de toutes catégories (même rendu)
  PG_PHOTOS,    // 2.12 : photos Immich (module « immich »)
  PG_TESLA,     // 2.15 : batterie d'une Tesla (module « tesla », TeslaMate) ; sans réglage
  PG_COUNT
};

extern const char* const PAGE_NAMES[PG_COUNT];   // libellés affichés
extern const char* const PAGE_IDS[PG_COUNT];     // identifiants des modules
// Ordre d'affichage (rotation et interface) : les pages de cours se suivent.
extern const uint8_t ROTATION[PG_COUNT];
// Pages de cours : Crypto, Actions Monde, Actions Europe, Indices, Matières premières, Personnalisée
bool pageCours(uint8_t page);
// Catégories des marchés (3.5.0) : pages de cours précédées d'une mosaïque
// (toutes sauf Personnalisée)
bool pageMarche(uint8_t page);

struct Config {
  uint32_t magic;
  uint16_t version;

  char wifiSsid[33];
  char wifiPass[65];
  char apSsid[33];

  char gateway[96];     // ex. http://192.168.1.44:8080
  char ville[48];       // inutilisée depuis la 3.0.0 (ville réglée sur la passerelle : VILLE) ; gardée, structure inchangée

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
  char pEurope[72];     // 3.5.0 : Actions Europe (ancien pDevises, repris dans pCustom à la mise à jour)
  char pCommo[72];
  char pIndices[56];    // 2.10 : pris sur `reserve` (structure 6, même taille que la 5)

  uint8_t reserve[8];   // place libre pour de futurs réglages (initialisée à 0)
                        // reserve[0] : pages apparues sans changement de structure, déjà
                        // activées une fois (bit RES_PAGE_TESLA, 2.15)
                        // reserve[2], reserve[3] : pages 16 à 31 (3.3.0, voir pagesMask)
                        // reserve[4] : repères de reprise (3.5.0 : RES_MARCHES)
                        // reserve[5], reserve[6] : seuils des marchés 24 h et 7 j, en demi-points
                        //   de pour cent (3.5.0, voir seuil24 / seuil7)
                        // reserve[1] : personnages masqués (3.1.0) ; bit à 0 = affiché,
                        // donc activés d'office à la mise à jour (voir persoActif)

  // 2.11 (structure 7) : ajouté à la fin, les structures 5 et 6 en sont le début
  char pCustom[96];     // valeurs préfixées par catégorie : « s1=c%3Abitcoin&s2=a%3ATSLA… »

  // 2.12 (structure 8) : ajouté à la fin, les structures 5 à 7 en sont le début
  char pPhotos[64];     // source et durée : « s=a%3A<id d'album>&duree=30 »
};

extern Config cfg;

void loadConfig();
void saveConfig();
// Pages actives (3.3.0 : jusqu'à 32 pages, voir pagesMask dans config.cpp)
uint32_t pagesMask();
void setPagesMask(uint32_t m);
bool pageEnabled(uint8_t page);
void setPageEnabled(uint8_t page, bool actif);
// Personnage du domaine (3.1.0) : Heure (Nimbus et Pousse), Tesla (Volta),
// Supervision (Bip). Autres pages : false.
bool persoActif(uint8_t page);
void persoRegler(uint8_t page, bool actif);

// Seuils des marchés (3.5.0), en demi-points de pour cent (6 = 3 %) : une
// valeur de la mosaïque au-delà de seuil24 a une page à 3 valeurs, au-delà
// de seuil7 (variation sur 7 jours) une page sur 30 jours.
uint8_t seuil24();
uint8_t seuil7();
void reglerSeuils(uint8_t demi24, uint8_t demi7);
String texteSeuil(uint8_t demi);   // 6 -> "3", 7 -> "3.5"

// Réglages d'un module : tampon et taille.
char*  modParams(uint8_t page);
size_t modParamsSize(uint8_t page);
// Valeur d'une clé dans les réglages d'un module ("" si absente).
String modParam(uint8_t page, const char* key);
