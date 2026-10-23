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
  b.brightNight = 10; strcpy(b.pSup, "ids=3,7,12"); strcpy(b.pDiscord, "salon=42"); strcpy(b.pBtc, "devise=usd");
  ecrire(b); loadConfig();
  CHECK(!cfg.validated, "2.1 -> a valider (jamais valide)");
  CHECK(!strcmp(cfg.ville, "Lyon") && cfg.pages == 0x1B && cfg.pageSec == 15, "2.1 -> pages, duree, ville conservees");
  CHECK(!strcmp(cfg.pSup, "ids=3,7,12") && !strcmp(cfg.pDiscord, "salon=42") && !strcmp(cfg.pBtc, "devise=usd"), "2.1 -> reglages des modules conserves");
  CHECK(!strcmp(cfg.apSsid, "MODELROGS Display") && cfg.brightDay == 60, "2.1 -> AP renomme, luminosite plafonnee");

  // 3. Validation puis nouvelle version du MEME firmware
  cfg.validated = 1; strcpy(cfg.apSsid, "Mon afficheur"); cfg.brightDay = 80; saveConfig();
  loadConfig();
  CHECK(cfg.validated == 1, "meme firmware -> validation conservee");
  CHECK(!strcmp(cfg.apSsid, "Mon afficheur") && cfg.brightDay == 80, "meme firmware -> reglages personnels conserves");
  CHECK(!strcmp(cfg.pSup, "ids=3,7,12"), "meme firmware -> services conserves");

  // 4. Firmware inconnu / EEPROM vierge
  memset(EEPROM.data, 0xFF, sizeof(EEPROM.data)); loadConfig();
  CHECK(!cfg.validated && !strcmp(cfg.apSsid, "MODELROGS Display") && cfg.wifiSsid[0] == 0, "vierge -> valeurs par defaut, a valider");
  printf("\ntaille Config = %u octets / %d\n", (unsigned)sizeof(Config), EEPROM_SIZE);
  return fails;
}
