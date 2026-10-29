// Lancement : ./apercu.sh (compile et lance aussi ce test)
// Test des règles de conservation de la configuration entre deux flashs.
#include "config.h"
#include <EEPROM.h>
uint32_t g_millis = 0;
static int fails = 0;
#define CHECK(c, m) do { bool ok = (c); printf("%s  %s\n", ok ? "OK   " : "ÉCHEC", m); fails += !ok; } while (0)
template <class T> static void ecrire(const T& v) { memset(EEPROM.data, 0xFF, sizeof(EEPROM.data)); memcpy(EEPROM.data, &v, sizeof(T)); }
struct V1 { uint32_t magic; uint16_t version; char wifiSsid[64]; char wifiPassword[64]; char apSsid[33]; char apPassword[65]; char discordUrl[160]; char discordChannel[64]; uint8_t b; };
struct V3 { uint32_t magic; uint16_t version; char wifiSsid[33]; char wifiPass[65]; char apSsid[33]; char gateway[96]; char ville[48];
  uint8_t pages, pageSec, nightOn; uint16_t nightStart, nightEnd; uint8_t brightDay, brightNight; char pHeure[24], pMeteo[24], pDiscord[40], pSup[300], pBtc[24]; };
// Structure 4 : Display 2.2 à 2.8
struct V4 { uint32_t magic; uint16_t version; char wifiSsid[33]; char wifiPass[65]; char apSsid[33]; char gateway[96]; char ville[48];
  uint8_t pages, pageSec, nightOn; uint16_t nightStart, nightEnd; uint8_t brightDay, brightNight; char pHeure[24], pMeteo[24], pDiscord[40], pSup[300], pBtc[24];
  uint8_t validated; char pCrypto[52]; char pPlex[4]; uint8_t ext26; uint8_t reserve[6]; };
static const uint16_t NOUVELLES = (1 << PG_ACTIONS) | (1 << PG_DEVISES) | (1 << PG_COMMO) | (1 << PG_INDICES);

int main() {
  // 1. Ancien firmware Discord (firmware différent)
  V1 a{}; a.magic = LEGACY_MAGIC; a.version = 1; strcpy(a.wifiSsid, "Maison"); strcpy(a.wifiPassword, "secret");
  strcpy(a.apSsid, "MODELROGS Discord"); strcpy(a.discordUrl, "http://192.168.1.44:5090/snapshot"); a.b = 100;
  ecrire(a); loadConfig();
  CHECK(!cfg.validated, "Discord -> configuration a valider");
  CHECK(!strcmp(cfg.wifiSsid, "Maison") && !strcmp(cfg.wifiPass, "secret"), "Discord -> Wi-Fi pre-rempli");
  CHECK(!strcmp(cfg.gateway, "http://192.168.1.44:8080"), "Discord -> passerelle deduite");
  CHECK(!strcmp(cfg.apSsid, "MODELROGS Display"), "Discord -> point d'acces renomme");
  CHECK(cfg.brightDay == 60, "Discord -> luminosite 60 %");

  // 2. Display 2.1.x (MRDG v3) : repris mais jamais valide
  V3 b{}; b.magic = LEGACY_MAGIC; b.version = 3; strcpy(b.wifiSsid, "Maison"); strcpy(b.apSsid, "MODELROGS Discord");
  strcpy(b.gateway, "http://10.0.0.5:8080"); strcpy(b.ville, "Lyon"); b.pages = 0x1B; b.pageSec = 15; b.brightDay = 100;
  b.brightNight = 10; strcpy(b.pSup, "ids=3,7,12"); strcpy(b.pDiscord, "salon=42"); strcpy(b.pBtc, "ids=ethereum,ripple");
  ecrire(b); loadConfig();
  CHECK(!cfg.validated, "2.1 -> a valider (jamais valide)");
  CHECK(!strcmp(cfg.ville, "Lyon") && cfg.pages == (0x1B | 1 << PG_PLEX | NOUVELLES) && cfg.pageSec == 15,
        "2.1 -> pages, duree, ville conservees (+ Plex, + pages de cours)");
  CHECK(!strcmp(cfg.pCrypto, "s1=ethereum&s2=ripple"), "2.1 -> cryptos reprises dans les listes, devise retiree");
  CHECK(!strcmp(cfg.pDiscord, "salon=42") && cfg.pSup[0] == 0, "2.1 -> salon conserve, choix des services retire");
  CHECK(!strcmp(cfg.apSsid, "MODELROGS Display") && cfg.brightDay == 60, "2.1 -> AP renomme, luminosite plafonnee");

  // 3. Display 2.8 (structure 4) validee : tout est repris, validation comprise
  V4 c{}; c.magic = PRODUCT_MAGIC; c.version = 4; strcpy(c.wifiSsid, "Maison"); strcpy(c.wifiPass, "secret");
  strcpy(c.apSsid, "Mon afficheur"); strcpy(c.gateway, "http://10.0.0.5:8080"); strcpy(c.ville, "Paris");
  c.pages = 0x2F; c.pageSec = 12; c.nightOn = 1; c.nightStart = 1380; c.nightEnd = 420; c.brightDay = 80; c.brightNight = 5;
  strcpy(c.pDiscord, "salon=42"); strcpy(c.pBtc, "devise=eur"); c.validated = 1;
  strcpy(c.pCrypto, "ids=bitcoin,ethereum,ripple&devise=eur"); c.ext26 = 1;
  ecrire(c); loadConfig();
  CHECK(cfg.validated == 1, "2.8 -> 2.9 : validation conservee");
  CHECK(!strcmp(cfg.wifiSsid, "Maison") && !strcmp(cfg.wifiPass, "secret") && !strcmp(cfg.apSsid, "Mon afficheur")
        && !strcmp(cfg.gateway, "http://10.0.0.5:8080") && !strcmp(cfg.ville, "Paris"), "2.8 -> 2.9 : Wi-Fi, AP, passerelle, ville");
  CHECK(cfg.pageSec == 12 && cfg.nightOn && cfg.nightStart == 1380 && cfg.nightEnd == 420 && cfg.brightDay == 80
        && cfg.brightNight == 5, "2.8 -> 2.9 : duree, nuit, luminosites");
  CHECK(cfg.pages == (0x2F | NOUVELLES) && !strcmp(cfg.pDiscord, "salon=42"), "2.8 -> 2.9 : pages (+ pages de cours), salon");
  CHECK(!strcmp(cfg.pCrypto, "s1=bitcoin&s2=ethereum&s3=ripple"), "2.8 -> 2.9 : 3 cryptos reprises dans les 3 listes");
  CHECK(!cfg.pActions[0] && !cfg.pDevises[0] && !cfg.pCommo[0], "2.8 -> 2.9 : pages de cours vides (valeurs du compose)");

  // 3b. Configuration 2.5 (structure 4 sans les ajouts 2.6) validee
  c.ext26 = 0; memset(c.pCrypto, 0, sizeof(c.pCrypto)); c.pages = 0x1F; strcpy(c.pBtc, "ids=solana&devise=usd");
  ecrire(c); loadConfig();
  CHECK(cfg.validated == 1 && !strcmp(cfg.pCrypto, "s1=solana"), "2.5 -> 2.9 : validation, crypto reprise");
  CHECK(cfg.pages == (0x1F | 1 << PG_PLEX | NOUVELLES), "2.5 -> 2.9 : Plex et pages de cours ajoutees");

  // 3c. Display 2.9.0 / 2.9.1 (structure 5) validee : tout est repris, page Indices ajoutee
  {
    Config v5{};
    memcpy(&v5, &cfg, sizeof(cfg));
    v5.magic = PRODUCT_MAGIC; v5.version = 5; v5.validated = 1; v5.pages = 0x01FF & ~(1 << PG_INDICES) & ~(1 << PG_METEO);
    strcpy(v5.pActions, "s1=AAPL"); strcpy(v5.pCrypto, "s1=solana"); memset(v5.pIndices, 0, sizeof(v5.pIndices));
    memset(v5.reserve, 0, sizeof(v5.reserve));
    ecrire(v5); loadConfig();
    CHECK(cfg.validated == 1 && !strcmp(cfg.pActions, "s1=AAPL") && !strcmp(cfg.pCrypto, "s1=solana")
          && !strcmp(cfg.wifiSsid, "Maison"), "2.9.1 -> 2.10 : validation et reglages conserves");
    CHECK((cfg.pages & 1 << PG_INDICES) && !(cfg.pages & 1 << PG_METEO) && !cfg.pIndices[0],
          "2.9.1 -> 2.10 : page Indices activee (valeurs du compose), pages decochees conservees");
  }

  // 4. Nouvelle version du MEME firmware (structure actuelle) : tout est conserve
  strcpy(cfg.pActions, "s1=AAPL&s2=MC.PA&m=BNP.PA%2CORA.PA"); strcpy(cfg.pCommo, "s1=GC%3DF");
  strcpy(cfg.pIndices, "s1=FCHI&s2=GSPC&m=%5EIBEX");
  cfg.pages &= ~(1 << PG_DEVISES); saveConfig();
  loadConfig();
  CHECK(cfg.validated == 1 && !strcmp(cfg.pActions, "s1=AAPL&s2=MC.PA&m=BNP.PA%2CORA.PA") && !strcmp(cfg.pCommo, "s1=GC%3DF")
        && !(cfg.pages & 1 << PG_DEVISES) && (cfg.pages & 1 << PG_COMMO), "meme firmware -> reglages des pages de cours conserves");
  CHECK(modParam(PG_ACTIONS, "m") == "BNP.PA,ORA.PA" && modParam(PG_COMMO, "s1") == "GC=F", "reglages relus et decodes");
  CHECK(modParam(PG_INDICES, "s2") == "GSPC" && modParam(PG_INDICES, "m") == "^IBEX", "indices conserves et decodes");

  // 5. Firmware inconnu / EEPROM vierge
  memset(EEPROM.data, 0xFF, sizeof(EEPROM.data)); loadConfig();
  CHECK(!cfg.validated && !strcmp(cfg.apSsid, "MODELROGS Display") && cfg.wifiSsid[0] == 0, "vierge -> valeurs par defaut, a valider");
  CHECK(cfg.pages == (1u << PG_COUNT) - 1 && !cfg.pCrypto[0], "vierge -> toutes les pages, Crypto selon le compose");
  printf("\ntaille Config = %u octets / %d\n", (unsigned)sizeof(Config), EEPROM_SIZE);
  return fails;
}
